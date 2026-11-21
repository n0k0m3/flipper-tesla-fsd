/* Module m3 (stock 0xC05A..0xDB8E): CAN signal decoders for Tesla Model 3/Y
   (platform 1), frame edits that inject stalk, gear, door and charge-port
   commands, and the getters that the B0 gauge and BLE status read. */
#ifndef M3_H
#define M3_H
#include "types.h"

/* CAN frame as the RX ring and the frame cache keep it (12 bytes). */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;   /* nonzero: the frame was not received, do not send */
} m3_frame_t;

typedef void (*m3_timer_cb_t)(uint8_t *arg);

/* Register a0 at the return of a void function.
   The difftest compares a0 at return also for void functions. Some stock
   paths return (or tail-call a function that returns at once) with a0 still
   holding an argument. These two macros put the same value in a0. They do
   not change what the firmware does; callers ignore a0.
   RETURN_A0(v): return with a0 = v.
   CALL_A0(fn, v): call the void(void) function fn with a0 = v. */
#define RETURN_A0(v)                                            \
    do {                                                        \
        (void)(v);                                              \
        return;                                                 \
    } while (0)
#define CALL_A0(fn, v) (((void (*)(uint32_t))(fn))((uint32_t)(v)))

/* ---- Config and state of other modules ---- */
extern uint8_t *g_cfg;              /* 0x20004074: config block */
extern uint8_t g_20004186;          /* 0x20004186: car flag from 0x7FF (no stalk gear selector?) */
extern uint8_t g_20004188;          /* 0x20004188: car variant from 0x7FF mux 3 (0..9) */
extern uint8_t g_2000bf2e;          /* 0x2000BF2E: m2 gesture block */
extern uint8_t g_2000bf30;          /* 0x2000BF30: m2 gesture block */
extern uint8_t g_2000bf32;          /* 0x2000BF32: m2 gesture block */
extern uint8_t g_2000bf33;          /* 0x2000BF33: m2 gesture block */
extern uint8_t g_2000bf1c[28];      /* 0x2000BF1C: m2 gesture block */
extern uint8_t g_2000c1f4;          /* 0x2000C1F4: frame cache slot 2 (0x273) data[5] */
extern uint8_t g_2000c24a;          /* 0x2000C24A: frame cache slot 9 (0x3C2 page 0) data[7] */
extern uint8_t g_2000c28e;          /* 0x2000C28E: frame cache slot 15 (0x3F5) data[3] */
extern m3_frame_t g_2000c2ac;       /* 0x2000C2AC: frame cache slot 18 (0x679) */

/* ---- m3 globals (names in ../re/names/m3.csv) ---- */
/* 0x3C3 right wheel buttons and 0x3E2 */
extern uint8_t g_3e2_mode;          /* 0x2000BF38: 0x3E2 data[0] bits 1:0 */
extern uint8_t g_3e2_btn[8];        /* 0x2000BF3C: button state for 0x3E2 */
extern uint8_t g_3c3_btn[8];        /* 0x200040F4: button state for 0x3C3 page 1 */
extern uint8_t g_3c3_sw0;           /* 0x200040FC: 0x3C3 page 0 data[5] bits 1:0 */
extern uint8_t g_3c3_sw1;           /* 0x200040FD: 0x3C3 page 0 data[5] bits 3:2 */
extern uint8_t g_3c3_sw2;           /* 0x200040FE: 0x3C3 page 0 data[5] bits 7:6 */
/* 0x679 edit */
extern uint8_t g_679_cfg[7];        /* 0x20004100: 0x679 edit settings from BLE */
extern uint8_t g_beam_timer;        /* 0x20004107: 0x249 high beam state counter */
/* 0x249 left stalk */
extern uint8_t g_beam_req;          /* 0x2000BF44: one high beam request (set on shift to P) */
extern uint8_t g_highbeam_on;       /* 0x2000BF45: high beam toggle */
extern uint8_t g_flash_active;      /* 0x2000BF46: headlight flasher on */
extern uint8_t g_flash_phase;       /* 0x2000BF47: flasher frame phase */
extern uint8_t g_flash_repeat;      /* 0x2000BF48: flashes left (0 = no limit) */
extern uint8_t g_lstalk_hi;         /* 0x2000BF49: value for data[1] bits 7:6 */
extern uint8_t g_lstalk_code;       /* 0x2000BF4A: value for data[2] bits 3:0 */
extern uint8_t g_lstalk_frames;     /* 0x2000BF4B: frames left to edit */
extern uint8_t g_lstalk_btn[8];     /* 0x2000BF4C: button state for 0x249 */
/* 0x229 right stalk and 0x118 gear */
extern uint8_t g_rstalk_active;     /* 0x2000BF54 */
extern uint8_t g_rstalk_row;        /* 0x2000BF55: stalk position (5 = park button) */
extern uint8_t g_rstalk_phase;      /* 0x2000BF56: 0xFF = every frame, else frame mod 3 */
extern uint8_t g_rstalk_count;      /* 0x2000BF57: frames left */
extern uint32_t g_rstalk_word;      /* 0x2000BF54: the 4 bytes above as one word */
extern uint8_t g_rstalk_btn[8];     /* 0x2000BF58: button state for 0x229 */
extern uint8_t g_gear;              /* 0x2000BF60: DI gear (1 P, 2 R, 3 N, 4 D) */
extern uint8_t g_accel_pedal;       /* 0x2000BF61: 0x118 data[4] */
extern uint8_t g_gear_prev;         /* 0x20004108 */
/* TPMS and VIN */
extern uint8_t g_tpms[4];           /* 0x2000410C */
extern uint8_t g_vin_done;          /* 0x2000BF64 */
extern uint8_t g_vin[17];           /* 0x2000BF65 */
extern uint8_t g_vin_step;          /* 0x20004110 */
/* Battery, charge port, range */
extern uint8_t g_heat_req;          /* 0x2000BF78: battery heat request by the module */
extern uint16_t g_heat_word;        /* 0x2000BF78: bytes BF78 and BF79 as one halfword */
extern uint8_t g_heat_car;          /* 0x2000BF79: 0x082 data[0] bit 2 */
extern uint8_t g_cp_cmd;            /* 0x2000BF7A: 1 open, 2 close the charge port */
extern uint8_t g_cp_state;          /* 0x2000BF7B: 0x25D data[0] bit 6 */
extern uint16_t g_brick_v;          /* 0x2000BF7C */
extern uint16_t g_pack_t;           /* 0x2000BF7E */
extern uint8_t g_pack_heating;      /* 0x2000BF80 */
extern uint8_t g_soc;               /* 0x2000BF81 */
extern uint16_t g_range;            /* 0x2000BF82 */
/* Brake */
extern uint8_t g_brake_hard;        /* 0x20004111 */
extern uint8_t g_brake_on;          /* 0x20004114 */
extern uint8_t g_brake_ev;          /* 0x20004115 */
extern uint16_t g_brake_t[4];       /* 0x20004118 */
extern uint32_t g_brake_pack[2];    /* 0x20004120: 5 packed bytes (and byte 0x20004125) */
extern uint8_t g_door_open_flag;    /* 0x20004125 */
/* Doors */
extern uint8_t g_frunk_open;        /* 0x2000BF84 */
extern uint8_t g_trunk_open;        /* 0x2000BF85 */
extern uint8_t g_door_fl;           /* 0x2000BF86 */
extern uint8_t g_door_fr;           /* 0x2000BF87 */
extern uint8_t g_door_rl;           /* 0x2000BF88 */
extern uint8_t g_door_rr;           /* 0x2000BF89 */
extern uint8_t g_1f9_cmd;           /* 0x2000BF8A */
extern uint8_t g_latch_raw[5];      /* 0x2000BF8B */
extern uint8_t g_door_cmd;          /* 0x2000BF9C */
extern uint8_t g_103_sw0;           /* 0x20004128 */
extern uint8_t g_103_sw1;           /* 0x20004129 */

/* ---- Stock functions of other modules ---- */
void buzzer_beeps(uint32_t n);
void buzzer_beep_onoff(uint32_t on);
void feature_d8_level(uint32_t v);
void manual_hold_start(void);
void lane_change_arm(void);
uint8_t ap_active(void);
void hvac_override_stop(uint32_t v);
void gesture_start(uint32_t code);
void bf34_request(void);
uint8_t bf1c_get(void);
void bf32_horn_set(uint32_t v);
uint8_t turn_state_get(void);
void frame_slot_copy(uint32_t load, uint32_t slot, void *buf);
void key_event(uint32_t code, uint32_t n, uint32_t kind);
void key_track(uint32_t code, uint32_t idle, uint8_t *st);
uint32_t trigger_on_change(uint8_t *st, uint32_t v, uint32_t ev_a, uint32_t ev_b, uint32_t a, uint32_t b);
void high_beam_links_horn(uint32_t v);
void horn_melody_start(uint32_t v);
void front_fog_strobe_set(uint32_t a, uint32_t b);
void vc273_frunk_req(void);
void sport_mode_by_speed(void);
void vc3b3_rx(void *frame);
void vc3b3_mode_set(uint32_t v);
uint8_t bodyctl_d2b6_get(void);
void bodyctl_d2b6_set(uint32_t on);
void high_beam_timer_switch(uint32_t on);
void hazard_auto_update(uint32_t v);
void req_678_set(uint32_t gear, uint32_t on);
uint32_t speed_get(void);
void can_tx(void *frame);
void status_push(uint32_t v);
void event_fire(uint32_t ev);
void seat_recall(uint32_t n);
void gauge_push(void);
uint32_t clock_ms(void);
uint32_t app_timer_stop(m3_timer_cb_t cb);
uint32_t app_timer_start(m3_timer_cb_t cb, uint32_t reload, uint32_t ms, uint8_t *arg);

/* ---- m3 functions ---- */
#include "m3_funcs.h"
#endif
