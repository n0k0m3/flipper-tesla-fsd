/* Helpers for the 0x3C2 steering-wheel state block of m2 (0x2000BF1C, 28 bytes).
   Stock 0xC05A..0xC0D4. */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xC05A. Returns the m2 mode byte 0x2000BF32 (set by 0xBA64). */
uint8_t swc_mode32_get(void)
{
    return g_2000bf32;
}

/* Stock 0xC064. Turns off the m2 modes that are on: 0xBF30 (0xEC90),
   0xBF32 (0xBA64) and 0xBF33. */
void swc_modes_off(void)
{
    if (g_2000bf30)
        horn_melody_start(0);
    if (g_2000bf32)
        bf32_horn_set(0);
    if (g_2000bf33)
        g_2000bf33 = 0;
}

/* Stock 0xC0AC. The 0x3C2 handler edits the next 50 page frames (0xBF2E).
   One beep. */
void swc_edit50_start(void)
{
    g_2000bf2e = 50;
    buzzer_beeps(1);
}

/* Stock 0xC0BE. */
void swc_state_clear(void)
{
    tmos_memset(g_2000bf1c, 0, 28);
}
