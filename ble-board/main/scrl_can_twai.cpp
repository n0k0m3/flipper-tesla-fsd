/*
 * scrl_can_twai.cpp — on-chip TWAI backend (ESP-IDF esp_driver_twai node API).
 * See scrl_can_hw.h.
 */

#include "scrl_can_hw.h"

#include <atomic>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

#include "scrl_board.h"
#include "soc/soc.h"  // APB_CLK_FREQ

#if (defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3))
// The fixed timing in set_timing() needs the TWAI clock (APB) at 80 MHz.
// Dynamic frequency scaling could lower it.
static_assert(APB_CLK_FREQ == 80000000, "TWAI timing assumes an 80 MHz APB clock");
#if defined(CONFIG_PM_ENABLE)
#error "Power management changes the APB clock: the TWAI timing assumes 80 MHz"
#endif
#endif

static const char *TAG = "scrl_twai";

// A pair has two ticks, at least tens of ms apart. Two slots are enough.
#define TX_SLOTS 2

// The driver keeps the pointer to a TX frame until the frame is done, so the
// frames live here.
struct TxSlot {
    twai_frame_t frame;
    uint8_t data[8];
    std::atomic<bool> busy;
};

static twai_node_handle_t s_node;
static TxSlot s_tx[TX_SLOTS];
static std::atomic<bool> s_node_bus_off;  // the current node is in bus-off
#if defined(SCRL_CAN_PIN_PROBE)
static int8_t s_rx_pin = SCRL_PIN_GROVE_B;  // pin probe: start on GPIO2
#else
static int8_t s_rx_pin = SCRL_PIN_CAN_RX;
#endif

// ── interrupt callbacks ───────────────────────────────────────────────────────

static bool IRAM_ATTR on_rx_done(twai_node_handle_t node, const twai_rx_done_event_data_t *,
                                 void *) {
    uint8_t data[8];
    twai_frame_t rx = {};
    rx.buffer = data;
    rx.buffer_len = sizeof(data);
    if (twai_node_receive_from_isr(node, &rx) != ESP_OK) return false;
    if (rx.header.dlc > 8u) {
        // Legal on the wire (it means 8 bytes), but CanFrame holds 8 bytes.
        scrl_can_note_rx_rejected();
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
    scrl_can_rx_frame(f, &woken);
    return woken == pdTRUE;
}

static bool IRAM_ATTR on_tx_done(twai_node_handle_t, const twai_tx_done_event_data_t *edata,
                                 void *) {
    scrl_can_note_tx_done(edata->is_tx_success);
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
        s_node_bus_off.store(true, std::memory_order_release);
        scrl_can_note_bus_off();
    }
    return false;
}

static bool IRAM_ATTR on_error(twai_node_handle_t, const twai_error_event_data_t *, void *) {
    scrl_can_note_bus_errors(1);
    return false;
}

// ── controller life cycle ─────────────────────────────────────────────────────

// The driver does not release the TX pin when a node goes away, so do it here.
static void release_pins(void) {
#if defined(SCRL_CAN_PIN_PROBE)
    // Direction unknown: both Grove pins are inputs with no pull, so the board
    // drives nothing whatever the wiring is.
    const gpio_num_t pins[] = {(gpio_num_t)SCRL_PIN_GROVE_A, (gpio_num_t)SCRL_PIN_GROVE_B};
    for (gpio_num_t pin : pins) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, GPIO_FLOATING);
    }
#else
    // Known wiring: TX is an input with pull-up (recessive at the transceiver
    // TXD), RX is an input.
    gpio_reset_pin((gpio_num_t)SCRL_PIN_CAN_TX);
    gpio_set_direction((gpio_num_t)SCRL_PIN_CAN_TX, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)SCRL_PIN_CAN_TX, GPIO_PULLUP_ONLY);
    gpio_reset_pin((gpio_num_t)SCRL_PIN_CAN_RX);
    gpio_set_direction((gpio_num_t)SCRL_PIN_CAN_RX, GPIO_MODE_INPUT);
#endif
}

void scrl_can_hw_init(void) {
    release_pins();
}

// Bit timing. Named here, so that a driver update cannot move it.
static esp_err_t set_timing(twai_node_handle_t node, twai_onchip_node_config_t *cfg) {
#if (defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && \
    CONFIG_SCRL_CAN_BITRATE == 500000
    // 80 MHz clock: the same timing as hypery's TWAI_TIMING_CONFIG_500KBITS()
    // on these chips: BRP 8, 20 quanta, TSEG1 15, TSEG2 4, SJW 3, sample
    // point 80 %. The driver's own calculation would give SJW 2.
    (void)cfg;
    twai_timing_advanced_config_t t = {};
    t.brp = 8;
    t.prop_seg = 0;  // no prop segment in this controller: TSEG1 holds it
    t.tseg_1 = 15;
    t.tseg_2 = 4;
    t.sjw = 3;
    t.ssp_offset = 0;  // one sample per bit
    return twai_node_reconfig_timing(node, &t, nullptr);
#else
    // ESP32-H2: 32 MHz clock, 16 quanta per bit at 500 kbit/s: sample point
    // 75 %, SJW 2. The T-2CAN samples at 80 %, which 16 quanta cannot make:
    // the next step is 81.25 %. Set through the basic config in start().
    (void)node;
    (void)cfg;
    return ESP_OK;
#endif
}

bool scrl_can_hw_start(bool listen_only) {
    twai_onchip_node_config_t cfg = {};
    // Listen-Only attaches no TX pin: the board drives nothing on the bus.
    cfg.io_cfg.tx = listen_only ? GPIO_NUM_NC : (gpio_num_t)SCRL_PIN_CAN_TX;
    cfg.io_cfg.rx = (gpio_num_t)s_rx_pin;
    cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.bit_timing.bitrate = CONFIG_SCRL_CAN_BITRATE;
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
    err = set_timing(node, &cfg);
    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = on_rx_done;
    cbs.on_tx_done = on_tx_done;
    cbs.on_state_change = on_state_change;
    cbs.on_error = on_error;
    if (err == ESP_OK) err = twai_node_register_event_callbacks(node, &cbs, nullptr);
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
    ESP_LOGD(TAG, "started: %s, RX GPIO%d", listen_only ? "Listen-Only" : "Active", (int)s_rx_pin);
    return true;
}

void scrl_can_hw_stop(void) {
    if (s_node) {
        // In bus-off the driver already counts the node as stopped: only delete it.
        if (!s_node_bus_off.load()) twai_node_disable(s_node);
        esp_err_t err = twai_node_delete(s_node);
        // A node that is not deleted keeps the controller: no later start works.
        if (err != ESP_OK) ESP_LOGE(TAG, "twai_node_delete: %s", esp_err_to_name(err));
        s_node = nullptr;
    }
    s_node_bus_off.store(false);
    release_pins();
    // The deleted node took its TX queue with it.
    for (TxSlot &slot : s_tx) slot.busy.store(false);
}

bool scrl_can_hw_send(const CanFrame &frame) {
    if (!s_node || s_node_bus_off.load()) return false;
    TxSlot *slot = nullptr;
    for (TxSlot &s : s_tx) {
        bool free_slot = false;
        if (s.busy.compare_exchange_strong(free_slot, true)) {
            slot = &s;
            break;
        }
    }
    if (!slot) return false;
    memcpy(slot->data, frame.data, sizeof(slot->data));
    slot->frame = {};
    slot->frame.header.id = frame.id;
    slot->frame.header.dlc = frame.dlc;
    slot->frame.buffer = slot->data;
    slot->frame.buffer_len = frame.dlc;
    if (twai_node_transmit(s_node, &slot->frame, 0) != ESP_OK) {
        slot->busy.store(false);
        return false;
    }
    return true;
}

int8_t scrl_can_hw_rx_pin(void) {
    return s_rx_pin;
}

void scrl_can_hw_probe_swap_rx(void) {
#if defined(SCRL_CAN_PIN_PROBE)
    s_rx_pin = (s_rx_pin == SCRL_PIN_GROVE_A) ? SCRL_PIN_GROVE_B : SCRL_PIN_GROVE_A;
#endif
}
