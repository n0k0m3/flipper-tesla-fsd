/*
 * tsl6_engine.c — start-up, frame input and settings of the TSL6 engine.
 * The start-up follows the stock app_init: data, tables_load, app timers,
 * the CAN filters of platform 0 (can_init), the link table, features_init,
 * tick_init, light_init. Not ported: LED, buzzer, the RF packets of the light
 * link (rf_send is busy: the zone records stay queued; the whole-car records
 * go to CAN 0x679). The CAN side is in tsl6_can.c, the BLE frame link in
 * tsl6_ble.c, the central role in tsl6_central.c.
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
void sport_mode_restore(void);
void features_off_later(void);
void links_init(void);
void can_filter_platform(uint32_t platform);
void tsl6_ble_reset(void);
void tsl6_can_reset(void);
void tsl6_central_reset(void);
void central_init(void);
uint32_t light_init(void);

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
    tsl6_ble_reset();
    tsl6_can_reset();
    tsl6_central_reset();
    tables_load();
    app_timer_init();
    if (hal->central)
        central_init();  /* stock ble_init: the central role of the BLE remotes */
    can_filter_platform(0);  /* stock can_init */
    links_init();
    features_init();
    tick_init();  /* the TSL6 tick stays off (patched tick_start): the board has its own Scroll Ack */
    light_init(); /* the light records: CAN 0x679 (whole car); the RF zone link has no radio here */
}

extern uint8_t port_reinit_wanted;  /* tsl6_stubs.c: the stock code asked for a chip reset */
int tsl6_ble_links(uint16_t out[2]);

uint32_t tsl6_poll(void)
{
    uint32_t next = port_tmos_run();
    if (port_reinit_wanted) {
        port_reinit_wanted = 0;
        /* The phone links stay: connect them to the new engine again. */
        uint16_t links[2];
        int n = tsl6_ble_links(links);
        tsl6_init(port_hal);
        for (int i = 0; i < n; i++)
            tsl6_ble_connect(links[i]);
        /* The reset can follow a factory reset by the car keys (cfg_reset does
           not report it): the board takes the settings again. */
        if (port_hal && port_hal->settings_changed)
            port_hal->settings_changed();
        next = port_tmos_run();
    }
    return next;
}


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
