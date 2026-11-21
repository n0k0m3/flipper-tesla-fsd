/* 0x678 frame: counter check and an edited copy with a request nibble
   (stock 0xFC6C..0xFD56). data[0] is a checksum (frame_checksum_first), data[1] bits 3:0
   are a counter. */
#include "tsl6.h"
#include "m5.h"

extern uint8_t g_20004150;   /* u8: request code (1..4) */
extern uint8_t g_20004151;   /* u8: edited frames left to send */
extern uint8_t g_20004152;   /* u8: good frames counted */
extern uint8_t g_20004186;   /* u8: 1 after 5 good 0x678 frames */

/* Stock 0xFC6C. Counts 0x678 frames with a good checksum and counter <= 15.
   On the 5th good frame it sets 0x20004186 and stops counting. */
void can_678_count(m5_frame_t *f)
{
    if (g_20004186 != 0 || f->data[1] > 15)
        return;
    if (f->data[0] != frame_checksum_first(f))
        return;
    uint8_t n = (uint8_t)(g_20004152 + 1);
    if (n <= 4) {
        g_20004152 = n;
    } else {
        g_20004152 = 0;
        g_20004186 = 1;
    }
}

/* Stock 0xFCB8. Sends a copy of the frame with the next counter, the request
   bit in data[1] bits 7:4 and a new checksum, while frames are left to send. */
void can_678_edit(m5_frame_t *f)
{
    if (g_20004151 == 0 || g_20004186 == 0)
        return;
    uint8_t v = (f->data[1] + 1) & 0x0F;
    switch (g_20004150) {
    case 1: v |= 0x10; break;
    case 2: v |= 0x20; break;
    case 3: v |= 0x80; break;
    case 4: v |= 0x40; break;
    default: break;
    }
    f->data[1] = v;
    f->data[0] = (uint8_t)frame_checksum_first(f);
    g_20004151 = g_20004151 - 1;
    can_tx(f);
}

/* Stock 0xFD34. Sets the request. long_ != 0 sends it in 10 frames, else in 1. */
void req_678_set(uint32_t code, uint32_t long_)
{
    g_20004150 = (uint8_t)code;
    g_20004151 = long_ ? 10 : 1;
}

/* Stock 0xFD44. Clears 0x20004150..0x20004152. */
void req_678_clear(void)
{
    tmos_memset(&g_20004150, 0, 3);
}
