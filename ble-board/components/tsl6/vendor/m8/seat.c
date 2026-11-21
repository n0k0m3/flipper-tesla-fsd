/* Seat control over CAN 0x4F3 (stock 0x16EBE..0x17828).
   The module drives two seats (0 and 1) with a bit mask in a 3-byte CAN
   frame 0x4F3. Each seat has 4 axes and each axis has two bits (one per
   direction). There are three users of the mask:
   - memory recall: drives the 4 axes of a seat to a stored position,
   - pulses: sends one mask for n x 100 ms, then 0,
   - a 6-step cycle (every 8 s) that uses the bits of data[2].
   The positions come from the CAN cache slots 16 (seat 0) and 17 (seat 1).
   Memory slots 0-4 are seat 0, 5-9 are seat 1. They live in the config
   block at offset (slot + 2) * 8, marked with 'Z'.
   "Seat" is the most likely meaning (2 x 4 axes, 5 memory slots each),
   but the frame 0x4F3 is not confirmed on a car. */
#include "tsl6.h"
#include "m8.h"

#define SEAT_CAN_ID  0x4f3
#define SEAT_MARK    'Z'

/* Positions of the 4 axes in a 12-byte CAN cache entry. */
static uint32_t pos0(const uint32_t *w) { return (w[1] & 0x7f) << 5 | w[0] >> 27; }
static uint32_t pos1(const uint32_t *w) { return (uint16_t)((w[1] >> 8) & 0xfff); }
static uint32_t pos2(const uint32_t *w) { return w[1] >> 20; }
static uint32_t pos3(const uint32_t *w) { return (uint16_t)(w[2] & 0xfff); }

/* Stock 0x16EBE. Sends g_seat.out in a 3-byte frame 0x4F3. */
uint32_t seat_can_send(void)
{
    can_frame_t f;   /* data[4..7] stay not set, as in stock */
    uint32_t v = g_seat.out;

    f.id = SEAT_CAN_ID;
    f.dlc = 3;
    f.data[0] = (uint8_t)v;
    f.data[1] = (uint8_t)(v >> 8);
    f.data[2] = (uint8_t)(v >> 16);
    f.data[3] = (uint8_t)(v >> 24);
    f.flag = 0;
    return can_tx(&f);
}

/* Stock 0x16F20. One axis of a recall. While *act is set, it ORs the bit
   for the direction into *out. It stops (clears *act) when the position is
   within +-5 of the target, or when the position passed the target.
   up != 0 means the axis moves up (bit_up), else down (bit_down).
   Stock returns act in a0. */
uint32_t seat_axis_step(uint8_t *act, uint32_t up, uint32_t *out, uint32_t bit_up,
                        uint32_t bit_down, uint32_t target, uint32_t cur)
{
    if (*act == 0)
        return (uint32_t)act;
    if (target - cur + 5 <= 10) {
        *act = 0;
        return (uint32_t)act;
    }
    if (up != 0) {
        *act = cur < target;
        if (cur < target)
            *out |= bit_up;
    } else {
        *act = cur >= target;
        if (cur >= target)
            *out |= bit_down;
    }
    return (uint32_t)act;
}

/* Stock 0x16F62. App timer (50 ms) of the memory recall. It steps all axes
   of each active seat and sends the mask. After 500 calls (25 s) it gives
   up and sends 0. It stops itself when no seat is active. */
uint32_t seat_recall_tick(void)
{
    uint32_t out = 0;
    uint32_t w[3];
    seat_recall_t *s;
    uint32_t r;

    s = &g_seat.seat[0];
    if (s->on) {
        frame_slot_copy(1, 16, w);
        seat_axis_step(&s->act[0], s->dir[0], &out, 4, 8, s->target.p0, pos0(w));
        seat_axis_step(&s->act[1], s->dir[1], &out, 0x80, 0x40, s->target.p1, pos1(w));
        seat_axis_step(&s->act[2], s->dir[2], &out, 0x4000, 0x8000, s->target.p2, pos2(w));
        seat_axis_step(&s->act[3], s->dir[3], &out, 0x400, 0x800, s->target.p3, pos3(w));
        if (!s->act[0] && !s->act[1] && !s->act[2] && !s->act[3])
            s->on = 0;
    }
    s = &g_seat.seat[1];
    if (s->on) {
        frame_slot_copy(1, 17, w);
        seat_axis_step(&s->act[0], s->dir[0], &out, 1, 2, s->target.p0, pos0(w));
        seat_axis_step(&s->act[1], s->dir[1], &out, 0x20, 0x10, s->target.p1, pos1(w));
        seat_axis_step(&s->act[2], s->dir[2], &out, 0x1000, 0x2000, s->target.p2, pos2(w));
        seat_axis_step(&s->act[3], s->dir[3], &out, 0x100, 0x200, s->target.p3, pos3(w));
        if (!s->act[0] && !s->act[1] && !s->act[2] && !s->act[3])
            s->on = 0;
    }
    g_seat.recall_ticks--;
    if (g_seat.recall_ticks == 0) {
        out = 0;
        g_seat.seat[0].on = 0;
        g_seat.seat[1].on = 0;
    }
    g_seat.out = out;
    r = seat_can_send();
    if (g_seat.seat[0].on == 0 && g_seat.seat[1].on == 0)
        return app_timer_stop((void *)seat_recall_tick);
    return r;
}

/* Stock 0x171EE. App timer of a pulse. While a recall runs it waits.
   It sends pulse_mask every 100 ms, pulse_limit times. Then it sends 0
   with 20 ms gaps (pulse_limit = 3). */
uint32_t seat_pulse_tick(void)
{
    uint32_t r;

    if (g_seat.seat[0].on || g_seat.seat[1].on)
        return app_timer_start((void *)seat_pulse_tick, 0, 100, 0);
    g_seat.out = g_seat.pulse_mask;
    r = seat_can_send();
    if (g_seat.pulse_mask == 0) {
        if (g_seat.pulse_cnt < g_seat.pulse_limit)
            r = app_timer_start((void *)seat_pulse_tick, 0, 20, 0);
    } else if (g_seat.pulse_cnt < g_seat.pulse_limit) {
        r = app_timer_start((void *)seat_pulse_tick, 0, 100, 0);
    } else {
        g_seat.pulse_cnt = 0;
        g_seat.pulse_limit = 3;
        g_seat.pulse_mask = 0;
        r = seat_pulse_tick();
    }
    /* Odd: after the call above the count goes up two times, so the
       trailing 0 is sent one time less. */
    g_seat.pulse_cnt++;
    return r;
}

/* Stock 0x17296. Starts a pulse of mask for limit x 100 ms.
   replace = 0 adds mask to the running mask. */
uint32_t seat_pulse(uint32_t mask, uint32_t limit, uint32_t replace)
{
    g_seat.pulse_limit = (uint8_t)limit;
    g_seat.pulse_cnt = 0;
    if (replace == 0)
        mask |= g_seat.pulse_mask;
    g_seat.pulse_mask = mask;
    return seat_pulse_tick();
}

/* Stock 0x172C2. App timer (8 s) of the cycle. Step 0..5 picks a pattern
   in data[2]: cycle bit 0 uses the low nibble, bit 1 the high nibble. */
uint32_t seat_cycle_step(void)
{
    uint32_t mask = 0;
    uint32_t bits = g_seat.cycle_bits;
    uint32_t r;

    switch (g_seat.cycle_step) {
    case 0:
        if (bits & 1)
            mask = 0x110000;
        if (bits & 2)
            mask |= 0x440000;
        break;
    case 1:
        if (bits & 1)
            mask = 0x220000;
        if (bits & 2)
            mask |= 0x880000;
        break;
    case 2:
    case 4:
        if (bits & 1)
            mask = 0x210000;
        if (bits & 2)
            mask |= 0x840000;
        break;
    case 3:
    case 5:
        if (bits & 1)
            mask = 0x120000;
        if (bits & 2)
            mask |= 0x480000;
        break;
    default:
        break;
    }
    r = seat_pulse(mask, 200, 1);
    g_seat.cycle_step = (uint8_t)((g_seat.cycle_step + 1) % 6);
    return r;
}

/* Stock 0x17358. Pulse of mask for 20 s. 0x150C8 arms the "next key
   stops it" flag (0x20004189). */
uint32_t seat_pulse_long(uint32_t mask)
{
    btn_lock_set(0);
    return seat_pulse(mask, 200, 1);
}

/* Stock 0x17378. Sets the bits "on" and clears the bits "off" of the cycle.
   Beeps (2 for off, 300 for on), then starts or stops the cycle timer.
   notify != 0 queues a gauge update. */
uint32_t seat_cycle_set(uint32_t on, uint32_t off, uint32_t notify)
{
    uint32_t r;

    g_seat.cycle_bits = (uint8_t)((~off & g_seat.cycle_bits) | on);
    g_seat.cycle_step = 0;
    if (off != 0)
        buzzer_beeps(2);
    else if (on != 0)
        buzzer_beeps(300);
    if (g_seat.cycle_bits == 0) {
        app_timer_stop((void *)seat_cycle_step);
        r = seat_pulse(0, 3, 1);
    } else {
        seat_cycle_step();
        r = app_timer_start((void *)seat_cycle_step, 1, 8000, 0);
    }
    if (notify != 0)
        return status_push(0);
    return r;
}

/* Stock 0x17404. */
uint32_t seat_cycle_get(void)
{
    return g_seat.cycle_bits;
}

/* Stock 0x1740E. A short pulse (3 x 100 ms) on seat 1 axis 0 or 1:
   1 -> bit 0, -1 -> bit 1, > 1 -> bit 5, < -1 -> bit 4. */
uint32_t seat_nudge(int32_t dir)
{
    uint32_t mask;

    if (dir == 0)
        return 0;
    if (dir == 1)
        mask = 1;
    else if (dir == -1)
        mask = 2;
    else if (dir > 1)
        mask = 0x20;
    else
        mask = 0x10;
    return seat_pulse(mask, 3, 1);
}

/* Stock 0x17436. Toggles the cycle bits. */
uint32_t seat_cycle_toggle(uint32_t bits)
{
    if (g_seat.cycle_bits & bits)
        return seat_cycle_set(0, bits, 1);
    return seat_cycle_set(bits, 0, 1);
}

/* Stock 0x17450. Recalls memory slot 0..9. A slot without the 'Z' mark
   does not start a recall; then it beeps "error", unless the other seat
   still recalls (then the timer restarts). */
uint32_t seat_recall(uint32_t slot)
{
    uint32_t w[3];
    seat_recall_t *s;

    if (slot > 9)
        return slot;
    if (slot <= 4) {
        s = &g_seat.seat[0];
        tmos_memcpy(&s->target, g_cfg + (slot + 2) * 8, 8);
        if (s->target.mark != SEAT_MARK)
            goto check;
        frame_slot_copy(1, 16, w);
    } else {
        s = &g_seat.seat[1];
        tmos_memcpy(&s->target, g_cfg + (slot + 2) * 8, 8);
        if (s->target.mark != SEAT_MARK)
            goto check;
        frame_slot_copy(1, 17, w);
    }
    s->dir[0] = pos0(w) < s->target.p0;
    s->dir[1] = pos1(w) < s->target.p1;
    s->dir[2] = pos2(w) < s->target.p2;
    s->dir[3] = pos3(w) < s->target.p3;
    s->act[0] = 1;
    s->act[1] = 1;
    s->act[2] = 1;
    s->act[3] = 1;
    s->on = 1;
check:
    if (g_seat.seat[0].on == 0 && g_seat.seat[1].on == 0)
        return tune_two_low(0);
    buzzer_beeps(1);
    g_seat.recall_ticks = 500;
    return app_timer_start((void *)seat_recall_tick, 1, 50, 0);
}

/* Stock 0x1764E. Stops the recall of seat 0 (mask bit 0) and seat 1
   (mask bit 1), and sends 0 for each one stopped.
   Stock keeps mask in a 32-byte stack frame. The second CAN send then finds
   the same stack bytes as stock (they end up in the unsent data bytes 4-7
   of the CAN mailbox). The volatile copy gives the rewrite that frame size. */
uint32_t seat_recall_cancel(uint32_t mask)
{
    volatile uint32_t keep[4];

    keep[3] = mask;
    if (g_seat.seat[0].on && (keep[3] & 1)) {
        g_seat.seat[0].on = 0;
        g_seat.out = 0;
        seat_can_send();
    }
    if (g_seat.seat[1].on == 0)
        return keep[3];
    if ((keep[3] & 2) == 0)
        return 0;
    g_seat.seat[1].on = 0;
    g_seat.out = 0;
    return seat_can_send();
}

/* Stock 0x176C8. Saves the position of the seat to memory slot 0..9, with
   limits per axis, then saves the config and beeps. Returns 1 when saved.
   The "all 0" branch can not run after the limits (stock dead code). */
uint32_t seat_save(uint32_t slot)
{
    uint32_t w[3];
    seat_pos_t p;   /* p.pad stays not set, as in stock */
    uint32_t v0, v1, v2, v3;

    if (slot > 9)
        return 0;
    frame_slot_copy(1, (slot > 4) + 16, w);
    v0 = pos0(w);
    v1 = pos1(w);
    v2 = (uint8_t)pos2(w);
    v3 = (uint8_t)w[2];
    p.p0 = (uint16_t)v0;
    p.p1 = (uint16_t)v1;
    p.p2 = (uint8_t)v2;
    p.p3 = (uint8_t)v3;
    p.mark = SEAT_MARK;
    if (v0 < 10)
        p.p0 = 10;
    else if (v0 > 250)
        p.p0 = 250;
    if (v1 < 30)
        p.p1 = 30;
    else if (v1 > 470)
        p.p1 = 470;
    if (v2 < 5)
        p.p2 = 5;
    else if (v2 > 75)
        p.p2 = 75;
    if (v3 < 10)
        p.p3 = 10;
    else if (v3 > 50)
        p.p3 = 50;
    if ((p.p0 | p.p1 | (p.p2 | p.p3 << 8)) == 0) {
        app_timer_start((void *)tune_two_low, 0, 100, 0);
        return 0;
    }
    tmos_memcpy(g_cfg + (slot + 2) * 8, &p, 8);
    cfg_save();
    app_timer_start((void *)tune_up, 0, 100, 0);
    return 1;
}

/* Stock 0x177F6. Sends the mask again when it is not 0.
   Stock returns its a0 unchanged when the mask is 0. */
uint32_t seat_resend(uint32_t a0)
{
    if (g_seat.out == 0)
        return a0;
    return seat_can_send();
}

/* Stock 0x17808. */
uint32_t seat_init(void)
{
    return tmos_memset_a0(&g_seat, 0, sizeof(g_seat));
}
