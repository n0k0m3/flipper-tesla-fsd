#pragma once
/*
 * scrl_tsl6.h — the TSL6 car features engine (components/tsl6) on the board.
 *
 * The engine runs in the CAN task. It reads every received frame, runs its
 * timers, and sends through scrl_can_send_tsl6(): only in Active, never 0x370.
 * Its settings are the 28 "tsl6" bytes of ScrlSettings. The engine keeps them,
 * with its tables, in NVS namespace "tsl6".
 *
 * Call scrl_tsl6_init() from app_main before the CAN task starts. Call the
 * other functions from the CAN task only.
 */

#include <stdint.h>

#include "fsd_handler.h"  // CanFrame
#include "scrl_settings.h"

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
