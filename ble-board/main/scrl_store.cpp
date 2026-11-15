/*
 * scrl_store.cpp — see scrl_store.h.
 */

#include "scrl_store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "scrl_store";
static const char *NS = "scrl";

static nvs_handle_t s_nvs;
static bool s_open;
static ScrlSettings s_saved;   // what NVS holds now
static bool s_saved_valid;     // false until every key was written once

void scrl_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s): erase", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK) err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    s_open = err == ESP_OK;
    if (!s_open) ESP_LOGE(TAG, "NVS open failed: %s. Settings will not persist", esp_err_to_name(err));
}

ScrlSettings scrl_store_load_settings(void) {
    ScrlSettings s = scrl_settings_defaults();
    if (!s_open) return s;
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    int found = 0;
    if (nvs_get_u8(s_nvs, "scrAck", &u8) == ESP_OK) { s.enabled = u8 != 0u; found++; }
    if (nvs_get_u8(s_nvs, "scrMode", &u8) == ESP_OK) { s.mode = u8; found++; }
    if (nvs_get_u8(s_nvs, "scrPat", &u8) == ESP_OK) { s.pattern = u8; found++; }
    if (nvs_get_u16(s_nvs, "scrDly", &u16) == ESP_OK) { s.delay_ms = u16; found++; }
    if (nvs_get_u16(s_nvs, "scrGap", &u16) == ESP_OK) { s.gap_ms = u16; found++; }
    if (nvs_get_u16(s_nvs, "scrInt", &u16) == ESP_OK) { s.interval_ms = u16; found++; }
    if (nvs_get_u8(s_nvs, "opMode", &u8) == ESP_OK) { s.op_mode = u8; found++; }
    ScrlSettings raw = s;
    scrl_settings_clamp(&s);
    // NVS matches the result only when every key exists and none was out of range.
    s_saved = s;
    s_saved_valid = found == 7 && scrl_settings_equal(raw, s);
    return s;
}

void scrl_store_save_settings(const ScrlSettings &s) {
    if (!s_open) return;
    bool all = !s_saved_valid;
    esp_err_t err = ESP_OK;
    bool wrote = false;
    auto put_u8 = [&](const char *key, uint8_t now, uint8_t before) {
        if (!all && now == before) return;
        esp_err_t e = nvs_set_u8(s_nvs, key, now);
        if (e != ESP_OK) err = e;
        wrote = true;
    };
    auto put_u16 = [&](const char *key, uint16_t now, uint16_t before) {
        if (!all && now == before) return;
        esp_err_t e = nvs_set_u16(s_nvs, key, now);
        if (e != ESP_OK) err = e;
        wrote = true;
    };
    put_u8("scrAck", s.enabled ? 1u : 0u, s_saved.enabled ? 1u : 0u);
    put_u8("scrMode", s.mode, s_saved.mode);
    put_u8("scrPat", s.pattern, s_saved.pattern);
    put_u16("scrDly", s.delay_ms, s_saved.delay_ms);
    put_u16("scrGap", s.gap_ms, s_saved.gap_ms);
    put_u16("scrInt", s.interval_ms, s_saved.interval_ms);
    put_u8("opMode", s.op_mode, s_saved.op_mode);
    if (!wrote) return;
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) {
        s_saved = s;
        s_saved_valid = true;
    } else {
        ESP_LOGE(TAG, "settings save failed: %s", esp_err_to_name(err));
        s_saved_valid = false;  // unknown state: write every key next time
    }
}

// A uniform 6-digit number from the hardware random source.
static uint32_t new_passkey(void) {
    const uint32_t limit = 4294000000u;  // largest multiple of 1000000 below 2^32
    uint32_t r;
    do {
        r = esp_random();
    } while (r >= limit);
    return r % 1000000u;
}

ScrlBootInfo scrl_store_boot(void) {
    ScrlBootInfo info;
    memset(&info, 0, sizeof(info));
    info.reset_reason = (uint8_t)esp_reset_reason();
    info.reset_log[0] = info.reset_reason;
    if (!s_open) {
        // No store: a passkey for this boot only. Pairing still needs it.
        info.passkey = new_passkey();
        return info;
    }

    uint8_t old_log[SCRL_RESET_LOG_LEN] = {};
    size_t len = sizeof(old_log);
    if (nvs_get_blob(s_nvs, "rstLog", old_log, &len) == ESP_OK && len == sizeof(old_log)) {
        memcpy(&info.reset_log[1], old_log, SCRL_RESET_LOG_LEN - 1);
    }
    nvs_set_blob(s_nvs, "rstLog", info.reset_log, sizeof(info.reset_log));

    nvs_get_u32(s_nvs, "pwrOnCnt", &info.power_on_count);
    if (info.reset_reason == (uint8_t)ESP_RST_POWERON) {
        info.power_on_count++;
        nvs_set_u32(s_nvs, "pwrOnCnt", info.power_on_count);
    }

    if (nvs_get_u32(s_nvs, "passkey", &info.passkey) != ESP_OK || info.passkey > 999999u) {
        info.passkey = new_passkey();
        nvs_set_u32(s_nvs, "passkey", info.passkey);
    }
    esp_err_t err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "boot record save failed: %s", esp_err_to_name(err));
    return info;
}

void scrl_store_factory_reset(void) {
    if (!s_open) return;
    nvs_erase_all(s_nvs);
    nvs_commit(s_nvs);
    s_saved_valid = false;
}
