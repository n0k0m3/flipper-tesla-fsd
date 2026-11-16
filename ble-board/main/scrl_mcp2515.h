#pragma once
/*
 * scrl_mcp2515.h — MCP2515 registers and the byte packing that needs no
 * hardware. Header-only and free of ESP-IDF: test/test_scrl_spi.cpp covers it.
 *
 * Register addresses, instructions and the CNF values come from the library
 * that hypery uses (autowp-mcp2515 1.3.1, mcp2515.h), so the bit timing is the
 * same as on the hypery MCP2515 builds.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fsd_handler.h"  // CanFrame

// Instructions.
#define MCP2515_INSTR_WRITE      0x02u
#define MCP2515_INSTR_READ       0x03u
#define MCP2515_INSTR_BITMOD     0x05u
#define MCP2515_INSTR_LOAD_TX0   0x40u  // load TXB0 from TXB0SIDH
#define MCP2515_INSTR_RTS_TX0    0x81u
#define MCP2515_INSTR_READ_RX0   0x90u  // read RXB0 from RXB0SIDH, clears RX0IF at CS high
#define MCP2515_INSTR_READ_RX1   0x94u  // read RXB1 from RXB1SIDH, clears RX1IF at CS high
#define MCP2515_INSTR_RESET      0xC0u

// Registers.
#define MCP2515_RXF0SIDH 0x00u
#define MCP2515_RXF1SIDH 0x04u
#define MCP2515_RXF2SIDH 0x08u
#define MCP2515_CANSTAT  0x0Eu
#define MCP2515_CANCTRL  0x0Fu
#define MCP2515_RXF3SIDH 0x10u
#define MCP2515_RXF4SIDH 0x14u
#define MCP2515_RXF5SIDH 0x18u
#define MCP2515_TEC      0x1Cu
#define MCP2515_RXM0SIDH 0x20u
#define MCP2515_RXM1SIDH 0x24u
#define MCP2515_CNF3     0x28u  // CNF3, CNF2, CNF1 follow in this order
#define MCP2515_CANINTE  0x2Bu
#define MCP2515_CANINTF  0x2Cu  // EFLG follows
#define MCP2515_EFLG     0x2Du
#define MCP2515_TXB0CTRL 0x30u
#define MCP2515_TXB1CTRL 0x40u
#define MCP2515_TXB2CTRL 0x50u
#define MCP2515_RXB0CTRL 0x60u
#define MCP2515_RXB1CTRL 0x70u

// CANCTRL / CANSTAT.
#define MCP2515_CANCTRL_REQOP 0xE0u
#define MCP2515_CANCTRL_ABAT  0x10u
#define MCP2515_CANCTRL_OSM   0x08u
#define MCP2515_MODE_NORMAL   0x00u
#define MCP2515_MODE_LISTEN   0x60u
#define MCP2515_MODE_CONFIG   0x80u
#define MCP2515_CANSTAT_OPMOD 0xE0u

// CANINTF / CANINTE bits.
#define MCP2515_INT_RX0  0x01u
#define MCP2515_INT_RX1  0x02u
#define MCP2515_INT_TX0  0x04u
#define MCP2515_INT_ERR  0x20u
#define MCP2515_INT_MERR 0x80u

// EFLG bits.
#define MCP2515_EFLG_RX1OVR 0x80u
#define MCP2515_EFLG_RX0OVR 0x40u
#define MCP2515_EFLG_TXBO   0x20u

// TXBnCTRL.
#define MCP2515_TXB_TXREQ 0x08u

// RXBnCTRL.
#define MCP2515_RXB0CTRL_BUKT 0x04u  // RXB0 full: the frame rolls over into RXB1

// Buffer layout from SIDH: SIDH, SIDL, EID8, EID0, DLC, D0..D7.
#define MCP2515_SIDL_EXIDE 0x08u
#define MCP2515_SIDL_SRR   0x10u  // standard frame: remote request
#define MCP2515_DLC_RTR    0x40u  // extended frame: remote request
#define MCP2515_BUF_LEN    13u

/** CNF3, CNF2, CNF1 (the register order) for 500 kbit/s. False for a crystal without values. */
static inline bool mcp2515_cnf_500k(uint32_t crystal_hz, uint8_t out[3]) {
    switch (crystal_hz) {
    case 8000000u:   // MCP_8MHz_500kBPS_CFG1..3 = 0x00, 0x90, 0x82
        out[0] = 0x82u;
        out[1] = 0x90u;
        out[2] = 0x00u;
        return true;
    case 16000000u:  // MCP_16MHz_500kBPS_CFG1..3 = 0x00, 0xF0, 0x86
        out[0] = 0x86u;
        out[1] = 0xF0u;
        out[2] = 0x00u;
        return true;
    default:
        return false;
    }
}

/** SIDH, SIDL, EID8, EID0 of a standard ID (filter or TX buffer). */
static inline void mcp2515_std_id(uint16_t id, uint8_t out[4]) {
    out[0] = (uint8_t)((id >> 3) & 0xFFu);
    out[1] = (uint8_t)((id & 0x07u) << 5);
    out[2] = 0u;
    out[3] = 0u;
}

/** Mask that compares all 11 bits of a standard ID and nothing else. */
static inline void mcp2515_std_mask(uint8_t out[4]) {
    mcp2515_std_id(0x7FFu, out);
}

/** Frame from a 13-byte receive buffer (from SIDH). False when the DLC is above 8. */
static inline bool mcp2515_rx_frame(const uint8_t b[MCP2515_BUF_LEN], CanFrame *f) {
    uint8_t dlc = b[4] & 0x0Fu;
    if (dlc > 8u) return false;
    memset(f, 0, sizeof(*f));
    bool ext = (b[1] & MCP2515_SIDL_EXIDE) != 0u;
    uint32_t id = ((uint32_t)b[0] << 3) | ((uint32_t)b[1] >> 5);
    if (ext) {
        id = (id << 2) | (b[1] & 0x03u);
        id = (id << 8) | b[2];
        id = (id << 8) | b[3];
        f->req = (b[4] & MCP2515_DLC_RTR) ? 1u : 0u;
    } else {
        f->req = (b[1] & MCP2515_SIDL_SRR) ? 1u : 0u;
    }
    f->id = id;
    f->ext = ext ? 1u : 0u;
    f->dlc = dlc;
    if (!f->req) memcpy(f->data, &b[5], dlc);
    return true;
}

/** TX buffer bytes (from TXBnSIDH) for a standard data frame. Returns the byte count. */
static inline uint8_t mcp2515_tx_bytes(const CanFrame &f, uint8_t out[MCP2515_BUF_LEN]) {
    mcp2515_std_id((uint16_t)(f.id & 0x7FFu), out);
    uint8_t dlc = f.dlc > 8u ? 8u : f.dlc;
    out[4] = dlc;
    memcpy(&out[5], f.data, dlc);
    return (uint8_t)(5u + dlc);
}
