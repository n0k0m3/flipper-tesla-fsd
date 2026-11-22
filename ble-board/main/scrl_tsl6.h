#pragma once
/*
 * scrl_tsl6.h — the TSL6 car features engine (components/tsl6) on the board.
 *
 * The engine runs in the CAN task. It reads every received frame, runs its
 * timers, and sends through scrl_can_send_tsl6(): only in Active, never 0x370.
 * Its settings are the 28 "tsl6" bytes of ScrlSettings. The engine keeps them,
 * with its tables, in NVS namespace "tsl6".
 *
 * The TSL6 app protocol: the BLE characteristic TSL6 (scrl_ble.h) carries the
 * TSL6 frames. The NimBLE host task hands its events to scrl_tsl6_ble_event();
 * the CAN task gives them to the engine in scrl_tsl6_poll().
 *
 * Call scrl_tsl6_init() from app_main before the CAN task starts. Call the
 * other functions from the CAN task only, except scrl_tsl6_ble_event() and
 * scrl_tsl6_tx_count().
 */

#include <stdint.h>

#include "fsd_handler.h"  // CanFrame
#include "scrl_settings.h"

enum ScrlTsl6Event : uint8_t {
    SCRL_TSL6_EV_OPEN = 1,   // a phone link subscribed to TSL6
    SCRL_TSL6_EV_DATA = 2,   // a write to TSL6
    SCRL_TSL6_EV_CLOSE = 3,  // unsubscribed, or the link is down
    SCRL_TSL6_EV_CENTRAL = 4,  // a result of the central role (scrl_central.h): a tsl6_cev_t
};

/** An event of the TSL6 characteristic. NimBLE host task. Drops it when the buffer is full. */
void scrl_tsl6_ble_event(ScrlTsl6Event ev, uint16_t conn, const uint8_t *data, size_t len);

/** Start the engine from its saved settings. Copies them to out_tsl6. */
void scrl_tsl6_init(uint8_t out_tsl6[SCRL_TSL6_LEN]);

/** One received frame. */
void scrl_tsl6_frame(const CanFrame &frame);

/**
 * Apply new settings from the app, run the timers that are due, and copy a
 * change that the engine made itself (TSL6 key gestures) back to the board
 * settings. Returns the ms until the next engine timer.
 */
uint32_t scrl_tsl6_poll(void);

/** Erase the saved engine settings and tables (board factory reset). */
void scrl_tsl6_erase_store(void);

/** Frames that the engine handed to the CAN controller. Safe from any task. */
uint32_t scrl_tsl6_tx_count(void);
