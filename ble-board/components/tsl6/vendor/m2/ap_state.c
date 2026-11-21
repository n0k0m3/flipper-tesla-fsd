/* AP state and the actions that follow it (stock 0xA972..0xAD50).
   State block 0x2000BF00..0x2000BF09 (10 bytes). */
#include "m2.h"

/* Stock 0xA972 (no entry in the export). 1 s repeat timer of ap_on: starts
   the tick timer when it did not start yet. Stock leaves a0 unchanged on the
   first return. */
uint32_t ap_on_retry_cb(uint8_t *arg)
{
    if (!(g_cfg[4] & 0x10))
        return (uint32_t)arg;
    if (ap_active() == 0)
        return 0;
    uint32_t r = tick_is_on();
    if (r)
        return r;
    return tick_start(1, 0);
}

/* Stock 0xA9A2. AP went on: with "follow AP" (cfg[4] bit 4) start the scroll
   tick timer and a 1 s retry timer. */
uint32_t ap_on(uint32_t a0)
{
    if (!(g_cfg[4] & 0x10))
        return a0;
    uint32_t r = tick_is_on();
    if (r)
        return r;
    app_timer_start(ap_on_retry_cb, 1, 1000, 0);
    return tick_start(1, 0);
}

/* Stock 0xA9E2. AP went off: stop the scroll tick timer. The retry timer
   stops only when the tick timer runs. */
uint32_t ap_off(uint32_t a0)
{
    if (!(g_cfg[4] & 0x10))
        return a0;
    if (tick_is_on() == 0)
        return 0;
    app_timer_stop(ap_on_retry_cb);
    return tick_start(0, 0);
}

/* Stock 0xAA1A. Timer callback: ends the 2 s engage window. */
uint32_t engage_window_end(uint8_t *arg)
{
    g_bf01 = 0;
    return (uint32_t)arg;
}

/* Stock 0xAA24. Timer callback: second stalk press (gesture 13). */
uint32_t ap_engage_second_press(uint8_t *arg)
{
    return gesture_start(13);
}

/* Stock 0xAA2A. 0x238 frame: data[1] bits 4:0 minus 1 is a speed limit
   index. With AP on a change sends event 150 (lower) or 151 (higher). */
uint32_t can_238(uint8_t *e)
{
    uint32_t r = bus_alive(1);
    if (r)
        return r;
    uint8_t v = (uint8_t)((e[4] & 31) - 1);
    uint8_t old = g_bf07;
    if (v == old)
        return 0;
    if (g_ap_active) {
        if (v < old)
            event_fire(150);
        else if (old < v)
            event_fire(151);
    }
    g_bf07 = v;
    return gauge_push();
}

/* Stock 0xAA8A. */
uint32_t bf08_get(void)
{
    return g_bf08;
}

/* Stock 0xAA94. */
uint32_t speed_limit_index(void)
{
    return g_bf07;
}

/* Stock 0xAA9E. */
uint32_t bf06_get(void)
{
    return g_bf06;
}

/* Stock 0xAAB2. Bit 8 of the u16 at 0x2000C284. */
uint32_t c284_bit8_get(void)
{
    return (g_2000c284 >> 8) & 1;
}

/* Stock 0xAAC0. Engage (on != 0) or disengage AP.
   Without 0x20004186: a 0xCD1E request.
   With it: one stalk press (gesture 13), and a second press after 300 ms
   when bit 8 of 0x2000C284 is set. on is compared as a full word. */
uint32_t ap_engage(uint32_t on)
{
    if (g_20004186 == 0) {
        if (on == 0)
            return rstalk_request(1, 1, 0);
        if ((g_2000c284 >> 8) & 1)
            return rstalk_request(3, 2, 0);
        return rstalk_request(3, 1, 0);
    }
    if (g_ap_active == on)
        return on;
    uint32_t r = gesture_start(13);
    if (on == 0)
        return r;
    if (!((g_2000c284 >> 8) & 1))
        return r;
    return app_timer_start(ap_engage_second_press, 0, 300, 0);
}

/* Stock 0xAB34. Auto AP by mode x (1, 2 or 3). cfg[4] bits 2:1 select the
   mode (2, 4 or 6); bit 5 turns it off. Runs only when 0xCD14 is 4. */
uint32_t ap_auto_mode(uint32_t x)
{
    uint8_t c = g_cfg[4];
    if ((c & 6) == 0)
        return x;
    if (c & 0x20)
        return x;
    uint32_t r = gear_get();
    if (r != 4)
        return r;
    if (x == 1) {
        if ((g_cfg[4] & 6) != 2)
            return r;
    } else if (x == 2) {
        if ((g_cfg[4] & 6) != r)    /* r is 4 here */
            return r;
    } else if (x == 3) {
        if ((g_cfg[4] & 6) != 6)
            return r;
    } else {
        return r;
    }

    if (g_ap_active)
        return reengage_cancel(r);
    g_bf01 = 1;
    r = app_timer_start(engage_window_end, 0, 2000, 0);
    if (x != 1)
        return r;
    if (g_20004186)
        return r;
    r = ap_engage(1);
    if ((uint8_t)(g_variant - 1) > 1)
        return r;
    if (speed_get() == 0)
        return 0;
    return ap_state_update_v12(1);
}

/* Stock 0xAC08. Sets the AP flag. A change sends event 80 (on) or 81 (off),
   starts or stops the scroll tick timer and updates the gauge. on is
   compared as a full word and stored as a byte. */
uint32_t ap_state_update(uint32_t on)
{
    uint32_t r;

    if (g_ap_active == on)
        return on;
    g_ap_active = (uint8_t)on;
    event_fire((on == 0) + 80);
    if (g_ap_active == 0) {
        ap_off(0);
    } else {
        feature_d8_reset();
        if (g_bf01) {
            g_bf01 = 0;
            r = app_timer_stop(engage_window_end);
            r = reengage_cancel(r);
        } else {
            r = ap_auto_mode(3);
        }
        ap_on(r);
    }
    status_push(0);
    return gauge_push();
}

/* Stock 0xAC78. Variants 1-2 with bus 1 down: sets the AP flag. */
uint32_t ap_state_update_v12(uint32_t on)
{
    if ((uint8_t)(g_variant - 1) > 1)
        return on;
    uint32_t r = bus_alive(1);
    if (r)
        return r;
    return ap_state_update(on);
}

/* Stock 0xACAC. 0x3E9 frame: the AP flag, except on variants 1-2 or when
   bus 1 is alive. Variants 7-8: data[3] bit 5; others: data[3] bit 0. */
uint32_t can_3e9(uint8_t *e)
{
    if ((uint8_t)(g_variant - 1) <= 1)
        return (uint32_t)e;
    uint32_t r = bus_alive(1);
    if (r)
        return r;
    uint32_t on;
    if ((uint8_t)(g_variant - 7) <= 1)
        on = ((e[6] | (uint32_t)e[7] << 8) >> 5) & 1;  /* stock: lhu 6(e) */
    else
        on = e[6] & 1;
    return ap_state_update(on);
}

/* Stock 0xAD08. AP status for the gauge: 2 re-engage active, 1 AP on,
   3 when 0x2000BF08 is 2, else 0. */
uint32_t ap_gauge_status(void)
{
    if (reengage_active())
        return 2;
    if (g_ap_active)
        return 1;
    if (g_bf08 == 2)
        return 3;
    return 0;
}

/* Stock 0xAD3A. Clears the AP state block 0x2000BF00..0x2000BF09. */
uint32_t ap_state_clear(void)
{
    return res_memset(&g_ap_active, 0, 10);
}
