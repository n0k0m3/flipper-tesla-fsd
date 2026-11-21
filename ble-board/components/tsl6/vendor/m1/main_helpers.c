/* Copied from m1/main.c by tools/tsl6-import.py. */
#include "tsl6.h"
#include "m1.h"

/* Stock 0x81DA. Always 1. */
uint32_t fixed_mode_get(void)
{
    return 1;
}

/* Stock 0x81DE. Turn signal state: 0 off, 1 left, 2 right, 3 hazard. */
uint8_t turn_state_get(void)
{
    return g_turn_state;
}

/* Stock 0x81E8. */
uint8_t epas_torque_mode_get(void)
{
    return g_2000bf09;
}

/* Stock 0x81F2. Clears frame-age counter i (i < 3).
   Stock is void; a0 keeps i (see the note in m1.h). */
uint32_t bus_age_clear(uint32_t i)
{
    if (i < 3)
        g_bus_age[i] = 0;
    return i;
}

/* Stock 0x8204. 1 when the master switch is on (config byte 4 bit 3 = 0)
   and counter i is below 3. */
uint32_t bus_alive(uint32_t i)
{
    if (g_cfg[4] & 8)
        return 0;
    if (i > 2)
        return 0;
    return g_bus_age[i] < 3;
}

/* Stock 0x822E. Edge detect on a range: when v enters [lo, hi], set *flag,
   and fire event arg when the gear (gear_get) is 4 (D). When v leaves,
   clear *flag. */
uint32_t range_edge(uint32_t v, uint32_t lo, uint32_t hi, uint8_t *flag, uint32_t arg)
{
    uint32_t r;

    if (v < lo || hi < v) {
        if (*flag)
            *flag = 0;
        return v;
    }
    if (*flag)
        return v;
    *flag = 1;
    r = gear_get();
    if (r != 4)
        return r;
    return event_fire(arg);
}

/* Stock 0x8272. */
uint32_t car_frames_seen(void)
{
    return g_2000bfd0 > 3;
}

/* Stock 0x8284. */
uint8_t platform_get(void)
{
    return g_platform;
}

/* Stock 0x828E. Copies one 12-byte frame slot of the table at 0x2000C1D4.
   load != 0: slot -> buf. load = 0: buf -> slot. */
void frame_slot_copy(uint32_t load, uint32_t slot, void *buf)
{
    uint8_t *p = g_frame_slots + slot * 12;

    if (load)
        tmos_memcpy(buf, p, 12);
    else
        tmos_memcpy(p, buf, 12);
}

/* Stock 0x842A. 1 while the scroll tick runs. */
uint8_t tick_is_on(void)
{
    return g_tick_on;
}
