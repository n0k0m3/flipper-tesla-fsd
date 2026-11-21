/* 0x3C2 steering-wheel switch frame, the frame editor (stock 0xB9D6..0xC05A).
   The second 0x3C2 handler changes the frame and sends it again.
   - Page 0: button remaps (cfg[10]) and the data[0] bit 2 feature
     (0x2000BF2F..BF33).
   - Page 1: the gesture code 0x2000BF2C puts the tick bytes in the frame,
     then the code is cleared. Every page-1 frame steps the gesture gap
     counter (scroll_gesture(0)).
   e is an RX ring entry (id u16, dlc, data[8], flag): data[n] is e[3 + n]. */
#include "m2.h"

/* Stock 0xB9D6. Stock leaves a0 unchanged. */
uint32_t bf34_request(uint32_t a0)
{
    g_bf34 = 1;
    return a0;
}

/* Stock 0xB9E2. */
uint32_t bf1c_get(void)
{
    return g_bf1c;
}

/* Stock 0xB9EC. */
uint32_t bf1d_get(void)
{
    return g_bf1d;
}

/* Stock 0xB9F6. */
uint32_t bf1d_set(uint32_t x)
{
    g_bf1d = (uint8_t)x;
    return x;
}

/* Stock 0xBA00. Requests gesture 15 (it toggles 0x2000BF1D) when BF1D is
   not x and 0xCD14 is not 2. It does not store x. x compares as a word. */
uint32_t bf1d_request(uint32_t x)
{
    if (g_bf1d == x)
        return x;
    uint32_t r = gear_get();
    if (r == 2)
        return r;
    r = tick_schedule();
    g_gesture_code = 15;
    return r;
}

/* Stock 0xBA30. Stock leaves a0 unchanged. */
uint32_t bf2d_start12(uint32_t a0)
{
    g_bf2d = 12;
    return a0;
}

/* Stock 0xBA3C. Returns the address of 0x2000BF31. */
uint32_t bf31_ptr(void)
{
    return (uint32_t)&g_bf31;
}

/* Stock 0xBA46. Returns the address of 0x2000BF30. */
uint32_t bf30_ptr(void)
{
    return (uint32_t)&g_bf30;
}

/* Stock 0xBA50. */
uint32_t bf33_set(uint32_t x)
{
    g_bf33 = (uint8_t)x;
    return x;
}

/* Stock 0xBA5A. */
uint32_t bf33_get(void)
{
    return g_bf33;
}

/* Stock 0xBA64. */
uint32_t bf32_horn_set(uint32_t x)
{
    g_bf32 = (uint8_t)x;
    horn_pulse(x);
    return status_push(0);
}

/* Page 0 of can_3c2_swc. Returns what stock leaves in a0 when no frame
   is sent. */
static uint32_t edit_page0(uint8_t *e, uint8_t *send)
{
    uint32_t r = (uint32_t)e;
    uint8_t *cfg = g_cfg;
    uint8_t c10 = cfg[10];

    /* Button remaps. */
    if ((c10 & 2) && (e[9] & 15) == 9) {
        e[9] = (uint8_t)((e[9] & ~3) | 2);
        *send = 1;
    }
    if (c10 & 8) {
        uint8_t d7 = e[10];
        if ((d7 & 3) == 2 && (e[9] & 48) == 16) {
            e[9] = (uint8_t)((e[9] & ~48) | 32);
            *send = 1;
        }
        if ((e[9] & 192) == 128 && (d7 & 192) == 64) {
            e[10] = (uint8_t)((e[10] & 63) | 0x80);
            *send = 1;
        }
    }

    /* data[0] bit 2 feature. */
    if (g_bf2f != 0) {
        if (!(e[3] & 4)) {
            g_bf2f = 0;
            if (g_bf32)
                r = bf32_horn_set(0);
        } else if (g_bf2f > 5) {
            if (g_bf32)
                goto use_ec86;
        } else {
            uint8_t n = (uint8_t)(g_bf2f + 1);
            g_bf2f = n;
            if (n <= 5) {
                if (c10 & 0x30) {
                    r = speed_get();
                    int32_t lim = (int32_t)(((g_cfg[10] >> 4) & 3) * 10);
                    if (lim >= (int32_t)r) {
                        e[3] &= ~4;
                        *send = 1;
                    }
                }
            } else {
                r = 1;
                if (cfg[3] & 0x20)
                    r = bf32_horn_set(1);
            }
        }
    } else if (g_bf31 || g_bf30 || g_bf32) {
use_ec86:
        r = horn_level() & 1;
        e[3] = (uint8_t)((e[3] & ~4) | (r << 2));
        *send = 1;
    } else {
        if (e[3] & 4) {
            r = c250_is_2();
            if (r == 0) {
                g_bf2f = 1;
                if (c10 & 0x30) {
                    r = speed_get();
                    uint8_t c = g_cfg[10];
                    int32_t lim = (int32_t)(((c >> 4) & 3) * 10);
                    if (lim >= (int32_t)r) {
                        horn_chirps_start((uint32_t)(c >> 6) + 1, 1);
                        *send = 1;
                        e[3] &= ~4;
                    }
                }
                goto code15;
            }
        }
        if (g_bf33) {
            e[3] |= 4;
            *send = 1;
        }
    }

code15:
    /* Variants 7-8: gesture 15 goes in the page-0 frame. */
    if (g_gesture_code == 15 && (uint8_t)(g_variant - 7) <= 1) {
        g_gesture_code = 0;
        *send = 1;
        g_bf1d ^= 1;
        e[6] = (uint8_t)((e[6] & 63) | 0x80);
    }
    return r;
}

/* Page 1, first table: the tick bytes of gesture codes 1..18. */
static void tick_bytes(uint8_t *e, uint8_t code)
{
    switch (code) {
    case 1:  e[5] = (uint8_t)((e[5] & 0xC0) | 1); break;     /* left wheel +1 */
    case 2:  e[3] = (uint8_t)((e[3] & ~0x18) | 0x10); break;
    case 3:  e[5] |= 63; break;                               /* left wheel -1 */
    case 4:  e[4] = (uint8_t)((e[4] & 63) | 0x80); break;
    case 5:  e[3] = (uint8_t)((e[3] & ~0x60) | 0x40); break;
    case 9:  e[6] = (uint8_t)((e[6] & 0xC0) | 1); break;     /* right wheel +1 */
    case 10: e[4] = (uint8_t)((e[4] & ~12) | 8); break;
    case 11: e[6] |= 63; break;                               /* right wheel -1 */
    case 12: e[4] = (uint8_t)((e[4] & ~3) | 2); break;
    case 13: e[4] = (uint8_t)((e[4] & ~0x30) | 0x20); break;
    case 17: e[6] = (uint8_t)((e[6] & 0xC0) | 5); break;     /* right wheel +5 */
    case 18: e[6] = (uint8_t)((e[6] & 0xC0) | 59); break;    /* right wheel -5 */
    default: break;
    }
}

/* Page 1, variants 7-8: codes 6, 7, 8, 14 and 16. */
static void bytes_v78(uint8_t *e, uint8_t code)
{
    switch (code) {
    case 6:  e[9] |= 1; break;
    case 7:  e[9] |= 4; break;
    case 8:  e[9] |= 16; break;
    case 14: e[9] |= 64; break;
    case 16: e[8] |= 64; e[10] |= 96; break;
    default: break;
    }
}

/* Page 1, variants 5, 6 and 9: codes 6, 7, 8, 14 and 16. */
static void bytes_v569(uint8_t *e, uint8_t code)
{
    switch (code) {
    case 6:  e[6] = (uint8_t)((e[6] & 63) | 0x80); break;
    case 7:  e[8] = (uint8_t)((e[8] & ~0x30) | 0x20); break;
    case 8:  e[8] = (uint8_t)((e[8] & 63) | 0x80); break;
    case 14: e[9] = (uint8_t)((e[9] & ~3) | 2); break;
    case 16: e[5] = (uint8_t)((e[5] & 63) | 0x80); break;
    default: break;
    }
}

/* Page 1 of can_3c2_swc. */
static uint32_t edit_page1(uint8_t *e, uint8_t *send)
{
    if (g_bf2d) {
        *send = 1;
        g_bf2d = (uint8_t)(g_bf2d - 1);
        e[3] = (uint8_t)((e[3] & ~0x60) | 0x40);
    }
    if (g_bf2e) {
        g_bf2e = (uint8_t)(g_bf2e - 1);
        e[3] = (uint8_t)((e[3] & ~0x60) | 0x40);
        e[4] = (uint8_t)((e[4] & ~0x30) | 0x20);
        *send = 1;
    }

    if (g_20004186) {
        uint32_t r = flash_step(send);
        if (r) {
            if ((uint8_t)(g_variant - 7) <= 1) {
                e[9] = (uint8_t)((e[9] & ~0x10) | ((r == 1) << 4));
            } else {
                uint32_t k = (r == 1) ? 2 : 1;
                e[8] = (uint8_t)((k << 6) | (e[8] & 63));
            }
            if (flash_active_get())
                app_timer_start(resend_frame10_cb, 0, 50, 0);
        }
        if (g_bf34) {
            g_bf34 = 0;
            uint8_t v = g_variant;
            if ((g_cfg[7] & 0x30) == 0x20 && (uint8_t)(v - 1) > 1 &&
                (g_2000c1f4 & 2) && (g_2000c28e_b & 0x30) == 0x30) {
                if ((uint8_t)(v - 7) <= 1)
                    e[9] |= 16;
                else
                    e[8] = (uint8_t)((e[8] & 63) | 0x80);
                *send = 1;
            }
        }
    }

    uint8_t code = g_gesture_code;
    if (code == 0)
        goto step;

    uint8_t v = g_variant;
    if ((uint8_t)(code - 1) <= 17) {
        if (code == 15) {
            if ((uint8_t)(v - 7) <= 1) {
                /* Variants 7-8 send code 15 on page 0. Keep the code. */
                bytes_v78(e, g_gesture_code);
                goto step;
            }
            g_bf1d ^= 1;
            e[9] = (uint8_t)((e[9] & ~12) | 8);
            goto other;
        }
        tick_bytes(e, code);
    }
    if ((uint8_t)(v - 7) <= 1) {
        bytes_v78(e, g_gesture_code);
        goto clear;
    }
other:
    if ((uint8_t)(v - 5) <= 1 || v == 9)
        bytes_v569(e, g_gesture_code);
clear:
    g_gesture_code = 0;
    *send = 1;
step:
    return scroll_gesture(0);
}

/* Stock 0xBA7E. 0x3C2 frame, second handler: edits the frame and sends it
   again when it changed. */
uint32_t can_3c2_swc(uint8_t *e)
{
    uint8_t send = 0;   /* stock: a byte on the stack, flash_step can set it */
    uint32_t r;
    uint8_t mux = e[3] & 3;

    if (mux == 0)
        r = edit_page0(e, &send);
    else if (mux == 1)
        r = edit_page1(e, &send);
    else
        r = (uint32_t)e;
    if (send)
        r = can_tx(e);
    return r;
}
