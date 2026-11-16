#pragma once
/*
 * scrl_can_hw.h — the interface between scrl_can.cpp and the CAN backend.
 *
 * Exactly one backend is in a build (Kconfig SCRL_CAN_*):
 *   scrl_can_twai.cpp       on-chip TWAI
 *   scrl_can_mcp2515.cpp    MCP2515 on SPI
 *   scrl_can_mcp2518fd.cpp  MCP2518FD on SPI, classic CAN 2.0 only
 *
 * scrl_can.cpp keeps the state, the counters and the TX rules. A backend only
 * moves frames and reports what the controller did.
 *
 * RECEIVE STAMP. Each frame gets rx_ms, which the 25 ms freshness gate reads.
 * The stamp must never be later than the real reception, else a stale frame
 * passes as fresh:
 *   TWAI     the receive interrupt stamps the frame.
 *   SPI      the time of the last poll that found the controller empty. The
 *            frame arrived after that, so the stamp is early, never late.
 */

#include <stdbool.h>
#include <stdint.h>
#include "fsd_handler.h"  // CanFrame
#include "freertos/FreeRTOS.h"

// ── backend: implemented by the one backend in the build ─────────────────────

/** Pins to a safe state, tasks and buses created. Called once from scrl_can_init(). */
void scrl_can_hw_init(void);

/** Put the controller on the bus. Called only when it is stopped. */
bool scrl_can_hw_start(bool listen_only);

/** Take the controller off the bus and drop frames that wait for TX. */
void scrl_can_hw_stop(void);

/** Hand one checked frame to the controller. False = no free slot or an error. */
bool scrl_can_hw_send(const CanFrame &frame);

/** GPIO for the rx_pin status field while running. */
int8_t scrl_can_hw_rx_pin(void);

/** NanoH2 pin probe only. */
void scrl_can_hw_probe_swap_rx(void);

// ── common: implemented in scrl_can.cpp, called by the backend ───────────────

/** One received frame, already stamped. From an ISR pass `woken`, from a task pass nullptr. */
void scrl_can_rx_frame(const CanFrame &frame, BaseType_t *woken);
void scrl_can_note_rx_rejected(void);
void scrl_can_note_rx_overflow(uint32_t lost);
void scrl_can_note_tx_done(bool ok);
void scrl_can_note_bus_errors(uint32_t count);
/** The controller went bus-off. The backend has stopped all TX already, or does it at once. */
void scrl_can_note_bus_off(void);
