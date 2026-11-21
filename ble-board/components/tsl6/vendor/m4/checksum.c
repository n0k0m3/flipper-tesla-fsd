/* Tesla frame checksums (stock 0xE9EA, 0xEA1C).
   The sum is the two id bytes plus the data bytes, modulo 256. */
#include "tsl6.h"
#include "m4.h"

/* Stock 0xE9EA. Checksum for a frame that carries it in data[0]:
   id bytes + data[1..dlc-1]. */
uint32_t frame_checksum_first(m4_frame_t *f)
{
    const uint8_t *p = (const uint8_t *)f;
    uint32_t s = ((f->id >> 8) + f->id) & 0xff;

    for (uint32_t i = 1; (uint8_t)i < f->dlc; i++)
        s = (s + p[3 + i]) & 0xff;
    return s;
}

/* Stock 0xEA1C. Checksum for a frame that carries it in the last byte:
   id bytes + data[0..dlc-2]. */
uint32_t frame_checksum_last(m4_frame_t *f)
{
    const uint8_t *p = (const uint8_t *)f;
    uint32_t s = ((f->id >> 8) + f->id) & 0xff;

    for (int i = 0; i < (int)f->dlc - 1; i = (uint8_t)(i + 1))
        s = (s + p[3 + i]) & 0xff;
    return s;
}
