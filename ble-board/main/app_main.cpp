/*
 * app_main.cpp — BLE Scroll Ack board (M5Stack NanoH2 + Unit Mini CAN).
 *
 * Tasks:
 *   can           takes frames from the receive interrupt, runs the frame logic,
 *                 sends the ticks, and owns the CAN controller
 *   housekeeping  button, console copy of the event log, BLE notifications,
 *                 update time limits, delayed restart
 *   NimBLE host   BLE callbacks (settings, commands, update data)
 */

#include <atomic>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_store.h"

#include "scrl_app.h"
#include "scrl_ble.h"
#include "scrl_board.h"
#include "scrl_can.h"
#include "scrl_log.h"
#include "scrl_logbuf.h"
#include "scrl_ota.h"
#include "scrl_proc.h"
#include "scrl_store.h"

static const char *TAG = "scrl";

// Above the NimBLE host task, below the BLE controller task: frame handling
// goes before BLE work, and the radio timing stays intact.
#define CAN_TASK_PRIORITY (configMAX_PRIORITIES - 3)
#define CAN_TASK_STACK 4096
#define HOUSEKEEPING_PRIORITY 3
#define HOUSEKEEPING_STACK 6144
#define HOUSEKEEPING_PERIOD_MS 100u
// Pin probe: listen this long on one Grove pin before the other one gets a turn.
#define PROBE_DWELL_MS 3000u
// A failed controller start is tried again after this long.
#define CAN_RETRY_MS 1000u
// Shortest time between two "bus errors" lines in the event log.
#define BUS_ERROR_LOG_MS 10000u
// Hold the button this long to delete the bonds.
#define BUTTON_LONG_MS 5000u

static ScrlBootInfo s_boot;
static std::atomic<bool> s_reboot_pending;
static std::atomic<uint32_t> s_reboot_at_ms;

// ── functions for the BLE and update code (scrl_app.h) ────────────────────────

void scrl_app_collect_status(ScrlStatus *out) {
    scrl_proc_fill_status(out);
    ScrlCanInfo can;
    scrl_can_get_info(&can);
    out->rx_dropped = can.rx_dropped + can.rx_rejected;
    out->tx_ok = can.tx_ok;
    out->tx_failed = can.tx_failed;
    out->tx_refused = can.tx_refused;
    out->bus_off_count = can.bus_off_count;
    out->rx_pin = can.rx_pin;
    out->reset_reason = s_boot.reset_reason;
    out->power_on_count = s_boot.power_on_count;
    out->uptime_s = scrl_millis() / 1000u;
    if (can.pins_checked) out->flags |= SCRL_FLAG_PINS_CHECKED;
    if (can.running) out->flags |= SCRL_FLAG_CAN_RUNNING;
    if (scrl_ble_pairing_open()) out->flags |= SCRL_FLAG_PAIRING_OPEN;
}

void scrl_app_apply_settings(const ScrlSettings &settings, bool release_bus_off_lock) {
    scrl_proc_set_settings(settings, release_bus_off_lock);
    scrl_store_save_settings(settings);
}

void scrl_app_reset_counters(void) {
    scrl_proc_reset_counters();
}

void scrl_app_request_reboot(uint32_t delay_ms) {
    s_reboot_at_ms.store(scrl_millis() + delay_ms);
    s_reboot_pending.store(true);
}

void scrl_app_factory_reset(void) {
    // Stop sending first: the settings go back to off and Listen-Only.
    scrl_proc_set_settings(scrl_settings_defaults(), false);
    scrl_store_factory_reset();
    ble_store_clear();
    scrl_app_request_reboot(500);
}

bool scrl_app_set_passkey(uint32_t passkey) {
    if (!scrl_store_set_passkey(passkey)) return false;
    s_boot.passkey = passkey;
    // USB console only, like the line at boot.
    ESP_LOGI(TAG, "BLE passkey changed: %06u", (unsigned)passkey);
    return true;
}

const ScrlBootInfo *scrl_app_boot_info(void) {
    return &s_boot;
}

// ── board ─────────────────────────────────────────────────────────────────────

static void board_pins_init(void) {
    // LEDs off. GPIO10 low removes the power of the WS2812.
    const gpio_num_t low_pins[] = {(gpio_num_t)SCRL_PIN_RGB_PWR, (gpio_num_t)SCRL_PIN_IR_LED,
                                   (gpio_num_t)SCRL_PIN_BLUE_LED};
    for (gpio_num_t pin : low_pins) {
        gpio_reset_pin(pin);
        gpio_set_level(pin, 0);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
    // The button pulls GPIO9 low. It is also the boot strap pin: input only.
    gpio_reset_pin((gpio_num_t)SCRL_PIN_BUTTON);
    gpio_set_direction((gpio_num_t)SCRL_PIN_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)SCRL_PIN_BUTTON, GPIO_PULLUP_ONLY);
}

// Short press: open the pairing window for 2 minutes. Hold 5 s: delete the bonds too.
static void button_poll(uint32_t now) {
    static bool down;          // debounced state
    static bool long_done;
    static uint8_t same;       // samples in a row that differ from `down`
    static uint32_t down_ms;

    bool pressed = gpio_get_level((gpio_num_t)SCRL_PIN_BUTTON) == 0;
    if (pressed == down) {
        same = 0;
    } else if (++same >= 2u) {  // 2 samples = 200 ms
        same = 0;
        down = pressed;
        if (down) {
            down_ms = now;
            long_done = false;
        } else if (!long_done) {
            scrl_ble_open_pairing(SCRL_BLE_PAIR_WINDOW_BUTTON_MS);
            scrl_log("BTN  pairing window open for 2 min");
        }
    }
    if (down && !long_done && (uint32_t)(now - down_ms) >= BUTTON_LONG_MS) {
        long_done = true;
        scrl_ble_forget_bonds();
        scrl_ble_open_pairing(SCRL_BLE_PAIR_WINDOW_BUTTON_MS);
        scrl_log("BTN  held 5 s: bonds deleted, pairing window open for 2 min");
    }
}

static void housekeeping_task(void *) {
    uint32_t console_cursor = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HOUSEKEEPING_PERIOD_MS));
        uint32_t now = scrl_millis();

        button_poll(now);
        scrl_ble_tick();

        // Copy new event lines to the USB console.
        uint32_t oldest = scrl_log_oldest_seq();
        if ((int32_t)(console_cursor - oldest) < 0) console_cursor = oldest;
        char line[SCRL_LOG_LINE_MAX];
        while (scrl_log_read(console_cursor, line, sizeof(line))) {
            ESP_LOGI(TAG, "%s", line);
            console_cursor++;
        }

        if (s_reboot_pending.load() && (int32_t)(now - s_reboot_at_ms.load()) >= 0) {
            ESP_LOGW(TAG, "restart");
            esp_restart();  // the reset puts the Grove pins back to inputs
        }
    }
}

// The CAN task owns the controller: start, stop, mode change, bus-off, probe.
static void can_task(void *) {
    bool running = false;
    bool listen_only = true;
    uint32_t start_at = 0;        // earliest time for the next start
    uint32_t probe_mark_ms = 0;   // start of the current probe dwell
    uint32_t probe_frames = 0;    // rx count at probe_mark_ms
    int8_t probe_found_pin = -1;  // pin that showed frames, reported once per change
    uint32_t logged_bus_errors = 0;
    uint32_t bus_error_log_ms = 0;
    int8_t logged_mode = -1;      // mode of the last "started" line: the probe restarts
                                  // the controller every few seconds and must not log each one

    for (;;) {
        uint32_t now = scrl_millis();
        bool want_listen_only = !scrl_proc_active();

        if ((!running || want_listen_only != listen_only) && (int32_t)(now - start_at) >= 0) {
            running = scrl_can_start(want_listen_only);
            listen_only = want_listen_only;
            if (running) {
                if (logged_mode != (int8_t)listen_only) {
                    logged_mode = (int8_t)listen_only;
                    scrl_log("CAN  started %s", listen_only ? "Listen-Only" : "Active");
                }
                ScrlCanInfo info;
                scrl_can_get_info(&info);
                probe_mark_ms = now;
                probe_frames = info.rx_frames;
            } else {
                start_at = now + CAN_RETRY_MS;
            }
        }

        CanFrame frame;
        if (scrl_can_receive(&frame, 20)) scrl_proc_handle_frame(frame);

        ScrlCanEvent event = scrl_can_poll_event();
        if (event != SCRL_CAN_EVENT_NONE) {
            running = false;
            logged_mode = -1;  // log the restart
            start_at = scrl_millis() + SCRL_CAN_BUS_OFF_REST_MS;
            if (event == SCRL_CAN_EVENT_BUS_OFF_LIMIT) {
                scrl_proc_set_inhibit(SCRL_INHIBIT_BUS_OFF, true);
                scrl_log("CAN  bus-off limit reached - locked to Listen-Only");
            } else {
                scrl_log("CAN  bus-off - controller restarts");
            }
        }

        ScrlCanInfo info;
        scrl_can_get_info(&info);

        // Bus errors are not in the Status JSON (no room). Report a rise here,
        // at most once in 10 s. A steady rise can mean a wrong bit timing.
        if (info.bus_errors != logged_bus_errors &&
            (uint32_t)(now - bus_error_log_ms) >= BUS_ERROR_LOG_MS) {
            scrl_log("CAN  bus errors %u (+%u)", (unsigned)info.bus_errors,
                     (unsigned)(info.bus_errors - logged_bus_errors));
            logged_bus_errors = info.bus_errors;
            bus_error_log_ms = now;
        }

        // Pin probe, only in a build with unchecked pins. Both Grove pins are
        // inputs here, so the probe cannot disturb the transceiver.
        if (!info.pins_checked && running && (uint32_t)(now - probe_mark_ms) >= PROBE_DWELL_MS) {
            if (info.rx_frames != probe_frames) {
                if (probe_found_pin != info.rx_pin) {
                    probe_found_pin = info.rx_pin;
                    scrl_log("CAN  frames arrive on GPIO%d: RX = GPIO%d, TX = GPIO%d",
                             (int)info.rx_pin, (int)info.rx_pin,
                             info.rx_pin == SCRL_PIN_GROVE_A ? SCRL_PIN_GROVE_B : SCRL_PIN_GROVE_A);
                }
            } else {
                scrl_can_stop();
                scrl_can_probe_swap_rx();
                running = false;
                start_at = now;
            }
            probe_mark_ms = now;
            probe_frames = info.rx_frames;
        }
    }
}

extern "C" void app_main(void) {
    board_pins_init();
    scrl_logbuf_init();
    scrl_store_init();
    s_boot = scrl_store_boot();
    ScrlSettings settings = scrl_store_load_settings();

    scrl_can_init();
    ScrlCanInfo can;
    scrl_can_get_info(&can);
    scrl_proc_init(settings, can.pins_checked ? 0u : SCRL_INHIBIT_NO_TX_PIN);
    scrl_ota_init();  // may hold the board in Listen-Only until the image is confirmed

    scrl_log("BOOT reset reason %u, power-on count %u", (unsigned)s_boot.reset_reason,
             (unsigned)s_boot.power_on_count);
    if (!can.pins_checked) scrl_log("CAN  pins not checked: Listen-Only pin probe");
    // The passkey goes only to the USB console, never into the event log that
    // BLE sends out. Write it on the label of the board.
    ESP_LOGI(TAG, "BLE passkey: %06u", (unsigned)s_boot.passkey);

    xTaskCreate(can_task, "can", CAN_TASK_STACK, nullptr, CAN_TASK_PRIORITY, nullptr);
    // The pairing window of power-on does not open on a restart (command,
    // update, crash): only a real power-on counts.
    scrl_ble_init(s_boot.passkey, s_boot.reset_reason == (uint8_t)ESP_RST_POWERON);
    xTaskCreate(housekeeping_task, "housekeeping", HOUSEKEEPING_STACK, nullptr,
                HOUSEKEEPING_PRIORITY, nullptr);
}
