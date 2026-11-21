/*
 * tsl6_engine.c — start-up, frame input and settings of the TSL6 engine.
 * The start-up follows the stock app_init: data, tables_load, app timers,
 * features_init, tick_init. Not ported: BLE, LED, buzzer, CAN driver, links,
 * the RF light link (light_init).
 */
#include <string.h>

#include "tsl6_engine.h"
#include "../gen/ram_regions.h"

extern uint8_t g_cfg_block[0x70];
extern uint8_t g_ba_table[256], g_remote_table[0x85];
extern uint32_t g_ab_table[64], g_rgb_zone[64], g_rgb_car[64];

void tables_load(void);
void factory_reset(void);
void app_timer_init(void);
void features_init(void);
uint32_t tick_init(void);
uint32_t platform_dispatch(uint8_t *f);
void sport_mode_restore(void);
void features_off_later(void);

static uint32_t s_tx;

/* The stock .data (0x20004000..0x200040A8) at its offsets in region 0. */
static void data_init(void)
{
    uint8_t *d = tsl6_r0;
    uint32_t p;

    memset(d, 0, 0xA8);
    d[0x72] = 1;                                              /* flag_257 */
    p = (uint32_t)(uintptr_t)g_cfg_block;    memcpy(d + 0x74, &p, 4);
    p = (uint32_t)(uintptr_t)g_ba_table;     memcpy(d + 0x78, &p, 4);
    p = (uint32_t)(uintptr_t)g_remote_table; memcpy(d + 0x7C, &p, 4);
    p = (uint32_t)(uintptr_t)g_ab_table;     memcpy(d + 0x80, &p, 4);
    p = (uint32_t)(uintptr_t)g_rgb_zone;     memcpy(d + 0x84, &p, 4);
    p = (uint32_t)(uintptr_t)g_rgb_car;      memcpy(d + 0x88, &p, 4);
    d[0x8C] = 0xFF;                                           /* rfq_task: none */
    static const uint16_t p4[3] = {0x0A9, 0x189, 0x68C}, p5[3] = {0x3EA, 0x37A, 0x3A1};
    memcpy(d + 0x98, p4, 6);
    memcpy(d + 0xA0, p5, 6);
}

void tsl6_init(const tsl6_hal_t *hal)
{
    _Static_assert(sizeof(void *) == 4, "the TSL6 globals hold 32-bit pointers");
    port_hal = hal;
    port_tmos_reset();
    memset(tsl6_r0, 0, sizeof(tsl6_r0));
    memset(tsl6_r1, 0, sizeof(tsl6_r1));
    memset(tsl6_r2, 0, sizeof(tsl6_r2));
    data_init();
    tables_load();
    app_timer_init();
    features_init();
    tick_init();  /* stays off: uid_crc_ok() is 0 in the port (the board has its own Scroll Ack) */
    s_tx = 0;
}

void tsl6_rx(uint32_t id, int ext, uint8_t dlc, const uint8_t *data)
{
    tsl6_frame_t f;

    if (ext || id > 0x7FF || dlc > 8)
        return;
    memset(&f, 0, sizeof(f));
    f.id = (uint16_t)id;
    f.dlc = dlc;
    memcpy(f.data, data, dlc);
    if ((g_cfg_block[4] & 8) == 0)  /* master switch on */
        platform_dispatch((uint8_t *)&f);
}

extern uint8_t port_reinit_wanted;  /* tsl6_stubs.c: the stock code asked for a chip reset */

uint32_t tsl6_poll(void)
{
    uint32_t next = port_tmos_run();
    if (port_reinit_wanted) {
        port_reinit_wanted = 0;
        tsl6_init(port_hal);
        /* The reset can follow a factory reset by the car keys (cfg_reset does
           not report it): the board takes the settings again. */
        if (port_hal && port_hal->settings_changed)
            port_hal->settings_changed();
        next = port_tmos_run();
    }
    return next;
}

/* The stock code sends through can_tx; every module declares it with its own
   types, all with the 12-byte frame as the first argument. */
uint32_t can_tx(void *frame)
{
    const tsl6_frame_t *f = (const tsl6_frame_t *)frame;
    if (!port_hal || !port_hal->send || f->id == 0x370)  /* 0x370: never (the torque echo faulted the car) */
        return 0;
    s_tx++;
    return (uint32_t)port_hal->send(f);
}

uint32_t tsl6_tx_count(void) { return s_tx; }

void tsl6_cfg_get(uint8_t out[TSL6_CFG_LEN])
{
    memcpy(out, g_cfg_block, 12);
    memcpy(out + 12, g_cfg_block + 96, 16);
}

void tsl6_cfg_set(const uint8_t in[TSL6_CFG_LEN])
{
    if ((g_cfg_block[0] ^ in[0]) & 7)
        sport_mode_restore();
    memcpy(g_cfg_block, in, 12);
    memcpy(g_cfg_block + 96, in + 12, 16);
    port_hal->store_write(0x08077A00u, g_cfg_block, 0x70);
    if (g_cfg_block[4] & 8)
        features_off_later();
}

void tsl6_factory_reset(void) { factory_reset(); }
