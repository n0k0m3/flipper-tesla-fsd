#pragma once
/*
 * scrl_scroll_ext.h — board-only additions to Scroll Ack, ported from the
 * TSL6 scroll settings. Header-only and free of ESP-IDF, so the host test
 * covers it (test/test_scrl_scroll_ext.cpp).
 *
 * scroll_ack.h stays the same as on the car-tested T-2CAN. This file changes
 * only what goes in and what comes out of scroll_ack_step():
 *   - Tick order (setting "pattern" 4 and 5): before each step, pick DOWN_UP or
 *     UP_DOWN. scroll_ack_step() reads the pattern only for a first tick.
 *   - Wheel (feature "lr"): after a first tick, choose the wheel of the pair.
 *     For the right wheel, move the tick from byte 2 (swcLeftScrollTicks) to
 *     byte 3 (swcRightScrollTicks). Both ticks of a pair use the same wheel.
 *   - Fixed interval (feature "rand" = 0): in Timer mode, replace the random
 *     due time that scroll_ack_step() drew with the exact mean.
 *
 * TSL6 reference (C rewrite of the stock runtime, scroll_gesture()):
 *   - lr 0 left, 1 right, 2 alternate left and right. lr 3 sends right-wheel
 *     tilt bits, not scroll ticks. The board does not port it.
 *   - jj 2 keeps the last direction. The reverse tick of a pair flips it, so
 *     the first tick changes direction from one pair to the next.
 *   - jj 3 draws the first direction at random for each pair.
 *   - rand 0 gives the exact interval. rand 1 adds a random part.
 */

#include <stdbool.h>
#include <stdint.h>

#include "scroll_ack.h"
#include "scrl_settings.h"

#define SCRL_LR_LEFT      0u
#define SCRL_LR_RIGHT     1u
#define SCRL_LR_ALTERNATE 2u

struct ScrlScrollExt {
    bool last_up_first;  // the first tick of the last pair was +1
    bool last_right;     // the last pair used the right wheel
    bool pair_right;     // the pair that runs now uses the right wheel
    bool any_pair;       // a first tick was sent since boot
};

/** The SCROLL_ACK_PATTERN_* value for the next first tick. */
static inline uint8_t scrl_ext_pattern(const ScrlScrollExt *ext, uint8_t pattern, uint32_t rand) {
    if (pattern == SCRL_PATTERN_ALTERNATE) {
        // The first pair starts with -1, the same as the default.
        bool up = ext->any_pair && !ext->last_up_first;
        return up ? SCROLL_ACK_PATTERN_UP_DOWN : SCROLL_ACK_PATTERN_DOWN_UP;
    }
    if (pattern == SCRL_PATTERN_RANDOM) {
        // Bit 7: scroll_ack_step() uses the low bits of the same value for its draws.
        return ((rand >> 7) & 1u) ? SCROLL_ACK_PATTERN_UP_DOWN : SCROLL_ACK_PATTERN_DOWN_UP;
    }
    return pattern;
}

/** The wheel of a new pair: true = right. */
static inline bool scrl_ext_next_right(const ScrlScrollExt *ext, uint8_t lr) {
    if (lr == SCRL_LR_RIGHT) return true;
    if (lr == SCRL_LR_ALTERNATE) return ext->any_pair && !ext->last_right;  // left first
    return false;
}

/**
 * Move the tick of *out from the left wheel to the right wheel. *out is a copy
 * of `native` with only the left tick field changed (scroll_ack_build()).
 */
static inline void scrl_ext_to_right(const CanFrame &native, CanFrame *out) {
    uint8_t tick = (uint8_t)(out->data[2] & 0x3Fu);
    out->data[2] = native.data[2];
    out->data[3] = (uint8_t)((native.data[3] & 0xC0u) | tick);
}

/**
 * Apply the wheel to a frame that scroll_ack_step() built. Call it for every
 * action that is not None, before the send. A first tick picks the wheel of
 * the pair, and the second tick uses the same wheel.
 */
static inline void scrl_ext_place_tick(ScrlScrollExt *ext, uint8_t lr, ScrollAckAction act,
                                       const CanFrame &native, CanFrame *out) {
    if (act == ScrollAckAction::None) return;
    if (act == ScrollAckAction::FirstTick) ext->pair_right = scrl_ext_next_right(ext, lr);
    if (ext->pair_right) scrl_ext_to_right(native, out);
}

/** Record a first tick that went out, so that the next pair can alternate. */
static inline void scrl_ext_first_sent(ScrlScrollExt *ext, const CanFrame &out) {
    ext->last_right = ext->pair_right;
    uint8_t field = ext->pair_right ? out.data[3] : out.data[2];
    ext->last_up_first = (field & 0x3Fu) == SCROLL_ACK_LTICK_POS;
    ext->any_pair = true;
}

/**
 * Fixed interval: when this step armed the Timer or sent a first tick in
 * Timer mode, set the next due time to now + the mean. armed_before is
 * runtime->timer_armed before the step.
 */
static inline void scrl_ext_fix_interval(ScrollAckRuntime *runtime, bool armed_before,
                                         ScrollAckAction act, uint8_t mode,
                                         uint16_t interval_mean_ms, uint32_t now_ms) {
    if (mode != SCROLL_ACK_MODE_TIMER) return;
    bool armed_now = !armed_before && runtime->timer_armed;
    if (armed_now || act == ScrollAckAction::FirstTick) {
        runtime->next_due_ms = now_ms + interval_mean_ms;
    }
}
