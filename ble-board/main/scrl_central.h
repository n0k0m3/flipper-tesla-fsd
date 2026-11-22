#pragma once
/*
 * scrl_central.h — the BLE central role of the board, for the BLE remote
 * buttons of the TSL6 engine (components/tsl6/port/tsl6_central.c).
 *
 * The engine calls the operations of scrl_central_ops() from the CAN task.
 * NimBLE runs them and reports each result as a TSL6 central event through
 * scrl_tsl6_ble_event(SCRL_TSL6_EV_CENTRAL, ...).
 *
 * Remotes pair with "Just Works" or a passkey that the board makes up, as on
 * the TSL6. For that pairing only, the board allows legacy pairing and no
 * MITM protection; a phone link still needs LE Secure Connections with the
 * passkey (scrl_ble.cpp checks it). The board keeps the addresses of its
 * remotes (NVS key "rbonds"), so their bonds do not count as phone bonds.
 */

#include <stdbool.h>
#include <stdint.h>

#include "host/ble_hs.h"
#include "tsl6_port.h"

#define SCRL_CENTRAL_REMOTES 3  // the TSL6 links up to 3 remotes

/** The operations for the engine (hal->central). */
const tsl6_central_ops_t *scrl_central_ops(void);

/** Load the remote list. Call once, after NVS and before BLE starts. */
void scrl_central_init(void);

/** True when the bond of this peer belongs to a remote, not to a phone. */
bool scrl_central_is_remote(const ble_addr_t *peer);

/** The own address type of the board (scrl_ble.cpp sets it at sync). */
void scrl_central_set_own_addr_type(uint8_t type);
