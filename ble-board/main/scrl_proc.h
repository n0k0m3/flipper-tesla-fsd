#pragma once
/*
 * scrl_proc.h — frame logic of the BLE board: a cut-down copy of
 * process_frame() from esp32/.firmware/main.cpp, with the same TX gates.
 *
 * It owns the shared FSDState and the Scroll Ack runtime. The CAN task feeds
 * it frames. The BLE task reads and writes settings and reads the status. One
 * mutex inside guards both.
 *
 * The board sends only Scroll Ack ticks (0x3C2). It links fsd_handler.cpp for
 * the frame parsers and the gate functions, and calls none of its modify or
 * echo handlers.
 */

#include <stdbool.h>
#include <stdint.h>

#include "fsd_handler.h"
#include "scrl_settings.h"
#include "scrl_status.h"

// Reasons that hold the board in Listen-Only, whatever the op mode setting says.
#define SCRL_INHIBIT_BUS_OFF       (1u << 0)  // 3 bus-off events in this power cycle
#define SCRL_INHIBIT_BLE_OTA       (1u << 1)  // a BLE update runs
#define SCRL_INHIBIT_IMAGE_PENDING (1u << 2)  // a new image runs and is not confirmed
#define SCRL_INHIBIT_NO_TX_PIN     (1u << 3)  // no TX path: pins not checked, or a Listen-Only build
#define SCRL_INHIBIT_CAPTURE       (1u << 4)  // a capture runs (scrl_capture.h); never saved

/** Set up the state: HW4 fixed, Listen-Only, the given settings. Call once. */
void scrl_proc_init(const ScrlSettings &settings, uint32_t inhibit);

/** Handle one received frame. CAN task only. */
void scrl_proc_handle_frame(const CanFrame &frame);

ScrlSettings scrl_proc_get_settings(void);

/** Counts up on each settings change. Safe from any task. */
uint32_t scrl_proc_settings_gen(void);

/** The TSL6 engine changed its settings itself: copy them, no other change. */
void scrl_proc_set_tsl6(const uint8_t tsl6[SCRL_TSL6_LEN]);

/**
 * Apply clamped settings. A change of the switch or the mode resets the episode.
 * release_bus_off_lock: the owner wrote op mode Active on purpose. That
 * releases a bus-off lock. The bus-off count stays, so the next bus-off locks again.
 */
void scrl_proc_set_settings(const ScrlSettings &settings, bool release_bus_off_lock);

void scrl_proc_set_inhibit(uint32_t bits, bool on);
uint32_t scrl_proc_get_inhibit(void);

/** True when the effective op mode is Active: the setting is Active and nothing inhibits it. */
bool scrl_proc_active(void);

/** Fill the Scroll Ack snapshot, the op mode, the frame counts and the flags that this file owns. */
void scrl_proc_fill_status(ScrlStatus *out);

/** Reset the sent, aborted and frame counters. */
void scrl_proc_reset_counters(void);

/**
 * BLE update gate: true when the car does not move and AP is not engaged.
 * With no CAN traffic at all (bench, car asleep) the answer is true.
 */
bool scrl_proc_parked(void);
