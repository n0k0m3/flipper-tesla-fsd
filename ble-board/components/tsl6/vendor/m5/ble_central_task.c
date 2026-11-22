/* Central TMOS task (stock 0x116BC..0x12200): scan, connect and the GATT
   client of the BLE remotes. Notifications of a linked remote become key
   codes; the decoder depends on the remote name in its bond record. */
#include "tsl6.h"
#include "m5.h"
#include "ble_remote.h"

extern uint8_t g_20004170[4];    /* "Free 2" gesture step per gesture (0 or 1) */
extern uint8_t g_20004187;       /* from 0x7FF mux 1: data[1] bit 3 */

void central_cb_empty(void);

/* WCH gapBondCBs_t and gapCentralRoleCB_t (stock 0x19698 and 0x196A4). */
static void *const bond_cbs[3] = {
    (void *)remote_passcode_cb, (void *)remote_pair_state_cb, 0,
};
static void *const central_cbs[3] = {
    (void *)remote_rssi_cb, (void *)remote_central_cb, (void *)central_cb_empty,
};

/* Remote names and the "Free 2" gesture table, one copy of stock
   0x195D8..0x19698. The strings keep their stock zero padding. The test
   compares 16 bytes at a constant pointer, so the layout is the same as
   stock, and the last 10 bytes copy the stock bytes that follow
   (the start of the bond callback table). */
static const uint8_t names[204] = {
    0x6b, 0x68, 0x69, 0x64, 0x00, 0x00, 0x00, 0x00,     /* 0x00 "khid" */
    0x4b, 0x52, 0x65, 0x6d, 0x6f, 0x74, 0x65, 0x00,     /* 0x08 "KRemote" */
    0x49, 0x6e, 0x73, 0x74, 0x61, 0x33, 0x36, 0x30,     /* 0x10 "Insta360 GPS Remote" */
    0x20, 0x47, 0x50, 0x53, 0x20, 0x52, 0x65, 0x6d,
    0x6f, 0x74, 0x65, 0x00, 0x56, 0x43, 0x2d, 0x42,     /* 0x24 "VC-BBx" */
    0x42, 0x78, 0x00, 0x00, 0x42, 0x54, 0x20, 0x4b,     /* 0x2C "BT KeyBoard" */
    0x65, 0x79, 0x42, 0x6f, 0x61, 0x72, 0x64, 0x00,
    0x4d, 0x4b, 0x34, 0x32, 0x34, 0x00, 0x00, 0x00,     /* 0x38 "MK424" */
    0x51, 0x47, 0x53, 0x51, 0x00, 0x00, 0x00, 0x00,     /* 0x40 "QGSQ" */
    0x4d, 0x49, 0x4e, 0x49, 0x5f, 0x4b, 0x45, 0x59,     /* 0x48 "MINI_KEYBOARD" */
    0x42, 0x4f, 0x41, 0x52, 0x44, 0x00, 0x00, 0x00,
    0x42, 0x4c, 0x45, 0x2d, 0x4d, 0x33, 0x00, 0x00,     /* 0x58 "BLE-M3" */
    0x59, 0x69, 0x73, 0x65, 0x72, 0x2d, 0x4a, 0x36,     /* 0x60 "Yiser-J6" */
    0x00, 0x00, 0x00, 0x00, 0x46, 0x72, 0x65, 0x65,     /* 0x6C "Free 2" */
    0x20, 0x32, 0x00, 0x00, 0x57, 0x69, 0x57, 0x55,     /* 0x74 "WiWU-WM105" */
    0x2d, 0x57, 0x4d, 0x31, 0x30, 0x35, 0x00, 0x00,
    0x4f, 0x4d, 0x00, 0x00, 0x43, 0x4f, 0x49, 0x44,     /* 0x80 "OM", 0x84 "COIDEA KM" */
    0x45, 0x41, 0x20, 0x4b, 0x4d, 0x00, 0x00, 0x00,
    /* 0x90: "Free 2" reports, [gesture][step] of 6 bytes */
    0x02, 0x00, 0xee, 0x02, 0x5e, 0x01, 0x03, 0x00,
    0xee, 0x02, 0x5e, 0x01, 0x02, 0x00, 0xfa, 0x00,
    0x5e, 0x01, 0x03, 0x00, 0xfa, 0x00, 0x5e, 0x01,
    0x02, 0x00, 0xf4, 0x01, 0xee, 0x02, 0x03, 0x00,
    0xf4, 0x01, 0xee, 0x02, 0x02, 0x00, 0xf4, 0x01,
    0xfa, 0x00, 0x03, 0x00, 0xf4, 0x01, 0xfa, 0x00,
    /* 0xC0: stock bytes after the table (see above) */
    0x24, 0x0a, 0x01, 0x00, 0x22, 0x0a, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
#define NAME(off) ((void *)&names[off])
#define FREE2_TBL 0x90

/* Key code base from the remote address: a[2] << 8 | a[1] << 16 | a[0] << 24. */
static inline uint32_t addr_base(const uint8_t *a)
{
    return (a[2] << 8) + (a[1] << 16) + ((uint32_t)a[0] << 24);
}

/* Key press logic for linked remotes. The first report of a key starts the
   long press time (event 1, 300 ms). The same key again before that time
   runs the long action. Every report restarts the release time (event 2). */
static inline __attribute__((always_inline)) void key_report(uint32_t code)
{
    if (g_2000bfef == 0) {
        g_2000bfef = 1;
        g_2000bfe8 = code;
        g_2000bfec = 0;
        g_2000bfee = 0;
        tmos_start_task(g_2000bfe4, 1, 480);
    } else if (g_2000bfe8 == code && g_2000bfee == 0) {
        g_2000bfee = 1;
        key_action(0);
        tmos_stop_task(g_2000bfe4, 1);
    }
    tmos_start_task(g_2000bfe4, 2, 640);
}

/* Key code of a report from the remotes after "Insta360 GPS Remote" in the
   name chain. Returns 0 for no key. */
static inline __attribute__((always_inline))
uint32_t key_decode(uint8_t *name, uint8_t *d, uint32_t n, uint8_t *a)
{
    if (tmos_memcmp(NAME(0x24), name, 5)) {         /* "VC-BB" */
        if (n == 4 && d[0] == 0xFF && d[1] == 2 && d[2] == 0x40 && d[3] == 1)
            return addr_base(a) + a[3];
        return 0;
    }
    if (tmos_memcmp(NAME(0x2C), name, 11) ||        /* "BT KeyBoard" */
        tmos_memcmp(NAME(0x38), name, 5) ||         /* "MK424" */
        tmos_memcmp(NAME(0x40), name, 4) ||         /* "QGSQ" */
        tmos_memcmp(NAME(0x48), name, 13)) {        /* "MINI_KEYBOARD" */
        /* HID keyboard report: modifier, 0, key, 0... */
        if (n == 8) {
            if (d[1] || d[3] || d[4] || d[5] || d[6] || d[7])
                return 0;
            return (d[0] << 8) + d[2] + (a[1] << 16) + ((uint32_t)a[0] << 24);
        }
        if (n == 5) {
            if (d[0] == 0 || d[1] || d[2] || d[3] || d[4])
                return 0;
            return addr_base(a) + d[0];
        }
        if (n == 2 && d[1] <= 2)
            return addr_base(a) + (uint8_t)(d[0] + d[1]);
        return 0;
    }
    if (tmos_memcmp(NAME(0x58), name, 6)) {         /* "BLE-M3" */
        if (n == 7) {
            if (d[0] != 7 || d[1] != 7 || d[6] != 1)
                return 0;
            if (d[2] == 0x70) {
                if (d[3] != 7)
                    return 0;
                if (d[4] == 0xF4 && d[5] == 2)
                    return addr_base(a);
                if (d[4] == 0x80 && d[5] == 12)
                    return addr_base(a) + 2;
                if (d[4] == 0x70 && d[5] == 7)
                    return addr_base(a) + 4;
                return 0;
            }
            if (d[2] == 0x5A) {
                if (d[3] == 13 && d[4] == 0x87 && d[5] == 4)
                    return addr_base(a) + 1;
                return 0;
            }
            if (d[2] == 0x32 && d[3] == 1 && d[4] == 0x87 && d[5] == 4)
                return addr_base(a) + 3;
            return 0;
        }
        if (n == 1 && d[0] == 0x20)
            return addr_base(a) + 5;
        return 0;
    }
    if (tmos_memcmp(NAME(0x60), name, 8)) {         /* "Yiser-J6" */
        if (n == 5) {
            if (d[0] != 3)
                return 0;
            uint8_t d1 = d[1], d2 = d[2];
            if (d1 == 0x2C) {
                if (d2 != 1)
                    return 0;
                if (d[3] == 200 && d[4] == 0)
                    return addr_base(a);
                if (d[3] == 0x20 && d[4] == 3)
                    return addr_base(a) + 2;
                if (d[3] == 0xF4 && d[4] == 1)
                    return addr_base(a) + 4;
            } else if (d1 == 0x90) {
                if (d2 != 1)
                    return 0;
                if (d[3] == 0x86 && d[4] == 1)
                    return addr_base(a) + 1;
            } else if (d1 == 0x96 && d2 == 0) {
                if (d[3] == 0x86 && d[4] == 1)
                    return addr_base(a) + 3;
                return 0;
            } else if (d2 != 1) {
                return 0;
            }
            if (d[4] == 3)
                return addr_base(a) + 5;
            return 0;
        }
        if (n == 3 && d[0] == 0 && d[2] <= 1) {
            uint32_t v = (uint8_t)(d[2] + d[1]);
            if (v == 0)
                return 0;
            if (v == 1)
                v = 0x80;
            return addr_base(a) + v;
        }
        return 0;
    }
    if (tmos_memcmp(NAME(0x6C), name, 6)) {         /* "Free 2" */
        if (n == 3) {
            if (d[0] > 32 || d[1] || d[2])
                return 0;
            return addr_base(a) + d[0];
        }
        if (n != 6 || d[1] != 0)
            return 0;
        /* Gestures of two reports: the second report gives 0x80 + gesture. */
        for (uint32_t k = 0; k < 4; k++) {
            uint8_t *step = &g_20004170[k];
            if (tmos_memcmp(d, NAME(FREE2_TBL + (k * 2 + *step) * 6), 6) == 0) {
                *step = 0;
                continue;
            }
            uint8_t s = (uint8_t)(*step + 1);
            *step = s;
            if (s <= 1)
                return 0;
            *step = 0;
            return addr_base(a) + k + 0x80;
        }
        return 0;
    }
    if (tmos_memcmp(NAME(0x74), name, 10) ||        /* "WiWU-WM105" */
        tmos_memcmp(NAME(0x80), name, 2)) {         /* "OM" */
        if (n != 6 || d[1] || d[2] || d[3] || d[5])
            return 0;
        uint32_t v = d[4];
        if (v != 0xFF)
            v = (uint8_t)(v << 4);
        return addr_base(a) + (v | d[0]);
    }
    if (tmos_memcmp(NAME(0x84), name, 9)) {         /* "COIDEA KM" */
        if (n != 8 || d[1] || d[3] || d[4] || d[5] || d[6])
            return 0;
        return addr_base(a) + (d[0] | d[2]);
    }
    return 0;
}

/* GATT_MSG_EVENT from a linked remote (slot i). */
static inline __attribute__((always_inline)) void gatt_msg(uint8_t *msg, uint32_t i)
{
    uint8_t method = msg[4];

    if (method == 0x0B) {                   /* ATT_READ_RSP: battery level */
        if (*(uint16_t *)(msg + 8) == 1) {
            uint8_t v = **(uint8_t **)(msg + 12);
            if (v <= 100)
                g_2000c008[i].batt = v;
        }
    } else if (method == 0x01) {            /* ATT_ERROR_RSP */
        if (msg[8] == 0x12)                 /* to a write request */
            remote_cccd_next(i, 0);
    } else if (method == 0x09) {            /* ATT_READ_BY_TYPE_RSP: characteristics */
        tmos_start_task(g_2000bff0, 0x10, 8000);
        uint32_t pairs = *(uint16_t *)(msg + 8);
        if (pairs != 0) {
            pair_ctx_t *c = g_2000bff8;
            for (uint32_t k = 0; k < pairs; k = (k + 1) & 0xFF) {
                uint8_t *r = *(uint8_t **)(msg + 12) + *(uint16_t *)(msg + 10) * k;
                uint32_t uuid = (r[6] << 8) | r[5];
                if (uuid == 0x2A19) {       /* battery level, readable */
                    if ((r[2] & 0x02) && c)
                        c->batt_handle = r[3];
                } else if (uuid == 0x2A4D || uuid == 0xCE82 || uuid == 0x4101) {
                    /* HID report or vendor key characteristic that notifies */
                    if ((r[2] & 0x30) && c && c->count <= 9) {
                        uint8_t h = r[3];
                        uint8_t n = c->count;
                        c->count = n + 1;
                        c->handles[n] = h + 1;   /* CCCD = value handle + 1 */
                    }
                }
            }
        }
        if (msg[1] == 0x1A)                 /* bleProcedureComplete */
            remote_cccd_next(i, 1);
    } else if (method == 0x1B || method == 0x1D) {  /* notification, indication */
        uint8_t *a = g_2000c008[i].addr;
        uint32_t off;
        for (off = 0; off != 3 * sizeof(remote_rec_t); off += sizeof(remote_rec_t)) {
            if (tmos_memcmp((uint8_t *)g_2000407c + off + 3, a, 6))
                break;
        }
        if (off == 3 * sizeof(remote_rec_t)) {
            if (g_2000bff8 == 0)
                GAPRole_TerminateLink(*(uint16_t *)(msg + 2));   /* not bonded */
            return;
        }
        uint8_t *d = *(uint8_t **)(msg + 12);
        uint32_t n = msg[10];
        /* ble_c0_send forwards the report to a phone that sniffs remotes. */
        if (ble_c0_send((uint16_t)(*(uint16_t *)(msg + 8) + 1), d, n) != 0)
            return;
        uint8_t *name = (uint8_t *)g_2000407c + off + 9;
        if (tmos_memcmp(NAME(0x00), name, 4) ||     /* "khid" */
            tmos_memcmp(NAME(0x08), name, 7)) {     /* "KRemote" */
            /* Consumer keys: d[2] picks a wheel button (0x7FF bit picks the side). */
            if (n != 8 || d[0] || d[1] || d[3] || d[4] || d[5] || d[6] || d[7])
                return;
            switch (d[2]) {
            case 0x1F: seat_pulse(g_20004187 ? 0x80 : 0x20, 10, 0); break;
            case 0x00: seat_pulse(0, 3, 1); break;
            case 0x1E: seat_pulse(g_20004187 ? 0x40 : 0x10, 10, 0); break;
            case 0x20: seat_pulse(g_20004187 ? 4 : 1, 10, 0); break;
            case 0x21: seat_pulse(g_20004187 ? 8 : 2, 10, 0); break;
            default: break;
            }
            return;
        }
        if (tmos_memcmp(NAME(0x10), name, 19)) {    /* "Insta360 GPS Remote" */
            if (n != 9 || d[0] != 0xFC || d[1] != 0xEF || d[2] != 0xFE ||
                d[3] != 0x86 || d[4] != 0 || d[5] != 3 || d[6] != 1)
                return;
            uint32_t code = (a[2] << 8) + (a[1] << 16) + (uint8_t)(d[7] + d[8]) +
                            ((uint32_t)a[0] << 24);
            if (code == 0)
                return;
            if (g_2000bfef == 0) {
                g_2000bfef = d[6];
                g_2000bfe8 = code;
                g_2000bfec = 0;
                g_2000bfee = 0;
                key_action(0);
            }
            tmos_start_task(g_2000bfe4, 2, 480);
            return;
        }
        uint32_t code = key_decode(name, d, n, a);
        if (code != 0)
            key_report(code);
    } else if (method == 0x13) {            /* ATT_WRITE_RSP */
        remote_cccd_next(i, 1);
    }
}

/* Stock 0x116BC. Central task event handler (registered by another module).
   Events: 0x8000 messages, 2 scan, 4 stop scan, 8 connect, 0x10 time-out
   (drop links, end pairing), 1 start the central role. */
uint32_t remote_task_event(uint32_t task, uint32_t ev)
{
    if ((int16_t)ev < 0) {
        uint8_t *msg = RES32(0x6C, uint8_t *, (uint32_t), task);   /* tmos_msg_receive */
        if (msg == 0)
            return ev ^ 0x8000;
        if (msg[0] == 0xB0) {               /* GATT_MSG_EVENT */
            uint16_t conn = *(uint16_t *)(msg + 2);
            uint32_t i;
            if (g_2000c008[0].conn == conn)
                i = 0;
            else if (g_2000c008[1].conn == conn)
                i = 1;
            else if (g_2000c008[2].conn == conn)
                i = 2;
            else
                goto free;
            gatt_msg(msg, i);
free:
            GATT_bm_free(msg + 8, msg[4]);
        }
        tmos_msg_deallocate(msg);
        return ev ^ 0x8000;
    }
    if (ev & 2) {
        if (GAPRole_CentralStartDiscovery(3, 1, 0) != 0)
            RES32(0x58, uint8_t, (uint32_t, uint32_t, uint32_t), task, 2, 1600);
        else
            RES32(0x5C, uint8_t, (uint32_t, uint32_t), task, 2);
        return ev ^ 2;
    }
    if (ev & 4) {
        GAPRole_ObserverCancelDiscovery();
        return ev ^ 4;
    }
    if (ev & 8) {
        if (g_2000bffd != 0)
            GAPRole_CentralEstablishLink(1, 0, g_2000bffe, g_2000bfff);
        return ev ^ 8;
    }
    if (ev & 0x10) {
        GAPRole_TerminateLink(0xFFFF);
        if (g_2000bff8 != 0) {
            pair_end();
            pair_result_send(3);
            app_timer_start(tune_two_low, 0, 100, 0);
        }
        g_2000bffc = 0;
        scan_if_needed();
        return ev ^ 0x10;
    }
    if (ev & 1) {
        RES32(0x1B0, uint8_t, (uint32_t, const void *, const void *),
              task, bond_cbs, central_cbs);    /* GAPRole_CentralStartDevice */
        return ev ^ 1;
    }
    return 0;
}
