#pragma once
/*
 * tsl6_engine.h — the TSL6 car features (imported from the C rewrite of the
 * TSL6 runtime V1.0.00) as an engine for the BLE board.
 *
 * Call every function from one task. The engine sends frames only through
 * hal->send; the board decides if they go on the bus (op mode, gates).
 *
 * Settings: the 28 bytes of the TSL6 settings that the app writes with A3:
 * A3 status bytes 24-35 (12 bytes) then 59-74 (16 bytes). Factory content:
 * every car feature off. Byte 4 bit 3 (A3 byte 28 bit 3) is the master
 * switch: 1 = the engine does nothing.
 */
#include <stdint.h>

#include "tsl6_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TSL6_CFG_LEN 28u
#define TSL6_CFG_A3_FIRST 24u  /* bytes 0-11 = A3 bytes 24-35 */
#define TSL6_CFG_A3_SECOND 59u /* bytes 12-27 = A3 bytes 59-74 */

/* Load the settings (hal->store_read) and start the features. */
void tsl6_init(const tsl6_hal_t *hal);

/* One received frame. Extended frames are ignored, as on the TSL6. The frame
   passes the TSL6 CAN filters of the detected platform first. */
void tsl6_rx(uint32_t id, int ext, uint8_t dlc, const uint8_t *data);

/* ---- TSL6 BLE frames (tsl6_ble.c): the TSL6 app protocol on a board link ----
   A link that subscribes to the TSL6 characteristic: tsl6_ble_connect() (it
   is authorized at once: the board pairing stands for the A8 password; it
   gets the A0 status). Each write: tsl6_ble_rx(). Frames out: hal->ble_send.
   At most 2 links, as on the TSL6. */
int tsl6_ble_connect(uint16_t conn);
void tsl6_ble_disconnect(uint16_t conn);
void tsl6_ble_rx(uint16_t conn, const uint8_t *data, uint16_t len);

/* A result of the central role of the board (hal->central). */
void tsl6_central_event(const tsl6_cev_t *e);

/* Run the timers that are due. Returns the ms until the next one (UINT32_MAX: none). */
uint32_t tsl6_poll(void);

void tsl6_cfg_get(uint8_t out[TSL6_CFG_LEN]);

/* Apply and save new settings, with the side effects of the stock A3 write. */
void tsl6_cfg_set(const uint8_t in[TSL6_CFG_LEN]);

/* Factory settings and tables. */
void tsl6_factory_reset(void);

/* Frames that the engine handed to hal->send, since init. */
uint32_t tsl6_tx_count(void);

#ifdef __cplusplus
}
#endif
