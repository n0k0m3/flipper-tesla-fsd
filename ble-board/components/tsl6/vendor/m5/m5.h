/* Module m5 (stock 0xFA56..0x12200): car frame handlers for the gauge,
   platform auto-detect, frame edits for other car makes, and the BLE
   central role that connects to BLE remotes. */
#ifndef M5_H
#define M5_H
#include "types.h"

/* Mark an m5 function that other m5 code calls. GCC must not inline it or use
   IPA facts about it: stock calls it, and the test compares a0 at return. */
#define M5_CALLED __attribute__((noipa))

/* Return from a void function with v in a0, as the stock code does (the
   test compares a0 also for void functions). Use it only where the
   compiler does not leave the same value there by itself. */
#define M5_RETURN_A0(v)                                          \
    do {                                                         \
        (void)(v);                                               \
        return;                                                  \
    } while (0)

/* Resident call with 32-bit arguments. Stock passes some u8/u16 arguments
   (task id, connection handle) as they came in, without a cast. The
   resident.h wrappers cast them; this keeps the stock register values. */
#define RES32(off, ret, args, ...) \
    (((ret (*) args)RESIDENT_SLOT(off))(__VA_ARGS__))

/* CAN frame as the RX ring and the frame cache keep it (12 bytes). */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;     /* nonzero: do not transmit */
} m5_frame_t;

/* ---- Stock functions of other modules (called, not rewritten here) ---- */
uint32_t clock_ms(void);                                   /* 0x81AC */
uint32_t app_timer_stop(void *cb);                         /* 0x9096 */
uint32_t app_timer_start(void *cb, uint32_t reload, uint32_t ms, void *arg); /* 0x910A */
void gauge_push(void);                                     /* 0x1386E */
void bus_age_clear(uint32_t a);
uint32_t turn_state_get(void);
uint32_t bus_alive(uint32_t a);
uint32_t range_edge(uint32_t v, uint32_t lo, uint32_t hi, uint8_t *flag, uint32_t snd);
uint32_t car_frames_seen(void);             /* car frames seen (0x2000BFD0 > 3) */
void frame_slot_copy(uint32_t dir, uint32_t idx, void *buf);     /* frame cache copy */
uint32_t ble_c0_send(uint32_t a, void *b, uint32_t c);
#include <string.h>
void tune_two_low(void);
void tune_up(void);
void led_off(void);
void led_toggle(void);
uint32_t hazard_state(void);
uint32_t speed_limit_index(void);
uint32_t ap_active(void);
void bus_mon_init(void);
uint32_t c250_is_2(uint32_t a);
void bf32_horn_set(uint32_t on);
void flash_mode_set(uint32_t a, uint32_t b);
uint32_t gear_get(void);
void vin_clear(void);
void key_track(uint32_t a, uint32_t b, void *c);
uint32_t frame_checksum_first(void *frame);
uint32_t frame_checksum_last(void *frame);
void cornering_light_on(void);
void front_fog_strobe_set(uint32_t a, uint32_t b);
void rear_fog_strobe_set(uint32_t a, uint32_t b);
void high_beam_hold_cb(void);
void high_beam_flash_cb(void);
void vc3e9_rx(void *frame);
void can_tx(void *frame);         /* CAN transmit of one frame */
void can_filter_platform(uint32_t platform);
void remote_table_save(void);
void ba_send_later(void *a, uint32_t b);
void status_push(uint32_t a);
void ble_send_any(uint32_t conn, uint32_t type, void *data, uint32_t len);
void button_action(uint32_t a);
void event_fire(uint32_t snd);
void seat_pulse(uint32_t a, uint32_t b, uint32_t c);

/* ---- m5 functions ---- */
void bodyctl_resend(void);
uint32_t speed_get(void);
void active_off(void);
void p6_21c_tick(void);
uint32_t chk_21c_p2(m5_frame_t *f);
uint32_t chk_498_p6(m5_frame_t *f);
uint32_t chk_21c_p3(m5_frame_t *f);
uint32_t chk_189_p4(m5_frame_t *f);
uint32_t chk_3a1_p5(m5_frame_t *f);
uint32_t chk_tesla_sum(m5_frame_t *f);
void key_release_cb(void);
void req_678_clear(void);
void func_hold_poll(void);
void steer_event(uint32_t snd);
void platform_set(uint32_t platform);
void platform_timeout(void);
uint32_t platform_check(uint32_t (*check)(m5_frame_t *), m5_frame_t *f);

#endif
