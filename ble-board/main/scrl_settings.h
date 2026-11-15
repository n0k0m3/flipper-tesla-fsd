#pragma once
/*
 * scrl_settings.h — the settings of the BLE board and their JSON codec (the
 * BLE Settings characteristic). Header-only and free of ESP-IDF, so the host
 * test covers it (test/test_scrl_settings.cpp).
 *
 * Names and ranges match the T-2CAN dashboard. scroll_ack_clamp_settings()
 * (esp32/.firmware/scroll_ack.h) stays the one place that knows the ranges.
 *
 * JSON in:  one flat object. Each key is optional. A write changes only the
 *           keys it names. Any error rejects the whole write.
 *   {"enabled":true,"mode":0,"pattern":0,"delay_ms":600,"gap_ms":100,
 *    "interval_ms":2500,"op_mode":0}
 * Numbers are unsigned integers. "enabled" takes true, false, 1 or 0.
 * "op_mode" takes only 0 (Listen-Only) or 1 (Active).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scroll_ack.h"  // FSDState, scroll_ack_clamp_settings, OpMode

struct ScrlSettings {
    bool enabled;          // Scroll Ack master switch (NVS scrAck)
    uint8_t mode;          // SCROLL_ACK_MODE_*      (NVS scrMode)
    uint8_t pattern;       // SCROLL_ACK_PATTERN_*   (NVS scrPat)
    uint16_t delay_ms;     // Prompt delay mean      (NVS scrDly)
    uint16_t gap_ms;       // gap between the ticks  (NVS scrGap)
    uint16_t interval_ms;  // Timer interval mean    (NVS scrInt)
    uint8_t op_mode;       // OpMode_ListenOnly / OpMode_Active (NVS opMode)
};

enum class ScrlSettingsParse : uint8_t { Ok, Syntax, UnknownKey, BadValue };

/** Safe defaults: Scroll Ack off, Listen-Only. */
static inline ScrlSettings scrl_settings_defaults(void) {
    ScrlSettings s;
    s.enabled = false;
    s.mode = SCROLL_ACK_MODE_PROMPT;
    s.pattern = SCROLL_ACK_PATTERN_DOWN_UP;
    s.delay_ms = SCROLL_ACK_DELAY_DEFAULT_MS;
    s.gap_ms = SCROLL_ACK_GAP_DEFAULT_MS;
    s.interval_ms = SCROLL_ACK_INTERVAL_DEFAULT_MS;
    s.op_mode = (uint8_t)OpMode_ListenOnly;
    return s;
}

/** Copy the settings into the shared state that the Scroll Ack code reads. */
static inline void scrl_settings_to_state(const ScrlSettings &s, FSDState *state) {
    state->scroll_ack = s.enabled;
    state->scroll_ack_mode = s.mode;
    state->scroll_ack_pattern = s.pattern;
    state->scroll_ack_delay_ms = s.delay_ms;
    state->scroll_ack_gap_ms = s.gap_ms;
    state->scroll_ack_interval_ms = s.interval_ms;
}

/** Force every value into its valid range. An unknown op mode is Listen-Only. */
static inline void scrl_settings_clamp(ScrlSettings *s) {
    FSDState scratch;  // about 900 bytes of stack; only the Scroll Ack fields are used
    FSDState *state = &scratch;
    scrl_settings_to_state(*s, state);
    scroll_ack_clamp_settings(state);
    s->mode = state->scroll_ack_mode;
    s->pattern = state->scroll_ack_pattern;
    s->delay_ms = state->scroll_ack_delay_ms;
    s->gap_ms = state->scroll_ack_gap_ms;
    s->interval_ms = state->scroll_ack_interval_ms;
    if (s->op_mode != (uint8_t)OpMode_Active) s->op_mode = (uint8_t)OpMode_ListenOnly;
}

static inline bool scrl_settings_equal(const ScrlSettings &a, const ScrlSettings &b) {
    return a.enabled == b.enabled && a.mode == b.mode && a.pattern == b.pattern &&
           a.delay_ms == b.delay_ms && a.gap_ms == b.gap_ms &&
           a.interval_ms == b.interval_ms && a.op_mode == b.op_mode;
}

/** Write the settings as JSON. Returns the length, or 0 when out is too small. */
static inline size_t scrl_settings_to_json(const ScrlSettings &s, char *out, size_t size) {
    int n = snprintf(out, size,
                     "{\"enabled\":%s,\"mode\":%u,\"pattern\":%u,\"delay_ms\":%u,"
                     "\"gap_ms\":%u,\"interval_ms\":%u,\"op_mode\":%u}",
                     s.enabled ? "true" : "false", (unsigned)s.mode, (unsigned)s.pattern,
                     (unsigned)s.delay_ms, (unsigned)s.gap_ms, (unsigned)s.interval_ms,
                     (unsigned)s.op_mode);
    return (n > 0 && (size_t)n < size) ? (size_t)n : 0u;
}

// ── JSON reader (flat object, unsigned integers and booleans only) ────────────

struct ScrlJsonCursor {
    const char *p;
    const char *end;
};

static inline void scrl_json_skip_ws(ScrlJsonCursor *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n')) {
        c->p++;
    }
}

static inline bool scrl_json_take(ScrlJsonCursor *c, char ch) {
    scrl_json_skip_ws(c);
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return true;
    }
    return false;
}

// Read "key" into out. Keys are short ASCII names with no escapes.
static inline bool scrl_json_key(ScrlJsonCursor *c, char *out, size_t size) {
    if (!scrl_json_take(c, '"')) return false;
    size_t n = 0;
    while (c->p < c->end && *c->p != '"') {
        if (*c->p == '\\' || n + 1u >= size) return false;
        out[n++] = *c->p++;
    }
    if (c->p >= c->end) return false;
    c->p++;  // closing quote
    out[n] = '\0';
    return true;
}

// Read an unsigned integer, or true / false as 1 / 0. A value above 2^32 - 1
// stops at 2^32 - 1, so the range clamp sees a large number, not a wrapped one.
static inline bool scrl_json_value(ScrlJsonCursor *c, uint32_t *out, bool *was_bool) {
    scrl_json_skip_ws(c);
    *was_bool = false;
    size_t left = (size_t)(c->end - c->p);
    if (left >= 4u && memcmp(c->p, "true", 4) == 0) {
        c->p += 4;
        *out = 1u;
        *was_bool = true;
        return true;
    }
    if (left >= 5u && memcmp(c->p, "false", 5) == 0) {
        c->p += 5;
        *out = 0u;
        *was_bool = true;
        return true;
    }
    if (c->p >= c->end || *c->p < '0' || *c->p > '9') return false;
    uint64_t v = 0;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        v = v * 10u + (uint64_t)(*c->p - '0');
        if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;
        c->p++;
    }
    // A fraction, an exponent or a letter after the digits is not an integer.
    if (c->p < c->end && (*c->p == '.' || *c->p == 'e' || *c->p == 'E' || *c->p == '-' ||
                          (*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z'))) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static inline uint16_t scrl_sat_u16(uint32_t v) {
    return v > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)v;
}

static inline uint8_t scrl_sat_u8(uint32_t v) {
    return v > 0xFFu ? (uint8_t)0xFFu : (uint8_t)v;
}

/**
 * Apply a JSON write to *inout. On Ok the result is clamped. On any error
 * *inout does not change. *op_mode_named (optional) tells if the write names
 * "op_mode": a write of Active is a deliberate act, a write of another key
 * while the mode is Active is not.
 */
static inline ScrlSettingsParse scrl_settings_from_json(const char *buf, size_t len,
                                                        ScrlSettings *inout,
                                                        bool *op_mode_named = nullptr) {
    ScrlJsonCursor c = {buf, buf + len};
    ScrlSettings next = *inout;
    bool named = false;
    if (op_mode_named) *op_mode_named = false;
    if (!scrl_json_take(&c, '{')) return ScrlSettingsParse::Syntax;
    if (!scrl_json_take(&c, '}')) {
        for (;;) {
            char key[16];
            uint32_t value = 0;
            bool was_bool = false;
            if (!scrl_json_key(&c, key, sizeof(key))) return ScrlSettingsParse::Syntax;
            if (!scrl_json_take(&c, ':')) return ScrlSettingsParse::Syntax;
            if (!scrl_json_value(&c, &value, &was_bool)) return ScrlSettingsParse::Syntax;

            if (strcmp(key, "enabled") == 0) {
                if (value > 1u) return ScrlSettingsParse::BadValue;
                next.enabled = value == 1u;
            } else if (was_bool) {
                return ScrlSettingsParse::BadValue;  // only "enabled" is a boolean
            } else if (strcmp(key, "mode") == 0) {
                next.mode = scrl_sat_u8(value);
            } else if (strcmp(key, "pattern") == 0) {
                next.pattern = scrl_sat_u8(value);
            } else if (strcmp(key, "delay_ms") == 0) {
                next.delay_ms = scrl_sat_u16(value);
            } else if (strcmp(key, "gap_ms") == 0) {
                next.gap_ms = scrl_sat_u16(value);
            } else if (strcmp(key, "interval_ms") == 0) {
                next.interval_ms = scrl_sat_u16(value);
            } else if (strcmp(key, "op_mode") == 0) {
                // Never guess Active from a bad value.
                if (value > (uint32_t)OpMode_Active) return ScrlSettingsParse::BadValue;
                next.op_mode = (uint8_t)value;
                named = true;
            } else {
                return ScrlSettingsParse::UnknownKey;
            }

            if (scrl_json_take(&c, ',')) continue;
            if (scrl_json_take(&c, '}')) break;
            return ScrlSettingsParse::Syntax;
        }
    }
    scrl_json_skip_ws(&c);
    if (c.p != c.end) return ScrlSettingsParse::Syntax;  // text after the object
    scrl_settings_clamp(&next);
    *inout = next;
    if (op_mode_named) *op_mode_named = named;
    return ScrlSettingsParse::Ok;
}
