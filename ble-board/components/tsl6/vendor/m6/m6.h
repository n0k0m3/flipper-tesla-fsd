/* Module m6 (stock 0x12200..0x1376C): BLE central (remote buttons) init and
   the F0 command, CAN set-up, filters, RX and TX, the settings tables in
   flash, and the A0 / B0 / AB / BA / B9 replies to the phone. */
#ifndef M6_H
#define M6_H
#include "tsl6.h"
#include "can_rx.h"

/* ---- BLE central state (0x2000BFF0, 24 bytes) ----
   The module connects as a BLE central to remote buttons ("F0" pairing). */
typedef struct {
    uint8_t task;          /* +0x0 TMOS task id of the central role */
    uint8_t pad1[3];
    uint8_t scan_on;       /* +0x4 1 while the phone asked for a scan (F0 01) */
    uint8_t pad5;
    uint16_t reply_conn;   /* +0x6 phone link that gets the F0 replies */
    uint8_t *pending;      /* +0x8 remote to add: 48-byte TMOS message */
    uint8_t auto_scan;     /* +0xC scan even when every remote is linked */
    uint8_t pad_d[11];
} central_t;

extern central_t g_central;   /* 0x2000BFF0 */

/* One remote link (12 bytes). */
typedef struct {
    int8_t rssi;           /* +0x0 0x80 = not known */
    uint8_t b1;            /* +0x1 */
    uint16_t handle;       /* +0x2 0xFFFE = free */
    uint8_t addr[6];       /* +0x4 BLE address */
    uint8_t state;         /* +0xA */
    uint8_t b11;
} remote_link_t;

#define REMOTE_LINKS 3
extern remote_link_t g_remote_links[REMOTE_LINKS];  /* 0x2000C008 (36 bytes) */

/* Second central task block (0x2000BFE4, 12 bytes). */
typedef struct {
    uint8_t task;          /* +0x0 TMOS task id */
    uint8_t scan_timed;    /* +0x1 */
    uint8_t pad[9];
    uint8_t b11;           /* +0xB */
} central2_t;

extern central2_t g_central2;  /* 0x2000BFE4 */

/* ---- Settings blocks in RAM and their flash pages ----
   Each 256-byte table ends in 0x5A (byte 255) when it is valid. */
#define TABLE_OK 0x5A
#define REMOTE_ENTRY 0x2B   /* 43-byte entry; +3 = BLE address */
#define REMOTE_TABLE 0x85   /* 3 entries + 1 spare byte + marker at +0x84 */

extern uint8_t g_cfg_block[0x70];     /* 0x2000C2B8 config (A3), flash 0x08077A00 */
extern uint8_t g_ba_table[256];       /* 0x2000C328 BLE buttons (BA), flash 0x08077800 */
extern uint8_t g_remote_table[REMOTE_TABLE]; /* 0x2000C428 remotes (F0), flash 0x08077500 */
extern uint32_t g_ab_table[64];       /* 0x2000C4B0 shortcuts (AB), flash 0x08077900 */
extern uint32_t g_rgb_zone[64];       /* 0x2000C5B0 ambient light tab 0 (B9), flash 0x08077700 */
extern uint32_t g_rgb_car[64];        /* 0x2000C6B0 ambient light tab 1 (B9), flash 0x08077600 */

/* .data pointers to the blocks (gp+116 ... gp+136). */
extern uint8_t *g_cfg;                /* 0x20004074 -> 0x2000C2B8 */
extern uint8_t *g_ba_table_p;         /* 0x20004078 -> 0x2000C328 */
extern uint8_t *g_remote_table_p;     /* 0x2000407C -> 0x2000C428 */
extern uint8_t *g_ab_table_p;         /* 0x20004080 -> 0x2000C4B0 */
extern uint8_t *g_rgb_zone_p;         /* 0x20004084 -> 0x2000C5B0 */
extern uint8_t *g_rgb_car_p;          /* 0x20004088 -> 0x2000C6B0 */

extern uint8_t g_2000c1d4[];         /* frame slots, 19 x 12 bytes (m1) */
extern uint32_t SystemCoreClock;      /* 0x20004090 */
extern uint16_t g_can_ids_p4[3];      /* 0x20004098: 0x0A9, 0x189, 0x68C */
extern uint16_t g_can_ids_p5[3];      /* 0x200040A0: 0x3EA, 0x37A, 0x3A1 */

/* ---- BLE link table (0x2000C7B0, m7 owns it) ---- */
typedef struct {
    uint16_t handle;   /* 0xFFFE = free */
    uint8_t authed;    /* 1 after a good A8 password */
    uint8_t mode;      /* 0 idle (settings), 1 gauge stream, 2 CAN tool ... */
} m6_link_t;

typedef struct {
    uint8_t busy;      /* 1 for 300 ms after a stream link sends */
    uint8_t pad;
    m6_link_t link[2];
} m6_links_t;

extern m6_links_t g_ble_links;   /* 0x2000C7B0 */
#define LINK_FREE 0xFFFE

/* Resident slots 0xC4 and 0xC8. */
#define RES_C4() GATT_InitClient()
#define RES_C8(task) GATT_RegisterForInd(task)

/* ---- Functions of other modules (stock code) ----
   Names from the csv files of other modules can change while those modules are
   rewritten. So this header links to the fixed fun_<hex> names and gives
   readable aliases with macros. Names from symbols.csv are used directly. */
void *memset(void *dst, int val, uint32_t len);            /* newlib memset */
void memcpy(void *dst, const void *src, uint32_t len);   /* newlib memcpy */
uint32_t bus_alive(uint32_t i);      /* master switch on and bus i alive (age < 3) */
void bus_age_clear(uint32_t i);          /* bus age i = 0 */
uint32_t platform_get(void);            /* platform byte */
void irq_disable(void);                /* irq off (mstatus = 0x1800) */
void irq_enable(void);                /* irq on (mstatus = 0x1888) */
void gpio_init_pin(void *port, uint32_t pin, uint32_t mode); /* GPIO_Init, 50 MHz */
void flash_write_page(uint32_t addr, void *data);   /* erase and program a 256-byte page */
void uid14_get(uint8_t *dst);        /* chip UID14 */
uint64_t bootflag_read(void);            /* first 8 bytes of the boot flag */
uint32_t tick_ms_left(void);           /* ms until the next tick, plus 50 */
uint32_t buzzer_beeps(uint32_t n);      /* buzzer: n beeps */
void led_toggle(uint32_t a);          /* LED toggle */
#define memset_(d, v, n) memset(d, v, n)
#define bus_alive(i) bus_alive(i)
#define bus_age_clear(i) bus_age_clear(i)
#define platform_get() platform_get()
#define irq_disable() irq_disable()
#define irq_enable() irq_enable()
#define gpio_init_pin(p, n, m) gpio_init_pin(p, n, m)
#define flash_write_page(a, d) flash_write_page(a, d)
#define uid14_get(d) uid14_get(d)
#define bootflag_read() bootflag_read()
#define tick_ms_left() tick_ms_left()
#define buzzer_beeps(n) buzzer_beeps(n)
#define led_toggle(a) led_toggle(a)

uint32_t ble_can_sniff(void *frame);                    /* 0x82B8 */
void platform_dispatch(void *frame);                    /* 0x14904 */
uint32_t app_timer_start(void *cb, uint32_t reload, uint32_t ms, void *arg); /* 0x910A */
uint32_t ble_send_frame(uint32_t conn, uint32_t type, void *data, uint32_t len); /* 0x8D02 */
/* 0x13B56 (m7 names it; not in functions.tsv, so it has no fun_ name). */
void queued_frame_cb(uint8_t *msg);
void tune_up(void);
void platform_stop(void);                                   /* platform state reset */
void pair_end(void);
void scan_if_needed(void);
uint32_t link_free_slot(void);
void pair_result_send(uint32_t code);                          /* F0 reply with one code */
void remote_status_send(void);
void scan_stop(void);
void remote_task_event(void);
void key_task_event(void);

/* ---- m6 ---- */
void central_init(void);
void central_scan_set(uint32_t on, uint32_t auto_scan);
void central_scan_start(void);
void cmd_f0_remote(uint32_t conn, uint8_t *p, uint32_t len);

void can_filter_init(void *ids, uint32_t len);
void can_filter_clear(void);
void can_tx(uint8_t *frame);
void can_filter_accept_all(uint32_t skip);
void can_filter_close(uint32_t skip);
void can_filter_set(uint32_t skip, void *ids, uint32_t len);
void can_filter_platform(uint32_t platform);
void can_rx_poll(void);
void can_init(void);

void ab_table_save(void);
void ab_table_reset(void);
void ba_table_save(void);
void ba_table_reset(void);
void rgb_zone_save(void);
void rgb_zone_reset(void);
void rgb_car_save(void);
void rgb_car_reset(void);
void remote_table_save(void);
void remote_table_reset(void);
void cfg_save(void);
void cfg_reset(void);
void tables_load(void);
void factory_reset(void);

void links_busy_clear(void);
void a0_status_send(void);
uint32_t links_busy_check(void);
void ab_table_send(uint32_t conn);
void ba_table_send(uint32_t conn);
void b9_table_reply(uint32_t conn, uint32_t tab, uint32_t now);
uint32_t b0_gauge_send(uint32_t a0);

#endif
