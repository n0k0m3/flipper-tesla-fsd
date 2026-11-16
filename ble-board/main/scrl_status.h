#pragma once
/*
 * scrl_status.h — packers for the BLE Status (JSON) and Live (19 bytes)
 * characteristics. Header-only and free of ESP-IDF, so the host test covers
 * them (test/test_scrl_status.cpp).
 *
 * STATUS JSON (one long read, at most SCRL_STATUS_JSON_MAX bytes):
 *   {"scroll_ack_sent_count":N,"scroll_ack_diag":{...},   same text as the T-2CAN
 *    "op":0|1,            op mode setting (0 Listen-Only, 1 Active)
 *    "flags":N,           SCRL_FLAG_* bits
 *    "rx":[all,0x3C2,0x39B,0x370,0x257,0x318,0x129,dropped],   frame counts
 *    "tx":[ok,failed,refused],
 *    "boff":N,            bus-off events since power-on
 *    "rst":N,             reset reason (esp_reset_reason_t)
 *    "pwr":N,             power-on count
 *    "up":N,              uptime, seconds
 *    "rxpin":N}           GPIO that receives CAN, -1 = controller off
 * Feed voltage and board current are not in it: no board build measures them.
 *
 * LIVE RECORD (little-endian):
 *   0  phase            ScrollAckPhase
 *   1  block            ScrollAckBlock
 *   2  AP state         last 0x39B
 *   3  hands-on state   last 0x39B
 *   4  flags            low byte of the SCRL_FLAG_* bits
 *   5  sent count       u32
 *   9  pairs aborted    u16, stops at 65535
 *   11 native age, ms   u16, stops at 65535
 *   13 DAS age, ms      u16, stops at 65535
 *   15 feed, mV         u16, always 0: no board build measures it
 *   17 board current    u16, always 0: no board build measures it
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "scroll_ack.h"

// The longest value that one ATT attribute can hold.
#define SCRL_STATUS_JSON_MAX 512u
#define SCRL_LIVE_RECORD_LEN 19u

// The low byte goes into the Live record.
#define SCRL_FLAG_TX_ALLOWED    (1u << 0)   // every TX gate is open now
#define SCRL_FLAG_TIMER_ARMED   (1u << 1)   // Timer mode: the interval runs
#define SCRL_FLAG_ACTIVE        (1u << 2)   // effective op mode is Active
#define SCRL_FLAG_WRONG_BUS     (1u << 3)   // 0x370 seen: this is not Vehicle CAN
#define SCRL_FLAG_OTA_BLIND     (1u << 4)   // no 0x318: the Tesla OTA gate cannot close
#define SCRL_FLAG_ENABLED       (1u << 5)   // Scroll Ack is on
#define SCRL_FLAG_BUS_OFF_LOCK  (1u << 6)   // 3 bus-off events: locked to Listen-Only
#define SCRL_FLAG_BLE_OTA       (1u << 7)   // a BLE update runs
#define SCRL_FLAG_AUTOPARK      (1u << 8)   // Autopark TX pause
#define SCRL_FLAG_TESLA_OTA     (1u << 9)   // Tesla OTA TX pause
#define SCRL_FLAG_HW4_SEEN      (1u << 10)  // a valid 0x39B was seen
#define SCRL_FLAG_IMAGE_PENDING (1u << 11)  // new image runs, not confirmed yet
#define SCRL_FLAG_PINS_CHECKED  (1u << 12)  // the build names the CAN pins
#define SCRL_FLAG_PAIRING_OPEN  (1u << 13)  // the board accepts a new bond now
#define SCRL_FLAG_CAN_RUNNING   (1u << 14)  // the CAN controller is started

struct ScrlStatus {
    ScrollAckRuntime scroll;  // snapshot of the Scroll Ack state
    uint8_t op_mode;          // the setting, not the effective mode
    uint16_t flags;           // SCRL_FLAG_*
    uint32_t rx_all;
    uint32_t rx_3c2;
    uint32_t rx_39b;
    uint32_t rx_370;
    uint32_t rx_257;
    uint32_t rx_318;
    uint32_t rx_129;
    uint32_t rx_dropped;
    uint32_t tx_ok;
    uint32_t tx_failed;
    uint32_t tx_refused;
    uint32_t bus_off_count;
    uint8_t reset_reason;
    uint32_t power_on_count;
    uint32_t uptime_s;
    int8_t rx_pin;
};

/**
 * Write the Status JSON. Returns the length, or 0 when out is too small.
 * A buffer of SCRL_STATUS_JSON_MAX + 1 bytes is always large enough.
 */
static inline size_t scrl_status_json(const ScrlStatus &st, char *out, size_t size) {
    char diag[256];
    int d = scroll_ack_format_diagnostics_json(diag, sizeof(diag), &st.scroll);
    if (d <= 0 || (size_t)d >= sizeof(diag)) return 0u;
    int n = snprintf(out, size,
                     "{\"scroll_ack_sent_count\":%u,%s,\"op\":%u,\"flags\":%u,"
                     "\"rx\":[%u,%u,%u,%u,%u,%u,%u,%u],\"tx\":[%u,%u,%u],"
                     "\"boff\":%u,\"rst\":%u,\"pwr\":%u,\"up\":%u,\"rxpin\":%d}",
                     (unsigned)st.scroll.sent_count, diag, (unsigned)st.op_mode,
                     (unsigned)st.flags,
                     (unsigned)st.rx_all, (unsigned)st.rx_3c2, (unsigned)st.rx_39b,
                     (unsigned)st.rx_370, (unsigned)st.rx_257, (unsigned)st.rx_318,
                     (unsigned)st.rx_129, (unsigned)st.rx_dropped,
                     (unsigned)st.tx_ok, (unsigned)st.tx_failed, (unsigned)st.tx_refused,
                     (unsigned)st.bus_off_count, (unsigned)st.reset_reason,
                     (unsigned)st.power_on_count, (unsigned)st.uptime_s, (int)st.rx_pin);
    return (n > 0 && (size_t)n < size) ? (size_t)n : 0u;
}

static inline void scrl_put_u16(uint8_t *p, uint32_t v) {
    if (v > 0xFFFFu) v = 0xFFFFu;
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static inline void scrl_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

/** Write the Live record into out[SCRL_LIVE_RECORD_LEN]. */
static inline void scrl_live_pack(const ScrlStatus &st, uint8_t *out) {
    out[0] = (uint8_t)st.scroll.phase;
    out[1] = (uint8_t)st.scroll.last_block;
    out[2] = st.scroll.das39b_ap_state;
    out[3] = st.scroll.das39b_hands_on;
    out[4] = (uint8_t)(st.flags & 0xFFu);
    scrl_put_u32(&out[5], st.scroll.sent_count);
    scrl_put_u16(&out[9], st.scroll.pairs_aborted);
    scrl_put_u16(&out[11], st.scroll.last_native_age_ms);
    scrl_put_u16(&out[13], st.scroll.last_das_age_ms);
    scrl_put_u16(&out[15], 0u);  // feed mV: not measured
    scrl_put_u16(&out[17], 0u);  // board current: not measured
}
