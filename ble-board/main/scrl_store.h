#pragma once
/*
 * scrl_store.h — persistent values in NVS, namespace "scrl".
 *
 * Settings use the same keys and types as the T-2CAN prefs.cpp:
 *   scrAck (u8), scrMode (u8), scrPat (u8), scrDly (u16), scrGap (u16), scrInt (u16)
 * Added for this board:
 *   opMode (u8), pwrOnCnt (u32), rstLog (4 bytes, newest first), passkey (u32)
 * NimBLE keeps the bonds in its own namespace.
 */

#include <stdbool.h>
#include <stdint.h>

#include "scrl_settings.h"

#define SCRL_RESET_LOG_LEN 4

struct ScrlBootInfo {
    uint8_t reset_reason;                   // esp_reset_reason_t of this boot
    uint8_t reset_log[SCRL_RESET_LOG_LEN];  // this boot first, then the 3 boots before it
    uint32_t power_on_count;                // boots with reset reason "power-on"
    uint32_t passkey;                       // 6-digit BLE passkey, 0 to 999999
};

/** Open NVS. Erases it when its layout is unusable. Call once, first. */
void scrl_store_init(void);

/** Load the settings, clamped. Missing keys give the defaults (off, Listen-Only). */
ScrlSettings scrl_store_load_settings(void);

/** Save the settings. Writes only the keys whose value changed. */
void scrl_store_save_settings(const ScrlSettings &settings);

/** Record this boot (reset log, power-on count) and load or create the passkey. */
ScrlBootInfo scrl_store_boot(void);

/** Erase the settings, the counters and the passkey. The next boot makes a new passkey. */
void scrl_store_factory_reset(void);
