/*
 * scrl_capture.cpp — the one capture instance and its locks: start and stop
 * requests (BLE task -> CAN task), the step marks, and the sender calls. The
 * order of the packets is in scrl_capture.h (scrl_cap_next).
 */

#include "scrl_capture.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "scrl_board.h"
#include "scrl_logbuf.h"
#include "scrl_proc.h"

static ScrlCapture s_cap;
static ScrlCapSender s_send;

// Requests: the BLE task writes them, the CAN task applies them.
static portMUX_TYPE s_req_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_req_start;
static bool s_req_stop;
static uint8_t s_req_mode;
static uint8_t s_req_ids[SCRL_CAP_LIST_MAX * 2u];
static size_t s_req_ids_len;

static bool start_pending(void) {
    taskENTER_CRITICAL(&s_req_lock);
    bool pending = s_req_start;
    taskEXIT_CRITICAL(&s_req_lock);
    return pending;
}

ScrlCapRefuse scrl_capture_request_start(uint8_t mode, const uint8_t *ids, size_t ids_len) {
#if !defined(CONFIG_SCRL_CAN_TWAI)
    (void)mode; (void)ids; (void)ids_len;
    return ScrlCapRefuse::NotTwai;
#else
    if (mode > SCRL_CAP_MODE_LIST || (mode != SCRL_CAP_MODE_LIST && ids_len != 0u) ||
        (mode == SCRL_CAP_MODE_LIST &&
         (ids_len == 0u || (ids_len & 1u) != 0u || ids_len > sizeof(s_req_ids)))) {
        return ScrlCapRefuse::BadRequest;
    }
    if (scrl_proc_get_inhibit() & SCRL_INHIBIT_BLE_OTA) return ScrlCapRefuse::Update;
    if (s_send.active.load()) return ScrlCapRefuse::Busy;
    taskENTER_CRITICAL(&s_req_lock);
    s_req_mode = mode;
    if (ids_len) memcpy(s_req_ids, ids, ids_len);
    s_req_ids_len = ids_len;
    s_req_stop = false;
    s_req_start = true;
    taskEXIT_CRITICAL(&s_req_lock);
    s_send.active.store(true);
    // Listen-Only at once: the CAN task restarts the controller without TX.
    scrl_proc_set_inhibit(SCRL_INHIBIT_CAPTURE, true);
    return ScrlCapRefuse::None;
#endif
}

void scrl_capture_request_stop(void) {
    if (!s_send.active.load()) return;
    taskENTER_CRITICAL(&s_req_lock);
    s_req_start = false;
    s_req_stop = true;
    taskEXIT_CRITICAL(&s_req_lock);
    scrl_proc_set_inhibit(SCRL_INHIBIT_CAPTURE, false);
}

bool scrl_capture_mark(uint8_t step) {
    if (!s_cap.running.load()) return false;
    return scrl_cap_mark_push(&s_send, step, scrl_millis());
}

void scrl_capture_can_tick(void) {
    bool start = false, stop = false;
    uint8_t mode = 0;
    static uint8_t ids[sizeof(s_req_ids)];
    size_t ids_len = 0;
    taskENTER_CRITICAL(&s_req_lock);
    if (s_req_start) {
        start = true;
        mode = s_req_mode;
        ids_len = s_req_ids_len;
        memcpy(ids, s_req_ids, ids_len);
        s_req_start = false;
    }
    if (s_req_stop) {
        stop = true;
        s_req_stop = false;
    }
    taskEXIT_CRITICAL(&s_req_lock);

    if (stop && s_cap.running.load()) {
        scrl_cap_stop(&s_cap);
        scrl_log("CAP  stopped: %u frames, %u lost in the ring", (unsigned)s_cap.rx_all.load(),
                 (unsigned)s_cap.ring_lost.load());
    }
    if (start) {
        // The sender is idle: s_send.active was false when the request came,
        // and the sender builds no packet while a start is pending.
        scrl_cap_sender_reset(&s_send, scrl_millis());
        if (scrl_cap_start(&s_cap, mode, ids, ids_len)) {
            scrl_log("CAP  started, mode %u: Listen-Only until it stops", (unsigned)mode);
        } else {
            s_send.active.store(false);
            scrl_proc_set_inhibit(SCRL_INHIBIT_CAPTURE, false);
            scrl_log("CAP  start refused: bad ID list");
        }
    }
}

void scrl_capture_can_feed(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data, uint32_t t_ms) {
    scrl_cap_feed(&s_cap, id, ext, dlc, data, t_ms);
}

void scrl_capture_link_lost(void) {
    if (!s_send.active.load()) return;
    scrl_capture_request_stop();
    // The CAN task applies the stop on its next loop. Until then the capture
    // stays active, so a new start cannot race the old one.
    if (!s_cap.running.load() && !start_pending()) scrl_cap_drop(&s_cap, &s_send);
}

bool scrl_capture_active(void) {
    return s_send.active.load();
}

size_t scrl_capture_next_packet(uint8_t *out, size_t size, uint32_t now_ms, uint32_t rx_queue_lost) {
    if (start_pending()) return 0u;  // the CAN task did not apply the start yet
    return scrl_cap_next(&s_cap, &s_send, out, size, now_ms, rx_queue_lost);
}

void scrl_capture_sent(void) {
    scrl_cap_next_sent(&s_cap, &s_send, scrl_millis());
}
