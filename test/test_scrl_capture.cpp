/*
 * test_scrl_capture.cpp — host tests for the capture core of the BLE board
 * (ble-board/main/scrl_capture.h): start rules, ID table, stream filter, ring,
 * packets, and a load case at 2000 frames/s with an iPhone MTU (185).
 * Build + run: make -C test check
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scrl_capture.h"

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

static ScrlCapture g_cap;  // 40 KB: not on the stack

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void feed(uint32_t id, uint32_t t, uint8_t fill, uint8_t dlc = 8, bool ext = false) {
    uint8_t d[8];
    memset(d, fill, sizeof(d));
    scrl_cap_feed(&g_cap, id, ext, dlc, d, t);
}

static void test_start_rules(void) {
    uint8_t ids[2 * (SCRL_CAP_LIST_MAX + 1)] = {0};
    CHECK(!scrl_cap_start(&g_cap, 3, nullptr, 0), "mode 3 refused");
    CHECK(!scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, ids, 2), "plan mode takes no list");
    CHECK(!scrl_cap_start(&g_cap, SCRL_CAP_MODE_LIST, ids, 0), "empty list refused");
    CHECK(!scrl_cap_start(&g_cap, SCRL_CAP_MODE_LIST, ids, 3), "odd list length refused");
    CHECK(!scrl_cap_start(&g_cap, SCRL_CAP_MODE_LIST, ids, sizeof(ids)), "list above the maximum refused");
    uint8_t big[2] = {0x00, 0x08};  // 0x800
    CHECK(!scrl_cap_start(&g_cap, SCRL_CAP_MODE_LIST, big, 2), "ID 0x800 refused");
    CHECK(!g_cap.running.load(), "a refused start leaves it stopped");
    uint8_t one[4] = {0x55, 0x01, 0xC2, 0x03};  // 0x155, 0x3C2
    CHECK(scrl_cap_start(&g_cap, SCRL_CAP_MODE_LIST, one, 4), "list of 2 IDs");
    CHECK(scrl_cap_streams(&g_cap, 0x155) && scrl_cap_streams(&g_cap, 0x3C2), "listed IDs stream");
    CHECK(!scrl_cap_streams(&g_cap, 0x118), "other IDs do not stream");
    CHECK(scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, nullptr, 0), "plan mode");
    for (size_t i = 0; i < SCRL_CAP_PLAN_COUNT; i++) {
        CHECK(scrl_cap_streams(&g_cap, SCRL_CAP_PLAN_IDS[i]), "plan ID 0x%03X streams", SCRL_CAP_PLAN_IDS[i]);
    }
    CHECK(SCRL_CAP_PLAN_COUNT == 27u, "27 plan IDs: %u", (unsigned)SCRL_CAP_PLAN_COUNT);
    CHECK(!scrl_cap_streams(&g_cap, 0x155), "a non-plan ID does not stream");
    CHECK(scrl_cap_start(&g_cap, SCRL_CAP_MODE_ALL, nullptr, 0), "all mode");
    CHECK(scrl_cap_streams(&g_cap, 0x000) && scrl_cap_streams(&g_cap, 0x7FF), "all IDs stream");
}

static void test_feed_and_packets(void) {
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, nullptr, 0);
    feed(0x3C2, 100, 0xAA);
    feed(0x155, 101, 0xBB);           // ID table only
    feed(0x3C2, 200, 0xCC, 3);        // short frame
    feed(0x18DAF100, 150, 0x11, 8, true);  // extended: counted only
    CHECK(g_cap.rx_all.load() == 4u, "rx_all %u", (unsigned)g_cap.rx_all.load());
    CHECK(g_cap.ext.load() == 1u, "ext 1");
    CHECK(g_cap.id_count.load() == 2u, "2 IDs in the table");
    CHECK(scrl_cap_pending(&g_cap) == 2u, "2 records in the ring");

    uint8_t pkt[512];
    uint8_t n = 0;
    CHECK(scrl_cap_pack_frames(&g_cap, pkt, 23, &n) == 0u, "no room below one record");
    size_t len = scrl_cap_pack_frames(&g_cap, pkt, 182, &n);
    CHECK(len == SCRL_CAP_HDR_LEN + 2u * SCRL_CAP_REC_LEN && n == 2u, "2 records: len %u", (unsigned)len);
    CHECK(pkt[0] == SCRL_CAP_FRAMES && pkt[1] == 2u && get16(&pkt[2]) == 0u && get32(&pkt[4]) == 0u, "header");
    const uint8_t *r = &pkt[8];
    CHECK(get32(&r[0]) == 100u && get16(&r[4]) == 0x3C2 && r[6] == 8u && r[7] == 0u && r[8] == 0xAA && r[15] == 0xAA,
          "record 1");
    r = &pkt[24];
    CHECK(get32(&r[0]) == 200u && r[6] == 3u && r[8] == 0xCC && r[10] == 0xCC && r[11] == 0u && r[15] == 0u,
          "record 2: data after the DLC is 0");
    CHECK(scrl_cap_pending(&g_cap) == 2u, "records stay until commit");
    scrl_cap_commit(&g_cap, n);
    CHECK(scrl_cap_pending(&g_cap) == 0u && g_cap.seq == 1u, "commit frees them, seq 1");

    uint32_t next = 0;
    len = scrl_cap_pack_ids(&g_cap, 0, pkt, 182, &next);
    CHECK(len == SCRL_CAP_HDR_LEN + 2u * SCRL_CAP_ID_REC_LEN && next == 2u, "ID table packet");
    CHECK(pkt[0] == SCRL_CAP_IDS && get16(&pkt[2]) == 1u, "ID header, seq 1");
    const uint8_t *e = &pkt[8];
    CHECK(get16(&e[0]) == 0x3C2 && e[2] == 3u && get32(&e[4]) == 2u && get32(&e[8]) == 100u &&
              get32(&e[12]) == 200u && e[16] == 0xCC && e[19] == 0u,
          "ID 0x3C2: 2 frames, 100-200 ms, last data");
    e = &pkt[32];
    CHECK(get16(&e[0]) == 0x155 && get32(&e[4]) == 1u, "ID 0x155");
    CHECK(scrl_cap_pack_ids(&g_cap, 2, pkt, 182, &next) == 0u, "the walk ends");
    scrl_cap_sent(&g_cap);

    len = scrl_cap_pack_mark(&g_cap, 7, 12345, pkt);
    CHECK(len == 16u && pkt[0] == SCRL_CAP_MARK && get32(&pkt[8]) == 12345u && pkt[12] == 7u, "mark");
    len = scrl_cap_pack_state(&g_cap, 999, 42, pkt);
    CHECK(len == 28u && pkt[0] == SCRL_CAP_STATE && get32(&pkt[8]) == 999u && pkt[12] == 1u && pkt[13] == 0u &&
              pkt[14] == 2u && get32(&pkt[16]) == 4u && get32(&pkt[20]) == 42u && get32(&pkt[24]) == 1u,
          "state");
    scrl_cap_stop(&g_cap);
    feed(0x3C2, 300, 0x01);
    CHECK(g_cap.rx_all.load() == 4u, "no feed after stop");
}

static void test_ring_full(void) {
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_ALL, nullptr, 0);
    for (uint32_t i = 0; i < SCRL_CAP_RING + 5u; i++) feed(0x100, i, (uint8_t)i);
    CHECK(scrl_cap_pending(&g_cap) == SCRL_CAP_RING, "ring full");
    CHECK(g_cap.ring_lost.load() == 5u, "5 lost");
    uint8_t pkt[512];
    uint8_t n = 0;
    // Free one packet, then the next record carries the lost flag.
    scrl_cap_pack_frames(&g_cap, pkt, 182, &n);
    scrl_cap_commit(&g_cap, n);
    feed(0x100, 9999, 0x77);
    uint32_t last = g_cap.head.load() - 1u;
    const uint8_t *r = g_cap.ring[last & (SCRL_CAP_RING - 1u)];
    CHECK(r[7] == 1u && get32(&r[0]) == 9999u, "the first record after a loss is flagged");
    feed(0x100, 10000, 0x78);
    r = g_cap.ring[(g_cap.head.load() - 1u) & (SCRL_CAP_RING - 1u)];
    CHECK(r[7] == 0u, "the next one is not");
    CHECK(get32(&pkt[4]) == 5u, "the header carries ring_lost");
    // The ID table stops at its size and the stream goes on.
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_ALL, nullptr, 0);
    for (uint32_t id = 0; id < 300u; id++) feed(id, id, 0);
    CHECK(g_cap.id_count.load() == SCRL_CAP_MAX_IDS, "table holds %u IDs", (unsigned)SCRL_CAP_MAX_IDS);
    CHECK(scrl_cap_pending(&g_cap) == 300u, "all 300 frames stream");
}

// A bus of 2000 frames/s for 20 s: 100 IDs, the 27 plan IDs among them. The
// sender runs every 10 ms and gets `per_s` notifications per second through.
static void load_case(uint32_t per_s, bool expect_no_loss) {
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, nullptr, 0);
    uint16_t bus_ids[100];
    for (int i = 0; i < 100; i++) bus_ids[i] = (uint16_t)(i < 27 ? SCRL_CAP_PLAN_IDS[i] : 0x400 + i);
    uint32_t streamed = 0, got = 0, packets = 0;
    uint16_t want_seq = 0;
    bool seq_ok = true;
    uint32_t budget = 0;  // notifications (x100) that the link can take now
    uint8_t pkt[182];
    for (uint32_t t = 0; t < 20000u; t++) {
        for (int k = 0; k < 2; k++) {  // 2 frames per ms
            uint16_t id = bus_ids[(t * 2u + (uint32_t)k) % 100u];
            if (scrl_cap_streams(&g_cap, id)) streamed++;
            feed(id, t, (uint8_t)t);
        }
        budget += per_s;  // per_s/1000 packets per ms, in units of 1/1000
        if (t % 10u == 0u) {
            for (int i = 0; i < 8 && budget >= 1000u; i++) {
                uint8_t n = 0;
                size_t len = scrl_cap_pack_frames(&g_cap, pkt, sizeof(pkt), &n);
                if (!len) break;
                if (get16(&pkt[2]) != want_seq) seq_ok = false;
                scrl_cap_commit(&g_cap, n);
                want_seq++;
                got += n;
                packets++;
                budget -= 1000u;
            }
            if (budget > 8000u) budget = 8000u;
        }
    }
    uint32_t lost = g_cap.ring_lost.load();
    uint32_t left = scrl_cap_pending(&g_cap);
    printf("  load %u notifications/s: %u streamed (%.0f/s), %u sent in %u packets, %u lost, %u left\n",
           (unsigned)per_s, (unsigned)streamed, streamed / 20.0, (unsigned)got, (unsigned)packets,
           (unsigned)lost, (unsigned)left);
    CHECK(got + lost + left == streamed, "every streamed frame is sent, lost or waiting");
    CHECK(seq_ok, "seq has no gap");
    if (expect_no_loss) CHECK(lost == 0u, "no loss at %u notifications/s", (unsigned)per_s);
    else CHECK(lost > 0u, "loss shows at %u notifications/s", (unsigned)per_s);
}

static ScrlCapSender g_send;

// One call of the sender: the packet type, or 0 for nothing. sent = the notify worked.
static uint8_t next_type(uint32_t now, bool sent = true) {
    uint8_t pkt[182];
    size_t len = scrl_cap_next(&g_cap, &g_send, pkt, sizeof(pkt), now, 0);
    if (!len) return 0;
    if (sent) scrl_cap_next_sent(&g_cap, &g_send, now);
    return pkt[0];
}

static void test_sender_order(void) {
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, nullptr, 0);
    scrl_cap_sender_reset(&g_send, 1000);
    g_send.active.store(true);
    for (uint32_t i = 0; i < 40; i++) feed(i % 2 ? 0x3C2 : 0x118, 1000 + i, 0);  // 40 records, 2 IDs
    feed(0x155, 1000, 0);
    CHECK(next_type(1000) == SCRL_CAP_STATE, "the state goes first");
    CHECK(next_type(1000) == SCRL_CAP_FRAMES, "then frames");
    CHECK(next_type(1000) == SCRL_CAP_IDS, "the first ID table takes its turn");
    CHECK(next_type(1000) == SCRL_CAP_FRAMES, "frames again (the table fits one packet)");
    CHECK(next_type(1000, false) == SCRL_CAP_FRAMES && next_type(1000) == SCRL_CAP_FRAMES,
          "a packet that did not go out comes again");
    CHECK(scrl_cap_mark_push(&g_send, 4, 1100), "mark queued");
    CHECK(next_type(1100) == SCRL_CAP_MARK, "a mark goes before everything");
    while (scrl_cap_pending(&g_cap)) CHECK(next_type(1200) == SCRL_CAP_FRAMES, "frames drain");
    CHECK(next_type(1300) == 0, "nothing waits");
    CHECK(next_type(2000) == SCRL_CAP_STATE, "the state once per second");
    feed(0x118, 6100, 0);
    CHECK(next_type(6100) == SCRL_CAP_STATE, "state at 6.1 s");
    CHECK(next_type(6100) == SCRL_CAP_IDS, "a new walk after 5 s (the last packet was frames)");
    CHECK(next_type(6100) == SCRL_CAP_FRAMES, "then frames");
    feed(0x118, 6200, 0);
    scrl_cap_stop(&g_cap);
    CHECK(next_type(6300) == SCRL_CAP_FRAMES, "after the stop: the rest of the ring");
    CHECK(next_type(6300) == SCRL_CAP_IDS, "then the last ID table");
    CHECK(next_type(6300) == SCRL_CAP_STATE, "then the last state");
    CHECK(g_send.active.load(), "still active until the next call");
    CHECK(next_type(6300) == 0 && !g_send.active.load(), "then the sender is done");
    for (int i = 0; i < 8; i++) scrl_cap_mark_push(&g_send, (uint8_t)i, 0);
    CHECK(!scrl_cap_mark_push(&g_send, 9, 0), "the mark queue holds 8");
    // A drop clears the ring and ends the capture.
    scrl_cap_start(&g_cap, SCRL_CAP_MODE_PLAN, nullptr, 0);
    scrl_cap_sender_reset(&g_send, 0);
    g_send.active.store(true);
    feed(0x118, 1, 0);
    scrl_cap_stop(&g_cap);
    scrl_cap_drop(&g_cap, &g_send);
    CHECK(scrl_cap_pending(&g_cap) == 0u && !g_send.active.load(), "drop");
}

int main(void) {
    printf("test_scrl_capture\n");
    test_start_rules();
    test_feed_and_packets();
    test_ring_full();
    test_sender_order();
    load_case(80, true);   // 27 of 100 IDs at 2000/s = 540 frames/s = 54 packets/s at MTU 185
    load_case(40, false);  // a slow link: the loss counter shows it
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
