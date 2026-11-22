/*
 * tsl6_can.c — the CAN side of the TSL6 (stock m6/can_rx.c, m6/can_filter.c)
 * on the board: the hardware filters as a software list, the receive path and
 * the send path.
 *
 * The TSL6 loads 14 filter banks of 4 standard IDs (56 IDs) for the detected
 * car platform. The car code only sees those frames. The board receives every
 * frame, so this file passes on only the frames that the TSL6 filters pass.
 */
#include <string.h>

#include "tsl6_port.h"
#include "tsl6_engine.h"
#include "../gen/ram_regions.h"

#define FILTER_IDS 56

extern uint8_t g_cfg_block[0x70];
extern uint16_t g_can_ids_p4[3], g_can_ids_p5[3];

void platform_stop(void);
uint32_t platform_dispatch(uint8_t *f);
uint32_t bus_alive(uint32_t a);
uint8_t ble_can_sniff(const tsl6_frame_t *f);

static uint16_t s_ids[FILTER_IDS];
static uint8_t s_all;   /* bank 0 in mask mode with mask 0: every ID passes */
static uint32_t s_tx;

/* Filter IDs of platform 0 (car detection) and platform 1 (Tesla Model 3/Y). */
static const uint16_t ids_platform0[8] = {
    0x0001, 0x0334, 0x03e9, 0x0370, 0x021c, 0x0189, 0x03a1, 0x0498,
};
static const uint16_t ids_platform1[56] = {
    0x02e1, 0x0129, 0x0229, 0x0257, 0x0273, 0x0333, 0x03c2, 0x03e2,
    0x03f5, 0x02f3, 0x0082, 0x03b3, 0x025d, 0x0118, 0x03c3, 0x0249,
    0x03e9, 0x039d, 0x0102, 0x0103, 0x033a, 0x03b6, 0x03e3, 0x03df,
    0x025a, 0x02b6, 0x0334, 0x03d8, 0x0266, 0x02e5, 0x031f, 0x03fe,
    0x0293, 0x04e2, 0x04e3, 0x020c, 0x0243, 0x0321, 0x0332, 0x03fd,
    0x0312, 0x0339, 0x0678, 0x04f3, 0x01f9, 0x0405, 0x0238, 0x0132,
    0x0292, 0x0352, 0x03d2, 0x0401, 0x02b4, 0x0679, 0x07ff, 0x0370,
};

/* Stock 0x125F8: up to 56 IDs (len in bytes, at most 0x70). Unused slots hold
   ID 0. A new list replaces bank 0 too, so "accept all" ends. */
void can_filter_init(void *ids, uint32_t len)
{
    uint32_t n = len <= 0x70 ? (len & 0xFF) : 0x70;
    memset(s_ids, 0, sizeof(s_ids));
    memcpy(s_ids, ids, n);
    s_all = 0;
}

void can_filter_clear(void)
{
    memset(s_ids, 0, sizeof(s_ids));
    s_all = 0;
}

void can_filter_accept_all(uint32_t skip)
{
    platform_stop();
    if (skip == 0)
        s_all = 1;
}

void can_filter_close(uint32_t skip)
{
    platform_stop();
    if (skip == 0)
        can_filter_clear();
}

void can_filter_set(uint32_t skip, void *ids, uint32_t len)
{
    if (skip == 0)
        can_filter_init(ids, len);
}

/* Stock 0x12780: the IDs of a platform. Other values leave every slot at 0. */
void can_filter_platform(uint32_t platform)
{
    uint16_t ids[60];
    uint32_t len;

    platform_stop();
    can_filter_clear();
    switch (platform) {
    case 0:
        memcpy(ids, ids_platform0, 16);
        len = 16;
        break;
    case 1:
        memcpy(ids, ids_platform1, 112);
        len = 112;
        break;
    case 2:
    case 3:
        ids[0] = 0x0a9;
        ids[1] = 0x21c;
        len = 4;
        break;
    case 4:
        memcpy(ids, g_can_ids_p4, 6);
        len = 6;
        break;
    case 5:
        memcpy(ids, g_can_ids_p5, 6);
        len = 6;
        break;
    case 6:
        ids[0] = 0x0a9;
        ids[1] = 0x498;
        len = 4;
        break;
    default:
        return;
    }
    can_filter_init(ids, len);
}

int tsl6_can_passes(uint16_t id)
{
    if (s_all)
        return 1;
    for (int i = 0; i < FILTER_IDS; i++)
        if (s_ids[i] == id)
            return 1;
    return 0;
}

/* Stock can_rx_poll for one frame: the CAN sniff (C0 tool) takes the frame
   first; ID 0x001 is the hidden test frame (buzzer, not on the board); the
   other frames go to the car features when the master switch is on. */
void tsl6_rx(uint32_t id, int ext, uint8_t dlc, const uint8_t *data)
{
    tsl6_frame_t f;

    if (ext || id > 0x7FF || dlc > 8 || !tsl6_can_passes((uint16_t)id))
        return;
    memset(&f, 0, sizeof(f));
    f.id = (uint16_t)id;
    f.dlc = dlc;
    memcpy(f.data, data, dlc);
    if (ble_can_sniff(&f))
        return;
    if (f.id == 1)
        return;
    if ((g_cfg_block[4] & 8) == 0)
        platform_dispatch((uint8_t *)&f);
}

/* Stock 0x126DC: sends a frame when bus 0 is alive and the frame flag is 0.
   Every module declares can_tx with its own types, all with the 12-byte frame
   first. 0x370 is never sent: the EPAS torque echo faulted the car. */
uint32_t can_tx(void *frame)
{
    const tsl6_frame_t *f = (const tsl6_frame_t *)frame;
    if (!bus_alive(0) || f->flag != 0 || f->dlc > 8 || f->id == 0x370)
        return 0;
    if (!port_hal || !port_hal->send)
        return 0;
    s_tx++;
    return (uint32_t)port_hal->send(f);
}

uint32_t tsl6_tx_count(void) { return s_tx; }
void tsl6_can_reset(void) { s_tx = 0; can_filter_clear(); }
