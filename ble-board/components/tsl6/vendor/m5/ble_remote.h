/* BLE remotes: the module is also a BLE central. It scans for and connects
   to up to 3 bonded BLE remotes (HID buttons), and it reads key codes from
   the advertising data of some broadcast remotes. A key code selects an
   action from the key table (button_action runs the action). */
#ifndef BLE_REMOTE_H
#define BLE_REMOTE_H
#include "types.h"

#define REMOTE_N 3
#define CONN_FREE 0xFFFE

/* Link slot (12 bytes, 3 at 0x2000C008). */
typedef struct {
    int8_t rssi;           /* 0x80 = unknown */
    uint8_t batt;          /* battery level (0..100) */
    uint16_t conn;         /* connection handle, CONN_FREE = free slot */
    uint8_t addr[6];
    uint8_t step;          /* next CCCD to enable (index in handles[]); 10 = done */
    uint8_t pad;
} remote_link_t;

/* Bonded remote record (43 bytes, 3 in the config area; pointer 0x2000407C). */
typedef struct {
    uint8_t f0;
    uint8_t batt_handle;   /* value handle of the battery level (0x2A19) */
    uint8_t addr_type;
    uint8_t addr[6];       /* all 0 = empty record */
    char name[24];
    uint8_t handles[10];   /* CCCD handles to enable (value handle + 1), 0 = none */
} remote_rec_t;

/* Pairing context (tmos message, pointer at 0x2000BFF8) while the module
   pairs with a new remote. */
typedef struct {
    uint8_t addr_type;
    uint8_t addr[6];
    char name[24];
    uint8_t pad;
    uint16_t conn;
    uint8_t batt_handle;
    uint8_t step;          /* next handle to enable */
    uint8_t count;         /* handles found by the characteristic discovery */
    uint8_t handles[10];
} pair_ctx_t;

/* Key table entry (6 bytes, 42 entries; pointer 0x20004078). */
typedef struct {
    uint8_t act_short;
    uint8_t act_long;
    uint16_t code_lo;      /* key code, 32 bits in two halves (2-byte aligned) */
    uint16_t code_hi;
} key_entry_t;

#define KEY_N 42

/* GAP role event header (WCH gapEventHdr_t). */
typedef struct {
    uint8_t event;
    uint8_t status;
    uint8_t opcode;
} gap_hdr_t;

/* GAP_DEVICE_INFO_EVENT (WCH gapDeviceInfoEvent_t). */
typedef struct {
    gap_hdr_t hdr;
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t addr[6];
    int8_t rssi;
    uint8_t data_len;
    uint8_t *data;
} gap_dev_info_t;

/* ATT write request (WCH attWriteReq_t). */
typedef struct {
    uint16_t handle;
    uint16_t len;
    uint8_t *value;
    uint8_t sig;
    uint8_t cmd;
} att_write_req_t;

extern remote_link_t g_2000c008[REMOTE_N];  /* link slots */
extern uint8_t g_2000c02c[22];   /* last "CMT4..." name seen */
extern uint8_t g_2000c044[15];   /* last "TE" manufacturer data seen */
extern remote_rec_t *g_2000407c; /* bonded remote records */
extern key_entry_t *g_20004078;  /* key table */
extern uint32_t g_20004174;      /* u32: time (ms) of the last "TE" advert */

/* Central state block at 0x2000BFF0. */
extern uint8_t g_2000bff0;       /* central TMOS task id */
extern uint8_t g_2000bff4;       /* scan report id from the phone, 0 = no report */
extern uint16_t g_2000bff6;      /* phone connection handle (F0 replies) */
extern pair_ctx_t *g_2000bff8;   /* pairing context, 0 = not pairing */
extern uint8_t g_2000bffc;       /* "scan now" flag */
extern uint8_t g_2000bffd;       /* a connect is pending */
extern uint8_t g_2000bffe;       /* address type to connect to */
extern uint8_t g_2000bfff[6];    /* address to connect to */

/* Key state block at 0x2000BFE4. */
extern uint8_t g_2000bfe4;       /* key TMOS task id */
extern uint8_t g_2000bfe5;
extern uint32_t g_2000bfe8;      /* current key code */
extern uint16_t g_2000bfec;      /* value from the "CMT4" name digits */
extern uint8_t g_2000bfee;       /* long press */
extern uint8_t g_2000bfef;       /* key down */

void remote_rssi_cb(uint32_t conn, uint32_t rssi);
void remote_pair_state_cb(void);
uint32_t remote_passcode_cb(uint8_t *addr, uint32_t conn);
void remote_central_cb(uint8_t *ev);
void remote_cccd_enable(uint32_t conn, uint32_t handle);
uint32_t adv_find(gap_dev_info_t *ev, uint32_t type, uint8_t **ptr, uint8_t *len);
void pair_end(void);
uint32_t link_free_slot(void);
void scan_if_needed(void);
void pair_result_send(uint32_t code);
void remote_cccd_next(uint32_t slot, uint32_t done);
void remote_status_send(void);
void scan_stop(void);
void remote_found(uint32_t addr_type, uint8_t *addr);
void key_action(uint32_t released);
void key_broadcast(uint32_t code, uint32_t num, uint32_t long_);
#endif
