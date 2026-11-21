/* Requests that edit the cached 0x3E9 frame (stock 0xFA56..0xFC6C).
   The frame cache entry 13 holds the last 0x3E9 frame. vc3e9_rx (m4)
   edits a copy of it with these request bytes and sends it. */
#include "tsl6.h"
#include "m5.h"

extern uint8_t g_20004144;   /* u8: request state for data[0] bits 3:2 (0..3) */
extern uint8_t g_20004145;   /* u8: request state for data[2] bit 6 (0..2) */
extern uint8_t g_20004146;   /* u8: request for data[0] bits 1:0 (0, 1, 2) */
extern uint8_t g_20004147;   /* u8: "started by the timer path" flag */
extern uint8_t g_20004148;   /* u8: trigger state of hazard_auto_update */
extern uint32_t g_2000414c;  /* u32: time (ms) of the trigger in hazard_auto_update */
extern uint8_t g_2000c273;   /* u8: byte that headlight_req_set reads (bits 1:0) */
extern uint8_t *g_cfg;       /* 0x20004074: config block */

/* Stock 0xFA56. Takes the cached 0x3E9 frame and sends an edited copy,
   when the cache holds a frame (dlc != 0). */
void bodyctl_resend(void)
{
    m5_frame_t f;

    frame_slot_copy(1, 13, &f);
    if (f.dlc != 0)
        vc3e9_rx(&f);
}

/* Stock 0xFA76. */
void hazard_req_set(uint32_t v)
{
    g_20004144 = (uint8_t)v;
}

/* Stock 0xFA7C. */
uint32_t hazard_req_get(void)
{
    return g_20004144;
}

/* Stock 0xFA82. Sets the data[0] bits 1:0 request. 3 toggles it between 1 and 2.
   From 0, the toggle uses bits 1:0 of 0x2000C273 to pick the next value. */
void headlight_req_set(uint32_t v)
{
    uint8_t n;

    if (v != 3) {
        g_20004146 = (uint8_t)v;
        return;
    }
    if (g_20004146 == 0)
        n = ((g_2000c273 & 3) == 1) ? 2 : 1;
    else
        n = (g_20004146 == 1) ? 2 : 1;
    g_20004146 = n;
}

/* Stock 0xFAB8. Returns the data[2] bit 6 request state (0x20004145). */
uint32_t bodyctl_d2b6_get(void)
{
    return g_20004145;
}

/* Stock 0xFABE. on != 0: set the data[2] bit 6 request when 0x2000BF60 is 4 and
   the speed is above 8. on = 0: a set request becomes "clear once" (2). */
void bodyctl_d2b6_set(uint32_t on)
{
    if (on == 0) {
        if (g_20004145 != 1) {
            g_20004145 = 0;
            return;
        }
        g_20004145 = 2;
    } else {
        if (gear_get() != 4)
            return;
        if (speed_get() <= 8)
            return;
        g_20004145 = 1;
    }
    bodyctl_resend();
}

/* Stock 0xFB0C. Hazard request: v != 0 sets 1 (on), v = 0 sets 2 (a
   short pulse, see vc3e9_rx) and clears 0x20004147. */
M5_CALLED void hazard_request(uint32_t v)
{
    g_20004144 = (uint8_t)((v == 0) + 1);
    if (v == 0)
        g_20004147 = 0;
}

/* Stock 0xFB1E. hazard_request(0x2000BEFC state == 0). */
void hazard_toggle(void)
{
    hazard_request(hazard_state() == 0);
}

/* Stock 0xFB32. Starts one of the two 500 ms timers and stops the other. */
void high_beam_timer_switch(uint32_t v)
{
    void *cb;

    if (v == 0) {
        app_timer_stop(high_beam_hold_cb);
        cb = high_beam_flash_cb;
    } else {
        g_20004147 = 1;
        app_timer_stop(high_beam_flash_cb);
        cb = high_beam_hold_cb;
    }
    app_timer_start(cb, 0, 500, 0);
}

/* Stock 0xFB80. Trigger state machine. Runs only when config byte 9 bits 3:2
   are set and car frames come in. */
void hazard_auto_update(uint32_t ev)
{
    if ((g_cfg[9] & 0x0C) == 0)
        return;
    if (car_frames_seen() == 0)
        return;
    if (ev == 0) {
        if (g_20004148 == 0)
            return;
        if (g_20004148 == 2) {
            app_timer_stop(high_beam_hold_cb);
            app_timer_stop(high_beam_flash_cb);
            g_20004144 = 1;
        }
        g_20004148 = 0;
        return;
    }

    uint32_t s = hazard_state();
    if (s == 1 || s == 7) {
        if (g_20004148 != 0)
            return;
        g_2000414c = clock_ms();
        g_20004148 = (g_20004147 != 0) ? 1 : ((g_cfg[9] >> 2) & 3);
        app_timer_stop(high_beam_hold_cb);
        app_timer_stop(high_beam_flash_cb);
        g_20004144 = 2;
        g_20004147 = 0;
        return;
    }
    if (g_20004148 == 0)
        return;
    if (g_20004144 != 0)
        return;
    if (turn_state_get() != 0)
        return;
    if (clock_ms() - g_2000414c <= 1499)
        return;
    g_20004148 = 0;
}

/* Stock 0xFC5A. Clears 0x20004144..0x20004147. */
void bodyctl_req_clear(void)
{
    tmos_memset(&g_20004144, 0, 4);
}
