/* Event rules and light records (stock 0x16310, 0x168D6..0x16EBE).
   Three 256-byte tables (set over BLE, saved in flash) map an event code to
   things to do:
   - g_evt_actions: 127 pairs {code, action}; the action runs in 0x1516E.
   - g_rfl_recs: 36 records of 7 bytes, sent over the RF link (rf_link.c)
     to a remote light unit. Record 0 is the base record.
   - g_light_recs: 36 records of 7 bytes for the local light output
     (0xC476). Record 0 is the idle record.
   Record layout (7 bytes, as two little-endian words w0/w1):
   - w0 bits 7:0 and bit 8: the event code that selects the record,
   - w0 bit 9: record on,
   - w0 bits 15:10: mode (local: 1 blink, 2 color cycle; RF: side bits,
     1 left, 2 right, 3 both),
   - bytes 2..4: R, G, B,
   - byte 5 bits 4:0: brightness (31 = full),
   - byte 6 bits 6:2: period, bit 7: hold (a side with hold set ignores
     the side updates).
   RF records go through a queue of 16. Each queued record is one RF packet
   (id 0xB562, type 0). */
#include "tsl6.h"
#include "m8.h"

#define RF_ID      0xb562
#define RF_CHAN    21
#define REC_LEN    7
#define MODE_MASK  0xffff03ffu

#define RFQ_EV_SEND  1
#define RFQ_EV_LIGHT 2

/* Stock 0x19B04. Colors of the cycle mode (bytes R, G, B): red, orange,
   yellow, green, cyan, blue, magenta. */
static const uint32_t k_cycle_colors[7] = {
    0x000000ff, 0x00007dff, 0x0000ffff, 0x0000ff00, 0x00ffff00, 0x00ff0000, 0x00ff00ff,
};

/* Stock 0x16310. Runs every rule for an event code: each action of
   g_evt_actions with that code, then each RF and local record 1..35 that
   is on and has that (9-bit) code. Stock returns the a0 of the last call
   (or code when nothing matched).
   The values sit in the same callee-saved registers as in stock (s0 index,
   s1 code, s2 end, s3/s4 table pointer addresses, s5 offset). The stock
   callees save these registers on the stack, and some of them later read
   that stack area without setting it first (unsent CAN data bytes, the
   unused tail of the flash config page). So the register values must be
   the same to give the same bytes. The empty asm statements keep gcc from
   moving the values to other registers. */
uint32_t event_fire(uint32_t code)
{
    /* port: plain C. The stock version pins s0-s5 for the differential test. */
    uint32_t r = code;
    uint8_t *e;

    for (uint32_t i = 0; i != 254; i += 2) {
        e = g_evt_actions + i;
        if (e[0] == code)
            r = button_action(e[1]);
    }
    for (uint32_t i = 1, off = REC_LEN; i != 36; i = (i + 1) & 0xff, off += REC_LEN) {
        e = g_rfl_recs + off;
        if ((((e[1] & 1u) << 8) | e[0]) == code && (e[1] & 2))
            r = light_rec_apply(0, i);
        e = g_light_recs + off;
        if (code == (((e[1] & 1u) << 8) | e[0]) && (e[1] & 2))
            r = light_rec_apply(1, i);
    }
    return r;
}
#undef KEEP

/* Stock 0x168D6. Done callback of an RF packet. It sends the next queued
   record in 2 ticks. The first ack (status 0) also ends the hello timer
   and marks the link up. */
uint32_t rfq_sent(uint32_t status)
{
    uint32_t r = tmos_start_task(g_rfq_task, RFQ_EV_SEND, 2);

    if (status == 0 && g_rf_linked == 0) {
        r = app_timer_stop((void *)rf_hello);
        g_rf_linked = 1;
    }
    return r;
}

/* Stock 0x16910. App timer: sends the hello packet (type 15, data B5 62).
   It repeats every 500 ms, 5 times at most, until an ack comes. */
uint32_t rf_hello(void)
{
    uint16_t sync = 0x62b5;
    uint32_t r;

    r = rf_send(RF_ID, RF_CHAN, 15, &sync, 2, rfq_sent);
    if (r != 0)
        return r;
    g_rf_hello_n++;
    if (g_rf_hello_n > 4)
        return r;
    return app_timer_start((void *)rf_hello, 0, 500, 0);
}

/* Stock 0x16968. TMOS event handler of the light task.
   Event 1: sends the next queued RF record.
   Event 2: one step of the active local record (g_light_idx). A record
   that is not set falls back to record 0. Blink toggles full brightness
   every period x 100 ms. Color cycle takes the next color every
   (period + 1) x 0.5 s. Then the record goes to the light output. */
uint32_t rfq_task_event(uint8_t task, uint32_t events)
{
    light_rec_t r;
    uint32_t mode, ticks, c;

    if (events & RFQ_EV_SEND) {
        if (g_rfq.count != 0 &&
            rf_send(RF_ID, RF_CHAN, 0, g_rfq.rec[g_rfq.head], REC_LEN, rfq_sent) == 0) {
            g_rfq.head = (g_rfq.head + 1) & 0xf;
            g_rfq.count = g_rfq.count - 1;
        }
        return events ^ RFQ_EV_SEND;
    }
    if ((events & RFQ_EV_LIGHT) == 0)
        return 0;
    tmos_memcpy(&r, g_light_recs + g_light_idx * REC_LEN, REC_LEN);
    if (g_light_idx != 0 && (r.b[1] & 0xfc) == 0) {
        g_light_idx = 0;
        tmos_memcpy(&r, g_light_recs, REC_LEN);
    }
    if (r.b[1] & 2) {
        mode = (r.w[0] >> 10) & 63;
        if (mode == 1) {
            if ((r.b[6] & 0x7c) == 0)
                goto out;
            g_light_blink ^= 1;
            if (g_light_blink)
                r.h[2] |= 0x1f00;
            ticks = ((r.w[1] >> 18) & 31) * 160;
        } else if (mode == 2) {
            c = k_cycle_colors[g_light_color];
            r.w[0] = (r.w[0] & 0xffff) | (c & 0xffff) << 16;
            r.b[4] = (uint8_t)(c >> 16);
            g_light_color = (uint8_t)((g_light_color + 1) % 7);
            ticks = (((r.w[1] >> 18) & 31) + 1) * 800;
        } else {
            goto out;
        }
        tmos_start_task(task, RFQ_EV_LIGHT, ticks);
    }
out:
    can_679_set(&r);
    return events ^ RFQ_EV_LIGHT;
}

/* Stock 0x16AEC. Queues one RF record (dropped when 16 are queued).
   When the queue was empty, it starts the send. */
uint32_t rfq_push(void *rec)
{
    uint8_t n = g_rfq.count;
    uint32_t r = (uint32_t)rec;

    if (g_rfq.count <= 15) {
        r = tmos_memcpy_a0(g_rfq.rec[g_rfq.tail], rec, REC_LEN);
        g_rfq.tail = (g_rfq.tail + 1) & 0xf;
        g_rfq.count = g_rfq.count + 1;
    }
    if (n == 0)
        return rfq_sent(0xff);
    return r;   /* stock returns its a0 */
}

/* Stock 0x16B68. App timer (1.5 s after rfl_green_timer(0)): sends green
   at full brightness to both sides, with hold. */
uint32_t rfl_send_green(void)
{
    light_rec_t r;

    tmos_memset(&r, 0, REC_LEN);
    r.w[0] = (r.w[0] & 0x3ff) | 0xff000c00;
    r.h[2] = (r.h[2] & 0xe000) | 0x1f00;
    r.b[6] |= 0x80;
    return rfq_push(&r);
}

/* Stock 0x16BB8. Sets the mode (side bits) and hold of an RF record, then
   queues it when arg != 0. arg 2 keeps the brightness, other values set
   full brightness. Stock returns the new w0 when arg is 0. */
uint32_t rfl_send_mode(uint32_t mode, uint32_t arg, light_rec_t *r)
{
    uint32_t w = (r->w[0] & MODE_MASK) | (mode & 63) << 10;

    r->w[0] = w;
    r->b[6] |= 0x80;
    if (arg == 0)
        return w;
    if (arg != 2)
        r->h[2] |= 0x1f00;
    return rfq_push(r);
}

/* Stock 0x16BFA. Applies record idx (0..35).
   which = 0: queues RF record idx and takes its hold bit for its sides.
   which != 0: makes local record idx active and runs a light step.
   noipa: keep one real function, as in stock (gcc would split it). */
__attribute__((noipa))
uint32_t light_rec_apply(uint32_t which, uint32_t idx)
{
    light_rec_t r;
    uint32_t sides;

    if (idx > 35)
        return which;
    if (which == 0) {
        tmos_memcpy(&r, g_rfl_recs + idx * REC_LEN, REC_LEN);
        sides = (r.w[0] >> 10) & 63;
        if (sides & 1)
            g_rfl_hold_l = (r.w[1] >> 23) & 1;
        if (sides & 2)
            g_rfl_hold_r = (r.w[1] >> 23) & 1;
        return rfq_push(&r);
    }
    g_light_idx = (uint8_t)idx;
    return tmos_start_task(g_rfq_task, RFQ_EV_LIGHT, 2);
}

/* Push the base RF record with the side bits "sides", hold off. */
static void rfl_push_sides(light_rec_t *r, uint32_t sides)
{
    r->w[0] = (r->w[0] & MODE_MASK) | sides;
    r->b[6] &= 0x7f;
    rfq_push(r);
}

/* Stock 0x16C6C. Updates the RF light from two side states (the caller
   passes data[3] bits 1:0 and 3:2 of a CAN frame; most likely the turn
   signals). Only when RF record 0 is on and a state changed.
   A side that turns on gets the base record with hold and full
   brightness (state 2 keeps the brightness). A side that turns off gets
   the plain base record back. A side with hold set (from a record of
   light_rec_apply) is left alone. Stock returns the a0 of the last call. */
uint32_t light_sides_update(uint32_t left, uint32_t right)
{
    light_rec_t r;
    uint8_t hold_l, hold_r;
    uint32_t a0, w;

    if ((g_rfl_recs[1] & 2) == 0)
        return left;
    if (g_rfl_prev_l == left && right == g_rfl_prev_r)
        return left;
    if (g_rfl_prev_l == 0)
        g_rfl_hold_l = 0;
    if (g_rfl_prev_r == 0)
        g_rfl_hold_r = 0;
    hold_l = g_rfl_hold_l;
    hold_r = g_rfl_hold_r;
    a0 = tmos_memcpy_a0(&r, g_rfl_recs, REC_LEN);
    if (left == 0) {
        if (right == 0) {
            /* Both off now. */
            w = r.w[0] & MODE_MASK;
            if (g_rfl_prev_l == 0) {
                if (g_rfl_prev_r != 0 && hold_r != 1)
                    w |= 0x800;
            } else if (g_rfl_prev_r == 0) {
                if (hold_l != 1)
                    w |= 0x400;
            } else if (hold_l != 1 && hold_r != 1) {
                w |= 0xc00;
            }
            r.w[0] = w;
            if (r.b[1] & 0xfc) {
                r.b[6] &= 0x7f;
                a0 = rfq_push(&r);
            }
        } else {
            if (hold_r == 1)
                goto save;
            if (g_rfl_prev_l != 0) {
                rfl_push_sides(&r, 0x400);
                tmos_memcpy(&r, g_rfl_recs, REC_LEN);
            }
            a0 = rfl_send_mode(2, right, &r);
        }
    } else if (right == 0) {
        if (hold_l == 1)
            goto save;
        if (g_rfl_prev_r != 0) {
            rfl_push_sides(&r, 0x800);
            tmos_memcpy(&r, g_rfl_recs, REC_LEN);
        }
        a0 = rfl_send_mode(1, left, &r);
    } else {
        if (hold_l == 1 || hold_r == 1)
            goto save;
        a0 = rfl_send_mode(3, left, &r);
    }
save:
    g_rfl_prev_r = (uint8_t)right;
    g_rfl_prev_l = (uint8_t)left;
    return a0;
}

/* Stock 0x16E02. Starts the hello timer (100 ms) while the link is not up.
   Returns g_rf_linked. */
uint32_t rf_hello_start(void)
{
    if (g_rf_linked != 0)
        return g_rf_linked;
    g_rf_hello_n = 0;
    app_timer_start((void *)rf_hello, 0, 100, 0);
    return g_rf_linked;
}

/* Stock 0x16E34. stop = 0 starts the 1.5 s green timer. Else it stops
   that timer and makes local record 0 active again. */
uint32_t rfl_green_timer(uint32_t stop)
{
    if (stop == 0)
        return app_timer_start((void *)rfl_send_green, 0, 1500, 0);
    app_timer_stop((void *)rfl_send_green);
    return light_rec_apply(1, 0);
}

/* Stock 0x16E66. Clears the light state and the queue, registers the light
   task, starts local record 0 and the RF hello. */
uint32_t light_init(void)
{
    tmos_memset(&g_light_idx, 0, 3);
    tmos_memset(&g_rfq, 0, sizeof(g_rfq));
    g_rfq_task = TMOS_ProcessEventRegister((void *)rfq_task_event);
    app_timer_stop((void *)rfl_send_green);
    light_rec_apply(1, 0);
    return rf_hello_start();
}
