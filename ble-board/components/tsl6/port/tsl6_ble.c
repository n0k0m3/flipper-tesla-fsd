/*
 * tsl6_ble.c — the TSL6 BLE frame link (stock m1/ble.c) on the BLE service of
 * the board. The board gives each phone link that subscribes to its TSL6
 * characteristic to tsl6_ble_connect(), and each write to tsl6_ble_rx(). The
 * engine sends its frames through hal->ble_send.
 *
 * Frames on the wire, as on the TSL6: 55 7F TYPE LENH LENL PAYLOAD CHK, with
 * CHK = 8-bit sum of TYPE, LENH, LENL and the payload.
 *
 * Not ported: advertising, the GATT service, the MTU and connection parameter
 * requests (the board's NimBLE service does them), and the A8 password. The
 * BLE pairing of the board authorizes a link: tsl6_ble_connect() marks it as
 * authorized, as a good A8 does on the TSL6.
 */
#include <string.h>

#include "tsl6_port.h"
#include "tsl6_engine.h"

#define LINKS 2
#define CONN_NONE 0xFFFEu
#define RX_MAX 1040u  /* stock BLE_RX_MAX */
#define TX_MAX 1040u  /* stock BLE_TX_MAX: payload limit of one frame */
#define RX_GAP_TICKS 480u  /* 300 ms: the parser starts again after a pause */

/* m7 link table (stock 0x2000C7B0). The engine owns it; ble_can_sniff and
   ble_c0_send read it as the stock m1 code does. */
typedef struct {
    uint8_t busy;
    uint8_t pad;
    struct {
        uint16_t handle;
        uint8_t authed;
        uint8_t mode;
    } link[2];
} links_t;
extern links_t g_ble_links;

void ble_cmd_dispatch(uint32_t conn, uint32_t type, uint8_t *data, uint32_t len);
void link_update(uint32_t conn, uint32_t op);
void link_auth_timer(uint8_t *arg);

typedef struct {
    uint16_t conn;   /* CONN_NONE = free */
    uint8_t state;   /* frame parser state, stock g_rx_state */
    uint8_t type;
    uint8_t sum;
    uint16_t len;
    uint16_t idx;
    uint32_t last;   /* tick of the last write */
    uint8_t buf[RX_MAX];
} rx_t;

static rx_t s_rx[LINKS];
static uint8_t s_tx[TX_MAX + 6];

void tsl6_ble_reset(void)
{
    memset(s_rx, 0, sizeof(s_rx));
    for (int i = 0; i < LINKS; i++)
        s_rx[i].conn = CONN_NONE;
}

static rx_t *rx_of(uint32_t conn)
{
    for (int i = 0; i < LINKS; i++)
        if (s_rx[i].conn == conn)
            return &s_rx[i];
    return NULL;
}

/* A phone link subscribed to the TSL6 characteristic. Returns 0, or -1 when
   both TSL6 links are taken (stock: the third link is dropped). */
int tsl6_ble_connect(uint16_t conn)
{
    if (rx_of(conn))
        return 0;
    rx_t *r = rx_of(CONN_NONE);
    if (!r)
        return -1;
    memset(r, 0, sizeof(*r));
    r->conn = conn;
    link_update(conn, 2);   /* the link table entry */
    link_update(conn, 1);   /* authorized: the board pairing stands for A8 */
    link_auth_timer(0);     /* pushes the status, as after a good A8 */
    return 0;
}

/* The phone links now (for an engine restart). */
int tsl6_ble_links(uint16_t out[2])
{
    int n = 0;
    for (int i = 0; i < LINKS; i++)
        if (s_rx[i].conn != CONN_NONE)
            out[n++] = s_rx[i].conn;
    return n;
}

void tsl6_ble_disconnect(uint16_t conn)
{
    rx_t *r = rx_of(conn);
    if (!r)
        return;
    r->conn = CONN_NONE;
    link_update(conn, 0);
}

/* Stock ble_write_cb: the frame parser. A full frame with a good checksum
   goes to ble_cmd_dispatch. */
void tsl6_ble_rx(uint16_t conn, const uint8_t *value, uint16_t len)
{
    rx_t *r = rx_of(conn);
    if (!r)
        return;
    uint32_t now = port_hal ? port_hal->now_ticks() : 0;
    if (now - r->last >= RX_GAP_TICKS)
        r->state = 0;
    r->last = now;

    for (uint16_t i = 0; i < len; i++) {
        uint8_t b = value[i];
        switch (r->state) {
        case 0:
            if (b != 0x55)
                goto reset;
            r->state = 1;
            continue;
        case 1:
            if (b != 0x7F)
                goto reset;
            r->state = 2;
            r->sum = 0;
            continue;
        case 2:
            r->state = 3;
            r->type = b;
            break;
        case 3:
            r->state = 4;
            r->len = (uint16_t)(b << 8);
            break;
        case 4:
            r->idx = 0;
            r->len |= b;
            if (r->len == 0)
                r->state = 6;
            else if (r->len <= RX_MAX)
                r->state = 5;
            else
                r->state = 0;
            break;
        case 5:
            if (r->idx >= RX_MAX)
                goto reset;
            r->buf[r->idx++] = b;
            if (r->idx >= r->len)
                r->state = 6;
            break;
        case 6:
            if (r->sum == b)
                ble_cmd_dispatch(conn, r->type, r->buf, r->len);
            goto reset;
        default:
            goto reset;
        }
        r->sum += b;
        continue;
reset:
        r->state = 0;
    }
}

/* Stock 0x8D02: builds a frame and sends it on the link. Nothing is sent when
   the payload is too long or the link is not a TSL6 link. */
uint32_t ble_send_frame(uint16_t conn, uint8_t type, const uint8_t *data, uint16_t len)
{
    if (len > TX_MAX - 1 || !rx_of(conn) || !port_hal || !port_hal->ble_send)
        return conn;
    uint8_t *p = s_tx;
    uint8_t sum = (uint8_t)((len >> 8) + (uint8_t)len + type);
    p[0] = 0x55;
    p[1] = 0x7F;
    p[2] = type;
    p[3] = (uint8_t)(len >> 8);
    p[4] = (uint8_t)len;
    for (uint16_t k = 0; k < len; k++) {
        p[5 + k] = data[k];
        sum += data[k];
    }
    p[5 + len] = sum;
    return (uint32_t)port_hal->ble_send(conn, p, (uint16_t)(len + 6));
}

/* Stock 0x82B8: CAN sniff (C0 tool). Sends the frame as C0 to each authorized
   link in mode 2. Returns the authed byte of the last such link, or 0: a
   sniffed frame does not go to the car features (stock can_rx_poll). */
uint8_t ble_can_sniff(const tsl6_frame_t *f)
{
    uint8_t ret = 0;
    uint8_t buf[2 + 8 + 4];

    for (int i = 0; i < 2; i++) {
        if (g_ble_links.link[i].handle != CONN_NONE && g_ble_links.link[i].mode == 2 &&
            g_ble_links.link[i].authed != 0) {
            ret = g_ble_links.link[i].authed;
            if (f->dlc <= 8) {
                buf[1] = (uint8_t)f->id;
                buf[0] = (uint8_t)((f->id >> 8) | (f->flag << 4));
                memcpy(&buf[2], f->data, f->dlc);
                ble_send_frame(g_ble_links.link[i].handle, 0xC0, buf, (uint16_t)(f->dlc + 2));
            }
        }
    }
    return ret;
}

/* Stock 0x8350: a report of a BLE remote, as C0, to each authorized link in
   mode 2. id goes big endian, the data up to 30 bytes. */
uint8_t ble_c0_send(uint32_t id, void *data, uint32_t len)
{
    uint8_t ret = 0;
    uint8_t buf[2 + 30];

    for (int i = 0; i < 2; i++) {
        if (g_ble_links.link[i].handle != CONN_NONE && g_ble_links.link[i].mode == 2 &&
            g_ble_links.link[i].authed != 0) {
            if (len > 30)
                len = 30;
            buf[0] = (uint8_t)(id >> 8);   /* stock stores (id << 8 | id >> 8) as u16 LE: big endian */
            buf[1] = (uint8_t)id;
            memcpy(buf + 2, data, len);
            ble_send_frame(g_ble_links.link[i].handle, 0xC0, buf, (uint16_t)(len + 2));
            ret = g_ble_links.link[i].authed;
        }
    }
    return ret;
}

/* Stock 0x8E1A drops both links (after a password change or a factory
   reset). The board keeps its links: the phone app has its own session. */
uint32_t ble_disconnect(void) { return 0; }

/* Stock: advertising with a code in the name (BLE button pairing hints). The
   board advertises with its own service. */
uint8_t adv_restart(uint8_t code, uint8_t long_wait)
{
    (void)code;
    (void)long_wait;
    return 0;
}
