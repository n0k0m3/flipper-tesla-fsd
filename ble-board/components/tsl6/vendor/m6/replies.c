/* Replies to the phone: A0 status, B0 gauge stream, and the queued AB, BA
   and B9 table replies (stock 0x12F62..0x1376C). */
#include "m6.h"

/* Getters of other modules. Most return one car value or flag. */
void *vin_get(void);              /* 19 bytes for A0 */
void *tpms_get(void);              /* 4 bytes for B0 */
void *brake_temp_get(void);              /* 5 bytes for B0 */
uint32_t vc334_d3_bit1_get(void);
uint32_t vc2b6_app_get(void);
uint32_t vc334_d3_bit0_get(void);
uint32_t reengage_active(void);
uint32_t heat_request_get(void);
uint32_t cp_state_get(void);
uint32_t seat_cycle_get(void);
uint32_t can_2f3_bf0f_on(void);
uint32_t swc_mode32_get(void);
uint32_t flash_active_get(void);
uint32_t tick_is_on(void);
uint32_t front_fog_strobe_get(void);
uint32_t front_fog_burst_get(void);
uint32_t rear_fog_strobe_get(void);
uint32_t rear_fog_burst_get(void);
uint32_t ap_active(void);
uint32_t sport_mode_get(void);
uint32_t speed_get(void);
uint32_t gear_get(void);
uint32_t ind_state2_bits(void);
uint32_t ap_gauge_status(void);
uint32_t doors_open_mask(void);
uint32_t lights_gauge_bits(void);
uint32_t soc_get(void);
uint32_t flag_3e3_get(void);
uint32_t vc273_bit0_get(void);
uint32_t bf84_get(void);
uint32_t can_3e2_mode_is1(void);
uint32_t bf85_get(void);
uint32_t odometer_get(void);
uint32_t accel_pedal_get(void);
uint32_t power_get(uint32_t i);
uint32_t altitude_get(void);
uint32_t heat_status_get(void);
uint32_t flag_257_get(void);
uint32_t epas_torque_mode_get(void);
uint32_t can_20c_v10_get(void);
uint32_t can_20c_v11_get(void);
uint32_t can_243_value_get(void);
uint32_t can_321_d5_get(void);
uint32_t pack_temp_get(void);
uint32_t brick_v_get(void);
uint32_t range_get(void);
uint32_t speed_limit_index(void);
uint32_t bf06_get(void);

extern uint8_t g_20004188;         /* A0 status byte 3 bit 4 */
extern uint16_t g_2000c268;        /* B0: bit 14 = 0 sets payload bit 34 */

/* Stock 0x12F62 (not in functions.tsv). Timer callback: clears the busy flag. */
void links_busy_clear(void)
{
    g_ble_links.busy = 0;
}

/* A0 status payload (76 bytes). */
typedef struct {
    uint8_t zero;          /* 0 */
    uint8_t boot[2];       /* 1-2 boot flag bytes 0-1 */
    uint8_t flags_a;       /* 3 bit 0 = 1, bits 1-4 */
    uint8_t uid[14];       /* 4-17 chip UID14 */
    uint8_t flags_b;       /* 18 */
    uint8_t flags_c;       /* 19 bit 7 = switch on and bus 0 not alive */
    uint16_t tick_left;    /* 20-21 ms to the next tick */
    uint8_t platform;      /* 22 bits 0-5 */
    uint8_t alive;         /* 23 bits 0-2 = frames 0-2 alive */
    uint8_t cfg[16];       /* 24-39 config bytes 0-15 */
    uint8_t info[19];      /* 40-58 */
    uint8_t cfg96[16];     /* 59-74 config bytes 96-111 */
    uint8_t end;           /* 75 */
} a0_status_t;

/* Fills the fields that only an authorized link gets. */
static void a0_fill_authed(a0_status_t *s)
{
    uint8_t v;

    s->flags_a |= vc334_d3_bit1_get() << 1;
    s->flags_a |= vc2b6_app_get() << 2;
    v = vc334_d3_bit0_get();
    s->flags_a = (g_20004188 << 4) | s->flags_a | (v << 3);

    s->flags_b = reengage_active();
    s->flags_b |= heat_request_get() << 1;
    s->flags_b |= cp_state_get() << 2;
    s->flags_b |= (seat_cycle_get() << 3) & 0x08;
    s->flags_b |= (seat_cycle_get() << 3) & 0x10;
    s->flags_b |= can_2f3_bf0f_on() << 5;
    s->flags_b |= swc_mode32_get() << 6;
    s->flags_b |= flash_active_get() << 7;

    s->flags_c = tick_is_on();
    s->flags_c |= front_fog_strobe_get() << 1;
    s->flags_c |= front_fog_burst_get() << 2;
    s->flags_c |= rear_fog_strobe_get() << 3;
    s->flags_c |= rear_fog_burst_get() << 4;
    s->flags_c |= ap_active() << 5;
    s->flags_c |= sport_mode_get() << 6;
    v = 0;
    if (bus_alive(0) == 0)
        v = ((g_cfg[4] >> 3) ^ 1) & 1;
    s->flags_c |= v << 7;

    s->tick_left = tick_ms_left();
    s->platform = platform_get() & 0x3f;
    s->alive = bus_alive(0);
    s->alive |= bus_alive(1) << 1;
    s->alive |= bus_alive(2) << 2;
    tmos_memcpy(s->cfg, g_cfg, 16);
    tmos_memcpy(s->cfg96, g_cfg + 96, 16);
}

/* Stock 0x12F6C. Sends the A0 status to each idle link (mode 0).
   A link without the password gets zeros in bytes 18-19, 24-39 and 59-74.
   The buffer is shared by the two links: bytes 20-23 keep the values of
   the first link, also for a second link without the password. */
void a0_status_send(void)
{
    a0_status_t s;
    uint64_t boot = bootflag_read();

    tmos_memset(&s, 0, sizeof(s));
    s.zero = 0;
    s.boot[0] = (uint8_t)boot;
    s.boot[1] = (uint8_t)(boot >> 8);
    uid14_get(s.uid);
    tmos_memcpy(s.info, vin_get(), 19);

    for (int i = 0; ; i = 1) {
        m6_link_t *l = &g_ble_links.link[i];
        if (l->handle != LINK_FREE && l->mode == 0) {
            s.flags_a = 1;
            if (l->authed != 0) {
                a0_fill_authed(&s);
            } else {
                s.flags_b = 0;
                s.flags_c = 0;
                tmos_memset(s.cfg, 0, 16);
                tmos_memset(s.cfg96, 0, 16);
            }
            ble_send_frame(g_ble_links.link[i].handle, 0xa0, &s, sizeof(s));
        }
        if (i != 0)
            break;
    }
}

/* Stock 0x13206. Returns 1 when a link is in a stream mode (mode != 0) and
   has the password. Then it also sets the busy flag for 300 ms. */
uint32_t links_busy_check(void)
{
    m6_link_t *a = &g_ble_links.link[0];
    m6_link_t *b = &g_ble_links.link[1];

    if (!(a->handle != LINK_FREE && a->mode != 0 && a->authed != 0) &&
        !(b->handle != LINK_FREE && b->mode != 0 && b->authed != 0))
        return 0;
    g_ble_links.busy = 1;
    app_timer_start(links_busy_clear, 0, 300, 0);
    return 1;
}

/* Queued reply for queued_frame_cb (12-byte TMOS message). */
typedef struct {
    uint16_t conn;
    uint8_t type;
    uint8_t pad;
    uint8_t *data;
    uint16_t len;
} queued_frame_t;

/* Queues a 256-byte table reply. The delay is 10 ms, or 100 ms when slow.
   The message is freed when no timer is free. */
static void table_send(uint32_t conn, uint8_t type, uint8_t *table, uint32_t slow)
{
    queued_frame_t *m = (queued_frame_t *)tmos_msg_allocate(12);

    if (m == 0)
        return;
    m->type = type;
    m->conn = (uint16_t)conn;
    m->data = table;
    m->len = 256;
    uint32_t ms = 100;
    if (!slow && !links_busy_check())
        ms = 10;
    if (app_timer_start(queued_frame_cb, 0, ms, m) != 0)
        tmos_msg_deallocate((uint8_t *)m);
}

/* Stock 0x13280. Queues the AB table reply. */
void ab_table_send(uint32_t conn)
{
    table_send(conn, 0xab, g_ab_table_p, 0);
}

/* Stock 0x132F0. Queues the BA table reply. */
void ba_table_send(uint32_t conn)
{
    table_send(conn, 0xba, g_ba_table_p, 0);
}

/* Stock 0x13360. Queues the B9 table reply of a tab (0 zone link, else whole
   car). slow != 0 uses 100 ms without the busy check.
   It queues the B9 (ambient light) table reply. (symbols.csv called it gauge_build until 2026-11-14.) */
void b9_table_reply(uint32_t conn, uint32_t tab, uint32_t slow)
{
    table_send(conn, 0xb9, tab != 0 ? g_rgb_car_p : g_rgb_zone_p, slow);
}

/* B0 gauge payload (33 bytes). Field bits are in the comments of
   b0_gauge_send. */
typedef struct {
    uint32_t w0;        /* 0-3 */
    uint32_t w1;        /* 4-7 */
    uint8_t b8[4];      /* 8-11 */
    uint32_t w12;       /* 12-15 */
    uint16_t h16;       /* 16-17 */
    uint8_t b18[15];    /* 18-32 */
} b0_gauge_t;

#define B0_LEN 33

/* Stock 0x133E0. Builds the B0 gauge payload and sends it to each link in
   gauge mode (mode 1) that has the password. Does nothing while the busy
   flag is set.
   Stock is a void function. It leaves in a0 the entry a0 (busy), the
   result of the last ble_send_frame, or bf06_get() << 4. The caller
   ignores a0. This C version returns the same value so that the
   difftest can compare a0. */
uint32_t b0_gauge_send(uint32_t a0)
{
    b0_gauge_t g;
    uint32_t v;

    if (g_ble_links.busy != 0)
        return a0;
    tmos_memset(&g, 0, B0_LEN);

    /* Word 0. */
    g.w0 = (g.w0 & ~0x1ffu) | (speed_get() & 0x1ff);          /* bits 0-8 */
    g.w0 = (g.w0 & ~0xe00u) | ((gear_get() & 7) << 9);        /* bits 9-11 */
    g.w0 = (g.w0 & ~0x3000u) | ((ind_state2_bits() & 3) << 12);      /* bits 12-13 */
    g.w0 = (g.w0 & ~0xc000u) | ((ap_gauge_status() & 3) << 14);      /* bits 14-15 */
    g.w0 = (g.w0 & ~0xf0000u) | ((doors_open_mask() & 0xf) << 16);   /* bits 16-19 */
    v = lights_gauge_bits();
    ((uint8_t *)&g.w0)[2] |= v << 4;                          /* bits 20-23 (OR) */
    g.w0 = (g.w0 & ~0x7f000000u) | ((soc_get() & 0x7f) << 24); /* bits 24-30 */
    v = flag_3e3_get();
    g.w0 = (g.w0 & 0x7fffffffu) | (v << 31);                  /* bit 31 */

    /* Word 1. */
    v = vc273_bit0_get();
    g.w1 = (g.w1 & ~1u) | (v == 0);                           /* bit 0 */
    v = bf84_get() & 1;
    g.w1 = (((g_2000c268 >> 14) ^ 1) & 1) << 2 | (g.w1 & ~6u) | (v << 1); /* bits 1-2 */
    g.w1 = (g.w1 & ~8u) | ((sport_mode_get() & 1) << 3);            /* bit 3 */
    g.w1 = (g.w1 & ~0x10u) | ((can_3e2_mode_is1() & 1) << 4);         /* bit 4 */
    g.w1 = (g.w1 & ~0x20u) | ((bf85_get() & 1) << 5);         /* bit 5 */
    v = odometer_get();
    g.w1 = (g.w1 & 0x3f) | (v << 6);                          /* bits 6-31 */

    tmos_memcpy(g.b8, tpms_get(), 4);                         /* bytes 8-11 */

    /* Word 12. */
    ((uint8_t *)&g.w12)[0] = accel_pedal_get();                      /* bits 0-7 */
    g.w12 = (g.w12 & 0xfff800ffu) | ((power_get(1) & 0x7ff) << 8);   /* bits 8-18 */
    g.w12 = (g.w12 & 0xc007ffffu) | ((power_get(0) & 0x7ff) << 19);  /* bits 19-29 */
    /* Altitude (14 bits): bits 0-1 in w12 bits 30-31, bits 2-13 in h16. */
    v = altitude_get() & 0xffff;
    g.w12 = (g.w12 & 0x3fffffffu) | (v << 30);
    g.h16 = (g.h16 & 0xf000) | ((v << 18) >> 20);
    g.h16 = (g.h16 & 0xcfff) | ((heat_status_get() & 3) << 12);      /* bits 12-13 */
    g.h16 = (g.h16 & 0xbfff) | ((flag_257_get() & 1) << 14);     /* bit 14 */
    v = epas_torque_mode_get();
    g.h16 = (g.h16 & 0x7fff) | (v << 15);                     /* bit 15 */

    tmos_memcpy(g.b18, brake_temp_get(), 5);                        /* bytes 18-22 */

    uint8_t *b = (uint8_t *)&g;
    v = can_20c_v10_get();                                           /* 10 bits */
    b[23] = v;
    b[24] = ((v >> 8) & 3) | (b[24] & 0xfc);
    v = can_20c_v11_get();                                           /* 11 bits */
    b[24] = (b[24] & 3) | ((v & 0x3f) << 2);
    b[25] = ((v >> 6) & 0x1f) | (b[25] & 0xe0);
    v = can_243_value_get();                                           /* 11 bits */
    b[26] = v >> 3;
    b[25] = (b[25] & 0x1f) | ((v & 7) << 5);
    b[27] = can_321_d5_get();
    v = pack_temp_get();                                           /* 9 bits */
    b[30] = (b[30] & 0x3f) | ((v & 3) << 6);
    b[31] = ((v >> 2) & 0x7f) | (b[31] & 0x80);
    v = brick_v_get();                                           /* 12 bits */
    b[28] = v;
    b[29] = ((v >> 8) & 0xf) | (b[29] & 0xf0);
    v = range_get();                                           /* 10 bits */
    b[29] = (b[29] & 0xf) | ((v & 0xf) << 4);
    b[30] = ((v >> 4) & 0x3f) | (b[30] & 0xc0);
    v = speed_limit_index();                                           /* 5 bits */
    b[31] = (b[31] & 0x7f) | ((v & 1) << 7);
    b[32] = ((v >> 1) & 0xf) | (b[32] & 0xf0);
    v = bf06_get();
    v <<= 4;
    b[32] = (b[32] & 0xf) | v;

    for (int i = 0; ; i = 1) {
        m6_link_t *l = &g_ble_links.link[i];
        if (l->handle != LINK_FREE && l->mode == 1 && l->authed != 0)
            v = ble_send_frame(l->handle, 0xb0, &g, B0_LEN);
        if (i == 1)
            break;
    }
    return v;
}
