#pragma once
/*
 * scrl_ble.h — BLE control of the board (NimBLE, peripheral only).
 *
 * One custom service, base UUID e591xxxx-51ba-4d0b-8422-5cb9dc778552:
 *   0001  service
 *   0002  Info         read                  JSON: version, build, board, chip, ID
 *   0003  Status       read                  JSON, see scrl_status.h
 *   0004  Live         read, notify (1 Hz)   19 bytes, see scrl_status.h
 *   0005  Settings     read, write           JSON, see scrl_settings.h
 *   0006  Command      write                 one byte, SCRL_CMD_*
 *   0007  Log          notify                event lines, the last 64 on subscribe
 *   0008  OTA control  write, notify         see scrl_ota.h
 *   0009  OTA data     write without response
 * Info is open. Every other characteristic needs an encrypted and
 * authenticated link.
 *
 * SECURITY
 *   - LE Secure Connections only, with bonding. The board shows nothing, so the
 *     passkey is static: 6 digits, made at first boot, kept in NVS, printed on
 *     the USB console for the label on the board.
 *   - The board accepts a new bond only in the first 5 minutes after power-on,
 *     or for 2 minutes after a press of the button. A restart without a power
 *     cycle does not open the window, except when the board has no bond at all.
 *   - It keeps 2 bonds and never drops one by itself. With 2 bonds a third
 *     phone cannot bond. Hold the button for 5 s (or send SCRL_CMD_FORGET_BONDS)
 *     to delete them.
 *   - A phone that does not reach an authenticated link in 30 s is disconnected.
 *
 * RADIO
 *   Connectable advertising every 1 s at 0 dBm, name SCRL-xxxx. One connection:
 *   advertising stops while a phone is connected.
 */

#include <stdbool.h>
#include <stdint.h>

#define SCRL_CMD_RESET_COUNTERS 0x01u
#define SCRL_CMD_REBOOT         0x02u
#define SCRL_CMD_FORGET_BONDS   0x03u
#define SCRL_CMD_FACTORY_RESET  0x04u
#define SCRL_CMD_CONFIRM_IMAGE  0x05u  // makes a new image permanent, see scrl_ota.h

#define SCRL_BLE_PAIR_WINDOW_BOOT_MS   (5u * 60u * 1000u)
#define SCRL_BLE_PAIR_WINDOW_BUTTON_MS (2u * 60u * 1000u)
#define SCRL_BLE_AUTH_TIMEOUT_MS       30000u

/** Start the BLE stack and advertise. power_on opens the 5-minute pairing window. */
void scrl_ble_init(uint32_t passkey, bool power_on);

/** Housekeeping, about every 100 ms: Live and Log notifications, time limits. */
void scrl_ble_tick(void);

/** Accept new bonds for the next duration_ms. */
void scrl_ble_open_pairing(uint32_t duration_ms);

bool scrl_ble_pairing_open(void);

/** Delete every bond and drop the link. */
void scrl_ble_forget_bonds(void);
