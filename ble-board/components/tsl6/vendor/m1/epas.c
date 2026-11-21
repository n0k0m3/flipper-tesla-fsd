/* 0x370 EPAS handler and the AP helper flags around it (stock 0x9EAA..0xA144).
   With 0x370 present, the runtime nudges the steering torque by sending a
   changed copy of 0x370 (the torque method). */
#include "tsl6.h"
#include "m1.h"

/* Torque offsets per step (stock 0x19B68), added to 0x802. */
static const int16_t torque_ofs[4] = {150, 180, -150, -180};

/* Stock 0x9EAA. Timer callback: ends the 6 s window after AP comes on.
   a0 passes through. */
uint32_t ap_window_end(uint32_t a0)
{
    g_ap_window = 0;
    return a0;
}

/* Stock 0x9EC8 (also the tail of 0x9EB0). g_d8 = AP active (ap_active)
   and not g_20004186. */
uint32_t feature_d8_eval(void)
{
    uint32_t r = ap_active();
    uint8_t v = 0;

    if (r)
        v = g_20004186 ^ 1;
    g_d8 = v & 1;
    return r;
}

/* Stock 0x9EB0. Same as feature_d8_eval when config byte 8 bit 4 is on,
   else g_d8 = 0 (a0 passes through). */
uint32_t feature_d8_update(uint32_t a0)
{
    if ((g_cfg[8] & 0x10) == 0) {
        g_d8 = 0;
        return a0;
    }
    return feature_d8_eval();
}

/* Stock 0x9EE8. Timer callback: ends the manual-scroll hold and runs the
   tick at once. */
uint32_t manual_hold_end(uint32_t a0)
{
    if (g_manual_scroll_hold == 0)
        return a0;
    g_manual_scroll_hold = 0;
    return tick_now();
}

/* Stock 0x9EF8. */
uint8_t feature_d8_get(void)
{
    return g_d8;
}

/* Stock 0x9EFE. Clears g_d8; with config byte 8 bit 4, ap_active() != 0 and
   g_20004186 = 0, it runs feature_d8_update 10 s later. */
uint32_t feature_d8_arm(uint32_t a0)
{
    uint32_t r;

    g_d8 = 0;
    if ((g_cfg[8] & 0x10) == 0)
        return a0;
    r = ap_active();
    if (r == 0 || g_20004186 != 0)
        return r;
    return app_timer_start(TIMER_CB(feature_d8_update), 0, 10000, 0);
}

/* Stock 0x9F42. v > 24: arm (feature_d8_arm) when the counter is 0.
   v <= 24: count the counter down. a0 = v passes through. */
uint32_t feature_d8_level(uint32_t v)
{
    if (v > 24) {
        if (g_d9 == 0)
            return feature_d8_arm(v);
        return v;
    }
    if (g_d9)
        g_d9--;
    return v;
}

/* Stock 0x9F5A. Counter = 25, then feature_d8_update. */
uint32_t feature_d8_reset(void)
{
    g_d9 = 25;
    return feature_d8_update(0);
}

/* Stock 0x9F64. A manual scroll: hold the tick for 5 s. */
uint32_t manual_hold_start(void)
{
    g_manual_scroll_hold = 1;
    return app_timer_start(TIMER_CB(manual_hold_end), 0, 5000, 0);
}

/* Stock 0x9F80. Restarts the 5 s hold when it runs. a0 passes through. */
uint32_t manual_hold_extend(uint32_t a0)
{
    if (g_manual_scroll_hold == 0)
        return a0;
    return manual_hold_start();
}

/* Stock 0x9F8A. 0x370 (EPAS) frame f (12-byte ring entry, data at f[3]).
   Frames with a bad checksum (data[7]) are ignored. AP on = data[6] bits 7:5 = 2. On a change it calls ap_state_update.
   While AP is on, the tick is on and the frame did not come from us
   (flag != 2), and data[2..3] (12-bit torque value) is 1961..2139, it sends
   a changed copy: torque = 1960 (turn state 1), 2140 (state 2), or
   0x802 + torque_ofs[step]; data[4] bits 7:6 = 01; counter data[6] + 1;
   new checksum. Returns the a0 that stock leaves. */
uint32_t can_370_epas(uint8_t *f)
{
    uint8_t c[12];
    uint32_t r;
    uint8_t ap_bits, ap, mine_ok;
    uint8_t mode;

    r = chk_tesla_sum(f);
    if (r == 0)
        return r;
    bus_age_clear(2);
    ap_bits = f[9] & 0xE0;
    ap = ap_bits == 0x40;
    /* fixed_mode_get() always returns 1, so the flag test always runs. */
    if (fixed_mode_get() & 2)
        mine_ok = 1;
    else
        mine_ok = f[11] != 2;
    if (g_ap_370 != ap) {
        g_ap_370 = ap;
        g_ap_window = ap;
        ap_state_update(ap);
        if (ap_bits == 0x40) {
            if (mine_ok)
                app_timer_start(TIMER_CB(ap_window_end), 0, 6000, 0);
        } else {
            g_torque_mode = 0;
            app_timer_stop(TIMER_CB(ap_window_end));
            app_timer_stop(TIMER_CB(scroll_gesture_cb));
        }
    }
    r = tick_is_on();
    if (r == 0 || ap_bits != 0x40 || !mine_ok)
        return r;

    tmos_memcpy(c, f, 12);
    mode = epas_torque_mode_get();
    r = mode;
    if (g_torque_mode == mode) {
        if (g_torque_mode == 0 && g_ap_window == 0)
            return r;
    } else {
        g_torque_mode = mode;
        g_torque_step = (g_torque_step + 1) & 3;
        if (mode) {
            r = app_timer_start(TIMER_CB(scroll_gesture_cb), 1, 3000, 0);
        } else {
            r = app_timer_stop(TIMER_CB(scroll_gesture_cb));
            if (g_ap_window == 0)
                return r;
        }
    }

    {
        uint16_t v = (uint16_t)((c[5] & 0x0F) << 8 | c[6]);
        uint16_t t;
        uint8_t turn;

        if ((uint16_t)(v - 1961) > 178)
            return r;
        turn = turn_state_get();
        if (turn == 1)
            t = 1960;
        else if (turn == 2)
            t = 2140;
        else
            t = (uint16_t)(torque_ofs[g_torque_step] + 0x802);
        c[6] = (uint8_t)t;
        c[7] = (c[7] & 0x3F) | 0x40;
        c[5] = (c[5] & 0xF0) | ((t >> 8) & 0x0F);
        c[9] = (c[9] & 0xF0) | ((c[9] + 1) & 0x0F);
        c[10] = frame_checksum_last(c);
        return 0; /* port: the 0x370 echo is never sent */
    }
}
