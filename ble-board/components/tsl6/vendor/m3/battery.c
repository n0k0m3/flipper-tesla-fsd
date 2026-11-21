/* Battery heating request (0x082), charge port (0x25D, 0x333), BMS values
   (0x332, 0x312) and range/SOC (0x33A). Stock 0xCF46..0xD216. */
#include "tsl6.h"
#include "m3.h"

/* Stock 0x19108: data[0..2] of 0x082 with the battery heating request set.
   Only 3 bytes are used. The other 13 bytes copy the stock bytes after it
   (padding and the start of a jump table), because the test compares
   constant pointers by 16 bytes of content. */
static const uint8_t k_082_heat[16] = {
    0xbf, 0x50, 0xbe, 0x00, 0xe2, 0x47, 0xff, 0xff,
    0x26, 0x48, 0xff, 0xff, 0x18, 0x48, 0xff, 0xff,
};

/* Stock 0xCF46. 0x082 (UI_tripPlanning) handler and edit.
   data[0] bit 2 = UI_requestActiveBatteryHeating. A change is stored and
   pushed in the status. While the module requests heating and the car does
   not, data[0..2] get the request and the frame is sent. */
void can_082_heat(m3_frame_t *f)
{
    uint32_t bit = (f->data[0] >> 2) & 1;

    if (g_heat_car != bit) {
        g_heat_car = (uint8_t)bit;
        status_push(0);
    }
    if (g_heat_req == 0 || bit != 0)
        return;
    tmos_memcpy(&f->data[0], (void *)k_082_heat, 3);
    can_tx(f);
}

/* Stock 0xCFAC. Sets the battery heating request, beeps, pushes the status.
   Note: the beep test reads BF78 and BF79 as one halfword, so the car's
   heating bit (BF79) also counts. */
void heat_request_set(uint8_t on)
{
    g_heat_req = on;
    buzzer_beep_onoff(g_heat_word != 0);
    status_push(0);
}

/* Stock 0xCFD2. Nonzero when the module or the car requests heating
   (halfword BF78). */
uint32_t heat_request_get(void)
{
    return g_heat_word != 0;
}

/* Stock 0xCFE0. Heating status: 2 = pack heating (0x312), 1 = requested,
   0 = off. */
uint32_t heat_status_get(void)
{
    if (g_pack_heating)
        return 2;
    return g_heat_word != 0;
}

/* Stock 0xCFFA. 0x25D handler: data[0] bit 6 (charge port state). A change
   gives event 0x66 (set) or 0x67 (clear), cancels the heating request and
   pushes the status. */
void can_25d_cp(m3_frame_t *f)
{
    uint32_t bit = (f->data[0] >> 6) & 1;

    if (g_cp_state == bit)
        return;
    g_cp_state = (uint8_t)bit;
    event_fire((bit == 0) + 0x66);
    if (g_heat_word != 0)
        heat_request_set(0);
    status_push(0);
}

/* Stock 0xD040. 0x333 (UI_chargeRequest) edit: puts the charge port command
   in data[0] bits 1:0 (1 open, 2 close; close also sets data[3] bit 7) and
   sends the frame two times. */
void can_333_cp_edit(m3_frame_t *f)
{
    uint32_t cmd = g_cp_cmd;

    if (cmd == 0)
        return;
    f->data[0] = (f->data[0] & 0xFC) | (cmd & 3);
    if (cmd == 2)
        f->data[3] |= 0x80;
    g_cp_cmd = 0;
    can_tx(f);
    can_tx(f);
}

/* Stock 0xD08E. Charge port request: open != 0 opens, 0 closes. Beeps. */
void cp_request(uint32_t open)
{
    buzzer_beep_onoff(open);
    g_cp_cmd = (open == 0) + 1;
}

/* Stock 0xD0B0. */
uint32_t cp_state_get(void)
{
    return g_cp_state != 0;
}

/* Stock 0xD0BE. 0x332 (BMS brick min/max), mux 1 (data[0] bits 1:0):
   average of the two 12-bit brick voltages (bits 2..13 and 16..27). */
void can_332_bms(m3_frame_t *f)
{
    uint32_t v;

    if ((f->data[0] & 3) != 1)
        return;
    v = (((f->data[3] & 0xF) << 8 | f->data[2])
         + ((f->data[1] & 0x3F) << 6 | (f->data[0] >> 2))) >> 1;
    if (g_brick_v == v)
        return;
    g_brick_v = (uint16_t)v;
    gauge_push();
}

/* Stock 0xD10A. */
uint16_t brick_v_get(void)
{
    return g_brick_v;
}

/* Stock 0xD114. 0x312 (BMS_thermalStatus): pack temperature = average of
   packTMin (bits 44..52) and packTMax (bits 53..61). Heating flag =
   packTMin < inletActiveHeatTarget (bits 35..43). */
void can_312_pack_temp(m3_frame_t *f)
{
    uint32_t tmin = (f->data[6] & 0x1F) << 4 | (f->data[5] >> 4);
    uint32_t heat = (f->data[5] & 0xF) << 5 | (f->data[4] >> 3);
    uint32_t tmax = (f->data[7] & 0x3F) << 3 | (f->data[6] >> 5);
    uint32_t flag = tmin < heat;
    uint32_t avg = (tmax + tmin) >> 1;

    if (g_pack_heating == flag && g_pack_t == avg)
        return;
    g_pack_heating = (uint8_t)flag;
    g_pack_t = (uint16_t)avg;
    gauge_push();
}

/* Stock 0xD17E. */
uint16_t pack_temp_get(void)
{
    return g_pack_t;
}

/* Stock 0xD188. 0x33A (UI_rangeSOC): range = bits 0..9; SOC = data[6]
   bits 6:0, or bits 20..26 when data[5] = 0xFF. */
void can_33a_range(m3_frame_t *f)
{
    uint32_t soc;
    uint32_t range;

    if (f->data[5] == 0xFF)
        soc = (f->data[3] & 7) << 4 | (f->data[2] >> 4);
    else
        soc = f->data[6] & 0x7F;
    range = (f->data[1] & 3) << 8 | f->data[0];
    if (g_soc == soc && g_range == range)
        return;
    g_soc = (uint8_t)soc;
    g_range = (uint16_t)range;
    gauge_push();
}

/* Stock 0xD1EC. */
uint8_t soc_get(void)
{
    return g_soc;
}

/* Stock 0xD1F6. */
uint16_t range_get(void)
{
    return g_range;
}

/* Stock 0xD200. Clears 0x2000BF78..0x2000BF83. */
void battery_clear(void)
{
    tmos_memset(&g_heat_req, 0, 12);
}
