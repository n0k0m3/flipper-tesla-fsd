#pragma once
/*
 * scroll_ack.h — Scroll Ack state machine (0x3C2 VCLEFT_switchStatus left
 * ticks, HW4-only, ESP32). Header-only and Arduino-independent, so the host
 * test exercises it directly (test/test_scroll_ack.cpp).
 *
 * WHAT IT DOES
 *   Sends LEFT scroll-wheel ticks. FSDState.scroll_ack_pattern picks -1 then +1
 *   (default), +1 then -1, +1 once, or -1 once. The second tick of a pair goes on
 *   the first idle native frame after a random gap (mean scroll_ack_gap_ms,
 *   +/-50 %). Two modes (FSDState.scroll_ack_mode) decide when ticks start:
 *   - Prompt (0, default): when fresh HW4 0x39B reports AP state 6 and
 *     hands-on state 3 (the visible prompt), after a random delay (mean
 *     scroll_ack_delay_ms, +/-50 %). One episode per state-3 prompt; it rearms
 *     after hands-on leaves 3. A prompt cleared during the delay sends
 *     nothing. Owner captures (2026-11-11) show one manual left tick changing
 *     hands-on from 3 to 1 in about 33 ms.
 *   - Timer (1, TSL6 pattern): while AP state is 6, one episode every
 *     scroll_ack_interval_ms +/-20 % (default 2.0-3.0 s),
 *     whatever the hands-on state. The first pair comes one interval after AP
 *     engages. Owner capture of the EVAAM TSL6 on the same car (2026-11-11):
 *     23 pairs in 57 s, 2.0-3.0 s apart; hands-on never reached 3.
 *   The default bytes and the 100 ms default gap match the TSL6 frames.
 *
 * WHY FOLLOW NATIVE TRAFFIC
 *   The feature never generates a frame from nothing. It waits for the car's own
 *   0x3C2 page-1 broadcast and sends a modified copy right after it, so the
 *   output keeps the native idle content. Each tick is an extra frame next to
 *   the native one, the same as the TSL6. In Timer mode the timer only picks
 *   which native frame gets the tick.
 *
 * WHICH BUS
 *   Ticks go only to a bus that carries 0x3C2 and has shown no 0x370 (EPAS) in
 *   the last SCROLL_ACK_EPAS_WINDOW_MS. Vehicle CAN has no 0x370. Chassis CAN
 *   carries a forwarded copy of 0x3C2 and also 0x370, and the car ignored ticks
 *   sent there (owner test, 2026-11-11). So the rule finds Vehicle CAN on either
 *   T-2CAN channel. DAS 0x39B is accepted from any bus.
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
 *   This file owns the episode state, the bus rule and frame building. The caller
 *   (main.cpp process_frame) owns the TX gates, feeds DAS and EPAS sightings,
 *   sends the frame on the bus it came from, and reports the result through
 *   scroll_ack_complete().
 */

#include "fsd_handler.h"  // CanFrame (typedef of CANFRAME), CAN_ID_VCLEFT_SWITCH
#include "can_driver.h"   // CanBusId
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// A valid HW4 0x39B is required this recently (wrap-safe unsigned age).
#define SCROLL_ACK_DAS_FRESH_MS 1000u
// Random spread around each configured mean (uniform, inclusive):
//   Prompt delay (prompt -> first tick) and the gap between ticks: mean +/- 50 %.
//   Timer interval (start to start): mean +/- 20 % (2500 ms -> 2000-3000 ms, the
//   range measured on the TSL6).
#define SCROLL_ACK_DELAY_SPREAD_PCT 50u
#define SCROLL_ACK_GAP_SPREAD_PCT 50u
#define SCROLL_ACK_INTERVAL_SPREAD_PCT 20u
// The second tick waits for an idle native frame at most this long past its due time.
#define SCROLL_ACK_SECOND_TIMEOUT_MS 2000u
// A bus that showed 0x370 this recently is not Vehicle CAN.
#define SCROLL_ACK_EPAS_WINDOW_MS 2000u
#define SCROLL_ACK_BUS_COUNT 2u

// Episode phase. A zero-initialised runtime starts Armed.
enum class ScrollAckPhase : uint8_t { Armed, Delay, SecondPending, EpisodeDone };

// What scroll_ack_step() decided for this native frame.
enum class ScrollAckAction : uint8_t { None, FirstTick, SecondTick };

enum class ScrollAckBlock : uint8_t {
    None, Disabled, TxDenied, WrongBus, NativeFresh, NotHW4,
    DasFresh, DasApState, DasHandsOn, NativeIdle, EpisodeDone, PairAborted, Timer,
    Delay, Gap
};

// Per-boot state. One file-static instance in main.cpp.
struct ScrollAckRuntime {
    ScrollAckPhase phase;
    bool timer_armed;          // Timer mode: AP gates held on the last Vehicle frame
    uint32_t next_due_ms;      // earliest time for the next first tick (both modes)
    uint32_t second_due_ms;    // earliest time for the second tick
    uint8_t second_tick;       // tick field value for the second tick
    bool das39b_seen;          // a valid 0x39B has been parsed
    uint32_t das39b_rx_ms;     // receive time of that 0x39B (freshness clock)
    uint8_t das39b_ap_state;   // AP state latched with das39b_rx_ms
    uint8_t das39b_hands_on;   // hands-on state latched with das39b_rx_ms
    bool epas_seen[SCROLL_ACK_BUS_COUNT];
    uint32_t epas_rx_ms[SCROLL_ACK_BUS_COUNT];
    uint32_t episode_generation; // increments on reset; detects stale completion
    ScrollAckBlock last_block;
    uint32_t last_native_age_ms;
    uint32_t last_das_age_ms;
    uint8_t last_vehicle_bus1; // 1 + bus index of the last Vehicle frame, 0 = none
    bool last_tx_allowed;
    uint32_t sent_count;       // successful ticks sent, diagnostics
    uint32_t pairs_aborted;    // first tick sent, balancing tick not sent
};

// A consistent snapshot of the TX gates for one native frame.
struct ScrollAckInput {
    bool enabled;       // FSDState.scroll_ack
    bool tx_allowed;    // fsd_can_transmit() && the AP injection gates
    CanBusId bus;       // bus the native frame arrived on
    bool fresh_native;  // fsd_tx_fresh() on the source frame
    bool hw4;           // hw_version == TeslaHW_HW4
    uint8_t mode;       // SCROLL_ACK_MODE_PROMPT or SCROLL_ACK_MODE_TIMER
    uint8_t pattern;    // SCROLL_ACK_PATTERN_*
    uint16_t delay_mean_ms;     // Prompt: prompt -> first tick
    uint16_t gap_mean_ms;       // first tick -> second tick
    uint16_t interval_mean_ms;  // Timer: first tick -> next first tick
    uint32_t rand;      // random value for this frame's draws
};

// ── internal helpers ──────────────────────────────────────────────────────────

// The signed 6-bit left-tick field lives in byte2 bits 5:0. -1 = 0x3F, +1 = 0x01.
#define SCROLL_ACK_LTICK_NEG 0x3Fu
#define SCROLL_ACK_LTICK_POS 0x01u

static inline bool scroll_ack_source_page1(const CanFrame &native) {
    return native.id == CAN_ID_VCLEFT_SWITCH && native.dlc == 8u &&
           (native.data[0] & 0x03u) == 1u;
}

// True when `native` is an idle page-1 0x3C2 frame safe to copy: right id,
// DLC 8, page index 1, both tick fields zero, every press/tilt field released,
// and both double-press bits clear. Any user-input field blocks it.
static inline bool scroll_ack_source_idle(const CanFrame &native) {
    if (!scroll_ack_source_page1(native)) return false;
    const uint8_t *d = native.data;
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

// Uniform draw in [mean - spread, mean + spread], spread = mean * pct / 100.
static inline uint32_t scroll_ack_jitter_ms(uint32_t mean_ms, uint32_t pct, uint32_t rand) {
    uint32_t spread = mean_ms * pct / 100u;
    if (spread == 0u) return mean_ms;
    return mean_ms - spread + (rand % (2u * spread + 1u));
}

// A second, mostly independent draw from the same per-frame random value.
static inline uint32_t scroll_ack_rand2(uint32_t rand) {
    uint32_t x = rand * 2654435761u;
    return x ^ (x >> 15);
}

// Tick values for a pattern. Returns false for a single-tick pattern.
static inline bool scroll_ack_pattern_ticks(uint8_t pattern, uint8_t *first, uint8_t *second) {
    switch (pattern) {
        case SCROLL_ACK_PATTERN_UP_DOWN:
            *first = SCROLL_ACK_LTICK_POS; *second = SCROLL_ACK_LTICK_NEG; return true;
        case SCROLL_ACK_PATTERN_UP_ONCE:
            *first = SCROLL_ACK_LTICK_POS; *second = 0u; return false;
        case SCROLL_ACK_PATTERN_DOWN_ONCE:
            *first = SCROLL_ACK_LTICK_NEG; *second = 0u; return false;
        default:  // SCROLL_ACK_PATTERN_DOWN_UP
            *first = SCROLL_ACK_LTICK_NEG; *second = SCROLL_ACK_LTICK_POS; return true;
    }
}

// Clamp the persisted Scroll Ack settings to their valid ranges.
static inline void scroll_ack_clamp_settings(FSDState *state) {
    if (state->scroll_ack_mode > SCROLL_ACK_MODE_TIMER) state->scroll_ack_mode = SCROLL_ACK_MODE_PROMPT;
    if (state->scroll_ack_pattern > SCROLL_ACK_PATTERN_DOWN_ONCE)
        state->scroll_ack_pattern = SCROLL_ACK_PATTERN_DOWN_UP;
    if (state->scroll_ack_delay_ms > SCROLL_ACK_DELAY_MAX_MS)
        state->scroll_ack_delay_ms = SCROLL_ACK_DELAY_MAX_MS;
    if (state->scroll_ack_gap_ms > SCROLL_ACK_GAP_MAX_MS)
        state->scroll_ack_gap_ms = SCROLL_ACK_GAP_MAX_MS;
    if (state->scroll_ack_interval_ms < SCROLL_ACK_INTERVAL_MIN_MS)
        state->scroll_ack_interval_ms = SCROLL_ACK_INTERVAL_MIN_MS;
    if (state->scroll_ack_interval_ms > SCROLL_ACK_INTERVAL_MAX_MS)
        state->scroll_ack_interval_ms = SCROLL_ACK_INTERVAL_MAX_MS;
}

// Wrap-safe "now is at or after due".
static inline bool scroll_ack_due(uint32_t now_ms, uint32_t due_ms) {
    return (int32_t)(now_ms - due_ms) >= 0;
}

// ── API ───────────────────────────────────────────────────────────────────────

// Latch one successfully parsed DAS sample with its receive timestamp.
static inline void scroll_ack_note_das39b(ScrollAckRuntime *runtime, uint32_t rx_ms,
                                          uint8_t ap_state, uint8_t hands_on) {
    runtime->das39b_seen = true;
    runtime->das39b_rx_ms = rx_ms;
    runtime->das39b_ap_state = ap_state;
    runtime->das39b_hands_on = hands_on;
}

// Production DAS ingest rule: any bus, valid DLC-8 frames only.
static inline void scroll_ack_note_hw4_das(ScrollAckRuntime *runtime, CanBusId bus,
                                           const CanFrame &frame,
                                           uint8_t ap_state, uint8_t hands_on) {
    (void)bus;
    if (frame.dlc == 8u) {
        scroll_ack_note_das39b(runtime, frame.rx_ms, ap_state, hands_on);
    }
}

// Record a 0x370 (EPAS) sighting. A bus with recent 0x370 is not Vehicle CAN.
static inline void scroll_ack_note_epas(ScrollAckRuntime *runtime, CanBusId bus,
                                        uint32_t rx_ms) {
    uint8_t i = (uint8_t)bus;
    if (i >= SCROLL_ACK_BUS_COUNT) return;
    runtime->epas_seen[i] = true;
    runtime->epas_rx_ms[i] = rx_ms;
}

static inline bool scroll_ack_is_vehicle_bus(const ScrollAckRuntime *runtime,
                                             CanBusId bus, uint32_t now_ms) {
    uint8_t i = (uint8_t)bus;
    if (i >= SCROLL_ACK_BUS_COUNT) return false;
    if (!runtime->epas_seen[i]) return true;
    return (uint32_t)(now_ms - runtime->epas_rx_ms[i]) > SCROLL_ACK_EPAS_WINDOW_MS;
}

static inline void scroll_ack_reset_episode(ScrollAckRuntime *runtime) {
    runtime->phase = ScrollAckPhase::Armed;
    runtime->timer_armed = false;
    runtime->episode_generation++;
    runtime->last_block = ScrollAckBlock::None;
}

// Shared web-command transition. Preserve DAS history and boot diagnostics.
static inline void scroll_ack_apply_enabled(FSDState *state, ScrollAckRuntime *runtime,
                                            bool enabled) {
    state->scroll_ack = enabled;
    scroll_ack_reset_episode(runtime);
}

// Shared web-command transition for the mode selector. Unknown values = Prompt.
static inline void scroll_ack_apply_mode(FSDState *state, ScrollAckRuntime *runtime,
                                         int mode) {
    state->scroll_ack_mode = (mode == SCROLL_ACK_MODE_TIMER) ? (uint8_t)SCROLL_ACK_MODE_TIMER
                                                             : (uint8_t)SCROLL_ACK_MODE_PROMPT;
    scroll_ack_reset_episode(runtime);
}

static inline int scroll_ack_format_sent_count_json(char *out, size_t size,
                                                     uint32_t sent_count) {
    return snprintf(out, size, "\"scroll_ack_sent_count\":%u", sent_count);
}

static inline const char *scroll_ack_phase_name(ScrollAckPhase phase) {
    if (phase == ScrollAckPhase::Delay) return "Delay";
    if (phase == ScrollAckPhase::SecondPending) return "SecondPending";
    if (phase == ScrollAckPhase::EpisodeDone) return "EpisodeDone";
    return "Armed";
}

static inline const char *scroll_ack_block_name(ScrollAckBlock block) {
    switch (block) {
        case ScrollAckBlock::Disabled: return "disabled";
        case ScrollAckBlock::TxDenied: return "tx_denied";
        case ScrollAckBlock::WrongBus: return "wrong_bus";
        case ScrollAckBlock::NativeFresh: return "stale_native";
        case ScrollAckBlock::NotHW4: return "not_hw4";
        case ScrollAckBlock::DasFresh: return "stale_das";
        case ScrollAckBlock::DasApState: return "das_ap_state";
        case ScrollAckBlock::DasHandsOn: return "das_hands_on";
        case ScrollAckBlock::NativeIdle: return "native_idle";
        case ScrollAckBlock::EpisodeDone: return "episode_done";
        case ScrollAckBlock::Timer: return "timer";
        case ScrollAckBlock::Delay: return "delay";
        case ScrollAckBlock::Gap: return "gap";
        case ScrollAckBlock::PairAborted: return "pair_aborted";
        default: return "none";
    }
}

static inline int scroll_ack_format_diagnostics_json(char *out, size_t size,
                                                      const ScrollAckRuntime *runtime) {
    char bus[8];
    if (runtime->last_vehicle_bus1 != 0u) {
        snprintf(bus, sizeof(bus), "\"can%u\"", (unsigned)(runtime->last_vehicle_bus1 - 1u));
    } else {
        snprintf(bus, sizeof(bus), "null");
    }
    return snprintf(out, size,
                    "\"scroll_ack_diag\":{\"phase\":\"%s\",\"block\":\"%s\","
                    "\"native_age_ms\":%u,\"das_age_ms\":%u,"
                    "\"das_ap_state\":%u,\"das_hands_on\":%u,"
                    "\"vehicle_bus\":%s,\"tx_allowed\":%s,"
                    "\"pairs_aborted\":%u,\"timer_armed\":%s}",
                    scroll_ack_phase_name(runtime->phase),
                    scroll_ack_block_name(runtime->last_block),
                    runtime->last_native_age_ms, runtime->last_das_age_ms,
                    runtime->das39b_ap_state, runtime->das39b_hands_on,
                    bus,
                    runtime->last_tx_allowed ? "true" : "false",
                    runtime->pairs_aborted,
                    runtime->timer_armed ? "true" : "false");
}

// First failing gate, ignoring the hands-on value and the idle check.
static inline ScrollAckBlock scroll_ack_gate_block(const ScrollAckRuntime *runtime,
                                                    const ScrollAckInput &input,
                                                    uint32_t now_ms) {
    if (!input.enabled) return ScrollAckBlock::Disabled;
    if (!input.tx_allowed) return ScrollAckBlock::TxDenied;
    if (!input.fresh_native) return ScrollAckBlock::NativeFresh;
    if (!input.hw4) return ScrollAckBlock::NotHW4;
    bool das_fresh = runtime->das39b_seen &&
                     (uint32_t)(now_ms - runtime->das39b_rx_ms) <= SCROLL_ACK_DAS_FRESH_MS;
    if (!das_fresh) return ScrollAckBlock::DasFresh;
    if (runtime->das39b_ap_state != 6u) return ScrollAckBlock::DasApState;
    return ScrollAckBlock::None;
}

// Decide what to do for one native 0x3C2 frame. Returns the action and, for a
// non-None action, writes the frame to send into *out. On None, *out is left
// untouched. Never changes sent_count.
static inline ScrollAckAction scroll_ack_step(ScrollAckRuntime *runtime,
                                              const ScrollAckInput &input,
                                              const CanFrame &native,
                                              uint32_t now_ms,
                                              CanFrame *out) {
    // A frame on a non-Vehicle bus (for example the Chassis copy of 0x3C2)
    // must not move the episode or the pair. Ignore it completely.
    if (!scroll_ack_is_vehicle_bus(runtime, input.bus, now_ms)) {
        return ScrollAckAction::None;
    }
    if (!scroll_ack_source_page1(native)) return ScrollAckAction::None;

    runtime->last_native_age_ms = native.rx_ms == 0u ? 0u : (uint32_t)(now_ms - native.rx_ms);
    runtime->last_das_age_ms = runtime->das39b_seen
        ? (uint32_t)(now_ms - runtime->das39b_rx_ms) : 0u;
    runtime->last_vehicle_bus1 = (uint8_t)(input.bus + 1u);
    runtime->last_tx_allowed = input.tx_allowed;

    ScrollAckBlock block = scroll_ack_gate_block(runtime, input, now_ms);
    bool idle = scroll_ack_source_idle(native);

    if (runtime->phase == ScrollAckPhase::SecondPending) {
        bool timed_out = (int32_t)(now_ms - runtime->second_due_ms) >
                         (int32_t)SCROLL_ACK_SECOND_TIMEOUT_MS;
        if (block != ScrollAckBlock::None || timed_out) {
            // A lost gate, or no idle frame for too long, ends the pair. No retry.
            // A lost gate also stops the Timer: it restarts a full interval later.
            if (block != ScrollAckBlock::None) runtime->timer_armed = false;
            runtime->phase = ScrollAckPhase::EpisodeDone;
            runtime->pairs_aborted++;
            runtime->last_block = ScrollAckBlock::PairAborted;
            return ScrollAckAction::None;
        }
        if (!scroll_ack_due(now_ms, runtime->second_due_ms)) {
            runtime->last_block = ScrollAckBlock::Gap;
            return ScrollAckAction::None;
        }
        if (!idle) {
            // Never overwrite a driver input. Wait for the next idle frame.
            runtime->last_block = ScrollAckBlock::NativeIdle;
            return ScrollAckAction::None;
        }
        runtime->last_block = ScrollAckBlock::None;
        scroll_ack_build(native, runtime->second_tick, out);
        return ScrollAckAction::SecondTick;
    }

    if (input.mode == SCROLL_ACK_MODE_TIMER) {
        // Timer mode has no per-prompt episode: a finished pair just waits.
        if (runtime->phase != ScrollAckPhase::Armed) runtime->phase = ScrollAckPhase::Armed;
        if (block != ScrollAckBlock::None) {
            runtime->timer_armed = false;
            runtime->last_block = block;
            return ScrollAckAction::None;
        }
        if (!runtime->timer_armed) {
            // AP just became eligible: wait one full interval before the first pair.
            runtime->timer_armed = true;
            runtime->next_due_ms = now_ms + scroll_ack_jitter_ms(
                input.interval_mean_ms, SCROLL_ACK_INTERVAL_SPREAD_PCT, input.rand);
            runtime->last_block = ScrollAckBlock::Timer;
            return ScrollAckAction::None;
        }
        if (!scroll_ack_due(now_ms, runtime->next_due_ms)) {
            runtime->last_block = ScrollAckBlock::Timer;
            return ScrollAckAction::None;
        }
        if (!idle) {
            // Due, but the driver is using a wheel control. Try the next frame.
            runtime->last_block = ScrollAckBlock::NativeIdle;
            return ScrollAckAction::None;
        }
        // Pairs are timed start to start, as measured on the TSL6.
        runtime->next_due_ms = now_ms + scroll_ack_jitter_ms(
            input.interval_mean_ms, SCROLL_ACK_INTERVAL_SPREAD_PCT, input.rand);
    } else {
        // Prompt mode. A cleared prompt rearms a finished or waiting episode.
        if (runtime->das39b_hands_on != 3u &&
            (runtime->phase == ScrollAckPhase::EpisodeDone || runtime->phase == ScrollAckPhase::Delay)) {
            runtime->phase = ScrollAckPhase::Armed;
        }
        if (block != ScrollAckBlock::None) {
            // A lost gate cancels a pending delay; the next prompt draws a new one.
            if (runtime->phase == ScrollAckPhase::Delay) runtime->phase = ScrollAckPhase::Armed;
            runtime->last_block = block;
            return ScrollAckAction::None;
        }
        if (runtime->phase == ScrollAckPhase::EpisodeDone) {
            // Ticks already sent for this prompt; wait until hands-on leaves 3.
            runtime->last_block = ScrollAckBlock::EpisodeDone;
            return ScrollAckAction::None;
        }
        if (runtime->das39b_hands_on != 3u) {
            runtime->last_block = ScrollAckBlock::DasHandsOn;
            return ScrollAckAction::None;
        }
        if (runtime->phase == ScrollAckPhase::Armed) {
            // New prompt: draw the random delay before the first tick.
            runtime->phase = ScrollAckPhase::Delay;
            runtime->next_due_ms = now_ms + scroll_ack_jitter_ms(
                input.delay_mean_ms, SCROLL_ACK_DELAY_SPREAD_PCT, input.rand);
        }
        if (!scroll_ack_due(now_ms, runtime->next_due_ms)) {
            runtime->last_block = ScrollAckBlock::Delay;
            return ScrollAckAction::None;
        }
        if (!idle) {
            // Prompt is up, but the driver is using a wheel control. Try the next frame.
            runtime->last_block = ScrollAckBlock::NativeIdle;
            return ScrollAckAction::None;
        }
    }

    uint8_t first = 0u;
    uint8_t second = 0u;
    bool pair = scroll_ack_pattern_ticks(input.pattern, &first, &second);
    runtime->second_tick = pair ? second : 0u;
    runtime->second_due_ms = now_ms + scroll_ack_jitter_ms(
        input.gap_mean_ms, SCROLL_ACK_GAP_SPREAD_PCT, scroll_ack_rand2(input.rand));
    runtime->last_block = ScrollAckBlock::None;
    scroll_ack_build(native, first, out);
    return ScrollAckAction::FirstTick;
}

// Advance the phase after a send result. Counters move only on success.
static inline void scroll_ack_complete(ScrollAckRuntime *runtime,
                                       ScrollAckAction action, bool sent,
                                       uint32_t decision_generation,
                                       bool enabled) {
    if (action == ScrollAckAction::None) return;
    if (sent) runtime->sent_count++;
    bool current = enabled && decision_generation == runtime->episode_generation;
    if (!current) return;  // a web reset owns the phase now
    if (action == ScrollAckAction::FirstTick) {
        // A single-tick pattern, or a failed first tick, ends this episode with
        // no retry. Otherwise wait for the balancing tick.
        bool pair = runtime->second_tick != 0u;
        runtime->phase = (sent && pair) ? ScrollAckPhase::SecondPending : ScrollAckPhase::EpisodeDone;
    } else {
        if (!sent) runtime->pairs_aborted++;
        runtime->phase = ScrollAckPhase::EpisodeDone;
    }
}

// Convenience for state-machine tests and callers with no intervening reset.
static inline void scroll_ack_complete(ScrollAckRuntime *runtime,
                                       ScrollAckAction action, bool sent) {
    scroll_ack_complete(runtime, action, sent, runtime->episode_generation, true);
}
