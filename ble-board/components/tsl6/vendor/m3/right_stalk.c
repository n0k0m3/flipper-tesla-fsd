/* 0x229 right stalk (gear selector) and 0x118 drive state (gear, pedal).
   Stock 0xCB82..0xCDC6, plus the park timer 0xD216 and 0xD356.

   0x229 frame: data[0] checksum, data[1] bits 3:0 counter, data[1] bits 6:4
   stalk position, data[2] bits 1:0 park button. */
#include "tsl6.h"
#include "m3.h"

/* Checksums per counter (stock 0x190A8). Row = stalk position (0..4);
   row 5 is the park button. Rows past 5 are past the end of the stock table
   (not reached: callers use rows 2..5). */
static const uint8_t k_229_crc[6][16] = {
    {0x46, 0x44, 0x52, 0x6d, 0x43, 0x41, 0xdd, 0xf9, 0x4c, 0xa5, 0xf6, 0x8c, 0x49, 0x2f, 0x31, 0x3b},
    {0xa6, 0xa4, 0xb2, 0x8d, 0xa3, 0xa1, 0x3d, 0x19, 0xac, 0x45, 0x16, 0x6c, 0xa9, 0xcf, 0xd1, 0xdb},
    {0xa9, 0xab, 0xbd, 0x82, 0xac, 0xae, 0x32, 0x16, 0xa3, 0x4a, 0x19, 0x63, 0xa6, 0xc0, 0xde, 0xd4},
    {0x49, 0x4b, 0x5d, 0x62, 0x4c, 0x4e, 0xd2, 0xf6, 0x43, 0xaa, 0xf9, 0x83, 0x46, 0x20, 0x3e, 0x34},
    {0xb7, 0xb5, 0xa3, 0x9c, 0xb2, 0xb0, 0x2c, 0x08, 0xbd, 0x54, 0x07, 0x7d, 0xb8, 0xde, 0xc0, 0xca},
    {0xaf, 0xad, 0xbb, 0x84, 0xaa, 0xa8, 0x34, 0x10, 0xa5, 0x4c, 0x1f, 0x65, 0xa0, 0xc6, 0xd8, 0xd2},};

/* Stock 0xCB82. 0x229 handler: stalk position gives button codes 0x39..0x40
   (0x39 idle). */
void can_229_rx(m3_frame_t *f)
{
    key_track(((f->data[1] >> 4) & 7) + 0x39, 0x39, g_rstalk_btn);
}

/* Stock 0xCB9E. 0x229 edit: while a request is active, writes the stalk
   position (or the park button for row 5), fixes counter and checksum, and
   sends the frame. phase 0xFF: every frame; else every third frame, the
   others get position 0. */
void can_229_edit(m3_frame_t *f)
{
    uint32_t ph;
    uint32_t row;
    uint32_t b1;
    uint32_t n;

    if (!g_rstalk_active)
        return;
    ph = g_rstalk_phase;
    b1 = f->data[1];
    if (ph != 0xFF) {
        g_rstalk_phase = (uint8_t)((ph + 1) % 3);
        if (ph != 0) {
            row = 0;
            goto stalk;
        }
    }
    row = g_rstalk_row;
    n = (g_rstalk_count - 1) & 0xFF;
    g_rstalk_count = (uint8_t)n;
    if (n == 0)
        g_rstalk_active = 0;
    if (row == 5) {
        f->data[2] = (f->data[2] & 0xFC) | 1;
        f->data[1] = (uint8_t)(b1 & 0x8F);
        goto crc;
    }
stalk:
    f->data[1] = (uint8_t)((b1 & 0x8F) | ((row & 7) << 4));
crc:;
    uint32_t b = f->data[1];
    uint32_t ctr = (b + 1) & 0xF;
    f->data[1] = (uint8_t)((b & 0xF0) | ctr);
    /* Stock reads past the table for row > 5 (not reached). */
    f->data[0] = k_229_crc[row][ctr];
    can_tx(f);
}

/* Stock 0xCC58. 0x118 handler (DI_systemStatus).
   data[2] bits 7:5 = gear (1 P, 2 R, 3 N, 4 D). A change from a known gear
   to a known gear gives event 0x3F..0x42 (0x3E + gear). A change from an
   unknown gear to P sets the high beam request and 0xB9D6.
   data[4] (accelerator pedal) goes to 0x9F42; a change pushes the gauge
   (and runs 0xF702 in D). */
void can_118_rx(m3_frame_t *f)
{
    uint32_t gear = f->data[2] >> 5;
    uint32_t pedal;

    if (g_gear != gear) {
        g_gear = (uint8_t)gear;
        if ((uint8_t)(g_gear_prev - 1) <= 3) {
            if ((uint8_t)(gear - 1) <= 3)
                key_event(gear + 0x3E, 1, 0);
        } else if (gear == 1) {
            beam_request();
            bf34_request();
        }
        g_gear_prev = g_gear;
        gauge_push();
    }
    feature_d8_level(f->data[4]);
    pedal = f->data[4];
    if (g_accel_pedal == pedal)
        RETURN_A0(pedal);
    g_accel_pedal = (uint8_t)pedal;
    if (g_gear == 4) {
        CALL_A0(sport_mode_by_speed, pedal);
        gauge_push();
        return;
    }
    CALL_A0(gauge_push, pedal);
}

/* Stock 0xCD0A. */
uint8_t accel_pedal_get(void)
{
    return g_accel_pedal;
}

/* Stock 0xCD14. */
uint8_t gear_get(void)
{
    return g_gear;
}

/* Stock 0xCD1E. Starts a 0x229 stalk request: row, frame count, and
   every = 1 for every frame (phase 0xFF), 0 for every third frame. */
void rstalk_request(uint8_t row, uint8_t count, uint8_t every)
{
    g_rstalk_active = 1;
    g_rstalk_row = row;
    g_rstalk_phase = (uint8_t)-every;
    g_rstalk_count = count;
}

/* Stock 0xCD3E. Shift request below 9 (speed unit of 0x100E0).
   With flag 0x20004186: 0xFD34(gear, 0). Else one 0x229 move:
   R = stalk up (row 2), D = stalk down (row 4), P = park button (row 5). */
void gear_request(uint32_t gear)
{
    if (g_gear == gear)
        return;
    if (speed_get() > 8)
        return;
    if (g_20004186) {
        req_678_set(gear, 0);
        return;
    }
    if (gear == 2)
        g_rstalk_word = 0x01000201;
    else if (gear == 4)
        g_rstalk_word = 0x01000401;
    else if (gear == 1)
        g_rstalk_word = 0x01000501;
}

/* Stock 0xCDB0. Clears 0x2000BF54..0x2000BF63 (also the gear and pedal). */
void rstalk_clear(void)
{
    tmos_memset(&g_rstalk_active, 0, 16);
}

/* Stock 0xD216 (timer callback, not in the Ghidra function list).
   In P: press the park button for 10 frames (every frame), or 0xFD34(1, 1). */
void park_timer_cb(uint8_t *arg)
{
    if (gear_get() != 1)
        return;
    if (g_20004186) {
        req_678_set(1, 1);
        return;
    }
    rstalk_request(5, 10, 1);
}

/* Stock 0xD356. Starts (500 ms, one shot) or stops the park timer. */
void park_timer_set(uint32_t on)
{
    if (on == 0) {
        app_timer_stop(park_timer_cb);
        return;
    }
    app_timer_start(park_timer_cb, 0, 500, 0);
}
