/*
 * scrl_can.cpp — TWAI glue (ESP-IDF esp_driver_twai node API). See scrl_can.h.
 */

#include "scrl_can.h"

#include <atomic>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "scrl_board.h"
#include "../../fsd_logic/fsd_can_ops.h"  // tesla_can_tx_valid

static const char *TAG = "scrl_can";

// Vehicle CAN carries about 2000 frames per second. 256 frames cover more than
// 100 ms of a stalled CAN task. Older frames fail the 25 ms freshness gate.
#define RX_QUEUE_LEN 256
// A pair has two ticks, at least tens of ms apart. Two slots are enough.
#define TX_SLOTS 2

// The driver keeps the pointer to a TX frame until the frame is done, so the
// frames live here.
struct TxSlot {
    twai_frame_t frame;
    uint8_t data[8];
    std::atomic<bool> busy;
};

static QueueHandle_t s_rx_queue;
static twai_node_handle_t s_node;
static TxSlot s_tx[TX_SLOTS];
static bool s_running;
static bool s_listen_only = true;
static int8_t s_rx_pin = SCRL_PIN_CAN_RX;
static int8_t s_tx_pin = SCRL_PIN_CAN_TX;

static std::atomic<uint32_t> s_rx_frames, s_rx_rejected, s_rx_dropped, s_rx_high_water;
static std::atomic<uint32_t> s_tx_queued, s_tx_ok, s_tx_failed, s_tx_refused;
static std::atomic<uint32_t> s_bus_errors, s_bus_off_count;
static std::atomic<bool> s_bus_off_pending;  // a bus-off that the CAN task did not handle yet
static std::atomic<bool> s_node_bus_off;     // the current node is in bus-off

// ── interrupt callbacks ───────────────────────────────────────────────────────

static bool IRAM_ATTR on_rx_done(twai_node_handle_t node, const twai_rx_done_event_data_t *,
                                 void *) {
    uint8_t data[8];
    twai_frame_t rx = {};
    rx.buffer = data;
    rx.buffer_len = sizeof(data);
    if (twai_node_receive_from_isr(node, &rx) != ESP_OK) return false;
    s_rx_frames.fetch_add(1, std::memory_order_relaxed);
    if (rx.header.dlc > 8u) {
        // Legal on the wire (it means 8 bytes), but CanFrame holds 8 bytes.
        s_rx_rejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    CanFrame f;
    f.id = rx.header.id;
    f.ext = rx.header.ide ? 1u : 0u;
    f.req = rx.header.rtr ? 1u : 0u;
    f.dlc = (uint8_t)rx.header.dlc;
    memset(f.data, 0, sizeof(f.data));
    if (!rx.header.rtr) memcpy(f.data, data, f.dlc);
    // Stamp the frame here, at reception. 0 means "unstamped" to fsd_tx_fresh()
    // and passes as fresh, so never store 0.
    uint32_t now = scrl_millis();
    f.rx_ms = now == 0u ? 1u : now;

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_rx_queue, &f, &woken) != pdTRUE) {
        s_rx_dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
        uint32_t waiting = (uint32_t)uxQueueMessagesWaitingFromISR(s_rx_queue);
        if (waiting > s_rx_high_water.load(std::memory_order_relaxed)) {
            s_rx_high_water.store(waiting, std::memory_order_relaxed);
        }
    }
    return woken == pdTRUE;
}

static bool IRAM_ATTR on_tx_done(twai_node_handle_t, const twai_tx_done_event_data_t *edata,
                                 void *) {
    if (edata->is_tx_success) {
        s_tx_ok.fetch_add(1, std::memory_order_relaxed);
    } else {
        s_tx_failed.fetch_add(1, std::memory_order_relaxed);
    }
    for (int i = 0; i < TX_SLOTS; i++) {
        if (edata->done_tx_frame == &s_tx[i].frame) {
            s_tx[i].busy.store(false, std::memory_order_release);
        }
    }
    return false;
}

static bool IRAM_ATTR on_state_change(twai_node_handle_t,
                                      const twai_state_change_event_data_t *edata, void *) {
    if (edata->new_sta == TWAI_ERROR_BUS_OFF && edata->old_sta != TWAI_ERROR_BUS_OFF) {
        s_bus_off_count.fetch_add(1, std::memory_order_relaxed);
        s_node_bus_off.store(true, std::memory_order_release);
        s_bus_off_pending.store(true, std::memory_order_release);
    }
    return false;
}

static bool IRAM_ATTR on_error(twai_node_handle_t, const twai_error_event_data_t *, void *) {
    s_bus_errors.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// ── controller life cycle ─────────────────────────────────────────────────────

// Both Grove pins become inputs with no pull. The driver does not release the
// TX pin when a node goes away, so do it here.
static void release_pins(void) {
    const gpio_num_t pins[] = {(gpio_num_t)SCRL_PIN_GROVE_A, (gpio_num_t)SCRL_PIN_GROVE_B};
    for (gpio_num_t pin : pins) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, GPIO_FLOATING);
    }
}

void scrl_can_init(void) {
    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(CanFrame));
    configASSERT(s_rx_queue);
    release_pins();
#if !defined(CONFIG_SCRL_CAN_PINS_TX1_RX2) && !defined(CONFIG_SCRL_CAN_PINS_TX2_RX1)
    s_rx_pin = SCRL_PIN_GROVE_B;  // pin probe: start on GPIO2
#endif
}

bool scrl_can_start(bool listen_only) {
    if (s_running) scrl_can_stop();
    if (!listen_only && (s_tx_pin < 0 || SCRL_PIN_CAN_RX < 0)) {
        ESP_LOGE(TAG, "Active refused: the build does not name the CAN pins");
        return false;
    }

    twai_onchip_node_config_t cfg = {};
    // Listen-Only attaches no TX pin: the board drives nothing on the Grove port.
    cfg.io_cfg.tx = listen_only ? GPIO_NUM_NC : (gpio_num_t)s_tx_pin;
    cfg.io_cfg.rx = (gpio_num_t)s_rx_pin;
    cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.bit_timing.bitrate = CONFIG_SCRL_CAN_BITRATE;
    // 32 MHz clock, 16 quanta per bit at 500 kbit/s: sample point 75 %, SJW 2.
    // This is the ESP-IDF standard timing for this chip. The T-2CAN (80 MHz,
    // 20 quanta) samples at 80 %, which 16 quanta cannot make: the next step is
    // 81.25 %. Named here so that a driver update cannot move it.
    cfg.bit_timing.sp_permill = 750;
    cfg.fail_retry_cnt = -1;  // normal CAN retransmission, as on the T-2CAN
    cfg.tx_queue_depth = TX_SLOTS;
    cfg.flags.enable_listen_only = listen_only ? 1u : 0u;

    twai_node_handle_t node = nullptr;
    esp_err_t err = twai_new_node_onchip(&cfg, &node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip: %s", esp_err_to_name(err));
        release_pins();
        return false;
    }
    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = on_rx_done;
    cbs.on_tx_done = on_tx_done;
    cbs.on_state_change = on_state_change;
    cbs.on_error = on_error;
    err = twai_node_register_event_callbacks(node, &cbs, nullptr);
    if (err == ESP_OK) {
        // The acceptance filter stays open: all frames pass, software filters.
        s_node_bus_off.store(false);
        for (TxSlot &slot : s_tx) slot.busy.store(false);
        err = twai_node_enable(node);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start: %s", esp_err_to_name(err));
        twai_node_delete(node);
        release_pins();
        return false;
    }
    s_node = node;
    s_listen_only = listen_only;
    s_running = true;
    ESP_LOGD(TAG, "started: %s, RX GPIO%d, TX %s", listen_only ? "Listen-Only" : "Active",
             (int)s_rx_pin, listen_only ? "not attached" : (s_tx_pin == 1 ? "GPIO1" : "GPIO2"));
    return true;
}

void scrl_can_stop(void) {
    if (s_node) {
        // In bus-off the driver already counts the node as stopped: only delete it.
        if (!s_node_bus_off.load()) twai_node_disable(s_node);
        esp_err_t err = twai_node_delete(s_node);
        // A node that is not deleted keeps the controller: no later start works.
        if (err != ESP_OK) ESP_LOGE(TAG, "twai_node_delete: %s", esp_err_to_name(err));
        s_node = nullptr;
    }
    s_node_bus_off.store(false);
    s_running = false;
    release_pins();
    // The deleted node took its TX queue with it.
    for (TxSlot &slot : s_tx) slot.busy.store(false);
}

bool scrl_can_receive(CanFrame *out, uint32_t timeout_ms) {
    return xQueueReceive(s_rx_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool scrl_can_send(const CanFrame &frame) {
    bool ok = s_running && !s_listen_only && !s_node_bus_off.load() &&
              frame.id == SCRL_CAN_TX_ALLOWED_ID && frame.ext == 0u && frame.req == 0u &&
              tesla_can_tx_valid(frame.id, frame.dlc);
    TxSlot *slot = nullptr;
    if (ok) {
        for (TxSlot &s : s_tx) {
            bool free_slot = false;
            if (s.busy.compare_exchange_strong(free_slot, true)) {
                slot = &s;
                break;
            }
        }
    }
    if (!slot) {
        s_tx_refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    memcpy(slot->data, frame.data, sizeof(slot->data));
    slot->frame = {};
    slot->frame.header.id = frame.id;
    slot->frame.header.dlc = frame.dlc;
    slot->frame.buffer = slot->data;
    slot->frame.buffer_len = frame.dlc;
    if (twai_node_transmit(s_node, &slot->frame, 0) != ESP_OK) {
        slot->busy.store(false);
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
#if !defined(CONFIG_SCRL_CAN_PINS_TX1_RX2) && !defined(CONFIG_SCRL_CAN_PINS_TX2_RX1)
    s_rx_pin = (s_rx_pin == SCRL_PIN_GROVE_A) ? SCRL_PIN_GROVE_B : SCRL_PIN_GROVE_A;
#endif
}

void scrl_can_get_info(ScrlCanInfo *out) {
    out->running = s_running;
    out->listen_only = s_listen_only;
    out->pins_checked = SCRL_PIN_CAN_TX >= 0;
    out->rx_pin = s_running ? s_rx_pin : (int8_t)-1;
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
