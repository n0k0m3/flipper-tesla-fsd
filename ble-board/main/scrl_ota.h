#pragma once
/*
 * scrl_ota.h — firmware update over BLE, with bootloader rollback.
 *
 * FLOW
 *   1. Phone writes BEGIN to OTA control: image size and SHA-256.
 *   2. Phone writes the image to OTA data, in chunks of MTU - 3 bytes, without
 *      response (or with response: iOS, Info "otaw":1). It never sends more
 *      than SCRL_OTA_WINDOW bytes past the last CREDIT offset.
 *   3. Phone writes END. The board checks the size, the SHA-256 and the image
 *      (magic, chip ID, checksum, board ID in scrl_board_desc.h), selects the
 *      new slot, and restarts.
 *   4. The new image boots as "pending". It stays in Listen-Only.
 *   5. An authenticated phone reconnects and writes the confirm command. Only
 *      then the image is valid. With no confirm in SCRL_OTA_CONFIRM_MS the board
 *      restarts and the bootloader goes back to the old image.
 *
 * An update starts only on an authenticated link with the car parked. The board
 * is in Listen-Only for the whole update.
 *
 * OTA CONTROL, write:
 *   01 <size u32 LE> <sha256, 32 bytes>   BEGIN
 *   02                                    END
 *   03                                    ABORT
 * OTA CONTROL, notify (6 bytes): <event> <status> <offset u32 LE>
 *   event 01 BEGIN result, 02 CREDIT, 03 END result, 04 update stopped
 *   offset = bytes written to flash so far
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCRL_OTA_OP_BEGIN 0x01u
#define SCRL_OTA_OP_END   0x02u
#define SCRL_OTA_OP_ABORT 0x03u

#define SCRL_OTA_EVT_BEGIN   0x01u
#define SCRL_OTA_EVT_CREDIT  0x02u
#define SCRL_OTA_EVT_END     0x03u
#define SCRL_OTA_EVT_STOPPED 0x04u

#define SCRL_OTA_OK              0x00u
#define SCRL_OTA_ERR_REQUEST     0x01u  // malformed control write
#define SCRL_OTA_ERR_STATE       0x02u  // wrong moment (update runs, or none runs)
#define SCRL_OTA_ERR_NOT_PARKED  0x03u  // the car moves or AP is engaged
#define SCRL_OTA_ERR_SIZE        0x04u  // image does not fit, or the byte count is wrong
#define SCRL_OTA_ERR_FLASH       0x05u  // flash write or erase failed
#define SCRL_OTA_ERR_SHA         0x06u  // SHA-256 does not match
#define SCRL_OTA_ERR_IMAGE       0x07u  // not a valid app image for this chip and board
#define SCRL_OTA_ERR_TIMEOUT     0x08u  // no data for SCRL_OTA_IDLE_MS
#define SCRL_OTA_ERR_LINK        0x09u  // the phone disconnected
#define SCRL_OTA_ERR_ABORTED     0x0Au  // the phone sent ABORT
#define SCRL_OTA_ERR_PENDING     0x0Bu  // the running image is not confirmed yet

// The phone may send this many bytes past the last CREDIT offset.
#define SCRL_OTA_WINDOW 4096u
// The board sends a CREDIT after this many new bytes.
#define SCRL_OTA_CREDIT_STEP 2048u
// An update with no data for this long stops.
#define SCRL_OTA_IDLE_MS 30000u
// A new image must be confirmed within this time.
#define SCRL_OTA_CONFIRM_MS (10u * 60u * 1000u)

#define SCRL_OTA_EVENT_LEN 6u

/** Read the state of the running image. Call once at boot, after scrl_proc_init(). */
void scrl_ota_init(void);

/** True while the running image waits for its confirm. */
bool scrl_ota_image_pending(void);

/** Mark the running image valid. Returns false when the flash write fails. */
bool scrl_ota_confirm(void);

/** True while an update runs. */
bool scrl_ota_active(void);

/**
 * Handle a write to OTA control. Fills the event to notify.
 * Returns true when the board must restart after the notification (END was good).
 */
bool scrl_ota_control(const uint8_t *data, size_t len, uint8_t event[SCRL_OTA_EVENT_LEN]);

/** Handle a write to OTA data. Returns true when `event` holds an event to notify. */
bool scrl_ota_data(const uint8_t *data, size_t len, uint8_t event[SCRL_OTA_EVENT_LEN]);

/** The phone disconnected: stop a running update. */
void scrl_ota_link_lost(void);

/**
 * Housekeeping, about every 100 ms. Stops an idle update and enforces the
 * confirm deadline. Returns true when `event` holds an event to notify.
 */
bool scrl_ota_tick(uint8_t event[SCRL_OTA_EVENT_LEN]);
