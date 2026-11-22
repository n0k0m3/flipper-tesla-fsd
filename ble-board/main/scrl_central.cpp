/*
 * scrl_central.cpp — see scrl_central.h.
 */

#include "scrl_central.h"

#include <atomic>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nvs.h"

#include "scrl_logbuf.h"
#include "scrl_tsl6.h"

static const char *TAG = "scrl_central";

// A connect that the engine started. The engine hears of the link only after
// the encryption (a remote takes no CCCD write before it), or of its failure.
struct Pending {
    uint16_t conn;
    bool reported;  // TSL6_CEV_CONNECTED sent
    ble_addr_t addr;
};

static Pending s_links[SCRL_CENTRAL_REMOTES + 1];
static std::atomic<uint8_t> s_own_addr_type{BLE_OWN_ADDR_PUBLIC};
static std::atomic<bool> s_scanning;

// Pairing of a remote: legacy and no MITM for this pairing only (see the header).
static bool s_relaxed;
static uint8_t s_saved_sc_only, s_saved_mitm, s_saved_lvl;

// The addresses of the bonded remotes (NVS "central"/"rbonds").
static ble_addr_t s_remotes[SCRL_CENTRAL_REMOTES];
static uint8_t s_nremotes;
static nvs_handle_t s_nvs;
static bool s_nvs_open;

static void post(const tsl6_cev_t &e) {
    // Only the used part of data goes into the buffer.
    size_t n = offsetof(tsl6_cev_t, data) + (e.len < sizeof(e.data) ? e.len : sizeof(e.data));
    scrl_tsl6_ble_event(SCRL_TSL6_EV_CENTRAL, e.conn, (const uint8_t *)&e, n);
}

static tsl6_cev_t ev(uint8_t type, uint16_t conn) {
    tsl6_cev_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.conn = conn;
    return e;
}

static Pending *link_of(uint16_t conn) {
    for (Pending &p : s_links)
        if (p.conn == conn) return &p;
    return nullptr;
}

static void sm_relax(bool on) {
    if (on == s_relaxed) return;
    if (on) {
        s_saved_sc_only = ble_hs_cfg.sm_sc_only;
        s_saved_mitm = ble_hs_cfg.sm_mitm;
        s_saved_lvl = ble_hs_cfg.sm_sec_lvl;
        ble_hs_cfg.sm_sc_only = 0;
        ble_hs_cfg.sm_mitm = 0;
        ble_hs_cfg.sm_sec_lvl = 0;
    } else {
        ble_hs_cfg.sm_sc_only = s_saved_sc_only;
        ble_hs_cfg.sm_mitm = s_saved_mitm;
        ble_hs_cfg.sm_sec_lvl = s_saved_lvl;
    }
    s_relaxed = on;
}

// ── the list of remote bonds ──────────────────────────────────────────────────

static void remotes_save(void) {
    if (!s_nvs_open) return;
    nvs_set_blob(s_nvs, "rbonds", s_remotes, sizeof(ble_addr_t) * s_nremotes);
    nvs_commit(s_nvs);
}

void scrl_central_init(void) {
    s_nvs_open = nvs_open("central", NVS_READWRITE, &s_nvs) == ESP_OK;
    size_t n = sizeof(s_remotes);
    if (s_nvs_open && nvs_get_blob(s_nvs, "rbonds", s_remotes, &n) == ESP_OK) s_nremotes = (uint8_t)(n / sizeof(ble_addr_t));
    for (Pending &p : s_links) p.conn = BLE_HS_CONN_HANDLE_NONE;
}

bool scrl_central_is_remote(const ble_addr_t *peer) {
    for (int i = 0; i < s_nremotes; i++)
        if (ble_addr_cmp(&s_remotes[i], peer) == 0) return true;
    return false;
}

static void remote_add(const ble_addr_t *peer) {
    if (scrl_central_is_remote(peer)) return;
    if (s_nremotes == SCRL_CENTRAL_REMOTES) {  // the oldest goes; the engine keeps 3 remotes
        memmove(s_remotes, s_remotes + 1, sizeof(ble_addr_t) * (SCRL_CENTRAL_REMOTES - 1));
        s_nremotes--;
    }
    s_remotes[s_nremotes++] = *peer;
    remotes_save();
}

void scrl_central_set_own_addr_type(uint8_t type) { s_own_addr_type.store(type); }

// ── NimBLE callbacks (host task) ──────────────────────────────────────────────

static void report_connected(Pending *p, uint8_t status) {
    if (!p || p->reported) return;
    p->reported = true;
    tsl6_cev_t e = ev(TSL6_CEV_CONNECTED, p->conn);
    e.status = status;
    e.addr_type = p->addr.type;
    memcpy(e.addr, p->addr.val, 6);
    post(e);
}

static int central_gap(struct ble_gap_event *event, void *) {
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        uint16_t conn = event->connect.conn_handle;
        if (event->connect.status != 0 || ble_gap_conn_find(conn, &desc) != 0) {
            // The engine's connect failed: report a link that did not come up.
            tsl6_cev_t e = ev(TSL6_CEV_CONNECTED, BLE_HS_CONN_HANDLE_NONE);
            e.status = 1;
            post(e);
            return 0;
        }
        Pending *p = link_of(BLE_HS_CONN_HANDLE_NONE);
        if (!p) {
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        p->conn = conn;
        p->reported = false;
        p->addr = desc.peer_id_addr;
        // Encrypt: with the stored key for a bonded remote, else pair.
        bool bonded = desc.sec_state.bonded;
        if (!bonded) sm_relax(true);
        if (ble_gap_security_initiate(conn) != 0) {
            sm_relax(false);
            report_connected(p, 0);  // no encryption: the remote may still work
        }
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        sm_relax(false);
        Pending *p = link_of(event->enc_change.conn_handle);
        if (event->enc_change.status == 0 && ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
            desc.sec_state.bonded) {
            remote_add(&desc.peer_id_addr);
        }
        report_connected(p, 0);
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        sm_relax(false);
        uint16_t conn = event->disconnect.conn.conn_handle;
        Pending *p = link_of(conn);
        if (!p) return 0;
        if (!p->reported) {
            p->reported = true;
            tsl6_cev_t e = ev(TSL6_CEV_CONNECTED, conn);
            e.status = 1;
            post(e);
        } else {
            tsl6_cev_t e = ev(TSL6_CEV_DISCONNECTED, conn);
            e.status = (uint8_t)event->disconnect.reason;
            post(e);
        }
        p->conn = BLE_HS_CONN_HANDLE_NONE;
        return 0;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        tsl6_cev_t e = ev(TSL6_CEV_NOTIFY, event->notify_rx.conn_handle);
        e.val_handle = event->notify_rx.attr_handle;
        e.status = event->notify_rx.indication ? 1 : 0;
        uint16_t len = 0;
        ble_hs_mbuf_to_flat(event->notify_rx.om, e.data, sizeof(e.data), &len);
        e.len = len;
        post(e);
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // A remote has no display: any passkey works, as on the TSL6 (random).
        struct ble_sm_io io;
        memset(&io, 0, sizeof(io));
        io.action = event->passkey.params.action;
        if (io.action == BLE_SM_IOACT_DISP || io.action == BLE_SM_IOACT_INPUT) {
            io.passkey = esp_random() % 1000000u;
        } else if (io.action == BLE_SM_IOACT_NUMCMP) {
            io.numcmp_accept = 1;
        } else {
            return 0;
        }
        ble_sm_inject_io(event->passkey.conn_handle, &io);
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The remote lost its key: forget the old bond and pair again.
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        sm_relax(true);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    default:
        return 0;
    }
}

static int disc_cb(struct ble_gap_event *event, void *) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &event->disc;
        tsl6_cev_t e = ev(TSL6_CEV_SCAN_REPORT, BLE_HS_CONN_HANDLE_NONE);
        e.evt_type = d->event_type;
        e.addr_type = d->addr.type;
        memcpy(e.addr, d->addr.val, 6);
        e.rssi = d->rssi;
        e.len = d->length_data < sizeof(e.data) ? d->length_data : sizeof(e.data);
        memcpy(e.data, d->data, e.len);
        post(e);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_scanning.store(false);
        post(ev(TSL6_CEV_SCAN_DONE, BLE_HS_CONN_HANDLE_NONE));
    }
    return 0;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *) {
    if (error->status == 0 && chr) {
        tsl6_cev_t e = ev(TSL6_CEV_CHR, conn);
        e.handle = chr->def_handle;
        e.props = chr->properties;
        e.val_handle = chr->val_handle;
        e.uuid16 = chr->uuid.u.type == BLE_UUID_TYPE_16 ? chr->uuid.u16.value : 0;
        post(e);
        return 0;
    }
    post(ev(TSL6_CEV_DISC_DONE, conn));  // BLE_HS_EDONE, or an error: the discovery ends
    return 0;
}

static int write_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *, void *) {
    tsl6_cev_t e = ev(TSL6_CEV_WRITE_DONE, conn);
    e.status = error->status == 0 ? 0 : (uint8_t)(error->status & 0xFF ? error->status & 0xFF : 1);
    post(e);
    return 0;
}

static int read_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *) {
    tsl6_cev_t e = ev(TSL6_CEV_READ_DONE, conn);
    if (error->status == 0 && attr) {
        uint16_t len = 0;
        ble_hs_mbuf_to_flat(attr->om, e.data, sizeof(e.data), &len);
        e.len = len;
    } else {
        e.status = 1;
    }
    post(e);
    return 0;
}

// ── operations (CAN task) ─────────────────────────────────────────────────────

static int op_scan(uint32_t ms, int active) {
    if (s_scanning.load()) return 0;
    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.passive = active ? 0 : 1;
    p.filter_duplicates = 0;
    int rc = ble_gap_disc(s_own_addr_type.load(), (int32_t)ms, &p, disc_cb, nullptr);
    if (rc == 0) s_scanning.store(true);
    return rc;
}

static void op_scan_stop(void) {
    if (s_scanning.exchange(false)) ble_gap_disc_cancel();
    // NimBLE reports no "complete" after a cancel; the stock role does.
    post(ev(TSL6_CEV_SCAN_DONE, BLE_HS_CONN_HANDLE_NONE));
}

static int op_connect(uint8_t addr_type, const uint8_t addr[6]) {
    if (s_scanning.exchange(false)) ble_gap_disc_cancel();
    ble_addr_t peer;
    peer.type = addr_type;
    memcpy(peer.val, addr, 6);
    int rc = ble_gap_connect(s_own_addr_type.load(), &peer, 3000, nullptr, central_gap, nullptr);
    if (rc != 0) ESP_LOGW(TAG, "connect: rc %d", rc);
    return rc;
}

static void op_connect_cancel(void) { ble_gap_conn_cancel(); }

static int op_disconnect(uint16_t conn) {
    return link_of(conn) ? ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM) : 1;
}

static int op_rssi(uint16_t conn, int8_t *out) { return ble_gap_conn_rssi(conn, out); }

static int op_disc_chrs(uint16_t conn) { return ble_gattc_disc_all_chrs(conn, 1, 0xFFFF, chr_cb, nullptr); }

static int op_write(uint16_t conn, uint16_t handle, const uint8_t *v, uint16_t len) {
    return ble_gattc_write_flat(conn, handle, v, len, write_cb, nullptr);
}

static int op_read(uint16_t conn, uint16_t handle) { return ble_gattc_read(conn, handle, read_cb, nullptr); }

static void op_forget(uint8_t addr_type, const uint8_t addr[6]) {
    ble_addr_t peer;
    peer.type = addr_type;
    memcpy(peer.val, addr, 6);
    ble_store_util_delete_peer(&peer);
    for (int i = 0; i < s_nremotes; i++) {
        if (ble_addr_cmp(&s_remotes[i], &peer) != 0) continue;
        memmove(s_remotes + i, s_remotes + i + 1, sizeof(ble_addr_t) * (s_nremotes - i - 1));
        s_nremotes--;
        remotes_save();
        break;
    }
}

static const tsl6_central_ops_t s_ops = {op_scan, op_scan_stop, op_connect, op_connect_cancel, op_disconnect,
                                         op_rssi, op_disc_chrs, op_write, op_read, op_forget};

const tsl6_central_ops_t *scrl_central_ops(void) { return &s_ops; }
