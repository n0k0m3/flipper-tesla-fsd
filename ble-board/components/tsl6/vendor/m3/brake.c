/* Brake pedal (0x39D) and brake temperatures (0x3FE). Stock 0xD248..0xD45A. */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xD248. Brake event: brake on while m2 byte 0xBF1C is set gives
   event 0x4C; brake off while it is clear gives 0x4D. */
void brake_event_update(void)
{
    uint32_t r = bf1c_get();

    if (g_brake_on) {
        if (r == 0)
            return;
        g_brake_ev = 1;
        key_event(0x4C, 1, 0);
    } else {
        if (r != 0)
            return;
        g_brake_ev = 0;
        key_event(0x4D, 1, 0);
    }
}

/* Stock 0xD284. 0x39D (IBST_status) handler.
   data[2] bit 1 = driver brake apply. data[3] > 80 = hard press.
   Config byte 9 bits 7:6 select what 0xFABE follows: 1 = brake, 2 = hard
   press. A hard press at speed 0 gives event 0xA0. */
void can_39d_brake(m3_frame_t *f)
{
    uint32_t on = (f->data[2] >> 1) & 1;

    if (g_brake_on != on) {
        g_brake_on = (uint8_t)on;
        if ((g_cfg[9] & 0xC0) == 0x40)
            bodyctl_d2b6_set(on);
        brake_event_update();
    }
    if (g_brake_hard == 0) {
        if (g_brake_on && f->data[3] > 0x50) {
            g_brake_hard = 1;
            if ((g_cfg[9] & 0xC0) == 0x80)
                bodyctl_d2b6_set(1);
            if (speed_get() == 0)
                event_fire(0xA0);
        }
    } else if (g_brake_on == 0) {
        g_brake_hard = 0;
        if ((g_cfg[9] & 0xC0) == 0x80)
            bodyctl_d2b6_set(0);
    }
    if (bodyctl_d2b6_get() == 1 && speed_get() <= 8)
        bodyctl_d2b6_set(0);
}

/* Stock 0xD350. */
uint8_t brake_ev_get(void)
{
    return g_brake_ev;
}

/* Stock 0xD378. Clears 0x20004114..0x20004115. */
void brake_clear(void)
{
    tmos_memset(&g_brake_on, 0, 2);
}

/* Stock 0xD38A. 0x3FE (brake temperatures): four 10-bit values from bit 12.
   On a change it stores them, packs them into 5 bytes at 0x20004120
   (t0 | t1 << 10 | t2 << 20 | t3 << 30) and pushes the gauge. */
void can_3fe_brake_temp(m3_frame_t *f)
{
    const uint8_t *d = f->data;
    uint32_t t0 = (d[1] >> 4) | (d[2] & 0x3F) << 4;
    uint32_t t1 = d[3] << 2 | (d[2] >> 6);
    uint32_t t2 = (d[5] & 3) << 8 | d[4];
    uint32_t t3 = (d[6] & 0xF) << 6 | (d[5] >> 2);

    if (g_brake_t[0] == t0 && g_brake_t[1] == t1 && g_brake_t[2] == t2 && g_brake_t[3] == t3)
        RETURN_A0(f);
    g_brake_t[0] = (uint16_t)t0;
    g_brake_t[1] = (uint16_t)t1;
    g_brake_t[2] = (uint16_t)t2;
    g_brake_t[3] = (uint16_t)t3;
    g_brake_pack[0] = t0 | t1 << 10 | t2 << 20 | t3 << 30;
    g_brake_pack[1] = (g_brake_pack[1] & 0xFFFFFF00) | (t3 >> 2);
    CALL_A0(gauge_push, g_brake_pack);
}

/* Stock 0xD442. */
uint32_t *brake_temp_get(void)
{
    return g_brake_pack;
}

/* Stock 0xD448. */
void brake_temp_clear(void)
{
    tmos_memset(g_brake_pack, 0, 5);
}
