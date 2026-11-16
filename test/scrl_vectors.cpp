/*
 * scrl_vectors.cpp — prints what the firmware code of the BLE board makes and
 * accepts, for the test of the web page (ble-board/web/test/page.test.mjs).
 * The page test must use the real packers and the real settings reader, not
 * a copy of them.
 *
 *   scrl_vectors            print the fixtures as one JSON object
 *   scrl_vectors parse      read two lines from stdin: the settings on the
 *                           board (JSON), then a write (JSON). Print the result.
 *   scrl_vectors passkey    read one line from stdin: a Command write as hex.
 *                           Print if the board takes it as a new passkey.
 *
 * Build: make -C test scrl_vectors
 */

#include <stdio.h>
#include <string.h>

#include <string>

#include "scrl_ble.h"  // SCRL_CMD_SET_PASSKEY
#include "scrl_passkey.h"
#include "scrl_sample.h"
#include "scrl_settings.h"
#include "scrl_status.h"

// Print a C string as a JSON string.
static void put_json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

static void put_hex(const uint8_t *data, size_t len) {
    putchar('"');
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    putchar('"');
}

static int print_fixtures(void) {
    char json[SCRL_STATUS_JSON_MAX + 1u];
    uint8_t live[SCRL_LIVE_RECORD_LEN];

    ScrlStatus sample = sample_status();
    if (scrl_status_json(sample, json, sizeof(json)) == 0u) return 1;
    scrl_live_pack(sample, live);
    printf("{\"status_json\":");
    put_json_string(json);
    printf(",\"live_hex\":");
    put_hex(live, sizeof(live));

    // The largest numbers that every field can hold.
    ScrlStatus big;
    memset(&big, 0xFF, sizeof(big));
    big.scroll.phase = ScrollAckPhase::EpisodeDone;
    big.scroll.last_block = ScrollAckBlock::DasHandsOn;
    big.scroll.last_vehicle_bus1 = 0;
    big.scroll.last_tx_allowed = false;
    big.scroll.timer_armed = false;
    big.op_mode = 0;
    big.flags = 0xFFFFu;
    big.reset_reason = 15;
    big.rx_pin = -1;
    if (scrl_status_json(big, json, sizeof(json)) == 0u) return 1;
    scrl_live_pack(big, live);
    printf(",\"big_status_json\":");
    put_json_string(json);
    printf(",\"big_live_hex\":");
    put_hex(live, sizeof(live));

    printf(",\"phases\":[");
    for (int i = 0; i <= (int)ScrollAckPhase::EpisodeDone; i++) {
        if (i) putchar(',');
        put_json_string(scroll_ack_phase_name((ScrollAckPhase)i));
    }
    printf("],\"blocks\":[");
    for (int i = 0; i <= (int)ScrollAckBlock::Gap; i++) {
        if (i) putchar(',');
        put_json_string(scroll_ack_block_name((ScrollAckBlock)i));
    }
    printf("]");

    ScrlSettings defaults = scrl_settings_defaults();
    if (scrl_settings_to_json(defaults, json, sizeof(json)) == 0u) return 1;
    printf(",\"default_settings_json\":");
    put_json_string(json);
    printf(",\"limits\":{\"delay_max\":%u,\"gap_max\":%u,\"interval_min\":%u,\"interval_max\":%u,"
           "\"mode_max\":%u,\"pattern_max\":%u}",
           (unsigned)SCROLL_ACK_DELAY_MAX_MS, (unsigned)SCROLL_ACK_GAP_MAX_MS,
           (unsigned)SCROLL_ACK_INTERVAL_MIN_MS, (unsigned)SCROLL_ACK_INTERVAL_MAX_MS,
           (unsigned)SCROLL_ACK_MODE_TIMER, (unsigned)SCROLL_ACK_PATTERN_DOWN_ONCE);
    printf(",\"status_max\":%u,\"live_len\":%u}\n", (unsigned)SCRL_STATUS_JSON_MAX,
           (unsigned)SCRL_LIVE_RECORD_LEN);
    return 0;
}

static bool read_line(std::string *out) {
    out->clear();
    int c;
    while ((c = getchar()) != EOF && c != '\n') out->push_back((char)c);
    return c != EOF || !out->empty();
}

// The board holds `current`. A phone writes `write`. Print what the board does.
static int parse_write(void) {
    std::string current, write;
    if (!read_line(&current) || !read_line(&write)) return 2;
    ScrlSettings s = scrl_settings_defaults();
    if (scrl_settings_from_json(current.data(), current.size(), &s) != ScrlSettingsParse::Ok) {
        fprintf(stderr, "the first line is not a valid settings JSON\n");
        return 2;
    }
    bool named = false;
    ScrlSettingsParse result = scrl_settings_from_json(write.data(), write.size(), &s, &named);
    char json[160];
    if (scrl_settings_to_json(s, json, sizeof(json)) == 0u) return 1;
    printf("{\"result\":%d,\"op_mode_named\":%s,\"settings\":%s}\n", (int)result,
           named ? "true" : "false", json);
    return 0;
}

// A phone writes these bytes to the Command characteristic. Same steps as
// write_command() in scrl_ble.cpp for SCRL_CMD_SET_PASSKEY.
static int passkey_write(void) {
    std::string hex;
    if (!read_line(&hex) || hex.size() % 2u != 0u) return 2;
    std::string bytes;
    for (size_t i = 0; i < hex.size(); i += 2u) {
        unsigned v = 0;
        if (sscanf(hex.substr(i, 2).c_str(), "%2x", &v) != 1) return 2;
        bytes.push_back((char)v);
    }
    uint32_t value = 0;
    bool ok = bytes.size() >= 1u && (uint8_t)bytes[0] == SCRL_CMD_SET_PASSKEY &&
              scrl_passkey_from_digits((const uint8_t *)bytes.data() + 1, bytes.size() - 1u, &value);
    printf("{\"ok\":%s,\"passkey\":%u}\n", ok ? "true" : "false", (unsigned)(ok ? value : 0u));
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "parse") == 0) return parse_write();
    if (argc > 1 && strcmp(argv[1], "passkey") == 0) return passkey_write();
    return print_fixtures();
}
