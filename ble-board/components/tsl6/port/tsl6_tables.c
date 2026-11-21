/*
 * tsl6_tables.c — the settings blocks of the TSL6 (stock m6/tables.c), with
 * the flash pages replaced by the store of the HAL (NVS on the board).
 *
 *   block       size  page (stock flash address, the store key)
 *   config      0x70  0x08077A00  A3 bytes 24-39 at 0-15, seat memories at 16-95, A3 bytes 59-74 at 96-111
 *   AB table    256   0x08077900  shortcuts (trigger, action)
 *   BA table    256   0x08077800  BLE buttons
 *   RGB zone    256   0x08077700  ambient light, tab 0
 *   RGB car     256   0x08077600  ambient light, tab 1
 *   remotes     0x85  0x08077500  F0 remotes
 *
 * A table is valid when its last byte is 0x5A. The config is valid when its
 * 4 password bytes (12-15) are printable. The board has no TSL6 password (the
 * phone pairs over BLE); the bytes keep the factory "1234" only for that check.
 */
#include <string.h>

#include "tsl6_port.h"
#include "tsl6_engine.h"

#define TABLE_OK 0x5A
#define REMOTE_TABLE 0x85
#define PAGE_CFG 0x08077A00u
#define PAGE_AB 0x08077900u
#define PAGE_BA 0x08077800u
#define PAGE_RGB_ZONE 0x08077700u
#define PAGE_RGB_CAR 0x08077600u
#define PAGE_REMOTES 0x08077500u

extern uint8_t g_cfg_block[0x70];
extern uint8_t g_ba_table[256];
extern uint8_t g_remote_table[REMOTE_TABLE];
extern uint32_t g_ab_table[64];
extern uint32_t g_rgb_zone[64];
extern uint32_t g_rgb_car[64];

static void save(uint32_t page, const void *src, uint32_t len)
{
    if (port_hal && port_hal->store_write)
        port_hal->store_write(page, src, len);
}

static int load(uint32_t page, void *dst, uint32_t len)
{
    return port_hal && port_hal->store_read && port_hal->store_read(page, dst, len);
}

void ab_table_save(void) { save(PAGE_AB, g_ab_table, 256); }
void ba_table_save(void) { save(PAGE_BA, g_ba_table, 256); }
void rgb_zone_save(void) { save(PAGE_RGB_ZONE, g_rgb_zone, 256); }
void rgb_car_save(void) { save(PAGE_RGB_CAR, g_rgb_car, 256); }
void remote_table_save(void) { save(PAGE_REMOTES, g_remote_table, REMOTE_TABLE); }

/* A TSL6 button or key changed a setting: save it and tell the board. */
void cfg_save(void)
{
    save(PAGE_CFG, g_cfg_block, 0x70);
    if (port_hal && port_hal->settings_changed)
        port_hal->settings_changed();
}

/* Factory shortcut pairs (trigger -> action): 02->14, 43->0B, 44->0C, 45->0D,
   0A->2C, 0C->2B, 0E->46, 10->05, 11->07, 1E->50, 1F->5F, 20->1E, 21->2A. */
void ab_table_reset(void)
{
    memset(g_ab_table, 0, 256);
    ((uint8_t *)g_ab_table)[255] = TABLE_OK;
    g_ab_table[0] = 0x0b431402;
    g_ab_table[1] = 0x0d450c44;
    g_ab_table[2] = 0x2b0c2c0a;
    g_ab_table[3] = 0x0510460e;
    g_ab_table[4] = 0x501e0711;
    g_ab_table[5] = 0x1e205f1f;
    ((uint16_t *)g_ab_table)[12] = 0x2a21;
    ab_table_save();
}

void ba_table_reset(void)
{
    memset(g_ba_table, 0, 256);
    g_ba_table[255] = TABLE_OK;
    ba_table_save();
}

void rgb_zone_reset(void)
{
    uint32_t *t = g_rgb_zone;

    memset(t, 0, 256);
    ((uint8_t *)t)[255] = TABLE_OK;
    t[0] = 0xff000000;
    t[1] = 0x99800300;
    t[2] = 0xff04;
    t[3] = 0x049aa023;
    t[4] = 0x030000ff;
    t[5] = 0xff089b00;
    t[6] = 0xa0230000;
    t[7] = 0x00ff089c;
    t[8] = 0x9f000300;
    t[9] = 0xff00ff0c;
    ((uint8_t *)t)[48] = 0;
    t[10] = 0x0c9e9803;
    t[11] = 0x03ff00ff;
    rgb_zone_save();
}

void rgb_car_reset(void)
{
    uint32_t *t = g_rgb_car;

    memset(t, 0, 256);
    ((uint8_t *)t)[255] = TABLE_OK;
    t[0] = 0x00ff0800;
    ((uint16_t *)t)[2] = 0x60ff;
    ((uint8_t *)t)[6] = 20;
    rgb_car_save();
}

void remote_table_reset(void)
{
    memset(g_remote_table, 0, REMOTE_TABLE);
    g_remote_table[REMOTE_TABLE - 1] = TABLE_OK;
    remote_table_save();
}

/* Factory config: bytes 0-11 = 00 00 FF 01 01 00 00 00 01 01 00 00 (every car
   feature off), password "1234", the rest 0. */
void cfg_reset(void)
{
    static const uint8_t factory[16] = {0x00, 0x00, 0xFF, 0x01, 0x01, 0x00, 0x00, 0x00,
                                        0x01, 0x01, 0x00, 0x00, '1', '2', '3', '4'};
    memset(g_cfg_block, 0, 0x70);
    memcpy(g_cfg_block, factory, sizeof(factory));
    save(PAGE_CFG, g_cfg_block, 0x70);
}

void tables_load(void)
{
    int ok = load(PAGE_CFG, g_cfg_block, 0x70);
    for (int i = 12; ok && i < 16; i++)
        if ((uint8_t)(g_cfg_block[i] - 0x20) > 0x5e)
            ok = 0;
    if (!ok)
        cfg_reset();
    if (!load(PAGE_AB, g_ab_table, 256) || ((uint8_t *)g_ab_table)[255] != TABLE_OK)
        ab_table_reset();
    if (!load(PAGE_BA, g_ba_table, 256) || g_ba_table[255] != TABLE_OK)
        ba_table_reset();
    if (!load(PAGE_RGB_ZONE, g_rgb_zone, 256) || ((uint8_t *)g_rgb_zone)[255] != TABLE_OK)
        rgb_zone_reset();
    if (!load(PAGE_RGB_CAR, g_rgb_car, 256) || ((uint8_t *)g_rgb_car)[255] != TABLE_OK)
        rgb_car_reset();
    if (!load(PAGE_REMOTES, g_remote_table, REMOTE_TABLE) || g_remote_table[REMOTE_TABLE - 1] != TABLE_OK)
        remote_table_reset();
}

void factory_reset(void)
{
    cfg_reset();
    ab_table_reset();
    ba_table_reset();
    rgb_zone_reset();
    rgb_car_reset();
    remote_table_reset();
}
