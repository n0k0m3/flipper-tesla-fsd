#pragma once
/*
 * scrl_rx_ids.h — the CAN IDs that scrl_proc.cpp reads.
 *
 * The SPI backends load these as exact hardware filters: the MCP2515 has only
 * two receive buffers and loses frames at the full Vehicle CAN rate. The TWAI
 * backend takes every frame. Keep this list equal to the IDs that
 * scrl_proc_handle_frame() looks at; test_scrl_spi checks the count.
 *
 * Order: the first two go to MCP2515 RXB0 (with rollover into RXB1), the
 * other four to RXB1. The busiest and most important IDs go first.
 */

#include <stdint.h>
#include "fsd_handler.h"  // CAN_ID_* (config.h)

#define SCRL_RX_ID_COUNT 6u

static const uint16_t SCRL_RX_IDS[SCRL_RX_ID_COUNT] = {
    CAN_ID_VCLEFT_SWITCH,   // 0x3C2: the native frame that a tick copies
    CAN_ID_DAS_STATUS_HW4,  // 0x39B: AP and hands-on state
    0x257u,                 // DI_speed: Autopark TX pause
    CAN_ID_STEER_ANGLE,     // 0x129
    CAN_ID_GTW_CAR_STATE,   // 0x318: Tesla OTA TX pause
    CAN_ID_EPAS_STATUS,     // 0x370: wrong-bus check
};
