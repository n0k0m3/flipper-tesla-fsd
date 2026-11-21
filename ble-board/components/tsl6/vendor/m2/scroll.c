/* Scroll gesture and scroll tick (stock 0xA144..0xA2D8).
   A gesture puts a code in g_gesture_code. The 0x3C2 handler then edits the
   next page-1 frame with the tick bytes of that code. */
#include "m2.h"

/* Stock 0xA144. jg != 0 starts a gesture: it picks the wheel (cfg[4] bits 7:6)
   and the direction (cfg[9] bits 1:0) and sets the gap counter to jg.
   jg == 0 steps the gap counter. At 0 it flips the direction and sends the
   reverse tick. jg is a full 32-bit value in stock (beqz a0). */
uint32_t scroll_gesture(uint32_t jg)
{
    uint32_t r = jg;
    uint8_t code;

    if (g_200040d8) {
        /* Another feature owns the wheel. */
        if (jg == 0)
            return 0;
        if (ap_active() == 0)
            return 0;
        return rstalk_request(3, 1, 0);
    }

    uint8_t gap = g_gesture_gap;
    if (jg == 0) {
        if (gap == 0)
            return 0;
        gap = (uint8_t)(gap - 1);
        g_gesture_gap = gap;
        if (gap != 0)
            return 0;
        g_gesture_dir = (g_gesture_dir == 0);
    } else {
        if (gap != 0)
            return jg;
        g_gesture_gap = (uint8_t)jg;

        uint8_t *cfg = g_cfg;
        uint32_t wheel = cfg[4] >> 6;
        if (wheel == 2)
            g_200040e0 = (g_200040e0 == 0);    /* alternate the wheels */
        else if (wheel > 2)
            g_200040e0 = 3;
        else if (wheel == 1)
            g_200040e0 = 1;
        else
            g_200040e0 = 0;

        uint32_t dir = cfg[9] & 3;
        if (dir == 2) {
            /* Keep the last direction. */
        } else if (dir > 2) {
            r = tmos_rand() & 1;
            g_gesture_dir = (uint8_t)r;
        } else if (dir == 1) {
            g_gesture_dir = 1;
        } else {
            g_gesture_dir = 0;
        }
    }

    uint8_t wheel = g_200040e0;
    if (wheel == 0) {
        /* No gesture for 5 s after a manual scroll. */
        if (g_manual_scroll_hold)
            return r;
        code = g_gesture_dir ? 3 : 1;
    } else if (wheel == 3) {
        code = g_gesture_dir ? 10 : 12;
    } else {
        code = g_gesture_dir ? 11 : 9;
    }
    return gesture_code_set(code);
}

/* Stock 0xA22A. Timer callback: one gesture with gap 1. */
uint32_t scroll_gesture_cb(uint8_t *arg)
{
    return scroll_gesture(1);
}

/* Stock 0xA22E. One tick of the scroll timer. Platform 1 (Tesla) uses the
   gesture; the torque method runs when bus 2 (0x370) is alive. Other
   platforms go to 0x104DE. */
uint32_t scroll_tick(uint32_t jg)
{
    uint32_t r;

    if (platform_get() != 1)
        return active_start(jg);

    uint32_t mode = fixed_mode_get();     /* stock: always 1 */
    r = mode;
    if (mode & 1) {
        if (bus_alive(2) != 0)
            goto torque;
        r = scroll_gesture(jg);
    }
    if (!(mode & 2))
        return r;
torque:
    r = bus_alive(2);
    if (r == 0 || jg == 0)
        return r;
    if ((g_cfg[3] & 15) == 15)
        return r;
    g_200040dc = (uint8_t)((g_200040dc + 1) & 3);
    g_200040db = 1;
    return app_timer_start(ap_window_end, 0, jg * 1000, 0);
}

/* Stock 0xA2C6. Clears the gesture state block 0x200040D8..0x200040DC. */
uint32_t gesture_state_clear(void)
{
    return res_memset(&g_200040d8, 0, 5);
}
