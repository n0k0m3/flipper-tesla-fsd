/* 0x3C2 steering-wheel switch frame, the button decoder (stock 0xB352..0xB9D6).
   data[0] bits 1:0 select the page (mux). Page 0 holds most buttons, page 1
   the scroll wheels (data[2] and data[3], 6-bit signed).
   A page-0 frame decodes one button code into 0x200040EC. The next page-1
   frame passes that code (or a page-1 code) to key_track.
   e is an RX ring entry (id u16, dlc, data[8], flag): data[n] is e[3 + n]. */
#include "m2.h"

/* Stock 0xB352. 1 when 0x2000C24F bits 6:5 are 2. */
uint32_t c24f_is_2(void)
{
    return (g_2000c24f & 0x60) == 0x40;
}

/* Stock 0xB368. Also a 100 ms repeat timer while c24f_is_2 is 1.
   The flag 0x2000BF20 keeps the last result. */
uint32_t c24f_poll(uint8_t *arg)
{
    uint32_t r = c24f_is_2();
    g_bf20 = (uint8_t)r;
    if (r == 0)
        return 0;
    return app_timer_start(c24f_poll, 0, 100, 0);
}

/* Stock 0xB396. 1 when 0x2000C250 bits 5:4 are 2. */
uint32_t c250_is_2(void)
{
    return (g_2000c250 & 0x30) == 0x20;
}

/* Stock 0xB3AA. Same as c24f_poll for c250_is_2 and 0x2000BF21. */
uint32_t c250_poll(uint8_t *arg)
{
    uint32_t r = c250_is_2();
    g_bf21 = (uint8_t)r;
    if (r == 0)
        return 0;
    return app_timer_start(c250_poll, 0, 100, 0);
}

/* Stock 0xB3E2. Starts gesture code (1..18): reschedules the scroll tick,
   runs feature_d8_arm for codes up to 11 and code 18, then sets the code. The
   range check uses the low byte, the code compares use the full word. */
uint32_t gesture_start(uint32_t code)
{
    if ((uint8_t)(code - 1) > 17)
        return code;
    uint32_t r = tick_schedule();
    if (code <= 11 || code == 18)
        r = feature_d8_arm();
    g_gesture_code = (uint8_t)code;
    return r;
}

/* Page 0: buttons. Returns what stock leaves in a0. */
static uint32_t page0(uint8_t *e)
{
    uint8_t b = (e[3] >> 4) & 1;
    if (g_bf1c != b) {
        g_bf1c = b;
        brake_event_update();
    }
    trigger_on_change(&g_200040ed, e[9] & 3, 118, 119, 2, 1);
    trigger_on_change(&g_200040ee, (e[9] >> 4) & 3, 132, 133, 2, 1);
    trigger_on_change(&g_200040ef, e[10] >> 6, 134, 135, 2, 1);
    trigger_on_change(&g_200040f0, (e[9] >> 2) & 3, 126, 127, 2, 1);

    uint8_t v = e[10];
    uint32_t val;
    if ((v & 3) == 1 && (e[9] & 0xC0) == 0x40) {
        if ((v & 12) == 4)
            val = 1;
        else if ((v & 12) == 8)
            val = 2;
        else
            val = 0;
    } else if ((v & 3) == 2) {
        val = 2;
    } else if ((e[9] & 0xC0) == 0x80 || (v & 12) == 8) {
        val = 2;
    } else {
        val = 0;
    }
    trigger_on_change(&g_200040f1, val, 130, 131, 2, 1);
    uint32_t r = horn_links_high_beam((e[3] >> 2) & 1);

    g_btn_code = 0;
    uint8_t x = e[3];
    uint8_t d4 = e[7];
    uint8_t d5 = e[8];
    uint8_t code = 0;
    if (x & 8)
        code = 55;
    else if (x & 4)
        code = 56;
    else if (d4 & 3)
        code = 19;
    else if (d4 & 12)
        code = 20;
    else if (d5 & 3)
        code = 21;
    else if (d5 & 12)
        code = 22;
    else if (d4 & 48)
        code = 23;
    else if (d4 & 192)
        code = 24;
    else if (d5 & 48)
        code = 25;
    else if (d5 & 192)
        code = 26;
    if (code)
        g_btn_code = code;

    if ((uint8_t)(g_variant - 7) <= 1) {
        if ((e[6] & 0xC0) != 0x80)
            return r;
        r = gear_get();
        if (r != 2)
            g_bf1d ^= 1;
        g_btn_code = 15;
        return r;
    }

    uint8_t y = e[4];
    if ((y & 3) == 2)
        code = 35;
    else if ((y & 12) == 8)
        code = 36;
    else if ((y & 48) == 32)
        code = 37;
    else if ((y & 192) == 128)
        code = 38;
    else {
        uint8_t z = e[5];
        r = z & 3;      /* stock: a0 holds this from here on */
        if ((z & 3) == 2)
            code = 39;
        else if ((z & 12) == 8)
            code = 40;
        else if ((z & 48) == 32)
            code = 41;
        else if ((z & 192) == 128)
            code = 42;
        else {
            uint8_t w = e[6];
            if ((w & 192) == 128)
                code = 27;
            else if ((w & 48) == 32)
                code = 28;
            else if ((w & 12) == 8)
                code = 29;
            else if ((w & 3) == 2)
                code = 30;
            else
                return r;
        }
    }
    g_btn_code = code;
    return r;
}

/* Sign-extends a 6-bit field. */
static int32_t sext6(uint8_t v)
{
    return (int32_t)((uint32_t)v << 26) >> 26;
}

/* Page 1: scroll wheels and the button code. */
static uint32_t page1(uint8_t *e)
{
    uint32_t code = g_btn_code;
    uint8_t b;

    if (code)
        goto send;

    uint8_t y = e[4];
    uint8_t x3 = e[3];
    int32_t l = sext6(e[5]);    /* left wheel */
    if ((y & 0xC0) == 0x80) {
        code = 4;
    } else if ((x3 & 24) == 16) {
        code = 2;
    } else if (l != 0) {
        manual_hold_extend();
        feature_d8_update(0);
        code = (l < 0) ? 3 : 1;
        if (g_bf20 == 0 && c24f_is_2() != 0) {
            c24f_poll(0);
            event_fire(l < 0 ? 44 : 43);
        }
    } else if ((y & 3) == 2) {
        code = 12;
    } else if ((y & 12) == 8) {
        code = 10;
    } else {
        int32_t rw = sext6(e[6]);   /* right wheel */
        if (rw != 0) {
            if (rw < 0)
                feature_d8_arm();
            if (g_bf21 == 0 && c250_is_2() != 0) {
                c250_poll(0);
                event_fire(rw < 0 ? 46 : 45);
            }
            uint32_t r = c24f_is_2();
            code = (rw < 0) ? 11 : 9;
            if (r)
                seat_nudge(sext6(e[6]));
        } else {
            uint8_t z = e[7];
            if (z & 64)
                code = 51;
            else if ((int8_t)z < 0)
                code = 52;
            else if (z & 16)
                code = 53;
            else if (z & 32)
                code = 54;
            else if ((x3 & 96) == 64)
                manual_hold_end(0);
        }
    }

    uint8_t v = g_variant;
    if ((uint8_t)(v - 7) <= 1) {
        uint32_t h = e[8] | ((uint32_t)e[9] << 8);  /* stock: lhu */
        if (h & 0x500) {
            lane_change_arm(0);
            hazard_auto_update(1);
        } else if (e[9] & 16) {
            code = 8;
        } else if (e[9] & 64) {
            code = 14;
        } else if (h & 64) {
            manual_hold_start();
        }
        high_beam_links_horn((e[9] >> 4) & 1);
    } else if ((uint8_t)(v - 5) <= 1 || v == 9) {
        if ((e[6] & 0xC0) == 0x80 || (e[8] & 48) == 32) {
            lane_change_arm(0);
            hazard_auto_update(1);
        } else if ((e[8] & 0xC0) == 0x80) {
            code = 8;
        } else if ((e[9] & 3) == 2) {
            code = 14;
        } else if ((e[9] & 12) == 8) {
            uint32_t r = gear_get();
            code = 15;
            if (r != 2)
                g_bf1d ^= 1;
        } else if ((e[5] & 0xC0) == 0x80) {
            manual_hold_start();
        }
        high_beam_links_horn((e[8] & 0xC0) == 0x80);
    } else {
        b = (e[8] >> 1) & 1;
        if (g_bf1e != b) {
            g_bf1e = b;
            if (b)
                key_event(5, 2, 2);
        }
        b = (e[8] >> 2) & 1;
        if (g_bf1f != b) {
            g_bf1f = b;
            if (b)
                key_event(13, 2, 2);
        }
        goto send;
    }

    b = (e[3] >> 5) & 3;
    if (g_bf1e != b) {
        g_bf1e = b;
        if (b == 2)
            code = 5;
    }
    b = (e[4] >> 4) & 3;
    if (g_bf1f != b) {
        g_bf1f = b;
        if (b == 2)
            code = 13;
    }
send:
    return key_track(code, 0, &g_bf24);
}

/* Stock 0xB41E. 0x3C2 frame, first handler: decodes the buttons. */
uint32_t can_3c2_buttons(uint8_t *e)
{
    uint8_t mux = e[3] & 3;
    if (mux == 0)
        return page0(e);
    if (mux == 1)
        return page1(e);
    return (uint32_t)e;
}
