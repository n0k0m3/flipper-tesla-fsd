/*
 * scrl_can.cpp — state, counters and TX rules of the CAN glue. See scrl_can.h.
 * The controller itself is in the backend (scrl_can_hw.h).
 */

#include "scrl_can.h"

#include <atomic>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "scrl_board.h"
#include "scrl_can_hw.h"
#include "../../fsd_logic/fsd_can_ops.h"  // tesla_can_tx_valid

static const char *TAG = "scrl_can";

// Vehicle CAN carries about 2000 frames per second. 256 frames cover more than
// 100 ms of a stalled CAN task. Older frames fail the 25 ms freshness gate.
#define RX_QUEUE_LEN 256

static QueueHandle_t s_rx_queue;
static bool s_running;
static bool s_listen_only = true;

static std::atomic<uint32_t> s_rx_frames, s_rx_rejected, s_rx_dropped, s_rx_high_water;
static std::atomic<uint32_t> s_tx_queued, s_tx_ok, s_tx_failed, s_tx_refused;
static std::atomic<uint32_t> s_bus_errors, s_bus_off_count;
static std::atomic<bool> s_bus_off_pending;  // a bus-off that the CAN task did not handle yet
static std::atomic<bool> s_hw_bus_off;       // the running controller is in bus-off

// ── called by the backend (also from the TWAI interrupt: keep them in IRAM) ───

void IRAM_ATTR scrl_can_rx_frame(const CanFrame &frame, BaseType_t *woken) {
    s_rx_frames.fetch_add(1, std::memory_order_relaxed);
    BaseType_t ok = woken ? xQueueSendFromISR(s_rx_queue, &frame, woken)
                          : xQueueSend(s_rx_queue, &frame, 0);
    if (ok != pdTRUE) {
        s_rx_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    uint32_t waiting = woken ? (uint32_t)uxQueueMessagesWaitingFromISR(s_rx_queue)
                             : (uint32_t)uxQueueMessagesWaiting(s_rx_queue);
    if (waiting > s_rx_high_water.load(std::memory_order_relaxed)) {
        s_rx_high_water.store(waiting, std::memory_order_relaxed);
    }
}

void IRAM_ATTR scrl_can_note_rx_rejected(void) {
    s_rx_frames.fetch_add(1, std::memory_order_relaxed);
    s_rx_rejected.fetch_add(1, std::memory_order_relaxed);
}

void scrl_can_note_rx_overflow(uint32_t lost) {
    s_rx_dropped.fetch_add(lost, std::memory_order_relaxed);
}

void IRAM_ATTR scrl_can_note_tx_done(bool ok) {
    if (ok) {
        s_tx_ok.fetch_add(1, std::memory_order_relaxed);
    } else {
        s_tx_failed.fetch_add(1, std::memory_order_relaxed);
    }
}

void IRAM_ATTR scrl_can_note_bus_errors(uint32_t count) {
    s_bus_errors.fetch_add(count, std::memory_order_relaxed);
}

void IRAM_ATTR scrl_can_note_bus_off(void) {
    if (s_hw_bus_off.exchange(true)) return;  // one event per bus-off
    s_bus_off_count.fetch_add(1, std::memory_order_relaxed);
    s_bus_off_pending.store(true, std::memory_order_release);
}

// ── CAN task API ──────────────────────────────────────────────────────────────

void scrl_can_init(void) {
    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(CanFrame));
    configASSERT(s_rx_queue);
    scrl_can_hw_init();
}

bool scrl_can_start(bool listen_only) {
    if (s_running) scrl_can_stop();
#if defined(CONFIG_SCRL_NEVER_TX)
    if (!listen_only) {
        ESP_LOGE(TAG, "Active refused: this build never transmits");
        return false;
    }
#endif
    if (!listen_only && SCRL_PIN_CAN_TX_CHECKED == 0) {
        ESP_LOGE(TAG, "Active refused: the build does not name the CAN pins");
        return false;
    }
    s_hw_bus_off.store(false);
    if (!scrl_can_hw_start(listen_only)) return false;
    s_listen_only = listen_only;
    s_running = true;
    return true;
}

void scrl_can_stop(void) {
    scrl_can_hw_stop();
    s_hw_bus_off.store(false);
    s_running = false;
}

bool scrl_can_receive(CanFrame *out, uint32_t timeout_ms) {
    return xQueueReceive(s_rx_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool scrl_can_send(const CanFrame &frame) {
    bool ok = s_running && !s_listen_only && !s_hw_bus_off.load() &&
              frame.id == SCRL_CAN_TX_ALLOWED_ID && frame.ext == 0u && frame.req == 0u &&
              tesla_can_tx_valid(frame.id, frame.dlc);
#if defined(CONFIG_SCRL_NEVER_TX)
    ok = false;
#endif
    if (!ok || !scrl_can_hw_send(frame)) {
        s_tx_refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    s_tx_queued.fetch_add(1, std::memory_order_relaxed);
    return true;
}

ScrlCanEvent scrl_can_poll_event(void) {
    if (!s_bus_off_pending.exchange(false)) return SCRL_CAN_EVENT_NONE;
    // Stop at once. A recovery in place would later send the ticks that still
    // wait in the controller, long after their native frame.
    scrl_can_stop();
    uint32_t count = s_bus_off_count.load();
    ESP_LOGW(TAG, "bus-off number %u", (unsigned)count);
    return count >= SCRL_CAN_BUS_OFF_LIMIT ? SCRL_CAN_EVENT_BUS_OFF_LIMIT : SCRL_CAN_EVENT_BUS_OFF;
}

void scrl_can_probe_swap_rx(void) {
    scrl_can_hw_probe_swap_rx();
}

void scrl_can_get_info(ScrlCanInfo *out) {
    out->running = s_running;
    out->listen_only = s_listen_only;
    out->pins_checked = SCRL_PIN_CAN_TX_CHECKED != 0;
    out->rx_pin = s_running ? scrl_can_hw_rx_pin() : (int8_t)-1;
    out->rx_frames = s_rx_frames.load();
    out->rx_rejected = s_rx_rejected.load();
    out->rx_dropped = s_rx_dropped.load();
    out->rx_high_water = s_rx_high_water.load();
    out->tx_queued = s_tx_queued.load();
    out->tx_ok = s_tx_ok.load();
    out->tx_failed = s_tx_failed.load();
    out->tx_refused = s_tx_refused.load();
    out->bus_errors = s_bus_errors.load();
    out->bus_off_count = s_bus_off_count.load();
}
