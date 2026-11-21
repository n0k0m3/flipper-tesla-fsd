/* Turn signals (0x3F5) and the AP actions that follow them
   (stock 0xA2D8..0xA972). State block 0x2000BEF0..0x2000BEFC (13 bytes).

   - The 0x3F5 handler keeps the indicator states and sends events.
   - A turn signal with AP on can send a lane-change gesture (codes 6/7 or
     the 0xCA1C request), after a 500 ms delay.
   - The end of a turn signal can re-engage AP: a 1 s retry timer waits for
     speed > 34 and |steering angle| < 61, for at most 30 tries. */
#include "m2.h"

/* Stock 0xA2D8. Returns 1 when AP may be engaged now.
   Variant 1-2 with bus 1 alive: AP flag bit 0, or 1 when the flag is 0.
   Other cases: 1 when AP is off, else 0. */
uint32_t ap_engage_allowed(void)
{
    uint8_t v = (uint8_t)(g_variant - 1);
    uint32_t ap = ap_active();

    if (v <= 1) {
        if (bus_alive(1) != 0 && ap == 0)
            return 1;
        return ap & 1;
    }
    if (ap != 0)
        return 0;
    return 1;
}

/* Stock 0xA318. Timer callback: AP re-engage retry, every 1 s. */
uint32_t ap_reengage_cb(uint8_t *arg)
{
    uint32_t ok = 0;

    if (speed_get() > 34)                   /* speed */
        ok = steer_abs() < 61;              /* |steering angle| */
    if (bus_alive(1) != 0 && bf08_get() != 2)
        goto retry;
    if (ok) {
        g_reengage_state = 1;
        if (ap_engage_allowed() == 0)
            return 0;
        return ap_engage(1);
    }
retry:;
    uint8_t n = (uint8_t)(g_reengage_tries + 1);
    g_reengage_tries = n;
    if (n <= 29)
        return app_timer_start(ap_reengage_cb, 0, 1000, 0);
    g_reengage_tries = 0;
    g_reengage_state = 1;
    return app_timer_stop(ap_reengage_cb);
}

/* Stock 0xA3C4 (no entry in the export). Lane-change request for side x
   (1 or 2). Variants 1, 2, 3, 4, 6 and 9 use the 0xCA1C request; the others
   send gesture code 6 or 7. dir selects the code pair. x is compared as a
   full word, only the range check uses the low byte. */
uint32_t lane_change_send(uint32_t x, uint32_t dir)
{
    if ((uint8_t)(x - 1) > 1)
        return x;
    uint8_t v = g_variant;
    if (v <= 9 && ((0x25Eu >> v) & 1)) {
        uint32_t code;
        if (dir)
            code = (x == 2) ? 6 : 2;
        else
            code = (x == 2) ? 4 : 8;
        return lstalk_inject(code, 4);
    }
    uint32_t t = x - 2;
    uint32_t c = dir ? (t != 0) : (t == 0);
    return gesture_start(c + 6);
}

/* Stock 0xA41A (no entry in the export). Timer callback. */
uint32_t lane_change_cb(uint8_t *arg)
{
    return lane_change_send(g_lc_side, 0);
}

/* Stock 0xA426 (no entry in the export). Timer callback. */
uint32_t lane_change_cb2(uint8_t *arg)
{
    uint8_t side = g_lc_side;
    g_lc_stage = 1;
    return lane_change_send(side, 1);
}

/* Stock 0xA43C. Turn signal change: x = 1 or 2 is a side, other values
   mean off. */
uint32_t turn_signal_event(uint32_t x)
{
    uint32_t r;

    if ((uint8_t)(x - 1) > 1) {
        /* Signal off. */
        if (g_reengage_state != 2)
            return x;
        if (ap_engage_allowed() != 0) {
            g_reengage_state = 3;
            g_reengage_tries = 0;
            uint32_t ms = ((g_cfg[6] >> 1) & 7) * 1000 + 100;
            return app_timer_start(ap_reengage_cb, 0, ms, 0);
        }
        g_reengage_state = 1;
        return 0;
    }

    r = ap_active();
    if (r == 0) {
        if (g_reengage_state != 3)
            return 0;
        g_reengage_state = 2;
        return app_timer_stop(ap_reengage_cb);
    }

    uint8_t c = g_cfg[4];
    if (g_lc_arm == 0) {
        if ((c & 0x26) != 0x20)
            return r;
        if (g_20004185 == 0)
            return r;
        r = tick_is_on();
        if (r == 0)
            return 0;
        g_lc_side = (uint8_t)x;
        return app_timer_start(lane_change_cb2, 0, 500, 0);
    }

    g_lc_arm = 0;
    if (c & 0x20) {
        if (c & 6)
            return r;
        if (g_20004185 == 0)
            return r;
    } else {
        if ((c & 6) == 0)
            return r;
        if (g_reengage_state == 0)
            return r;
        if (g_20004185 != 0)
            return r;
        g_reengage_state = 2;
        r = app_timer_stop(ap_reengage_cb);
    }
    if ((uint8_t)(g_variant - 1) > 3)
        return r;
    g_lc_side = (uint8_t)x;
    return app_timer_start(lane_change_cb, 0, 500, 0);
}

/* Stock 0xA5A4. Arms the lane change. Stock leaves a0 unchanged. */
uint32_t lane_change_arm(uint32_t a0)
{
    g_lc_arm = 1;
    return a0;
}

/* Stock 0xA5B0. Stops the re-engage (state 1). Stock leaves a0 unchanged
   on the early return. */
uint32_t reengage_cancel(uint32_t a0)
{
    uint8_t c = g_cfg[4];
    if ((c & 6) == 0)
        return a0;
    if ((c & 0x20) | g_reengage_state)
        return a0;
    g_reengage_state = 1;
    app_timer_stop(ap_reengage_cb);
    return status_push(0);
}

/* Stock 0xA5F6. Stops the re-engage (state 0). */
uint32_t reengage_off(uint32_t a0)
{
    if (g_reengage_state == 0)
        return a0;
    g_reengage_state = 0;
    app_timer_stop(ap_reengage_cb);
    return status_push(0);
}

/* Stock 0xA624. 0x3F5 frame. e is an RX ring entry (id u16, dlc, data[8],
   flag), so data[n] is e[3 + n]. data[0] bits 1:0 left indicator, bits 3:2
   right indicator, bits 7:4 hazard state; data[4] holds more light bits. */
uint32_t can_3f5_lights(uint8_t *d)
{
    uint8_t b7 = d[7];
    uint8_t left = d[3] & 3;
    uint8_t right = (d[3] >> 2) & 3;
    uint8_t f9 = (b7 >> 4) & 3;
    uint8_t fb = b7 & 1;
    uint8_t fa = (b7 >> 6) & 1;

    if (g_ind_left != left || g_ind_right != right || g_bef9 != f9 || g_befb != fb || g_befa != fa) {
        g_bef9 = f9;
        g_ind_left = left;
        g_ind_right = right;
        g_befb = fb;
        g_befa = fa;
        gauge_push();
    }

    uint8_t f8 = (d[7] >> 2) & 1;
    if (g_bef8 != f8) {
        g_bef8 = f8;
        if (flash_active_get() == 0)
            event_fire((g_bef8 == 0) + 84);
        gauge_push();
    }

    uint8_t hz = d[3] >> 4;
    if (g_hazard != hz)
        g_hazard = hz;

    uint8_t e1 = g_200040e1;
    if (e1 == 1) {
        if (g_hazard)
            g_200040e1 = 2;
    } else if (e1 == 0) {
        if (hazard_req_get() == 1)
            g_200040e1 = g_hazard ? 2 : 1;
    } else if (e1 == 2) {
        if (g_hazard == 0) {
            g_200040e1 = 0;
            hazard_req_set(0);
        }
    }

    uint8_t l1 = (uint8_t)(left - 1);   /* <= 1: left on */
    uint8_t r1 = (uint8_t)(right - 1);  /* <= 1: right on */
    uint8_t st = g_lc_stage;

    if (st == 1) {
        if (l1 > 1 && r1 > 1) {
            g_lc_stage = 2;
            uint32_t ms = (g_variant == 5) ? 450 : 250;
            app_timer_start(lane_change_cb, 0, ms, 0);
        }
    } else if (st == 2) {
        if (l1 <= 1 || r1 <= 1)
            g_lc_stage = 0;
    } else if (st == 0) {
        uint8_t s5 = g_turn_state;
        if (l1 <= 1) {
            if (r1 <= 1) {
                /* Both on: hazard. */
                if ((g_hazard == 1 || g_hazard == 7) && g_turn_state != 3) {
                    g_turn_state = 3;
                    event_fire(94);
                }
            } else if (g_turn_state != 1) {
                g_turn_state = 1;
                turn_signal_event(1);
                event_fire(106);
                event_fire(108);
            }
        } else if (r1 <= 1) {
            if (s5 != 2) {
                g_turn_state = 2;
                turn_signal_event(2);
                event_fire(107);
                event_fire(108);
            }
        } else if (s5 != 0) {
            /* Both off. */
            if (s5 <= 2) {
                hazard_auto_update(0);
                turn_signal_event(0);
                event_fire((ap_active() != 0) + 96);
                event_fire(109);
            } else if (s5 == 3) {
                event_fire(95);
            }
            g_turn_state = 0;
            g_lc_arm = 0;
        }
    }
    return light_sides_update(left, right);
}

/* Stock 0xA8AC. Returns 1 when the re-engage state is not 0. */
uint32_t reengage_active(void)
{
    return g_reengage_state != 0;
}

/* Stock 0xA8BA. Light bits for the gauge. */
uint32_t lights_gauge_bits(void)
{
    uint32_t r = (uint32_t)(g_bef9 - 1) < 2;
    r |= (uint32_t)g_bef8 << 1;
    r |= g_bef8 ? 4 : ((uint32_t)g_befb << 2);
    r |= (uint32_t)g_befa << 3;
    return r & 0xFF;
}

/* Stock 0xA900. Bit 0: left state is 2. Bit 1: right state is 2. */
uint32_t ind_state2_bits(void)
{
    uint32_t r = g_ind_left == 2;
    if (g_ind_right == 2)
        r |= 2;
    return r;
}

/* Stock 0xA922. Returns the hazard field of 0x3F5. */
uint32_t hazard_state(void)
{
    return g_hazard;
}

/* Stock 0xA92C. Two 2-bit fields of the u16 at 0x2000C28E (bits 5:4 and
   13:12). Returns 1 if one is 1, else 2 if one is 2, else 0. */
uint32_t c28e_field_state(void)
{
    uint16_t v = g_2000c28e;
    uint32_t a = (v >> 4) & 3;
    uint32_t b = (v >> 12) & 3;
    if (a == 1 || b == 1)
        return 1;
    if (a == 2 || b == 2)
        return 2;
    return 0;
}

/* Stock 0xA95C. Clears the state block 0x2000BEF0..0x2000BEFC. */
uint32_t turn_state_clear(void)
{
    return res_memset(&g_lc_arm, 0, 13);
}
