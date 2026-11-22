/*
 * replay.c — host test of the TSL6 engine: replay a car capture (candump
 * text, "(t) can0 ID#DATA") into the engine and count the frames it sends.
 *
 *   replay CAPTURE                 every settings bit, one at a time
 *   replay CAPTURE HEX56           one settings block (28 bytes as hex)
 *   replay CAPTURE ble             a phone link: the TSL6 commands and their replies
 *   replay CAPTURE remote          a phone pairs a BLE remote (fake central role), then a key
 *
 * Build: test/run.sh (32-bit host build: the TSL6 globals hold 32-bit pointers).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsl6_engine.h"

typedef struct {
    uint32_t ms10;  /* capture time in 0.1 ms */
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} rec_t;

static rec_t *s_recs;
static size_t s_nrecs;
static uint32_t s_now;  /* 0.625 ms ticks */

/* The store: one buffer per page key. */
typedef struct { uint32_t page; uint32_t len; uint8_t buf[256]; } page_t;
static page_t s_pages[8];

static uint32_t tx_ids[0x800];
static uint32_t s_tx;

static uint32_t now_ticks(void) { return s_now; }

static int send(const tsl6_frame_t *f)
{
    tx_ids[f->id & 0x7FF]++;
    s_tx++;
    return 1;
}

static page_t *page_find(uint32_t page, int make)
{
    for (int i = 0; i < 8; i++)
        if (s_pages[i].page == page)
            return &s_pages[i];
    if (!make)
        return NULL;
    for (int i = 0; i < 8; i++)
        if (s_pages[i].page == 0) {
            s_pages[i].page = page;
            return &s_pages[i];
        }
    abort();
}

static int store_read(uint32_t page, void *dst, uint32_t len)
{
    page_t *p = page_find(page, 0);
    if (!p || p->len != len)
        return 0;
    memcpy(dst, p->buf, len);
    return 1;
}

static void store_write(uint32_t page, const void *src, uint32_t len)
{
    page_t *p = page_find(page, 1);
    p->len = len;
    memcpy(p->buf, src, len);
}

static int s_quiet;
static void log_line(const char *line) { if (!s_quiet) fprintf(stderr, "  log: %s\n", line); }
static void settings_changed(void) {}
static void reboot(void) {}

/* Frames to the phone, by type. */
static uint32_t ble_types[256];
static uint8_t last_a0[76];
static uint32_t last_a0_len;
static uint8_t last_reply[1100];
static uint32_t last_reply_len;
static int ble_send(uint16_t conn, const uint8_t *f, uint16_t len)
{
    (void)conn;
    if (len < 6 || f[0] != 0x55 || f[1] != 0x7F)
        abort();
    uint8_t sum = 0;
    for (int i = 2; i < len - 1; i++)
        sum += f[i];
    if (sum != f[len - 1] || (uint16_t)(f[3] << 8 | f[4]) != len - 6)
        abort();
    ble_types[f[2]]++;
    if (f[2] == 0xA0 && len - 6 <= 76) {
        memcpy(last_a0, f + 5, len - 6);
        last_a0_len = len - 6;
    }
    memcpy(last_reply, f, len);
    last_reply_len = len;
    return 1;
}
static const uint8_t UID14[14] = {'S', 'C', 'R', 'L', '-', 'H', '2', 0, 0x01, 0x00, 0x00, 0xfe, 0xda, 0x74};
static void uid14(uint8_t out[14]) { memcpy(out, UID14, 14); }

/* ---- fake central role: one remote "BT KeyBoard" with a HID report (notify)
   and a battery level. Results come one poll later, as from a radio. ---- */
static const uint8_t REMOTE_ADDR[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
#define REMOTE_CONN 0x41
static tsl6_cev_t s_cev[32];
static int s_ncev;
static int s_scans, s_connects, s_cccd_writes;
static uint16_t s_cccd_handle;
static void cev(tsl6_cev_t e) { if (s_ncev < 32) s_cev[s_ncev++] = e; }
static int c_scan(uint32_t ms, int active)
{
    (void)ms; (void)active;
    s_scans++;
    tsl6_cev_t e = {0};
    e.type = TSL6_CEV_SCAN_REPORT; e.evt_type = 0; e.addr_type = 0; memcpy(e.addr, REMOTE_ADDR, 6); e.rssi = -60;
    static const uint8_t adv[] = {2, 1, 6, 12, 9, 'B', 'T', ' ', 'K', 'e', 'y', 'B', 'o', 'a', 'r', 'd'};
    memcpy(e.data, adv, sizeof(adv)); e.len = sizeof(adv);
    cev(e);
    tsl6_cev_t d = {0}; d.type = TSL6_CEV_SCAN_DONE; cev(d);
    return 0;
}
static void c_scan_stop(void) { tsl6_cev_t d = {0}; d.type = TSL6_CEV_SCAN_DONE; cev(d); }
static int c_connect(uint8_t type, const uint8_t addr[6])
{
    s_connects++;
    tsl6_cev_t e = {0};
    e.type = TSL6_CEV_CONNECTED; e.addr_type = type; memcpy(e.addr, addr, 6); e.conn = REMOTE_CONN;
    cev(e);
    return 0;
}
static void c_connect_cancel(void) {}
static int c_disconnect(uint16_t conn) { tsl6_cev_t e = {0}; e.type = TSL6_CEV_DISCONNECTED; e.conn = conn; cev(e); return 0; }
static int c_rssi(uint16_t conn, int8_t *out) { (void)conn; *out = -55; return 0; }
static int c_disc(uint16_t conn)
{
    tsl6_cev_t e = {0};
    e.type = TSL6_CEV_CHR; e.conn = conn; e.handle = 0x1f; e.props = 0x12; e.val_handle = 0x20; e.uuid16 = 0x2A4D; cev(e);
    e.handle = 0x2f; e.props = 0x02; e.val_handle = 0x30; e.uuid16 = 0x2A19; cev(e);
    tsl6_cev_t d = {0}; d.type = TSL6_CEV_DISC_DONE; d.conn = conn; cev(d);
    return 0;
}
static int c_write(uint16_t conn, uint16_t handle, const uint8_t *v, uint16_t len)
{
    if (len == 2 && v[0] == 1 && v[1] == 0) { s_cccd_writes++; s_cccd_handle = handle; }
    tsl6_cev_t e = {0}; e.type = TSL6_CEV_WRITE_DONE; e.conn = conn; cev(e);
    return 0;
}
static int c_read(uint16_t conn, uint16_t handle)
{
    (void)handle;
    tsl6_cev_t e = {0}; e.type = TSL6_CEV_READ_DONE; e.conn = conn; e.data[0] = 90; e.len = 1; cev(e);
    return 0;
}
static int s_forgets;
static void c_forget(uint8_t type, const uint8_t addr[6]) { (void)type; if (!memcmp(addr, REMOTE_ADDR, 6)) s_forgets++; }
static const tsl6_central_ops_t central_ops = {c_scan, c_scan_stop, c_connect, c_connect_cancel, c_disconnect,
                                               c_rssi, c_disc, c_write, c_read, c_forget};
static void central_deliver(void)
{
    int n = s_ncev;
    tsl6_cev_t batch[32];
    memcpy(batch, s_cev, sizeof(tsl6_cev_t) * (size_t)n);
    s_ncev = 0;
    for (int i = 0; i < n; i++)
        tsl6_central_event(&batch[i]);
}

static tsl6_hal_t hal = {now_ticks, send, store_read, store_write, log_line, settings_changed, reboot, ble_send, uid14, NULL};

/* One TSL6 command from the phone, as the app sends it. */
static void cmd(uint8_t type, const uint8_t *data, uint16_t len)
{
    uint8_t f[300];
    uint8_t sum = (uint8_t)(type + (len >> 8) + (uint8_t)len);
    f[0] = 0x55; f[1] = 0x7F; f[2] = type; f[3] = (uint8_t)(len >> 8); f[4] = (uint8_t)len;
    for (uint16_t i = 0; i < len; i++) { f[5 + i] = data[i]; sum += data[i]; }
    f[5 + len] = sum;
    /* In two writes, as a phone with a small MTU does. */
    uint16_t half = (uint16_t)((len + 6) / 2);
    tsl6_ble_rx(1, f, half);
    tsl6_ble_rx(1, f + half, (uint16_t)(len + 6 - half));
}

static void load(const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[128], prev[128] = "";
    size_t cap = 1 << 20;

    if (!fp) {
        perror(path);
        exit(2);
    }
    s_recs = malloc(cap * sizeof(rec_t));
    while (fgets(line, sizeof(line), fp)) {
        double t;
        unsigned id;
        char hex[40] = "";
        if (line[0] != '(' || sscanf(line, "(%lf) can0 %x#%39s", &t, &id, hex) < 2)
            continue;
        if (strcmp(line, prev) == 0)  /* the capture repeats some packets */
            continue;
        strcpy(prev, line);
        if (s_nrecs == cap)
            s_recs = realloc(s_recs, (cap *= 2) * sizeof(rec_t));
        rec_t *r = &s_recs[s_nrecs++];
        memset(r, 0, sizeof(*r));
        r->ms10 = (uint32_t)(t * 10000.0 + 0.5);
        r->id = (uint16_t)id;
        r->dlc = (uint8_t)(strlen(hex) / 2);
        for (int i = 0; i < r->dlc && i < 8; i++)
            sscanf(hex + 2 * i, "%2hhx", &r->data[i]);
    }
    fclose(fp);
}

/* One replay with the given settings. Returns the frames sent. */
/* Commands at fixed times of a replay (s from the start), for the ble mode. */
typedef struct { double at; uint8_t type; uint8_t len; uint8_t data[48]; } step_t;
static const step_t *s_steps;
static int s_nsteps;

static uint32_t run(const uint8_t cfg[TSL6_CFG_LEN])
{
    int step = 0;
    memset(s_pages, 0, sizeof(s_pages));
    memset(tx_ids, 0, sizeof(tx_ids));
    s_tx = 0;
    s_now = s_recs[0].ms10 * 16 / 1000;
    tsl6_init(&hal);
    if (cfg)
        tsl6_cfg_set(cfg);
    for (size_t i = 0; i < s_nrecs; i++) {
        uint32_t t = s_recs[i].ms10 * 16 / 1000;  /* 0.1 ms -> 0.625 ms */
        /* Fire the timers that fall before this frame, in time order. */
        for (;;) {
            uint32_t next = tsl6_poll();
            if (next == UINT32_MAX)
                break;
            uint32_t due = s_now + (next * 8 + 4) / 5;
            if (next == 0 || (int32_t)(t - due) < 0)
                break;
            s_now = due;
        }
        s_now = t;
        while (step < s_nsteps && (s_recs[i].ms10 - s_recs[0].ms10) / 10000.0 >= s_steps[step].at) {
            const step_t *st = &s_steps[step++];
            if (st->type == 0)
                tsl6_ble_connect(1);
            else if (st->type == 0xF0) {  /* F0: the UID14 first */
                uint8_t f[64];
                memcpy(f, UID14, 14);
                memcpy(f + 14, st->data, st->len);
                cmd(0xF0, f, (uint16_t)(14 + st->len));
            } else if (st->type == 1) {    /* a HID report of the remote */
                tsl6_cev_t e = {0};
                e.type = TSL6_CEV_NOTIFY; e.conn = REMOTE_CONN; e.val_handle = 0x20;
                memcpy(e.data, st->data, st->len); e.len = st->len;
                cev(e);
            } else
                cmd(st->type, st->data, st->len);
        }
        if (s_ncev)
            central_deliver();
        tsl6_rx(s_recs[i].id, 0, s_recs[i].dlc, s_recs[i].data);
    }
    tsl6_poll();
    return s_tx;
}

static void print_ids(void)
{
    for (int id = 0; id < 0x800; id++)
        if (tx_ids[id])
            printf(" %03X:%u", id, tx_ids[id]);
    printf("\n");
}

int main(int argc, char **argv)
{
    uint8_t base[TSL6_CFG_LEN], cfg[TSL6_CFG_LEN];

    if (argc < 2) {
        fprintf(stderr, "usage: replay CAPTURE [HEX56]\n");
        return 2;
    }
    load(argv[1]);
    if (s_nrecs == 0) {
        fprintf(stderr, "no frames in %s\n", argv[1]);
        return 2;
    }
    printf("frames %zu, %.1f s\n", s_nrecs, (s_recs[s_nrecs - 1].ms10 - s_recs[0].ms10) / 10000.0);

    uint32_t n = run(NULL);
    tsl6_cfg_get(base);
    printf("factory settings: sent %u", n);
    print_ids();
    if (n != 0) {
        printf("FAIL: the factory settings must send nothing\n");
        return 1;
    }

    if (argc == 3 && strcmp(argv[2], "remote") == 0) {
        hal.central = &central_ops;
        static const step_t steps[] = {
            {0.0, 0, 0, {0}},
            {1.0, 0xF0, 1, {1}},                                   /* F0 01: scan with reports */
            {3.0, 0xF0, 1, {2}},                                   /* F0 02: stop, list */
            /* F0 05: add the remote: 05, addr type, addr, 24-byte name */
            {4.0, 0xF0, 32, {5, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 'B', 'T', ' ', 'K', 'e', 'y', 'B', 'o', 'a', 'r', 'd'}},
            {10.0, 1, 8, {0, 0, 0x28, 0, 0, 0, 0, 0}},             /* a key down (HID report) */
            {10.05, 1, 8, {0, 0, 0, 0, 0, 0, 0, 0}},               /* key up */
            {12.0, 0xF0, 1, {4}},                                  /* F0 04: list */
        };
        s_steps = steps;
        s_nsteps = (int)(sizeof(steps) / sizeof(steps[0]));
        memset(ble_types, 0, sizeof(ble_types));
        n = run(NULL);
        printf("remote: scans %d, connects %d, CCCD writes %d (handle %#x); frames to the phone:", s_scans, s_connects,
               s_cccd_writes, s_cccd_handle);
        for (int ty = 0; ty < 256; ty++)
            if (ble_types[ty])
                printf(" %02X:%u", ty, ble_types[ty]);
        printf("\n");
        int bad = 0;
        if (s_scans == 0 || s_connects == 0) { printf("FAIL: no scan or no connect\n"); bad = 1; }
        if (s_cccd_writes == 0 || s_cccd_handle != 0x21) { printf("FAIL: the HID report notifications are not on\n"); bad = 1; }
        if (!ble_types[0xF0]) { printf("FAIL: no F0 frame\n"); bad = 1; }
        if (s_forgets == 0) { printf("FAIL: no bond delete before the pairing (0x417)\n"); bad = 1; }
        /* The saved remote: the remote table (store page 0x08077500) holds the address. */
        uint8_t tbl[0x85];
        if (!store_read(0x08077500u, tbl, sizeof(tbl)) || memcmp(tbl + 3, REMOTE_ADDR, 6) != 0 || memcmp(tbl + 9, "BT KeyBoard", 11) != 0) {
            printf("FAIL: the remote is not in the saved remote table\n");
            bad = 1;
        }
        printf(bad ? "FAIL\n" : "PASS\n");
        return bad;
    }

    if (argc == 3 && strcmp(argv[2], "ble") == 0) {
        /* A phone link for the whole replay: status, tables, info, the gauge
           stream for 60 s, then the CAN tool sniff for 10 s. */
        static const step_t steps[] = {
            {0.0, 0, 0, {0}},                 /* connect: A0 status */
            {1.0, 0xA0, 1, {0}},              /* status */
            {2.0, 0xAB, 1, {2}},              /* shortcut table */
            {3.0, 0xBA, 1, {2}},              /* BLE button table */
            {4.0, 0xC1, 1, {0}},              /* state word */
            {5.0, 0xD0, 1, {1}},              /* vehicle info */
            {6.0, 0xA0, 1, {0}},              /* back to the status link */
            {100.0, 0xB0, 1, {1}},            /* gauge stream on */
            {160.0, 0xB0, 1, {0}},            /* off */
            {200.0, 0xC0, 1, {1}},            /* CAN tool on: every ID */
            {210.0, 0xC0, 1, {0}},            /* off */
            {211.0, 0xA0, 1, {0}},
        };
        s_steps = steps;
        s_nsteps = (int)(sizeof(steps) / sizeof(steps[0]));
        memset(ble_types, 0, sizeof(ble_types));
        n = run(NULL);
        printf("ble link: CAN sent %u; frames to the phone:", n);
        for (int ty = 0; ty < 256; ty++)
            if (ble_types[ty])
                printf(" %02X:%u", ty, ble_types[ty]);
        printf("\nlast A0 (%u bytes):", last_a0_len);
        for (unsigned i = 0; i < last_a0_len; i++)
            printf("%s%02x", i % 4 ? "" : " ", last_a0[i]);
        printf("\n");
        int bad = 0;
        if (n != 0) { printf("FAIL: the factory settings sent CAN frames\n"); bad = 1; }
        if (last_a0_len != 76 || (last_a0[3] & 1) != 1 || memcmp(last_a0 + 4, "SCRL-H2", 7) != 0) { printf("FAIL: A0 status\n"); bad = 1; }
        static const uint8_t need[] = {0xA0, 0xAB, 0xBA, 0xC1, 0xD0, 0xB0, 0xC0};
        for (unsigned i = 0; i < sizeof(need); i++)
            if (!ble_types[need[i]]) { printf("FAIL: no %02X frame\n", need[i]); bad = 1; }
        if (ble_types[0xAF]) { printf("FAIL: activation nag AF\n"); bad = 1; }
        printf(bad ? "FAIL\n" : "PASS\n");
        return bad;
    }

    if (argc == 3) {
        if (strlen(argv[2]) != 2 * TSL6_CFG_LEN) {
            fprintf(stderr, "HEX56: 28 bytes as hex\n");
            return 2;
        }
        for (unsigned i = 0; i < TSL6_CFG_LEN; i++)
            sscanf(argv[2] + 2 * i, "%2hhx", &cfg[i]);
        n = run(cfg);
        printf("settings %s: sent %u", argv[2], n);
        print_ids();
        return 0;
    }

    /* Every settings bit, one at a time (master switch off: byte 4 bit 3 = 0). */
    s_quiet = 1;
    for (unsigned byte = 0; byte < TSL6_CFG_LEN; byte++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            if (byte == 4 && bit == 3)
                continue;
            memcpy(cfg, base, sizeof(cfg));
            cfg[byte] ^= (uint8_t)(1u << bit);
            n = run(cfg);
            if (n) {
                printf("byte %2u bit %u (A3 byte %2u) -> %02X: sent %u", byte, bit,
                       byte < 12 ? 24 + byte : 59 + byte - 12, cfg[byte], n);
                print_ids();
            }
        }
    }
    printf("PASS\n");
    return 0;
}
