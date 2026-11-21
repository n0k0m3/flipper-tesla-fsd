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
 * "pattern" also takes the board values 4 (alternate) and 5 (random).
 *
 * Ported TSL6 features (SCRL_FEATURES below) follow these keys. Each key is the
 * TSL6 setting ID, and each value is the TSL6 raw value, so the control app
 * maps them by name. A value above the maximum of its feature is rejected.
 * A feature with a maximum of 1 also takes true and false.
 *
 * "tsl6" is last: the settings of the TSL6 car features engine
 * (components/tsl6), 28 bytes as 56 hex digits. Bytes 0-11 are the TSL6 A3
 * status bytes 24-35, bytes 12-27 are A3 bytes 59-74, with the TSL6 bit
 * layout. The factory value has every car feature off.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scroll_ack.h"  // FSDState, scroll_ack_clamp_settings, OpMode

// Board-only tick orders, after the four SCROLL_ACK_PATTERN_* values of the
// T-2CAN. They pick DOWN_UP or UP_DOWN for each pair (scrl_scroll_ext.h).
#define SCRL_PATTERN_ALTERNATE 4u  // the first tick changes direction each pair (TSL6 jj 2)
#define SCRL_PATTERN_RANDOM    5u  // a random first direction for each pair (TSL6 jj 3)

// Ported TSL6 features: X(name, TSL6 setting ID, maximum, default).
// The order is the layout of the NVS blob "feat": only add at the end.
//   lr    scroll wheel: 0 left (volume), 1 right (set speed), 2 alternate.
//         TSL6 value 3 (right-wheel tilt) is not ported.
//   rand  1 = Timer interval random (mean +/- 20 %), 0 = exact mean.
#define SCRL_FEATURES(X) \
    X(LR, "lr", 2, 0)    \
    X(RAND, "rand", 1, 1)

enum ScrlFeature : uint8_t {
#define SCRL_FEATURE_ENUM(name, key, max, def) SCRL_F_##name,
    SCRL_FEATURES(SCRL_FEATURE_ENUM)
#undef SCRL_FEATURE_ENUM
    SCRL_F_COUNT
};

struct ScrlFeatureDef {
    const char *key;  // JSON key = TSL6 setting ID
    uint8_t max;
    uint8_t def;
};

static const ScrlFeatureDef SCRL_FEATURE_DEFS[SCRL_F_COUNT] = {
#define SCRL_FEATURE_DEF(name, key, max, def) {key, max, def},
    SCRL_FEATURES(SCRL_FEATURE_DEF)
#undef SCRL_FEATURE_DEF
};

// Largest settings JSON: the fixed keys, each feature as ,"key":255, and "tsl6".
#define SCRL_SETTINGS_JSON_MAX 512u
// Settings of the TSL6 engine (TSL6_CFG_LEN in components/tsl6/port/tsl6_engine.h).
#define SCRL_TSL6_LEN 28u
// Factory TSL6 settings: A3 bytes 24-35 = 00 00 FF 01 01 00 00 00 01 01 00 00
// (every car feature off, the engine switch on), A3 bytes 59-74 = 0.
static const uint8_t SCRL_TSL6_FACTORY[SCRL_TSL6_LEN] = {0x00, 0x00, 0xFF, 0x01, 0x01, 0x00, 0x00,
                                                       0x00, 0x01, 0x01, 0x00, 0x00};
// Longest JSON key + 1.
#define SCRL_SETTINGS_KEY_MAX 32u

struct ScrlSettings {
    bool enabled;          // Scroll Ack master switch (NVS scrAck)
    uint8_t mode;          // SCROLL_ACK_MODE_*      (NVS scrMode)
    uint8_t pattern;       // SCROLL_ACK_PATTERN_*   (NVS scrPat)
    uint16_t delay_ms;     // Prompt delay mean      (NVS scrDly)
    uint16_t gap_ms;       // gap between the ticks  (NVS scrGap)
    uint16_t interval_ms;  // Timer interval mean    (NVS scrInt)
    uint8_t op_mode;       // OpMode_ListenOnly / OpMode_Active (NVS opMode)
    uint8_t feat[SCRL_F_COUNT];  // ported TSL6 features (NVS blob feat)
    uint8_t tsl6[SCRL_TSL6_LEN];  // TSL6 engine settings (NVS blob tsl6)
};

static inline int scrl_feature_find(const char *key) {
    for (int i = 0; i < (int)SCRL_F_COUNT; i++) {
        if (strcmp(key, SCRL_FEATURE_DEFS[i].key) == 0) return i;
    }
    return -1;
}

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
    for (int i = 0; i < (int)SCRL_F_COUNT; i++) s.feat[i] = SCRL_FEATURE_DEFS[i].def;
    memcpy(s.tsl6, SCRL_TSL6_FACTORY, sizeof(s.tsl6));
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
    uint8_t pattern = s->pattern;
    FSDState scratch;  // about 900 bytes of stack; only the Scroll Ack fields are used
    FSDState *state = &scratch;
    scrl_settings_to_state(*s, state);
    scroll_ack_clamp_settings(state);
    s->mode = state->scroll_ack_mode;
    s->pattern = state->scroll_ack_pattern;
    s->delay_ms = state->scroll_ack_delay_ms;
    s->gap_ms = state->scroll_ack_gap_ms;
    s->interval_ms = state->scroll_ack_interval_ms;
    if (pattern == SCRL_PATTERN_ALTERNATE || pattern == SCRL_PATTERN_RANDOM) s->pattern = pattern;
    if (s->op_mode != (uint8_t)OpMode_Active) s->op_mode = (uint8_t)OpMode_ListenOnly;
    for (int i = 0; i < (int)SCRL_F_COUNT; i++) {
        if (s->feat[i] > SCRL_FEATURE_DEFS[i].max) s->feat[i] = SCRL_FEATURE_DEFS[i].def;
    }
}

static inline bool scrl_settings_equal(const ScrlSettings &a, const ScrlSettings &b) {
    return a.enabled == b.enabled && a.mode == b.mode && a.pattern == b.pattern &&
           a.delay_ms == b.delay_ms && a.gap_ms == b.gap_ms &&
           a.interval_ms == b.interval_ms && a.op_mode == b.op_mode &&
           memcmp(a.feat, b.feat, sizeof(a.feat)) == 0 &&
           memcmp(a.tsl6, b.tsl6, sizeof(a.tsl6)) == 0;
}

/** Write the settings as JSON. Returns the length, or 0 when out is too small. */
static inline size_t scrl_settings_to_json(const ScrlSettings &s, char *out, size_t size) {
    int n = snprintf(out, size,
                     "{\"enabled\":%s,\"mode\":%u,\"pattern\":%u,\"delay_ms\":%u,"
                     "\"gap_ms\":%u,\"interval_ms\":%u,\"op_mode\":%u",
                     s.enabled ? "true" : "false", (unsigned)s.mode, (unsigned)s.pattern,
                     (unsigned)s.delay_ms, (unsigned)s.gap_ms, (unsigned)s.interval_ms,
                     (unsigned)s.op_mode);
    if (n <= 0 || (size_t)n >= size) return 0u;
    size_t len = (size_t)n;
    for (int i = 0; i < (int)SCRL_F_COUNT; i++) {
        n = snprintf(out + len, size - len, ",\"%s\":%u", SCRL_FEATURE_DEFS[i].key,
                     (unsigned)s.feat[i]);
        if (n <= 0 || (size_t)n >= size - len) return 0u;
        len += (size_t)n;
    }
    static const char hex[] = "0123456789abcdef";
    if (len + 10u + 2u * SCRL_TSL6_LEN + 1u >= size) return 0u;
    memcpy(out + len, ",\"tsl6\":\"", 9);
    len += 9u;
    for (unsigned i = 0; i < SCRL_TSL6_LEN; i++) {
        out[len++] = hex[s.tsl6[i] >> 4];
        out[len++] = hex[s.tsl6[i] & 15u];
    }
    out[len++] = '"';
    if (len + 1u >= size) return 0u;
    out[len++] = '}';
    out[len] = '\0';
    return len;
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

// Read "<56 hex digits>" (upper or lower case) into out.
static inline bool scrl_json_hex(ScrlJsonCursor *c, uint8_t *out, size_t n) {
    if (!scrl_json_take(c, '"')) return false;
    if ((size_t)(c->end - c->p) < 2u * n + 1u) return false;
    for (size_t i = 0; i < 2u * n; i++) {
        char ch = c->p[i];
        uint8_t v;
        if (ch >= '0' && ch <= '9') v = (uint8_t)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v = (uint8_t)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v = (uint8_t)(ch - 'A' + 10);
        else return false;
        out[i / 2u] = (uint8_t)((i & 1u) ? (out[i / 2u] | v) : (v << 4));
    }
    if (c->p[2u * n] != '"') return false;
    c->p += 2u * n + 1u;
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
            char key[SCRL_SETTINGS_KEY_MAX];
            uint32_t value = 0;
            bool was_bool = false;
            if (!scrl_json_key(&c, key, sizeof(key))) return ScrlSettingsParse::Syntax;
            if (!scrl_json_take(&c, ':')) return ScrlSettingsParse::Syntax;
            if (strcmp(key, "tsl6") == 0) {
                // Raw bytes: the TSL6 engine reads them as the TSL6 did.
                if (!scrl_json_hex(&c, next.tsl6, SCRL_TSL6_LEN)) return ScrlSettingsParse::BadValue;
                if (scrl_json_take(&c, ',')) continue;
                if (scrl_json_take(&c, '}')) break;
                return ScrlSettingsParse::Syntax;
            }
            if (!scrl_json_value(&c, &value, &was_bool)) return ScrlSettingsParse::Syntax;

            int feature = scrl_feature_find(key);
            if (strcmp(key, "enabled") == 0) {
                if (value > 1u) return ScrlSettingsParse::BadValue;
                next.enabled = value == 1u;
            } else if (feature >= 0) {
                const ScrlFeatureDef &def = SCRL_FEATURE_DEFS[feature];
                if (was_bool && def.max != 1u) return ScrlSettingsParse::BadValue;
                // Reject, not clamp: a value the board does not know must not
                // turn into another car function.
                if (value > def.max) return ScrlSettingsParse::BadValue;
                next.feat[feature] = (uint8_t)value;
            } else if (was_bool) {
                return ScrlSettingsParse::BadValue;  // only "enabled" and 0/1 features are booleans
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
