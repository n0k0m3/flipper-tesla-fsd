/*
 * scrl_proc.cpp — frame logic. See scrl_proc.h.
 *
 * The order of the steps follows process_frame() in esp32/.firmware/main.cpp:
 * count, bus rule, gate upkeep from the last parsed DAS and speed values, then
 * the parser for this frame's ID, then the Scroll Ack block for 0x3C2.
 */

#include "scrl_proc.h"

#include <atomic>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "scrl_board.h"
#include "scrl_can.h"
#include "scrl_logbuf.h"
#include "scrl_scroll_ext.h"
#include "scroll_ack.h"
#include "../../fsd_logic/fsd_can_ops.h"    // tesla_can_rx_accept
#include "../../fsd_logic/fsd_freshness.h"  // fsd_tx_fresh

// DI_speed. The firmware config.h leaves this ID to fsd_logic/fsd_handler.h,
// which this build does not include (its name collides with the firmware header).
#define SCRL_CAN_ID_DI_SPEED 0x257u
// The board has one bus. scroll_ack.h calls it can0.
#define SCRL_BUS CAN_BUS_PRIMARY
// The Tesla OTA gate reads 0x318. With traffic for this long and no 0x318, the
// gate cannot close: report it.
#define OTA_BLIND_AFTER_MS 5000u
// No frame for this long: the bus is silent (bench, or the car sleeps).
#define BUS_SILENT_MS 2000u
// DI_vehicleSpeed raw value for 0 km/h (kph = raw * 0.08 - 40), and the band
// around it that counts as standstill (0.5 km/h).
#define SPEED_RAW_ZERO 500
#define SPEED_RAW_STILL_BAND 6

static SemaphoreHandle_t s_lock;
static FSDState s_state;
static ScrollAckRuntime s_scroll;
static ScrlScrollExt s_scroll_ext;
static ScrlSettings s_settings;
static uint32_t s_inhibit;
static std::atomic<bool> s_active;
static std::atomic<uint32_t> s_settings_gen;

static uint32_t s_rx_3c2, s_rx_39b, s_rx_370, s_rx_257, s_rx_318, s_rx_129;
static uint32_t s_first_rx_ms;  // 0 = no frame yet
static uint32_t s_last_rx_ms;

// In an unnamed namespace, with its own name: scrl_proc.cpp and scrl_ota.cpp
// each had a file-scope "struct Lock" with inline members. The linker kept
// one destructor for both, so a proc lock gave the mutex of the other file,
// and the next lock in the same task blocked for ever.
namespace {
struct ProcLock {
    ProcLock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~ProcLock() { xSemaphoreGive(s_lock); }
    ProcLock(const ProcLock &) = delete;
    ProcLock &operator=(const ProcLock &) = delete;
};
}  // namespace

// Effective op mode = the setting, unless something inhibits Active.
static void update_op_mode(void) {
    OpMode mode = (s_settings.op_mode == (uint8_t)OpMode_Active && s_inhibit == 0u)
                      ? OpMode_Active : OpMode_ListenOnly;
    if (s_state.op_mode != mode) {
        s_state.op_mode = mode;
        // A pair that waits for its second tick must not go on across a mode change.
        scroll_ack_reset_episode(&s_scroll);
    }
    s_active.store(mode == OpMode_Active);
}

void scrl_proc_init(const ScrlSettings &settings, uint32_t inhibit) {
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    // This car has HW4. 0x39B is HW4-only, and the Scroll Ack gate also needs a
    // valid 0x39B before it sends.
    fsd_state_init(&s_state, TeslaHW_HW4);
    // fsd_state_init turns these on for the T-2CAN. This board has no code path
    // for them, but keep the state honest.
    s_state.nag_killer = false;
    s_state.suppress_speed_chime = false;
    memset(&s_scroll, 0, sizeof(s_scroll));
    memset(&s_scroll_ext, 0, sizeof(s_scroll_ext));
    s_settings = settings;
    scrl_settings_clamp(&s_settings);
    scrl_settings_to_state(s_settings, &s_state);
    s_inhibit = inhibit;
    update_op_mode();
}

void scrl_proc_handle_frame(const CanFrame &frame) {
    uint32_t now = scrl_millis();
    // True when a modified copy of this frame still tracks the car's live frame.
    bool fresh = fsd_tx_fresh(frame.rx_ms, now);
    // Extended and remote frames, and DLC above 8, are counted and not parsed.
    bool usable = tesla_can_rx_accept(frame.id, frame.ext != 0u, frame.req != 0u, frame.dlc);

    ProcLock lock;
    s_state.rx_count++;
    if (s_first_rx_ms == 0u) s_first_rx_ms = now == 0u ? 1u : now;
    s_last_rx_ms = now;

    // Bus rule: a bus that carries 0x370 (EPAS) is not Vehicle CAN.
    if (usable && frame.id == CAN_ID_EPAS_STATUS) {
        scroll_ack_note_epas(&s_scroll, SCRL_BUS, now);
        s_rx_370++;
    }

    // Gate upkeep, from the values of the last parsed DAS and speed frames.
    if (s_state.das_ap_state < DAS_APSTATE_ENGAGED) {
        s_state.ap_unstable_tick_ms = now;
        s_state.soft_engage_latched = false;
        s_state.ap_inject_count = 0;
    }
    fsd_abort_guard_update(&s_state);
    bool autopark_before = s_state.autopark_tx_block;
    fsd_autopark_update(&s_state, now);
    if (!autopark_before && s_state.autopark_tx_block) {
        scrl_log("AUTOPARK in-car Autopark - TX paused");
    } else if (autopark_before && !s_state.autopark_tx_block) {
        scrl_log("AUTOPARK episode ended - TX resumed");
    }

    if (!usable || frame.dlc == 0u) return;

    // Tesla OTA gate.
    if (frame.id == CAN_ID_GTW_CAR_STATE) {
        s_rx_318++;
        s_state.seen_gtw_car_state++;
        bool was_ota = s_state.tesla_ota_in_progress;
        fsd_handle_gtw_car_state(&s_state, &frame);
        if (!was_ota && s_state.tesla_ota_in_progress) {
            scrl_log("OTA  started - TX suspended");
        } else if (was_ota && !s_state.tesla_ota_in_progress) {
            scrl_log("OTA  finished - TX resumed");
        }
        return;
    }

    // DAS status: AP state and hands-on state.
    if (frame.id == CAN_ID_DAS_STATUS_HW4) {
        s_rx_39b++;
        fsd_handle_das_status_hw4(&s_state, &frame);
        // Latch the values of this same parse, with the receive time.
        scroll_ack_note_hw4_das(&s_scroll, SCRL_BUS, frame, s_state.das_ap_state,
                                s_state.das_hands_on_state);
        return;
    }

    // Steering angle: feeds the Soft Engage gate (off by default).
    if (frame.id == CAN_ID_STEER_ANGLE) {
        s_rx_129++;
        fsd_handle_steering_angle(&s_state, &frame, now);
        return;
    }

    // Vehicle speed: feeds the Autopark release.
    if (frame.id == SCRL_CAN_ID_DI_SPEED) {
        s_rx_257++;
        fsd_handle_di_speed(&s_state, &frame);
        s_state.last_speed_tick_ms = now;
        return;
    }

    if (frame.id != CAN_ID_VCLEFT_SWITCH) return;
    s_rx_3c2++;

    // Scroll Ack: copy the car's own idle page-1 frame and send a left tick
    // pair. Same block as on the T-2CAN.
    if (!s_state.scroll_ack) {
        scroll_ack_reset_episode(&s_scroll);
        return;
    }
    bool tx = fsd_can_transmit(&s_state);
    bool ap_ok = fsd_ap_first_allows(&s_state, now) &&
                 fsd_soft_engage_allows(&s_state, now) &&
                 fsd_abort_guard_allows(&s_state);

    ScrollAckInput in;
    in.enabled = true;
    in.tx_allowed = tx && ap_ok;
    in.bus = SCRL_BUS;
    in.fresh_native = fresh;
    // HW4 is fixed at build time. Also require that a valid 0x39B was seen.
    in.hw4 = s_state.hw_version == TeslaHW_HW4 && s_state.das_hw4_status_seen;
    in.mode = s_state.scroll_ack_mode;
    in.delay_mean_ms = s_state.scroll_ack_delay_ms;
    in.gap_mean_ms = s_state.scroll_ack_gap_ms;
    in.interval_mean_ms = s_state.scroll_ack_interval_ms;
    in.rand = esp_random();
    // Board tick orders (alternate, random) become DOWN_UP or UP_DOWN here.
    in.pattern = scrl_ext_pattern(&s_scroll_ext, s_settings.pattern, in.rand);

    bool armed_before = s_scroll.timer_armed;
    CanFrame out;
    ScrollAckAction act = scroll_ack_step(&s_scroll, in, frame, now, &out);
    if (s_settings.feat[SCRL_F_RAND] == 0u) {
        scrl_ext_fix_interval(&s_scroll, armed_before, act, in.mode, in.interval_mean_ms, now);
    }
    if (act == ScrollAckAction::None) return;
    scrl_ext_place_tick(&s_scroll_ext, s_settings.feat[SCRL_F_LR], act, frame, &out);

    bool sent = scrl_can_send(out);
    scroll_ack_complete(&s_scroll, act, sent);
    if (sent && act == ScrollAckAction::FirstTick) scrl_ext_first_sent(&s_scroll_ext, out);
    if (sent) {
        uint8_t field = s_scroll_ext.pair_right ? out.data[3] : out.data[2];
        scrl_log("SCRL 0x3C2 %s tick %s (%s) TX %s",
                 s_scroll_ext.pair_right ? "right" : "left",
                 (field & 0x3Fu) == 0x01u ? "+1" : "-1",
                 act == ScrollAckAction::FirstTick ? "first" : "second",
                 can_bus_name(SCRL_BUS));
    }
}

ScrlSettings scrl_proc_get_settings(void) {
    ProcLock lock;
    return s_settings;
}

void scrl_proc_set_settings(const ScrlSettings &settings, bool release_bus_off_lock) {
    ProcLock lock;
    bool reset = settings.enabled != s_settings.enabled || settings.mode != s_settings.mode;
    if (release_bus_off_lock && settings.op_mode == (uint8_t)OpMode_Active) {
        s_inhibit &= ~SCRL_INHIBIT_BUS_OFF;
    }
    s_settings = settings;
    scrl_settings_to_state(s_settings, &s_state);
    if (reset) scroll_ack_reset_episode(&s_scroll);
    update_op_mode();
    s_settings_gen.fetch_add(1);
}

uint32_t scrl_proc_settings_gen(void) {
    return s_settings_gen.load();
}

void scrl_proc_set_tsl6(const uint8_t tsl6[SCRL_TSL6_LEN]) {
    ProcLock lock;
    memcpy(s_settings.tsl6, tsl6, SCRL_TSL6_LEN);
    s_settings_gen.fetch_add(1);
}

void scrl_proc_set_inhibit(uint32_t bits, bool on) {
    ProcLock lock;
    if (on) {
        s_inhibit |= bits;
    } else {
        s_inhibit &= ~bits;
    }
    update_op_mode();
}

uint32_t scrl_proc_get_inhibit(void) {
    ProcLock lock;
    return s_inhibit;
}

bool scrl_proc_active(void) {
    return s_active.load();
}

void scrl_proc_fill_status(ScrlStatus *out) {
    uint32_t now = scrl_millis();
    ProcLock lock;
    out->scroll = s_scroll;
    out->op_mode = s_settings.op_mode;
    out->rx_all = s_state.rx_count;
    out->rx_3c2 = s_rx_3c2;
    out->rx_39b = s_rx_39b;
    out->rx_370 = s_rx_370;
    out->rx_257 = s_rx_257;
    out->rx_318 = s_rx_318;
    out->rx_129 = s_rx_129;

    // Same gates as the Scroll Ack block, without the Soft Engage latch write.
    bool tx_allowed = fsd_can_transmit(&s_state) && fsd_ap_first_allows(&s_state, now) &&
                      (!s_state.soft_engage || s_state.soft_engage_latched) &&
                      fsd_abort_guard_allows(&s_state);
    bool ota_blind = s_rx_318 == 0u && s_first_rx_ms != 0u &&
                     (uint32_t)(now - s_first_rx_ms) > OTA_BLIND_AFTER_MS;
    uint16_t flags = 0;
    if (tx_allowed) flags |= SCRL_FLAG_TX_ALLOWED;
    if (s_scroll.timer_armed) flags |= SCRL_FLAG_TIMER_ARMED;
    if (s_state.op_mode == OpMode_Active) flags |= SCRL_FLAG_ACTIVE;
    if (!scroll_ack_is_vehicle_bus(&s_scroll, SCRL_BUS, now)) flags |= SCRL_FLAG_WRONG_BUS;
    if (ota_blind) flags |= SCRL_FLAG_OTA_BLIND;
    if (s_state.scroll_ack) flags |= SCRL_FLAG_ENABLED;
    if (s_inhibit & SCRL_INHIBIT_BUS_OFF) flags |= SCRL_FLAG_BUS_OFF_LOCK;
    if (s_inhibit & SCRL_INHIBIT_BLE_OTA) flags |= SCRL_FLAG_BLE_OTA;
    if (s_state.autopark_tx_block) flags |= SCRL_FLAG_AUTOPARK;
    if (s_state.tesla_ota_in_progress) flags |= SCRL_FLAG_TESLA_OTA;
    if (s_state.das_hw4_status_seen) flags |= SCRL_FLAG_HW4_SEEN;
    if (s_inhibit & SCRL_INHIBIT_IMAGE_PENDING) flags |= SCRL_FLAG_IMAGE_PENDING;
    if (s_inhibit & SCRL_INHIBIT_CAPTURE) flags |= SCRL_FLAG_CAPTURE;
    out->flags = flags;
}

void scrl_proc_reset_counters(void) {
    ProcLock lock;
    s_scroll.sent_count = 0;
    s_scroll.pairs_aborted = 0;
    s_state.rx_count = 0;
    s_rx_3c2 = s_rx_39b = s_rx_370 = s_rx_257 = s_rx_318 = s_rx_129 = 0;
}

bool scrl_proc_parked(void) {
    uint32_t now = scrl_millis();
    ProcLock lock;
    bool silent = s_first_rx_ms == 0u || (uint32_t)(now - s_last_rx_ms) > BUS_SILENT_MS;
    if (silent) return true;

    // AP engaged (3 to 6) or aborting (8, 9), from a fresh 0x39B.
    bool das_fresh = s_scroll.das39b_seen &&
                     (uint32_t)(now - s_scroll.das39b_rx_ms) <= SCROLL_ACK_DAS_FRESH_MS;
    uint8_t ap = s_scroll.das39b_ap_state;
    if (das_fresh && ap >= DAS_APSTATE_ENGAGED && ap <= DAS_APSTATE_ABORTED) return false;

    // The bus has traffic. If this bus carries 0x257, the speed must be fresh,
    // valid and zero. vehicle_speed_kph hides a reverse speed, so use the raw value.
    if (!s_state.speed_seen) return true;  // 0x257 is not on this bus: no speed gate
    if (!fsd_speed_valid(&s_state, now)) return false;
    int delta = (int)s_state.di_speed_raw - SPEED_RAW_ZERO;
    if (delta < 0) delta = -delta;
    return delta <= SPEED_RAW_STILL_BAND;
}
