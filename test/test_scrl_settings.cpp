/*
 * test_scrl_settings.cpp — host tests for the settings codec of the BLE board
 * (ble-board/main/scrl_settings.h).
 * Build + run: make -C test check
 */

#include <stdio.h>
#include <string.h>

#include "scrl_passkey.h"
#include "scrl_settings.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            g_pass++;                                                           \
        } else {                                                                \
            g_fail++;                                                           \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                                \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

static ScrlSettingsParse parse(const char *json, ScrlSettings *s) {
    return scrl_settings_from_json(json, strlen(json), s);
}

static void test_defaults(void) {
    ScrlSettings s = scrl_settings_defaults();
    CHECK(!s.enabled, "Scroll Ack is off by default");
    CHECK(s.op_mode == OpMode_ListenOnly, "Listen-Only by default");
    CHECK(s.mode == SCROLL_ACK_MODE_PROMPT, "Prompt mode by default");
    CHECK(s.pattern == SCROLL_ACK_PATTERN_DOWN_UP, "-1 then +1 by default");
    CHECK(s.delay_ms == 600 && s.gap_ms == 100 && s.interval_ms == 2500, "default times");
    CHECK(s.feat[SCRL_F_LR] == 0 && s.feat[SCRL_F_RAND] == 1, "left wheel, random interval");
    ScrlSettings c = s;
    scrl_settings_clamp(&c);
    CHECK(scrl_settings_equal(c, s), "the defaults are already in range");
}

static void test_round_trip(void) {
    ScrlSettings s = scrl_settings_defaults();
    s.enabled = true;
    s.mode = SCROLL_ACK_MODE_TIMER;
    s.pattern = SCROLL_ACK_PATTERN_UP_ONCE;
    s.delay_ms = 1234;
    s.gap_ms = 250;
    s.interval_ms = 4000;
    s.op_mode = OpMode_Active;
    s.feat[SCRL_F_LR] = 2;
    s.feat[SCRL_F_RAND] = 0;
    char json[SCRL_SETTINGS_JSON_MAX];
    size_t n = scrl_settings_to_json(s, json, sizeof(json));
    CHECK(n > 0 && n == strlen(json), "to_json returns the length");
    CHECK(strcmp(json, "{\"enabled\":true,\"mode\":1,\"pattern\":2,\"delay_ms\":1234,"
                       "\"gap_ms\":250,\"interval_ms\":4000,\"op_mode\":1,"
                       "\"lr\":2,\"rand\":0}") == 0,
          "exact JSON text: %s", json);
    ScrlSettings back = scrl_settings_defaults();
    CHECK(parse(json, &back) == ScrlSettingsParse::Ok, "own JSON parses");
    CHECK(scrl_settings_equal(back, s), "round trip keeps every value");

    char small[20];
    CHECK(scrl_settings_to_json(s, small, sizeof(small)) == 0, "a small buffer returns 0");
    // Too small for the last feature or for the closing brace only.
    for (size_t cut = n - 8u; cut <= n; cut++) {
        CHECK(scrl_settings_to_json(s, json, cut) == 0, "a buffer of %zu bytes returns 0", cut);
    }
    CHECK(scrl_settings_to_json(s, json, n + 1u) == n, "a buffer of exactly n + 1 bytes is enough");

    // Every feature at 255 (worst case) still fits in SCRL_SETTINGS_JSON_MAX.
    ScrlSettings big = s;
    big.enabled = false;
    big.delay_ms = big.gap_ms = big.interval_ms = 65535;
    big.mode = big.pattern = big.op_mode = 255;
    memset(big.feat, 255, sizeof(big.feat));
    CHECK(scrl_settings_to_json(big, json, sizeof(json)) > 0, "the worst case fits");
}

static void test_features(void) {
    ScrlSettings s = scrl_settings_defaults();
    CHECK(parse("{\"lr\":1}", &s) == ScrlSettingsParse::Ok && s.feat[SCRL_F_LR] == 1, "lr 1");
    CHECK(parse("{\"lr\":2,\"rand\":false}", &s) == ScrlSettingsParse::Ok &&
              s.feat[SCRL_F_LR] == 2 && s.feat[SCRL_F_RAND] == 0, "lr 2, rand false");
    CHECK(parse("{\"rand\":true}", &s) == ScrlSettingsParse::Ok && s.feat[SCRL_F_RAND] == 1,
          "a 0/1 feature takes true");
    ScrlSettings before = s;
    CHECK(parse("{\"lr\":3}", &s) == ScrlSettingsParse::BadValue, "lr 3 (tilt) is not ported");
    CHECK(parse("{\"lr\":true}", &s) == ScrlSettingsParse::BadValue, "lr is not a boolean");
    CHECK(parse("{\"rand\":2}", &s) == ScrlSettingsParse::BadValue, "rand above 1");
    CHECK(parse("{\"rand\":0,\"lr\":9}", &s) == ScrlSettingsParse::BadValue, "one bad value rejects all");
    CHECK(scrl_settings_equal(s, before), "rejected writes change nothing");

    // The clamp puts a stored value that is out of range back to the default.
    s.feat[SCRL_F_LR] = 7;
    s.feat[SCRL_F_RAND] = 5;
    scrl_settings_clamp(&s);
    CHECK(s.feat[SCRL_F_LR] == 0 && s.feat[SCRL_F_RAND] == 1, "clamp to the defaults");

    // Board tick orders survive the clamp; other unknown patterns do not.
    CHECK(parse("{\"pattern\":4}", &s) == ScrlSettingsParse::Ok && s.pattern == SCRL_PATTERN_ALTERNATE,
          "pattern 4 (alternate)");
    CHECK(parse("{\"pattern\":5}", &s) == ScrlSettingsParse::Ok && s.pattern == SCRL_PATTERN_RANDOM,
          "pattern 5 (random)");
    CHECK(parse("{\"pattern\":6}", &s) == ScrlSettingsParse::Ok && s.pattern == SCROLL_ACK_PATTERN_DOWN_UP,
          "pattern 6 becomes -1 then +1");

    // Every feature key is unique, fits the key buffer and is found by name.
    for (int i = 0; i < (int)SCRL_F_COUNT; i++) {
        CHECK(strlen(SCRL_FEATURE_DEFS[i].key) < SCRL_SETTINGS_KEY_MAX, "key %d fits", i);
        CHECK(scrl_feature_find(SCRL_FEATURE_DEFS[i].key) == i, "key %d is unique", i);
        CHECK(SCRL_FEATURE_DEFS[i].def <= SCRL_FEATURE_DEFS[i].max, "default %d in range", i);
    }
}

static void test_partial_write(void) {
    ScrlSettings s = scrl_settings_defaults();
    CHECK(parse("{\"gap_ms\":300}", &s) == ScrlSettingsParse::Ok, "one key");
    CHECK(s.gap_ms == 300, "the named key changes");
    CHECK(s.delay_ms == 600 && s.interval_ms == 2500 && !s.enabled, "the other keys stay");
    CHECK(parse("{}", &s) == ScrlSettingsParse::Ok, "an empty object changes nothing");
    CHECK(s.gap_ms == 300, "still 300");
    CHECK(parse(" {\r\n \"enabled\" : true ,\t\"op_mode\" : 1 } \n", &s) ==
              ScrlSettingsParse::Ok, "white space is allowed");
    CHECK(s.enabled && s.op_mode == OpMode_Active, "both keys applied");
    CHECK(parse("{\"enabled\":0}", &s) == ScrlSettingsParse::Ok && !s.enabled,
          "enabled takes 0");
    CHECK(parse("{\"enabled\":1}", &s) == ScrlSettingsParse::Ok && s.enabled,
          "enabled takes 1");
}

static void test_clamp(void) {
    ScrlSettings s = scrl_settings_defaults();
    CHECK(parse("{\"delay_ms\":9000,\"gap_ms\":5000,\"interval_ms\":50}", &s) ==
              ScrlSettingsParse::Ok, "values out of range are accepted");
    CHECK(s.delay_ms == SCROLL_ACK_DELAY_MAX_MS, "delay stops at the maximum: %u", s.delay_ms);
    CHECK(s.gap_ms == SCROLL_ACK_GAP_MAX_MS, "gap stops at the maximum: %u", s.gap_ms);
    CHECK(s.interval_ms == SCROLL_ACK_INTERVAL_MIN_MS, "interval stops at the minimum: %u",
          s.interval_ms);
    // The longest TSL6 timer value (30-35 s) fits: the board keeps it.
    CHECK(parse("{\"interval_ms\":32500}", &s) == ScrlSettingsParse::Ok, "TSL6 timer 8");
    CHECK(s.interval_ms == 32500u, "a 32.5 s interval stays: %u", s.interval_ms);
    CHECK(parse("{\"interval_ms\":99999}", &s) == ScrlSettingsParse::Ok, "large interval");
    CHECK(s.interval_ms == SCROLL_ACK_INTERVAL_MAX_MS, "interval stops at the maximum: %u",
          s.interval_ms);
    // 65536 + 100 must not wrap to 100.
    CHECK(parse("{\"delay_ms\":65636}", &s) == ScrlSettingsParse::Ok, "above 16 bits");
    CHECK(s.delay_ms == SCROLL_ACK_DELAY_MAX_MS, "no 16-bit wrap: %u", s.delay_ms);
    // 2^32 + 100 must not wrap to 100.
    CHECK(parse("{\"gap_ms\":4294967396}", &s) == ScrlSettingsParse::Ok, "above 32 bits");
    CHECK(s.gap_ms == SCROLL_ACK_GAP_MAX_MS, "no 32-bit wrap: %u", s.gap_ms);
    CHECK(parse("{\"mode\":7,\"pattern\":200}", &s) == ScrlSettingsParse::Ok, "unknown choices");
    CHECK(s.mode == SCROLL_ACK_MODE_PROMPT, "an unknown mode becomes Prompt");
    CHECK(s.pattern == SCROLL_ACK_PATTERN_DOWN_UP, "an unknown pattern becomes -1 then +1");
    // 256 + 1 must not wrap to Timer.
    CHECK(parse("{\"mode\":257}", &s) == ScrlSettingsParse::Ok && s.mode == SCROLL_ACK_MODE_PROMPT,
          "no 8-bit wrap on mode");

    ScrlSettings raw = scrl_settings_defaults();
    raw.op_mode = 2;  // OpMode_Service from a damaged store
    scrl_settings_clamp(&raw);
    CHECK(raw.op_mode == OpMode_ListenOnly, "an unknown stored op mode becomes Listen-Only");
}

static void test_rejects(void) {
    ScrlSettings start = scrl_settings_defaults();
    start.gap_ms = 111;
    struct Case {
        const char *json;
        ScrlSettingsParse want;
    };
    const Case cases[] = {
        {"", ScrlSettingsParse::Syntax},
        {"[]", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":300", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":300}x", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":300,}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\" 300}", ScrlSettingsParse::Syntax},
        {"{gap_ms:300}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":-5}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":1.5}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":1e3}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":\"300\"}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":null}", ScrlSettingsParse::Syntax},
        {"{\"gap_ms\":true}", ScrlSettingsParse::BadValue},
        {"{\"enabled\":2}", ScrlSettingsParse::BadValue},
        {"{\"op_mode\":2}", ScrlSettingsParse::BadValue},
        {"{\"op_mode\":true}", ScrlSettingsParse::BadValue},
        {"{\"volume\":3}", ScrlSettingsParse::UnknownKey},
        {"{\"a_key_name_longer_than_31_chars_x\":3}", ScrlSettingsParse::Syntax},
        {"{\"a_very_long_key_name\":3}", ScrlSettingsParse::UnknownKey},
        // A good key first: the bad one must still reject the whole write.
        {"{\"gap_ms\":300,\"op_mode\":9}", ScrlSettingsParse::BadValue},
        {"{\"enabled\":true,\"nope\":1}", ScrlSettingsParse::UnknownKey},
    };
    for (const Case &c : cases) {
        ScrlSettings s = start;
        ScrlSettingsParse got = parse(c.json, &s);
        CHECK(got == c.want, "'%s' gives %d, want %d", c.json, (int)got, (int)c.want);
        CHECK(scrl_settings_equal(s, start), "'%s' must change nothing", c.json);
    }
    // The reader takes a length, not a terminator.
    ScrlSettings s = start;
    const char *text = "{\"gap_ms\":300}{\"op_mode\":1}";
    CHECK(scrl_settings_from_json(text, 14, &s) == ScrlSettingsParse::Ok && s.gap_ms == 300 &&
              s.op_mode == OpMode_ListenOnly, "only len bytes are read");
}

static void test_op_mode_named(void) {
    ScrlSettings s = scrl_settings_defaults();
    s.op_mode = OpMode_Active;
    bool named = true;
    const char *other = "{\"gap_ms\":300}";
    CHECK(scrl_settings_from_json(other, strlen(other), &s, &named) == ScrlSettingsParse::Ok &&
              !named && s.op_mode == OpMode_Active,
          "a write of another key keeps Active and does not name op_mode");
    const char *active = "{\"op_mode\":1}";
    CHECK(scrl_settings_from_json(active, strlen(active), &s, &named) == ScrlSettingsParse::Ok &&
              named, "a write of op_mode names it, also when the value does not change");
    const char *bad = "{\"op_mode\":1,\"nope\":1}";
    named = true;
    CHECK(scrl_settings_from_json(bad, strlen(bad), &s, &named) == ScrlSettingsParse::UnknownKey &&
              !named, "a refused write names nothing");
}

static bool passkey(const char *text, uint32_t *out) {
    return scrl_passkey_from_digits((const uint8_t *)text, strlen(text), out);
}

static void test_passkey_digits(void) {
    uint32_t v = 77;
    CHECK(passkey("123456", &v) && v == 123456u, "six digits");
    CHECK(passkey("000000", &v) && v == 0u, "all zeros is a passkey");
    CHECK(passkey("000042", &v) && v == 42u, "leading zeros count as digits");
    CHECK(passkey("999999", &v) && v == SCRL_PASSKEY_MAX, "largest passkey");
    v = 77;
    const char *bad[] = {"", "12345", "1234567", "12345a", " 23456", "12 456", "-12345", "12345\n", "1.2345"};
    for (const char *text : bad) {
        CHECK(!passkey(text, &v), "'%s' is not a passkey", text);
        CHECK(v == 77u, "'%s' must not change the output", text);
    }
    // The reader takes a length. Digits after it are not read.
    CHECK(scrl_passkey_from_digits((const uint8_t *)"1234567", 6, &v) && v == 123456u, "only len bytes are read");
    const uint8_t with_nul[6] = {'1', '2', '3', 0, '5', '6'};
    CHECK(!scrl_passkey_from_digits(with_nul, sizeof(with_nul), &v), "a zero byte is not a digit");
}

static void test_to_state(void) {
    ScrlSettings s = scrl_settings_defaults();
    s.enabled = true;
    s.mode = SCROLL_ACK_MODE_TIMER;
    s.pattern = SCROLL_ACK_PATTERN_DOWN_ONCE;
    s.delay_ms = 10;
    s.gap_ms = 20;
    s.interval_ms = 3000;
    s.op_mode = OpMode_Active;
    FSDState state;
    memset(&state, 0, sizeof(state));
    scrl_settings_to_state(s, &state);
    CHECK(state.scroll_ack && state.scroll_ack_mode == SCROLL_ACK_MODE_TIMER &&
              state.scroll_ack_pattern == SCROLL_ACK_PATTERN_DOWN_ONCE, "switch, mode, pattern");
    CHECK(state.scroll_ack_delay_ms == 10 && state.scroll_ack_gap_ms == 20 &&
              state.scroll_ack_interval_ms == 3000, "times");
    // The op mode has gates of its own (bus-off lock, update). Only the firmware sets it.
    CHECK(state.op_mode == OpMode_ListenOnly, "to_state never sets the op mode");
}

int main(void) {
    printf("test_scrl_settings\n");
    test_defaults();
    test_round_trip();
    test_features();
    test_partial_write();
    test_clamp();
    test_rejects();
    test_op_mode_named();
    test_passkey_digits();
    test_to_state();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
