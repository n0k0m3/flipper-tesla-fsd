/* Module m2 (stock 0xA144..0xC05A): scroll gesture and tick, AP state,
   0x3C2 steering-wheel frame edits, and the state that goes with them.

   Most functions return uint32_t. Stock code often ends in a tail call or
   leaves a value in a0, and the test compares a0. So each function returns
   what stock leaves in a0, also where Ghidra shows void. */
#ifndef M2_H
#define M2_H
#include "tsl6.h"

/* ---- Calls into the resident API with the a0 result kept. ---- */

/* tmos_memset through its slot. Stock tail-calls it, so a0 is its result. */
static inline uint32_t res_memset(void *dst, uint32_t val, uint32_t len)
{
    return ((uint32_t (*)(void *, uint32_t, uint32_t))RESIDENT_SLOT(0x48))(dst, val, len);
}

/* TMOS_ProcessEventRegister through its slot, full a0 kept. */
static inline uint32_t res_event_register(uint32_t (*cb)(uint32_t, uint32_t))
{
    return ((uint32_t (*)(void *))RESIDENT_SLOT(0x80))((void *)cb);
}

/* tmos_start_reload_task through its slot, full a0 kept. */
static inline uint32_t res_start_reload_task(uint32_t task, uint32_t ev, uint32_t ticks)
{
    return ((uint32_t (*)(uint32_t, uint32_t, uint32_t))RESIDENT_SLOT(0x22c))(task, ev, ticks);
}

/* ---- Functions of other modules (stock names or fun_<hex>). ---- */

typedef uint32_t (*m2_timer_cb_t)(uint8_t *arg);

uint32_t app_timer_start(void *cb, uint32_t reload, uint32_t ms, uint8_t *arg); /* 0x910A */
uint32_t app_timer_stop(void *cb);                                              /* 0x9096 */
uint32_t gauge_push(void);          /* 0x1386E */
uint32_t tick_start(uint32_t on, uint32_t flag); /* 0x164E4 */
uint32_t tick_schedule(void);       /* 0x163E6 */
uint32_t fixed_mode_get(void);            /* returns 1 */
uint32_t bus_alive(uint32_t i);      /* 1 when the CAN bus i is alive (counter at 0x200040E5+i < 3) and cfg[4] bit 3 is 0 */
uint32_t platform_get(void);            /* returns g_platform */
uint32_t frame_slot_copy(uint32_t dir, uint32_t i, void *buf); /* copies saved frame i (0x2000C1D4 + 12 i) */
uint32_t tick_is_on(void);            /* returns 0x2000418C (tick timer on) */
uint32_t feature_d8_arm(void);
uint32_t feature_d8_reset(void);
uint32_t active_start(uint32_t jg);
uint32_t speed_get(void);
uint32_t steer_abs(void);
uint32_t flash_step(void *p);
uint32_t flash_active_get(void);
uint32_t lstalk_inject(uint32_t a, uint32_t b);
uint32_t gear_get(void);            /* returns 0x2000BF60 */
uint32_t rstalk_request(uint32_t a, uint32_t b, uint32_t c);
uint32_t hazard_req_set(uint32_t a);
uint32_t hazard_req_get(void);
uint32_t hazard_auto_update(uint32_t a);
uint32_t can_tx(void *p);
uint32_t status_push(uint32_t a);
uint32_t event_fire(uint32_t ev);    /* runs the user actions bound to event code ev */
uint32_t light_sides_update(uint32_t a, uint32_t b);
uint32_t vc273_d3b7_pulse(void);
uint32_t buzzer_beep_onoff(uint32_t x);
uint32_t tune_two_low(uint32_t x);
uint32_t frame_checksum_last(uint8_t *e);      /* frame checksum */
uint32_t buzzer_beeps(uint32_t x);
uint32_t vc273_bit0_get(void);
uint32_t cp_state_get(void);
uint32_t brake_event_update(void);
uint32_t trigger_on_change(uint8_t *st, uint32_t v, uint32_t ev_a, uint32_t ev_b, uint32_t p5, uint32_t p6);
uint32_t horn_links_high_beam(uint32_t x);
uint32_t high_beam_links_horn(uint32_t x);
uint32_t key_track(uint32_t code, uint32_t b, uint8_t *st);
uint32_t key_event(uint32_t a, uint32_t b, uint32_t c);
uint32_t manual_hold_extend(void);
uint32_t feature_d8_update(uint32_t x);
uint32_t manual_hold_start(void);
uint32_t manual_hold_end(uint32_t x);
uint32_t seat_nudge(int32_t x);
uint32_t horn_level(void);
uint32_t horn_chirps_start(uint32_t a, uint32_t b);
uint32_t horn_pulse(uint32_t on);

/* 0x9EAA has no entry in the export; m1 names it ap_window_end. */
uint32_t ap_window_end(uint8_t *arg);

/* ---- Globals (stock addresses). ---- */

extern uint8_t *g_cfg;                 /* 0x20004074: config block pointer */
extern uint8_t g_200040d8;             /* u8: gesture state block start (5 bytes, d8..dc) */
extern uint8_t g_manual_scroll_hold;   /* 0x200040DA */
extern uint8_t g_200040db;             /* u8: m1 calls it g_ap_window */
extern uint8_t g_200040dc;             /* u8: 2-bit counter, m1 calls it g_torque_step */
extern uint8_t g_gesture_dir;          /* 0x200040DE */
extern uint8_t g_gesture_gap;          /* 0x200040DF */
extern uint8_t g_200040e0;             /* u8: wheel select 0, 1, 3 */
extern uint8_t g_gesture_code;         /* 0x2000BF2C */
extern uint8_t g_ap_active;            /* 0x2000BF00: AP on (from 0x3E9 or 0x370) */
extern uint8_t g_200040e1;             /* u8: state 0..2 with 0xFA7C */
extern uint8_t g_20004185;             /* u8 */
extern uint8_t g_20004186;             /* u8 */
extern uint8_t g_20004188;             /* u8: car variant (1..9) */
#define g_variant g_20004188
extern uint16_t g_2000c284;            /* u16: bit 8 used by ap_engage */
extern uint16_t g_2000c28e;            /* u16 */

/* AP block 0x2000BF00..0x2000BF09 (10 bytes), g_ap_active is byte 0. */
extern uint8_t g_bf01;                 /* 0x2000BF01: 2 s engage window open */
extern uint8_t g_bf06;                 /* 0x2000BF06 */
extern uint8_t g_bf07;                 /* 0x2000BF07: 0x238 speed limit index */
extern uint8_t g_bf08;                 /* 0x2000BF08 */

/* Bus monitor 0x200040E4..E7 and the 0x2B6/0x293/0x243 bytes E8..EB. */
extern uint8_t g_bus_task;             /* 0x200040E4: TMOS task id */
extern uint8_t g_bus_age[3];           /* 0x200040E5: seconds since a frame on bus 0..2, max 3 */
extern uint8_t g_200040e8, g_200040e9, g_200040ea, g_200040eb;
#define g_e8 g_200040e8                /* app value for 0x2B6 */
#define g_e9 g_200040e9                /* 0x2B6 edit pending */
#define g_ea g_200040ea                /* 0x293 mode, bit 7 = edit pending */
#define g_eb g_200040eb                /* 0x243 state-5 count */
extern uint8_t g_20004184;             /* u8: 1 when 0x243 count is 4 */

/* Vehicle block 0x2000BF0C..0x2000BF1B (16 bytes). */
extern uint8_t g_2000bf0c, g_2000bf0d, g_2000bf0e, g_2000bf0f, g_2000bf10, g_2000bf14;
extern uint16_t g_2000bf12, g_2000bf16, g_2000bf18, g_2000bf1a;
#define g_bf0c g_2000bf0c              /* 0x2F3 data[2] bits 5:4, car value */
#define g_bf0d g_2000bf0d              /* app value */
#define g_bf0e g_2000bf0e              /* 0x2F3 field, car value */
#define g_bf0f g_2000bf0f              /* app value */
#define g_bf10 g_2000bf10              /* 0x2F3 override on */
#define g_bf12 g_2000bf12              /* 0x20C data[1..2] value */
#define g_bf14 g_2000bf14              /* 0x321 data[5] */
#define g_bf16 g_2000bf16              /* 0x243 11-bit value */
#define g_bf18 g_2000bf18              /* 0x20C data[0..1] 11 bits */
#define g_bf1a g_2000bf1a              /* 0x20C data[4..5] 10 bits */

/* 0x3C2 button decoder. */
extern uint8_t g_200040ec;             /* u8: button code from page 0, sent on page 1 */
#define g_btn_code g_200040ec
extern uint8_t g_200040ed, g_200040ee, g_200040ef, g_200040f0, g_200040f1; /* trigger_on_change states */
extern uint8_t g_2000bf1c, g_2000bf1d, g_2000bf1e, g_2000bf1f, g_2000bf20, g_2000bf21;
#define g_bf1c g_2000bf1c              /* 0x3C2 page 0 data[0] bit 4 */
#define g_bf1d g_2000bf1d              /* toggled by a button */
#define g_bf1e g_2000bf1e
#define g_bf1f g_2000bf1f
#define g_bf20 g_2000bf20              /* c24f_is_2 result, 100 ms poll */
#define g_bf21 g_2000bf21              /* c250_is_2 result, 100 ms poll */
extern uint8_t g_2000bf24;             /* state block for key_track */
#define g_bf24 g_2000bf24
extern uint8_t g_2000c24f, g_2000c250;
extern uint8_t g_2000c1f4;             /* byte 32 of the saved frames 0x2000C1D4 */
#define g_2000c28e_b (*(uint8_t *)&g_2000c28e)

/* 0x3C2 editor block 0x2000BF2C..0x2000BF34. g_gesture_code is byte 0. */
extern uint8_t g_2000bf2d, g_2000bf2e, g_2000bf2f, g_2000bf30, g_2000bf31;
extern uint8_t g_2000bf32, g_2000bf33, g_2000bf34;
#define g_bf2d g_2000bf2d              /* page-1 frames left with data[0] bits 6:5 = 2 */
#define g_bf2e g_2000bf2e              /* same, also data[1] bits 5:4 = 2 */
#define g_bf2f g_2000bf2f              /* data[0] bit 2 frame count */
#define g_bf30 g_2000bf30
#define g_bf31 g_2000bf31
#define g_bf32 g_2000bf32
#define g_bf33 g_2000bf33              /* force data[0] bit 2 */
#define g_bf34 g_2000bf34              /* one-shot request (bf34_request) */

/* Turn signal and re-engage block 0x2000BEF0..0x2000BEFC (13 bytes). */
extern uint8_t g_lc_arm;               /* 0x2000BEF0: lane change armed */
extern uint8_t g_reengage_state;       /* 0x2000BEF1: 0 off, 1 idle, 2 wait, 3 retry timer */
extern uint8_t g_reengage_tries;       /* 0x2000BEF2 */
extern uint8_t g_lc_side;              /* 0x2000BEF3: 1 left, 2 right */
extern uint8_t g_lc_stage;             /* 0x2000BEF4 */
extern uint8_t g_turn_state;           /* 0x2000BEF5: 0 off, 1 left, 2 right, 3 hazard */
extern uint8_t g_ind_left;             /* 0x2000BEF6: 0x3F5 data[0] bits 1:0 */
extern uint8_t g_ind_right;            /* 0x2000BEF7: 0x3F5 data[0] bits 3:2 */
extern uint8_t g_bef8;                 /* 0x2000BEF8: 0x3F5 data[4] bit 2 */
extern uint8_t g_bef9;                 /* 0x2000BEF9: 0x3F5 data[4] bits 5:4 */
extern uint8_t g_befa;                 /* 0x2000BEFA: 0x3F5 data[4] bit 6 */
extern uint8_t g_befb;                 /* 0x2000BEFB: 0x3F5 data[4] bit 0 */
extern uint8_t g_hazard;               /* 0x2000BEFC: 0x3F5 data[0] bits 7:4 */

/* ---- m2 functions. ---- */

uint32_t scroll_gesture(uint32_t jg);  /* 0xA144 */
uint32_t scroll_gesture_cb(uint8_t *arg);       /* 0xA22A */
uint32_t scroll_tick(uint32_t jg);     /* 0xA22E */
uint32_t gesture_state_clear(void);               /* 0xA2C6 */
uint32_t ap_active(void);               /* 0xAAA8 */
uint32_t ap_engage_allowed(void);
uint32_t ap_reengage_cb(uint8_t *arg);
uint32_t lane_change_send(uint32_t x, uint32_t dir);
uint32_t lane_change_cb(uint8_t *arg);
uint32_t lane_change_cb2(uint8_t *arg);
uint32_t turn_signal_event(uint32_t x);
uint32_t lane_change_arm(uint32_t a0);
uint32_t reengage_cancel(uint32_t a0);
uint32_t reengage_off(uint32_t a0);
uint32_t can_3f5_lights(uint8_t *d);
uint32_t reengage_active(void);
uint32_t lights_gauge_bits(void);
uint32_t ind_state2_bits(void);
uint32_t hazard_state(void);
uint32_t c28e_field_state(void);
uint32_t turn_state_clear(void);
uint32_t bf08_get(void);
uint32_t ap_engage(uint32_t on);
uint32_t ap_on_retry_cb(uint8_t *arg);
uint32_t ap_on(uint32_t a0);
uint32_t ap_off(uint32_t a0);
uint32_t engage_window_end(uint8_t *arg);
uint32_t ap_engage_second_press(uint8_t *arg);
uint32_t can_238(uint8_t *e);
uint32_t speed_limit_index(void);
uint32_t bf06_get(void);
uint32_t c284_bit8_get(void);
uint32_t ap_auto_mode(uint32_t x);
uint32_t ap_state_update(uint32_t on);
uint32_t ap_state_update_v12(uint32_t on);
uint32_t can_3e9(uint8_t *e);
uint32_t ap_gauge_status(void);
uint32_t ap_state_clear(void);
uint32_t bus_mon_event(uint32_t task, uint32_t events);
uint32_t bus_mon_init(void);
uint32_t can_2b6(uint8_t *e);
uint32_t vc2b6_app_set(uint32_t x);
uint32_t vc2b6_app_get(void);
uint32_t can_293(uint8_t *e);
uint32_t mode_293_step(void);
uint32_t vehicle_e8_clear(void);
uint32_t hvac_override_start(uint8_t *arg);
uint32_t hvac_override_stop(uint8_t *arg);
uint32_t can_243(uint8_t *e);
uint32_t can_321(uint8_t *e);
uint32_t can_20c(uint8_t *e);
uint32_t can_243_value_get(void);
uint32_t can_321_d5_get(void);
uint32_t can_20c_v10_get(void);
uint32_t can_20c_v11_get(void);
uint32_t can_2f3(uint8_t *e);
uint32_t can_2f3_bf0d_set(uint32_t x);
uint32_t can_2f3_bf0d_is1(void);
uint32_t can_2f3_bf0f_set(uint32_t x);
uint32_t can_2f3_bf0f_on(void);
uint32_t hvac_override_check(uint32_t a0);
uint32_t can_2f3_app_follow(uint32_t a0);
uint32_t vehicle_bf0c_clear(void);
uint32_t resend_frame10_cb(uint8_t *arg);
uint32_t c24f_is_2(void);
uint32_t c24f_poll(uint8_t *arg);
uint32_t c250_is_2(void);
uint32_t c250_poll(uint8_t *arg);
uint32_t can_3c2_buttons(uint8_t *e);
uint32_t bf34_request(uint32_t a0);
uint32_t bf1c_get(void);
uint32_t bf1d_get(void);
uint32_t bf1d_set(uint32_t x);
uint32_t bf1d_request(uint32_t x);
uint32_t bf2d_start12(uint32_t a0);
uint32_t bf31_ptr(void);
uint32_t bf30_ptr(void);
uint32_t bf33_set(uint32_t x);
uint32_t bf33_get(void);
uint32_t bf32_horn_set(uint32_t x);
uint32_t can_3c2_swc(uint8_t *e);
uint32_t gesture_code_set(uint32_t code);      /* 0xB3D8 */
uint32_t gesture_start(uint32_t code);      /* 0xB3E2: start gesture code */

#endif
