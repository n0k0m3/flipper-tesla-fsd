/* Doors and pending actions (stock 0xDB8E..0xDF0A).
   0x102 (left doors) gives the latch state of the front-left and rear-left
   doors and the handle switches. A long pull on a handle starts the frunk
   (front door) or toggles the charge port (rear door), as the settings
   "TimeOpenFrontBox" and "TimeOpenChargeDoor" (config byte 5) say. */
#include "tsl6.h"
#include "m4.h"

/* Stock 0xDB8E. Runs a pending action (code, see door_cmd_run) when the car is
   ready (0x339 lock state). Otherwise it asks the car (0x273 request 2) and
   the 0x273 echo runs the action 1 s later. A busy flag (speed_get) gives the
   error beep. */
void action_when_ready(uint32_t code)
{
    if (speed_get()) {
        tune_two_low(0);
        return;
    }
    g_2000bf9c = (uint8_t)code;
    if (!vc273_unlocked())
        vc273_lock_req_set(2);
    else
        door_cmd_run(0);
    buzzer_beeps(1);
}

/* Stock 0xDBD0. */
uint8_t bf84_get(void)
{
    return g_2000bf84;
}

/* Stock 0xDBDA. Pending action 0x19 opens the frunk (vc273_frunk_req). */
void frunk_open_when_ready(void)
{
    action_when_ready(0x19);
}

/* Stock 0xDBDE. 0x102 handler (left doors). data[0] bits 3:0 = front latch,
   bits 7:4 = rear latch (1 = open). The handle switch bit depends on the car
   variant (0x7FF mux 3: 7 and 8 use other bits). */
void door_left_rx(m4_frame_t *f)
{
    uint32_t pulled = 0;
    uint32_t latch = f->data[0] & 0xf;

    if ((uint8_t)(g_20004188 - 7) <= 1) {
        if (latch == 1)
            pulled = (f->data[1] >> 4) & 1;
    } else if (latch == 1) {
        /* Bits 2 and 8 of the 16-bit word data[1..2]. */
        pulled = ((f->data[1] & 4) | (f->data[2] & 1)) != 0;
    }
    if (hold_check((g_cfg[5] >> 2) & 7, pulled, &g_2000bf90, &g_2000bf94))
        frunk_open_when_ready();

    latch = f->data[0] & 0xf0;
    pulled = 0;
    if ((uint8_t)(g_20004188 - 7) <= 1) {
        if (latch == 0x10)
            pulled = (f->data[1] >> 5) & 1;
    } else if (latch == 0x10) {
        pulled = (f->data[1] >> 3) & 1;
    }
    if (hold_check(g_cfg[5] >> 5, pulled, &g_2000bf91, &g_2000bf98))
        cp_request((uint8_t)(cp_state_get() ^ 1));

    door_update(&g_2000bf8b, &g_2000bf86, f->data[0] & 0xf);
    door_update(&g_2000bf8d, &g_2000bf88, f->data[0] >> 4);
    trigger_on_change(&g_20004126, f->data[0] & 0xf, 0x72, 0x73, 1, 2);
    trigger_on_change(&g_20004127, f->data[0] >> 4, 0x7a, 0x7b, 1, 2);
}

/* Stock 0xDD22. */
uint8_t bf85_get(void)
{
    return g_2000bf85;
}

/* Stock 0xDD2C. Pending action 0x18 (0x3B3 data[0] bit 0, can_3b3_send_bit). */
void action_18_when_ready(void)
{
    action_when_ready(0x18);
}

/* Stock 0xDD32. Runs action 0x1A (0x3B3 data[0] bit 1) at once. */
void action_1a_run(void)
{
    g_2000bf9c = 0x1a;
    door_cmd_run(0);
}

/* Stock 0xDD42. Bit set for each open door: 0 FL, 1 RL, 2 FR, 3 RR. */
uint32_t doors_open_mask(void)
{
    uint32_t m = g_2000bf86 == 1;

    m |= g_2000bf88 == 1 ? 2 : 0;
    m |= g_2000bf87 == 1 ? 4 : 0;
    m |= g_2000bf89 == 1 ? 8 : 0;
    return m;
}

/* Stock 0xDD8E. */
void doors_reset(void)
{
    tmos_memset(&g_2000bf84, 0, 0x1c);
}

/* Stock 0xDDA4 (timer callback, not in the Ghidra function list).
   Turns off every car feature that the module drives. */
void features_off_cb(uint8_t *arg)
{
    bf1d_set(0);
    sport_mode_restore();
    headlight_req_set(0);
    vc334_d3_clear();
    vc3b3_b0_restore();
    vc273_b0_restore();
    can_2f3_app_follow();
    vehicle_e8_clear();
    if (tick_is_on())
        tick_start(0, 0);
    if (reengage_active())
        reengage_off();
    if (heat_request_get())
        heat_request_set(0);
    if (seat_cycle_get())
        seat_cycle_set(0, 3, 1);
    if (swc_mode32_get())
        bf32_horn_set(0);
    if (flash_active_get())
        flash_mode_set(0, 0);
    if (front_fog_on_get())
        front_fog_on_set(0, 0);
    if (front_fog_strobe_get())
        front_fog_strobe_set(0, 0);
    if (front_fog_burst_get())
        front_fog_burst_set(0, 0);
    if (rear_fog_on_get())
        rear_fog_on_set(0, 0);
    if (rear_fog_strobe_get())
        rear_fog_strobe_set(0, 0);
    if (rear_fog_burst_get())
        rear_fog_burst_set(0, 0);
    if (highbeam_get())
        highbeam_set(0);
    if (vc273_b4_get())
        vc273_b4_set(0);
    status_push(0);
}

/* Stock 0xDE90. Car variant 1..4, state 4 and ap_active: runs rstalk_request(3, 15, 1)
   and feature_d8_arm when the last call is 2 s or more ago. Else the error beep. */
void action_rstalk_d8(void)
{
    uint32_t now;

    if ((uint8_t)(g_20004188 - 1) > 3)
        return;
    if (gear_get() != 4 || !ap_active()) {
        tune_two_low(0);
        return;
    }
    now = clock_ms();
    if (now - g_20004134 > 1999) {
        rstalk_request(3, 15, 1);
        feature_d8_arm();
    }
    g_20004134 = now;
}

/* Stock 0xDEF6. */
void features_off_later(void)
{
    app_timer_start(features_off_cb, 0, 1000, 0);
}
