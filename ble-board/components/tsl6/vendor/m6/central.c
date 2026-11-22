/* BLE central role for remote buttons: init, scan control and the F0
   command from the phone (stock 0x12200..0x125F8).
   The central task code itself is in m5 (0x10A54..0x116BC). */
#include "m6.h"

/* Stock 0x12200. Registers the two central tasks and sets the GAP and
   bond parameters. Called once from main. */
void central_init(void)
{
    uint8_t bond[4];
    uint32_t passkey;

    tmos_memset(&g_central, 0, sizeof(g_central));
    tmos_memset(g_remote_links, 0, sizeof(g_remote_links));
    g_central.reply_conn = LINK_FREE;
    g_central.task = TMOS_ProcessEventRegister(remote_task_event);

    GAP_SetParamValue(2, 80);    /* TGAP_GEN_DISC_SCAN: 80 ms */
    GAP_SetParamValue(7, 6);     /* TGAP_CONN_EST_INT_MIN */
    GAP_SetParamValue(8, 160);   /* TGAP_CONN_EST_INT_MAX */
    GAP_SetParamValue(13, 100);  /* TGAP_CONN_EST_SUPERV_TIMEOUT */

    bond[0] = 2;   /* pairing mode */
    bond[2] = 3;   /* IO capabilities */
    passkey = 0;
    bond[1] = 1;   /* MITM */
    bond[3] = 1;   /* bonding */
    GAPBondMgr_SetParameter(0x40f, 4, &passkey);
    GAPBondMgr_SetParameter(0x408, 1, &bond[0]);
    GAPBondMgr_SetParameter(0x409, 1, &bond[1]);
    GAPBondMgr_SetParameter(0x40a, 1, &bond[2]);
    GAPBondMgr_SetParameter(0x40d, 1, &bond[3]);

    for (int i = 0; i < REMOTE_LINKS; i++) {
        g_remote_links[i].rssi = -128;
        g_remote_links[i].handle = LINK_FREE;
    }
    RES_C4();
    RES_C8(g_central.task);
    tmos_set_event(g_central.task, 1);

    tmos_memset(&g_central2, 0, sizeof(g_central2));
    g_central2.task = TMOS_ProcessEventRegister(key_task_event);
}

/* Stock 0x12348. on = 0 sets event 4 of the central task (0x10F46),
   else 0x10AC0 starts a scan when one is needed. */
void central_scan_set(uint32_t on, uint32_t auto_scan)
{
    g_central.auto_scan = (uint8_t)auto_scan;
    if (on == 0) {
        scan_stop();
        return;
    }
    scan_if_needed();
}

/* Stock 0x1235A. Scans and starts a 20 s timer (event 4 of the second task). */
void central_scan_start(void)
{
    g_central.auto_scan = 1;
    scan_if_needed();
    g_central2.scan_timed = 1;
    g_central2.b11 = 0;
    tmos_start_task(g_central2.task, 4, 32000);  /* 32000 x 0.625 ms = 20 s */
}

/* F0 05: add the remote in p (32 bytes: 05, addr type, addr[6], 24 bytes). */
static void f0_add(uint8_t *p, uint32_t len)
{
    uint32_t i;

    pair_end();
    g_central.scan_on = 0;
    if ((uint8_t)len != 32)
        return;
    for (i = 0; i != 3 * REMOTE_ENTRY; i += REMOTE_ENTRY) {
        if (tmos_isbufset(g_remote_table_p + i + 3, 0, 6))
            break;
    }
    if (i == 3 * REMOTE_ENTRY) {
        pair_result_send(5);   /* table full */
        return;
    }
    if (link_free_slot() > 2) {
        pair_result_send(6);   /* too many links */
        return;
    }
    GAPBondMgr_SetParameter(0x417, 7, p + 1);
    g_central.pending = tmos_msg_allocate(48);
    if (g_central.pending == 0)
        return;
    tmos_memset(g_central.pending, 0, 48);
    uint8_t *m = g_central.pending;
    *(uint16_t *)(m + 0x20) = LINK_FREE;
    m[0] = p[1];
    tmos_memcpy(m + 1, p + 2, 6);
    tmos_memcpy(g_central.pending + 7, p + 8, 24);
    pair_result_send(0);
    g_central.auto_scan = 1;
    scan_if_needed();
}

/* F0 07: remove the remote with the address at p + 2 (8 bytes). */
static void f0_remove(uint8_t *p, uint32_t len)
{
    pair_end();
    g_central.scan_on = 0;
    if ((uint8_t)len != 8)
        return;
    for (uint32_t i = 0; i != 3 * REMOTE_ENTRY; i += REMOTE_ENTRY) {
        /* An empty entry (address 0) matches an address of 0. */
        if (!tmos_memcmp(g_remote_table_p + i + 3, p + 2, 6))
            continue;
        GAPBondMgr_SetParameter(0x417, 7, p + 1);
        tmos_memset(g_remote_table_p + i, 0, REMOTE_ENTRY);
        remote_table_save();
        for (remote_link_t *l = g_remote_links; l != &g_remote_links[REMOTE_LINKS]; l++) {
            if (l->handle != LINK_FREE && tmos_memcmp(l->addr, p + 2, 6))
                GAPRole_TerminateLink(l->handle);
        }
        g_central.auto_scan = 0;
        scan_if_needed();
        pair_result_send(4);
        app_timer_start(tune_up, 0, 100, 0);
        return;
    }
}

/* Stock 0x1239A. F0 command from the phone link conn.
   p[0]: 1 scan, 2 stop and list, 4 list, 5 add, 7 remove. 3 and 6 do nothing. */
void cmd_f0_remote(uint32_t conn, uint8_t *p, uint32_t len)
{
    g_central.reply_conn = (uint16_t)conn;
    switch ((uint8_t)(p[0] - 1)) {
    case 0:
        pair_end();
        g_central.scan_on = 1;
        g_central.auto_scan = 1;
        scan_if_needed();
        return;
    case 1:
        pair_end();
        g_central.scan_on = 0;
        g_central.auto_scan = 0;
        scan_stop();
        remote_status_send();
        return;
    case 3:
        remote_status_send();
        return;
    case 4:
        f0_add(p, len);
        return;
    case 6:
        f0_remove(p, len);
        return;
    default:
        return;
    }
}
