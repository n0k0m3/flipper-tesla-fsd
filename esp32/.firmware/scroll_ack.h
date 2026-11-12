#pragma once
/*
 * scroll_ack.h — DAS Scroll Ack state machine (0x3C2 VCLEFT_switchStatus left
 * ticks, HW4-only, ESP32). Header-only and Arduino-independent, so the host
 * test exercises it directly (test/test_scroll_ack.cpp).
 *
 * WHAT IT DOES
 *   After fresh HW4 0x39B DAS data reports AP state 6 and hands-on state 3, the
 *   feature acknowledges the hands-on prompt with one balanced pair of LEFT
 *   scroll-wheel ticks on 0x3C2: a -1 tick, then a +1 tick on the next eligible
 *   native frame. It copies the car's own idle page-1 0x3C2 frame and alters
 *   only the 6-bit signed left-tick field, so an injected tick is byte-identical
 *   to a thumb (0x3C2 has no counter and no checksum).
 *
 * WHY FOLLOW NATIVE TRAFFIC (no timer)
 *   The feature never generates a frame from nothing. It waits for the car's own
 *   0x3C2 page-1 broadcast (50 ms cadence) and sends a modified copy, so the
 *   output keeps the native pacing and idle content. The balancing +1 rides the
 *   next native frame, not a fixed software delay.
 *
 * BIT LAYOUT (VCLEFT_switchStatus page 1, Model Y 2026.26.6.5 VEH DBC;
 * little-endian over the 8 bytes). 1 = released / idle:
 *   page index (VCLEFT_switchStatusIndex) : byte0 bits 0-1  (== 1 for page 1)
 *   swcLeftScrollTicks  : byte2 bits 0-5, 6-bit signed  (idle 0; -1 = 0x3F, +1 = 0x01)
 *   swcRightScrollTicks : byte3 bits 0-5, 6-bit signed  (idle 0)
 *   swcLeftPressed      : byte0 bits 5-6  (idle 1)
 *   swcRightPressed     : byte1 bits 4-5  (idle 1)
 *   swcLeftTiltLeft     : byte1 bits 6-7  (idle 1)
 *   swcLeftTiltRight    : byte0 bits 3-4  (idle 1)
 *   swcRightTiltLeft    : byte1 bits 0-1  (idle 1)
 *   swcRightTiltRight   : byte1 bits 2-3  (idle 1)
 *   swcLeftDoublePress  : byte5 bit 1     (idle 0)
 *   swcRightDoublePress : byte5 bit 2     (idle 0)
 * The captured idle fixture 29 55 00 00 00 00 00 80 passes every idle check.
 *
 * OWNERSHIP
 *   This file owns only the episode state machine and frame building. The caller
 *   (main.cpp process_frame) owns the FSDState snapshot, the TX gates, the DAS
 *   0x39B freshness note, the send on the primary bus, and the success report
 *   back via scroll_ack_complete().
 */

#include "fsd_handler.h"  // CanFrame (typedef of CANFRAME), CAN_ID_VCLEFT_SWITCH
#include <stdbool.h>
#include <stdint.h>

// A standard HW4 0x39B seen on the primary channel is required this recently for
// the first tick (wrap-safe unsigned age). Matches the spec's 1000 ms window.
#define SCROLL_ACK_DAS_FRESH_MS 1000u

// Episode phase. A zero-initialised runtime starts Armed.
enum class ScrollAckPhase : uint8_t { Armed, SecondPending, EpisodeDone };

// What scroll_ack_step() decided for this native frame.
enum class ScrollAckAction : uint8_t { None, FirstTick, SecondTick };

// Per-boot episode state. One file-static instance in main.cpp.
struct ScrollAckRuntime {
    ScrollAckPhase phase;
    bool das39b_seen;        // a standard primary-channel 0x39B has been seen
    uint32_t das39b_rx_ms;   // now_ms of that 0x39B (freshness clock)
    uint32_t sent_count;     // successful ticks sent (first + second), diagnostics
};

// A consistent snapshot of the TX gates and DAS values for one native frame.
struct ScrollAckInput {
    bool enabled;       // FSDState.scroll_ack
    bool tx_allowed;    // fsd_can_transmit() && the AP injection gates
    bool primary_bus;   // source arrived on CAN_BUS_PRIMARY
    bool fresh_native;  // fsd_tx_fresh() on the source frame
    bool hw4;           // hw_version == TeslaHW_HW4
    uint8_t ap_state;   // das_ap_state
    uint8_t hands_on;   // das_hands_on_state
};

// ── internal helpers ──────────────────────────────────────────────────────────

// The signed 6-bit left-tick field lives in byte2 bits 5:0. -1 = 0x3F, +1 = 0x01.
#define SCROLL_ACK_LTICK_NEG 0x3Fu
#define SCROLL_ACK_LTICK_POS 0x01u

// True when `native` is a fresh idle page-1 0x3C2 frame safe to copy: right id,
// DLC 8, page index 1, both tick fields zero, every press/tilt field released,
// and both double-press bits clear. Any user-input field blocks it.
static inline bool scroll_ack_source_idle(const CanFrame &native) {
    if (native.id != CAN_ID_VCLEFT_SWITCH) return false;
    if (native.dlc != 8u) return false;
    const uint8_t *d = native.data;
    if ((d[0] & 0x03u) != 1u) return false;           // page index 1
    if ((d[2] & 0x3Fu) != 0u) return false;           // left scroll ticks 0
    if ((d[3] & 0x3Fu) != 0u) return false;           // right scroll ticks 0
    if (((d[0] >> 5) & 0x03u) != 1u) return false;    // left pressed released
    if (((d[1] >> 4) & 0x03u) != 1u) return false;    // right pressed released
    if (((d[1] >> 6) & 0x03u) != 1u) return false;    // left tilt left released
    if (((d[0] >> 3) & 0x03u) != 1u) return false;    // left tilt right released
    if ((d[1] & 0x03u) != 1u) return false;           // right tilt left released
    if (((d[1] >> 2) & 0x03u) != 1u) return false;    // right tilt right released
    if ((d[5] & 0x02u) != 0u) return false;           // left double-press clear
    if ((d[5] & 0x04u) != 0u) return false;           // right double-press clear
    return true;
}

// Copy `native` and set the left-tick field (byte2 bits 5:0) to `ltick`,
// preserving byte2 bits 7:6 and every other byte.
static inline void scroll_ack_build(const CanFrame &native, uint8_t ltick, CanFrame *out) {
    *out = native;
    out->data[2] = (uint8_t)((native.data[2] & 0xC0u) | (ltick & 0x3Fu));
}

// ── API ───────────────────────────────────────────────────────────────────────

// Mark the standard primary-channel 0x39B DAS source as seen, and stamp its
// time. Call only for a standard HW4 0x39B parsed on CAN_BUS_PRIMARY.
static inline void scroll_ack_note_das39b(ScrollAckRuntime *runtime, uint32_t now_ms) {
    runtime->das39b_seen = true;
    runtime->das39b_rx_ms = now_ms;
}

// Decide what to do for one native 0x3C2 frame. Returns the action and, for a
// non-None action, writes the frame to build into *out. On None, *out is left
// untouched. May rearm or cancel the episode but never changes sent_count.
static inline ScrollAckAction scroll_ack_step(ScrollAckRuntime *runtime,
                                              const ScrollAckInput &input,
                                              const CanFrame &native,
                                              uint32_t now_ms,
                                              CanFrame *out) {
    // Wrap-safe DAS freshness: a standard primary 0x39B within the window.
    bool das_fresh = runtime->das39b_seen &&
                     (uint32_t)(now_ms - runtime->das39b_rx_ms) <= SCROLL_ACK_DAS_FRESH_MS;

    // Gates that apply to BOTH ticks. Hands-on is NOT here — the first tick adds
    // it, the second tick ignores it (the first tick may clear it to 1).
    bool gates = input.enabled && input.tx_allowed && input.primary_bus &&
                 input.fresh_native && input.hw4 && input.ap_state == 6u && das_fresh;
    bool idle = scroll_ack_source_idle(native);

    // Rearm a finished episode once hands-on has left 3.
    if (runtime->phase == ScrollAckPhase::EpisodeDone && input.hands_on != 3u) {
        runtime->phase = ScrollAckPhase::Armed;
    }

    if (runtime->phase == ScrollAckPhase::SecondPending) {
        // A lost gate or a non-idle source ends the episode without a retry.
        if (!gates || !idle) {
            runtime->phase = ScrollAckPhase::EpisodeDone;
            return ScrollAckAction::None;
        }
        scroll_ack_build(native, SCROLL_ACK_LTICK_POS, out);
        return ScrollAckAction::SecondTick;
    }

    if (runtime->phase == ScrollAckPhase::Armed) {
        if (gates && idle && input.hands_on == 3u) {
            scroll_ack_build(native, SCROLL_ACK_LTICK_NEG, out);
            return ScrollAckAction::FirstTick;
        }
        return ScrollAckAction::None;
    }

    // EpisodeDone and hands-on still 3: nothing until it leaves 3.
    return ScrollAckAction::None;
}

// Advance the phase after a send result. Counters move only on success.
static inline void scroll_ack_complete(ScrollAckRuntime *runtime,
                                       ScrollAckAction action,
                                       bool sent) {
    if (action == ScrollAckAction::FirstTick) {
        if (sent) {
            runtime->sent_count++;
            runtime->phase = ScrollAckPhase::SecondPending;
        } else {
            runtime->phase = ScrollAckPhase::EpisodeDone;  // no retry
        }
    } else if (action == ScrollAckAction::SecondTick) {
        if (sent) runtime->sent_count++;
        runtime->phase = ScrollAckPhase::EpisodeDone;       // pair done either way
    }
}
