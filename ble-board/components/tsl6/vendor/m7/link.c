/* BLE link table and reply helpers (stock 0x1376C..0x13C76, 0x14860). */
#include "m7.h"

/* Stock 0x1376C. Copies the payload into a TMOS message and sends it as a
   frame. Nothing is sent when the allocation fails. */
uint32_t ble_send_copy(uint32_t conn, uint32_t type, void *data, uint32_t len)
{
    uint8_t *msg = msg_alloc32(len);

    if (msg == 0)
        return 0;
    tmos_memcpy(msg, data, len);
    ble_send_frame(conn, type, msg, len);
    return tmos_msg_deallocate(msg);
}

/* Stock 0x137C6. Sets the mode of the link with this handle. */
void link_set_mode(uint32_t conn, uint32_t mode)
{
    int i;

    if (conn == g_ble_links.link[0].handle)
        i = 0;
    else if (conn == g_ble_links.link[1].handle)
        i = 1;
    else
        return;
    g_ble_links.link[i].mode = (uint8_t)mode;
}

/* Stock 0x137F2. CAN 0x401 (platform 1): sends the 8 data bytes as a D1 frame
   to every authorized link in mode 4. frame = RX ring entry (data at +3).
   Returns the stock a0: the last send result, else the frame pointer. */
uint32_t link_forward_401(uint8_t *frame)
{
    uint32_t a0 = (uint32_t)frame;

    if (g_ble_links.busy != 0)
        return a0;
    for (int i = 0;; i = 1) {
        volatile ble_link_t *l = &g_ble_links.link[i];
        if (l->handle != LINK_FREE && l->mode == LINK_MODE_RAW401 && l->authed != 0)
            a0 = ble_send_frame(l->handle, 0xD1, frame + 3, 8);
        if (i == 1)
            return a0;
    }
}

/* Stock 0x1386E. Sends a B0 gauge frame when a link streams the gauge.
   At most one frame per 35 ms; a later frame waits on a 35 ms timer. */
void gauge_push(void)
{
    volatile ble_link_t *l = g_ble_links.link;

    if (!(l[0].handle != LINK_FREE && l[0].mode == LINK_MODE_GAUGE && l[0].authed != 0) &&
        !(l[1].handle != LINK_FREE && l[1].mode == LINK_MODE_GAUGE && l[1].authed != 0))
        return;
    uint32_t now = clock_ms();
    if (now - g_gauge_last_ms > 34) {
        g_gauge_last_ms = now;
        b0_gauge_send(0);
        return;
    }
    app_timer_start(b0_gauge_send, 0, 35, 0);
}

/* Stock 0x138FC. Queues a BA frame (len byte + data) for the idle links.
   The timer sends it after 10 ms, or 100 ms while a stream link is active.
   Returns 0, or the deallocate result when the timer start fails. */
uint32_t ba_send_later(void *data, uint32_t len)
{
    uint8_t *msg = msg_alloc32(len + 1);

    if (msg == 0)
        return 0;
    msg[0] = (uint8_t)len;
    tmos_memcpy(msg + 1, data, len);
    uint32_t ms = links_busy_check() ? 100 : 10;
    if (app_timer_start(ba_send_cb, 0, ms, msg) == 0)
        return 0;
    return tmos_msg_deallocate(msg);
}

/* Stock 0x13968. Sends the A0 status to the idle links (mode 0).
   No password check here. Rate limit 35 ms; 100 ms while a stream runs. */
void status_push(void)
{
    volatile ble_link_t *l = g_ble_links.link;
    uint32_t streaming = links_busy_check();

    if (!(l[0].handle != LINK_FREE && l[0].mode == LINK_MODE_IDLE) &&
        !(l[1].handle != LINK_FREE && l[1].mode == LINK_MODE_IDLE))
        return;
    if (streaming) {
        app_timer_start(a0_status_send, 0, 100, 0);
        return;
    }
    uint32_t now = clock_ms();
    if (now - g_status_last_ms > 34) {
        g_status_last_ms = now;
        a0_status_send(0);
        return;
    }
    app_timer_start(a0_status_send, 0, 35, 0);
}

/* Stock 0x139F4. Timer callback and A8/A3 follow-up: pushes the status.
   When the module is not activated, it starts the AF nag (300 ms). */
void link_auth_timer(uint8_t *arg)
{
    app_timer_stop(link_auth_timer);
    status_push();
    if (uid_crc_ok() == 0)
        app_timer_start(activation_nag, 0, 300, 0);
}

/* Stock 0x13A2E. Link table update.
   op 2: puts the handle in the first free entry (not authorized, mode 0).
   op 1: marks the link authorized (mode 0).
   other: frees the entry of this handle. */
void link_update(uint32_t conn, uint32_t op)
{
    volatile ble_link_t *l = g_ble_links.link;
    int i;

    if (op == 2) {
        if (l[0].handle == LINK_FREE)
            i = 0;
        else if (l[1].handle == LINK_FREE)
            i = 1;
        else
            return;
        l[i].handle = (uint16_t)conn;
        l[i].authed = 0;
        l[i].mode = 0;
        return;
    }
    if (conn == l[0].handle)
        i = 0;
    else if (conn == l[1].handle)
        i = 1;
    else
        return;
    if (op == 1) {
        l[i].authed = 1;
    } else {
        l[i].handle = LINK_FREE;
        l[i].authed = 0;
    }
    l[i].mode = 0;
}

/* Stock 0x13AB8. Sends a frame. A frame with data goes through a copy. */
uint32_t ble_send_any(uint32_t conn, uint32_t type, void *data, uint32_t len)
{
    if (data == 0 || len == 0)
        return ble_send_frame(conn, type, data, len);
    return ble_send_copy(conn, type, data, len);
}

/* Stock 0x13AD4. Timer callback of ba_send_later: sends BA to every
   authorized idle link, then frees the message. */
void ba_send_cb(uint8_t *msg)
{
    if (msg == 0)
        return;
    for (int i = 0;; i = 1) {
        volatile ble_link_t *l = &g_ble_links.link[i];
        if (l->handle != LINK_FREE && l->mode == LINK_MODE_IDLE && l->authed != 0)
            ble_send_any(l->handle, 0xBA, msg + 1, msg[0]);
        if (i == 1)
            break;
    }
    tmos_msg_deallocate(msg);
}

/* Queued frame: {u16 conn; u8 type; void *data (+4); u16 len (+8)}. */
typedef struct {
    uint16_t conn;
    uint8_t type;
    void *data;
    uint16_t len;
} queued_frame_t;

/* Stock 0x13B56. Timer callback: sends a queued frame, then frees it. */
void queued_frame_cb(uint8_t *p)
{
    if (p == 0)
        return;
    queued_frame_t *q = (queued_frame_t *)p;
    ble_send_any(q->conn, q->type, q->data, q->len);
    tmos_msg_deallocate(p);
}

/* Stock 0x13B86. Sends a reply with one payload byte. */
uint32_t ble_reply_byte(uint32_t conn, uint32_t type, uint32_t b)
{
    uint8_t v = (uint8_t)b;

    return ble_send_copy(conn, type, &v, 1);
}

/* Stock 0x13B9E. Sends BD with one byte (an error reply). */
uint32_t ble_reply_bd(uint32_t conn, uint32_t b)
{
    return ble_reply_byte(conn, 0xBD, b);
}

/* Stock 0x13BA8. Timer callback when the module is not activated:
   sends AF 02 to every authorized idle link, then starts 0x94EA via 0x94D0. */
void activation_nag(uint8_t *arg)
{
    volatile ble_link_t *l = g_ble_links.link;

    if (l[0].handle != LINK_FREE && l[0].mode == LINK_MODE_IDLE && l[0].authed != 0)
        ble_reply_byte(l[0].handle, 0xAF, 2);
    if (l[1].handle != LINK_FREE && l[1].mode == LINK_MODE_IDLE && l[1].authed != 0)
        ble_reply_byte(l[1].handle, 0xAF, 2);
    buzzer_play(tune_two_low, 0, 1);
}

/* Stock 0x13C2C. Factory reset (0x12F30: all flash blocks and bonds), then
   replies (type, 01), disconnects after 100 ms and resets the chip after
   200 ms. A4 and AD use it. */
void factory_reset_reply(uint32_t conn, uint32_t type)
{
    factory_reset();
    app_timer_start(ble_disconnect, 0, 100, 0);
    app_timer_start(system_reset, 0, 200, 0);
    ble_reply_byte(conn, type, 1);
}

/* Stock 0x14860. Clears the link table: both entries free. */
void links_init(void)
{
    tmos_memset((void *)&g_ble_links, 0, 10);
    g_ble_links.link[0].handle = LINK_FREE;
    g_ble_links.link[0].authed = 0;
    g_ble_links.link[0].mode = 0;
    g_ble_links.link[1].handle = LINK_FREE;
    g_ble_links.link[1].authed = 0;
    g_ble_links.link[1].mode = 0;
}
