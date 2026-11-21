/* Platform auto-detect and the frame handlers of platforms 2..6
   (stock 0x1024C..0x10928).

   g_platform (0x2000BFD1): 0 = auto-detect, 1 = Tesla Model 3/Y, 2..6 = other
   cars. In auto-detect, platform_dispatch sends every frame to
   platform_detect. A run of good frames of one kind selects the platform.
   On platforms 1..6, each good frame restarts a 2 s timer. When it fires,
   the platform goes back to 0 (auto-detect).

   Platforms 2..6 edit and resend some frames while the "active" flag
   0x2000BFD4 is set (active_start sets it for N seconds). They also map
   steering-wheel buttons (0xA9, 0x37A, 0x3EA) to key codes 0x53..0x57
   (0x52 = no key) for the key handler key_track. */
#include "tsl6.h"
#include "m5.h"

extern uint8_t g_platform;      /* 0x2000BFD1 */
extern uint8_t g_2000bfd0;      /* u8: good Tesla frames counted (car_frames_seen: > 3) */
extern uint8_t g_2000bfd4;      /* u8: platforms 2..6 "active" flag (12-byte block) */
extern uint8_t g_2000bfd8[8];   /* key handler state for key_track */
extern uint8_t g_2000bfe0;      /* u8: platform 5 saw 0x3EA */
extern uint8_t g_20004166;      /* u8: detect run count, Tesla (0x370, 0x3E9, 0x334) */
extern uint8_t g_20004167;      /* u8: detect run count, platform 6 (0x498) */
extern uint8_t g_20004168;      /* u8: detect run count, platform 4 (0x189) */
extern uint8_t g_20004169;      /* u8: detect run count, platform 5 (0x3A1) */
extern uint8_t g_2000416a;      /* u8: detect run count, platform 2 (0x21C) */
extern uint8_t g_2000416b;      /* u8: detect run count, platform 3 (0x21C) */
extern uint8_t g_2000416c;      /* u8: platform 6 0x21C counter (0..15) */

static const uint8_t tbl_21c_p6[2][17] = {  /* stock 0x19524 */
    { 0x09, 0xf5, 0x4c, 0x9a, 0x23, 0x2b, 0x92, 0x44,
      0xfd, 0x54, 0xed, 0x3b, 0x82, 0x8a, 0x33, 0xe5, 0x5c },
    { 0x0a, 0xd2, 0x6b, 0xbd, 0x04, 0x0c, 0xb5, 0x63,
      0xda, 0x73, 0xca, 0x1c, 0xa5, 0xad, 0x14, 0xc2, 0x7b },
};
static const uint8_t tbl_21c_p2[16] = {     /* stock 0x19514 */
    0xd2, 0x6b, 0xbd, 0x04, 0x0c, 0xb5, 0x63, 0xda,
    0x73, 0xca, 0x1c, 0xa5, 0xad, 0x14, 0xc2, 0x7b,
};
static const uint8_t tbl_3a1_a[16] = {      /* stock 0x19548 */
    0x22, 0x7f, 0x98, 0xc5, 0x4b, 0x16, 0xf1, 0xac,
    0xf0, 0xad, 0x4a, 0x17, 0x99, 0xc4, 0x23, 0x00,
};
static const uint8_t tbl_3a1_b[16] = {      /* stock 0x19558 */
    0x95, 0xc8, 0x2f, 0x72, 0xfc, 0xa1, 0x46, 0x1b,
    0x47, 0x1a, 0xfd, 0xa0, 0x2e, 0x73, 0x94, 0x00,
};
static const uint8_t tbl_3a1_c[16] = {      /* stock 0x19568 */
    0xfc, 0xa1, 0x46, 0x1b, 0x95, 0xc8, 0x2f, 0x72,
    0x2e, 0x73, 0x94, 0xc9, 0x47, 0x1a, 0xfd, 0x00,
};

/* ---- Frame checks for auto-detect. Each returns 1 for a good frame. ---- */

/* Stock 0x1024C. Platform 2 0x21C: data[0] is 9 or 10, data[1..5] are 0. */
uint32_t chk_21c_p2(m5_frame_t *f)
{
    if ((uint8_t)(f->data[0] - 9) > 1)
        return 0;
    return f->data[1] == 0 && f->data[2] == 0 && f->data[3] == 0 &&
           f->data[4] == 0 && f->data[5] == 0;
}

/* Stock 0x1027A. Platform 6 0x498: data[0..7] are 0. */
uint32_t chk_498_p6(m5_frame_t *f)
{
    if (f->data[0] != 0)
        return 0;
    return f->data[1] == 0 && f->data[2] == 0 && f->data[3] == 0 &&
           f->data[4] == 0 && f->data[5] == 0 && f->data[6] == 0 &&
           f->data[7] == 0;
}

/* Stock 0x102A2. Platform 3 0x21C: data[0] is 4. */
uint32_t chk_21c_p3(m5_frame_t *f)
{
    return f->data[0] == 4;
}

/* Stock 0x102AE. Platform 4 0x189: data[4..7] are 0. */
uint32_t chk_189_p4(m5_frame_t *f)
{
    if (f->data[4] != 0)
        return 0;
    return f->data[5] == 0 && f->data[6] == 0 && f->data[7] == 0;
}

/* Stock 0x102CA. Platform 5 0x3A1: data[1] <= 14 and data[2] is
   0x10, 0x20, 0x30 or 0xF4. */
uint32_t chk_3a1_p5(m5_frame_t *f)
{
    if (f->data[1] > 14)
        return 0;
    uint8_t v = f->data[2];
    if ((v & 0xDF) == 0x10 || v == 0x20)
        return 1;
    return v == 0xF4;
}

/* ---- Active flag and platform switch ---- */

/* Stock 0x102FA. Timer callback: ends the active time. */
M5_CALLED void active_off(void)
{
    g_2000bfd4 = 0;
    app_timer_stop(active_off);
}

/* Stock 0x1030E. Platform 6: 20 ms timer that sends 0x21C. data[0] and
   data[7] come from a table row: row 1 while active, else row 0. */
void p6_21c_tick(void)
{
    m5_frame_t f;
    const uint8_t *t = tbl_21c_p6[g_2000bfd4 != 0];
    uint8_t n = g_2000416c;

    f.id = 0x21C;
    f.dlc = 8;
    f.data[0] = t[0];
    f.data[1] = 0;
    f.data[2] = 0;
    f.data[3] = 0;
    f.data[4] = 0;
    f.data[5] = 0;
    f.data[6] = (uint8_t)(n << 4);
    f.data[7] = t[n + 1];
    f.flag = 0;
    g_2000416c = (n + 1) & 0x0F;
    can_tx(&f);
}

/* Stock 0x1037C. Tesla: data[7] is the checksum (frame_checksum_last). */
M5_CALLED uint32_t chk_tesla_sum(m5_frame_t *f)
{
    return f->data[7] == frame_checksum_last(f);
}

/* Stock 0x1039C. Timer callback: key release. */
M5_CALLED void key_release_cb(void)
{
    key_track(0x52, 0x52, g_2000bfd8);
}

/* Stock 0x103B0. Tesla 0x334 with a good checksum: counts up to "car seen". */
void tesla_seen_count(void)
{
    if (car_frames_seen() == 0)
        g_2000bfd0 = g_2000bfd0 + 1;
}

/* Stock 0x103D2. Stops the platform 2..6 state (can_filter_platform calls it). */
void platform_stop(void)
{
    req_678_clear();
    active_off();
    tmos_memset(&g_2000bfd4, 0, 12);
    led_off();
    app_timer_stop(led_toggle);
    app_timer_stop(platform_timeout);
}

/* Stock 0x10414. Selects a platform: sets the CAN filters (can_filter_platform),
   starts the LED blink timer led_toggle (not for 0), and on platform 6 the
   20 ms 0x21C timer. */
M5_CALLED void platform_set(uint32_t platform)
{
    vin_clear();
    bus_mon_init();
    bus_age_clear(0);
    req_678_clear();
    g_2000bfd0 = 0;
    g_platform = (uint8_t)platform;
    can_filter_platform((uint8_t)platform);
    if (platform != 0)
        app_timer_start(led_toggle, 1, 500, 0);
    status_push(0);
    if (platform == 6)
        app_timer_start(p6_21c_tick, 1, 20, 0);
}

/* Stock 0x10488. Timer callback: no good frame for 2 s, back to auto-detect. */
M5_CALLED void platform_timeout(void)
{
    platform_set(0);
}

/* Stock 0x1048E. One detect step: a good frame counts the run up; at
   max good frames in a row it selects the platform. A bad frame ends the run. */
M5_CALLED void detect_step(uint8_t *run, uint32_t max, uint32_t platform,
                         uint32_t (*check)(m5_frame_t *), m5_frame_t *f)
{
    if (check(f) == 0) {
        *run = 0;
        return;
    }
    uint8_t n = (uint8_t)(*run + 1);
    if (n < max) {
        *run = n;
        return;
    }
    *run = 0;
    bus_age_clear(0);
    platform_set(platform);
}

/* Stock 0x104DE. Sets the active flag for secs seconds (0 = 1 s). */
void active_start(uint32_t secs)
{
    if (secs == 0)
        secs = 1;
    g_2000bfd4 = 1;
    app_timer_start(active_off, 0, secs * 1000, 0);
}

/* Stock 0x10504. Checks a frame. A good frame restarts the 2 s timer that
   returns to auto-detect. Returns the check result. */
M5_CALLED uint32_t platform_check(uint32_t (*check)(m5_frame_t *), m5_frame_t *f)
{
    uint32_t ok = check(f);

    if (ok) {
        bus_age_clear(0);
        app_timer_start(platform_timeout, 0, 2000, 0);
    }
    return ok;
}

/* ---- Frame handlers of platforms 2..6 (from platform_dispatch) ---- */

/* Stock 0x10538. 0xA9 (platforms 2, 3, 4, 6): buttons in data[0] and data[1].
   A key press starts a 600 ms timer that sends the release. */
void key_a9(m5_frame_t *f)
{
    uint8_t b = f->data[0];
    uint32_t key;

    if (b & 0x10) {
        key = 0x53;
    } else if (b & 0x04) {
        key = 0x54;
    } else if (b & 0x40) {
        key = 0x55;
    } else if (b & 0x01) {
        key = 0x56;
    } else if (f->data[1] & 1) {
        key = 0x57;
    } else {
        key_track(0x52, 0x52, g_2000bfd8);
        return;
    }
    app_timer_start(key_release_cb, 0, 600, 0);
    key_track(key, 0x52, g_2000bfd8);
}

/* Stock 0x105AC. Platform 2 0x21C: while active, sends a copy with
   data[0] = 10, data[6] counter + 1 (bits 7:4) and data[7] from a table. */
void p2_21c(m5_frame_t *f)
{
    if (platform_check(chk_21c_p2, f) == 0 || g_2000bfd4 == 0)
        return;
    f->data[0] = 10;
    uint8_t c = (uint8_t)(f->data[6] + 0x10);
    f->data[6] = c;
    f->data[7] = tbl_21c_p2[c >> 4];
    can_tx(f);
}

/* Stock 0x1060A. Platform 6 0x498: only the check (keeps the platform). */
void p6_498(m5_frame_t *f)
{
    platform_check(chk_498_p6, f);
}

/* Stock 0x10618. Platform 3 0x21C: while active, sends a copy with
   200 added to the big-endian words data[1..2] and data[3..4], and 25
   added to data[5]. */
void p3_21c(m5_frame_t *f)
{
    if (platform_check(chk_21c_p3, f) == 0 || g_2000bfd4 == 0)
        return;
    uint32_t a = ((f->data[1] << 8) | f->data[2]) + 200;
    uint32_t b = ((f->data[3] << 8) | f->data[4]) + 200;
    f->data[1] = (uint8_t)(a >> 8);
    f->data[2] = (uint8_t)a;
    f->data[3] = (uint8_t)(b >> 8);
    f->data[4] = (uint8_t)b;
    f->data[5] = (uint8_t)(f->data[5] + 25);
    can_tx(f);
}

/* Stock 0x10698. Platform 4 0x189: while active, sends a copy with data[0] = 2. */
void p4_189(m5_frame_t *f)
{
    if (platform_check(chk_189_p4, f) == 0 || g_2000bfd4 == 0)
        return;
    f->data[0] = 2;
    can_tx(f);
}

/* Stock 0x106D4. Platform 4 0x68C: while active, sends a copy with data[3] = 8. */
void p4_68c(m5_frame_t *f)
{
    if (g_2000bfd4 == 0)
        return;
    f->data[3] = 8;
    can_tx(f);
}

/* Stock 0x106EA. Platform 5 0x3A1: while active, sends a copy with the next
   counter (data[1] + 1) % 15, data[2] = 0x30 and data[0] from a table.
   After a 0x3EA frame, data[3..4] = 0x2B 0x2E and data[5..7] = 0. Else, if
   data[3..4] are not 0, data[3..4] = 0x5D 0x59 and data[5..7] = 0. */
void p5_3a1(m5_frame_t *f)
{
    if (platform_check(chk_3a1_p5, f) == 0 || g_2000bfd4 == 0)
        return;
    uint8_t n = (uint8_t)((int32_t)(f->data[1] + 1) % 15);
    f->data[2] = 0x30;
    f->data[1] = n;
    if (g_2000bfe0 != 0) {
        f->data[0] = tbl_3a1_a[n];
        f->data[3] = 0x2B;
        f->data[4] = 0x2E;
    } else if (f->data[3] == 0 && f->data[4] == 0) {
        f->data[0] = tbl_3a1_b[n];
        goto send;
    } else {
        f->data[0] = tbl_3a1_c[n];
        f->data[3] = 0x5D;
        f->data[4] = 0x59;
    }
    f->data[5] = 0;
    f->data[6] = 0;
    f->data[7] = 0;
send:
    can_tx(f);
}

/* Stock 0x1079A. Platform 5 0x37A: buttons to key codes. */
void key_37a(m5_frame_t *f)
{
    uint8_t b0 = f->data[0];
    uint32_t key;

    if (b0 & 0x40)
        key = 0x53;
    else if (f->data[1] & 0x20)
        key = 0x54;
    else if (b0 & 0x80)
        key = 0x55;
    else if (f->data[1] & 0x10)
        key = 0x56;
    else if (b0 & 0x10)
        key = 0x57;
    else
        key = 0x52;
    key_track(key, 0x52, g_2000bfd8);
}

/* Stock 0x107EA. Platform 5 0x3EA: buttons to key codes. Also sets the
   "0x3EA seen" flag. */
void key_3ea(m5_frame_t *f)
{
    uint8_t b1 = f->data[1];
    uint32_t key;

    g_2000bfe0 = 1;
    if (b1 & 0x40) {
        key = 0x57;
    } else {
        uint8_t b2 = f->data[2];
        if (b2 & 1)
            key = 0x56;
        else if (b2 & 2)
            key = 0x54;
        else if ((b2 & 4) == 0)
            key = 0x52;
        else if (b1 & 0x10)
            key = 0x53;
        else if (b1 & 0x20)
            key = 0x55;
        else
            key = 0x52;
    }
    key_track(key, 0x52, g_2000bfd8);
}

/* Stock 0x1084C. Auto-detect (platform 0): one detect step per known id. */
void platform_detect(m5_frame_t *f)
{
    switch (f->id) {
    case 0x370:
    case 0x334:
    case 0x3E9:
        detect_step(&g_20004166, 3, 1, chk_tesla_sum, f);
        break;
    case 0x21C:
        detect_step(&g_2000416a, 10, 2, chk_21c_p2, f);
        detect_step(&g_2000416b, 10, 3, chk_21c_p3, f);
        break;
    case 0x189:
        detect_step(&g_20004168, 10, 4, chk_189_p4, f);
        break;
    case 0x498:
        detect_step(&g_20004167, 4, 6, chk_498_p6, f);
        break;
    case 0x3A1:
        detect_step(&g_20004169, 10, 5, chk_3a1_p5, f);
        break;
    default:
        break;
    }
}

/* Stock 0x10912. Clears 0x2000BFD0..0x2000BFE3. */
void platform_clear(void)
{
    tmos_memset(&g_2000bfd0, 0, 20);
}

/* Stock 0x10928. Empty callback (third entry of the central callback
   table at 0x196A4). */
void central_cb_empty(void)
{
}
