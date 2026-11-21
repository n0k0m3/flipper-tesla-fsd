/*
 * replay.c — host test of the TSL6 engine: replay a car capture (candump
 * text, "(t) can0 ID#DATA") into the engine and count the frames it sends.
 *
 *   replay CAPTURE                 every settings bit, one at a time
 *   replay CAPTURE HEX56           one settings block (28 bytes as hex)
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

static const tsl6_hal_t hal = {now_ticks, send, store_read, store_write, log_line, settings_changed, reboot};

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
static uint32_t run(const uint8_t cfg[TSL6_CFG_LEN])
{
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
