/*
 * test_scroll_ack.cpp — host unit tests for esp32/.firmware/scroll_ack.h.
 *
 * The Scroll Ack state machine is Arduino-independent and header-only, so the
 * exact trigger values, output bytes, freshness gates, primary-only routing,
 * idle-source validation, one-pair limit, rearm, cancellation and send-failure
 * paths all get host coverage here. No ESP32 or car is needed.
 *
 * Build + run:  make -C test check
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "scroll_ack.h"

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

// The captured native idle page-1 frame: 29 55 00 00 00 00 00 80.
static const uint8_t k_idle[8] = {0x29, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};

// A fresh native 0x3C2 page-1 idle frame.
static CanFrame idle_frame(void) {
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x3C2u;
    f.dlc = 8;
    memcpy(f.data, k_idle, 8);
    return f;
}

// Fully valid input for a first tick.
static ScrollAckInput good_input(void) {
    ScrollAckInput in;
    in.enabled     = true;
    in.tx_allowed  = true;
    in.primary_bus = true;
    in.fresh_native = true;
    in.hw4         = true;
    in.ap_state    = 6;
    in.hands_on    = 3;
    return in;
}

// A runtime whose DAS 0x39B source is fresh as of now_ms.
static ScrollAckRuntime fresh_runtime(uint32_t now_ms) {
    ScrollAckRuntime r;
    memset(&r, 0, sizeof(r));
    scroll_ack_note_das39b(&r, now_ms);
    return r;
}

static bool data_is(const CanFrame &f, const char *hex16) {
    char got[17];
    for (int i = 0; i < 8; i++) sprintf(got + 2 * i, "%02x", f.data[i]);
    char want[17];
    for (int i = 0; i < 16; i++) want[i] = (char)tolower((unsigned char)hex16[i]);
    want[16] = 0;
    return strcmp(got, want) == 0;
}

// ── 1. disabled ───────────────────────────────────────────────────────────────
static void test_scroll_ack_disabled(void) {
    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    in.enabled = false;
    CanFrame nat = idle_frame();
    CanFrame out;
    for (int i = 0; i < 5; i++) {
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "disabled: action must be None");
    }
}

// ── 2. the pair bytes + a second tick after hands-on clears ───────────────────
static void test_scroll_ack_pair_bytes_and_second_after_clear(void) {
    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    CanFrame nat = idle_frame();
    CanFrame out;
    memset(&out, 0, sizeof(out));

    ScrollAckAction a = scroll_ack_step(&r, in, nat, 1000, &out);
    CHECK(a == ScrollAckAction::FirstTick, "valid input -> FirstTick");
    CHECK(out.id == 0x3C2u, "first tick id 0x3C2, got 0x%03X", (unsigned)out.id);
    CHECK(out.dlc == 8, "first tick dlc 8, got %u", out.dlc);
    CHECK(data_is(out, "29553F0000000080"), "first tick bytes 29 55 3F ..");

    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(r.phase == ScrollAckPhase::SecondPending, "after first send -> SecondPending");
    CHECK(r.sent_count == 1u, "after first send -> sent_count 1, got %u", r.sent_count);

    // Hands-on changes to 1 (the first tick cleared it). The second tick must
    // still fire on the next native frame.
    in.hands_on = 1;
    memset(&out, 0, sizeof(out));
    a = scroll_ack_step(&r, in, nat, 1000, &out);
    CHECK(a == ScrollAckAction::SecondTick, "hands-on 1 still -> SecondTick");
    CHECK(data_is(out, "2955010000000080"), "second tick bytes 29 55 01 ..");

    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.phase == ScrollAckPhase::EpisodeDone, "after second send -> EpisodeDone");
    CHECK(r.sent_count == 2u, "after second send -> sent_count 2, got %u", r.sent_count);
}

// ── 3. trigger values ─────────────────────────────────────────────────────────
static void test_scroll_ack_trigger_values(void) {
    CanFrame nat = idle_frame();
    CanFrame out;
    // hands-on 0,1,2,4 block the first tick (ap 6).
    for (uint8_t ho = 0; ho <= 4; ho++) {
        if (ho == 3) continue;
        ScrollAckRuntime r = fresh_runtime(1000);
        ScrollAckInput in = good_input();
        in.hands_on = ho;
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "ap 6 hands-on %u -> no first tick", ho);
    }
    // ap 0,1,2,3,7 block the first tick (hands-on 3).
    const uint8_t aps[] = {0, 1, 2, 3, 7};
    for (unsigned i = 0; i < sizeof(aps); i++) {
        ScrollAckRuntime r = fresh_runtime(1000);
        ScrollAckInput in = good_input();
        in.ap_state = aps[i];
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "ap %u hands-on 3 -> no first tick", aps[i]);
    }
    // Only ap 6 + hands-on 3 passes.
    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick,
          "ap 6 + hands-on 3 -> FirstTick");
}

// ── 4. DAS 0x39B required and fresh ───────────────────────────────────────────
static void test_scroll_ack_das_required_and_fresh(void) {
    CanFrame nat = idle_frame();
    CanFrame out;
    ScrollAckInput in = good_input();

    // Never seen a 0x39B: no first tick (0x399-only hands-on is not enough).
    ScrollAckRuntime r;
    memset(&r, 0, sizeof(r));
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
          "unseen 0x39B -> no first tick");

    // Seen at t=0. Age 1000 ms passes.
    r = fresh_runtime(0);
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick,
          "0x39B age 1000 ms -> FirstTick");

    // Age 1001 ms blocks.
    r = fresh_runtime(0);
    CHECK(scroll_ack_step(&r, in, nat, 1001, &out) == ScrollAckAction::None,
          "0x39B age 1001 ms -> no first tick");
}

// ── 5. primary-only + native freshness ────────────────────────────────────────
static void test_scroll_ack_primary_and_native_freshness(void) {
    CanFrame nat = idle_frame();
    CanFrame out;

    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    in.primary_bus = false;
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
          "secondary bus -> no output");

    r = fresh_runtime(1000);
    in = good_input();
    in.fresh_native = false;
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
          "stale native -> no output");
}

// ── 6. idle-source validation ─────────────────────────────────────────────────
static void test_scroll_ack_idle_validation(void) {
    ScrollAckInput in = good_input();
    const uint8_t sentinel = 0xEE;

    struct { const char *name; int byte; uint8_t val; } bad[] = {
        {"page index 0", 0, 0x28},          // byte0 low 2 bits -> 0 (page 0)
        {"left tick nonzero", 2, 0x05},     // byte2 bits 0-5
        {"right tick nonzero", 3, 0x05},    // byte3 bits 0-5
        {"left pressed", 0, 0x09},          // byte0 bits 5-6 -> 0 (pressed), keep page 1
        {"right pressed", 1, 0x45},         // byte1 bits 4-5 -> 0
        {"left tilt L", 1, 0x15},           // byte1 bits 6-7 -> 0
        {"left tilt R", 0, 0x21},           // byte0 bits 3-4 -> 0
        {"right tilt L", 1, 0x54},          // byte1 bits 0-1 -> 0
        {"right tilt R", 1, 0x51},          // byte1 bits 2-3 -> 0
        {"left double", 5, 0x02},           // byte5 bit 1
        {"right double", 5, 0x04},          // byte5 bit 2
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ScrollAckRuntime r = fresh_runtime(1000);
        CanFrame nat = idle_frame();
        nat.data[bad[i].byte] = bad[i].val;
        CanFrame out;
        memset(&out, sentinel, sizeof(out));
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "%s -> no output", bad[i].name);
        CHECK(out.data[0] == sentinel, "%s -> out left unchanged", bad[i].name);
    }

    // Wrong DLC.
    {
        ScrollAckRuntime r = fresh_runtime(1000);
        CanFrame nat = idle_frame();
        nat.dlc = 7;
        CanFrame out;
        memset(&out, sentinel, sizeof(out));
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "dlc 7 -> no output");
        CHECK(out.data[0] == sentinel, "dlc 7 -> out left unchanged");
    }
}

// ── 7. one pair per episode + rearm ───────────────────────────────────────────
static void test_scroll_ack_one_pair_and_rearm(void) {
    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    CanFrame nat = idle_frame();
    CanFrame out;

    // Pair, with hands-on held at 3 the whole time.
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick, "first");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::SecondTick, "second");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);

    // Repeated native frames while hands-on stays 3: no third tick.
    for (int i = 0; i < 5; i++) {
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "hands-on stays 3 -> no third tick");
    }
    CHECK(r.sent_count == 2u, "still only one pair sent, got %u", r.sent_count);

    // Hands-on 1 rearms; a return to 3 permits one new pair.
    in.hands_on = 1;
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
          "rearm at hands-on 1 -> no tick yet");
    in.hands_on = 3;
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick,
          "return to 3 -> new first tick");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::SecondTick,
          "new second tick");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.sent_count == 4u, "two pairs total -> sent_count 4, got %u", r.sent_count);
}

// ── 8. send failure + pending cancel ──────────────────────────────────────────
static void test_scroll_ack_send_failure_and_pending_cancel(void) {
    CanFrame nat = idle_frame();
    CanFrame out;

    // A failed first send ends the episode.
    {
        ScrollAckRuntime r = fresh_runtime(1000);
        ScrollAckInput in = good_input();
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick, "first");
        scroll_ack_complete(&r, ScrollAckAction::FirstTick, false);
        CHECK(r.phase == ScrollAckPhase::EpisodeDone, "failed first -> EpisodeDone");
        CHECK(r.sent_count == 0u, "failed first -> no count");
    }
    // A failed second send ends the episode.
    {
        ScrollAckRuntime r = fresh_runtime(1000);
        ScrollAckInput in = good_input();
        scroll_ack_step(&r, in, nat, 1000, &out);
        scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::SecondTick, "second");
        scroll_ack_complete(&r, ScrollAckAction::SecondTick, false);
        CHECK(r.phase == ScrollAckPhase::EpisodeDone, "failed second -> EpisodeDone");
        CHECK(r.sent_count == 1u, "failed second -> count stays 1");
    }

    // Each lost gate cancels a pending second action.
    struct { const char *name; void (*mut)(ScrollAckInput *, CanFrame *, uint32_t *); } cancels[] = {
        {"feature disable", [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->enabled = false; }},
        {"tx denial",       [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->tx_allowed = false; }},
        {"non-HW4",         [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->hw4 = false; }},
        {"ap not 6",        [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->ap_state = 2; }},
        {"stale DAS",       [](ScrollAckInput *, CanFrame *, uint32_t *now) { *now = 3000; }},
        {"non-primary",     [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->primary_bus = false; }},
        {"stale native",    [](ScrollAckInput *in, CanFrame *, uint32_t *) { in->fresh_native = false; }},
        {"non-idle source", [](ScrollAckInput *, CanFrame *nat2, uint32_t *) { nat2->data[2] = 0x05; }},
    };
    for (unsigned i = 0; i < sizeof(cancels) / sizeof(cancels[0]); i++) {
        ScrollAckRuntime r = fresh_runtime(1000);
        ScrollAckInput in = good_input();
        CanFrame nat2 = idle_frame();
        uint32_t now = 1000;
        scroll_ack_step(&r, in, nat2, now, &out);
        scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
        CHECK(r.phase == ScrollAckPhase::SecondPending, "%s: pending set up", cancels[i].name);
        cancels[i].mut(&in, &nat2, &now);
        ScrollAckAction a = scroll_ack_step(&r, in, nat2, now, &out);
        CHECK(a == ScrollAckAction::None, "%s: pending cancel -> None", cancels[i].name);
        CHECK(r.phase == ScrollAckPhase::EpisodeDone, "%s: pending cancel -> EpisodeDone",
              cancels[i].name);
        CHECK(r.sent_count == 1u, "%s: cancel keeps count 1", cancels[i].name);
    }
}

// ── 9. the counter counts only successful sends ───────────────────────────────
static void test_scroll_ack_counter_counts_only_success(void) {
    ScrollAckRuntime r = fresh_runtime(1000);
    ScrollAckInput in = good_input();
    CanFrame nat = idle_frame();
    CanFrame out;

    scroll_ack_step(&r, in, nat, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, false);
    CHECK(r.sent_count == 0u, "failed first -> count 0, got %u", r.sent_count);

    // New episode after a rearm, this time both sends succeed.
    in.hands_on = 1;
    scroll_ack_step(&r, in, nat, 1000, &out);
    in.hands_on = 3;
    scroll_ack_step(&r, in, nat, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    scroll_ack_step(&r, in, nat, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.sent_count == 2u, "one good pair -> count 2, got %u", r.sent_count);
}

int main(void) {
    printf("test_scroll_ack: Scroll Ack state machine host tests\n");
    test_scroll_ack_disabled();
    test_scroll_ack_pair_bytes_and_second_after_clear();
    test_scroll_ack_trigger_values();
    test_scroll_ack_das_required_and_fresh();
    test_scroll_ack_primary_and_native_freshness();
    test_scroll_ack_idle_validation();
    test_scroll_ack_one_pair_and_rearm();
    test_scroll_ack_send_failure_and_pending_cancel();
    test_scroll_ack_counter_counts_only_success();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
