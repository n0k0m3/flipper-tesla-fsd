/*
 * tsl6_central.c — the WCH BLE central API that the TSL6 remote button code
 * (m5/ble_central*.c, m6/central.c) uses, on the central role of the board
 * (hal->central, NimBLE in main/scrl_central.cpp).
 *
 * The stock code gets two kinds of results:
 *   - GAP events through its central callback table (gapCentralRoleCB_t):
 *     device found (0x0D), scan done (0x01), link up (0x05), link down (0x06),
 *     and the RSSI callback;
 *   - GATT client results as task messages (GATT_MSG_EVENT 0xB0): read by
 *     type (characteristic discovery, 0x09), read (0x0B), write (0x13),
 *     error (0x01), notification (0x1B) and indication (0x1D).
 * The board reports its NimBLE results with tsl6_central_event(). This file
 * builds the WCH layouts from them.
 *
 * Pairing: the board answers the passkey requests of a remote itself (a
 * random passkey, as the stock passcode callback does).
 */
#include <stdlib.h>
#include <string.h>

#include "tsl6_port.h"
#include "tsl6_engine.h"

#define GAP_DEVICE_INIT_DONE 0x00
#define GAP_DEVICE_DISCOVERY 0x01
#define GAP_LINK_ESTABLISHED 0x05
#define GAP_LINK_TERMINATED 0x06
#define GAP_DEVICE_INFO 0x0D
#define GATT_MSG_EVENT 0xB0
#define ATT_ERROR_RSP 0x01
#define ATT_READ_BY_TYPE_RSP 0x09
#define ATT_READ_RSP 0x0B
#define ATT_WRITE_REQ 0x12
#define ATT_WRITE_RSP 0x13
#define ATT_HANDLE_VALUE_NOTI 0x1B
#define ATT_HANDLE_VALUE_IND 0x1D
#define PROCEDURE_COMPLETE 0x1A
#define CONN_ALL 0xFFFFu
#define SCAN_MS_MIN 200u  /* the stock scan is 80 x 0.625 ms; a remote advertises less often */

typedef void (*rssi_cb_t)(uint32_t conn, uint32_t rssi);
typedef void (*central_cb_t)(uint8_t *ev);

/* WCH gapDeviceInfoEvent_t, as m5/ble_remote.h declares it. */
typedef struct {
    uint8_t event;
    uint8_t status;
    uint8_t opcode;
    uint8_t event_type;
    uint8_t addr_type;
    uint8_t addr[6];
    int8_t rssi;
    uint8_t data_len;
    uint8_t *data;
} dev_info_t;

/* WCH attWriteReq_t and attReadReq_t. */
typedef struct {
    uint16_t handle;
    uint16_t len;
    uint8_t *value;
    uint8_t sig;
    uint8_t cmd;
} write_req_t;

static uint8_t s_task = 0xFF;         /* central task (the GATT messages go there) */
static void *const *s_cbs;            /* gapCentralRoleCB_t: rssi, event, (unused) */
static uint32_t s_scan_ms = SCAN_MS_MIN;

static const tsl6_central_ops_t *ops(void) { return port_hal ? port_hal->central : NULL; }

static void central_event(uint8_t *ev)
{
    if (s_cbs && s_cbs[1])
        ((central_cb_t)s_cbs[1])(ev);
}

void tsl6_central_reset(void)
{
    s_task = 0xFF;
    s_cbs = NULL;
    s_scan_ms = SCAN_MS_MIN;
}

/* ---- GAP ---- */

uint8_t port_GAP_SetParamValue(uint32_t id, uint32_t value)
{
    if ((id & 0xFFFF) == 2) {  /* TGAP_GEN_DISC_SCAN, 0.625 ms units */
        uint32_t ms = (value & 0xFFFF) * 5u / 8u;
        s_scan_ms = ms < SCAN_MS_MIN ? SCAN_MS_MIN : ms;
    }
    return 0;
}

/* The board sets its own pairing parameters. 0x417 (GAPBOND_ERASE_SINGLEBOND,
   address type + address) deletes the bond of a remote: the stock code sends
   it before it pairs a remote, and when the phone removes one. */
uint8_t port_GAPBondMgr_SetParameter(uint32_t param, uint32_t len, void *value)
{
    const uint8_t *v = (const uint8_t *)value;
    if ((param & 0xFFFF) == 0x417 && (len & 0xFF) == 7 && ops() && ops()->forget)
        ops()->forget(v[0], v + 1);
    return 0;
}

uint8_t port_GAPBondMgr_PasscodeRsp(uint32_t conn, uint32_t status, uint32_t passcode)
{
    (void)conn; (void)status; (void)passcode;  /* the board answers the passkey itself */
    return 0;
}

uint8_t port_GATT_InitClient(void) { return 0; }
void port_GATT_RegisterForInd(uint32_t task) { (void)task; }

/* Starts the central role. The stock role reports "init done" next. */
uint8_t port_GAPRole_CentralStartDevice(uint32_t task, void *bond_cbs, void *central_cbs)
{
    (void)bond_cbs;
    s_task = (uint8_t)task;
    s_cbs = (void *const *)central_cbs;
    if (!ops())
        return 1;  /* no central role on this board: the remote code waits */
    uint8_t ev[4] = {0, 0, GAP_DEVICE_INIT_DONE, 0};
    central_event(ev);
    return 0;
}

uint8_t port_GAPRole_CentralStartDiscovery(uint32_t mode, uint32_t active, uint32_t white_list)
{
    (void)mode; (void)white_list;
    if (!ops() || !ops()->scan)
        return 1;
    return ops()->scan(s_scan_ms, active & 0xFF) == 0 ? 0 : 1;
}

/* The stock role reports "scan done" after a cancel too: the board does. */
uint8_t port_GAPRole_CentralCancelDiscovery(void)
{
    if (ops() && ops()->scan_stop)
        ops()->scan_stop();
    return 0;
}

uint8_t port_GAPRole_ObserverCancelDiscovery(void) { return port_GAPRole_CentralCancelDiscovery(); }

uint8_t port_GAPRole_CentralEstablishLink(uint32_t high_duty, uint32_t white_list, uint32_t addr_type, uint8_t *addr)
{
    (void)high_duty; (void)white_list;
    if (!ops() || !ops()->connect)
        return 1;
    return ops()->connect((uint8_t)addr_type, addr) == 0 ? 0 : 1;
}

/* 0xFFFF: stop a connect that runs (the stock time-out). Else drop the link. */
uint8_t port_GAPRole_TerminateLink(uint32_t conn)
{
    if (!ops())
        return 1;
    conn &= 0xFFFF;
    if (conn == CONN_ALL) {
        if (ops()->connect_cancel)
            ops()->connect_cancel();
        return 0;
    }
    return ops()->disconnect && ops()->disconnect((uint16_t)conn) == 0 ? 0 : 1;
}

/* The stock role calls the RSSI callback later; the board has the value now. */
uint8_t port_GAPRole_ReadRssiCmd(uint32_t conn)
{
    int8_t rssi;
    if (!ops() || !ops()->rssi || ops()->rssi((uint16_t)conn, &rssi) != 0)
        return 1;
    if (s_cbs && s_cbs[0])
        ((rssi_cb_t)s_cbs[0])(conn & 0xFFFF, (uint32_t)(int32_t)rssi);
    return 0;
}

/* ---- GATT client ---- */

/* The stock code allocates the value of a write here. */
void *port_GATT_bm_alloc(uint32_t conn, uint32_t opcode, uint32_t size, uint16_t *got, uint32_t flag)
{
    (void)conn; (void)opcode; (void)flag;
    void *p = calloc(1, (size & 0xFFFF) ? (size & 0xFFFF) : 1);
    if (got)
        *got = p ? (uint16_t)size : 0;
    return p;
}

/* The values of the messages below sit inside the message: the message free
   frees them. A write value is freed by GATT_WriteCharValue. */
void port_GATT_bm_free(void *msg, uint32_t opcode) { (void)msg; (void)opcode; }

uint8_t port_GATT_DiscAllChars(uint32_t conn, uint32_t start, uint32_t end, uint32_t task)
{
    (void)start; (void)end; (void)task;
    if (!ops() || !ops()->disc_chrs)
        return 1;
    return ops()->disc_chrs((uint16_t)conn) == 0 ? 0 : 1;
}

uint8_t port_GATT_WriteCharValue(uint32_t conn, void *req, uint32_t task)
{
    (void)task;
    write_req_t *r = (write_req_t *)req;
    int rc = 1;
    if (ops() && ops()->write)
        rc = ops()->write((uint16_t)conn, r->handle, r->value, r->len);
    free(r->value);  /* WCH frees the value of a write that it takes */
    r->value = NULL;
    return rc == 0 ? 0 : 1;
}

uint8_t port_GATT_ReadCharValue(uint32_t conn, void *req, uint32_t task)
{
    (void)task;
    uint16_t handle = *(const uint16_t *)req;
    if (!ops() || !ops()->read)
        return 1;
    return ops()->read((uint16_t)conn, handle) == 0 ? 0 : 1;
}

/* A GATT_MSG_EVENT to the central task: header, then the attribute fields at
   +8 (u16), +10 (u16) and a value pointer at +12. The value sits at +16. */
static void gatt_msg(uint8_t status, uint16_t conn, uint8_t method, uint16_t f8, uint16_t f10,
                     const uint8_t *data, uint16_t len)
{
    if (s_task == 0xFF)
        return;
    uint8_t *m = port_tmos_msg_allocate(16u + len);
    if (!m)
        return;
    m[0] = GATT_MSG_EVENT;
    m[1] = status;
    memcpy(m + 2, &conn, 2);
    m[4] = method;
    memcpy(m + 8, &f8, 2);
    memcpy(m + 10, &f10, 2);
    uint8_t *value = m + 16;
    memcpy(m + 12, &value, sizeof(value));
    if (len)
        memcpy(value, data, len);
    port_tmos_msg_send(s_task, m);
}

/* ---- results from the board ---- */

void tsl6_central_event(const tsl6_cev_t *e)
{
    switch (e->type) {
    case TSL6_CEV_SCAN_REPORT: {
        dev_info_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.opcode = GAP_DEVICE_INFO;
        ev.event_type = e->evt_type;
        ev.addr_type = e->addr_type;
        memcpy(ev.addr, e->addr, 6);
        ev.rssi = e->rssi;
        ev.data_len = (uint8_t)(e->len > sizeof(e->data) ? sizeof(e->data) : e->len);
        ev.data = (uint8_t *)e->data;
        central_event((uint8_t *)&ev);
        break;
    }
    case TSL6_CEV_SCAN_DONE: {
        uint8_t ev[8] = {0, 0, GAP_DEVICE_DISCOVERY};
        central_event(ev);
        break;
    }
    case TSL6_CEV_CONNECTED: {
        /* gapEstLinkReqEvent_t: status at 1, address at 4, handle at 10. */
        uint8_t ev[20];
        memset(ev, 0, sizeof(ev));
        ev[1] = e->status;
        ev[2] = GAP_LINK_ESTABLISHED;
        ev[3] = e->addr_type;
        memcpy(ev + 4, e->addr, 6);
        memcpy(ev + 10, &e->conn, 2);
        central_event(ev);
        break;
    }
    case TSL6_CEV_DISCONNECTED: {
        /* gapTerminateLinkEvent_t: handle at 4, reason at 6. */
        uint8_t ev[8] = {0, 0, GAP_LINK_TERMINATED};
        memcpy(ev + 4, &e->conn, 2);
        ev[6] = e->status;
        central_event(ev);
        break;
    }
    case TSL6_CEV_CHR: {
        /* One attReadByTypeRsp pair of 7 bytes: declaration handle, properties,
           value handle, 16-bit UUID. */
        uint8_t r[7];
        memcpy(r, &e->handle, 2);
        r[2] = e->props;
        memcpy(r + 3, &e->val_handle, 2);
        memcpy(r + 5, &e->uuid16, 2);
        gatt_msg(0, e->conn, ATT_READ_BY_TYPE_RSP, 1, 7, r, 7);
        break;
    }
    case TSL6_CEV_DISC_DONE:
        gatt_msg(PROCEDURE_COMPLETE, e->conn, ATT_READ_BY_TYPE_RSP, 0, 7, NULL, 0);
        break;
    case TSL6_CEV_WRITE_DONE:
        if (e->status == 0) {
            gatt_msg(0, e->conn, ATT_WRITE_RSP, 0, 0, NULL, 0);
        } else {
            /* attErrorRsp_t: request opcode at +8. */
            gatt_msg(0, e->conn, ATT_ERROR_RSP, ATT_WRITE_REQ, e->status, NULL, 0);
        }
        break;
    case TSL6_CEV_READ_DONE:
        if (e->status == 0)
            gatt_msg(0, e->conn, ATT_READ_RSP, e->len, 0, e->data, e->len);
        break;
    case TSL6_CEV_NOTIFY:
        gatt_msg(0, e->conn, e->status ? ATT_HANDLE_VALUE_IND : ATT_HANDLE_VALUE_NOTI, e->val_handle, e->len,
                 e->data, e->len);
        break;
    default:
        break;
    }
}
