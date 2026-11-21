/* 0x334 echo (stock 0xF574..0xF778). The module sends a copy of the car's
   0x334 frame with these fields changed:
     data[0] bit 5     sport mode ("Auto sport mode", config byte 0 bits 2:0)
     data[2] bits 4:0  regen level ("Regenerative braking", config byte 0 bits 7:5)
     data[3] bits 1:0  two request bits (gear state 1 only)
     data[3] bits 7:6  stopping mode ("Stopping mode", config byte 1 bits 6:5)
   data[6] bits 7:4 is a counter and the last byte is the checksum. */
#include "tsl6.h"
#include "m4.h"

/* data[2] bits 4:0 for each regen setting (stock 0x19B70). */
static const uint8_t regen_level[8] = { 0x00, 0x00, 0x05, 0x0a, 0x14, 0x1f, 0x00, 0x00 };
/* Speed thresholds for auto sport mode (stock 0x19B78). */
static const uint8_t sport_speed[8] = { 0xff, 0x33, 0x4c, 0x66, 0x7f, 0x99, 0xb2, 0xcc };

/* Stock 0xF574. 0x334 handler. */
void vc334_rx(m4_frame_t *f)
{
    uint8_t *p = (uint8_t *)f;
    uint8_t *cfg;
    uint8_t stop_mode[4] = { 0x00, 0x80, 0x40, 0x00 };
    uint32_t changed = 0;
    uint32_t v = (f->data[0] >> 5) & 1;
    uint32_t idx;
    uint8_t d3;

    if (g_20004138 != v) {
        g_20004138 = (uint8_t)v;
        g_20004139 = (uint8_t)v;
        status_push(0);
        gauge_push();
    }
    if (g_20004139 != g_20004138) {
        f->data[0] = (f->data[0] & ~0x20) | ((g_20004139 & 1) << 5);
        changed = 1;
    }
    cfg = g_cfg;
    if (cfg[0] & 0xe0) {
        f->data[2] = (f->data[2] & 0xe0) | regen_level[cfg[0] >> 5];
        changed = 1;
    }
    if (g_2000413a) {
        f->data[3] |= 2;
        changed = 1;
    } else if (g_2000413b) {
        f->data[3] |= 1;
        changed = 1;
    }
    d3 = f->data[3];
    idx = g_2000413c;
    if (idx == 0) {
        if (!(cfg[1] & 0x60))
            goto check;
        idx = (cfg[1] >> 5) & 3;
    }
    /* Stock does not check idx < 4 here: g_2000413c above 3 reads the stack.
       vc334_stop_mode_set only stores 0..3. */
    if (stop_mode[idx] != (d3 & 0xc0)) {
        f->data[3] = (d3 & 0x3f) | stop_mode[idx];
        goto send;
    }
check:
    if (!changed)
        return;
send:
    f->data[6] += 0x10;
    {
        int last = f->dlc - 1;
        p[3 + last] = (uint8_t)frame_checksum_last(f);
    }
    can_tx(f);
}

/* Stock 0xF692. */
void sport_mode_set(uint32_t on)
{
    g_20004139 = (uint8_t)on;
    buzzer_beep_onoff(on);
    status_push(0);
}

/* Stock 0xF6A8. */
uint8_t sport_mode_get(void)
{
    return g_20004139;
}

/* Stock 0xF6AE. 0 = use the setting, 1..3 = stopping mode. */
void stop_mode_set(uint32_t mode)
{
    if (mode <= 3)
        g_2000413c = (uint8_t)mode;
}

/* Stock 0xF6BA. Sets data[3] bit 1 (bit1 != 0) or bit 0. The two exclude
   each other. Only in gear state 1; else the error beep. */
void vc334_d3_set(uint32_t on, uint32_t bit1)
{
    if (gear_get() == 1) {
        if (bit1) {
            g_2000413a = (uint8_t)on;
            if (on)
                g_2000413b = 0;
        } else {
            g_2000413b = (uint8_t)on;
            if (on)
                g_2000413a = 0;
        }
        buzzer_beep_onoff(on);
    } else {
        tune_two_low(0);
    }
    status_push(0);
}

/* Stock 0xF702. Auto sport mode: on at or above the threshold speed. */
void sport_mode_by_speed(uint32_t speed)
{
    uint32_t on;

    if (!(g_cfg[0] & 7))
        return;
    on = !(speed < sport_speed[g_cfg[0] & 7]);
    if (g_20004139 == on)
        return;
    g_20004139 = (uint8_t)on;
    status_push(0);
}

/* Stock 0xF73A. */
void vc334_d3_bit1_set(uint32_t on)
{
    vc334_d3_set(on, 1);
}

/* Stock 0xF740. */
uint8_t vc334_d3_bit1_get(void)
{
    return g_2000413a;
}

/* Stock 0xF746. */
void vc334_d3_bit0_set(uint32_t on)
{
    vc334_d3_set(on, 0);
}

/* Stock 0xF74C. */
uint8_t vc334_d3_bit0_get(void)
{
    return g_2000413b;
}

/* Stock 0xF752. Wanted sport mode = the car's. */
void sport_mode_restore(void)
{
    g_20004139 = g_20004138;
}

/* Stock 0xF760. */
void vc334_d3_clear(void)
{
    g_2000413a = 0;
    g_2000413b = 0;
}

/* Stock 0xF766. */
void vc334_reset(void)
{
    tmos_memset(&g_20004138, 0, 5);
}
