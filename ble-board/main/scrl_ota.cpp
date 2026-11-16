/*
 * scrl_ota.cpp — see scrl_ota.h.
 *
 * The BLE host task calls control, data and link_lost. The housekeeping task
 * calls tick and confirm. One mutex keeps them apart.
 */

#include "scrl_ota.h"

#include <string.h>

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

#include "scrl_board.h"
#include "scrl_board_desc.h"
#include "scrl_logbuf.h"
#include "scrl_proc.h"

static const char *TAG = "scrl_ota";

static SemaphoreHandle_t s_lock;
static bool s_pending;            // the running image is not confirmed
static bool s_rollback_failed;    // the rollback call found no other valid image
static uint32_t s_boot_ms;

static bool s_active;
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_target;
static mbedtls_sha256_context s_sha;
static uint8_t s_want_sha[32];
static uint32_t s_size;
static uint32_t s_received;
static uint32_t s_credit_sent;    // offset of the last CREDIT
static uint32_t s_last_data_ms;

struct Lock {
    Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};

static void fill_event(uint8_t *event, uint8_t evt, uint8_t status) {
    event[0] = evt;
    event[1] = status;
    event[2] = (uint8_t)(s_received & 0xFFu);
    event[3] = (uint8_t)((s_received >> 8) & 0xFFu);
    event[4] = (uint8_t)((s_received >> 16) & 0xFFu);
    event[5] = (uint8_t)(s_received >> 24);
}

// True when the board descriptor of a new image names this board. Logs a refusal.
static bool board_ok(const ScrlBoardDesc &desc) {
    if (scrl_board_desc_match(desc, scrl_board_desc)) return true;
    char name[sizeof(desc.board) + 1] = {};
    if (desc.magic == SCRL_BOARD_DESC_MAGIC) memcpy(name, desc.board, sizeof(desc.board));
    scrl_log("UPD  image is for board \"%s\", this is \"%s\"", name[0] ? name : "?",
             scrl_board_desc.board);
    return false;
}

// Stop a running update and leave the old image selected.
static void stop_update(uint8_t status) {
    if (!s_active) return;
    esp_ota_abort(s_handle);
    mbedtls_sha256_free(&s_sha);
    s_active = false;
    scrl_proc_set_inhibit(SCRL_INHIBIT_BLE_OTA, false);
    scrl_log("UPD  stopped, status %u, %u of %u bytes", (unsigned)status, (unsigned)s_received,
             (unsigned)s_size);
}

void scrl_ota_init(void) {
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    s_boot_ms = scrl_millis();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending = true;
        // An image that nobody confirmed must not send.
        scrl_proc_set_inhibit(SCRL_INHIBIT_IMAGE_PENDING, true);
        scrl_log("UPD  new image runs from %s - confirm it within 10 min", running->label);
    }
}

bool scrl_ota_image_pending(void) {
    Lock lock;
    return s_pending;
}

bool scrl_ota_confirm(void) {
    Lock lock;
    if (!s_pending) return true;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "confirm failed: %s", esp_err_to_name(err));
        return false;
    }
    s_pending = false;
    scrl_proc_set_inhibit(SCRL_INHIBIT_IMAGE_PENDING, false);
    scrl_log("UPD  image confirmed");
    return true;
}

bool scrl_ota_active(void) {
    Lock lock;
    return s_active;
}

static uint8_t begin_update(const uint8_t *data, size_t len) {
    if (len != 1u + 4u + 32u) return SCRL_OTA_ERR_REQUEST;
    if (s_active) return SCRL_OTA_ERR_STATE;
    // Confirm the running image first. Else a second update could replace the
    // only image that is known to work.
    if (s_pending) return SCRL_OTA_ERR_PENDING;
    if (!scrl_proc_parked()) return SCRL_OTA_ERR_NOT_PARKED;

    uint32_t size = (uint32_t)data[1] | ((uint32_t)data[2] << 8) | ((uint32_t)data[3] << 16) |
                    ((uint32_t)data[4] << 24);
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (!target) return SCRL_OTA_ERR_FLASH;
    if (size < sizeof(esp_image_header_t) || size > target->size) return SCRL_OTA_ERR_SIZE;

    // Listen-Only for the whole update. The CAN task restarts the controller.
    scrl_proc_set_inhibit(SCRL_INHIBIT_BLE_OTA, true);
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        scrl_proc_set_inhibit(SCRL_INHIBIT_BLE_OTA, false);
        return SCRL_OTA_ERR_FLASH;
    }
    memcpy(s_want_sha, &data[5], sizeof(s_want_sha));
    mbedtls_sha256_init(&s_sha);
    mbedtls_sha256_starts(&s_sha, 0);
    s_target = target;
    s_size = size;
    s_received = 0;
    s_credit_sent = 0;
    s_last_data_ms = scrl_millis();
    s_active = true;
    scrl_log("UPD  started, %u bytes to %s", (unsigned)size, target->label);
    return SCRL_OTA_OK;
}

static uint8_t end_update(void) {
    if (!s_active) return SCRL_OTA_ERR_STATE;
    if (s_received != s_size) {
        stop_update(SCRL_OTA_ERR_SIZE);
        return SCRL_OTA_ERR_SIZE;
    }
    uint8_t sha[32];
    mbedtls_sha256_finish(&s_sha, sha);
    if (memcmp(sha, s_want_sha, sizeof(sha)) != 0) {
        stop_update(SCRL_OTA_ERR_SHA);
        return SCRL_OTA_ERR_SHA;
    }
    mbedtls_sha256_free(&s_sha);
    s_active = false;  // the handle is gone after esp_ota_end, good or bad
    // esp_ota_end checks the image: magic, chip ID, segment checksum, hash.
    esp_err_t err = esp_ota_end(s_handle);
    if (err == ESP_OK) {
        // Same chip is not enough: the image must name this board.
        ScrlBoardDesc desc;
        if (esp_partition_read(s_target, SCRL_BOARD_DESC_OFFSET, &desc, sizeof(desc)) != ESP_OK ||
            !board_ok(desc)) {
            err = ESP_ERR_OTA_VALIDATE_FAILED;
        }
    }
    if (err == ESP_OK) err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(err));
        scrl_proc_set_inhibit(SCRL_INHIBIT_BLE_OTA, false);
        scrl_log("UPD  image rejected");
        return err == ESP_ERR_OTA_VALIDATE_FAILED ? SCRL_OTA_ERR_IMAGE : SCRL_OTA_ERR_FLASH;
    }
    // Stay in Listen-Only until the restart.
    scrl_log("UPD  image written to %s - restart", s_target->label);
    return SCRL_OTA_OK;
}

bool scrl_ota_control(const uint8_t *data, size_t len, uint8_t *event) {
    Lock lock;
    uint8_t op = len > 0u ? data[0] : 0u;
    bool restart = false;
    if (op == SCRL_OTA_OP_BEGIN) {
        uint8_t status = begin_update(data, len);
        fill_event(event, SCRL_OTA_EVT_BEGIN, status);
    } else if (op == SCRL_OTA_OP_END && len == 1u) {
        uint8_t status = end_update();
        fill_event(event, SCRL_OTA_EVT_END, status);
        restart = status == SCRL_OTA_OK;
    } else if (op == SCRL_OTA_OP_ABORT && len == 1u) {
        uint8_t status = s_active ? SCRL_OTA_ERR_ABORTED : SCRL_OTA_ERR_STATE;
        stop_update(SCRL_OTA_ERR_ABORTED);
        fill_event(event, SCRL_OTA_EVT_STOPPED, status);
    } else {
        fill_event(event, SCRL_OTA_EVT_STOPPED, SCRL_OTA_ERR_REQUEST);
    }
    return restart;
}

bool scrl_ota_data(const uint8_t *data, size_t len, uint8_t *event) {
    Lock lock;
    if (!s_active || len == 0u) return false;
    uint8_t status = SCRL_OTA_OK;
    if (len > s_size - s_received) {
        status = SCRL_OTA_ERR_SIZE;
    } else if (s_received == 0u) {
        // Check the image header before the first flash write. A small MTU
        // can cut the header: then check the magic byte only. esp_ota_end()
        // checks the complete image, chip ID included, in every case.
        esp_image_header_t header;
        if (data[0] != ESP_IMAGE_HEADER_MAGIC) {
            status = SCRL_OTA_ERR_IMAGE;
        } else if (len >= sizeof(header)) {
            memcpy(&header, data, sizeof(header));
            if (header.chip_id != (esp_chip_id_t)CONFIG_IDF_FIRMWARE_CHIP_ID) {
                status = SCRL_OTA_ERR_IMAGE;
            }
        }
        // The board descriptor too, when this first write holds it. end_update()
        // checks it again from flash in every case.
        if (status == SCRL_OTA_OK && len >= SCRL_BOARD_DESC_OFFSET + sizeof(ScrlBoardDesc)) {
            ScrlBoardDesc desc;
            memcpy(&desc, &data[SCRL_BOARD_DESC_OFFSET], sizeof(desc));
            if (!board_ok(desc)) status = SCRL_OTA_ERR_IMAGE;
        }
    }
    if (status == SCRL_OTA_OK && esp_ota_write(s_handle, data, len) != ESP_OK) {
        status = SCRL_OTA_ERR_FLASH;
    }
    if (status != SCRL_OTA_OK) {
        stop_update(status);
        fill_event(event, SCRL_OTA_EVT_STOPPED, status);
        return true;
    }
    mbedtls_sha256_update(&s_sha, data, len);
    s_received += (uint32_t)len;
    s_last_data_ms = scrl_millis();
    if (s_received - s_credit_sent >= SCRL_OTA_CREDIT_STEP || s_received == s_size) {
        s_credit_sent = s_received;
        fill_event(event, SCRL_OTA_EVT_CREDIT, SCRL_OTA_OK);
        return true;
    }
    return false;
}

void scrl_ota_link_lost(void) {
    Lock lock;
    stop_update(SCRL_OTA_ERR_LINK);
}

bool scrl_ota_tick(uint8_t *event) {
    Lock lock;
    uint32_t now = scrl_millis();
    if (s_active && (uint32_t)(now - s_last_data_ms) > SCRL_OTA_IDLE_MS) {
        stop_update(SCRL_OTA_ERR_TIMEOUT);
        fill_event(event, SCRL_OTA_EVT_STOPPED, SCRL_OTA_ERR_TIMEOUT);
        return true;
    }
    if (s_pending && !s_rollback_failed && (uint32_t)(now - s_boot_ms) > SCRL_OTA_CONFIRM_MS) {
        // Nobody confirmed the new image. Go back to the old one. This call
        // restarts the board and does not return when it works.
        ESP_LOGW(TAG, "no confirm in time: roll back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // No other valid image. Keep this one running in Listen-Only. A confirm
        // is still possible.
        s_rollback_failed = true;
        scrl_log("UPD  rollback not possible: no other valid image");
    }
    return false;
}
