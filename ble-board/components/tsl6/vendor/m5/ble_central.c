/* BLE central role: link slots, pairing and bond records, scan reports to
   the phone (stock 0x1092A..0x11046, 0x11238..0x116BC). */
#include "tsl6.h"
#include "m5.h"
#include "ble_remote.h"

/* Resident slot 0x0F4. */
static inline uint8_t gatt_read_char_value(uint16_t conn, void *req, uint8_t task)
{
    return GATT_ReadCharValue(conn, req, task);
}

/* Stock 0x1092A. Central RSSI callback (table 0x196A4, entry 0): stores the
   RSSI in the slot of the link. */
void remote_rssi_cb(uint32_t conn, uint32_t rssi)
{
    uint32_t i;

    if (g_2000c008[0].conn == conn)
        i = 0;
    else if (g_2000c008[1].conn == conn)
        i = 1;
    else if (g_2000c008[2].conn == conn)
        i = 2;
    else
        return;
    g_2000c008[i].rssi = (int8_t)rssi;
}

/* Stock 0x1096E. Writes 0x0001 (notifications on) to a CCCD handle. */
M5_CALLED void remote_cccd_enable(uint32_t conn, uint32_t handle)
{
    uint16_t on = 1;
    att_write_req_t req;

    req.sig = 0;
    req.cmd = 0;
    req.len = 2;
    req.handle = (uint16_t)handle;
    req.value = RES32(0x128, void *, (uint32_t, uint32_t, uint32_t, void *, uint32_t),
                      conn, 0x12, 2, 0, 0);          /* GATT_bm_alloc */
    if (req.value == 0)
        return;
    tmos_memcpy(req.value, &on, 2);
    if (RES32(0x10C, uint8_t, (uint32_t, void *, uint32_t),
              conn, &req, g_2000bff0) != 0)          /* GATT_WriteCharValue */
        GATT_bm_free(&req, 0x12);
}

/* Stock 0x109D4. Finds an AD structure of the given type in the advertising
   data. Returns 0 and the data pointer and length when found, else 1. */
M5_CALLED uint32_t adv_find(gap_dev_info_t *ev, uint32_t type, uint8_t **ptr, uint8_t *len)
{
    uint8_t *data = ev->data;
    uint32_t n = ev->data_len;
    uint32_t i = 0;

    *ptr = 0;
    *len = 0;
    while (i < n) {
        uint8_t *p = data + i;
        uint32_t l = p[0];
        if (l == 0)
            return 1;
        if ((int32_t)n < (int32_t)(i + l))
            return 1;
        if (p[1] == type) {
            *ptr = data + i + 2;
            *len = (uint8_t)(l - 1);
            return 0;
        }
        i = (i + l + 1) & 0xFF;
    }
    return 1;
}

/* Stock 0x10A22. Empty bond state callback (table 0x19698, entry 1). */
void remote_pair_state_cb(void)
{
}

/* Stock 0x10A24. Passcode callback (table 0x19698, entry 0): answers with a
   random passcode. Remotes have no display, so any passcode works. */
uint32_t remote_passcode_cb(uint8_t *addr, uint32_t conn)
{
    uint32_t r = tmos_rand();
    return RES32(0x170, uint8_t, (uint32_t, uint32_t, uint32_t),
                 conn, 0, r % 1000000);              /* GAPBondMgr_PasscodeRsp */
}

/* Stock 0x10A54. Ends pairing: drops the link and frees the context. */
M5_CALLED void pair_end(void)
{
    if (g_2000bff8 == 0)
        return;
    GAPRole_TerminateLink(g_2000bff8->conn);
    tmos_msg_deallocate((uint8_t *)g_2000bff8);
    g_2000bff8 = 0;
}

/* Stock 0x10A8E. Returns the first free link slot, or 3 when none is free. */
M5_CALLED uint32_t link_free_slot(void)
{
    if (g_2000c008[0].conn == CONN_FREE)
        return 0;
    if (g_2000c008[1].conn == CONN_FREE)
        return 1;
    if (g_2000c008[2].conn == CONN_FREE)
        return 2;
    return 3;
}

/* Stock 0x10AC0. Starts a scan (event 2 in 10 ms) when one is asked for,
   when the key table has a key, or when a slot is free and a remote is bonded. */
M5_CALLED void scan_if_needed(void)
{
    g_2000bffd = 0;
    if (g_2000bffc != 0)
        goto scan;
    key_entry_t *k = g_20004078;
    for (uint32_t i = 0; i < KEY_N; i++) {
        if ((((uint32_t)k[i].code_hi << 16) | k[i].code_lo) != 0)
            goto scan;
    }
    if (link_free_slot() > 2)
        return;
    for (uint32_t off = 0; off != 3 * sizeof(remote_rec_t); off += sizeof(remote_rec_t)) {
        if (tmos_isbufset((uint8_t *)g_2000407c + off + 3, 0, 6) == 0)
            goto scan;
    }
    return;
scan:
    tmos_start_task(g_2000bff0, 2, 16);
}

/* Stock 0x10B76. Sends the pairing result to the phone: F0 frame 06 00 code
   (1 = paired, 2 = failed or link lost, 3 = stopped). Also ends a scan report. */
M5_CALLED void pair_result_send(uint32_t code)
{
    uint8_t msg[4];

    msg[2] = (uint8_t)code;
    msg[0] = 6;
    msg[1] = 0;
    g_2000bff4 = 0;
    ble_send_any(g_2000bff6, 0xF0, msg, 3);
}

/* Stock 0x10BA6. Next step of the CCCD set-up of a link (after a write
   response, or after the link comes up).
   Not pairing: enables the next non-zero handle of the bonded record.
   Pairing: enables the next handle found by the discovery. When all are
   done, it saves the new remote in a free record (or record 0) and ends
   pairing. done = 0 means the last write failed: that handle is dropped. */
M5_CALLED void remote_cccd_next(uint32_t slot, uint32_t done)
{
    remote_link_t *lk = (remote_link_t *)((uint8_t *)g_2000c008 + slot * 12);

    if (g_2000bff8 == 0) {
        for (uint32_t off = 0; off != 3 * sizeof(remote_rec_t); off += sizeof(remote_rec_t)) {
            if (tmos_memcmp((uint8_t *)g_2000407c + off + 3, lk->addr, 6) == 0)
                continue;
            uint8_t *rec = (uint8_t *)g_2000407c + off;
            for (;;) {
                uint8_t s = lk->step;
                if (s > 9)
                    M5_RETURN_A0(lk);   /* stock returns with lk in a0 */
                lk->step = s + 1;
                uint8_t h = rec[s + 33];
                if (h != 0) {
                    remote_cccd_enable(lk->conn, h);
                    return;
                }
            }
        }
        return;
    }

    tmos_start_task(g_2000bff0, 0x10, 8000);
    if (done == 0 && g_2000bff8->step != 0)
        ((uint8_t *)g_2000bff8)[g_2000bff8->step + 36] = 0;   /* handles[step - 1] */
    if (g_2000bff8->step < g_2000bff8->count) {
        pair_ctx_t *c = g_2000bff8;
        remote_cccd_enable(lk->conn, c->handles[c->step]);
        g_2000bff8->step++;
        return;
    }

    for (uint32_t off = 0; off != 3 * sizeof(remote_rec_t); off += sizeof(remote_rec_t)) {
        if (tmos_memcmp(g_2000bff8->addr, (uint8_t *)g_2000407c + off + 3, 6) != 0)
            goto end;   /* already bonded */
    }
    uint32_t n;
    for (n = 0; n < 3; n++) {
        if (tmos_isbufset((uint8_t *)g_2000407c + n * 43 + 3, 0, 6) != 0)
            break;
    }
    if (n == 3)
        n = 0;      /* no free record: overwrite record 0 */
    uint32_t off = (n & 0xFF) * 43;
    ((uint8_t *)g_2000407c)[off + 2] = g_2000bff8->addr_type;
    ((uint8_t *)g_2000407c)[off + 1] = g_2000bff8->batt_handle;
    tmos_memcpy((uint8_t *)g_2000407c + off + 3, g_2000bff8->addr, 6);
    tmos_memcpy((uint8_t *)g_2000407c + off + 9, g_2000bff8->name, 24);
    tmos_memcpy((uint8_t *)g_2000407c + off + 33, g_2000bff8->handles, 10);
    remote_table_save();
    tmos_stop_task(g_2000bff0, 0x10);
    lk->step = 10;
end:
    pair_end();
    pair_result_send(1);
    app_timer_start(tune_up, 0, 100, 0);
}

/* Stock 0x10DEC. Sends the remote status to the phone: F0 frame of 136 bytes
   04, report id, 03, then the 3 records (43 bytes each, + 4 bytes after them).
   In each record copy, byte 0 = RSSI (0x80 = not linked) and byte 1 = battery.
   For a linked remote it asks for a new RSSI and battery read. */
M5_CALLED void remote_status_send(void)
{
    uint8_t *msg = tmos_msg_allocate(136);

    if (msg == 0)
        return;
    msg[0] = 4;
    msg[1] = g_2000bff4;
    msg[2] = 3;
    tmos_memcpy(msg + 3, g_2000407c, 133);
    for (uint8_t *p = msg; p != msg + 129; p += 43) {
        p[3] = 0x80;
        p[4] = 0;
        for (uint32_t i = 0; i < REMOTE_N; i++) {
            remote_link_t *lk = &g_2000c008[i];
            if (lk->conn == CONN_FREE)
                continue;
            if (tmos_memcmp(p + 6, lk->addr, 6) == 0)
                continue;
            if (GAPRole_ReadRssiCmd(lk->conn) == 0)
                p[3] = (uint8_t)lk->rssi;
            if (lk->step > 9) {
                uint16_t h = ((uint8_t *)g_2000407c)[(p - msg) + 1];
                gatt_read_char_value(lk->conn, &h, g_2000bff0);
                p[4] = lk->batt;
            }
            break;
        }
    }
    ble_send_any(g_2000bff6, 0xF0, msg, 136);
    tmos_msg_deallocate(msg);
}

/* Stock 0x10F46. Sets central event 4 (stop the scan). */
M5_CALLED void scan_stop(void)
{
    tmos_set_event(g_2000bff0, 4);
}

/* Stock 0x10F5A. A device was found. Connects to it when it is the remote
   being paired, or a bonded remote that is not linked yet (and a slot is free). */
M5_CALLED void remote_found(uint32_t addr_type, uint8_t *addr)
{
    if (g_2000bff8 != 0) {
        if (tmos_memcmp(g_2000bff8->addr, addr, 6) == 0)
            return;
    } else {
        if (link_free_slot() > 2)
            return;
        for (remote_link_t *lk = g_2000c008; lk != g_2000c008 + REMOTE_N; lk++) {
            if (lk->conn != CONN_FREE && tmos_memcmp(lk->addr, addr, 6) != 0)
                return;
        }
        uint32_t off;
        for (off = 0; off != 3 * sizeof(remote_rec_t); off += sizeof(remote_rec_t)) {
            if (tmos_memcmp((uint8_t *)g_2000407c + off + 3, addr, 6) != 0)
                break;
        }
        if (off == 3 * sizeof(remote_rec_t))
            return;
    }
    g_2000bffd = 1;
    g_2000bffe = (uint8_t)addr_type;
    tmos_memcpy(g_2000bfff, addr, 6);
    g_2000bffc = 0;
    scan_stop();
}

/* ---- Central role event callback (table 0x196A4, entry 1) ---- */

/* GAP_DEVICE_INFO_EVENT (0x0D). */
static inline __attribute__((always_inline)) void on_device_info(gap_dev_info_t *ev)
{
    uint8_t *p;
    uint8_t len;
    uint32_t code;

    remote_found(ev->addr_type, ev->addr);

    /* Scan report to the phone: 03, id, rssi, type, addr, name (up to 23). */
    if (g_2000bff4 != 0 && ev->event_type != 3 && adv_find(ev, 9, &p, &len) == 0) {
        uint8_t r[34];
        memset(r, 0, 34);
        tmos_memset(r, 0, 34);    /* stock clears the buffer twice */
        r[0] = 3;
        r[1] = g_2000bff4;
        r[2] = (uint8_t)ev->rssi;
        r[3] = ev->addr_type;
        tmos_memcpy(r + 4, ev->addr, 6);
        if (len > 22)
            len = 23;
        tmos_memcpy(r + 10, p, len);
        ble_send_any(g_2000bff6, 0xF0, r, 34);
    }

    /* Broadcast remote "CMT4xy?-dddd...": the name carries the key code.
       x is '5', 'K' or 'S', y is '3' or '0'. 'S' marks a long press. */
    code = 0;
    if (adv_find(ev, 9, &p, &len) == 0 && len == 22 &&
        p[0] == 'C' && p[1] == 'M' && p[2] == 'T' && p[3] == '4' &&
        (p[4] == '5' || ((uint8_t)(p[4] - 'K') & 0xF7) == 0) &&
        (p[5] == '3' || p[5] == '0') && p[7] == '-') {
        if (tmos_memcmp(g_2000c02c, p, 22) != 0) {
            key_broadcast(code, 0, 0);   /* same advert again: key still down */
        } else {
            tmos_memcpy(g_2000c02c, p, 22);
            code = (ev->addr[1] << 16) + (ev->addr[2] << 8) + p[6] + (ev->addr[0] << 24);
            uint32_t num = 0;
            for (uint32_t i = 8; i < 12; i++) {
                uint8_t c = p[i];
                if ((uint8_t)(c - '0') <= 9)
                    num = (uint16_t)(num * 10 - '0' + c);
            }
            key_broadcast(code, num, p[4] == 'S');
        }
    }

    /* Broadcast remote with manufacturer data "TE..", 0xBF, 0xAA (15 bytes).
       A new advert (other data, or 350 ms later) gives the key code.
       Stock odd behavior: when no new code comes, it uses the code of the
       "CMT4" check above (0 when that did not match). */
    if (adv_find(ev, 0xFF, &p, &len) == 0 && len == 15 &&
        p[0] == 'T' && p[1] == 'E' && p[4] == 0xBF && p[5] == 0xAA) {
        uint32_t now = clock_ms();
        if (now - g_20004174 > 349 && tmos_memcmp(g_2000c044, p, 15) == 0) {
            tmos_memcpy(g_2000c044, p, 15);
            code = (ev->addr[1] << 16) + (ev->addr[2] << 8) + p[3] + (ev->addr[0] << 24);
        }
        g_20004174 = now;
        key_broadcast(code, 0, 0);
    }
}

/* GAP_LINK_ESTABLISHED_EVENT (0x05). */
static inline __attribute__((always_inline)) void on_link_up(uint8_t *ev)
{
    uint16_t conn = *(uint16_t *)(ev + 10);

    tmos_stop_task(g_2000bff0, 0x10);
    if (ev[1] != 0) {
        /* Connect failed. */
        if (g_2000bff8 != 0) {
            pair_end();
            pair_result_send(2);
            app_timer_start(tune_two_low, 0, 100, 0);
        }
    } else {
        uint32_t n = link_free_slot();
        if (n > 2) {
            GAPRole_TerminateLink(*(uint16_t *)(ev + 10));
        } else {
            remote_link_t *lk = &g_2000c008[n];
            lk->conn = *(uint16_t *)(ev + 10);
            tmos_memcpy(lk->addr, ev + 4, 6);
            if (g_2000bff8 != 0)
                g_2000bff8->conn = *(uint16_t *)(ev + 10);
            remote_status_send();
            if (g_2000bff8 != 0) {
                tmos_start_task(g_2000bff0, 0x10, 8000);
                GATT_DiscAllChars(g_2000c008[n].conn, 1, 0xFFFF, g_2000bff0);
            } else {
                g_2000c008[n].step = 0;
                remote_cccd_next(n, 1);
            }
        }
    }
    (void)conn;
    g_2000bffc = 0;
    scan_if_needed();
}

/* GAP_LINK_TERMINATED_EVENT (0x06). */
static inline __attribute__((always_inline)) void on_link_down(uint8_t *ev)
{
    uint16_t conn = *(uint16_t *)(ev + 4);
    uint32_t i;

    if (g_2000c008[0].conn == conn)
        i = 0;
    else if (g_2000c008[1].conn == conn)
        i = 1;
    else if (g_2000c008[2].conn == conn)
        i = 2;
    else
        return;
    *(uint32_t *)&g_2000c008[i] = 0xFFFE0080;   /* rssi 0x80, batt 0, conn free */
    g_2000c008[i].step = 0;
    tmos_memset(g_2000c008[i].addr, 0, 6);
    if (g_2000bff8 != 0) {
        pair_end();
        pair_result_send(2);
    } else {
        remote_status_send();
    }
    scan_if_needed();
}

/* Stock 0x11238. */
void remote_central_cb(uint8_t *ev)
{
    switch (ev[2]) {
    case 0x00:          /* GAP_DEVICE_INIT_DONE_EVENT */
        scan_if_needed();
        break;
    case 0x01:          /* GAP_DEVICE_DISCOVERY_EVENT: scan done */
        if (g_2000bffd == 0) {
            scan_if_needed();    /* scan again when needed */
            break;
        }
        tmos_set_event(g_2000bff0, 8);              /* connect */
        tmos_start_task(g_2000bff0, 0x10, 3200);    /* connect time-out */
        break;
    case 0x05:
        on_link_up(ev);
        break;
    case 0x06:
        on_link_down(ev);
        break;
    case 0x0D:
        on_device_info((gap_dev_info_t *)ev);
        break;
    case 0x10:          /* GAP_DIRECT_DEVICE_INFO_EVENT */
        remote_found(ev[4], ev + 5);
        break;
    default:
        break;
    }
}
