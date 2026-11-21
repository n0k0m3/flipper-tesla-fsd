/* 0x3B3 handlers (stock 0xF778..0xF834). The module sends a copy of the
   car's 0x3B3 frame with data[1] bits 3:1 and bit 0 changed. */
#include "tsl6.h"
#include "m4.h"

/* Stock 0xF778. data[1] bits 3:1 changed: tell headlight_req_set. */
void vc3b3_mode_rx(m4_frame_t *f)
{
    uint8_t v = (f->data[1] >> 1) & 7;

    if (g_2000413d == v)
        return;
    g_2000413d = v;
    headlight_req_set(0);
}

/* Stock 0xF792. 0x3B3 handler: mode 1 or 2 sets data[1] bits 3:1 to 1 or 3.
   data[1] bit 0 follows the wanted value when it differs from the car's. */
void vc3b3_rx(m4_frame_t *f)
{
    uint32_t changed = 0;
    uint8_t b;

    if (g_20004140 == 1) {
        f->data[1] = (f->data[1] & 0xf1) | 2;
        changed = 1;
    } else if (g_20004140 == 2) {
        f->data[1] = (f->data[1] & 0xf1) | 6;
        changed = 1;
    }
    b = f->data[1] & 1;
    if (g_20004141 != b) {
        g_20004141 = b;
        g_20004142 = b;
    }
    if (g_20004142 != g_20004141)
        f->data[1] = (f->data[1] & 0xfe) | (g_20004142 & 1);
    else if (!changed)
        return;
    can_tx(f);
}

/* Stock 0xF7FC. Wanted data[1] bit 0. The beep is inverted. */
void vc3b3_b0_set(uint32_t on)
{
    g_20004142 = (uint8_t)on;
    buzzer_beep_onoff(on ^ 1);
}

/* Stock 0xF808. */
uint8_t vc3b3_b0_get(void)
{
    return g_20004142;
}

/* Stock 0xF80E. 0 = keep, 1 or 2 = data[1] bits 3:1 = 1 or 3. */
void vc3b3_mode_set(uint32_t mode)
{
    g_20004140 = (uint8_t)mode;
}

/* Stock 0xF814. */
void vc3b3_b0_restore(void)
{
    g_20004142 = g_20004141;
}

/* Stock 0xF822. */
void vc3b3_reset(void)
{
    tmos_memset(&g_20004140, 0, 3);
}
