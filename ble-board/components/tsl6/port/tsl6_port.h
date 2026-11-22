#pragma once
/*
 * tsl6_port.h — what the imported TSL6 code needs from the port, in place of
 * the WCH MCU header (ch32v20x.h) and the stock BLE ROM (resident.h slots).
 *
 * The TSL6 code is not reentrant: call every tsl6_* and port_* function from
 * one task (the CAN task of the board).
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- TMOS (WCH BLE OS) emulation: tsl6_tmos.c ---- */
uint32_t port_tmos_rand(void);
int port_tmos_memcmp(const void *a, const void *b, uint32_t len);  /* TRUE when equal, as the ROM */
int port_tmos_isbufset(const uint8_t *buf, uint8_t val, uint32_t len);
void port_tmos_memset(void *dst, uint8_t val, uint32_t len);
void port_tmos_memcpy(void *dst, const void *src, uint32_t len);
uint8_t port_tmos_set_event(uint8_t task, uint16_t ev);
uint8_t port_tmos_clear_event(uint8_t task, uint16_t ev);
uint8_t port_tmos_start_task(uint8_t task, uint16_t ev, uint32_t ticks);
uint8_t port_tmos_start_reload_task(uint8_t task, uint16_t ev, uint32_t ticks);
uint8_t port_tmos_stop_task(uint8_t task, uint16_t ev);
uint32_t port_tmos_get_task_timer(uint8_t task, uint16_t ev);
uint8_t port_tmos_msg_send(uint8_t task, uint8_t *msg);
uint8_t port_tmos_msg_deallocate(uint8_t *msg);
uint8_t *port_tmos_msg_receive(uint8_t task);
uint8_t *port_tmos_msg_allocate(uint32_t len);
uint32_t port_TMOS_GetSystemClock(void);  /* ticks of 0.625 ms */
uint8_t port_TMOS_ProcessEventRegister(void *cb);
uint32_t port_resident_trap(uint32_t slot);

/* ---- WCH BLE central API: tsl6_central.c. The stock code calls some of them
   with 32-bit arguments, so every argument is 32 bits here. ---- */
uint8_t port_GAP_SetParamValue(uint32_t id, uint32_t value);
uint8_t port_GAPBondMgr_SetParameter(uint32_t param, uint32_t len, void *value);
uint8_t port_GAPBondMgr_PasscodeRsp(uint32_t conn, uint32_t status, uint32_t passcode);
uint8_t port_GATT_InitClient(void);
void port_GATT_RegisterForInd(uint32_t task);
uint8_t port_GAPRole_CentralStartDevice(uint32_t task, void *bond_cbs, void *central_cbs);
uint8_t port_GAPRole_CentralStartDiscovery(uint32_t mode, uint32_t active, uint32_t white_list);
uint8_t port_GAPRole_CentralCancelDiscovery(void);
uint8_t port_GAPRole_ObserverCancelDiscovery(void);
uint8_t port_GAPRole_CentralEstablishLink(uint32_t high_duty, uint32_t white_list, uint32_t addr_type, uint8_t *addr);
uint8_t port_GAPRole_TerminateLink(uint32_t conn);
uint8_t port_GAPRole_ReadRssiCmd(uint32_t conn);
void *port_GATT_bm_alloc(uint32_t conn, uint32_t opcode, uint32_t size, uint16_t *got, uint32_t flag);
void port_GATT_bm_free(void *msg, uint32_t opcode);
uint8_t port_GATT_DiscAllChars(uint32_t conn, uint32_t start, uint32_t end, uint32_t task);
uint8_t port_GATT_WriteCharValue(uint32_t conn, void *req, uint32_t task);
uint8_t port_GATT_ReadCharValue(uint32_t conn, void *req, uint32_t task);

/* Run the TMOS events that are due. Returns the ms until the next timer, or
   UINT32_MAX when no timer runs. */
uint32_t port_tmos_run(void);
void port_tmos_reset(void);

/* ---- HAL: the board firmware fills it (tsl6_engine.h) ---- */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;
} tsl6_frame_t;  /* the stock 12-byte frame layout */

/* ---- Central role (BLE remote buttons): tsl6_central.c ----
   The board does the radio work (NimBLE). Each operation returns 0 when it
   started; the result comes later as an event (tsl6_cev_t). */
typedef struct {
    int (*scan)(uint32_t ms, int active);                 /* scan, then TSL6_CEV_SCAN_DONE */
    void (*scan_stop)(void);                              /* stop, then TSL6_CEV_SCAN_DONE */
    int (*connect)(uint8_t addr_type, const uint8_t addr[6]);  /* TSL6_CEV_CONNECTED (after the encryption) */
    void (*connect_cancel)(void);
    int (*disconnect)(uint16_t conn);                     /* TSL6_CEV_DISCONNECTED */
    int (*rssi)(uint16_t conn, int8_t *out);              /* now */
    int (*disc_chrs)(uint16_t conn);                      /* TSL6_CEV_CHR each, then TSL6_CEV_DISC_DONE */
    int (*write)(uint16_t conn, uint16_t handle, const uint8_t *v, uint16_t len);  /* TSL6_CEV_WRITE_DONE */
    int (*read)(uint16_t conn, uint16_t handle);          /* TSL6_CEV_READ_DONE */
    void (*forget)(uint8_t addr_type, const uint8_t addr[6]);  /* delete the bond of a remote */
} tsl6_central_ops_t;

enum {
    TSL6_CEV_SCAN_REPORT = 1,  /* evt_type, addr_type, addr, rssi, data (advertising data) */
    TSL6_CEV_SCAN_DONE,
    TSL6_CEV_CONNECTED,        /* status (0 = up), addr_type, addr, conn */
    TSL6_CEV_DISCONNECTED,     /* conn, status = reason */
    TSL6_CEV_CHR,              /* conn, handle (declaration), props, val_handle, uuid16 (0 = 128-bit) */
    TSL6_CEV_DISC_DONE,        /* conn */
    TSL6_CEV_WRITE_DONE,       /* conn, status (0 = good, else the ATT error) */
    TSL6_CEV_READ_DONE,        /* conn, status, data */
    TSL6_CEV_NOTIFY,           /* conn, val_handle, status (1 = indication), data */
};

typedef struct {
    uint8_t type;
    uint8_t status;
    uint16_t conn;
    uint8_t addr_type;
    uint8_t addr[6];
    int8_t rssi;
    uint8_t evt_type;
    uint8_t props;
    uint16_t handle;
    uint16_t val_handle;
    uint16_t uuid16;
    uint16_t len;
    uint8_t data[64];
} tsl6_cev_t;

typedef struct {
    uint32_t (*now_ticks)(void);                         /* 0.625 ms ticks, free-running */
    int (*send)(const tsl6_frame_t *f);                  /* 1 = queued for the bus */
    int (*store_read)(uint32_t page, void *dst, uint32_t len);       /* 1 = found */
    void (*store_write)(uint32_t page, const void *src, uint32_t len);
    void (*log)(const char *line);
    void (*settings_changed)(void);                      /* the TSL6 code changed its config (buttons) */
    void (*reboot)(void);
    int (*ble_send)(uint16_t conn, const uint8_t *frame, uint16_t len);  /* one TSL6 frame to a phone link */
    void (*uid14)(uint8_t out[14]);                      /* the 14-byte ID that A0 reports, A4/A5/F0 check */
    const tsl6_central_ops_t *central;                   /* NULL: no BLE remotes */
} tsl6_hal_t;

extern const tsl6_hal_t *port_hal;

#ifdef __cplusplus
}
#endif
