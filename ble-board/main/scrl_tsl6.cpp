/*
 * scrl_tsl6.cpp — see scrl_tsl6.h.
 */

#include "scrl_tsl6.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "scrl_can.h"
#include "scrl_logbuf.h"
#include "scrl_proc.h"
#include "tsl6_engine.h"

static_assert(SCRL_TSL6_LEN == TSL6_CFG_LEN, "the board and the engine agree on the settings size");

static const char *TAG = "scrl_tsl6";
static const char *NS = "tsl6";

static nvs_handle_t s_nvs;
static bool s_open;
static bool s_changed;              // the engine changed its settings itself
static uint32_t s_gen_seen = UINT32_MAX;
static std::atomic<uint32_t> s_tx;

static uint32_t now_ticks(void) {
    return (uint32_t)(esp_timer_get_time() / 625);  // 0.625 ms
}

static int send(const tsl6_frame_t *f) {
    // Listen-Only (the setting, a capture, an update, bus-off): the engine runs
    // and sends nothing. Not counted as a refused send.
    if (!scrl_proc_active()) return 0;
    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = f->id;
    frame.dlc = f->dlc;
    memcpy(frame.data, f->data, sizeof(f->data));
    if (!scrl_can_send_tsl6(frame)) return 0;
    s_tx.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// One NVS blob per stock flash page: key "p" + the low 20 bits of the address.
static void page_key(uint32_t page, char key[8]) {
    snprintf(key, 8, "p%05x", (unsigned)(page & 0xFFFFFu));
}

static int store_read(uint32_t page, void *dst, uint32_t len) {
    if (!s_open) return 0;
    char key[8];
    page_key(page, key);
    size_t n = len;
    return nvs_get_blob(s_nvs, key, dst, &n) == ESP_OK && n == len;
}

static void store_write(uint32_t page, const void *src, uint32_t len) {
    if (!s_open) return;
    char key[8];
    page_key(page, key);
    esp_err_t err = nvs_set_blob(s_nvs, key, src, len);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "save %s failed: %s", key, esp_err_to_name(err));
}

static void log_line(const char *line) { scrl_log("%s", line); }
static void settings_changed(void) { s_changed = true; }
static void reboot(void) {}

static const tsl6_hal_t s_hal = {now_ticks, send, store_read, store_write,
                                 log_line, settings_changed, reboot};

void scrl_tsl6_init(uint8_t out_tsl6[SCRL_TSL6_LEN]) {
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    s_open = err == ESP_OK;
    if (!s_open) ESP_LOGE(TAG, "NVS open failed: %s. TSL6 settings will not persist", esp_err_to_name(err));
    tsl6_init(&s_hal);
    tsl6_cfg_get(out_tsl6);
}

void scrl_tsl6_frame(const CanFrame &frame) {
    tsl6_rx(frame.id, frame.ext != 0u, frame.dlc, frame.data);
}

uint32_t scrl_tsl6_poll(void) {
    uint32_t gen = scrl_proc_settings_gen();
    if (gen != s_gen_seen) {
        s_gen_seen = gen;
        ScrlSettings s = scrl_proc_get_settings();
        uint8_t cur[SCRL_TSL6_LEN];
        tsl6_cfg_get(cur);
        if (memcmp(cur, s.tsl6, sizeof(cur)) != 0) {
            tsl6_cfg_set(s.tsl6);
            scrl_log("TSL6 settings applied");
        }
    }
    uint32_t next = tsl6_poll();
    if (s_changed) {
        s_changed = false;
        uint8_t cur[SCRL_TSL6_LEN];
        tsl6_cfg_get(cur);
        scrl_proc_set_tsl6(cur);
        scrl_log("TSL6 settings changed in the car");
    }
    return next;
}

void scrl_tsl6_erase_store(void) {
    if (!s_open) return;
    nvs_erase_all(s_nvs);
    nvs_commit(s_nvs);
}

uint32_t scrl_tsl6_tx_count(void) {
    return s_tx.load(std::memory_order_relaxed);
}
