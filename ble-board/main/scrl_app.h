#pragma once
/*
 * scrl_app.h — functions of app_main.cpp that the BLE and OTA code call.
 */

#include <stdint.h>

#include "scrl_settings.h"
#include "scrl_status.h"
#include "scrl_store.h"

/** The complete status: frame logic, CAN controller, boot record, BLE flags. */
void scrl_app_collect_status(ScrlStatus *out);

/** Apply clamped settings to the frame logic and save them. See scrl_proc_set_settings(). */
void scrl_app_apply_settings(const ScrlSettings &settings, bool release_bus_off_lock);

/** Reset the Scroll Ack counters and the frame counters. */
void scrl_app_reset_counters(void);

/** Restart the board after delay_ms. The housekeeping task does it. */
void scrl_app_request_reboot(uint32_t delay_ms);

/** Erase the settings, the counters and the bonds, then restart. The passkey stays. */
void scrl_app_factory_reset(void);

const ScrlBootInfo *scrl_app_boot_info(void);
