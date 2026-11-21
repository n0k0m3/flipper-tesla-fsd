/* 0x3E9 echo (stock 0xF834..0xFA56). The module sends a copy of the 0x3E9
   frame (AP body controls) with these fields changed:
     data[0] bits 1:0  headlight request (headlight_req_set mode: 1 = on, 2 = off)
     data[0] bits 3:2  high beam flash (g_20004144)
     data[0] bits 7:4  cleared by "Turn off auto wipers" (config byte 1 bit 7)
     data[1] bits 4:3, data[3] bit 0  "Manual high beam" = 2 (config byte 7)
     data[2] bit 6     g_20004145
   data[6] bits 7:4 is a counter and the last byte is the checksum. */
#include "tsl6.h"
#include "m4.h"

/* Upper nibble of data[6] + 1 (counter of the frame). */
#define COUNTER_INC(f) \
    ((f)->data[6] = (uint8_t)((((f)->data[6] & 0xf0) + 0x10) | ((f)->data[6] & 0x0f)))

/* Stock 0xF834 (timer callback, not in the Ghidra function list).
   High beam on, checked every 1.5 s, until hazard_state gives 1 or 7. */
void high_beam_hold_cb(uint8_t *arg)
{
    uint8_t st = hazard_state();

    if (st == 1 || st == 7) {
        app_timer_stop(high_beam_flash_cb);
        return;
    }
    g_20004144 = 1;
    app_timer_start(high_beam_hold_cb, 0, 1500, 0);
}

/* Stock 0xF874 (timer callback). While hazard_state gives 7: one high beam
   flash every 1.5 s. Else stop the flash and the hold timer. */
void high_beam_flash_cb(uint8_t *arg)
{
    if (hazard_state() == 7) {
        g_20004144 = 2;
        g_20004147 = 0;
        app_timer_start(high_beam_flash_cb, 0, 1500, 0);
        return;
    }
    g_20004144 = 0;
    app_timer_stop(high_beam_hold_cb);
}

/* Stock 0xF8B8. 0x3E9 handler (also called with the frame slot copy). */
void vc3e9_rx(m4_frame_t *f)
{
    uint8_t *p = (uint8_t *)f;
    uint8_t *cfg;
    uint32_t changed = 0;

    if (g_20004146 == 1) {
        if ((f->data[0] & 3) != 1) {
            f->data[0] = (f->data[0] & 0xfc) | 1;
            changed = 1;
        }
    } else if (g_20004146 == 2) {
        if ((f->data[0] & 3) != 0 && !ap_active()) {
            f->data[0] &= 0xfc;
            changed = 1;
        }
    }
    cfg = g_cfg;
    if ((cfg[7] & 0x30) == 0x20 && (uint8_t)(g_20004188 - 1) > 1) {
        if (g_2000c1d4[2].data[5] & 2)
            f->data[1] = (f->data[1] & 0xe7) | 0x10;
        f->data[3] &= 0xfe;
        changed = 1;
    }
    if ((int8_t)cfg[1] < 0) {
        f->data[0] &= 0x0f;
        changed = 1;
    }
    switch (g_20004144) {
    case 2:
        f->data[0] = (f->data[0] & 0xf3) | 4;
        g_20004144 = 3;
        app_timer_start(bodyctl_resend, 0, 20, 0);
        changed = 1;
        break;
    case 3:
        /* The counter steps here and again below: +2 in this frame. */
        g_20004144 = 0;
        f->data[0] &= 0xf3;
        COUNTER_INC(f);
        changed = 1;
        break;
    case 1:
        f->data[0] = (f->data[0] & 0xf3) | 4;
        changed = 1;
        break;
    default:
        break;
    }
    if (g_20004145 == 1) {
        f->data[2] |= 0x40;
    } else if (g_20004145 == 2) {
        /* Also a double counter step. */
        g_20004145 = 0;
        f->data[2] &= 0xbf;
        COUNTER_INC(f);
    } else if (!changed) {
        return;
    }
    COUNTER_INC(f);
    {
        int last = f->dlc - 1;
        p[3 + last] = (uint8_t)frame_checksum_last(f);
    }
    can_tx(f);
}
