/* 0x679 frame edit with settings from BLE (stock 0xC3A8..0xC4AA).
   g_679_cfg (7 bytes at 0x20004100):
     byte 1 bit 1     edit on
     byte 1 bits 7:2  nonzero: copy bytes 2..4 to data[1..3]
     byte 5 bits 4:0  level L: 1..10 adds L*10 to data[4] bits 6:0,
                      11..20 sets (L-10)*10, 21..31 sets 0 (max 100)
     byte 5 bits 6:5  value for data[5] bits 1:0 */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xC3A8. Edits a 0x679 frame and sends it when its dlc is not 0. */
void can_679_edit(m3_frame_t *f)
{
    const uint8_t *c = g_679_cfg;
    uint32_t copy;
    uint32_t level;

    if ((c[1] & 2) == 0)
        return;
    copy = (c[1] >> 2) != 0;
    if (copy) {
        f->data[1] = c[2];
        f->data[2] = c[3];
        f->data[3] = c[4];
    }
    level = c[5] & 0x1F;
    if (level != 0) {
        uint32_t v;
        if (level <= 10)
            v = ((f->data[4] & 0x7F) + level * 10) & 0xFF;
        else if (level <= 20)
            v = ((level - 10) * 10) & 0xFF;
        else
            v = 0;
        if (v > 100)
            v = 100;
        f->data[4] = (v & 0x7F) | (f->data[4] & 0x80);
    } else if (!copy) {
        return;
    }
    if (f->dlc == 0)
        return;
    f->data[5] = (f->data[5] & 0xFC) | ((c[5] >> 5) & 3);
    can_tx(f);
}

/* Stock 0xC476. Takes 7 new setting bytes and applies them to the cached
   0x679 frame (cache slot 18). */
void can_679_set(void *cfg)
{
    tmos_memcpy(g_679_cfg, cfg, 7);
    can_679_edit(&g_2000c2ac);
}

/* Stock 0xC498. */
void can_679_clear(void)
{
    tmos_memset(g_679_cfg, 0, 7);
}
