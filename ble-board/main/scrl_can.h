#pragma once
/*
 * scrl_can.h — glue for the one bus of the board (Vehicle CAN).
 *
 * The backend (TWAI, MCP2515 or MCP2518FD, see scrl_can_hw.h) stamps each
 * received frame and puts it in a queue. One task (the CAN task) owns
 * everything else: it takes frames from the queue, sends ticks, and starts,
 * stops and restarts the controller. Call every function here from that task,
 * except scrl_can_get_info().
 *
 * Listen-Only uses the hardware listen-only mode of the controller. The TWAI
 * backend also attaches no TX pin then.
 */

#include <stdbool.h>
#include <stdint.h>
#include "fsd_handler.h"  // CanFrame

// The only ID that the Scroll Ack code may send: 0x3C2, the tick.
#define SCRL_CAN_TX_ALLOWED_ID CAN_ID_VCLEFT_SWITCH
// The TSL6 engine sends the IDs of the features that the owner turned on. It
// never sends 0x370: the EPAS torque echo faulted the car (traction, VDC).
#define SCRL_CAN_TSL6_DENIED_ID 0x370u
// Bus-off events in one power cycle before the board locks itself to Listen-Only.
#define SCRL_CAN_BUS_OFF_LIMIT 3u
// Wait this long after a bus-off before the controller restarts.
#define SCRL_CAN_BUS_OFF_REST_MS 250u

struct ScrlCanInfo {
    bool running;            // the controller is started
    bool listen_only;        // hardware listen-only (no ACK, no TX)
    bool pins_checked;       // the build can attach a TX path (false: NanoH2 pin probe)
    int8_t rx_pin;           // TWAI: GPIO that receives. SPI controller: its CS GPIO. -1 = off
    uint32_t rx_frames;      // frames taken from the controller
    uint32_t rx_rejected;    // DLC above 8 or CAN FD: counted, not passed on
    uint32_t rx_dropped;     // lost: task queue full, or controller receive overflow
    uint32_t rx_high_water;  // most frames that waited in the task queue
    uint32_t tx_queued;      // frames handed to the controller
    uint32_t tx_ok;          // frames that went out on the bus
    uint32_t tx_failed;      // frames the controller gave up on, or that a stop aborted
    uint32_t tx_refused;     // send calls refused (mode, ID, bus-off, no slot)
    uint32_t bus_errors;     // bus errors that the controller reports
    uint32_t bus_off_count;  // bus-off events since power-on
};

enum ScrlCanEvent : uint8_t {
    SCRL_CAN_EVENT_NONE = 0,
    SCRL_CAN_EVENT_BUS_OFF,         // bus-off: the controller is stopped, restart after the rest
    SCRL_CAN_EVENT_BUS_OFF_LIMIT,   // bus-off number SCRL_CAN_BUS_OFF_LIMIT: go to Listen-Only
};

/** Create the frame queue and put the pins in a safe state. Call once, first. */
void scrl_can_init(void);

/** Start the controller. Active needs checked pins. Returns false on failure. */
bool scrl_can_start(bool listen_only);

/** Stop the controller: off the bus, frames that wait for TX dropped. */
void scrl_can_stop(void);

/** Wait up to timeout_ms for one received frame. */
bool scrl_can_receive(CanFrame *out, uint32_t timeout_ms);

/** Queue one frame for TX. Returns false when the frame is not sent. */
bool scrl_can_send(const CanFrame &frame);

/** Queue one frame of the TSL6 engine: any standard ID except SCRL_CAN_TSL6_DENIED_ID. */
bool scrl_can_send_tsl6(const CanFrame &frame);

/** Report a bus-off, once per event. On an event the controller is already stopped. */
ScrlCanEvent scrl_can_poll_event(void);

/** NanoH2 pin probe: listen on the other Grove pin. No effect on other boards. */
void scrl_can_probe_swap_rx(void);

/** Counters and state. Safe from any task. */
void scrl_can_get_info(ScrlCanInfo *out);
