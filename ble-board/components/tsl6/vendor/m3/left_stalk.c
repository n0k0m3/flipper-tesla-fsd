/* 0x249 left stalk (SCCM_leftStalk): decoder, frame edit and the actions
   that inject stalk moves (turn signal, high beam, headlight flasher).
   Stock 0xC4AA..0xCB82.

   Frame layout (Model 3/Y, most variants):
     data[0]       checksum (from a table per payload and counter)
     data[1] 3:0   counter
     data[1] 5:4   high beam stalk: 1 = pull (flash), 2 = push
     data[1] 7:6   a button
     data[2] 3:0   turn signal stalk position
   Variants 6 and 9 use another layout (data[1] whole byte = counter). */
#include "tsl6.h"
#include "m3.h"

/* Checksums per counter (stock 0x18ED8): flasher frame (data[1] bits 5:4 = 2). */
static const uint8_t k_249_beam_crc[16] =
{0x77, 0x04, 0xc6, 0x3f, 0x3f, 0x6f, 0xa0, 0xb2, 0xd3, 0xb2, 0x0e, 0xc4, 0xd6, 0xff, 0x43, 0x22};

/* Stock 0x18EE8 and 0x18EF8: variants 6 and 9, turn signal codes 6/8 and 2/4. */
static const uint8_t k_249_v6_crc_b[16] =
{0x04, 0x77, 0xb5, 0x4c, 0x4c, 0x1c, 0xd3, 0xc1, 0xa0, 0xc1, 0x7d, 0xb7, 0xa5, 0x8c, 0x30, 0x51};
static const uint8_t k_249_v6_crc_a[16] =
{0x52, 0x21, 0xe3, 0x1a, 0x1a, 0x4a, 0x85, 0x97, 0xf6, 0x97, 0x2b, 0xe1, 0xf3, 0xda, 0x00, 0x07};

/* Stock 0x18F08: row = turn signal position (data[2] bits 3:0), + 10 when
   data[1] bits 5:4 are not 0. */
static const uint8_t k_249_crc[26][16] = {
    {0x9b, 0xe8, 0x2a, 0xd3, 0xd3, 0x83, 0x4c, 0x5e, 0x3f, 0x5e, 0xe2, 0x28, 0x3a, 0x13, 0xaf, 0xce},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x87, 0xf4, 0x36, 0xcf, 0xcf, 0x9f, 0x50, 0x42, 0x23, 0x42, 0xfe, 0x34, 0x26, 0x0f, 0xb3, 0xd2},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xa3, 0xd0, 0x12, 0xeb, 0xeb, 0xbb, 0x74, 0x66, 0x07, 0x66, 0xda, 0x10, 0x02, 0x2b, 0x97, 0xf6},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xbf, 0xcc, 0x0e, 0xf7, 0xf7, 0xa7, 0x68, 0x7a, 0x1b, 0x7a, 0xc6, 0x0c, 0x1e, 0x37, 0x8b, 0xea},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xeb, 0x98, 0x5a, 0xa3, 0xa3, 0xf3, 0x3c, 0x2e, 0x4f, 0x2e, 0x92, 0x58, 0x4a, 0x63, 0xdf, 0xbe},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xed, 0x9e, 0x5c, 0xa5, 0xa5, 0xf5, 0x3a, 0x28, 0x49, 0x28, 0x94, 0x5e, 0x4c, 0x65, 0xd9, 0xb8},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xf1, 0x82, 0x40, 0xb9, 0xb9, 0xe9, 0x26, 0x34, 0x55, 0x34, 0x88, 0x42, 0x50, 0x79, 0xc5, 0xa4},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xd5, 0xa6, 0x64, 0x9d, 0x9d, 0xcd, 0x02, 0x10, 0x71, 0x10, 0xac, 0x66, 0x74, 0x5d, 0xe1, 0x80},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xc9, 0xba, 0x78, 0x81, 0x81, 0xd1, 0x1e, 0x0c, 0x6d, 0x0c, 0xb0, 0x7a, 0x68, 0x41, 0xfd, 0x9c},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x9d, 0xee, 0x2c, 0xd5, 0xd5, 0x85, 0x4a, 0x58, 0x39, 0x58, 0xe4, 0x2e, 0x3c, 0x15, 0xa9, 0xc8},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x6c, 0x1f, 0xdd, 0x24, 0x24, 0x74, 0xbb, 0xa9, 0xc8, 0xa9, 0x15, 0xdf, 0xcd, 0xe4, 0x58, 0x39},
    {0x5a, 0x29, 0xeb, 0x12, 0x12, 0x42, 0x8d, 0x9f, 0xfe, 0x9f, 0x23, 0xe9, 0xfb, 0xd2, 0x6e, 0x0f},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1a, 0x69, 0xab, 0x52, 0x52, 0x02, 0xcd, 0xdf, 0xbe, 0xdf, 0x63, 0xa9, 0xbb, 0x92, 0x2e, 0x4f},
    {0x2c, 0x5f, 0x9d, 0x64, 0x64, 0x34, 0xfb, 0xe9, 0x88, 0xe9, 0x55, 0x9f, 0x8d, 0xa4, 0x18, 0x79},
};

/* Stock 0x19048: row = data[1] bits 7:6, + 3 when data[1] bits 5:4 are not 0.
   Row 6 is past the end of the stock table (it reads the first row of the
   table at 0x190A8); it is copied here so that the result is the same. */
static const uint8_t k_249_btn_crc[7][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x6c, 0x1f, 0xdd, 0x24, 0x24, 0x74, 0xbb, 0xa9, 0xc8, 0xa9, 0x15, 0xdf, 0xcd, 0xe4, 0x58, 0x39},
    {0x5a, 0x29, 0xeb, 0x12, 0x12, 0x42, 0x8d, 0x9f, 0xfe, 0x9f, 0x23, 0xe9, 0xfb, 0xd2, 0x6e, 0x0f},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1a, 0x69, 0xab, 0x52, 0x52, 0x02, 0xcd, 0xdf, 0xbe, 0xdf, 0x63, 0xa9, 0xbb, 0x92, 0x2e, 0x4f},
    {0x2c, 0x5f, 0x9d, 0x64, 0x64, 0x34, 0xfb, 0xe9, 0x88, 0xe9, 0x55, 0x9f, 0x8d, 0xa4, 0x18, 0x79},
    {0x46, 0x44, 0x52, 0x6d, 0x43, 0x41, 0xdd, 0xf9, 0x4c, 0xa5, 0xf6, 0x8c, 0x49, 0x2f, 0x31, 0x3b},
};

/* Stock 0xC4AA. Turns the headlight flasher on or off. Off also resets the
   0x3B3 edit (0xF80E(0)) and sends the cached 0x3B3 frame once
   (config byte 1 bit 2). */
void flash_set(uint32_t on)
{
    m3_frame_t buf;

    g_flash_active = (uint8_t)on;
    if (on)
        return;
    vc3b3_mode_set(0);
    if ((g_cfg[1] & 4) == 0)
        return;
    frame_slot_copy(1, 8, &buf);
    if (buf.dlc)
        can_tx(&buf);
}

/* Stock 0xC4E8. One flasher step per 0x249 (and 0x3C2) frame.
   Returns 0 when the flasher and the high beam are off. Otherwise sets *dirty
   and returns 1 (high beam phase) or 2 (flash phase). The phase counts modulo
   (config byte 1 bits 1:0) + 1, at least 2. With config byte 1 bit 2 and AP
   off, it also edits and sends the cached 0x3B3 frame. */
uint32_t flash_step(uint8_t *dirty)
{
    uint32_t cfg;
    uint32_t mod;
    uint32_t ph;
    m3_frame_t buf;

    if (!g_flash_active) {
        if (!g_highbeam_on)
            return 0;
        *dirty = 1;
        return 1;
    }
    cfg = g_cfg[1];
    mod = (cfg & 3) ? (cfg & 3) + 1 : 2;
    ph = (g_flash_phase + 1u) % mod;
    g_flash_phase = (uint8_t)ph;
    if ((cfg & 4) && !ap_active()) {
        frame_slot_copy(1, 8, &buf);
        if (buf.dlc) {
            vc3b3_mode_set((ph != 0) + 1);
            vc3b3_rx(&buf);
        }
    }
    *dirty = 1;
    if (ph != 0)
        return 2;
    if (g_flash_repeat) {
        g_flash_repeat--;
        if (g_flash_repeat == 0)
            flash_set(0);
    }
    return 1;
}

/* Stock 0xC5A2. 0x249 handler.
   Variants 6 and 9: data[3] bits 1:0 = 1 or 2 (turn signal) runs 0xA5A4 and
   0xFB80(1). Others: high beam stalk (data[1] bits 5:4) gives button codes
   0x43..0x46 (0x43 idle); a turn signal (data[2] bits 3:0) runs 0xA5A4 and
   0xFB80(1); 0xEA50 gets the high beam stalk value. */
void can_249_rx(m3_frame_t *f)
{
    uint32_t v = g_20004188;

    if (v == 6 || v == 9) {
        if ((uint8_t)((f->data[3] & 3) - 1) <= 1) {
            lane_change_arm();
            hazard_auto_update(1);
        }
        return;
    }
    key_track(((f->data[1] >> 4) & 3) + 0x43, 0x43, g_lstalk_btn);
    if (f->data[2] & 0xF) {
        lane_change_arm();
        hazard_auto_update(1);
    }
    high_beam_links_horn((f->data[1] >> 4) & 3);
}

/* High beam condition: config byte 7 bits 5:4 = 2, variant not 1 or 2,
   cached 0x273 data[5] bit 1 and cached 0x3F5 data[3] bits 5:4 = 3. */
static uint32_t beam_allowed(void)
{
    return (g_cfg[7] & 0x30) == 0x20
        && (uint8_t)(g_20004188 - 1) > 1
        && (g_2000c1f4 & 2)
        && (g_2000c28e & 0x30) == 0x30;
}

/* Variants 6 and 9: write the turn signal code into the other frame layout. */
static void can_249_edit_v6(m3_frame_t *f)
{
    uint32_t n = g_lstalk_frames;
    uint32_t ctr;
    uint32_t code;

    if (n == 0)
        return;
    ctr = (f->data[1] + 1) & 0xF;
    f->data[1] = (uint8_t)ctr;
    code = g_lstalk_code;
    if (code == 2 || code == 4) {
        f->data[0] = k_249_v6_crc_a[ctr];
        f->data[2] = 0xE2;
        f->data[3] = 0x72;
    } else if (code == 6 || code == 8) {
        f->data[0] = k_249_v6_crc_b[ctr];
        f->data[2] = 0xD6;
        f->data[3] = 0x6D;
    }
    g_lstalk_frames = (uint8_t)(n - 1);
    can_tx(f);
}

/* Stock 0xC624. 0x249 edit. Puts the button request, the turn signal code,
   the flasher and the high beam into the frame, fixes counter and checksum,
   and sends it. */
void can_249_edit(m3_frame_t *f)
{
    uint8_t dirty;
    uint32_t r;
    uint32_t beam = 0;
    uint32_t s;

    if (g_20004188 == 6 || g_20004188 == 9) {
        can_249_edit_v6(f);
        return;
    }
    dirty = 0;
    if (f->data[1] & 0xC0)
        manual_hold_start();    /* button pressed: 5 s manual scroll hold */
    if (g_lstalk_hi) {
        f->data[1] = (f->data[1] & 0x3F) | (uint8_t)(g_lstalk_hi << 6);
        g_lstalk_hi = 0;
        dirty = 1;
    }
    if (g_lstalk_frames) {
        g_lstalk_frames--;
        f->data[2] = (f->data[2] & 0xF0) | (g_lstalk_code & 0xF);
        dirty = 1;
    }
    r = flash_step(&dirty);
    if (r == 0) {
        s = g_beam_timer;
        if ((f->data[1] & 0x30) == 0x10) {
            /* Driver pulls the stalk. */
            if (s <= 4) {
                if (beam_allowed()) {
                    g_beam_timer = 5;
                    beam = 1;
                }
            } else if (s != 5) {
                g_beam_timer = 5;
            }
        } else if (s != 0) {
            s = (s + 1) & 0xFF;
            if (s > 14) {
                g_beam_timer = 0;
                beam = 1;
            } else {
                g_beam_timer = (uint8_t)s;
            }
        }
    } else if (beam_allowed()) {
        g_beam_timer = 15;
        beam = 1;
    } else {
        f->data[1] = (f->data[1] & 0xCF) | (uint8_t)((r == 1) << 4);
    }

    if (g_beam_req) {
        g_beam_req = 0;
        if (beam_allowed())
            beam = 1;
    }
    if (beam) {
        /* High beam push (data[1] bits 5:4 = 2). */
        uint32_t b = (f->data[1] & 0xCF) | 0x20;
        uint32_t ctr = (b + 1) & 0xF;
        f->data[1] = (uint8_t)((b & 0xF0) | ctr);
        f->data[0] = k_249_beam_crc[ctr];
        can_tx(f);
        return;
    }
    if (!dirty)
        return;
    uint32_t b = f->data[1];
    uint32_t ctr = (b + 1) & 0xF;
    b = (b & 0xF0) | ctr;
    f->data[1] = (uint8_t)b;
    if (b & 0xC0)
        f->data[0] = k_249_btn_crc[(b >> 6) + ((b & 0x30) ? 3 : 0)][ctr];
    else
        f->data[0] = k_249_crc[((b & 0x30) ? 10 : 0) + (f->data[2] & 0xF)][ctr];
    can_tx(f);
}

/* Stock 0xC924. Called on a shift to P (0x118). */
void beam_request(void)
{
    g_beam_req = 1;
}

/* Stock 0xC930. Flasher on for 3 flashes. */
void flash_start3(void)
{
    g_flash_active = 1;
    g_flash_phase = 0;
    g_flash_repeat = 3;
}

/* Stock 0xC946. */
void highbeam_set(uint8_t on)
{
    g_highbeam_on = on;
}

/* Stock 0xC950. */
uint8_t highbeam_get(void)
{
    return g_highbeam_on;
}

/* Stock 0xC95A. Sets the flasher (no flash limit). Off also clears the
   high beam toggle. beep != 0 gives the beep for on or off (0x9774). */
void flash_mode_set(uint32_t on, uint32_t beep)
{
    g_flash_repeat = 0;
    flash_set(on);
    if (!on)
        g_highbeam_on = 0;
    if (beep)
        buzzer_beep_onoff(on);
    status_push(0);
}

/* Stock 0xC990. Stops the flasher (and 0xF39C), or clears the high beam. */
void flash_cancel(void)
{
    if (g_flash_active) {
        flash_mode_set(0, 0);
        front_fog_strobe_set(0, 0);
        return;
    }
    if (g_highbeam_on)
        g_highbeam_on = 0;
}

/* Stock 0xC9C4. With AP off: flash_cancel. With AP on, config byte 7
   bits 5:4 = 1 and variant 1..4: toggles the high beam. */
void highbeam_toggle(void)
{
    if (!ap_active()) {
        flash_cancel();
        return;
    }
    if ((g_cfg[7] & 0x30) == 0x10 && (uint8_t)(g_20004188 - 1) <= 3)
        g_highbeam_on ^= 1;
}

/* Stock 0xCA12. */
uint8_t flash_active_get(void)
{
    return g_flash_active;
}

/* Stock 0xCA1C. */
void lstalk_inject(uint8_t code, uint8_t frames)
{
    g_lstalk_code = code;
    g_lstalk_frames = frames;
}

/* Stock 0xCA2E. Turn signal action. dir 0, 1 or 2; full != 0 for the long
   variant. sel = 0x81DE (a setting: 1 or 2 fixes the side).
   Variants 1, 2, 3, 4, 6, 9 (stalk cars) inject the stalk position (codes
   2, 4, 6, 8) for 4 frames of 0x249. The other variants (no stalk) use the
   steering-wheel gesture codes 6 and 7 (0xB3E2). */
void turn_signal(uint32_t full, uint32_t dir)
{
    uint32_t sel = turn_state_get();
    uint32_t var = g_20004188;
    uint32_t g;
    uint32_t follow;

    if (var <= 9 && ((0x25Eu >> var) & 1)) {
        g_lstalk_code = 0;
        if (dir == 1) {
            if (sel == 1) {
                if (full)
                    g_lstalk_code = 4;
            } else if (sel == 2) {
                if (!full)
                    g_lstalk_code = 8;
            } else {
                g_lstalk_code = full ? 4 : 8;
            }
        } else if (dir == 0) {
            if (sel == 1)
                g_lstalk_code = 6;
            else if (sel == 2)
                g_lstalk_code = 2;
        } else if (dir == 2) {
            if (sel == 1)
                g_lstalk_code = full ? 4 : 6;
            else if (sel == 2)
                g_lstalk_code = full ? 2 : 8;
            else
                g_lstalk_code = full ? 4 : 8;
        }
        uint32_t code = g_lstalk_code;
        if (code == 0)
            return;
        g_lstalk_frames = 4;
        if (code == 4 || code == 8) {
            lane_change_arm();
            hazard_auto_update(1);
        }
        return;
    }

    if (dir == 1) {
        if (sel == 1) {
            if (!full)
                return;
            g = 7;
            follow = full;
        } else if (sel == 2) {
            if (full)
                return;
            g = 6;
            follow = 1;
        } else if (full) {
            g = 7;
            follow = full;
        } else {
            g = 6;
            follow = 1;
        }
    } else if (dir == 0 || dir == 2) {
        if (sel == 1) {
            g = 6;
            follow = 0;
        } else if (sel == 2) {
            g = 7;
            follow = 0;
        } else if (dir == 0) {
            return;
        } else if (full) {
            g = 7;
            follow = full;
        } else {
            g = 6;
            follow = 1;
        }
    } else {
        return;
    }
    gesture_start(g);
    if (follow) {
        lane_change_arm();
        hazard_auto_update(1);
    }
}

/* Stock 0xCB4A. Presses the left stalk button: data[1] bits 7:6 = 1 on stalk
   variants 1..4, else steering-wheel gesture code 16. */
void lstalk_button(void)
{
    if ((uint8_t)(g_20004188 - 1) <= 3) {
        g_lstalk_hi = 1;
        return;
    }
    gesture_start(16);
}

/* Stock 0xCB6C. Clears 0x2000BF44..0x2000BF53. */
void lstalk_clear(void)
{
    tmos_memset(&g_beam_req, 0, 16);
}
