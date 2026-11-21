/* Module m7 (stock 0x1376C..0x15F14): BLE link table, BLE command dispatch,
   platform CAN dispatch and the button action table. */
#ifndef M7_H
#define M7_H
#include "tsl6.h"

/* ---- BLE link table (0x2000C7B0, 10 bytes) ---- */

#define LINK_FREE 0xFFFE  /* handle value of a free entry */

/* Link modes (byte 3 of an entry). The commands A0/B0/C0/D0/D1 set them. */
enum {
    LINK_MODE_IDLE = 0,    /* settings link: gets A0 status, BA and AF pushes */
    LINK_MODE_GAUGE = 1,   /* B0 01: gauge stream */
    LINK_MODE_CAN = 2,     /* C0: CAN tool (filter and send) */
    LINK_MODE_INFO = 3,    /* D0 01 / D2 01 */
    LINK_MODE_RAW401 = 4,  /* D1 01: forwards CAN 0x401 data as D1 */
};

typedef struct {
    uint16_t handle;  /* connection handle, LINK_FREE when not used */
    uint8_t authed;   /* 1 after a good A8 password */
    uint8_t mode;     /* LINK_MODE_* */
} ble_link_t;

typedef struct {
    uint8_t busy;        /* 1 while a stream link holds the radio (0x13206) */
    uint8_t pad;
    ble_link_t link[2];  /* two BLE connections */
} ble_links_t;

extern volatile ble_links_t g_ble_links;  /* 0x2000C7B0 */

/* ---- Other RAM ---- */
extern uint8_t *g_cfg;              /* 0x20004074 (gp+116): pointer to the config block */
extern uint32_t g_gauge_last_ms;    /* 0x20004178 (gp+376): last B0 push */
extern uint32_t g_status_last_ms;   /* 0x2000417C (gp+380): last A0 push */

/* ---- Resident calls with the full 32-bit argument ----
   Stock passes the length register as is (no 16-bit mask). */
static inline uint8_t *msg_alloc32(uint32_t len)
{
    return ((uint8_t * (*)(uint32_t))RESIDENT_SLOT(0x70))(len);
}

/* ---- Functions of other modules (stock code) ---- */
typedef void (*timer_cb_t)(uint8_t *arg);
uint32_t clock_ms(void);
uint32_t app_timer_stop(void *cb);
uint32_t app_timer_start(void *cb, uint32_t reload, uint32_t ms, void *arg);
uint32_t ble_send_frame(uint32_t conn, uint32_t type, void *data, uint32_t len);
void ble_disconnect(void);
void system_reset(void);
uint32_t uid_crc_ok(void);                  /* uid_crc_ok: 1 when activated (0x200040D3) */
uint32_t links_busy_check(void);                 /* links_busy_check: 1 when a stream link is active; sets busy */
void a0_status_send(uint8_t *arg);             /* a0_status_send */
void b0_gauge_send(uint8_t *arg);             /* b0_gauge_send */
void factory_reset(void);                     /* factory_reset */
uint32_t buzzer_play(void *cb, uint32_t a, uint32_t b);  /* buzzer_play */
uint32_t tune_two_low(uint8_t *arg);          /* tune_two_low */

/* ---- m7: link helpers (link.c) ---- */
uint32_t ble_send_copy(uint32_t conn, uint32_t type, void *data, uint32_t len);
void link_set_mode(uint32_t conn, uint32_t mode);
uint32_t link_forward_401(uint8_t *frame);
void gauge_push(void);
uint32_t ba_send_later(void *data, uint32_t len);
void status_push(void);
void link_auth_timer(uint8_t *arg);
void link_update(uint32_t conn, uint32_t op);
uint32_t ble_send_any(uint32_t conn, uint32_t type, void *data, uint32_t len);
void ba_send_cb(uint8_t *msg);
void queued_frame_cb(uint8_t *p);
uint32_t ble_reply_byte(uint32_t conn, uint32_t type, uint32_t b);
uint32_t ble_reply_bd(uint32_t conn, uint32_t b);
void activation_nag(uint8_t *arg);
void factory_reset_reply(uint32_t conn, uint32_t type);
void links_init(void);

/* ---- m7: BLE commands (ble_cmd.c) ---- */
void ble_cmd_dispatch(uint32_t conn, uint32_t type, uint8_t *data, uint32_t len);

/* ---- m7: platform dispatch (platform.c) ---- */
uint32_t car_state_word(void);
uint32_t platform_dispatch(uint8_t *f);
void features_init(void);
void btn_lock_set(uint8_t *arg);
uint32_t master_switch_set(uint32_t off);
void send_339(uint8_t *arg);

/* ---- m7: button actions (button.c) ---- */
uint32_t button_action(uint32_t code);
#endif
