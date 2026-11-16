/*
 * test_scrl_status.cpp — host tests for the status packers and the event log
 * ring of the BLE board (ble-board/main/scrl_status.h, scrl_log.h).
 * Build + run: make -C test check
 */

#include <stdio.h>
#include <string.h>

#include "scrl_log.h"
#include "scrl_sample.h"
#include "scrl_status.h"

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

static void test_status_json(void) {
    ScrlStatus st = sample_status();
    char json[SCRL_STATUS_JSON_MAX + 1u];
    size_t n = scrl_status_json(st, json, sizeof(json));
    CHECK(n > 0 && n == strlen(json), "status JSON returns its length");
    CHECK(json[0] == '{' && json[n - 1] == '}', "one object");
    CHECK(strstr(json, "\"scroll_ack_sent_count\":16909060,") != nullptr, "sent count: %s", json);
    // The diagnostics block is the same text that the T-2CAN dashboard reads.
    char diag[256];
    scroll_ack_format_diagnostics_json(diag, sizeof(diag), &st.scroll);
    CHECK(strstr(json, diag) != nullptr, "the T-2CAN diagnostics block is inside");
    CHECK(strstr(json, "\"phase\":\"SecondPending\"") != nullptr, "phase name");
    CHECK(strstr(json, "\"op\":1,\"flags\":1061,") != nullptr, "op mode and flags: %s", json);
    CHECK(strstr(json, "\"rx\":[1000,10,20,0,30,40,50,1],") != nullptr, "rx counts: %s", json);
    CHECK(strstr(json, "\"tx\":[8,2,3],") != nullptr, "tx counts: %s", json);
    CHECK(strstr(json, "\"boff\":1,\"rst\":1,\"pwr\":42,\"up\":3600,\"rxpin\":2}") != nullptr,
          "tail: %s", json);

    st.rx_pin = -1;
    n = scrl_status_json(st, json, sizeof(json));
    CHECK(n > 0 && strstr(json, "\"rxpin\":-1}") != nullptr, "controller off gives -1");

    char small[64];
    CHECK(scrl_status_json(st, small, sizeof(small)) == 0, "a small buffer returns 0");
}

// One ATT attribute holds 512 bytes. The longest possible status must fit.
static void test_status_json_worst_case(void) {
    ScrlStatus st;
    memset(&st, 0xFF, sizeof(st));
    // The longest names: phase "SecondPending" (13), block "das_hands_on" (12).
    st.scroll.phase = ScrollAckPhase::SecondPending;
    st.scroll.last_block = ScrollAckBlock::DasHandsOn;
    st.scroll.last_vehicle_bus1 = 1;      // "can0" (6 with quotes) is longer than null
    st.scroll.last_tx_allowed = false;    // "false" is longer than "true"
    st.scroll.timer_armed = false;
    st.op_mode = 1;
    st.flags = 0xFFFFu;
    st.reset_reason = 0xFFu;
    st.rx_pin = -128;
    char json[SCRL_STATUS_JSON_MAX + 1u];
    size_t n = scrl_status_json(st, json, sizeof(json));
    CHECK(n > 0, "the longest status fits in %u bytes", (unsigned)SCRL_STATUS_JSON_MAX);
    CHECK(n <= SCRL_STATUS_JSON_MAX, "length %u", (unsigned)n);
    printf("  longest status JSON: %u of %u bytes\n", (unsigned)n, (unsigned)SCRL_STATUS_JSON_MAX);

    // Every block name must be at most as long as the one used above.
    for (int b = 0; b <= (int)ScrollAckBlock::Gap; b++) {
        CHECK(strlen(scroll_ack_block_name((ScrollAckBlock)b)) <= strlen("das_hands_on"),
              "block name %d is longer than the worst case", b);
    }
}

static void test_live_record(void) {
    ScrlStatus st = sample_status();
    uint8_t rec[SCRL_LIVE_RECORD_LEN + 1u];
    memset(rec, 0xAA, sizeof(rec));
    scrl_live_pack(st, rec);
    const uint8_t want[SCRL_LIVE_RECORD_LEN] = {
        0x02,                    // phase SecondPending
        0x0E,                    // block Gap
        0x06, 0x03,              // AP state, hands-on
        0x25,                    // flags low byte: TX allowed, Active, enabled
        0x04, 0x03, 0x02, 0x01,  // sent count
        0x07, 0x00,              // pairs aborted
        0x02, 0x00,              // native age
        0x28, 0x00,              // DAS age
        0x00, 0x00, 0x00, 0x00,  // feed mV, board current
    };
    CHECK(memcmp(rec, want, sizeof(want)) == 0, "exact live record bytes");
    CHECK(rec[SCRL_LIVE_RECORD_LEN] == 0xAA, "the packer writes 19 bytes, not more");
    CHECK(SCRL_LIVE_RECORD_LEN <= 23u - 3u, "the record fits the minimum ATT MTU");

    st.scroll.pairs_aborted = 70000;
    st.scroll.last_native_age_ms = 0x12345678u;
    st.scroll.last_das_age_ms = 65536;
    st.flags = 0xFF00u | SCRL_FLAG_WRONG_BUS;
    scrl_live_pack(st, rec);
    CHECK(rec[9] == 0xFF && rec[10] == 0xFF, "pairs aborted stops at 65535");
    CHECK(rec[11] == 0xFF && rec[12] == 0xFF, "native age stops at 65535");
    CHECK(rec[13] == 0xFF && rec[14] == 0xFF, "DAS age stops at 65535");
    CHECK(rec[4] == SCRL_FLAG_WRONG_BUS, "only the low flag byte goes into the record");
}

static void test_flag_layout(void) {
    // The control app reads these bits. A change here must be a deliberate one.
    CHECK(SCRL_FLAG_TX_ALLOWED == 0x0001 && SCRL_FLAG_TIMER_ARMED == 0x0002 &&
              SCRL_FLAG_ACTIVE == 0x0004 && SCRL_FLAG_WRONG_BUS == 0x0008 &&
              SCRL_FLAG_OTA_BLIND == 0x0010, "the five design flags are bits 0-4");
    CHECK(SCRL_FLAG_ENABLED == 0x0020 && SCRL_FLAG_BUS_OFF_LOCK == 0x0040 &&
              SCRL_FLAG_BLE_OTA == 0x0080, "bits 5-7");
    CHECK(SCRL_FLAG_CAN_RUNNING == 0x4000, "highest flag fits 16 bits");
}

static void test_log_ring(void) {
    static ScrlLogRing ring;
    memset(&ring, 0, sizeof(ring));
    char line[SCRL_LOG_LINE_MAX];
    CHECK(scrl_log_ring_oldest(&ring) == 0 && !scrl_log_ring_get(&ring, 0, line, sizeof(line)),
          "an empty ring has no line");

    scrl_log_ring_add(&ring, "first");
    scrl_log_ring_add(&ring, "second");
    CHECK(ring.next_seq == 2 && scrl_log_ring_oldest(&ring) == 0, "two lines");
    CHECK(scrl_log_ring_get(&ring, 0, line, sizeof(line)) && strcmp(line, "first") == 0, "line 0");
    CHECK(scrl_log_ring_get(&ring, 1, line, sizeof(line)) && strcmp(line, "second") == 0, "line 1");
    CHECK(!scrl_log_ring_get(&ring, 2, line, sizeof(line)), "line 2 does not exist yet");

    // A long line loses its end and stays terminated.
    char longtext[200];
    memset(longtext, 'x', sizeof(longtext) - 1);
    longtext[sizeof(longtext) - 1] = '\0';
    scrl_log_ring_add(&ring, longtext);
    CHECK(scrl_log_ring_get(&ring, 2, line, sizeof(line)) &&
              strlen(line) == SCRL_LOG_LINE_MAX - 1u, "a long line is cut to %u",
          (unsigned)(SCRL_LOG_LINE_MAX - 1u));

    // A small output buffer cuts the copy.
    char tiny[4];
    CHECK(scrl_log_ring_get(&ring, 0, tiny, sizeof(tiny)) && strcmp(tiny, "fir") == 0,
          "small out buffer");
    CHECK(!scrl_log_ring_get(&ring, 0, tiny, 0), "size 0 is refused");

    // Fill past the ring size: the oldest lines go away.
    for (unsigned i = 3; i < SCRL_LOG_LINES + 10u; i++) {
        char text[24];
        snprintf(text, sizeof(text), "line %u", i);
        scrl_log_ring_add(&ring, text);
    }
    CHECK(ring.next_seq == SCRL_LOG_LINES + 10u, "sequence numbers keep counting");
    CHECK(scrl_log_ring_oldest(&ring) == 10u, "the ring holds the last %u lines",
          (unsigned)SCRL_LOG_LINES);
    CHECK(!scrl_log_ring_get(&ring, 9, line, sizeof(line)), "line 9 is gone");
    CHECK(scrl_log_ring_get(&ring, 10, line, sizeof(line)) && strcmp(line, "line 10") == 0,
          "line 10 is the oldest: %s", line);
    CHECK(scrl_log_ring_get(&ring, SCRL_LOG_LINES + 9u, line, sizeof(line)) &&
              strcmp(line, "line 73") == 0, "newest line: %s", line);
}

int main(void) {
    printf("test_scrl_status\n");
    test_status_json();
    test_status_json_worst_case();
    test_live_record();
    test_flag_layout();
    test_log_ring();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
