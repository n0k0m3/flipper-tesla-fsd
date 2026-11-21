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

typedef struct {
    uint32_t (*now_ticks)(void);                         /* 0.625 ms ticks, free-running */
    int (*send)(const tsl6_frame_t *f);                  /* 1 = queued for the bus */
    int (*store_read)(uint32_t page, void *dst, uint32_t len);       /* 1 = found */
    void (*store_write)(uint32_t page, const void *src, uint32_t len);
    void (*log)(const char *line);
    void (*settings_changed)(void);                      /* the TSL6 code changed its config (buttons) */
    void (*reboot)(void);
} tsl6_hal_t;

extern const tsl6_hal_t *port_hal;

#ifdef __cplusplus
}
#endif
