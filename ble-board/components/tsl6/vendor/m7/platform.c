/* Platform CAN dispatch and car-config state (stock 0x1489E..0x1516E). */
#include "m7.h"

/* Car config from the 0x7FF mux frames (gp+384..gp+394). */
extern uint8_t g_cfg7ff_180;    /* 0x20004180: mux 1, data[5] & 7 < 4 */
extern uint8_t g_carcfg[5];     /* 0x20004184: [1] mux 2, [3] mux 1, [4] car type (mux 3) */
extern uint8_t g_btn_lock;      /* 0x20004189: set 20 ms after a button action */
extern uint8_t g_send339_cnt;   /* 0x2000418A: repeat counter of the 0x339 frame */

uint32_t bus_alive(uint32_t n);
uint32_t platform_get(void);  /* platform byte */
uint32_t frame_slot_copy(uint32_t dir, uint32_t slot, void *buf);
uint32_t platform_check(void *cb, void *frame);
void chk_tesla_sum(void);
uint32_t platform_set(uint32_t v);
void can_tx(void *frame);
void *memcpy(void *dst, const void *src, uint32_t n);  /* memcpy (libc) */
void cfg_save(void);
void bus_age_clear(uint32_t v);
void led_on(void);
void led_off(void);
uint32_t buzzer_beep_onoff(uint32_t v);
uint32_t seat_resend(uint32_t a0);
void seat_init(void);
void can_370_epas(void *f);
uint32_t can_3d8_altitude(void *f);
uint32_t can_3c2_swc(void *f);

/* Frame handlers of other modules: all take the RX ring entry. */
uint32_t platform_detect(void *f);  /* stock 0x1084c */
uint32_t can_332_bms(void *f);  /* stock 0xd0be */
uint32_t can_25a_tpms(void *f);  /* stock 0xcdc6 */
uint32_t can_2b6(void *f);  /* stock 0xadca */
uint32_t vc273_rx(void *f);  /* stock 0xee5e */
uint32_t can_293(void *f);  /* stock 0xae2a */
uint32_t can_25d_cp(void *f);  /* stock 0xcffa */
uint32_t can_2f3(void *f);  /* stock 0xb0b4 */
uint32_t can_31f_tpms(void *f);  /* stock 0xce0c */
uint32_t can_321(void *f);  /* stock 0xafcc */
uint32_t can_312_pack_temp(void *f);  /* stock 0xd114 */
uint32_t can_2e1_frunk(void *f);  /* stock 0xd762 */
uint32_t can_1f9_door_edit(void *f);  /* stock 0xd8ba */
uint32_t can_238(void *f);  /* stock 0xaa2a */
uint32_t can_249_rx(void *f);  /* stock 0xc5a2 */
uint32_t can_249_edit(void *f);  /* stock 0xc624 */
uint32_t can_257_speed(void *f);  /* stock 0xff9a */
uint32_t can_243(void *f);  /* stock 0xaf58 */
uint32_t can_20c(void *f);  /* stock 0xafea */
uint32_t can_229_rx(void *f);  /* stock 0xcb82 */
uint32_t can_229_edit(void *f);  /* stock 0xcb9e */
uint32_t can_103_doors(void *f);  /* stock 0xd7b6 */
uint32_t can_082_heat(void *f);  /* stock 0xcf46 */
uint32_t door_left_rx(void *f);  /* stock 0xdbde */
uint32_t can_129_steer(void *f);  /* stock 0x1011e */
uint32_t can_118_rx(void *f);  /* stock 0xcc58 */
uint32_t can_3e9(void *f);  /* stock 0xacac */
uint32_t vc3e9_rx(void *f);  /* stock 0xf8b8 */
uint32_t can_3e2_rx(void *f);  /* stock 0xc0d4 */
uint32_t can_3e3_flag(void *f);  /* stock 0xfde8 */
uint32_t can_3fe_brake_temp(void *f);  /* stock 0xd38a */
uint32_t can_3f5_lights(void *f);  /* stock 0xa624 */
uint32_t can_679_edit(void *f);  /* stock 0xc3a8 */
uint32_t can_678_count(void *f);  /* stock 0xfc6c */
uint32_t can_678_edit(void *f);  /* stock 0xfcb8 */
uint32_t can_405_vin(void *f);  /* stock 0xce74 */
uint32_t can_39d_brake(void *f);  /* stock 0xd284 */
uint32_t vc339_lock_rx(void *f);  /* stock 0xedea */
uint32_t can_333_cp_edit(void *f);  /* stock 0xd040 */
uint32_t tesla_seen_count(void *f);  /* stock 0x103b0 */
uint32_t vc334_rx(void *f);  /* stock 0xf574 */
uint32_t can_33a_range(void *f);  /* stock 0xd188 */
uint32_t vc3b3_mode_rx(void *f);  /* stock 0xf778 */
uint32_t vc3b3_rx(void *f);  /* stock 0xf792 */
uint32_t can_3b6_odometer(void *f);  /* stock 0xfd56 */
uint32_t can_3c3_rx(void *f);  /* stock 0xc172 */
uint32_t can_3c3_edit(void *f);  /* stock 0xc30e */
uint32_t can_3c2_buttons(void *f);  /* stock 0xb41e */
uint32_t key_a9(void *f);  /* stock 0x10538 */
uint32_t p2_21c(void *f);  /* stock 0x105ac */
uint32_t p3_21c(void *f);  /* stock 0x10618 */
uint32_t p4_189(void *f);  /* stock 0x10698 */
uint32_t p4_68c(void *f);  /* stock 0x106d4 */
uint32_t p5_3a1(void *f);  /* stock 0x106ea */
uint32_t key_3ea(void *f);  /* stock 0x107ea */
uint32_t key_37a(void *f);  /* stock 0x1079a */
uint32_t p6_498(void *f);  /* stock 0x1060a */
uint32_t vc273_echo(void *f, uint32_t v);
uint32_t can_power(void *f, uint32_t v);
/* Init functions that 0x15044 calls. */
void platform_clear(void);  /* stock 0x10912 */
void vin_clear(void);  /* stock 0xcf30 */
void gesture_state_clear(void);  /* stock 0xa2c6 */
void bus_mon_init(void);  /* stock 0xad7c */
void brake_temp_clear(void);  /* stock 0xd448 */
void tpms_clear(void);  /* stock 0xce58 */
void gauge_vals_clear(void);  /* stock 0xfe6a */
void can_679_clear(void);  /* stock 0xc498 */
void req_678_clear(void);  /* stock 0xfd44 */
void battery_clear(void);  /* stock 0xd200 */
void vehicle_e8_clear(void);  /* stock 0xaec4 */
void vehicle_bf0c_clear(void);  /* stock 0xb2ce */
void doors_reset(void);  /* stock 0xdd8e */
void lstalk_clear(void);  /* stock 0xcb6c */
void rstalk_clear(void);  /* stock 0xcdb0 */
void vc273_reset(void);  /* stock 0xf55e */
void vc3b3_reset(void);  /* stock 0xf822 */
void vc334_reset(void);  /* stock 0xf766 */
void bodyctl_req_clear(void);  /* stock 0xfc5a */
void ap_state_clear(void);  /* stock 0xad3a */
void turn_state_clear(void);  /* stock 0xa95c */
void swc_state_clear(void);  /* stock 0xc0be */
void can_3c3_3e2_clear(void);  /* stock 0xc37a */
void brake_clear(void);  /* stock 0xd378 */
void horn_reset(void);  /* stock 0xedca */
void func_hold_clear(void);  /* stock 0xff88 */

/* Stock 0x1489E. Packed state for the C1 reply:
   bits 31..29 = bus_alive(0..2) (0x8204), bits 23..16 = platform,
   bits 15..8 = car type, bit 3 = carcfg[2], bit 2 = carcfg[0],
   bit 1 = carcfg[1], bit 0 = carcfg[3]. */
uint32_t car_state_word(void)
{
    uint32_t f0 = bus_alive(0);
    uint32_t f1 = bus_alive(1);
    uint32_t f2 = bus_alive(2);
    uint32_t plat = platform_get();

    return f2 << 29 | f0 << 31 | f1 << 30 |
           (uint32_t)g_carcfg[4] << 8 | (uint32_t)g_carcfg[2] << 3 | g_carcfg[3] |
           (uint32_t)g_carcfg[0] << 2 | (uint32_t)g_carcfg[1] << 1 | plat << 16;
}

/* Copies the frame into CAN slot n (0x828E direction 0). */
static uint32_t slot_store(uint32_t n, uint8_t *f)
{
    return frame_slot_copy(0, n, f);
}

/* 0x7FF (car config, multiplexed by data[0]). */
static void can_7ff_config(uint8_t *f)
{
    switch (f[3]) {
    case 1:
        g_cfg7ff_180 = (f[8] & 7) < 4;
        g_carcfg[3] = (f[4] >> 3) & 1;
        return;
    case 2:
        g_carcfg[1] = (f[8] & 0x18) == 8;
        return;
    case 3:
        break;
    default:
        return;
    }
    switch ((f[5] >> 2) & 7) {
    case 0:
        g_carcfg[4] = 7;
        break;
    case 1:
        g_carcfg[4] = 8;
        break;
    case 2:
        if (g_carcfg[2] != 0)
            g_carcfg[4] = 5;
        else if (g_cfg7ff_180 != 0 || g_carcfg[0] != 0)
            g_carcfg[4] = 1;
        else
            g_carcfg[4] = 3;
        break;
    case 3:
        if (g_carcfg[2] != 0)
            g_carcfg[4] = 6;
        else if (g_cfg7ff_180 != 0 || g_carcfg[0] != 0)
            g_carcfg[4] = 2;
        else
            g_carcfg[4] = 4;
        break;
    case 7:
        g_carcfg[4] = 9;
        break;
    default:
        g_carcfg[4] = 0;
        break;
    }
}

/* Stock a0 after a path that calls nothing: the address of the jump table
   entry of the platform switch (0x196D4 + 4 * platform). No caller uses it.
   The rewrite returns a copy, so the difftest sees the same a0. */
static const uint32_t stock_jt[10] = {
    0xffffb254, 0xffffb260, 0xffffb884, 0xffffb8a4, 0xffffb8c4, 0xffffb904, 0xffffb944, 0xffffb81a,
    0xffffb822, 0xffffb826,
};
#define A0_JT(p) ((uint32_t)&stock_jt[p])

/* Platform 1 (Tesla Model 3/Y): the compare tree on the CAN id.
   Returns the stock a0 (A0_JT(1)) when it calls no handler. */
static uint32_t dispatch_tesla(uint8_t *f)
{
    uint32_t id = *(uint16_t *)f;

    switch (id) {
    case 0x082: return can_082_heat(f);
    case 0x102: slot_store(0, f); return door_left_rx(f);
    case 0x103: return can_103_doors(f);
    case 0x118: return can_118_rx(f);
    case 0x129: return can_129_steer(f);
    case 0x132: return slot_store(1, f);
    case 0x1F9: return can_1f9_door_edit(f);
    case 0x20C: return can_20c(f);
    case 0x229: can_229_rx(f); return can_229_edit(f);
    case 0x238: return can_238(f);
    case 0x243: return can_243(f);
    case 0x249: can_249_rx(f); return can_249_edit(f);
    case 0x257: return can_257_speed(f);
    case 0x25A: return can_25a_tpms(f);
    case 0x25D: return can_25d_cp(f);
    case 0x266: return can_power(f, 1);
    case 0x273: slot_store(2, f); vc273_rx(f); return vc273_echo(f, 0);
    case 0x292: return slot_store(3, f);
    case 0x293: return can_293(f);
    case 0x2B4: return slot_store(4, f);
    case 0x2B6: return can_2b6(f);
    case 0x2E1: return can_2e1_frunk(f);
    case 0x2E5: return can_power(f, 0);
    case 0x2F3: return can_2f3(f);
    case 0x312: return can_312_pack_temp(f);
    case 0x31F: return can_31f_tpms(f);
    case 0x321: return can_321(f);
    case 0x332:
        if ((f[3] & 3) == 1)
            slot_store(5, f);
        return can_332_bms(f);
    case 0x333: return can_333_cp_edit(f);
    case 0x334:
        if (platform_check(chk_tesla_sum, f) == 0)
            return 0;
        tesla_seen_count(f);
        return vc334_rx(f);
    case 0x339: return vc339_lock_rx(f);
    case 0x33A: return can_33a_range(f);
    case 0x352:
        if ((f[3] & 3) == 0)
            return slot_store(6, f);
        if ((f[3] & 3) == 1)
            return slot_store(7, f);
        return A0_JT(1);
    case 0x370:
        can_370_epas(f);
        return platform_check(chk_tesla_sum, f);
    case 0x39D: return can_39d_brake(f);
    case 0x3B3: slot_store(8, f); vc3b3_mode_rx(f); return vc3b3_rx(f);
    case 0x3B6: return can_3b6_odometer(f);
    case 0x3C2:
        if ((f[3] & 3) == 0)
            slot_store(9, f);
        else if ((f[3] & 3) == 1)
            slot_store(10, f);
        can_3c2_buttons(f);
        return can_3c2_swc(f);
    case 0x3C3: can_3c3_rx(f); return can_3c3_edit(f);
    case 0x3D2: return slot_store(11, f);
    case 0x3D8: return can_3d8_altitude(f);
    case 0x3DF: return slot_store(12, f);
    case 0x3E2: return can_3e2_rx(f);
    case 0x3E3: return can_3e3_flag(f);
    case 0x3E9:
        slot_store(13, f);
        if (platform_check(chk_tesla_sum, f) == 0)
            return 0;
        can_3e9(f);
        return vc3e9_rx(f);
    case 0x3F5: slot_store(15, f); return can_3f5_lights(f);
    case 0x3FD:
        if ((f[3] & 7) != 0)
            return A0_JT(1);
        return slot_store(14, f);
    case 0x3FE: return can_3fe_brake_temp(f);
    case 0x401: return link_forward_401(f);
    case 0x405: return can_405_vin(f);
    case 0x4E2:
        if ((f[3] & 7) != 5)
            return A0_JT(1);
        return slot_store(16, f);
    case 0x4E3:
        if ((f[3] & 7) != 5)
            return A0_JT(1);
        return slot_store(17, f);
    case 0x4F3:
        /* Stock passes no argument: a0 is still the jump table address. */
        return seat_resend(A0_JT(1));
    case 0x678: can_678_count(f); return can_678_edit(f);
    case 0x679: slot_store(18, f); return can_679_edit(f);
    case 0x7FF: can_7ff_config(f); return A0_JT(1);
    default: return A0_JT(1);
    }
}

/* Stock 0x14904. Calls the frame handlers of the platform (0x2000BFD1).
   f = RX ring entry {u16 id; u8 dlc; u8 data[8]; u8 flag}. */
uint32_t platform_dispatch(uint8_t *f)
{
    uint32_t id;
    uint32_t plat = platform_get();

    /* Stock ends each path with a tail call, so a0 is the handler's a0. */
    switch (plat) {
    case 0:
        return platform_detect(f);
    case 1:
        return dispatch_tesla(f);
    case 2:
        id = *(uint16_t *)f;
        if (id == 0xA9)
            return key_a9(f);
        else if (id == 0x21C)
            return p2_21c(f);
        else
            return A0_JT(plat);
    case 3:
        id = *(uint16_t *)f;
        if (id == 0xA9)
            return key_a9(f);
        else if (id == 0x21C)
            return p3_21c(f);
        else
            return A0_JT(plat);
    case 4:
        id = *(uint16_t *)f;
        if (id == 0x189)
            return p4_189(f);
        else if (id == 0x68C)
            return p4_68c(f);
        else if (id == 0xA9)
            return key_a9(f);
        else
            return A0_JT(plat);
    case 5:
        id = *(uint16_t *)f;
        if (id == 0x3A1)
            return p5_3a1(f);
        else if (id == 0x3EA)
            return key_3ea(f);
        else if (id == 0x37A)
            return key_37a(f);
        else
            return A0_JT(plat);
    case 6:
        id = *(uint16_t *)f;
        if (id == 0xA9)
            return key_a9(f);
        else if (id == 0x498)
            return p6_498(f);
        else
            return A0_JT(plat);
    default:
        return platform_set(0);
    }
}

/* Stock 0x15044. Resets the car config and the state of every feature. */
void features_init(void)
{
    tmos_memset(g_carcfg, 0, 5);
    platform_clear();
    vin_clear();
    gesture_state_clear();
    bus_mon_init();
    brake_temp_clear();
    tpms_clear();
    gauge_vals_clear();
    can_679_clear();
    req_678_clear();
    battery_clear();
    vehicle_e8_clear();
    vehicle_bf0c_clear();
    doors_reset();
    lstalk_clear();
    rstalk_clear();
    vc273_reset();
    vc3b3_reset();
    vc334_reset();
    bodyctl_req_clear();
    ap_state_clear();
    turn_state_clear();
    swc_state_clear();
    can_3c3_3e2_clear();
    brake_clear();
    horn_reset();
    func_hold_clear();
    seat_init();
}

/* Stock 0x150C8. Timer callback: sets the button lock flag. */
void btn_lock_set(uint8_t *arg)
{
    g_btn_lock = 1;
}

/* Stock 0x150D0. Master switch (config byte 4 bit 3): 1 = module off. */
uint32_t master_switch_set(uint32_t off)
{
    g_cfg[4] = (g_cfg[4] & ~8) | (off & 1) << 3;
    cfg_save();
    bus_age_clear(0);
    status_push();
    if (off == 0)
        led_off();
    else
        led_on();
    return buzzer_beep_onoff(off ^ 1);
}

/* CAN 0x339 frame: data 00 00 00 00 00 F0 00 00 (RX ring entry layout). */
static const uint8_t frame_339[12] = {
    0x39, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0x00, 0x00, 0x00,
};

/* Stock 0x15122. Sends the 0x339 frame; a timer repeats it every 25 ms,
   3 frames in all. */
void send_339(uint8_t *arg)
{
    uint8_t buf[12];
    uint8_t n = g_send339_cnt + 1;

    g_send339_cnt = n;
    if (n <= 2)
        app_timer_start(send_339, 0, 25, 0);
    else
        g_send339_cnt = 0;
    memcpy(buf, frame_339, 12);
    can_tx(buf);
}
