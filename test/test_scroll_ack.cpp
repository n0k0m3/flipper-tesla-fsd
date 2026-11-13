/*
 * test_scroll_ack.cpp — host tests for the Scroll Ack state machine (Prompt + Timer).
 * Build + run: make -C test check
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

static const uint8_t k_idle[8] = {0x29, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};

static CanFrame idle_frame(uint32_t rx_ms) {
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = CAN_ID_VCLEFT_SWITCH;
    f.dlc = 8;
    f.rx_ms = rx_ms;
    memcpy(f.data, k_idle, 8);
    return f;
}

static ScrollAckInput good_input(void) {
    ScrollAckInput in;
    in.enabled = true;
    in.tx_allowed = true;
    in.bus = CAN_BUS_PRIMARY;
    in.fresh_native = true;
    in.hw4 = true;
    in.mode = SCROLL_ACK_MODE_PROMPT;
    in.pattern = SCROLL_ACK_PATTERN_DOWN_UP;
    in.delay_mean_ms = 0;      // tick on the first prompt frame
    in.gap_mean_ms = 0;        // second tick on the next frame
    in.interval_mean_ms = 2500;
    in.rand = 0;  // shortest draws: Timer interval 2000 ms, so timing is exact
    return in;
}

static ScrollAckInput timer_input(void) {
    ScrollAckInput in = good_input();
    in.mode = SCROLL_ACK_MODE_TIMER;
    return in;
}

static ScrollAckRuntime blank_runtime(void) {
    ScrollAckRuntime r;
    memset(&r, 0, sizeof(r));
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

// Feed one native frame at `now` with fresh DAS (AP state `ap`, hands-on `ho`).
static ScrollAckAction feed(ScrollAckRuntime *r, const ScrollAckInput &in, uint32_t now,
                            CanFrame *out, uint8_t ap = 6, uint8_t ho = 3) {
    scroll_ack_note_das39b(r, now, ap, ho);
    CanFrame nat = idle_frame(now);
    return scroll_ack_step(r, in, nat, now, out);
}

static void test_disabled(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.enabled = false;
    CanFrame out;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::None, "disabled -> no tick");
    CHECK(r.last_block == ScrollAckBlock::Disabled, "disabled reason recorded");
}

static void test_pair_bytes_on_prompt(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    memset(&out, 0, sizeof(out));

    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "hands-on 3 -> -1");
    CHECK(out.id == CAN_ID_VCLEFT_SWITCH && out.dlc == 8, "tick keeps id and DLC");
    CHECK(data_is(out, "29553f0000000080"), "first tick is exactly left -1");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(r.phase == ScrollAckPhase::SecondPending, "sent -1 waits for +1");

    // The -1 clears the prompt: hands-on drops to 1 before the next frame.
    CHECK(feed(&r, in, 1100, &out, 6, 1) == ScrollAckAction::SecondTick,
          "next frame -> +1 even after hands-on left 3");
    CHECK(data_is(out, "2955010000000080"), "second tick is exactly left +1");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.phase == ScrollAckPhase::EpisodeDone, "pair done");
    CHECK(r.sent_count == 2u, "pair counts two ticks, got %u", r.sent_count);
}

static void test_trigger_values(void) {
    CanFrame out;
    for (uint8_t ho = 0; ho <= 4; ho++) {
        if (ho == 3) continue;
        ScrollAckRuntime r = blank_runtime();
        ScrollAckInput in = good_input();
        CHECK(feed(&r, in, 1000, &out, 6, ho) == ScrollAckAction::None,
              "AP 6 hands-on %u -> no tick", ho);
        CHECK(r.last_block == ScrollAckBlock::DasHandsOn, "hands-on reason");
    }
    const uint8_t states[] = {0, 1, 2, 3, 5, 7};
    for (unsigned i = 0; i < sizeof(states); i++) {
        ScrollAckRuntime r = blank_runtime();
        ScrollAckInput in = good_input();
        CHECK(feed(&r, in, 1000, &out, states[i], 3) == ScrollAckAction::None,
              "AP state %u hands-on 3 -> no tick", states[i]);
        CHECK(r.last_block == ScrollAckBlock::DasApState, "AP state reason");
    }
}

static void test_one_pair_per_prompt_and_rearm(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    feed(&r, in, 1100, &out);
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    for (uint32_t t = 1200; t <= 5000; t += 100) {
        CHECK(feed(&r, in, t, &out) == ScrollAckAction::None,
              "hands-on still 3 at %u -> no more ticks", t);
    }
    CHECK(r.last_block == ScrollAckBlock::EpisodeDone, "episode-done reason");
    CHECK(feed(&r, in, 5100, &out, 6, 1) == ScrollAckAction::None, "hands-on 1 rearms only");
    CHECK(r.phase == ScrollAckPhase::Armed, "rearmed");
    CHECK(feed(&r, in, 5200, &out) == ScrollAckAction::FirstTick, "new prompt -> new pair");
}

static void test_das_required_and_fresh(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    CanFrame nat = idle_frame(1000);
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
          "unseen DAS -> no tick");
    CHECK(r.last_block == ScrollAckBlock::DasFresh, "unseen DAS reason");

    r = blank_runtime();
    scroll_ack_note_das39b(&r, 0, 6, 3);
    nat = idle_frame(1000);
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::FirstTick,
          "DAS age 1000 ms -> tick");
    r = blank_runtime();
    scroll_ack_note_das39b(&r, 0, 6, 3);
    nat = idle_frame(1001);
    CHECK(scroll_ack_step(&r, in, nat, 1001, &out) == ScrollAckAction::None,
          "DAS age 1001 ms -> no tick");
}

static void test_das_ingest_any_bus(void) {
    ScrollAckRuntime r = blank_runtime();
    CanFrame das;
    memset(&das, 0, sizeof(das));
    das.id = CAN_ID_DAS_STATUS_HW4;
    das.dlc = 7;
    das.rx_ms = 900;
    scroll_ack_note_hw4_das(&r, CAN_BUS_PRIMARY, das, 6, 3);
    CHECK(!r.das39b_seen, "DLC 7 DAS -> no stamp");
    das.dlc = 8;
    scroll_ack_note_hw4_das(&r, CAN_BUS_SECONDARY, das, 6, 3);
    CHECK(r.das39b_seen, "secondary-bus DAS stamps");
    CHECK(r.das39b_rx_ms == 900u, "DAS uses receive time");
    CHECK(r.das39b_ap_state == 6u && r.das39b_hands_on == 3u, "DAS snapshot latched");
}

static void test_vehicle_bus_rule(void) {
    ScrollAckRuntime r = blank_runtime();
    CHECK(scroll_ack_is_vehicle_bus(&r, CAN_BUS_PRIMARY, 1000), "no EPAS seen -> Vehicle");
    scroll_ack_note_epas(&r, CAN_BUS_SECONDARY, 1000);
    CHECK(!scroll_ack_is_vehicle_bus(&r, CAN_BUS_SECONDARY, 1000), "EPAS bus is not Vehicle");
    CHECK(!scroll_ack_is_vehicle_bus(&r, CAN_BUS_SECONDARY, 3000), "EPAS 2000 ms ago still blocks");
    CHECK(scroll_ack_is_vehicle_bus(&r, CAN_BUS_SECONDARY, 3001), "EPAS gone > 2000 ms -> Vehicle");
    CHECK(scroll_ack_is_vehicle_bus(&r, CAN_BUS_PRIMARY, 1000), "other bus unaffected");
}

// Pre-repin wiring: can0 = Vehicle, can1 = Chassis (0x370 + forwarded 0x3C2).
// The Chassis copy must never get a tick and must not disturb the episode.
static void test_chassis_copy_ignored(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput veh = good_input();
    ScrollAckInput chs = good_input();
    chs.bus = CAN_BUS_SECONDARY;
    CanFrame out;
    scroll_ack_note_epas(&r, CAN_BUS_SECONDARY, 1000);
    CHECK(feed(&r, chs, 1000, &out) == ScrollAckAction::None, "Chassis copy first -> no tick");
    CHECK(r.phase == ScrollAckPhase::Armed && r.last_vehicle_bus1 == 0u,
          "Chassis copy leaves no trace");
    CHECK(feed(&r, veh, 1002, &out) == ScrollAckAction::FirstTick, "Vehicle frame -> -1");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    scroll_ack_note_epas(&r, CAN_BUS_SECONDARY, 1050);
    CHECK(feed(&r, chs, 1052, &out) == ScrollAckAction::None, "Chassis copy cannot take +1");
    CHECK(r.phase == ScrollAckPhase::SecondPending, "pair still pending");
    CHECK(feed(&r, veh, 1100, &out) == ScrollAckAction::SecondTick, "+1 on Vehicle");
    CHECK(r.last_vehicle_bus1 == 1u, "diagnostic shows can0");
}

// Post-repin wiring: can0 = Party (0x370, no 0x3C2), can1 = Vehicle.
static void test_vehicle_on_secondary(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.bus = CAN_BUS_SECONDARY;
    CanFrame out;
    scroll_ack_note_epas(&r, CAN_BUS_PRIMARY, 1000);
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "can1 Vehicle ticks");
}

static void test_native_gates(void) {
    CanFrame out;
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.fresh_native = false;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::None, "stale native -> no tick");
    CHECK(r.last_block == ScrollAckBlock::NativeFresh, "stale native reason");

    r = blank_runtime();
    in = good_input();
    in.tx_allowed = false;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::None, "TX denied -> no tick");
    CHECK(r.last_block == ScrollAckBlock::TxDenied, "TX denied reason");

    r = blank_runtime();
    in = good_input();
    in.hw4 = false;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::None, "not HW4 -> no tick");
    CHECK(r.last_block == ScrollAckBlock::NotHW4, "not HW4 reason");
    in.hw4 = true;
    CHECK(feed(&r, in, 1100, &out) == ScrollAckAction::FirstTick, "gate back -> tick");
}

static void test_idle_validation(void) {
    ScrollAckInput in = good_input();
    struct { const char *name; int byte; uint8_t val; } bad[] = {
        {"left tick", 2, 0x05}, {"right tick", 3, 0x05},
        {"left pressed", 0, 0x09}, {"right pressed", 1, 0x45},
        {"left tilt L", 1, 0x15}, {"left tilt R", 0, 0x21},
        {"right tilt L", 1, 0x54}, {"right tilt R", 1, 0x51},
        {"left double", 5, 0x02}, {"right double", 5, 0x04},
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ScrollAckRuntime r = blank_runtime();
        CanFrame out;
        scroll_ack_note_das39b(&r, 1000, 6, 3);
        CanFrame nat = idle_frame(1000);
        nat.data[bad[i].byte] = bad[i].val;
        CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None,
              "%s -> no tick", bad[i].name);
        CHECK(r.last_block == ScrollAckBlock::NativeIdle, "%s reason", bad[i].name);
        CHECK(feed(&r, in, 1100, &out) == ScrollAckAction::FirstTick,
              "%s: prompt pair fires on the next idle frame", bad[i].name);
    }
    ScrollAckRuntime r = blank_runtime();
    CanFrame out;
    scroll_ack_note_das39b(&r, 1000, 6, 3);
    CanFrame nat = idle_frame(1000);
    nat.data[0] = 0x28;  // page 0
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None, "page 0 -> no tick");
    nat = idle_frame(1000);
    nat.dlc = 7;
    CHECK(scroll_ack_step(&r, in, nat, 1000, &out) == ScrollAckAction::None, "DLC 7 -> no tick");
}

static void test_pair_abort(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    scroll_ack_note_das39b(&r, 1100, 6, 1);
    CanFrame nat = idle_frame(1100);
    nat.data[3] = 0x01;  // driver turns the right wheel
    CHECK(scroll_ack_step(&r, in, nat, 1100, &out) == ScrollAckAction::None,
          "non-idle source: second tick waits");
    CHECK(r.phase == ScrollAckPhase::SecondPending, "pair still pending");
    CHECK(r.last_block == ScrollAckBlock::NativeIdle, "waiting reason is native_idle");
    CHECK(feed(&r, in, 1200, &out, 6, 1) == ScrollAckAction::SecondTick,
          "second tick on the next idle frame");

    // No idle frame for more than 2 s past due ends the pair.
    r = blank_runtime();
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    for (uint32_t t = 1100; t <= 3000; t += 100) {
        scroll_ack_note_das39b(&r, t, 6, 1);
        CanFrame busy = idle_frame(t);
        busy.data[3] = 0x01;
        CHECK(scroll_ack_step(&r, in, busy, t, &out) == ScrollAckAction::None, "busy %u", t);
    }
    CHECK(r.phase == ScrollAckPhase::SecondPending, "still pending at timeout edge");
    scroll_ack_note_das39b(&r, 3100, 6, 1);
    CanFrame late = idle_frame(3100);
    late.data[3] = 0x01;
    CHECK(scroll_ack_step(&r, in, late, 3100, &out) == ScrollAckAction::None, "timeout");
    CHECK(r.phase == ScrollAckPhase::EpisodeDone, "timeout ends the episode");
    CHECK(r.pairs_aborted == 1u, "abort counted");
    CHECK(r.last_block == ScrollAckBlock::PairAborted, "abort reason");

    r = blank_runtime();
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(feed(&r, in, 1100, &out, 2, 3) == ScrollAckAction::None, "AP drop ends the pair");
    CHECK(r.pairs_aborted == 1u && r.phase == ScrollAckPhase::EpisodeDone,
          "AP drop counts abort");
}

static void test_send_failures(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "-1 decided");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, false);
    CHECK(r.phase == ScrollAckPhase::EpisodeDone && r.sent_count == 0u,
          "failed -1: no +1, no count, no retry");
    CHECK(feed(&r, in, 1100, &out) == ScrollAckAction::None, "no retry while hands-on 3");

    r = blank_runtime();
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(feed(&r, in, 1100, &out) == ScrollAckAction::SecondTick, "+1 decided");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, false);
    CHECK(r.phase == ScrollAckPhase::EpisodeDone && r.pairs_aborted == 1u,
          "failed +1 counts as aborted pair");
    CHECK(r.sent_count == 1u, "only the sent -1 counts, got %u", r.sent_count);
}

static void test_switch_reset_and_generation(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    CanFrame out;
    FSDState state;
    memset(&state, 0, sizeof(state));
    state.scroll_ack = true;

    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "in-flight -1 decided");
    uint32_t generation = r.episode_generation;
    scroll_ack_apply_enabled(&state, &r, false);
    CHECK(!state.scroll_ack, "switch OFF updates state");
    CHECK(r.phase == ScrollAckPhase::Armed, "switch OFF resets episode");
    CHECK(r.das39b_seen, "switch reset preserves DAS history");
    scroll_ack_apply_enabled(&state, &r, true);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true, generation, state.scroll_ack);
    CHECK(r.sent_count == 1u, "stale completion counts the actual send");
    CHECK(r.phase == ScrollAckPhase::Armed, "stale completion cannot set SecondPending");
}

static void test_diagnostics_json(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.bus = CAN_BUS_SECONDARY;
    CanFrame out;
    char json[512];
    CHECK(scroll_ack_format_diagnostics_json(json, sizeof(json), &r) > 0, "blank JSON");
    CHECK(strstr(json, "\"vehicle_bus\":null") != NULL, "no Vehicle bus yet");

    scroll_ack_note_das39b(&r, 1000, 6, 2);
    CanFrame nat = idle_frame(950);
    scroll_ack_step(&r, in, nat, 1000, &out);
    CHECK(scroll_ack_format_diagnostics_json(json, sizeof(json), &r) > 0, "JSON formatted");
    CHECK(strstr(json, "\"scroll_ack_diag\":{") != NULL, "diagnostic object");
    CHECK(strstr(json, "\"phase\":\"Armed\"") != NULL, "phase name");
    CHECK(strstr(json, "\"block\":\"das_hands_on\"") != NULL, "block name");
    CHECK(strstr(json, "\"native_age_ms\":50") != NULL, "native age JSON");
    CHECK(strstr(json, "\"das_ap_state\":6") != NULL, "AP state JSON");
    CHECK(strstr(json, "\"das_hands_on\":2") != NULL, "hands-on JSON");
    CHECK(strstr(json, "\"vehicle_bus\":\"can1\"") != NULL, "Vehicle bus JSON");
    CHECK(strstr(json, "\"tx_allowed\":true") != NULL, "TX gate JSON");
    CHECK(strstr(json, "\"pairs_aborted\":0") != NULL, "abort counter JSON");
    CHECK(strstr(json, "\"timer_armed\":false") != NULL, "timer flag JSON");

    char sent[64];
    CHECK(scroll_ack_format_sent_count_json(sent, sizeof(sent), 1) > 0, "counter JSON");
    CHECK(strcmp(sent, "\"scroll_ack_sent_count\":1") == 0, "counter JSON value, got %s", sent);
}


// ── Timer mode (TSL6 pattern) ────────────────────────────────────────────────

static void test_timer_first_pair_after_one_interval(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    CanFrame out;
    CHECK(feed(&r, in, 1000, &out, 6, 1) == ScrollAckAction::None, "AP start arms the timer only");
    CHECK(r.timer_armed && r.next_due_ms == 3000u, "first pair due 2000 ms later, got %u",
          r.next_due_ms);
    CHECK(r.last_block == ScrollAckBlock::Timer, "waiting reason is timer");
    CHECK(feed(&r, in, 2900, &out, 6, 1) == ScrollAckAction::None, "not due at 2900");
    CHECK(feed(&r, in, 3000, &out, 6, 1) == ScrollAckAction::FirstTick, "due at 3000 -> -1");
    CHECK(data_is(out, "29553f0000000080"), "timer first tick is left -1");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(feed(&r, in, 3100, &out, 6, 1) == ScrollAckAction::SecondTick, "next frame -> +1");
    CHECK(data_is(out, "2955010000000080"), "timer second tick is left +1");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.sent_count == 2u, "pair counts two ticks, got %u", r.sent_count);
}

static void test_timer_repeats_and_ignores_hands_on(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    CanFrame out;
    feed(&r, in, 0, &out, 6, 1);
    uint32_t prev = 0;
    int pairs = 0;
    for (uint32_t t = 100; t <= 20100; t += 100) {
        uint8_t ho = (uint8_t)((t / 100u) % 4u);  // hands-on cycles 0..3
        ScrollAckAction a = feed(&r, in, t, &out, 6, ho);
        if (a == ScrollAckAction::FirstTick) {
            if (prev) CHECK(t - prev == 2000u, "pairs 2000 ms apart, got %u", t - prev);
            prev = t;
            pairs++;
        }
        scroll_ack_complete(&r, a, true);
    }
    CHECK(pairs == 10, "10 pairs in 20 s, got %d", pairs);
    CHECK(r.sent_count == 20u, "20 ticks, got %u", r.sent_count);
}

static void test_jitter_ranges(void) {
    CHECK(scroll_ack_jitter_ms(2500, 20, 0) == 2000u, "interval min 2000");
    CHECK(scroll_ack_jitter_ms(2500, 20, 1000) == 3000u, "interval max 3000");
    CHECK(scroll_ack_jitter_ms(2500, 20, 1001) == 2000u, "interval wraps");
    uint32_t lo = 0xFFFFFFFFu, hi = 0u;
    for (uint32_t i = 0; i < 5000; i++) {
        uint32_t v = scroll_ack_jitter_ms(600, 50, scroll_ack_rand2(i * 7919u));
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    CHECK(lo == 300u && hi == 900u, "delay 600 +/-50%% spans 300-900, got %u-%u", lo, hi);
    CHECK(scroll_ack_jitter_ms(0, 50, 12345) == 0u, "mean 0 -> 0");
    CHECK(scroll_ack_jitter_ms(1, 50, 12345) == 1u, "tiny mean -> no spread");
}

// Prompt mode: random delay from the prompt to the first tick.
static void test_prompt_delay(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.delay_mean_ms = 600;
    in.rand = 0;  // shortest draw: 300 ms
    CanFrame out;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::None, "prompt starts the delay");
    CHECK(r.phase == ScrollAckPhase::Delay && r.next_due_ms == 1300u,
          "delay due at 1300, got %u", r.next_due_ms);
    CHECK(r.last_block == ScrollAckBlock::Delay, "waiting reason is delay");
    CHECK(feed(&r, in, 1200, &out) == ScrollAckAction::None, "not due at 1200");
    CHECK(r.next_due_ms == 1300u, "delay is drawn once per prompt");
    CHECK(feed(&r, in, 1300, &out) == ScrollAckAction::FirstTick, "due at 1300 -> first tick");

    // A prompt cleared during the delay sends nothing, and the next prompt redraws.
    r = blank_runtime();
    feed(&r, in, 1000, &out);
    CHECK(feed(&r, in, 1200, &out, 6, 1) == ScrollAckAction::None, "driver cleared prompt");
    CHECK(r.phase == ScrollAckPhase::Armed, "cleared prompt rearms");
    CHECK(feed(&r, in, 1300, &out, 6, 1) == ScrollAckAction::None, "no tick without prompt");
    CHECK(feed(&r, in, 5000, &out) == ScrollAckAction::None, "new prompt starts a new delay");
    CHECK(r.next_due_ms == 5300u, "new delay from the new prompt, got %u", r.next_due_ms);

    // A lost gate during the delay cancels it.
    r = blank_runtime();
    feed(&r, in, 1000, &out);
    CHECK(feed(&r, in, 1100, &out, 2, 3) == ScrollAckAction::None, "AP drop during delay");
    CHECK(r.phase == ScrollAckPhase::Armed, "AP drop cancels the delay");
}

// Random gap between the two ticks.
static void test_gap(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = good_input();
    in.gap_mean_ms = 600;
    CanFrame out;
    CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "first tick");
    uint32_t gap = r.second_due_ms - 1000u;
    CHECK(gap >= 300u && gap <= 900u, "gap 600 +/-50%% drawn, got %u", gap);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    uint32_t t = 1100;
    for (; t < 1000u + gap; t += 100) {
        CHECK(feed(&r, in, t, &out, 6, 1) == ScrollAckAction::None, "gap not over at %u", t);
        CHECK(r.last_block == ScrollAckBlock::Gap, "waiting reason is gap");
    }
    CHECK(feed(&r, in, t, &out, 6, 1) == ScrollAckAction::SecondTick, "second tick after the gap");
}

static void test_patterns(void) {
    struct { uint8_t pat; const char *first; const char *second; } cases[] = {
        {SCROLL_ACK_PATTERN_DOWN_UP,   "29553f0000000080", "2955010000000080"},
        {SCROLL_ACK_PATTERN_UP_DOWN,   "2955010000000080", "29553f0000000080"},
        {SCROLL_ACK_PATTERN_UP_ONCE,   "2955010000000080", NULL},
        {SCROLL_ACK_PATTERN_DOWN_ONCE, "29553f0000000080", NULL},
        {9,                            "29553f0000000080", "2955010000000080"},  // unknown = default
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ScrollAckRuntime r = blank_runtime();
        ScrollAckInput in = good_input();
        in.pattern = cases[i].pat;
        CanFrame out;
        CHECK(feed(&r, in, 1000, &out) == ScrollAckAction::FirstTick, "pattern %u first", cases[i].pat);
        CHECK(data_is(out, cases[i].first), "pattern %u first bytes", cases[i].pat);
        scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
        if (cases[i].second) {
            CHECK(r.phase == ScrollAckPhase::SecondPending, "pattern %u pends", cases[i].pat);
            CHECK(feed(&r, in, 1100, &out, 6, 1) == ScrollAckAction::SecondTick,
                  "pattern %u second", cases[i].pat);
            CHECK(data_is(out, cases[i].second), "pattern %u second bytes", cases[i].pat);
        } else {
            CHECK(r.phase == ScrollAckPhase::EpisodeDone, "pattern %u single tick done", cases[i].pat);
            CHECK(feed(&r, in, 1100, &out) == ScrollAckAction::None,
                  "pattern %u no more ticks this prompt", cases[i].pat);
            CHECK(r.pairs_aborted == 0u, "single tick is not an abort");
        }
    }
    // Timer mode with a single tick repeats every interval.
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    in.pattern = SCROLL_ACK_PATTERN_UP_ONCE;
    CanFrame out;
    int ticks = 0;
    for (uint32_t t = 0; t <= 10000; t += 100) {
        ScrollAckAction a = feed(&r, in, t, &out, 6, 1);
        if (a != ScrollAckAction::None) ticks++;
        scroll_ack_complete(&r, a, true);
    }
    CHECK(ticks == 5, "timer single tick: 5 in 10 s, got %d", ticks);
}

static void test_settings_clamp(void) {
    FSDState st;
    memset(&st, 0, sizeof(st));
    st.scroll_ack_mode = 9;
    st.scroll_ack_pattern = 9;
    st.scroll_ack_delay_ms = 60000;
    st.scroll_ack_gap_ms = 60000;
    st.scroll_ack_interval_ms = 10;
    scroll_ack_clamp_settings(&st);
    CHECK(st.scroll_ack_mode == SCROLL_ACK_MODE_PROMPT, "mode clamp");
    CHECK(st.scroll_ack_pattern == SCROLL_ACK_PATTERN_DOWN_UP, "pattern clamp");
    CHECK(st.scroll_ack_delay_ms == SCROLL_ACK_DELAY_MAX_MS, "delay clamp");
    CHECK(st.scroll_ack_gap_ms == SCROLL_ACK_GAP_MAX_MS, "gap clamp");
    CHECK(st.scroll_ack_interval_ms == SCROLL_ACK_INTERVAL_MIN_MS, "interval low clamp");
    st.scroll_ack_interval_ms = 60000;
    scroll_ack_clamp_settings(&st);
    CHECK(st.scroll_ack_interval_ms == SCROLL_ACK_INTERVAL_MAX_MS, "interval high clamp");
}

static void test_timer_ap_drop_rearms(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    CanFrame out;
    feed(&r, in, 0, &out, 6, 1);
    feed(&r, in, 1900, &out, 2, 1);
    CHECK(!r.timer_armed, "AP drop disarms the timer");
    CHECK(feed(&r, in, 2000, &out, 6, 1) == ScrollAckAction::None, "AP back: no instant tick");
    CHECK(feed(&r, in, 4000, &out, 6, 1) == ScrollAckAction::FirstTick, "tick one interval later");
}

static void test_timer_abort_and_failures(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    CanFrame out;
    feed(&r, in, 0, &out, 6, 1);
    CHECK(feed(&r, in, 2000, &out, 6, 1) == ScrollAckAction::FirstTick, "-1 sent");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    scroll_ack_note_das39b(&r, 2100, 6, 1);
    CanFrame nat = idle_frame(2100);
    nat.data[3] = 0x01;
    CHECK(scroll_ack_step(&r, in, nat, 2100, &out) == ScrollAckAction::None, "input delays +1");
    CHECK(feed(&r, in, 2150, &out, 2, 1) == ScrollAckAction::None, "AP drop aborts pair");
    CHECK(r.pairs_aborted == 1u, "abort counted");
    CHECK(feed(&r, in, 2200, &out, 6, 1) == ScrollAckAction::None, "AP back: timer restarts");
    CHECK(feed(&r, in, 4199, &out, 6, 1) == ScrollAckAction::None, "not due before full interval");
    CHECK(feed(&r, in, 4200, &out, 6, 1) == ScrollAckAction::FirstTick, "pair one interval later");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    CHECK(feed(&r, in, 4300, &out, 6, 1) == ScrollAckAction::SecondTick, "+1");
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(feed(&r, in, 6200, &out, 6, 1) == ScrollAckAction::FirstTick, "next pair on schedule");
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, false);
    CHECK(feed(&r, in, 6300, &out, 6, 1) == ScrollAckAction::None, "failed -1: no +1, no retry");
    CHECK(feed(&r, in, 8200, &out, 6, 1) == ScrollAckAction::FirstTick, "next interval tries again");
}

static void test_timer_chassis_copy_ignored(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput veh = timer_input();
    ScrollAckInput chs = timer_input();
    chs.bus = CAN_BUS_SECONDARY;
    CanFrame out;
    int veh_ticks = 0, chs_ticks = 0;
    for (uint32_t t = 0; t <= 10100; t += 100) {
        scroll_ack_note_epas(&r, CAN_BUS_SECONDARY, t);
        ScrollAckAction a = feed(&r, veh, t, &out, 6, 1);
        if (a != ScrollAckAction::None) veh_ticks++;
        scroll_ack_complete(&r, a, true);
        if (feed(&r, chs, t + 2, &out, 6, 1) != ScrollAckAction::None) chs_ticks++;
    }
    CHECK(chs_ticks == 0, "no tick on the Chassis copy, got %d", chs_ticks);
    CHECK(veh_ticks == 10, "5 pairs on Vehicle in 10 s, got %d", veh_ticks);
}

static void test_timer_wrap_safe(void) {
    ScrollAckRuntime r = blank_runtime();
    ScrollAckInput in = timer_input();
    CanFrame out;
    uint32_t t0 = 0xFFFFFF00u;
    feed(&r, in, t0, &out, 6, 1);
    CHECK(feed(&r, in, t0 + 1900u, &out, 6, 1) == ScrollAckAction::None, "not due across wrap");
    CHECK(feed(&r, in, t0 + 2000u, &out, 6, 1) == ScrollAckAction::FirstTick, "due across wrap");
}

static void test_mode_switch(void) {
    ScrollAckRuntime r = blank_runtime();
    FSDState state;
    memset(&state, 0, sizeof(state));
    ScrollAckInput in = good_input();
    CanFrame out;
    // Prompt pair done, hands-on still 3: Prompt waits for a new prompt.
    feed(&r, in, 1000, &out);
    scroll_ack_complete(&r, ScrollAckAction::FirstTick, true);
    feed(&r, in, 1100, &out);
    scroll_ack_complete(&r, ScrollAckAction::SecondTick, true);
    CHECK(r.phase == ScrollAckPhase::EpisodeDone, "prompt episode done");

    scroll_ack_apply_mode(&state, &r, 1);
    CHECK(state.scroll_ack_mode == SCROLL_ACK_MODE_TIMER, "mode 1 -> Timer");
    CHECK(r.phase == ScrollAckPhase::Armed && !r.timer_armed, "mode change resets");
    in.mode = state.scroll_ack_mode;
    CHECK(feed(&r, in, 1200, &out) == ScrollAckAction::None, "Timer waits one interval");
    CHECK(feed(&r, in, 3200, &out) == ScrollAckAction::FirstTick, "Timer pair after interval");

    scroll_ack_apply_mode(&state, &r, 7);
    CHECK(state.scroll_ack_mode == SCROLL_ACK_MODE_PROMPT, "unknown mode -> Prompt");
    scroll_ack_apply_mode(&state, &r, -1);
    CHECK(state.scroll_ack_mode == SCROLL_ACK_MODE_PROMPT, "negative mode -> Prompt");
}

int main(void) {
    printf("test_scroll_ack: Scroll Ack (Prompt + Timer) host tests\n");
    test_disabled();
    test_pair_bytes_on_prompt();
    test_trigger_values();
    test_one_pair_per_prompt_and_rearm();
    test_das_required_and_fresh();
    test_das_ingest_any_bus();
    test_vehicle_bus_rule();
    test_chassis_copy_ignored();
    test_vehicle_on_secondary();
    test_native_gates();
    test_idle_validation();
    test_pair_abort();
    test_send_failures();
    test_switch_reset_and_generation();
    test_diagnostics_json();
    test_timer_first_pair_after_one_interval();
    test_timer_repeats_and_ignores_hands_on();
    test_jitter_ranges();
    test_prompt_delay();
    test_gap();
    test_patterns();
    test_settings_clamp();
    test_timer_ap_drop_rearms();
    test_timer_abort_and_failures();
    test_timer_chassis_copy_ignored();
    test_timer_wrap_safe();
    test_mode_switch();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
