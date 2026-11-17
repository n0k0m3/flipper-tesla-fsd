/*
 * test_scrl_scroll_ext.cpp — host tests for the BLE board additions to Scroll
 * Ack (ble-board/main/scrl_scroll_ext.h): tick order, wheel, fixed interval.
 * Build + run: make -C test check
 */

#include <stdio.h>
#include <string.h>

#include "scrl_scroll_ext.h"

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

static ScrollAckInput input(uint8_t mode) {
    ScrollAckInput in;
    in.enabled = true;
    in.tx_allowed = true;
    in.bus = CAN_BUS_PRIMARY;
    in.fresh_native = true;
    in.hw4 = true;
    in.mode = mode;
    in.pattern = SCROLL_ACK_PATTERN_DOWN_UP;
    in.delay_mean_ms = 0;
    in.gap_mean_ms = 0;
    in.interval_mean_ms = 2500;
    in.rand = 0;
    return in;
}

// One board step, the same order as scrl_proc_handle_frame(). Returns the action
// and the frame that would go out. Every send succeeds.
struct Board {
    ScrollAckRuntime rt;
    ScrlScrollExt ext;
    uint8_t pattern;
    uint8_t lr;
    bool rand_on;
};

static ScrollAckAction board_step(Board *b, ScrollAckInput in, uint32_t now, CanFrame *out,
                                  uint8_t hands_on = 3) {
    scroll_ack_note_das39b(&b->rt, now, 6, hands_on);
    CanFrame nat = idle_frame(now);
    in.pattern = scrl_ext_pattern(&b->ext, b->pattern, in.rand);
    bool armed_before = b->rt.timer_armed;
    ScrollAckAction act = scroll_ack_step(&b->rt, in, nat, now, out);
    if (!b->rand_on) {
        scrl_ext_fix_interval(&b->rt, armed_before, act, in.mode, in.interval_mean_ms, now);
    }
    if (act == ScrollAckAction::None) return act;
    scrl_ext_place_tick(&b->ext, b->lr, act, nat, out);
    scroll_ack_complete(&b->rt, act, true);
    if (act == ScrollAckAction::FirstTick) scrl_ext_first_sent(&b->ext, *out);
    return act;
}

static Board new_board(uint8_t pattern, uint8_t lr, bool rand_on = true) {
    Board b;
    memset(&b, 0, sizeof(b));
    b.pattern = pattern;
    b.lr = lr;
    b.rand_on = rand_on;
    return b;
}

// Run one Prompt-mode pair (prompt, first tick, second tick, prompt cleared).
// Writes the two frames. Returns false when the pair did not go out.
static bool prompt_pair(Board *b, uint32_t *now, CanFrame *first, CanFrame *second) {
    ScrollAckInput in = input(SCROLL_ACK_MODE_PROMPT);
    if (board_step(b, in, *now, first) != ScrollAckAction::FirstTick) return false;
    *now += 50;
    if (board_step(b, in, *now, second) != ScrollAckAction::SecondTick) return false;
    *now += 50;
    CanFrame scratch;
    board_step(b, in, *now, &scratch, 1);  // prompt gone: the episode rearms
    *now += 50;
    return true;
}

static void test_left_is_unchanged(void) {
    Board b = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_LEFT);
    uint32_t now = 1000;
    CanFrame f1, f2;
    CHECK(prompt_pair(&b, &now, &f1, &f2), "pair sent");
    CHECK(f1.data[2] == 0x3F && f1.data[3] == 0x00, "left -1, right untouched");
    CHECK(f2.data[2] == 0x01 && f2.data[3] == 0x00, "left +1");
    for (int i = 0; i < 8; i++) {
        if (i == 2) continue;
        CHECK(f1.data[i] == k_idle[i] && f2.data[i] == k_idle[i], "byte %d is the native byte", i);
    }
}

static void test_right_wheel(void) {
    Board b = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_RIGHT);
    uint32_t now = 1000;
    CanFrame f1, f2;
    CHECK(prompt_pair(&b, &now, &f1, &f2), "pair sent");
    CHECK(f1.data[2] == 0x00 && f1.data[3] == 0x3F, "right -1, left idle: %02x %02x",
          f1.data[2], f1.data[3]);
    CHECK(f2.data[2] == 0x00 && f2.data[3] == 0x01, "right +1");

    // The high bits of bytes 2 and 3 stay native.
    CanFrame nat = idle_frame(0);
    nat.data[2] = 0xC0;
    nat.data[3] = 0x80;
    CanFrame out = nat;
    out.data[2] = (uint8_t)(0xC0 | 0x3F);
    scrl_ext_to_right(nat, &out);
    CHECK(out.data[2] == 0xC0 && out.data[3] == (0x80 | 0x3F), "high bits kept: %02x %02x",
          out.data[2], out.data[3]);
}

static void test_alternate_wheel(void) {
    Board b = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_ALTERNATE);
    uint32_t now = 1000;
    bool want_right = false;  // left first
    for (int i = 0; i < 6; i++) {
        CanFrame f1, f2;
        CHECK(prompt_pair(&b, &now, &f1, &f2), "pair %d sent", i);
        bool right = (f1.data[3] & 0x3F) != 0;
        CHECK(right == want_right, "pair %d wheel %s", i, right ? "right" : "left");
        bool right2 = (f2.data[3] & 0x3F) != 0;
        CHECK(right2 == right, "pair %d second tick on the same wheel", i);
        want_right = !want_right;
    }
}

static void test_alternate_order(void) {
    Board b = new_board(SCRL_PATTERN_ALTERNATE, SCRL_LR_LEFT);
    uint32_t now = 1000;
    uint8_t want = 0x3F;  // -1 first, the same as the default
    for (int i = 0; i < 6; i++) {
        CanFrame f1, f2;
        CHECK(prompt_pair(&b, &now, &f1, &f2), "pair %d sent", i);
        CHECK(f1.data[2] == want, "pair %d first tick %02x, want %02x", i, f1.data[2], want);
        CHECK((uint8_t)((f1.data[2] + f2.data[2]) & 0x3F) == 0, "pair %d is balanced", i);
        want = want == 0x3F ? 0x01 : 0x3F;
    }
    // Alternate with the right wheel follows the right tick field.
    Board r = new_board(SCRL_PATTERN_ALTERNATE, SCRL_LR_RIGHT);
    CanFrame f1, f2;
    now = 1000;
    CHECK(prompt_pair(&r, &now, &f1, &f2) && f1.data[3] == 0x3F, "right pair 1 starts -1");
    CHECK(prompt_pair(&r, &now, &f1, &f2) && f1.data[3] == 0x01, "right pair 2 starts +1");
}

static void test_random_order(void) {
    ScrlScrollExt ext;
    memset(&ext, 0, sizeof(ext));
    int up = 0;
    for (uint32_t r = 0; r < 4096; r++) {
        uint8_t p = scrl_ext_pattern(&ext, SCRL_PATTERN_RANDOM, r * 2654435761u);
        CHECK(p == SCROLL_ACK_PATTERN_DOWN_UP || p == SCROLL_ACK_PATTERN_UP_DOWN, "a pair pattern");
        if (p == SCROLL_ACK_PATTERN_UP_DOWN) up++;
    }
    CHECK(up > 1500 && up < 2600, "both directions come up: %d of 4096 start +1", up);
    // The T-2CAN patterns pass through.
    for (uint8_t p = 0; p <= SCROLL_ACK_PATTERN_DOWN_ONCE; p++) {
        CHECK(scrl_ext_pattern(&ext, p, 12345u) == p, "pattern %u unchanged", p);
    }
}

static void test_single_tick_right(void) {
    Board b = new_board(SCROLL_ACK_PATTERN_UP_ONCE, SCRL_LR_RIGHT);
    CanFrame f;
    ScrollAckInput in = input(SCROLL_ACK_MODE_PROMPT);
    CHECK(board_step(&b, in, 1000, &f) == ScrollAckAction::FirstTick, "single tick");
    CHECK(f.data[2] == 0x00 && f.data[3] == 0x01, "right +1 only");
    CHECK(board_step(&b, in, 1050, &f) == ScrollAckAction::None, "no second tick");
}

// Timer mode: time of each first tick, with frames every 10 ms.
static int timer_firsts(Board *b, uint32_t rand, uint32_t *times, int max, uint32_t until) {
    int n = 0;
    ScrollAckInput in = input(SCROLL_ACK_MODE_TIMER);
    in.rand = rand;
    for (uint32_t now = 1000; now < until && n < max; now += 10) {
        CanFrame out;
        if (board_step(b, in, now, &out) == ScrollAckAction::FirstTick) times[n++] = now;
    }
    return n;
}

static void test_fixed_interval(void) {
    // rand 0: exactly the mean, whatever the random value.
    Board b = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_LEFT, false);
    uint32_t t[8];
    int n = timer_firsts(&b, 0xFFFFFFFFu, t, 8, 20000);
    CHECK(n >= 5, "%d pairs in 19 s", n);
    CHECK(n > 0 && t[0] == 1000 + 2500, "first pair one interval after arming: %u", n > 0 ? t[0] : 0);
    for (int i = 1; i < n; i++) CHECK(t[i] - t[i - 1] == 2500, "interval %d = %u", i, t[i] - t[i - 1]);

    // rand 1 with the largest draw: mean + 20 %.
    Board r = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_LEFT, true);
    uint32_t big = 1000;  // 1000 % 1001 = 1000 -> 2000 + 1000
    n = timer_firsts(&r, big, t, 8, 20000);
    CHECK(n > 0 && t[0] == 1000 + 3000, "random draw kept with rand 1: %u", n > 0 ? t[0] : 0);

    // Prompt mode is not changed by rand 0.
    Board p = new_board(SCROLL_ACK_PATTERN_DOWN_UP, SCRL_LR_LEFT, false);
    ScrollAckInput in = input(SCROLL_ACK_MODE_PROMPT);
    in.delay_mean_ms = 1000;
    in.rand = 0;  // shortest delay: 500 ms
    CanFrame out;
    uint32_t first = 0;
    for (uint32_t now = 1000; now < 3000 && first == 0; now += 10) {
        if (board_step(&p, in, now, &out) == ScrollAckAction::FirstTick) first = now;
    }
    CHECK(first == 1500, "prompt delay still random: %u", first);
}

int main(void) {
    printf("test_scrl_scroll_ext\n");
    test_left_is_unchanged();
    test_right_wheel();
    test_alternate_wheel();
    test_alternate_order();
    test_random_order();
    test_single_tick_right();
    test_fixed_interval();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
