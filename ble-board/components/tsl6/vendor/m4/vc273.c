/* 0x273 echo and 0x339 lock state (stock 0xEDE0..0xF574).
   The module sends a copy of the car's 0x273 frame (frame slot 2) with bits
   changed. Known bits:
     data[0] bit 3  front fog light (on, strobe, burst, cornering light)
     data[0] bit 5  frunk open request (one shot)
     data[2] bit 7  rear fog light (on, strobe, burst, reverse gear)
     data[2] bits 3:1  request code (2 = wake/unlock, see action_when_ready)
   The other bits are not identified. State is in g_vc273 (0x2000BFAC). */
#include "tsl6.h"
#include "m4.h"

#define SLOT_273 (&g_2000c1d4[2])

/* Stock 0xEDE0 (timer callback). */
void vc273_clear_bits_off_cb(uint8_t *arg)
{
    g_vc273.b2 = 0;
}

/* Stock 0xEDEA. 0x339 handler. data[1] bits 7:4 = lock state. States
   1, 3, 4, 6, 7, 9, 11, 13, 14 (mask 0x6ADA) count as "unlocked".
   On "unlocked": run the pending action in 100 ms, trigger 0x5D.
   On the other states: trigger 0x5C (state 10 skips hvac_override_check). */
void vc339_lock_rx(m4_frame_t *f)
{
    uint32_t st = f->data[1] >> 4;
    uint32_t open;

    if ((uint8_t)(st - 1) > 14)
        return;
    open = st == 15 ? 0 : ((1u << st) & 0x6ada) != 0;
    if (g_vc273.c3 == open)
        return;
    g_vc273.c3 = (uint8_t)open;
    if (open) {
        hvac_override_stop(0);
        door_cmd_schedule(100);
        event_fire(0x5d);
    } else {
        if (st != 10)
            hvac_override_check();
        event_fire(0x5c);
    }
}

/* Stock 0xEE5E. 0x273 from the car. data[0] bit 0 going to 0 turns the
   features off after 1 s (trigger 0x53); going to 1 gives trigger 0x52.
   data[6] bit 5 is stored as the car state and the wanted state. */
void vc273_rx(m4_frame_t *f)
{
    uint8_t b = f->data[0] & 1;

    if (g_vc273.b1 != b) {
        g_vc273.b1 = b;
        if (!b)
            features_off_later();
        rfl_green_timer(g_vc273.b1 != 0);
        event_fire((g_vc273.b1 == 0) + 0x52);
    }
    b = (f->data[6] >> 5) & 1;
    if (g_vc273.af != b) {
        g_vc273.af = b;
        g_vc273.b0 = b;
    }
}

/* Stock 0xEED6. Applies the wanted bits to a 0x273 frame and sends it when a
   bit is set or always != 0. One-shot requests are cleared. */
void vc273_echo(m4_frame_t *f, uint32_t always)
{
    vc273_t *s = &g_vc273;
    uint32_t changed = 0;

    /* Front fog light: strobe, cornering, burst, on. */
    if (s->b9) {
        changed = 1;
        f->data[0] = (f->data[0] & ~8) | ((s->ba & 1) << 3);
    } else if (s->b3) {
        changed = 1;
        f->data[0] |= 8;
    } else if (s->be) {
        changed = 1;
        f->data[0] = (f->data[0] & ~8) | ((s->bf & 1) << 3);
    } else if (s->b7) {
        changed = 1;
        f->data[0] |= 8;
    }

    /* Rear fog light: strobe, reverse gear ("rear fog while reversing",
       config byte 0 bit 3), burst, on. */
    if (s->bb) {
        changed = 1;
        f->data[2] = (f->data[2] & 0x7f) | (uint8_t)(s->bc << 7);
    } else if ((g_cfg[0] & 8) && gear_get() == 2) {
        changed = 1;
        f->data[2] |= 0x80;
    } else if (s->c0) {
        changed = 1;
        f->data[2] = (f->data[2] & 0x7f) | (uint8_t)(s->c1 << 7);
    } else if (s->b8) {
        changed = 1;
        f->data[2] |= 0x80;
    }

    if (s->b4) {
        changed = 1;
        f->data[7] = (f->data[7] & 0xe7) | 8;
    }
    if (s->b6) {
        s->b6 = 0;
        f->data[0] |= 0x20;
        changed = 1;
    }
    if (s->b5) {
        s->b5 = 0;
        f->data[3] |= 0x80;
        changed = 1;
    }
    if (s->ae) {
        changed = 1;
        f->data[3] = (f->data[3] & 0xfc) | (s->ae & 3);
        s->ae = 0;
    }
    if (s->ac) {
        s->ac = 0;
        f->data[3] = (f->data[3] & 0xc7) | 8;
        changed = 1;
    }
    if (s->b0 != s->af && gear_get() == 2) {
        changed = 1;
        f->data[6] = (f->data[6] & 0xdf) | ((s->b0 & 1) << 5);
    }
    if (s->ad) {
        if (s->ad == 2)
            door_cmd_schedule(1000);
        changed = 1;
        f->data[2] = (f->data[2] & 0xf1) | ((s->ad & 7) << 1);
        s->ad = 0;
    }
    if (s->b2 && ((f->data[2] & 1) || (f->data[0] & 4) || (int8_t)f->data[7] < 0)) {
        f->data[2] &= 0xfe;
        f->data[0] &= 0xfb;
        f->data[7] &= 0x7f;
    } else if (!changed && !always) {
        return;
    }
    can_tx(f);
}

/* Stock 0xF128. "Cornering light": front fog on for 5 s (restarts the 5 s). */
void cornering_light_on(void)
{
    if (!g_vc273.b3) {
        g_vc273.b3 = 1;
        vc273_echo(SLOT_273, 1);
    }
    app_timer_start(cornering_light_off_cb, 0, 5000, 0);
}

/* Stock 0xF17C (timer callback, not in the Ghidra function list). */
void cornering_light_off_cb(uint8_t *arg)
{
    g_vc273.b3 = 0;
    vc273_echo(SLOT_273, 1);
}

/* Stock 0xF192. Common part of the fog strobe and burst switches.
   other = the strobe flag of the other light, which shares the timer cb. */
void fog_flash_set(uint8_t *phase, uint8_t *flag, uint32_t on, uint32_t beep,
                   uint32_t other, m4_timer_cb_t cb)
{
    *flag = (uint8_t)on;
    if (!on) {
        vc273_echo(SLOT_273, 1);
        if (!other)
            app_timer_stop(cb);
    } else if (!other) {
        *phase = 0;
        cb(0);
    }
    if (beep)
        buzzer_beep_onoff(on);
    status_push(0);
}

/* Stock 0xF1E4 (timer callback). Fog strobe: inverts the light every 50 ms. */
void fog_strobe_cb(uint8_t *arg)
{
    m4_frame_t buf;

    frame_slot_copy(1, 2, &buf);
    g_vc273.bd = !g_vc273.bd;
    app_timer_start(fog_strobe_cb, 0, 50, 0);
    if (g_vc273.b9)
        buf.data[0] = (buf.data[0] & ~8) | ((g_vc273.bd & 1) << 3);
    g_vc273.ba = (buf.data[0] >> 3) & 1;
    if (g_vc273.bb)
        buf.data[2] = (buf.data[2] & 0x7f) | (uint8_t)(g_vc273.bd << 7);
    g_vc273.bc = (buf.data[2] >> 7) & 1;
    vc273_echo(&buf, 1);
}

/* Stock 0xF28C (timer callback). Fog burst: 4 steps of 135 ms, then 1.5 s. */
void fog_burst_cb(uint8_t *arg)
{
    m4_frame_t buf;
    uint32_t step, lvl, ms;

    frame_slot_copy(1, 2, &buf);
    step = g_vc273.c2;
    lvl = ~step & 1;
    g_vc273.c2 = (uint8_t)(step + 1);
    ms = 135;
    if (g_vc273.c2 > 3) {
        g_vc273.c2 = 0;
        ms = 1500;
    }
    app_timer_start(fog_burst_cb, 0, ms, 0);
    if (g_vc273.be)
        buf.data[0] = (buf.data[0] & ~8) | (lvl << 3);
    g_vc273.bf = (buf.data[0] >> 3) & 1;
    if (g_vc273.c0)
        buf.data[2] = (buf.data[2] & 0x7f) | (uint8_t)(lvl << 7);
    g_vc273.c1 = (buf.data[2] >> 7) & 1;
    vc273_echo(&buf, 1);
}

/* Stock 0xF344. Clears data[2] bit 0, data[0] bit 2 and data[7] bit 7 in
   every echo. */
void vc273_clear_bits_on(void)
{
    g_vc273.b2 = 1;
}

/* Stock 0xF350. Same as vc273_clear_bits_on, for 10 s. */
void vc273_clear_bits_10s(void)
{
    g_vc273.b2 = 1;
    app_timer_start(vc273_clear_bits_off_cb, 0, 10000, 0);
}

/* Stock 0xF370. data[0] bit 0 of the last car 0x273. */
uint8_t vc273_bit0_get(void)
{
    return g_vc273.b1;
}

/* Stock 0xF37A. One shot: data[3] bit 7. */
void vc273_d3b7_pulse(void)
{
    g_vc273.b5 = 1;
}

/* Stock 0xF386. data[7] bits 4:3 = 1 while set. */
void vc273_b4_set(uint32_t on)
{
    g_vc273.b4 = (uint8_t)on;
    buzzer_beep_onoff(on);
}

/* Stock 0xF392. */
uint8_t vc273_b4_get(void)
{
    return g_vc273.b4;
}

/* Stock 0xF39C. "Front fog strobe". */
void front_fog_strobe_set(uint32_t on, uint32_t beep)
{
    fog_flash_set(&g_vc273.bd, &g_vc273.b9, on, beep, g_vc273.bb, fog_strobe_cb);
}

/* Stock 0xF3C4. */
uint8_t front_fog_strobe_get(void)
{
    return g_vc273.b9;
}

/* Stock 0xF3CE. "Rear fog strobe". */
void rear_fog_strobe_set(uint32_t on, uint32_t beep)
{
    fog_flash_set(&g_vc273.bd, &g_vc273.bb, on, beep, g_vc273.b9, fog_strobe_cb);
}

/* Stock 0xF3F6. */
uint8_t rear_fog_strobe_get(void)
{
    return g_vc273.bb;
}

/* Stock 0xF400. Front fog burst. */
void front_fog_burst_set(uint32_t on, uint32_t beep)
{
    fog_flash_set(&g_vc273.c2, &g_vc273.be, on, beep, g_vc273.c0, fog_burst_cb);
}

/* Stock 0xF428. */
uint8_t front_fog_burst_get(void)
{
    return g_vc273.be;
}

/* Stock 0xF432. Rear fog burst. */
void rear_fog_burst_set(uint32_t on, uint32_t beep)
{
    fog_flash_set(&g_vc273.c2, &g_vc273.c0, on, beep, g_vc273.be, fog_burst_cb);
}

/* Stock 0xF45A. */
uint8_t rear_fog_burst_get(void)
{
    return g_vc273.c0;
}

/* Stock 0xF464. Rear fog on. */
void rear_fog_on_set(uint32_t on, uint32_t beep)
{
    g_vc273.b8 = (uint8_t)on;
    vc273_echo(SLOT_273, 1);
    if (beep)
        buzzer_beep_onoff(on);
}

/* Stock 0xF4A0. */
uint8_t rear_fog_on_get(void)
{
    return g_vc273.b8;
}

/* Stock 0xF4AA. Front fog on. */
void front_fog_on_set(uint32_t on, uint32_t beep)
{
    g_vc273.b7 = (uint8_t)on;
    vc273_echo(SLOT_273, 1);
    if (beep)
        buzzer_beep_onoff(on);
}

/* Stock 0xF4E6. */
uint8_t front_fog_on_get(void)
{
    return g_vc273.b7;
}

/* Stock 0xF4F0. One shot: data[3] bits 1:0 = 2 (on) or 1 (off). */
void vc273_d3_req(uint32_t on)
{
    g_vc273.ae = (on != 0) + 1;
}

/* Stock 0xF500. 0x102 data[6] bits 6:4 == 2 (frame slot 0). */
uint32_t vc102_d6_is2(void)
{
    return (g_2000c1d4[0].data[6] & 0x70) == 0x20;
}

/* Stock 0xF514. Wanted data[6] bit 5 (sent in reverse gear only). */
void vc273_b0_set(uint32_t on)
{
    g_vc273.b0 = (uint8_t)on;
}

/* Stock 0xF51E. */
uint32_t vc273_b0_get(void)
{
    return g_vc273.b0 != 0;
}

/* Stock 0xF52C. One shot: request code in data[2] bits 3:1. */
void vc273_lock_req_set(uint32_t v)
{
    g_vc273.ad = (uint8_t)v;
}

/* Stock 0xF536. Lock state from 0x339 is "unlocked" (see vc339_lock_rx). */
uint8_t vc273_unlocked(void)
{
    return g_vc273.c3;
}

/* Stock 0xF540. One shot: frunk open request (data[0] bit 5). */
void vc273_frunk_req(void)
{
    g_vc273.b6 = 1;
}

/* Stock 0xF54C. */
void vc273_b0_restore(void)
{
    g_vc273.b0 = g_vc273.af;
}

/* Stock 0xF55E. */
void vc273_reset(void)
{
    tmos_memset(&g_vc273, 0, sizeof(g_vc273));
}
