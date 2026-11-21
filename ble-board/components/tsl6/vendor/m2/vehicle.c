/* Bus monitor and the vehicle frame handlers 0x2B6, 0x293, 0x243, 0x321,
   0x20C and 0x2F3 (stock 0xAD50..0xB352).

   A frame handler gets a pointer e to an RX ring entry: id u16, dlc, data[8],
   flag. So data[n] is e[3 + n]. Handlers that change a frame send it again
   with can_tx. */
#include "m2.h"

/* Stock 0xAD50 (no entry in the export). TMOS task of the bus monitor.
   Event 1 (every 1 s) ages the three bus counters 0x200040E5..E7 up to 3.
   A frame on bus i sets its counter to 0 (bus_alive checks < 3). */
uint32_t bus_mon_event(uint32_t task, uint32_t events)
{
    if (!(events & 1))
        return 0;
    for (uint8_t *p = &g_bus_age[0]; p != &g_bus_age[3]; p++) {
        if (*p <= 2)
            *p = (uint8_t)(*p + 1);
    }
    return events ^ 1;
}

/* Stock 0xAD7C. Starts the bus monitor: all buses down, 1 s reload task. */
uint32_t bus_mon_init(void)
{
    res_memset(&g_bus_task, 0, 4);
    g_bus_age[0] = 3;
    g_bus_age[1] = 3;   /* stock: one halfword store of 0x0303 */
    g_bus_age[2] = 3;
    uint32_t t = res_event_register(bus_mon_event);
    g_bus_task = (uint8_t)t;
    return res_start_reload_task(t, 1, 1600);
}

/* Stock 0xADCA. 0x2B6 frame: once after vc2b6_app_set, sets data[0] bits 3 and 6
   and sends the frame. */
uint32_t can_2b6(uint8_t *e)
{
    if (g_e9 == 0)
        return (uint32_t)e;
    g_e9 = 0;
    e[3] = (e[3] & ~0x48) | 0x48;
    return can_tx(e);
}

/* Stock 0xADEA. With 0xCD14 = 1 stores x in 0x200040E8/E9 (the next 0x2B6
   frame gets the bits). x is stored as a byte, compared as a word. */
uint32_t vc2b6_app_set(uint32_t x)
{
    if (gear_get() == 1) {
        g_e8 = (uint8_t)x;
        g_e9 = (uint8_t)x;
        if (x == 0)
            vc273_d3b7_pulse();
        buzzer_beep_onoff(x);
    } else {
        tune_two_low(0);
    }
    return status_push(0);
}

/* Stock 0xAE24. */
uint32_t vc2b6_app_get(void)
{
    return g_e8;
}

/* Stock 0xAE2A. 0x293 frame: when bit 7 of 0x200040EA is set, writes the
   mode (low bits of 0x200040EA) in data[0] bits 4:2, steps the counter in
   data[6] bits 7:4, sets the checksum in the last data byte and sends. */
uint32_t can_293(uint8_t *e)
{
    uint8_t v = g_ea;
    if ((int8_t)v >= 0)
        return (uint32_t)e;
    v &= 0x7F;
    g_ea = v;
    if (v <= 1)
        e[3] = (uint8_t)((e[3] & ~0x1C) | ((v & 7) << 2));
    else if (v == 2)
        e[3] = (uint8_t)((e[3] & ~0x1C) | 0x18);
    e[9] = (uint8_t)(e[9] + 16);
    uint8_t n = e[2];
    int32_t last = (int32_t)n - 1;      /* dlc 0 writes e[2] (stock) */
    uint32_t c = frame_checksum_last(e);
    e[3 + last] = (uint8_t)c;
    return can_tx(e);
}

/* Stock 0xAEA2. Steps the 0x293 mode 0 -> 1 -> 2 -> 0 and marks it for the
   next frame. */
uint32_t mode_293_step(void)
{
    uint8_t v = g_ea;
    v = (uint8_t)((((int32_t)(v & 0x7F) + 1) % 3) | 0x80);
    g_ea = v;
    return buzzer_beeps(1);
}

/* Stock 0xAEC4. Clears 0x200040E8..0x200040EA. */
uint32_t vehicle_e8_clear(void)
{
    return res_memset(&g_e8, 0, 3);
}

/* Stock 0xAED6 (no entry in the export). Timer callback: marks the 0x2F3
   override on and starts the end timer: 0, 1, 5 or 10 minutes by cfg[9]
   bits 5:4. Stock builds the minute table on the stack. */
uint32_t hvac_override_start(uint8_t *arg)
{
    static const uint8_t minutes[4] = {0, 1, 5, 10};
    g_bf10 = 1;
    uint32_t ms = minutes[(g_cfg[9] >> 4) & 3] * 60000u;
    return app_timer_start(hvac_override_stop, 0, ms, 0);
}

/* Stock 0xAF1E. Timer callback: ends the 0x2F3 override. Stock leaves a0
   unchanged when cfg[9] bits 5:4 are 0. */
uint32_t hvac_override_stop(uint8_t *arg)
{
    if (!(g_cfg[9] & 0x30))
        return (uint32_t)arg;
    g_bf10 = 0;
    app_timer_stop(hvac_override_stop);
    return app_timer_stop(hvac_override_start);
}

/* Stock 0xAF58. 0x243 frame: data[0] bits 2:0 is a state. Five frames with
   state 5 make 0x200040EB reach 4 (0x20004184 = 1). State 0 resets it.
   With state 4 (or state 0 before the count), it keeps the 11-bit value of
   data[3] bits 7:6, data[4] and data[5] bit 0 in 0x2000BF16. */
uint32_t can_243(uint8_t *e)
{
    uint8_t st = e[3] & 7;
    uint8_t n = g_eb;
    g_20004184 = (n == 4);
    if (st == 0) {
        if (n == 4) {
            g_eb = 0;
            return (uint32_t)e;
        }
    } else if (st == 5) {
        if (n != 4)
            g_eb = (uint8_t)(n + 1);
        return (uint32_t)e;
    } else if (st != 4 || n != 4) {
        return (uint32_t)e;
    }
    uint32_t v = (uint32_t)(e[6] >> 6) | ((uint32_t)e[7] << 2) | ((uint32_t)(e[8] & 1) << 10);
    if (g_bf16 == v)
        return (uint32_t)e;
    g_bf16 = (uint16_t)v;
    return gauge_push();
}

/* Stock 0xAFCC. 0x321 frame: keeps data[5] in 0x2000BF14. */
uint32_t can_321(uint8_t *e)
{
    uint8_t v = e[8];
    if (g_bf14 == v)
        return (uint32_t)e;
    g_bf14 = v;
    return gauge_push();
}

/* Stock 0xAFEA. 0x20C frame: an 11-bit value (data[0..1]) in 0x2000BF18,
   a 10-bit value (data[4..5]) in 0x2000BF1A, and data[1] bits 7:5 +
   data[2] in 0x2000BF12. */
uint32_t can_20c(uint8_t *e)
{
    uint32_t a = ((uint32_t)(e[4] & 7) << 8) | e[3];
    uint32_t b = ((uint32_t)(e[8] & 3) << 8) | e[7];
    if (g_bf18 != a || g_bf1a != b) {
        g_bf18 = (uint16_t)a;
        g_bf1a = (uint16_t)b;
        gauge_push();
    }
    uint32_t c = (uint32_t)(e[4] >> 5) | ((uint32_t)e[5] << 3);
    if (g_bf12 != c)
        g_bf12 = (uint16_t)c;
    return (uint32_t)e;
}

/* Stock 0xB08C. */
uint32_t can_243_value_get(void)
{
    return g_bf16;
}

/* Stock 0xB096. */
uint32_t can_321_d5_get(void)
{
    return g_bf14;
}

/* Stock 0xB0A0. */
uint32_t can_20c_v10_get(void)
{
    return g_bf1a;
}

/* Stock 0xB0AA. */
uint32_t can_20c_v11_get(void)
{
    return g_bf18;
}

/* Stock 0xB0B4. 0x2F3 frame. Two fields follow the car (0x2000BF0E, BF0C)
   unless the app set other values (0x2000BF0F, BF0D); then the frame gets
   the app values and is sent again. During the override (0x2000BF10) the
   frame also gets data[2] = 101 and data[3] bits 4:2 = 1, unless that mode
   field is 1 or 2 already (then the override ends). */
uint32_t can_2f3(uint8_t *e)
{
    uint32_t r = (uint32_t)e;
    uint32_t send = 0;
    uint8_t f;

    if ((uint8_t)(g_variant - 7) <= 1) {
        f = (e[7] >> 6) & 1;
        if (f != g_bf0e) {
            g_bf0e = f;
            g_bf0f = f;
            r = status_push(0);
        }
        if (g_bf0e != g_bf0f) {
            e[7] = (uint8_t)((e[7] & ~0x40) | ((g_bf0f & 1) << 6));
            send = 1;
        }
    } else {
        f = e[6] >> 5;
        if (f != g_bf0e) {
            g_bf0e = f;
            g_bf0f = f;
            r = status_push(0);
        }
        if (g_bf0e != g_bf0f) {
            e[6] = (uint8_t)((e[6] & 31) | (g_bf0f << 5));
            send = 1;
        }
    }

    f = (e[5] >> 4) & 3;
    if (g_bf0c != f) {
        g_bf0c = f;
        g_bf0d = f;
    }
    if (g_bf0c != g_bf0d) {
        e[5] = (uint8_t)((e[5] & ~0x30) | ((g_bf0d & 3) << 4));
        send = 1;
    }

    if (g_bf10) {
        uint8_t x = e[6];
        if ((((x & 28) - 4) & 248) != 0) {
            e[5] = 101;
            e[6] = (uint8_t)((x & ~0x1C) | 4);
            return can_tx(e);
        }
        r = hvac_override_stop(0);
    }
    if (send)
        return can_tx(e);
    return r;
}

/* Stock 0xB200. App sets field BF0D: x == 0 gives 2, else 1. */
uint32_t can_2f3_bf0d_set(uint32_t x)
{
    g_bf0d = (uint8_t)((x == 0) + 1);
    return buzzer_beep_onoff(x);
}

/* Stock 0xB212. */
uint32_t can_2f3_bf0d_is1(void)
{
    return g_bf0d == 1;
}

/* Stock 0xB222. App sets field BF0F: x != 0 gives 4, else 1. */
uint32_t can_2f3_bf0f_set(uint32_t x)
{
    g_bf0f = x ? 4 : 1;
    buzzer_beep_onoff(x);
    return status_push(0);
}

/* Stock 0xB242. */
uint32_t can_2f3_bf0f_on(void)
{
    return g_bf0f >= 2;
}

/* Stock 0xB254. Starts the override after 3 s when the value 0x2000BF16
   is more than 49 above 0x2000BF12 (signed 16-bit difference). Stock leaves
   a0 unchanged when cfg[9] bits 5:4 are 0. */
uint32_t hvac_override_check(uint32_t a0)
{
    if (!(g_cfg[9] & 0x30))
        return a0;
    uint32_t r = vc273_bit0_get();
    if (r != 0) {
        r = cp_state_get();
        if (r == 0)
            return 0;
    }
    int16_t d = (int16_t)(g_bf16 - g_bf12);
    if (d <= 49)
        return r;
    return app_timer_start(hvac_override_start, 0, 3000, 0);
}

/* Stock 0xB2B4. App values follow the car values again. Stock leaves a0
   unchanged. */
uint32_t can_2f3_app_follow(uint32_t a0)
{
    g_bf0f = g_bf0e;
    g_bf0d = g_bf0c;
    return a0;
}

/* Stock 0xB2CE. Clears the block 0x2000BF0C..0x2000BF1B. */
uint32_t vehicle_bf0c_clear(void)
{
    return res_memset(&g_bf0c, 0, 16);
}

/* Stock 0xB2E4 (no entry in the export). Timer callback: takes the saved
   frame 10 (0x2000C1D4 + 10 * 12), writes the result of flash_step in it and
   sends it. Variants 7-8: data[6] bit 4 = (result == 1); others: data[5]
   bits 7:6 = 2 when the result is 1, else 1. */
uint32_t resend_frame10_cb(uint8_t *arg)
{
    uint8_t tmp;
    uint8_t f[12];

    uint32_t r = frame_slot_copy(1, 10, f);
    if (f[2] == 0)
        return r;
    tmp = 0;
    r = flash_step(&tmp);
    if (r == 0)
        return 0;
    if ((uint8_t)(g_variant - 7) <= 1) {
        f[9] = (uint8_t)((f[9] & ~0x10) | ((r == 1) << 4));
    } else {
        uint32_t v = (r == 1) ? 2 : 1;
        f[8] = (uint8_t)((f[8] & 63) | (v << 6));
    }
    return can_tx(f);
}
