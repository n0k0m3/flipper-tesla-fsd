/* 0x3E2 and 0x3C3 (right steering-wheel switches) decoders.
   Stock 0xC0D4..0xC3A8.
   Button codes go to the button state machine 0xE896 (code, idle code, state).
   Switch changes go to 0xE992, which raises action events (0x16310). */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xC0D4. 0x3E2 handler.
   data[1] bit 6, data[1] bit 7, data[2] bit 0, data[2] bit 1 give the button
   codes 0x47, 0x48, 0x49, 0x4A (first match wins); 0x46 is idle.
   data[0] bits 1:0 is a mode; a change gives event 0x4E (mode 1) or 0x4F. */
void can_3e2_rx(m3_frame_t *f)
{
    uint32_t code;
    uint32_t mode;

    if (f->data[1] & 0x40)
        code = 0x47;
    else if (f->data[1] & 0x80)
        code = 0x48;
    else if (f->data[2] & 1)
        code = 0x49;
    else if (f->data[2] & 2)
        code = 0x4A;
    else
        code = 0x46;
    key_track(code, 0x46, g_3e2_btn);

    mode = f->data[0] & 3;
    if (g_3e2_mode == mode)
        return;
    g_3e2_mode = (uint8_t)mode;
    key_event(mode == 1 ? 0x4E : 0x4F, 1, 0);
    gauge_push();
}

/* Stock 0xC162. */
uint32_t can_3e2_mode_is1(void)
{
    return g_3e2_mode == 1;
}

/* Bit fields of 0x3C3 page 1 in test order, with their button codes.
   The match value is the field value that counts as pressed (0 = any bit). */
typedef struct {
    uint8_t idx;    /* data index */
    uint8_t mask;
    uint8_t match;  /* 0: pressed when any bit is set */
    uint8_t code;
} btn_field_t;

static const btn_field_t k_3c3_fields[] = {
    {4, 0x0C, 0, 0x13}, {4, 0x30, 0, 0x14}, {3, 0x0C, 0, 0x15}, {3, 0x30, 0, 0x16},
    {4, 0xC0, 0, 0x17}, {5, 0x03, 0, 0x18}, {3, 0xC0, 0, 0x19}, {4, 0x03, 0, 0x1A},
    {0, 0x0C, 0x08, 0x2B}, {0, 0x30, 0x20, 0x2C}, {0, 0xC0, 0x80, 0x2D},
    {1, 0x03, 0x02, 0x2E}, {1, 0x0C, 0x08, 0x2F}, {1, 0x30, 0x20, 0x30}, {1, 0xC0, 0x80, 0x31},
    {2, 0x03, 0x02, 0x32}, {3, 0x03, 0x02, 0x1F}, {2, 0xC0, 0x80, 0x20},
    {2, 0x30, 0x20, 0x21}, {2, 0x0C, 0x08, 0x22},
};

/* Stock 0xC172. 0x3C3 handler.
   Page 0 (data[0] bits 1:0 = 0): three 2-bit switches in data[5]. A change of
   the first one with config byte 1 bits 4:3 = 1 also runs action 0x17450(5 or 6).
   Page 1: the first pressed field (table above) gives the button code; 0 = idle. */
void can_3c3_rx(m3_frame_t *f)
{
    uint32_t page = f->data[0] & 3;

    if (page == 0) {
        if (trigger_on_change(&g_3c3_sw0, f->data[5] & 3, 0x78, 0x79, 2, 1)
            && (g_cfg[1] & 0x18) == 8) {
            if (g_3c3_sw0 == 1)
                seat_recall(5);
            else if (g_3c3_sw0 == 2)
                seat_recall(6);
        }
        trigger_on_change(&g_3c3_sw1, (f->data[5] >> 2) & 3, 0x80, 0x81, 2, 1);
        trigger_on_change(&g_3c3_sw2, f->data[5] >> 6, 0x88, 0x89, 2, 1);
        return;
    }
    if (page != 1)
        return;

    uint32_t code = 0;
    for (uint32_t i = 0; i < ARRAY_LEN(k_3c3_fields); i++) {
        const btn_field_t *b = &k_3c3_fields[i];
        uint32_t v = f->data[b->idx] & b->mask;
        if (b->match ? v == b->match : v != 0) {
            code = b->code;
            break;
        }
    }
    key_track(code, 0, g_3c3_btn);
}

/* Stock 0xC30E. Edits 0x3C3 page 0 and sends it.
   Config byte 10 bit 2: data[5] bits 3:0 = 9 becomes bits 1:0 = 2.
   Config byte 10 bit 3: with cached 0x3C2 page 0 data[7] bits 3:2 = 2,
   data[5] bits 7:6 = 1 becomes 2. */
void can_3c3_edit(m3_frame_t *f)
{
    uint32_t cfg;
    uint32_t send = 0;

    if (f->data[0] & 3)
        return;
    cfg = g_cfg[10];
    if ((cfg & 4) && (f->data[5] & 0xF) == 9) {
        f->data[5] = (f->data[5] & 0xFC) | 2;
        send = 1;
    }
    if ((cfg & 8) && (g_2000c24a & 0xC) == 8 && (f->data[5] & 0xC0) == 0x40) {
        f->data[5] = (f->data[5] & 0x3F) | 0x80;
        can_tx(f);
        return;
    }
    if (send)
        can_tx(f);
}

/* Stock 0xC37A. Clears the 0x3E2 state (0x2000BF38, 12 bytes) and the
   0x3C3 button state (0x200040F4, 8 bytes). */
void can_3c3_3e2_clear(void)
{
    tmos_memset(&g_3e2_mode, 0, 12);
    tmos_memset(g_3c3_btn, 0, 8);
}
