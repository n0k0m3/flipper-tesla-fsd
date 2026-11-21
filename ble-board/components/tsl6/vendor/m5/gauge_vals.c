/* Tesla (platform 1) frames that feed the B0 gauge stream
   (stock 0xFD56..0xFE80). Each handler stores a value and calls gauge_push
   when the value changes. The values live in a 12-byte block at 0x2000BFC4. */
#include "tsl6.h"
#include "m5.h"

extern uint32_t g_2000bfc4;      /* u32: odometer from 0x3B6, raw / 100 */
extern int16_t g_altitude;       /* 0x2000BFC8, s16: 14-bit value from 0x3D8 */
extern int16_t g_2000bfca[2];    /* s16[2]: 11-bit power, [0] 0x2E5, [1] 0x266 */
extern uint8_t g_2000bfce;       /* u8: 0x3E3 data[1] bit 0 */
extern uint8_t g_20004188;       /* u8: state from 0x7FF mux 3 */

/* Stock 0xFD56. 0x3B6: data[0..3] is a 32-bit little-endian value.
   0xFFFFFFFF (no data) is ignored. */
void can_3b6_odometer(m5_frame_t *f)
{
    uint32_t v = f->data[1] * 0x100u + f->data[2] * 0x10000u + f->data[0] +
                 f->data[3] * 0x1000000u;
    if (v == 0xFFFFFFFFu)
        return;
    v /= 100;
    if (g_2000bfc4 == v)
        return;
    g_2000bfc4 = v;
    gauge_push();
}

/* Stock 0xFD9C. */
uint32_t odometer_get(void)
{
    return g_2000bfc4;
}

/* Stock 0xFDA8. 0x3D8: signed 14-bit value data[0] | (data[1] & 0x3F) << 8. */
void can_3d8_altitude(m5_frame_t *f)
{
    int32_t v = (int32_t)((((f->data[1] & 0x3F) << 8) | f->data[0]) << 18) >> 18;
    if (g_altitude == v)
        return;
    g_altitude = (int16_t)v;
    gauge_push();
}

/* Stock 0xFDDA. */
int32_t altitude_get(void)
{
    return g_altitude;
}

/* Stock 0xFDE8. 0x3E3: data[1] bit 0. */
void can_3e3_flag(m5_frame_t *f)
{
    uint8_t v = f->data[1] & 1;
    if (g_2000bfce == v)
        return;
    g_2000bfce = v;
    gauge_push();
}

/* Stock 0xFE08. */
uint32_t flag_3e3_get(void)
{
    return g_2000bfce;
}

/* Stock 0xFE12. 0x2E5 (idx 0) and 0x266 (idx 1): signed 11-bit value
   data[0] | (data[1] & 7) << 8. The index is not checked. */
void can_power(m5_frame_t *f, uint32_t idx)
{
    int32_t v = (int32_t)((((f->data[1] & 7) << 8) | f->data[0]) << 21) >> 21;
    if (g_2000bfca[idx] == v)
        return;
    g_2000bfca[idx] = (int16_t)v;
    gauge_push();
}

/* Stock 0xFE42. Returns 0 while 0x20004188 is 7 or 8. */
int32_t power_get(uint32_t idx)
{
    if ((uint8_t)(g_20004188 - 7) <= 1)
        return 0;
    return g_2000bfca[idx];
}

/* Stock 0xFE6A. */
void gauge_vals_clear(void)
{
    tmos_memset(&g_2000bfc4, 0, 12);
}
