#pragma once
/*
 * scrl_can.h — TWAI glue for the one bus of the board (can0 = Vehicle CAN).
 *
 * The receive interrupt stamps each frame and puts it in a queue. One task
 * (the CAN task) owns everything else: it takes frames from the queue, sends
 * ticks, and starts, stops and restarts the controller. Call every function
 * here from that task, except scrl_can_get_info().
 *
 * Listen-Only uses the hardware listen-only mode with no TX pin: the board
 * drives nothing on the Grove port. Active attaches the TX pin.
 */

#include <stdbool.h>
#include <stdint.h>
#include "fsd_handler.h"  // CanFrame

// The only ID this board may send: 0x3C2, the Scroll Ack tick.
#define SCRL_CAN_TX_ALLOWED_ID CAN_ID_VCLEFT_SWITCH
// Bus-off events in one power cycle before the board locks itself to Listen-Only.
#define SCRL_CAN_BUS_OFF_LIMIT 3u
// Wait this long after a bus-off before the controller restarts.
#define SCRL_CAN_BUS_OFF_REST_MS 250u

struct ScrlCanInfo {
    bool running;            // the controller is started
    bool listen_only;        // hardware listen-only (no ACK, no TX)
    bool pins_checked;       // the build names the TX and RX pins
    int8_t rx_pin;           // GPIO that receives now, -1 = none
    uint32_t rx_frames;      // frames taken from the controller
    uint32_t rx_rejected;    // DLC above 8: counted, not passed on
    uint32_t rx_dropped;     // lost because the task queue was full
    uint32_t rx_high_water;  // most frames that waited in the task queue
    uint32_t tx_queued;      // frames handed to the controller
    uint32_t tx_ok;          // frames that went out on the bus
    uint32_t tx_failed;      // frames the controller gave up on
    uint32_t tx_refused;     // send calls refused (mode, ID, bus-off, no slot)
    uint32_t bus_errors;     // bit, stuff, form and ACK errors, arbitration losses
    uint32_t bus_off_count;  // bus-off events since power-on
};

enum ScrlCanEvent : uint8_t {
    SCRL_CAN_EVENT_NONE = 0,
    SCRL_CAN_EVENT_BUS_OFF,         // bus-off: the controller is stopped, restart after the rest
    SCRL_CAN_EVENT_BUS_OFF_LIMIT,   // bus-off number SCRL_CAN_BUS_OFF_LIMIT: go to Listen-Only
};

/** Create the frame queue. Call once, before any other function. */
void scrl_can_init(void);

/** Start the controller. Active needs checked pins. Returns false on failure. */
bool scrl_can_start(bool listen_only);

/** Stop the controller, drop frames that wait for TX, and release both pins. */
void scrl_can_stop(void);

/** Wait up to timeout_ms for one received frame. */
bool scrl_can_receive(CanFrame *out, uint32_t timeout_ms);

/** Queue one frame for TX. Returns false when the frame is not sent. */
bool scrl_can_send(const CanFrame &frame);

/** Report a bus-off, once per event. On an event the controller is already stopped. */
ScrlCanEvent scrl_can_poll_event(void);

/** Pin probe for a build with unchecked pins: listen on the other Grove pin. */
void scrl_can_probe_swap_rx(void);

/** Counters and state. Safe from any task. */
void scrl_can_get_info(ScrlCanInfo *out);
