/* Module m8 (stock 0x15F14..0x17828): BLE command A7, event rules, tick
   timer, RF link, light records, seat control over CAN 0x4F3. */
#ifndef M8_H
#define M8_H
#include "types.h"
#include "resident.h"

/* ---- Globals of other modules (stock addresses) ---- */
extern uint8_t *g_cfg;              /* 0x20004074, config block pointer */

/* CAN frame for 0x126DC (same layout as the RX ring entry). */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;                   /* must be 0 to send */
} can_frame_t;

/* Stock calls the resident API in place. Without this, gcc -Os makes
   out-of-line copies of these wrappers, which adds stack frames. */
static inline void tmos_memcpy(void *dst, void *src, uint32_t len) __attribute__((always_inline));
static inline void tmos_memset(void *dst, uint8_t val, uint32_t len) __attribute__((always_inline));

/* tmos_memset / tmos_memcpy that keep the a0 of the resident call. Some
   stock functions end in a tail call to them, or return a0 after them
   by accident. */
static inline uint32_t tmos_memset_a0(void *dst, uint8_t val, uint32_t len)
{
    return ((uint32_t (*)(void *, uint8_t, uint32_t))RESIDENT_SLOT(0x48))(dst, val, len);
}
static inline uint32_t tmos_memcpy_a0(void *dst, void *src, uint32_t len)
{
    return ((uint32_t (*)(void *, void *, uint32_t))RESIDENT_SLOT(0x4c))(dst, src, len);
}

/* ---- Functions of other modules ---- */
uint32_t app_timer_start(void *cb, uint32_t reload, uint32_t ms, uint8_t *arg);
uint32_t app_timer_stop(void *cb);
void scroll_tick(uint8_t jg);       /* 0xA22E */

uint32_t uid_crc_ok(void);            /* returns byte 0x200040D3 */
uint32_t bus_alive(uint32_t ch);     /* CAN channel ready */
uint32_t platform_get(void);            /* returns g_platform */
uint32_t frame_slot_copy(uint32_t dir, uint32_t slot, void *buf); /* copy CAN cache slot */
uint32_t tick_is_on(void);            /* returns g_tick_on */
uint32_t buzzer_beeps(uint32_t n);      /* beep pattern */
uint32_t tune_two_low(uint32_t a);      /* beep pattern ("error") */
uint32_t tune_up(void);            /* beep pattern ("saved") */
uint32_t buzzer_beep_onoff(uint32_t on);
uint32_t feature_d8_get(void);            /* returns byte 0x200040D8 */
uint32_t ap_engage(uint32_t on);
uint32_t vc2b6_app_set(uint32_t on);
uint32_t can_2f3_bf0f_set(uint32_t on);
uint32_t bf32_horn_set(uint32_t on);
uint32_t flash_mode_set(uint32_t a, uint32_t b);
uint32_t gear_get(void);            /* returns byte 0x2000BF60 */
uint32_t heat_request_set(uint32_t on);
uint32_t cp_request(uint32_t on);
uint32_t action_when_ready(uint32_t n);
uint32_t frunk_open_when_ready(void);            /* action_when_ready(25) */
uint32_t action_18_when_ready(void);            /* action_when_ready(24) */
uint32_t action_1a_run(void);
uint32_t vc273_d3b7_pulse(uint32_t a0);     /* sets byte 0x2000BFB5, returns a0 */
uint32_t front_fog_strobe_set(uint32_t a, uint32_t b);
uint32_t rear_fog_strobe_set(uint32_t a, uint32_t b);
uint32_t front_fog_burst_set(uint32_t a, uint32_t b);
uint32_t rear_fog_burst_set(uint32_t a, uint32_t b);
uint32_t vc273_d3_req(uint32_t on);
uint32_t vc102_d6_is2(void);
uint32_t vc273_lock_req_set(uint32_t n);
uint32_t sport_mode_set(uint32_t on);
uint32_t vc334_d3_bit1_set(uint32_t on);
uint32_t vc334_d3_bit0_set(uint32_t on);
uint32_t ap_active(void);            /* returns g_ap_active */
uint32_t can_679_set(void *rec);       /* local light output */
uint32_t active_off(uint32_t a);
uint32_t can_tx(void *frame);    /* CAN send {u16 id; u8 dlc; u8 data[8]; u8 flag} */
uint32_t cfg_save(void);           /* save the config to flash */
uint32_t status_push(uint32_t a);
uint32_t ble_send_any(uint32_t conn, uint32_t cmd, void *data, uint32_t len); /* BLE reply */
uint32_t ble_reply_bd(uint32_t conn, uint32_t v);  /* BLE reply BD {v} */
uint32_t send_339(uint32_t a);
uint32_t btn_lock_set(uint32_t a);
uint32_t button_action(uint32_t a);

/* ---- m8: tick timer (tick.c) ---- */
extern uint8_t g_tick_on;           /* 0x2000418C */
extern uint8_t g_tick_task;         /* 0x2000418D, TMOS task id */

uint32_t tick_ms_left(void);
uint32_t tick_schedule(void);
uint32_t tick_now(void);
uint32_t tick_start(uint32_t on, uint32_t notify);
uint32_t tick_event(uint32_t task, uint32_t events);
uint32_t tick_init(void);

/* ---- m8: RF link (rf_link.c) ---- */
typedef uint32_t (*rf_done_cb_t)(uint32_t status);

typedef struct {
    uint8_t task;                   /* +0, TMOS task id */
    rf_done_cb_t done;              /* +4, called once at the end */
    uint8_t tries;                  /* +8, tries left */
    uint8_t len;                    /* +9, packet length; 0 = idle */
    union {                         /* +10, the packet */
        uint16_t id_be;             /* id, big endian */
        uint8_t pkt[258];           /* id[2], type | seq << 4, data */
    };
} rf_link_t;                        /* 268 bytes */

/* WCH rfConfig_t. */
typedef struct {
    uint8_t mode;                   /* +0 LLEMode */
    uint8_t channel;                /* +1 */
    uint32_t frequency;             /* +4 */
    uint32_t access_addr;           /* +8 */
    uint32_t crc_init;              /* +12 */
    void *status_cb;                /* +16 */
    uint32_t channel_map;           /* +20 */
    uint8_t resv;                   /* +24 */
    uint8_t heart_period;           /* +25 */
    uint8_t hop_period;             /* +26 */
    uint8_t hop_index;              /* +27 */
    uint8_t rx_max_len;             /* +28 */
    uint8_t tx_max_len;             /* +29 */
} rf_config_t;                      /* 32 bytes */

extern rf_link_t g_rf;              /* 0x2000C7BC */
extern uint8_t g_rf_seq;            /* 0x2000418E, packet sequence 0..15 */

uint32_t rf_done(uint32_t status);
uint32_t rf_retry(void);
uint32_t rf_task_event(uint8_t task, uint32_t events);
uint32_t rf_status_cb(uint32_t sta, uint32_t crc, uint8_t *rx);
uint32_t rf_send(uint32_t id, uint32_t chan, uint32_t type, void *data, uint32_t len,
                 rf_done_cb_t done);
uint32_t rf_init(void);

/* ---- m8: seat control over CAN 0x4F3 (seat.c) ---- */
typedef struct {
    uint16_t p0;                    /* axis 0 position (12 bits) */
    uint16_t p1;                    /* axis 1 position (12 bits) */
    uint8_t p2;                     /* axis 2 position */
    uint8_t p3;                     /* axis 3 position */
    uint8_t mark;                   /* 'Z' = slot in use */
    uint8_t pad;
} seat_pos_t;                       /* 8 bytes, one memory slot */

typedef struct {
    uint8_t on;                     /* recall runs */
    uint8_t dir[4];                 /* 1 = axis moves up */
    uint8_t act[4];                 /* 1 = axis still moves */
    uint8_t pad;
    seat_pos_t target;
} seat_recall_t;                    /* 18 bytes */

typedef struct {
    uint8_t cycle_bits;             /* 0x2000C93C */
    uint8_t cycle_step;             /* 0x2000C93D, 0..5 */
    uint8_t pulse_cnt;              /* 0x2000C93E */
    uint8_t pulse_limit;            /* 0x2000C93F */
    uint32_t pulse_mask;            /* 0x2000C940 */
    uint32_t out;                   /* 0x2000C944, mask sent in 0x4F3 */
    uint16_t recall_ticks;          /* 0x2000C948, 50 ms ticks left */
    seat_recall_t seat[2];          /* 0x2000C94A, 0x2000C95C */
    uint8_t pad[2];
} seat_state_t;                     /* 0x34 bytes */

extern seat_state_t g_seat;         /* 0x2000C93C */

uint32_t seat_can_send(void);
uint32_t seat_axis_step(uint8_t *act, uint32_t up, uint32_t *out, uint32_t bit_up,
                        uint32_t bit_down, uint32_t target, uint32_t cur);
uint32_t seat_recall_tick(void);
uint32_t seat_pulse_tick(void);
uint32_t seat_pulse(uint32_t mask, uint32_t limit, uint32_t replace);
uint32_t seat_cycle_step(void);
uint32_t seat_pulse_long(uint32_t mask);
uint32_t seat_cycle_set(uint32_t on, uint32_t off, uint32_t notify);
uint32_t seat_cycle_get(void);
uint32_t seat_nudge(int32_t dir);
uint32_t seat_cycle_toggle(uint32_t bits);
uint32_t seat_recall(uint32_t slot);
uint32_t seat_recall_cancel(uint32_t mask);
uint32_t seat_save(uint32_t slot);
uint32_t seat_resend(uint32_t a0);
uint32_t seat_init(void);

/* ---- m8: event rules and light records (light.c) ---- */
typedef union {
    uint8_t b[8];                   /* 7 bytes used */
    uint16_t h[4];
    uint32_t w[2];
} light_rec_t;

typedef struct {
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t rec[16][7];
} rf_queue_t;                       /* 115 bytes */

extern uint8_t *g_evt_actions;      /* 0x20004080 -> 0x2000C4B0, {code, action} x 127 */
extern uint8_t *g_rfl_recs;         /* 0x20004084 -> 0x2000C5B0, RF light records */
extern uint8_t *g_light_recs;       /* 0x20004088 -> 0x2000C6B0, local light records */
extern uint8_t g_rfq_task;          /* 0x2000408C, TMOS task id of the light task */
extern uint8_t g_rfl_hold_l;        /* 0x2000418F */
extern uint8_t g_rfl_hold_r;        /* 0x20004190 */
extern uint8_t g_light_blink;       /* 0x20004191, blink phase */
extern uint8_t g_light_color;       /* 0x20004192, color cycle index 0..6 */
extern uint8_t g_rfl_prev_l;        /* 0x20004193 */
extern uint8_t g_rfl_prev_r;        /* 0x20004194 */
extern uint8_t g_light_idx;         /* 0x20004198, active local record */
extern uint8_t g_rf_hello_n;        /* 0x20004199, hellos sent */
extern uint8_t g_rf_linked;         /* 0x2000419A, 1 after the first ack */
extern rf_queue_t g_rfq;            /* 0x2000C8C8 */

uint32_t event_fire(uint32_t code);
uint32_t rfq_sent(uint32_t status);
uint32_t rf_hello(void);
uint32_t rfq_task_event(uint8_t task, uint32_t events);
uint32_t rfq_push(void *rec);
uint32_t rfl_send_green(void);
uint32_t rfl_send_mode(uint32_t mode, uint32_t arg, light_rec_t *r);
uint32_t light_rec_apply(uint32_t which, uint32_t idx);
uint32_t light_sides_update(uint32_t left, uint32_t right);
uint32_t rf_hello_start(void);
uint32_t rfl_green_timer(uint32_t stop);
uint32_t light_init(void);

/* ---- m8: BLE command A7 (cmd_a7.c) ---- */
uint32_t cmd_a7_control(uint32_t conn, uint32_t cmd, uint32_t code);

#endif
