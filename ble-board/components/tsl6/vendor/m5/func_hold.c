/* Turn on one of four outputs while c250_is_2(0) is nonzero
   (stock 0xFE80..0xFF9A). Each output is enabled by a config bit. */
#include "tsl6.h"
#include "m5.h"

extern uint8_t g_20004154;   /* u8: output 0 (bf32_horn_set) is on */
extern uint8_t g_20004155;   /* u8: output 1 (flash_mode_set) is on */
extern uint8_t g_20004156;   /* u8: output 2 (front_fog_strobe_set) is on */
extern uint8_t g_20004157;   /* u8: output 3 (rear_fog_strobe_set) is on */
extern uint8_t *g_cfg;       /* 0x20004074: config block */

/* Stock 0xFE80 (not in the Ghidra function list; app timer callback).
   Polls every 100 ms. When c250_is_2(0) is 0, it turns off every output
   that func_hold_start turned on. */
M5_CALLED void func_hold_poll(void)
{
    uint32_t r = c250_is_2(0);

    if (r != 0) {
        app_timer_start(func_hold_poll, 0, 100, 0);
        return;
    }
    if (g_20004154) {
        g_20004154 = 0;
        bf32_horn_set(r);    /* r is 0 here */
    }
    if (g_20004155) {
        g_20004155 = 0;
        flash_mode_set(0, 0);
    }
    if (g_20004156) {
        g_20004156 = 0;
        front_fog_strobe_set(0, 0);
    }
    if (g_20004157) {
        g_20004157 = 0;
        rear_fog_strobe_set(0, 0);
    }
}

/* Stock 0xFEF0. Turns on output n (0..3) when its config bit is set,
   then runs the poll func_hold_poll at once. */
void func_hold_start(uint32_t n)
{
    if (n == 1) {
        if ((g_cfg[1] & 0x03) == 0)
            return;
        g_20004155 = 1;
        flash_mode_set(1, 0);
    } else if (n == 0) {
        if ((g_cfg[3] & 0xC0) == 0)
            return;
        g_20004154 = 1;
        bf32_horn_set(1);
    } else if (n == 2) {
        if ((g_cfg[6] & 0x20) == 0)
            return;
        g_20004156 = 1;
        front_fog_strobe_set(1, 1);
    } else if (n == 3) {
        if ((g_cfg[7] & 0x40) == 0)
            return;
        g_20004157 = 1;
        rear_fog_strobe_set(1, 1);
    } else {
        return;
    }
    func_hold_poll();
}

/* Stock 0xFF88. Clears 0x20004154..0x20004157. */
void func_hold_clear(void)
{
    tmos_memset(&g_20004154, 0, 4);
}
