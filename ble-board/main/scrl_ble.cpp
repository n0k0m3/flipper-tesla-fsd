/*
 * scrl_ble.cpp — see scrl_ble.h.
 *
 * The NimBLE host task runs the GAP and GATT callbacks. The housekeeping task
 * runs scrl_ble_tick(). The few values that both touch are atomic.
 */

#include "scrl_ble.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "scrl_app.h"
#include "scrl_board.h"
#include "scrl_logbuf.h"
#include "scrl_log.h"
#include "scrl_ota.h"
#include "scrl_passkey.h"
#include "scrl_proc.h"
#include "scrl_settings.h"
#include "scrl_status.h"

extern "C" void ble_store_config_init(void);

static const char *TAG = "scrl_ble";

// e591xxxx-51ba-4d0b-8422-5cb9dc778552, least significant byte first.
#define SCRL_UUID(n)                                                                       \
    BLE_UUID128_INIT(0x52, 0x85, 0x77, 0xdc, 0xb9, 0x5c, 0x22, 0x84, 0x0b, 0x4d, 0xba, 0x51, \
                     (n), 0x00, 0x91, 0xe5)

static const ble_uuid128_t UUID_SERVICE = SCRL_UUID(0x01);
static const ble_uuid128_t UUID_INFO = SCRL_UUID(0x02);
static const ble_uuid128_t UUID_STATUS = SCRL_UUID(0x03);
static const ble_uuid128_t UUID_LIVE = SCRL_UUID(0x04);
static const ble_uuid128_t UUID_SETTINGS = SCRL_UUID(0x05);
static const ble_uuid128_t UUID_COMMAND = SCRL_UUID(0x06);
static const ble_uuid128_t UUID_LOG = SCRL_UUID(0x07);
static const ble_uuid128_t UUID_OTA_CTRL = SCRL_UUID(0x08);
static const ble_uuid128_t UUID_OTA_DATA = SCRL_UUID(0x09);

enum ChrId : intptr_t {
    CHR_INFO, CHR_STATUS, CHR_LIVE, CHR_SETTINGS, CHR_COMMAND, CHR_LOG, CHR_OTA_CTRL, CHR_OTA_DATA
};

// Advertise once per second (unit 0.625 ms).
#define ADV_INTERVAL_UNITS 1600
// Log lines sent per tick, so a replay of the ring does not flood the link.
#define LOG_LINES_PER_TICK 4
// Longest value that a write may carry (ATT limit).
#define WRITE_MAX 512

#define READ_AUTH (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN)
#define WRITE_AUTH_FLAGS (BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define NOTIFY_AUTH \
    (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN)

static uint16_t s_h_info, s_h_status, s_h_live, s_h_settings, s_h_command, s_h_log, s_h_ota_ctrl,
    s_h_ota_data;

static uint32_t s_passkey;
static uint8_t s_own_addr_type;
static char s_name[12];

static std::atomic<uint16_t> s_conn{BLE_HS_CONN_HANDLE_NONE};
static std::atomic<bool> s_authenticated;
static std::atomic<uint32_t> s_conn_ms;
static std::atomic<bool> s_sub_live, s_sub_log, s_sub_ota;
static std::atomic<uint32_t> s_pair_until_ms;  // the pairing window closes at this time
static std::atomic<bool> s_pair_open;

static uint32_t s_log_cursor;    // next log line to notify (housekeeping task)
static uint32_t s_live_last_ms;

// Status JSON of the last read at offset 0. A long read takes several requests
// and every one must see the same text.
static char s_status_json[SCRL_STATUS_JSON_MAX + 1u];
static size_t s_status_len;

static void advertise(void);

// ── helpers ───────────────────────────────────────────────────────────────────

bool scrl_ble_pairing_open(void) {
    if (!s_pair_open.load()) return false;
    if ((int32_t)(scrl_millis() - s_pair_until_ms.load()) >= 0) {
        s_pair_open.store(false);
        return false;
    }
    return true;
}

void scrl_ble_open_pairing(uint32_t duration_ms) {
    s_pair_until_ms.store(scrl_millis() + duration_ms);
    s_pair_open.store(true);
}

static bool link_authenticated(uint16_t conn) {
    struct ble_gap_conn_desc desc;
    if (conn == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(conn, &desc) != 0) return false;
    return desc.sec_state.encrypted && desc.sec_state.authenticated;
}

// Send one notification, cut to what the MTU carries. Only on an authenticated link.
static void notify(uint16_t handle, const void *data, size_t len) {
    uint16_t conn = s_conn.load();
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_authenticated.load()) return;
    uint16_t mtu = ble_att_mtu(conn);
    if (mtu < 23u) return;
    if (len > (size_t)(mtu - 3u)) len = mtu - 3u;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om) ble_gatts_notify_custom(conn, handle, om);
}

void scrl_ble_forget_bonds(void) {
    ble_store_clear();
    uint16_t conn = s_conn.load();
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    scrl_log("BLE  bonds deleted");
}

// ── GATT ──────────────────────────────────────────────────────────────────────

static int read_info(struct os_mbuf *om) {
    const esp_app_desc_t *app = esp_app_get_description();
    const ScrlBootInfo *boot = scrl_app_boot_info();
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    char sha[9];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    char json[320];
    int n = snprintf(json, sizeof(json),
                     "{\"fw\":\"%s\",\"build\":\"%s\",\"built\":\"%s %s\",\"idf\":\"%s\","
                     "\"board\":\"%s\",\"chip\":\"%s\",\"can\":\"%s\","
                     "\"id\":\"%02x%02x%02x%02x%02x%02x\",\"rst_log\":[%u,%u,%u,%u]}",
                     app->version, sha, app->date, app->time, app->idf_ver,
                     CONFIG_SCRL_BOARD_ID, CONFIG_IDF_TARGET, SCRL_CAN_KIND,
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                     (unsigned)boot->reset_log[0], (unsigned)boot->reset_log[1],
                     (unsigned)boot->reset_log[2], (unsigned)boot->reset_log[3]);
    if (n <= 0 || (size_t)n >= sizeof(json)) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(om, json, (uint16_t)n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_status(struct os_mbuf *om, uint16_t offset) {
    if (offset == 0u || s_status_len == 0u) {
        ScrlStatus st;
        scrl_app_collect_status(&st);
        s_status_len = scrl_status_json(st, s_status_json, sizeof(s_status_json));
        if (s_status_len == 0u) return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(om, s_status_json, (uint16_t)s_status_len) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_live(struct os_mbuf *om) {
    ScrlStatus st;
    scrl_app_collect_status(&st);
    uint8_t rec[SCRL_LIVE_RECORD_LEN];
    scrl_live_pack(st, rec);
    return os_mbuf_append(om, rec, sizeof(rec)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_settings(struct os_mbuf *om) {
    char json[160];
    size_t n = scrl_settings_to_json(scrl_proc_get_settings(), json, sizeof(json));
    if (n == 0u) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(om, json, (uint16_t)n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int write_settings(const uint8_t *data, size_t len) {
    ScrlSettings s = scrl_proc_get_settings();
    bool op_mode_named = false;
    ScrlSettingsParse result =
        scrl_settings_from_json((const char *)data, len, &s, &op_mode_named);
    if (result != ScrlSettingsParse::Ok) {
        scrl_log("SET  write refused, error %u", (unsigned)result);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    scrl_app_apply_settings(s, op_mode_named);
    scrl_log("SET  on=%u mode=%u pat=%u dly=%u gap=%u int=%u op=%u", (unsigned)s.enabled,
             (unsigned)s.mode, (unsigned)s.pattern, (unsigned)s.delay_ms, (unsigned)s.gap_ms,
             (unsigned)s.interval_ms, (unsigned)s.op_mode);
    return 0;
}

static int write_command(const uint8_t *data, size_t len) {
    if (len < 1u) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (data[0] == SCRL_CMD_SET_PASSKEY) {
        uint32_t passkey = 0;
        if (!scrl_passkey_from_digits(&data[1], len - 1u, &passkey)) {
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        // Save first. If the save fails, the old passkey stays in use, so the
        // board and its label cannot differ after the next start.
        if (!scrl_app_set_passkey(passkey)) return BLE_ATT_ERR_UNLIKELY;
        s_passkey = passkey;  // for the next pairing; the bonds do not change
        scrl_log("CMD  passkey changed");  // never the digits: the log goes out over BLE
        return 0;
    }
    if (len != 1u) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    switch (data[0]) {
    case SCRL_CMD_RESET_COUNTERS:
        scrl_app_reset_counters();
        scrl_log("CMD  counters reset");
        return 0;
    case SCRL_CMD_REBOOT:
        scrl_log("CMD  restart");
        scrl_app_request_reboot(500);
        return 0;
    case SCRL_CMD_FORGET_BONDS:
        scrl_ble_forget_bonds();
        return 0;
    case SCRL_CMD_FACTORY_RESET:
        scrl_log("CMD  factory reset");
        scrl_app_factory_reset();
        return 0;
    case SCRL_CMD_CONFIRM_IMAGE:
        // This write arrived on an authenticated link: BLE, pairing and the
        // path for the next update all work in the new image.
        return scrl_ota_confirm() ? 0 : BLE_ATT_ERR_UNLIKELY;
    default:
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static int access_cb(uint16_t conn, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    ChrId id = (ChrId)(intptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (id) {
        case CHR_INFO: return read_info(ctxt->om);
        case CHR_STATUS: return read_status(ctxt->om, ctxt->offset);
        case CHR_LIVE: return read_live(ctxt->om);
        case CHR_SETTINGS: return read_settings(ctxt->om);
        default: return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    // The stack already checked encryption and authentication from the flags.
    // Check again: a write here can change what the board sends on the car bus.
    if (!link_authenticated(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;

    static uint8_t buf[WRITE_MAX];  // host task only
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t event[SCRL_OTA_EVENT_LEN];
    switch (id) {
    case CHR_SETTINGS:
        return write_settings(buf, len);
    case CHR_COMMAND:
        return write_command(buf, len);
    case CHR_OTA_CTRL: {
        bool restart = scrl_ota_control(buf, len, event);
        if (s_sub_ota.load()) notify(s_h_ota_ctrl, event, sizeof(event));
        if (restart) scrl_app_request_reboot(1500);
        return 0;
    }
    case CHR_OTA_DATA:
        if (scrl_ota_data(buf, len, event) && s_sub_ota.load()) {
            notify(s_h_ota_ctrl, event, sizeof(event));
        }
        return 0;
    default:
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
}

// Every member is named, in declaration order, so the table has no silent defaults.
#define SCRL_CHR(uuid_, id_, flags_, handle_)                                              \
    {.uuid = &(uuid_).u, .access_cb = access_cb, .arg = (void *)(id_), .descriptors = nullptr, \
     .flags = (flags_), .min_key_size = 16, .val_handle = &(handle_), .cpfd = nullptr}

static const struct ble_gatt_chr_def s_chrs[] = {
    SCRL_CHR(UUID_INFO, CHR_INFO, BLE_GATT_CHR_F_READ, s_h_info),
    SCRL_CHR(UUID_STATUS, CHR_STATUS, READ_AUTH, s_h_status),
    SCRL_CHR(UUID_LIVE, CHR_LIVE, READ_AUTH | NOTIFY_AUTH, s_h_live),
    SCRL_CHR(UUID_SETTINGS, CHR_SETTINGS, READ_AUTH | BLE_GATT_CHR_F_WRITE | WRITE_AUTH_FLAGS,
             s_h_settings),
    SCRL_CHR(UUID_COMMAND, CHR_COMMAND, BLE_GATT_CHR_F_WRITE | WRITE_AUTH_FLAGS, s_h_command),
    SCRL_CHR(UUID_LOG, CHR_LOG, NOTIFY_AUTH, s_h_log),
    SCRL_CHR(UUID_OTA_CTRL, CHR_OTA_CTRL, BLE_GATT_CHR_F_WRITE | WRITE_AUTH_FLAGS | NOTIFY_AUTH,
             s_h_ota_ctrl),
    SCRL_CHR(UUID_OTA_DATA, CHR_OTA_DATA, BLE_GATT_CHR_F_WRITE_NO_RSP | WRITE_AUTH_FLAGS,
             s_h_ota_data),
    {.uuid = nullptr, .access_cb = nullptr, .arg = nullptr, .descriptors = nullptr, .flags = 0,
     .min_key_size = 0, .val_handle = nullptr, .cpfd = nullptr},
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID_SERVICE.u, .includes = nullptr,
     .characteristics = s_chrs},
    {.type = 0, .uuid = nullptr, .includes = nullptr, .characteristics = nullptr},
};

// ── GAP ───────────────────────────────────────────────────────────────────────

static void link_down(void) {
    s_conn.store(BLE_HS_CONN_HANDLE_NONE);
    s_authenticated.store(false);
    s_sub_live.store(false);
    s_sub_log.store(false);
    s_sub_ota.store(false);
    scrl_ota_link_lost();
}

static int gap_event(struct ble_gap_event *event, void *) {
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn.store(event->connect.conn_handle);
            s_conn_ms.store(scrl_millis());
            s_authenticated.store(false);
            scrl_log("BLE  connected");
        } else {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        scrl_log("BLE  disconnected, reason %d", event->disconnect.reason);
        link_down();
        advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0) return 0;
        if (event->enc_change.status == 0 && desc.sec_state.encrypted &&
            desc.sec_state.authenticated) {
            s_authenticated.store(true);
            scrl_log("BLE  link authenticated%s", desc.sec_state.bonded ? ", bonded" : "");
        } else {
            // Encrypted without the passkey (Just Works), or a failure. Such a
            // link gets nothing. Do not keep its bond either.
            s_authenticated.store(false);
            if (event->enc_change.status == 0) ble_store_util_delete_peer(&desc.peer_id_addr);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
            scrl_log("BLE  link refused: not authenticated (status %d)", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // A new pairing. A bonded phone does not come here: it has its key.
        if (event->passkey.params.action == BLE_SM_IOACT_DISP && scrl_ble_pairing_open()) {
            struct ble_sm_io io = {};
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = s_passkey;
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            // The owner now types the passkey. Give the full time limit again.
            s_conn_ms.store(scrl_millis());
            scrl_log("BLE  pairing started");
        } else {
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
            scrl_log("BLE  pairing refused: %s",
                     scrl_ble_pairing_open() ? "method" : "pairing window is closed");
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The phone lost its key and pairs again. Allow it only in the window.
        if (!scrl_ble_pairing_open()) return BLE_GAP_REPEAT_PAIRING_IGNORE;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        bool on = event->subscribe.cur_notify != 0;
        if (event->subscribe.attr_handle == s_h_live) {
            s_sub_live.store(on);
        } else if (event->subscribe.attr_handle == s_h_log) {
            s_sub_log.store(on);
        } else if (event->subscribe.attr_handle == s_h_ota_ctrl) {
            s_sub_ota.store(on);
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void advertise(void) {
    if (s_conn.load() != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;

    // Advertising data: flags and the service UUID. The name goes into the scan
    // response: both do not fit into 31 bytes.
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &UUID_SERVICE;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);

    struct ble_hs_adv_fields rsp = {};
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = ADV_INTERVAL_UNITS;
    params.itvl_max = ADV_INTERVAL_UNITS;
    if (rc == 0) {
        rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER, &params, gap_event,
                               nullptr);
    }
    if (rc != 0) ESP_LOGE(TAG, "advertising failed: rc=%d", rc);
}

// A bond or a subscription record did not fit into the store. The stock
// handler deletes the oldest bond to make room. Never do that: a stranger could
// push the owner's phones out that way. The write fails, and the owner deletes
// bonds on purpose (button or command).
static int store_status(struct ble_store_status_event *event, void *arg) {
    if (event->event_code == BLE_STORE_EVENT_OVERFLOW) {
        scrl_log("BLE  bond store is full - nothing deleted, record not kept");
        return BLE_HS_ESTORE_CAP;
    }
    return ble_store_util_status_rr(event, arg);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no BLE address: rc=%d", rc);
        return;
    }
    uint8_t addr[6] = {};
    ble_hs_id_copy_addr(s_own_addr_type, addr, nullptr);
    snprintf(s_name, sizeof(s_name), "SCRL-%02X%02X", addr[1], addr[0]);
    ble_svc_gap_device_name_set(s_name);

    // 0 dBm keeps the TX current peak low. The phone is about 1 m away.
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N0);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N0);

    // A board with no bond is of no use to its owner: let the first phone pair
    // after any kind of start.
    int bonds = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &bonds) == 0 && bonds == 0) {
        scrl_ble_open_pairing(SCRL_BLE_PAIR_WINDOW_BOOT_MS);
    }
    scrl_log("BLE  ready, name %s, %d bond(s), pairing window %s", s_name, bonds,
             scrl_ble_pairing_open() ? "open" : "closed");
    advertise();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset, reason %d", reason);
    link_down();
}

static void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void scrl_ble_init(uint32_t passkey, bool power_on) {
    s_passkey = passkey;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE start failed: %s. The board runs without BLE", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = store_status;

    // LE Secure Connections with a passkey (MITM protection) and bonding.
    // Level 4 refuses a pairing request without the SC or the MITM flag.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_sec_lvl = 4;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT table failed: rc=%d", rc);
        return;
    }
    ble_store_config_init();
    if (power_on) scrl_ble_open_pairing(SCRL_BLE_PAIR_WINDOW_BOOT_MS);
    nimble_port_freertos_init(host_task);
}

void scrl_ble_tick(void) {
    uint32_t now = scrl_millis();
    // The update time limits run with or without a phone.
    uint8_t event[SCRL_OTA_EVENT_LEN];
    if (scrl_ota_tick(event) && s_sub_ota.load()) notify(s_h_ota_ctrl, event, sizeof(event));

    uint16_t conn = s_conn.load();
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;

    if (!s_authenticated.load()) {
        // A link that stays unauthenticated blocks the one connection slot.
        if ((uint32_t)(now - s_conn_ms.load()) > SCRL_BLE_AUTH_TIMEOUT_MS) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            s_conn_ms.store(now);  // one terminate per timeout
            scrl_log("BLE  dropped: no authenticated link in 30 s");
        }
        s_log_cursor = scrl_log_oldest_seq();  // the phone gets the whole ring on subscribe
        return;
    }

    if (s_sub_live.load() && (uint32_t)(now - s_live_last_ms) >= 1000u) {
        s_live_last_ms = now;
        ScrlStatus st;
        scrl_app_collect_status(&st);
        uint8_t rec[SCRL_LIVE_RECORD_LEN];
        scrl_live_pack(st, rec);
        notify(s_h_live, rec, sizeof(rec));
    }

    if (s_sub_log.load()) {
        uint32_t oldest = scrl_log_oldest_seq();
        if ((int32_t)(s_log_cursor - oldest) < 0) s_log_cursor = oldest;
        char line[SCRL_LOG_LINE_MAX];
        for (int i = 0; i < LOG_LINES_PER_TICK && scrl_log_read(s_log_cursor, line, sizeof(line)); i++) {
            notify(s_h_log, line, strlen(line));
            s_log_cursor++;
        }
    }
}
