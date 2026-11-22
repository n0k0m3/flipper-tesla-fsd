/* BLE command dispatcher (stock 0x13C76..0x14860).
   Frame: 55 7F TYPE LEN PAYLOAD CHK. The BLE layer calls this function with
   the connection handle, TYPE, the payload and its length. */
#include "m7.h"

/* 256-byte tables that the AB/BA/B9 commands write (pointers in .data,
   names from m6). */
extern uint8_t *g_ba_table_p;   /* 0x20004078 (gp+120): BA BLE button table */
extern uint8_t *g_ab_table_p;   /* 0x20004080 (gp+128): AB shortcut table */
extern uint8_t *g_rgb_zone_p;   /* 0x20004084 (gp+132): B9 tab 0 (RGB zone link) */
extern uint8_t *g_rgb_car_p;    /* 0x20004088 (gp+136): B9 tab 1 (RGB whole car) */

/* Stock functions that the commands call. */
uint32_t ota_cmd(uint32_t conn, uint32_t type, uint8_t *data, uint32_t len);
uint32_t uid14_check(uint8_t *data, uint32_t len);  /* uid14_check: 0 when len = 14 and data = UID14 */
uint32_t uid_crc(void);  /* uid_crc: CRC of UID bytes 0..11 = the AF code */
void bootflag_set_crc(uint32_t v);  /* bootflag_set_crc: stores v as the UID CRC in the boot flag */
void features_off_later(void);
void led_set_hold(uint32_t v);
void sport_mode_restore(void);
void cfg_save(void);  /* cfg_save: saves the config */
void tune_up(uint8_t *arg);  /* tune_up */
void bus_age_clear(uint32_t v);  /* bus_age_clear */
void gesture_start(uint32_t v);
void cmd_a7_control(uint32_t conn, uint32_t type, uint32_t v);  /* cmd_a7_control */
void reengage_off(void);
void reengage_cancel(void);
void ab_table_send(uint32_t conn);  /* ab_table_send */
void ba_table_send(uint32_t conn);  /* ba_table_send */
void ab_table_save(void);  /* ab_table_save */
void ba_table_save(void);  /* ba_table_save */
void rgb_zone_save(void);  /* rgb_zone_save */
void rgb_car_save(void);  /* rgb_car_save */
void rgb_zone_reset(void);  /* rgb_zone_reset */
void rgb_car_reset(void);  /* rgb_car_reset */
void central_scan_start(void);  /* central_scan_start */
void central_scan_set(uint32_t a, uint32_t b);  /* central_scan_set */
uint32_t rf_hello_start(void);  /* rf_hello_start */
void light_rec_apply(uint32_t a, uint32_t b);  /* light_rec_apply */
void b9_table_reply(uint32_t conn, uint32_t a, uint32_t b);  /* B9 table reply (m6 notes; not the B0 builder) */
void tick_start(uint32_t on, uint32_t b);
void cmd_f0_remote(uint32_t conn, uint8_t *data, uint32_t len);  /* cmd_f0_remote */
void frame_slot_copy(uint32_t dir, uint32_t slot, void *buf);  /* frame_slot_copy */
uint32_t range_get(void);  /* range_get */
uint32_t pack_temp_get(void);  /* pack_temp_get */
uint32_t heat_status_get(void);  /* heat_status_get */
uint32_t soc_get(void);  /* soc_get */
uint32_t odometer_get(void);  /* odometer_get */
void platform_stop(void);
void platform_set(uint32_t v);
void can_filter_accept_all(uint32_t n);  /* can_filter_accept_all */
void can_filter_close(uint32_t n);  /* can_filter_close */
void can_filter_set(uint32_t n, void *list, uint32_t size);  /* can_filter_set */
void can_tx(void *frame);  /* can_tx */

/* Little-endian access to a byte buffer. */
static uint32_t get32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Bytes hi:lo of a 12-byte CAN slot as a 16-bit value. */
static uint32_t be16(const uint8_t *b, int lo)
{
    return (uint32_t)b[lo + 1] << 8 | b[lo];
}

/* D2 01: 8-byte reply from CAN slot 4. */
static void cmd_d2_info(uint32_t conn)
{
    uint8_t q[8];
    uint8_t c[12];

    tmos_memset(q, 0, 8);
    frame_slot_copy(1, 4, c);
    put16(q + 0, be16(c, 3));
    put16(q + 2, be16(c, 5));
    put16(q + 4, be16(c, 7));
    ble_send_frame(conn, 0xD2, q, 8);
}

/* D0 01: 29-byte vehicle info reply, built from CAN slots 1, 11, 5, 6, 7, 3
   and the range, pack temperature, heating, SOC and odometer getters. */
static void cmd_d0_info(uint32_t conn, uint32_t type)
{
    uint8_t p[32];
    uint8_t b[12];
    uint32_t w, r, v1, v2;

    tmos_memset(p, 0, 29);
    frame_slot_copy(1, 1, b);
    put16(p + 0, be16(b, 3));
    put16(p + 2, be16(b, 5));
    frame_slot_copy(1, 11, b);
    put32(p + 4, ((uint32_t)b[4] << 8) + ((uint32_t)b[5] << 16) + b[3] + ((uint32_t)b[6] << 24));
    put32(p + 8, ((uint32_t)b[8] << 8) + ((uint32_t)b[9] << 16) + b[7] + ((uint32_t)b[10] << 24));
    frame_slot_copy(1, 5, b);
    v1 = (((get32(b + 4) & 63) << 6) | (get32(b) >> 26)) & 0xFFF;
    v2 = (get32(b + 4) >> 8) & 0xFFF;
    w = (get32(p + 16) & 0xF000FFFF) | v1 << 16;
    w = (w & 0x0FFFFFFF) | v2 << 28;
    put32(p + 16, w);
    p[20] = (uint8_t)(v2 >> 4);
    frame_slot_copy(1, 6, b);
    put16(p + 14, be16(b, 5));
    put16(p + 12, be16(b, 7));
    frame_slot_copy(1, 7, b);
    put16(p + 16, be16(b, 5));
    frame_slot_copy(1, 3, b);
    w = (get32(p + 20) & 0xFFFC00FF) | (be16(b, 8) & 0x3FF) << 8;
    put32(p + 20, w);
    r = range_get();
    put32(p + 20, (get32(p + 20) & 0xF003FFFF) | (r & 0x3FF) << 18);
    r = pack_temp_get();
    put32(p + 20, (get32(p + 20) & 0x3FFFFFFF) | r << 30);
    put32(p + 24, (get32(p + 24) & ~0x7Fu) | ((r >> 2) & 0x7F));
    r = heat_status_get();
    put32(p + 20, (get32(p + 20) & 0xCFFFFFFF) | (r & 3) << 28);
    r = soc_get();
    put32(p + 24, (get32(p + 24) & 0xFFFFC07F) | (r & 0x7F) << 7);
    r = odometer_get();
    put32(p + 24, (get32(p + 24) & 0x3FFF) | r << 14);
    p[28] = (uint8_t)(r >> 18);
    ble_send_frame(conn, type, p, 29);
}

/* C0 FF FF id...: sorts the 16-bit ids into three filter lists.
   ids <= 0x7FF: list 0 (56 entries); bit 12: list 1; bit 13: list 2 (6 each).
   Stock bugs, kept: the counters are 8 bits and do not stop at the list size.
   An overflow of list 1 runs into list 2 and then list 0 (the same order as
   the stock stack frame). The index is 8 bits too, so more than 255 ids loop
   forever. */
static void cmd_c0_lists(uint8_t *data, uint32_t len)
{
    /* One array in the stock stack order: l1[6], l2[6], l0[56]. */
    uint16_t a[68];
    uint16_t *l1 = a, *l2 = a + 6, *l0 = a + 12;
    uint32_t n = ((len - 2) & 0xFFFF) >> 1;
    uint32_t i = 0, c0 = 0, c1 = 0, c2 = 0;

    tmos_memset(l0, 0, 112);
    tmos_memset(l1, 0, 12);
    tmos_memset(l2, 0, 12);
    while (i < n) {
        i++;
        uint16_t v = *(uint16_t *)(data + i * 2);
        if (v <= 0x7FF) {
            a[12 + c0] = v;  /* l0[c0] */
            c0 = (c0 + 1) & 0xFF;
        } else if (v & 0x1000) {
            a[c1] = v;  /* l1[c1] */
            c1 = (c1 + 1) & 0xFF;
        } else if (v & 0x2000) {
            a[6 + c2] = v;  /* l2[c2] */
            c2 = (c2 + 1) & 0xFF;
        }
        i &= 0xFF;
    }
    can_filter_set(0, l0, 112);
    can_filter_set(1, l1, 12);
    can_filter_set(2, l2, 12);
}

/* C0 id_hi id_lo data...: sends one CAN frame (up to 8 bytes). */
static void cmd_c0_send(uint8_t *data, uint32_t m)
{
    struct {
        uint16_t id;
        uint8_t dlc;
        uint8_t data[8];
        uint8_t bus;  /* +11 */
    } f;

    f.id = (uint16_t)(data[0] << 8 | data[1]);
    f.dlc = (uint8_t)m;
    tmos_memcpy(f.data, data + 2, m & 0xFF);
    if (f.id <= 0x7FF) {
        f.bus = 0;
    } else {
        /* Stock bug, kept: with bits 12 and 13 clear, bus keeps the old
           stack byte. */
        if (f.id & 0x1000)
            f.bus = 1;
        if (f.id & 0x2000)
            f.bus = 2;
    }
    bus_age_clear(*(volatile uint8_t *)&f.bus);
    can_tx(&f);
}

/* C0: CAN tool. len 1: 01 = mode on (lists open), 00 = off, 02 = lists closed.
   FF FF ...: filter lists. Other: send a frame. Replies BC 01. */
static void cmd_c0(uint32_t conn, uint8_t *data, uint32_t len)
{
    uint8_t b = data[0];

    if (len == 1) {
        if (b == 1) {
            link_set_mode(conn, LINK_MODE_CAN);
            can_filter_accept_all(0);
            can_filter_accept_all(1);
            can_filter_accept_all(2);
        } else if (b == 0) {
            link_set_mode(conn, LINK_MODE_IDLE);
            bus_age_clear(0);
            platform_set(0);
        } else if (b == 2) {
            link_set_mode(conn, LINK_MODE_CAN);
            can_filter_close(0);
            can_filter_close(1);
            can_filter_close(2);
        }
        return;
    }
    if (b == 0xFF && data[1] == 0xFF) {
        link_set_mode(conn, LINK_MODE_CAN);
        platform_stop();
        uint16_t v = *(uint16_t *)(data + 2);
        if (v == 0x1000) {
            can_filter_accept_all(1);
            can_filter_close(0);
            can_filter_close(2);
        } else if (v == 0x2000) {
            can_filter_accept_all(2);
            can_filter_close(0);
            can_filter_close(1);
        } else if (v == 0) {
            can_filter_accept_all(0);
            can_filter_close(1);
            can_filter_close(2);
        } else {
            cmd_c0_lists(data, len);
        }
    } else {
        uint32_t m = len - 2;
        if ((m & 0xFFFF) > 8)
            return;
        link_set_mode(conn, LINK_MODE_CAN);
        platform_stop();
        cmd_c0_send(data, m);
    }
    ble_reply_byte(conn, 0xBC, 1);
}

/* A3: settings write (76 bytes). Bytes 4..17 = UID14, bytes 36..39 = the new
   password (printable ASCII). Copies bytes 24..39 to config 0..15 and bytes
   59..74 to config 96..111. A bad frame gets BD 00. */
static void cmd_a3_settings(uint32_t conn, uint8_t *data, uint32_t len)
{
    uint8_t *msg;
    int ok;

    if (len != 76)
        goto bad;
    msg = msg_alloc32(76);
    if (msg == 0)
        goto bad;
    tmos_memcpy(msg, data, 76);
    ok = uid14_check(msg + 4, 14) == 0;
    for (uint8_t *p = msg + 36; p != msg + 40; p++) {
        if ((uint8_t)(*p - 32) > 94) {
            ok = 0;
            break;
        }
    }
    tmos_msg_deallocate(msg);
    if (!ok)
        goto bad;
    if ((g_cfg[3] ^ data[27]) & 0x10)
        led_set_hold((data[27] >> 4) & 1);
    if ((g_cfg[0] ^ data[24]) & 7)
        sport_mode_restore();
    tmos_memcpy(g_cfg, data + 24, 16);
    tmos_memcpy(g_cfg + 96, data + 59, 16);
    cfg_save();
    app_timer_start(tune_up, 0, 100, 0);
    link_auth_timer(0);
    if (g_cfg[4] & 8)
        features_off_later();
    return;
bad:
    buzzer_play(tune_two_low, 0, 1);
    ble_reply_bd(conn, 0);
}

/* A9: password change. Bytes 18..21 = old password, 22..25 = new one.
   Replies A9 02 (short), A9 00 (wrong old password) or A9 01, then
   removes the link and disconnects. The fixed key does not work here. */
static void cmd_a9_password(uint32_t conn, uint8_t *data, uint32_t len)
{
    if (len <= 25) {
        ble_reply_byte(conn, 0xA9, 2);
        return;
    }
    uint8_t *pw = g_cfg + 12;
    for (int i = 0; i < 4; i++) {
        if (data[18 + i] != pw[i]) {
            ble_reply_byte(conn, 0xA9, 0);
            return;
        }
    }
    tmos_memcpy(pw, data + 22, 4);
    cfg_save();
    link_update(conn, 0);
    ble_reply_byte(conn, 0xA9, 1);
    app_timer_start(ble_disconnect, 0, 100, 0);
}


/* AB / BA with 256 bytes: table upload, the last byte must be 'Z'. */
static int table_ok(uint32_t conn, uint8_t *data)
{
    if (data[255] != 'Z') {
        ble_reply_bd(conn, 0);
        return 0;
    }
    return 1;
}

/* B9: 0 = query, 1 = table upload (258 bytes), 2 = set, 3 = reset table.
   Each path ends with a b9_table_reply reply. */
static void cmd_b9(uint32_t conn, uint8_t *data, uint32_t len)
{
    uint32_t flag = 0;

    if (len <= 1)
        return;
    switch (data[0]) {
    case 0:
        rf_hello_start();
        break;
    case 1:
        if (rf_hello_start() == 0) {
            ble_reply_byte(conn, 0xB9, 0xFF);
            flag = 1;
            break;
        }
        if (len != 258)
            return;
        if (data[257] != 'Z') {
            ble_reply_bd(conn, 0);
        } else if (data[1] == 0) {
            tmos_memcpy(g_rgb_zone_p, data + 2, 256);
            rgb_zone_save();
        } else {
            tmos_memcpy(g_rgb_car_p, data + 2, 256);
            rgb_car_save();
        }
        app_timer_start(tune_up, 0, 100, 0);
        break;
    case 2:
        if (rf_hello_start() == 0) {
            ble_reply_byte(conn, 0xB9, 0xFF);
            return;
        }
        if (data[2] > 35)
            return;
        light_rec_apply(data[1], data[2]);
        /* Stock replies BB 01 here, not B9. */
        ble_reply_byte(conn, 0xBB, 1);
        return;
    case 3:
        rf_hello_start();
        if (data[1] == 0)
            rgb_zone_reset();
        else
            rgb_car_reset();
        app_timer_start(tune_up, 0, 100, 0);
        break;
    default:
        return;
    }
    b9_table_reply(conn, data[1], flag);
}

/* Commands that need the "activation" (uid_crc_ok = uid_crc_ok: the boot flag
   holds the CRC of the chip UID, written by AF). The others run anyway. */
static int needs_activation(uint32_t type)
{
    if (type > 0xBB) {
        if (type > 0xD2)
            return type == 0xF0;
        if (type >= 0xD0)
            return 1;
        return ((type + 64) & 0xFF) <= 1;  /* C0, C1 */
    }
    if (type >= 0xB9)
        return 1;
    if (type > 0xA7) {
        if (type < 0xAA)
            return 0;
        if (type <= 0xAB)
            return 1;
        return type == 0xB0;
    }
    if (type >= 0xA3)
        return 1;
    return type == 0xA1;
}

/* Stock 0x13C76. */
void ble_cmd_dispatch(uint32_t conn, uint32_t type, uint8_t *data, uint32_t len)
{
    int i;

    /* port: no OTA, activation or A8 password commands. The board links are
       authorized by the BLE pairing (tsl6_ble_connect). */

    /* Every other command needs an authorized link. */
    if (conn == g_ble_links.link[0].handle) {
        i = 0;
    } else if (conn == g_ble_links.link[1].handle) {
        i = 1;
    } else {
        link_update(conn, 2);
        goto no_auth;
    }
    if (g_ble_links.link[i].authed == 0)
        goto no_auth;
    if (needs_activation(type) && uid_crc_ok() == 0) {
        app_timer_start(activation_nag, 0, 300, 0);
        return;
    }

    switch (type) {
    case 0xA0:  /* status: link back to idle mode, push the status */
        link_set_mode(conn, LINK_MODE_IDLE);
        link_auth_timer(0);
        return;
    case 0xA1:  /* no-nag function on/off */
        tick_start(data[0] != 0, 1);
        return;
    case 0xA2:
        if ((uint8_t)(data[0] - 1) <= 4)
            gesture_start(data[0]);
        ble_reply_byte(conn, 0xA2, data[0]);
        return;
    case 0xA3:
        cmd_a3_settings(conn, data, len);
        return;
    case 0xA4:  /* UID14, then factory reset, reply A4 01 and restart */
        if (uid14_check(data, len & 0xFF) != 0)
            return;
        factory_reset_reply(conn, 0xA4);
        return;
    case 0xA5:  /* UID14, then restart (no factory reset, unlike A4) */
        if (uid14_check(data, len & 0xFF) != 0)
            return;
        app_timer_start(ble_disconnect, 0, 100, 0);
        app_timer_start(system_reset, 0, 200, 0);
        ble_reply_byte(conn, 0xA5, 1);
        return;
    case 0xA6:
        if (data[0] == 0)
            reengage_off();
        else
            reengage_cancel();
        return;
    case 0xA7:  /* car controls */
        cmd_a7_control(conn, 0xA7, data[0]);
        return;
    case 0xA9:
        cmd_a9_password(conn, data, len);
        return;
    case 0xAB:
        if (len == 1) {
            if (data[0] == 2)
                ab_table_send(conn);
            return;
        }
        if (len != 256 || !table_ok(conn, data))
            return;
        tmos_memcpy(g_ab_table_p, data, 256);
        ab_table_save();
        ab_table_send(conn);
        app_timer_start(tune_up, 0, 100, 0);
        return;
    case 0xB0:  /* gauge stream on (01) or off */
        if (data[0] == 1) {
            link_set_mode(conn, LINK_MODE_GAUGE);
            gauge_push();
        } else {
            link_set_mode(conn, LINK_MODE_IDLE);
        }
        return;
    case 0xB9:
        cmd_b9(conn, data, len);
        return;
    case 0xBA:
        if (len == 1) {
            if (data[0] == 1) {
                central_scan_start();
                ble_send_any(conn, 0xBA, data, 1);
            } else if (data[0] == 2) {
                central_scan_set(1, 0);
                ba_table_send(conn);
            }
            return;
        }
        if (len != 256 || !table_ok(conn, data))
            return;
        tmos_memcpy(g_ba_table_p, data, 256);
        ba_table_save();
        central_scan_set(1, 0);
        ba_table_send(conn);
        app_timer_start(tune_up, 0, 100, 0);
        return;
    case 0xBB:  /* button action by code */
        button_action(data[0]);
        ble_reply_byte(conn, 0xBB, 1);
        return;
    case 0xC0:
        cmd_c0(conn, data, len);
        return;
    case 0xC1:  /* 32-bit state word */
    {
        uint32_t v = car_state_word();
        ble_send_copy(conn, 0xC1, &v, 4);
        return;
    }
    case 0xD0:
    case 0xD2:
        if (data[0] != 1)
            return;
        link_set_mode(conn, LINK_MODE_INFO);
        if (g_ble_links.busy != 0)
            return;
        if (type == 0xD2)
            cmd_d2_info(conn);
        else
            cmd_d0_info(conn, type);
        return;
    case 0xD1:
        if (data[0] == 1)
            link_set_mode(conn, LINK_MODE_RAW401);
        return;
    case 0xF0:  /* UID14 + payload */
        if (len <= 13)
            return;
        if (uid14_check(data, 14) != 0)
            return;
        cmd_f0_remote(conn, data + 14, (len - 14) & 0xFFFF);
        return;
    default:
        return;
    }

no_auth:
    app_timer_start(link_auth_timer, 0, 300, 0);
    ble_reply_byte(conn, 0xA8, 0xFF);
}
