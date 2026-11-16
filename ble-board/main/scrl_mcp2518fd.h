#pragma once
/*
 * scrl_mcp2518fd.h — MCP2518FD registers and the packing that needs no
 * hardware, classic CAN 2.0 only. Header-only and free of ESP-IDF:
 * test/test_scrl_spi.cpp covers it.
 *
 * The crystal check, the bit-timing check, the error counters and the
 * detection states come from hypery's esp32/.firmware/can_mcp2518fd.h (its own
 * host test covers them). Register addresses, the start sequence and the
 * bit-timing search come from the library that hypery uses (ACAN2517FD
 * 2.1.16), so the controller runs as on the hypery T-2CAN FD build.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "can_mcp2518fd.h"  // hypery helpers and MCP2518FD_REG_CON/OSC/TBC/TSCON/BDIAG0/DEVID

// SPI instructions: 4-bit opcode, 12-bit address, sent high byte first.
#define MCP2518FD_OP_RESET 0x0u
#define MCP2518FD_OP_WRITE 0x2u
#define MCP2518FD_OP_READ  0x3u

static inline void mcp2518fd_cmd(uint8_t op, uint16_t addr, uint8_t out[2]) {
    uint16_t cmd = (uint16_t)(((uint16_t)op << 12) | (addr & 0x0FFFu));
    out[0] = (uint8_t)(cmd >> 8);
    out[1] = (uint8_t)(cmd & 0xFFu);
}

// Registers that hypery's header does not name.
static constexpr uint16_t SCRL_FD_REG_NBTCFG = 0x004;
static constexpr uint16_t SCRL_FD_REG_DBTCFG = 0x008;
static constexpr uint16_t SCRL_FD_REG_TDC    = 0x00C;
static constexpr uint16_t SCRL_FD_REG_TREC   = 0x034;
static constexpr uint16_t SCRL_FD_REG_IOCON  = 0xE04;
static constexpr uint16_t SCRL_FD_RAM_START  = 0x400;
static constexpr uint16_t SCRL_FD_RAM_END    = 0xC00;  // first address after the RAM

static inline uint16_t scrl_fd_fifocon(uint8_t m) { return (uint16_t)(0x05C + 12 * (m - 1)); }
static inline uint16_t scrl_fd_fifosta(uint8_t m) { return (uint16_t)(0x060 + 12 * (m - 1)); }
static inline uint16_t scrl_fd_fifoua(uint8_t m)  { return (uint16_t)(0x064 + 12 * (m - 1)); }
static inline uint16_t scrl_fd_fltcon(uint8_t n)  { return (uint16_t)(0x1D0 + n); }
static inline uint16_t scrl_fd_fltobj(uint8_t n)  { return (uint16_t)(0x1F0 + 8 * n); }
static inline uint16_t scrl_fd_mask(uint8_t n)    { return (uint16_t)(0x1F4 + 8 * n); }

// FIFO plan, as the library: FIFO1 receives, FIFO2 transmits. No TXQ, no TEF.
static constexpr uint8_t SCRL_FD_RX_FIFO = 1;
static constexpr uint8_t SCRL_FD_TX_FIFO = 2;
static constexpr uint8_t SCRL_FD_RX_FIFO_SIZE = 32;  // 8-byte objects
static constexpr uint8_t SCRL_FD_TX_FIFO_SIZE = 1;   // one tick at a time

// CiCON operation modes (bits 26-24 to request, 23-21 to read).
static constexpr uint8_t SCRL_FD_MODE_LISTEN   = 3;
static constexpr uint8_t SCRL_FD_MODE_NORMAL20 = 6;  // CAN 2.0: FDF and BRS always 0

// CiFIFOSTAm byte 0.
static constexpr uint8_t SCRL_FD_STA_NOT_EMPTY_OR_FULL = 1u << 0;  // RX: not empty. TX: not full
static constexpr uint8_t SCRL_FD_STA_TX_EMPTY          = 1u << 2;  // TX: FIFO empty
static constexpr uint8_t SCRL_FD_STA_RXOV              = 1u << 3;  // RX: overflow, write 0 to clear
static constexpr uint8_t SCRL_FD_STA_TXATIF            = 1u << 4;  // TX: attempts exhausted

// CiFIFOCONm byte 1.
static constexpr uint8_t SCRL_FD_CON_UINC  = 1u << 0;
static constexpr uint8_t SCRL_FD_CON_TXREQ = 1u << 1;

// CiTREC: transmitter bus-off.
static constexpr uint32_t SCRL_FD_TREC_TXBO = 1u << 21;

// CiOSC: system clock divide by 2.
static constexpr uint8_t SCRL_FD_OSC_SCLKDIV = 1u << 4;

// Filter: compare the 11 SID bits and IDE (MIDE), so only standard frames with
// the exact ID pass.
static constexpr uint32_t SCRL_FD_MASK_STD_EXACT = 0x7FFu | (1u << 30);

// RX and TX objects with no time stamp: ID word, flags word, 8 data bytes.
static constexpr uint8_t SCRL_FD_OBJ_LEN = 16;

static inline uint32_t scrl_fd_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline void scrl_fd_put_le32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/** CiCON operation mode as read back. */
static inline uint8_t scrl_fd_opmod(uint32_t con) { return (uint8_t)mcp2518fd_opmod(con); }

struct ScrlFdTiming {
    uint32_t brp, tseg1, tseg2, sjw;  // in time quanta (register field + 1)
};

/**
 * Nominal timing for 500 kbit/s as ACAN2517FDSettings computes it with
 * DataBitRateFactor x1: the most quanta with an exact rate, sample point 80 %,
 * SJW = TSEG2. False when no exact rate exists.
 */
static inline bool scrl_fd_timing_500k(uint32_t sysclk_hz, ScrlFdTiming *t) {
    const uint32_t rate = 500000u;
    const uint32_t max_tq = 32u + 16u;  // library data-phase limits, also used for x1
    uint32_t best_brp = 0, best_tq = 0;
    for (uint32_t tq = 4; tq <= max_tq; tq++) {
        uint32_t brp = sysclk_hz / rate / tq;
        if (brp == 0u) break;
        if (brp <= 256u && (uint64_t)rate * tq * brp == sysclk_hz) {
            best_brp = brp;
            best_tq = tq;
        }
    }
    if (best_brp == 0u) return false;
    uint32_t ps2 = best_tq / 5u;
    if (ps2 == 0u) ps2 = 1u;
    t->brp = best_brp;
    t->tseg2 = ps2;
    t->tseg1 = best_tq - ps2 - 1u;
    t->sjw = ps2;
    return mcp2518fd_timing_ok(sysclk_hz, t->brp, t->tseg1, t->tseg2, t->sjw);
}

/** CiNBTCFG (and CiDBTCFG, which the library fills the same way at x1). */
static inline uint32_t scrl_fd_btcfg(const ScrlFdTiming &t) {
    return ((t.brp - 1u) << 24) | ((t.tseg1 - 1u) << 16) | ((t.tseg2 - 1u) << 8) | (t.sjw - 1u);
}

/**
 * Frame from a receive object. False for CAN FD frames and for a DLC above 8:
 * the caller counts them as rejected.
 */
static inline bool scrl_fd_rx_frame(const uint8_t obj[SCRL_FD_OBJ_LEN], CanFrame *f) {
    uint32_t idw = scrl_fd_le32(&obj[0]);
    uint32_t flags = scrl_fd_le32(&obj[4]);
    if (flags & (1u << 7)) return false;  // FDF
    uint8_t dlc = (uint8_t)(flags & 0x0Fu);
    if (dlc > 8u) return false;
    memset(f, 0, sizeof(*f));
    bool ext = (flags & (1u << 4)) != 0u;
    f->id = ext ? (((idw >> 11) & 0x3FFFFu) | ((idw & 0x7FFu) << 18)) : (idw & 0x7FFu);
    f->ext = ext ? 1u : 0u;
    f->req = (flags & (1u << 5)) ? 1u : 0u;
    f->dlc = dlc;
    if (!f->req) memcpy(f->data, &obj[8], dlc);
    return true;
}

/** Transmit object for a standard classic data frame (FDF, BRS, RTR, IDE all 0). */
static inline void scrl_fd_tx_object(const CanFrame &f, uint8_t obj[SCRL_FD_OBJ_LEN]) {
    memset(obj, 0, SCRL_FD_OBJ_LEN);
    uint8_t dlc = f.dlc > 8u ? 8u : f.dlc;
    scrl_fd_put_le32(&obj[0], f.id & 0x7FFu);
    scrl_fd_put_le32(&obj[4], dlc);
    memcpy(&obj[8], f.data, dlc);
}
