/* Module m1 (stock 0x8180..0xA144): main loop, BLE links, app timers,
   UID check, buzzer, LED, boot flag and OTA, 0x370 EPAS handler. */
#ifndef M1_H
#define M1_H
#include "types.h"
#include "tsl6_port.h"
#include "app_timer.h"

/* Note: the test compares a0 at the return, also for stock void functions.
   So many functions here return the value that stock leaves in a0, and
   functions that leave a0 alone take it as an argument and return it. */

/* A function as an app timer callback (stock passes any function). */
#define TIMER_CB(f) ((app_timer_cb_t)(void *)(f))

/* ---- RAM globals (stock addresses) ---- */
extern uint32_t SystemCoreClock;       /* 0x20004090 */
extern uint8_t *g_cfg;                 /* 0x20004074, config block */
extern uint8_t g_uid_crc_ok;           /* 0x200040D3 */
extern uint8_t g_led_hold;             /* 0x200040D4 */
extern uint8_t g_ota_armed;            /* 0x200040D5, set by a good 53 */
extern uint8_t g_torque_mode;          /* 0x200040D6 */
extern uint8_t g_d8;                   /* 0x200040D8 */
extern uint8_t g_d9;                   /* 0x200040D9, countdown */
extern uint8_t g_manual_scroll_hold;   /* 0x200040DA */
extern uint8_t g_ap_window;            /* 0x200040DB, 6 s after AP on (0x370) */
extern uint8_t g_torque_step;          /* 0x200040DC, 0..3 */
extern uint8_t g_ap_370;               /* 0x200040DD, AP state from 0x370 */
extern uint8_t g_bus_age[3];           /* 0x200040E5, s since a frame per bus */
extern uint8_t g_20004186;
extern uint8_t g_tick_on;              /* 0x2000418C */
extern uint8_t g_turn_state;           /* 0x2000BEF5 */
extern uint8_t g_2000bf09;
extern uint8_t g_2000bfd0;
extern uint8_t g_platform;             /* 0x2000BFD1 */
extern uint8_t g_frame_slots[];        /* 0x2000C1D4, 12-byte slots */

/* ---- Functions of other modules ---- */
void can_rx_poll(void);
uint8_t gear_get(void);
uint32_t event_fire(uint32_t code);
uint8_t ap_active(void);  /* returns g_ap_active */
uint32_t chk_tesla_sum(uint8_t *frame);  /* data[7] = frame_checksum_last */
uint32_t ap_state_update(uint32_t on);
uint32_t tick_now(void);
uint8_t frame_checksum_last(uint8_t *frame);
uint32_t can_tx(uint8_t *frame);
void scroll_gesture_cb(uint8_t *arg);
uint32_t ble_send_any(uint16_t conn, uint8_t type, uint8_t *data, uint16_t len);
uint32_t ble_reply_byte(uint16_t conn, uint8_t type, uint8_t b);
void tables_load(void);
void can_init(void);
void links_init(void);
void features_init(void);
void tick_init(void);
void light_init(void);

/* ---- SDK (stock code) ---- */
void SystemCoreClockUpdate(void);
void Delay_Init(void);

/* ---- main.c ---- */
void main_loop(void);
uint8_t uid_crc_ok(void);
uint32_t clock_us(void);
uint32_t fixed_mode_get(void);
uint8_t turn_state_get(void);
uint8_t epas_torque_mode_get(void);
uint32_t bus_age_clear(uint32_t i);
uint32_t bus_alive(uint32_t i);
uint32_t range_edge(uint32_t v, uint32_t lo, uint32_t hi, uint8_t *flag, uint32_t arg);
uint32_t car_frames_seen(void);
uint8_t platform_get(void);
void frame_slot_copy(uint32_t load, uint32_t slot, void *buf);
uint8_t tick_is_on(void);
int main(void);
void app_init(void);

/* ---- uid.c ---- */
uint16_t crc16_step(uint16_t crc, uint8_t b);
void uid14_get(uint8_t *buf);
uint32_t uid14_check(const uint8_t *buf, uint8_t len);
uint16_t uid_crc(void);
uint8_t uid_crc_check(void);

/* ---- sys.c ---- */
void system_reset(void);
uint32_t gpio_init_pin(void *port, uint16_t pin, uint32_t mode);
uint32_t irq_disable(uint32_t a0);
uint32_t irq_enable(uint32_t a0);
uint32_t hw_init(void);
uint32_t flash_write_page(uint32_t addr, uint32_t *data);
uint32_t iwdg_init(uint32_t a0);

/* ---- led.c ---- */
uint32_t led_hold_cb(uint32_t a0);
uint32_t led_on(uint32_t a0);
uint32_t led_off(uint32_t a0);
uint32_t led_set_hold(uint32_t hold);
uint32_t led_toggle(uint32_t a0);
uint32_t led_init(void);

/* ---- ota.c ---- */
uint32_t ota_cmd(uint16_t conn, uint8_t type, uint8_t *data, uint16_t len);
uint32_t bootflag_store(const void *flag);
uint64_t bootflag_read(void);
uint32_t bootflag_set_mode(uint8_t mode);
void bootflag_write(void);
uint8_t bootflag_set_crc(uint16_t crc);
uint8_t bootflag_init(void);

/* ---- epas.c ---- */
uint32_t ap_window_end(uint32_t a0);
uint32_t feature_d8_eval(void);
uint32_t feature_d8_update(uint32_t a0);
uint32_t manual_hold_end(uint32_t a0);
uint8_t feature_d8_get(void);
uint32_t feature_d8_arm(uint32_t a0);
uint32_t feature_d8_level(uint32_t v);
uint32_t feature_d8_reset(void);
uint32_t manual_hold_start(void);
uint32_t manual_hold_extend(uint32_t a0);
uint32_t can_370_epas(uint8_t *f);

/* ---- ble.c, buzzer.c (see ble.h, buzzer.h) ---- */
void ble_init(void);
uint32_t buzzer_init(void);
#endif
