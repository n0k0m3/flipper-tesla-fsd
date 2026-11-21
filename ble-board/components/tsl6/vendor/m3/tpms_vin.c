/* Tire pressures (0x25A, 0x31F) and the VIN (0x405). Stock 0xCDC6..0xCF46. */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xCDC6. 0x25A: tire values in data[0..3]. Stores them and pushes
   the gauge on a change. */
void can_25a_tpms(m3_frame_t *f)
{
    uint8_t a = f->data[0];
    uint8_t b = f->data[1];
    uint8_t c = f->data[2];
    uint8_t d = f->data[3];

    if (g_tpms[0] == a && g_tpms[1] == b && g_tpms[2] == c && g_tpms[3] == d)
        RETURN_A0(f);
    g_tpms[0] = a;
    g_tpms[1] = b;
    g_tpms[2] = c;
    g_tpms[3] = d;
    CALL_A0(gauge_push, f);
}

/* Stock 0xCE0C. 0x31F (TPMS_sensorData): tire pressures in data[0], [2],
   [4], [6]. Same as 0x25A otherwise. */
void can_31f_tpms(m3_frame_t *f)
{
    uint8_t a = f->data[0];
    uint8_t b = f->data[2];
    uint8_t c = f->data[4];
    uint8_t d = f->data[6];

    if (g_tpms[0] == a && g_tpms[1] == b && g_tpms[2] == c && g_tpms[3] == d)
        RETURN_A0(f);
    g_tpms[0] = a;
    g_tpms[1] = b;
    g_tpms[2] = c;
    g_tpms[3] = d;
    CALL_A0(gauge_push, f);
}

/* Stock 0xCE52. */
uint8_t *tpms_get(void)
{
    return g_tpms;
}

/* Stock 0xCE58. */
void tpms_clear(void)
{
    tmos_memset(g_tpms, 0, 4);
}

/* Stock 0xCE6A. Returns the VIN block: done flag, then 17 characters. */
uint8_t *vin_get(void)
{
    return &g_vin_done;
}

/* Stock 0xCE74. 0x405 (VIN, multiplexed by data[0]): mux 0x10 gives
   characters 0..2 (data[5..7]), 0x11 gives 3..9 (data[1..7]), 0x12 gives
   10..16. The three frames must come in order; then the VIN is done and
   stays fixed. */
void can_405_vin(m3_frame_t *f)
{
    uint32_t mux;

    if (g_vin_done)
        return;
    mux = f->data[0];
    if (g_vin_step == 0) {
        if (mux != 0x10) {
            g_vin_step = 0;
            return;
        }
        g_vin_step = 1;
        tmos_memcpy(&g_vin[0], &f->data[5], 3);
        return;
    }
    if (g_vin_step == 1) {
        if (mux == 0x11) {
            g_vin_step = 2;
            tmos_memcpy(&g_vin[3], &f->data[1], 7);
            return;
        }
    } else if (g_vin_step == 2 && mux == 0x12) {
        g_vin_step = 3;
        tmos_memcpy(&g_vin[10], &f->data[1], 7);
        g_vin_done = 1;
        return;
    }
    g_vin_step = 0;
}

/* Stock 0xCF30. Clears the VIN block (19 bytes). */
void vin_clear(void)
{
    tmos_memset(&g_vin_done, 0, 19);
}
