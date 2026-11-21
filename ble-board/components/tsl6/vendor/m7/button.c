/* Button action table (stock 0x1516E, 3494 bytes).
   The BB command and the steering wheel button mapping call it with an
   action code. Codes 1..0xFD; the stock code is a 253-entry jump table on
   (code - 1) & 0xFF. The case labels below are that index (code - 1), as in
   the stock table, so code = index + 1.
   Every path ends in a tail call, so the return value is the stock a0. */
#include "m7.h"

extern uint8_t g_carcfg[5];  /* 0x20004184 */
extern uint8_t g_btn_lock;   /* 0x20004189 */

uint32_t tick_start(uint32_t on, uint32_t b);
uint32_t cfg_save(void);
uint32_t seat_pulse(uint32_t, uint32_t, uint32_t);
uint32_t seat_cycle_set(uint32_t, uint32_t, uint32_t);
uint32_t seat_cycle_toggle(uint32_t);
uint32_t seat_recall(uint32_t);
uint32_t tick_is_on(void);
uint32_t adv_restart(uint32_t, uint32_t);
uint32_t buzzer_beeps(uint32_t);
/* tune_two_low: see m7.h */
uint32_t tune_falling3(uint32_t);
uint32_t tune_wobble(uint32_t);
uint32_t tune_up(uint32_t);
uint32_t tune_down(uint32_t);
uint32_t tune_scale(uint32_t);
uint32_t tune_three(uint32_t);
uint32_t buzzer_beep_onoff(uint32_t);
uint32_t reengage_off(void);
uint32_t ap_active(void);
uint32_t ap_engage(uint32_t);
uint32_t vc2b6_app_set(uint32_t);
uint32_t vc2b6_app_get(void);
uint32_t mode_293_step(void);
uint32_t can_2f3_bf0d_set(uint32_t);
uint32_t can_2f3_bf0d_is1(void);
uint32_t can_2f3_bf0f_set(uint32_t);
uint32_t can_2f3_bf0f_on(void);
uint32_t gesture_start(uint32_t);
uint32_t bf1d_get(void);
uint32_t bf1d_request(uint32_t);
uint32_t bf2d_start12(void);
uint32_t bf33_set(uint32_t);
uint32_t bf33_get(void);
uint32_t bf32_horn_set(uint32_t);
uint32_t swc_mode32_get(void);
uint32_t swc_edit50_start(void);
uint32_t flash_start3(void);
uint32_t highbeam_set(uint32_t);
uint32_t highbeam_get(void);
uint32_t flash_mode_set(uint32_t, uint32_t);
uint32_t flash_active_get(void);
uint32_t turn_signal(uint32_t, uint32_t);
uint32_t lstalk_button(void);
uint32_t gear_get(void);
uint32_t gear_request(uint32_t);
uint32_t heat_request_set(uint32_t);
uint32_t heat_request_get(void);
uint32_t cp_request(uint32_t);
uint32_t cp_state_get(void);
uint32_t park_timer_set(uint32_t);
uint32_t action_when_ready(uint32_t);
uint32_t frunk_open_when_ready(void);
uint32_t action_18_when_ready(void);
uint32_t action_1a_run(void);
uint32_t action_rstalk_d8(void);
uint32_t horn_melody_start(uint32_t);
uint32_t horn_chirps_start(uint32_t, uint32_t);
uint32_t vc273_clear_bits_off_cb(uint32_t);
uint32_t vc273_clear_bits_on(void);
uint32_t vc273_clear_bits_10s(void);
uint32_t vc273_b4_set(uint32_t);
uint32_t vc273_b4_get(void);
uint32_t front_fog_strobe_set(uint32_t, uint32_t);
uint32_t front_fog_strobe_get(void);
uint32_t rear_fog_strobe_set(uint32_t, uint32_t);
uint32_t rear_fog_strobe_get(void);
uint32_t front_fog_burst_set(uint32_t, uint32_t);
uint32_t front_fog_burst_get(void);
uint32_t rear_fog_burst_set(uint32_t, uint32_t);
uint32_t rear_fog_burst_get(void);
uint32_t rear_fog_on_set(uint32_t, uint32_t);
uint32_t rear_fog_on_get(void);
uint32_t front_fog_on_set(uint32_t, uint32_t);
uint32_t front_fog_on_get(void);
uint32_t vc273_d3_req(uint32_t);
uint32_t vc102_d6_is2(void);
uint32_t vc273_b0_set(uint32_t);
uint32_t vc273_b0_get(void);
uint32_t vc273_lock_req_set(uint32_t);
uint32_t vc273_unlocked(void);
uint32_t sport_mode_set(uint32_t);
uint32_t sport_mode_get(void);
uint32_t stop_mode_set(uint32_t);
uint32_t vc334_d3_bit1_set(uint32_t);
uint32_t vc334_d3_bit1_get(void);
uint32_t vc334_d3_bit0_set(uint32_t);
uint32_t vc334_d3_bit0_get(void);
uint32_t vc3b3_b0_set(uint32_t);
uint32_t vc3b3_b0_get(void);
uint32_t headlight_req_set(uint32_t);
uint32_t hazard_request(uint32_t);
uint32_t hazard_toggle(uint32_t);

/* Stock 0x1516E. */
uint32_t button_action(uint32_t code)
{
    uint32_t r;

    /* Master switch off (config byte 4 bit 3): only codes 0xFA..0xFC
       (master switch toggle/on/off) run. */
    if ((g_cfg[4] & 8) != 0 && ((code + 6) & 0xFF) > 2)
        return code;
    /* The first action after the 20 ms lock only calls 0x17296(0, 3, 1). */
    if (g_btn_lock != 0) {
        g_btn_lock = 0;
        return seat_pulse(0, 3, 1);
    }
    uint32_t i = (code - 1) & 0xFF;
    if (i <= 7)
        app_timer_start(btn_lock_set, 0, 20, 0);
    switch (i) {
    case 0:
        return seat_pulse(4, 200, 0);
    case 1:
        return seat_pulse(8, 200, 0);
    case 2:
        return seat_pulse(0x80, 200, 0);
    case 3:
        return seat_pulse(0x40, 200, 0);
    case 4:
        return seat_pulse(1, 200, 0);
    case 5:
        return seat_pulse(2, 200, 0);
    case 6:
        return seat_pulse(0x20, 200, 0);
    case 7:
        return seat_pulse(0x10, 200, 0);
    case 8:
        return vc273_clear_bits_10s();
    case 9:
        return action_when_ready(1);
    case 10:
        return action_when_ready(4);
    case 0xb:
        return action_when_ready(7);
    case 0xc:
        return action_when_ready(10);
    case 0xd:
    case 0xe:
    case 0xf:
    case 0x10:
        return stop_mode_set((code - 0xE) & 0xFF);
    case 0x11:
        /* Off, unless config byte 4 bit 4 is set and 0xAAA8 is nonzero. */
        if ((g_cfg[4] & 0x10) != 0 && (r = ap_active()) != 0)
        return r;
        return tick_start(0, 1);
    case 0x12:
        return tick_start(1, 1);
    case 0x13:
        /* Toggle, with the same condition as code 0x12. */
        if ((g_cfg[4] & 0x10) != 0 && (r = ap_active()) != 0)
        return r;
        return tick_start((uint8_t)(tick_is_on() ^ 1), 1);
    case 0x14:
        return ap_engage((uint8_t)(ap_active() ^ 1));
    case 0x15:
        return ap_engage(0);
    case 0x16:
        return ap_engage(1);
    case 0x17:
        return action_rstalk_d8();
    case 0x18:
        return vc3b3_b0_set((uint8_t)(vc3b3_b0_get() ^ 1));
    case 0x19:
        return vc273_clear_bits_on();
    case 0x1a:
        return vc273_clear_bits_off_cb(0);
    case 0x1b:
        return heat_request_set(0);
    case 0x1c:
        return heat_request_set(1);
    case 0x1d:
        return heat_request_set((uint8_t)(heat_request_get() ^ 1));
    case 0x1e:
        return cp_request((uint8_t)(cp_state_get() ^ 1));
    case 0x1f:
    case 0x20:
        return cp_request(0);
    case 0x21:
        return cp_request(1);
    case 0x22:
        return vc273_b4_set((uint8_t)(vc273_b4_get() ^ 1));
    case 0x23:
        return vc273_b4_set(0);
    case 0x24:
        return vc273_b4_set(1);
    case 0x27:
        return frunk_open_when_ready();
    case 0x28:
        return action_18_when_ready();
    case 0x29:
        return action_1a_run();
    case 0x2a:
        return vc273_d3_req((uint8_t)(vc102_d6_is2() ^ 1));
    case 0x2b:
        return vc273_b0_set((uint8_t)(vc273_b0_get() ^ 1));
    case 0x2c:
        return bf32_horn_set(0);
    case 0x2d:
        return bf32_horn_set(1);
    case 0x2e:
        return flash_mode_set(0, 0);
    case 0x2f:
        return flash_mode_set(1, 0);
    case 0x30:
        return front_fog_strobe_set(0, 0);
    case 0x31:
        return front_fog_strobe_set(1, 0);
    case 0x32:
        return front_fog_burst_set(0, 0);
    case 0x33:
        return front_fog_burst_set(1, 0);
    case 0x34:
        return rear_fog_strobe_set(0, 0);
    case 0x35:
        return rear_fog_strobe_set(1, 0);
    case 0x36:
        return rear_fog_burst_set(0, 0);
    case 0x37:
        return rear_fog_burst_set(1, 0);
    case 0x38:
        return front_fog_on_set((uint8_t)(front_fog_on_get() ^ 1), 1);
    case 0x39:
        return front_fog_on_set(0, 0);
    case 0x3a:
        return front_fog_on_set(1, 0);
    case 0x3b:
        return vc273_lock_req_set((vc273_unlocked() == 0) + 1);
    case 0x3c:
        return vc273_lock_req_set(1);
    case 0x3d:
        return vc273_lock_req_set(2);
    case 0x3e:
        /* Config byte 4 bits 2:1: toggle between 0 and 3. */
        {
        uint32_t was_off = (g_cfg[4] & 6) == 0;
        uint32_t v = was_off ? 3 : 0;
        uint8_t c = g_cfg[4];
        if (((c >> 1) & 3) != v) {
            g_cfg[4] = (c & ~6) | v << 1;
            cfg_save();
        }
        reengage_off();
        return buzzer_beep_onoff(was_off);
        }
    case 0x3f:
        /* Config byte 4 bits 2:1 = 0. */
        {
        uint8_t c = g_cfg[4];
        if (((c >> 1) & 3) != 0) {
            g_cfg[4] = c & ~6;
            cfg_save();
        }
        return reengage_off();
        }
    case 0x40:
        /* Config byte 4 bits 2:1 = 3. */
        {
        uint8_t c = g_cfg[4];
        if (((c >> 1) & 3) != 3) {
            g_cfg[4] = c | 6;
            cfg_save();
        }
        return reengage_off();
        }
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
        return horn_chirps_start((0x46 - code) & 0xFF, 0);
    case 0x45:
        return can_2f3_bf0f_set((uint8_t)(can_2f3_bf0f_on() ^ 1));
    case 0x46:
        return can_2f3_bf0f_set(0);
    case 0x47:
        return can_2f3_bf0f_set(1);
    case 0x48:
        return can_2f3_bf0d_set((uint8_t)(can_2f3_bf0d_is1() ^ 1));
    case 0x49:
        return can_2f3_bf0d_set(0);
    case 0x4a:
        return can_2f3_bf0d_set(1);
    case 0x4d:
        return seat_cycle_set(0, 1, 1);
    case 0x4e:
        return seat_cycle_set(1, 0, 1);
    case 0x4f:
        return seat_cycle_toggle(1);
    case 0x50:
        return seat_cycle_toggle(2);
    case 0x51:
        return seat_cycle_set(0, 2, 1);
    case 0x52:
        return seat_cycle_set(2, 0, 1);
    case 0x53:
        return horn_melody_start(0);
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
        return horn_chirps_start((0x5A - code) & 0xFF, 1);
    case 0x59:
        return bf32_horn_set((uint8_t)(swc_mode32_get() ^ 1));
    case 0x5a:
        return flash_mode_set((uint8_t)(flash_active_get() ^ 1), 0);
    case 0x5b:
        return front_fog_strobe_set((uint8_t)(front_fog_strobe_get() ^ 1), 1);
    case 0x5c:
        return front_fog_burst_set((uint8_t)(front_fog_burst_get() ^ 1), 1);
    case 0x5d:
        return rear_fog_strobe_set((uint8_t)(rear_fog_strobe_get() ^ 1), 1);
    case 0x5e:
        return rear_fog_burst_set((uint8_t)(rear_fog_burst_get() ^ 1), 1);
    case 0x5f:
        return rear_fog_on_set((uint8_t)(rear_fog_on_get() ^ 1), 1);
    case 0x60:
        return rear_fog_on_set(0, 0);
    case 0x61:
        return rear_fog_on_set(1, 0);
    case 0x62:
        return flash_start3();
    case 99:
        return gear_request(4);
    case 100:
        return gear_request(2);
    case 0x65:
        return gear_request(1);
    case 0x67:
        return gear_request(gear_get() == 4 ? 2 : 4);
    case 0x68:
        return horn_melody_start(1);
    case 0x69:
        return horn_melody_start(2);
    case 0x6a:
        return turn_signal(1, 0);
    case 0x6b:
        return turn_signal(1, 1);
    case 0x6c:
        return turn_signal(0, 1);
    case 0x6d:
        return turn_signal(0, 2);
    case 0x6e:
        return turn_signal(1, 2);
    case 0x6f:
        return highbeam_set((uint8_t)(highbeam_get() ^ 1));
    case 0x70:
        return highbeam_set(0);
    case 0x71:
        return highbeam_set(1);
    case 0x72:
        return bf33_set((uint8_t)(bf33_get() ^ 1));
    case 0x73:
        return bf33_set(0);
    case 0x74:
        return bf33_set(1);
    case 0x75:
        return bf1d_request(0);
    case 0x76:
        return bf1d_request(1);
    case 0x77:
        return lstalk_button();
    case 0x79:
        return action_when_ready(2);
    case 0x7a:
        return action_when_ready(5);
    case 0x7b:
        return action_when_ready(8);
    case 0x7c:
        return action_when_ready(0xb);
    case 0x7d:
        return action_when_ready(3);
    case 0x7e:
        return action_when_ready(6);
    case 0x7f:
        return action_when_ready(9);
    case 0x80:
        return action_when_ready(0xc);
    case 0x81:
        return hazard_toggle(0);
    case 0x82:
        return hazard_request(1);
    case 0x83:
        return hazard_request(0);
    case 0x84:
        return action_when_ready(0xd);
    case 0x85:
        return action_when_ready(0xe);
    case 0x86:
        return action_when_ready(0xf);
    case 0x87:
        return bf1d_request((uint8_t)(bf1d_get() ^ 1));
    case 0x88:
        return gesture_start(0x11);
    case 0x89:
        return gesture_start(0x12);
    case 0x8a:
        return bf2d_start12();
    case 0x8b:
        return gesture_start(5);
    case 0x8c:
        return gesture_start(1);
    case 0x8d:
        return gesture_start(3);
    case 0x8e:
        return gesture_start(4);
    case 0x8f:
        return gesture_start(2);
    case 0x90:
        return gesture_start(((uint8_t)(g_carcfg[4] - 1) > 3) + 13);
    case 0x91:
        return gesture_start(9);
    case 0x92:
        return gesture_start(0xb);
    case 0x93:
        return gesture_start(0xc);
    case 0x94:
        return gesture_start(10);
    case 0x95:
        return sport_mode_set((uint8_t)(sport_mode_get() ^ 1));
    case 0x96:
        return sport_mode_set(0);
    case 0x97:
        return sport_mode_set(1);
    case 0x98:
        return action_when_ready(0x11);
    case 0x99:
        return action_when_ready(0x12);
    case 0x9a:
        return action_when_ready(0x13);
    case 0x9b:
        return action_when_ready(0x10);
    case 0x9e:
        return vc334_d3_bit0_set((uint8_t)(vc334_d3_bit0_get() ^ 1));
    case 0x9f:
        return vc334_d3_bit1_set((uint8_t)(vc334_d3_bit1_get() ^ 1));
    case 0xa9:
        return vc2b6_app_set((uint8_t)(vc2b6_app_get() ^ 1));
    case 0xaa:
        return headlight_req_set(3);
    case 0xab:
        return headlight_req_set(2);
    case 0xac:
        return headlight_req_set(1);
    case 0xad:
        return headlight_req_set(0);
    case 0xae:
        return swc_edit50_start();
    case 0xb1:
        return park_timer_set(1);
    case 0xb2:
        return mode_293_step();
    case 0xb3:
        return buzzer_beeps(500);
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xb7:
    case 0xb8:
        return buzzer_beeps((code - 0xB4) & 0xFFFF);
    case 0xb9:
        return tune_up(0);
    case 0xba:
        return tune_down(0);
    case 0xbb:
        return tune_scale(0);
    case 0xbc:
        return tune_three(0);
    case 0xbd:
        return tune_falling3(0);
    case 0xbe:
        return tune_wobble(0);
    case 0xbf:
        return tune_two_low(0);
    case 199:
    case 200:
    case 0xc9:
    case 0xca:
    case 0xcb:
    case 0xcc:
    case 0xcd:
    case 0xce:
    case 0xcf:
    case 0xd0:
        return seat_recall((code + 0x38) & 0xFF);
    case 0xe0:
        return adv_restart(0, 1);
    case 0xe1:
        return adv_restart(0xfc, 0);
    case 0xe2:
        return adv_restart(0xfd, 0);
    case 0xe3:
    case 0xe4:
    case 0xe5:
    case 0xe6:
    case 0xe7:
    case 0xe8:
    case 0xe9:
    case 0xea:
    case 0xeb:
    case 0xec:
    case 0xed:
    case 0xee:
    case 0xef:
    case 0xf0:
    case 0xf1:
    case 0xf2:
    case 0xf3:
    case 0xf4:
        return adv_restart((code + 0x1C) & 0xFF, 0);
    case 0xf6:
        buzzer_beeps(1);
        return adv_restart(200, 0);
    case 0xf7:
        buzzer_beep_onoff(0);
        return adv_restart(0xc9, 0);
    case 0xf8:
        buzzer_beep_onoff(1);
        return adv_restart(0xca, 0);
    case 0xf9:
        return master_switch_set(((g_cfg[4] >> 3) ^ 1) & 1);
    case 0xfa:
        return master_switch_set(1);
    case 0xfb:
        return master_switch_set(0);
    case 0xfc:
        tune_scale(0);
        return app_timer_start(system_reset, 0, 1000, 0);
}
  /* Codes without an action: a0 is still the code. */
  return code;
}
