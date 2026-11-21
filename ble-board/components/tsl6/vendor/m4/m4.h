/* Module m4 (stock 0xDB8E..0xFA56): door and lock state, the key (button)
   event machine, and the CAN frames that the module echoes with changed bits
   (0x273, 0x334, 0x3B3, 0x3E9, 0x3C2 page 0). */
#ifndef M4_H
#define M4_H
#include "types.h"

/* CAN frame as the frame cache and the RX ring keep it (12 bytes). */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;      /* nonzero: can_frame_send does not send it */
} m4_frame_t;

typedef void (*m4_timer_cb_t)(uint8_t *arg);

/* ---- RAM of other modules (stock addresses) ---- */
extern uint8_t *g_cfg;                  /* 0x20004074, config block */
extern m4_frame_t g_2000c1d4[];         /* 0x2000C1D4, frame cache (frame_slot_copy) */
extern uint8_t g_20004186;              /* 0x20004186 */
extern uint8_t g_20004188;              /* 0x20004188, car variant (0x7FF mux 3) */

/* ---- RAM of this module ---- */
/* 0x2000BF84..0x2000BF9F: door state (memset 0x1C by doors_reset). */
extern uint8_t g_2000bf84, g_2000bf85;
extern uint8_t g_2000bf86, g_2000bf87, g_2000bf88, g_2000bf89; /* latch FL, FR, RL, RR */
extern uint8_t g_2000bf8b, g_2000bf8d;
extern uint8_t g_2000bf90, g_2000bf91;
extern uint32_t g_2000bf94, g_2000bf98;
extern uint8_t g_2000bf9c;              /* pending action code */
extern uint8_t g_20004126, g_20004127;
extern uint32_t g_20004134;             /* time of the last action_rstalk_d8 call, ms */

/* 0x2000412A..0x2000412F: key event state. */
extern uint8_t g_2000412a, g_2000412b, g_2000412c, g_2000412d, g_2000412e, g_2000412f;
extern uint8_t g_20004130, g_20004131, g_20004132;

/* 0x2000BFA0: horn driver. The horn is bit 2 of data[0] in the 0x3C2 page-0
   frame (the "pulse horn" setting, config byte 3 bits 7:6, sets its rate).
   horn_reset clears the 12 bytes. */
typedef struct {
    uint16_t idx;              /* next step */
    uint16_t len;              /* steps in the pattern */
    const uint16_t *pattern;   /* step times, ms */
    uint8_t level;             /* horn bit to send next toggle (inverted) */
    uint8_t fast;              /* short chirps */
    uint8_t count;             /* chirps left */
} horn_t;
extern horn_t g_horn;          /* 0x2000BFA0 */

/* 0x2000BFAC: state of the 0x273 echo (memset 0x18 by vc273_reset). */
typedef struct {
    uint8_t ac;          /* +0  0xBFAC one shot: data[3] bits 5:3 = 1 */
    uint8_t ad;          /* +1  0xBFAD one shot: data[2] bits 3:1 */
    uint8_t ae;          /* +2  0xBFAE one shot: data[3] bits 1:0 */
    uint8_t af;          /* +3  0xBFAF data[6] bit 5 as the car sent it */
    uint8_t b0;          /* +4  0xBFB0 data[6] bit 5 wanted */
    uint8_t b1;          /* +5  0xBFB1 data[0] bit 0 as the car sent it */
    uint8_t b2;          /* +6  0xBFB2 clear data[2].0, data[0].2, data[7].7 */
    uint8_t b3;          /* +7  0xBFB3 data[0] bit 3 on (5 s) */
    uint8_t b4;          /* +8  0xBFB4 data[7] bits 4:3 = 1 */
    uint8_t b5;          /* +9  0xBFB5 one shot: data[3] bit 7 */
    uint8_t b6;          /* +10 0xBFB6 one shot: data[0] bit 5 */
    uint8_t b7;          /* +11 0xBFB7 data[0] bit 3 on */
    uint8_t b8;          /* +12 0xBFB8 data[2] bit 7 on */
    uint8_t b9;          /* +13 0xBFB9 data[0] bit 3 flashes (fast) */
    uint8_t ba;          /* +14 0xBFBA data[0] bit 3 level of the flash */
    uint8_t bb;          /* +15 0xBFBB data[2] bit 7 flashes (fast) */
    uint8_t bc;          /* +16 0xBFBC data[2] bit 7 level of the flash */
    uint8_t bd;          /* +17 0xBFBD fast flash phase */
    uint8_t be;          /* +18 0xBFBE data[0] bit 3 flashes (burst) */
    uint8_t bf;          /* +19 0xBFBF data[0] bit 3 level of the burst */
    uint8_t c0;          /* +20 0xBFC0 data[2] bit 7 flashes (burst) */
    uint8_t c1;          /* +21 0xBFC1 data[2] bit 7 level of the burst */
    uint8_t c2;          /* +22 0xBFC2 burst step */
    uint8_t c3;          /* +23 0xBFC3 lock state from 0x339 is in the "ready" set */
} vc273_t;
extern vc273_t g_vc273;        /* 0x2000BFAC */

/* 0x20004138..0x2000413C: 0x334 echo state (memset 5 by vc334_reset). */
extern uint8_t g_20004138, g_20004139, g_2000413a, g_2000413b, g_2000413c;
extern uint8_t g_2000413d;
/* 0x20004140..0x20004142: 0x3B3 echo state (memset 3 by vc3b3_reset). */
extern uint8_t g_20004140, g_20004141, g_20004142;
/* 0x20004144..0x20004147: 0x3E9 echo state. */
extern uint8_t g_20004144, g_20004145, g_20004146, g_20004147;

/* ---- Functions of other modules ---- */
uint32_t app_timer_start(m4_timer_cb_t cb, uint32_t reload, uint32_t ms, uint8_t *arg);
uint32_t app_timer_stop(m4_timer_cb_t cb);
uint32_t clock_ms(void);
uint32_t tick_schedule(void);
uint32_t tick_start(uint32_t on, uint32_t notify);
uint32_t ap_off(void);
void gauge_push(void);
void system_reset(void);
uint32_t tick_is_on(void);
uint32_t buzzer_play(m4_timer_cb_t cb, void (*done)(void), uint32_t ms);
uint32_t uid_crc(void);
uint32_t buzzer_beeps(uint32_t n);
uint32_t tune_two_low(uint32_t x);
void tune_falling3(uint8_t *arg);
void tune_wobble(uint8_t *arg);
uint32_t tune_scale(uint32_t x);
uint32_t buzzer_beep_onoff(uint32_t on);
uint32_t bootflag_set_crc(void);
uint32_t feature_d8_arm(void);
uint32_t reengage_off(void);
uint8_t reengage_active(void);
uint8_t hazard_state(void);
uint32_t c28e_field_state(void);
uint32_t ap_engage_second_press(uint32_t x);
uint8_t ap_active(void);
uint32_t c284_bit8_get(void);
uint32_t ap_auto_mode(uint32_t x);
uint32_t ap_state_update_v12(uint32_t x);
uint32_t vehicle_e8_clear(void);
uint32_t hvac_override_stop(uint32_t x);
uint32_t hvac_override_check(void);
uint32_t can_2f3_app_follow(void);
uint32_t c24f_is_2(void);
uint32_t c250_is_2(void);
uint8_t *bf31_ptr(void);
uint8_t *bf30_ptr(void);
uint32_t bf33_set(uint32_t on);
uint32_t bf32_horn_set(uint32_t on);
uint32_t bf1d_set(uint32_t on);
uint32_t swc_mode32_get(void);
uint32_t swc_modes_off(void);
uint32_t flash_start3(void);
uint32_t highbeam_set(uint32_t on);
uint8_t highbeam_get(void);
uint32_t flash_mode_set(uint32_t a, uint32_t b);
uint32_t flash_cancel(void);
uint32_t highbeam_toggle(void);
uint32_t flash_active_get(void);
uint32_t gear_get(void);
uint32_t rstalk_request(uint32_t a, uint32_t b, uint32_t c);
uint32_t heat_request_set(uint32_t x);
uint32_t heat_request_get(void);
uint32_t cp_request(uint8_t on);
uint8_t cp_state_get(void);
uint32_t brake_ev_get(void);
uint32_t park_timer_set(uint32_t x);
void door_cmd_run(uint8_t *arg);
uint32_t hold_check(uint32_t secs, uint32_t active, uint8_t *state, uint32_t *t0);
uint32_t door_update(uint8_t *last, uint8_t *stored, uint32_t latch);
uint32_t door_output_set(uint32_t on);
uint32_t door_cmd_schedule(uint32_t ms);
void bodyctl_resend(uint8_t *arg);
uint32_t headlight_req_set(uint32_t x);
void hazard_toggle(uint8_t *arg);
uint32_t func_hold_start(uint32_t x);
uint16_t speed_get(void);
uint32_t can_tx(m4_frame_t *f);
uint32_t ab_table_reset(void);
uint32_t ba_table_reset(void);
uint32_t cfg_reset(void);
uint32_t status_push(uint32_t x);
uint32_t button_action(uint32_t action);
uint32_t event_fire(uint32_t event);
uint32_t rfl_green_timer(uint32_t on);
uint32_t seat_pulse_long(uint32_t x);
uint32_t seat_cycle_set(uint32_t a, uint32_t b, uint32_t c);
uint32_t seat_cycle_get(void);
uint32_t seat_recall_cancel(uint32_t x);
uint32_t car_frames_seen(void);
uint32_t frame_slot_copy(uint32_t dir, uint32_t idx, m4_frame_t *buf);

/* ---- This module ---- */
/* doors.c */
void action_when_ready(uint32_t code);
uint8_t bf84_get(void);
void frunk_open_when_ready(void);
void door_left_rx(m4_frame_t *f);
uint8_t bf85_get(void);
void action_18_when_ready(void);
void action_1a_run(void);
uint32_t doors_open_mask(void);
void doors_reset(void);
void features_off_cb(uint8_t *arg);
void action_rstalk_d8(void);
void features_off_later(void);

/* keys.c */
uint32_t key_event(uint32_t key, uint32_t count, uint32_t phase);
uint32_t key_track(uint32_t key, uint32_t idle, uint8_t *st);
uint32_t trigger_on_change(uint8_t *last, uint32_t val, uint32_t trig_a, uint32_t trig_b,
                           uint32_t a, uint32_t b);

/* checksum.c */
uint32_t frame_checksum_first(m4_frame_t *f);
uint32_t frame_checksum_last(m4_frame_t *f);

/* horn.c */
void high_beam_links_horn(uint32_t on);
uint32_t horn_links_high_beam(uint32_t on);
void horn_toggle(uint8_t *arg);
void horn_melody_step(uint8_t *arg);
uint32_t horn_melody_stop(void);
void horn_chirp_step(uint8_t *arg);
uint8_t horn_level(void);
void horn_melody_start(uint32_t n);
void horn_chirps_start(uint32_t count, uint32_t fast);
void horn_pulse(uint32_t on);
void horn_reset(void);

/* vc273.c */
void vc273_clear_bits_off_cb(uint8_t *arg);
void vc339_lock_rx(m4_frame_t *f);
void vc273_rx(m4_frame_t *f);
void vc273_echo(m4_frame_t *f, uint32_t always);
void cornering_light_on(void);
void cornering_light_off_cb(uint8_t *arg);
void fog_flash_set(uint8_t *phase, uint8_t *flag, uint32_t on, uint32_t beep,
                   uint32_t other, m4_timer_cb_t cb);
void fog_strobe_cb(uint8_t *arg);
void fog_burst_cb(uint8_t *arg);
void vc273_clear_bits_on(void);
void vc273_clear_bits_10s(void);
uint8_t vc273_bit0_get(void);
void vc273_d3b7_pulse(void);
void vc273_b4_set(uint32_t on);
uint8_t vc273_b4_get(void);
void front_fog_strobe_set(uint32_t on, uint32_t beep);
uint8_t front_fog_strobe_get(void);
void rear_fog_strobe_set(uint32_t on, uint32_t beep);
uint8_t rear_fog_strobe_get(void);
void front_fog_burst_set(uint32_t on, uint32_t beep);
uint8_t front_fog_burst_get(void);
void rear_fog_burst_set(uint32_t on, uint32_t beep);
uint8_t rear_fog_burst_get(void);
void rear_fog_on_set(uint32_t on, uint32_t beep);
uint8_t rear_fog_on_get(void);
void front_fog_on_set(uint32_t on, uint32_t beep);
uint8_t front_fog_on_get(void);
void vc273_d3_req(uint32_t on);
uint32_t vc102_d6_is2(void);
void vc273_b0_set(uint32_t on);
uint32_t vc273_b0_get(void);
void vc273_lock_req_set(uint32_t v);
uint8_t vc273_unlocked(void);
void vc273_frunk_req(void);
void vc273_b0_restore(void);
void vc273_reset(void);

/* vc334.c */
void vc334_rx(m4_frame_t *f);
void sport_mode_set(uint32_t on);
uint8_t sport_mode_get(void);
void stop_mode_set(uint32_t mode);
void vc334_d3_set(uint32_t on, uint32_t bit1);
void sport_mode_by_speed(uint32_t speed);
void vc334_d3_bit1_set(uint32_t on);
uint8_t vc334_d3_bit1_get(void);
void vc334_d3_bit0_set(uint32_t on);
uint8_t vc334_d3_bit0_get(void);
void sport_mode_restore(void);
void vc334_d3_clear(void);
void vc334_reset(void);

/* vc3b3.c */
void vc3b3_mode_rx(m4_frame_t *f);
void vc3b3_rx(m4_frame_t *f);
void vc3b3_b0_set(uint32_t on);
uint8_t vc3b3_b0_get(void);
void vc3b3_mode_set(uint32_t mode);
void vc3b3_b0_restore(void);
void vc3b3_reset(void);

/* vc3e9.c */
void high_beam_hold_cb(uint8_t *arg);
void high_beam_flash_cb(uint8_t *arg);
void vc3e9_rx(m4_frame_t *f);
#endif
