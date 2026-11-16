/*
 * test_scrl_spi.cpp — host tests for the hardware-free parts of the BLE board
 * SPI backends and the board identity (ble-board/main):
 *   scrl_mcp2515.h     CNF values, ID packing, buffer packing
 *   scrl_mcp2518fd.h   SPI commands, register addresses, bit timing, objects
 *   scrl_rx_ids.h      the hardware filter list
 *   scrl_board_desc.h  the board check of a BLE update
 * Build + run: make -C test check
 */

#include <stdio.h>
#include <string.h>

#include "scrl_board_desc.h"
#include "scrl_mcp2515.h"
#include "scrl_mcp2518fd.h"
#include "scrl_rx_ids.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            g_pass++;                                                           \
        } else {                                                                \
            g_fail++;                                                           \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                                \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

static CanFrame std_frame(uint16_t id, uint8_t dlc) {
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = id;
    f.dlc = dlc;
    for (uint8_t i = 0; i < 8; i++) f.data[i] = (uint8_t)(0xA0u + i);
    return f;
}

// ── MCP2515 ───────────────────────────────────────────────────────────────────

// Bit rate from CNF3, CNF2, CNF1 by the datasheet formula, independent of the
// table: TQ = 2 (BRP + 1) / Fosc, bit = SYNC + PRSEG + PHSEG1 + PHSEG2.
static uint32_t mcp2515_rate(uint32_t crystal_hz, const uint8_t cnf[3]) {
    uint32_t brp = (cnf[2] & 0x3Fu) + 1u;
    uint32_t prseg = (cnf[1] & 0x07u) + 1u;
    uint32_t phseg1 = ((cnf[1] >> 3) & 0x07u) + 1u;
    uint32_t phseg2 = (cnf[0] & 0x07u) + 1u;
    uint32_t tq = 1u + prseg + phseg1 + phseg2;
    return crystal_hz / (2u * brp * tq);
}

static void test_mcp2515_cnf(void) {
    uint8_t cnf[3];
    CHECK(mcp2515_cnf_500k(8000000u, cnf), "8 MHz has values");
    CHECK(cnf[0] == 0x82 && cnf[1] == 0x90 && cnf[2] == 0x00, "8 MHz = autowp CFG3/2/1");
    CHECK(mcp2515_rate(8000000u, cnf) == 500000u, "8 MHz gives 500 kbit/s");
    CHECK(cnf[1] & 0x80u, "8 MHz: BTLMODE set, PHSEG2 from CNF3");
    CHECK(mcp2515_cnf_500k(16000000u, cnf), "16 MHz has values");
    CHECK(cnf[0] == 0x86 && cnf[1] == 0xF0 && cnf[2] == 0x00, "16 MHz = autowp CFG3/2/1");
    CHECK(mcp2515_rate(16000000u, cnf) == 500000u, "16 MHz gives 500 kbit/s");
    CHECK(!mcp2515_cnf_500k(20000000u, cnf), "20 MHz has no values");
}

static void test_mcp2515_ids(void) {
    uint8_t b[4];
    mcp2515_std_id(0x3C2u, b);
    CHECK(b[0] == 0x78 && b[1] == 0x40 && b[2] == 0 && b[3] == 0, "0x3C2 -> SIDH 0x78 SIDL 0x40");
    CHECK((b[1] & MCP2515_SIDL_EXIDE) == 0, "standard filter: EXIDE clear");
    mcp2515_std_mask(b);
    CHECK(b[0] == 0xFF && b[1] == 0xE0 && b[2] == 0 && b[3] == 0, "mask: all 11 SID bits, no data bytes");
}

static void test_mcp2515_frames(void) {
    uint8_t buf[MCP2515_BUF_LEN] = {};
    CanFrame tx = std_frame(0x3C2u, 8);
    CHECK(mcp2515_tx_bytes(tx, buf) == 13u, "8-byte frame fills 13 bytes");
    CHECK(buf[4] == 8u, "TX DLC without RTR");
    CanFrame rx;
    CHECK(mcp2515_rx_frame(buf, &rx), "round trip parses");
    CHECK(rx.id == 0x3C2u && rx.ext == 0 && rx.req == 0 && rx.dlc == 8, "round trip header");
    CHECK(memcmp(rx.data, tx.data, 8) == 0, "round trip data");

    CanFrame shorter = std_frame(0x129u, 3);
    CHECK(mcp2515_tx_bytes(shorter, buf) == 8u, "3-byte frame fills 8 bytes");
    CHECK(mcp2515_rx_frame(buf, &rx) && rx.dlc == 3 && rx.data[3] == 0, "short frame, rest zero");

    // Extended ID 0x18DAF110: SIDH, SIDL with EXIDE and EID17:16, EID8, EID0.
    uint32_t eid = 0x18DAF110u;
    uint8_t ext[MCP2515_BUF_LEN] = {};
    ext[0] = (uint8_t)(eid >> 21);
    ext[1] = (uint8_t)(((eid >> 18) & 0x07u) << 5) | MCP2515_SIDL_EXIDE | (uint8_t)((eid >> 16) & 0x03u);
    ext[2] = (uint8_t)(eid >> 8);
    ext[3] = (uint8_t)eid;
    ext[4] = 2;
    CHECK(mcp2515_rx_frame(ext, &rx) && rx.ext == 1 && rx.id == eid, "extended ID parses");

    uint8_t rtr[MCP2515_BUF_LEN] = {};
    mcp2515_std_id(0x123u, rtr);
    rtr[1] |= MCP2515_SIDL_SRR;
    rtr[4] = 4;
    rtr[5] = 0x55;
    CHECK(mcp2515_rx_frame(rtr, &rx) && rx.req == 1 && rx.data[0] == 0, "standard remote frame, no data");

    uint8_t bad[MCP2515_BUF_LEN] = {};
    bad[4] = 9;
    CHECK(!mcp2515_rx_frame(bad, &rx), "DLC 9 rejected");
}

// ── MCP2518FD ─────────────────────────────────────────────────────────────────

static void test_fd_commands(void) {
    uint8_t c[2];
    mcp2518fd_cmd(MCP2518FD_OP_READ, 0x060, c);
    CHECK(c[0] == 0x30 && c[1] == 0x60, "READ 0x060");
    mcp2518fd_cmd(MCP2518FD_OP_WRITE, 0xE07, c);
    CHECK(c[0] == 0x2E && c[1] == 0x07, "WRITE 0xE07");
    mcp2518fd_cmd(MCP2518FD_OP_RESET, 0, c);
    CHECK(c[0] == 0 && c[1] == 0, "RESET");
    CHECK(scrl_fd_fifocon(1) == 0x05C && scrl_fd_fifosta(1) == 0x060 && scrl_fd_fifoua(1) == 0x064,
          "FIFO1 registers");
    CHECK(scrl_fd_fifocon(2) == 0x068 && scrl_fd_fifosta(2) == 0x06C && scrl_fd_fifoua(2) == 0x070,
          "FIFO2 registers");
    CHECK(scrl_fd_fltcon(5) == 0x1D5 && scrl_fd_fltobj(5) == 0x218 && scrl_fd_mask(0) == 0x1F4,
          "filter registers");
    CHECK(MCP2518FD_CON_REQ_CONFIG == 0x0C, "Configuration mode request with ABAT");
}

static void test_fd_timing(void) {
    ScrlFdTiming t;
    CHECK(scrl_fd_timing_500k(20000000u, &t), "20 MHz SYSCLK has a timing");
    CHECK(t.brp == 1 && t.tseg1 == 31 && t.tseg2 == 8 && t.sjw == 8, "40 quanta, 80 %%, SJW 8");
    CHECK(scrl_fd_btcfg(t) == 0x001E0707u, "CiNBTCFG 0x001E0707 (got 0x%08X)", (unsigned)scrl_fd_btcfg(t));
    CHECK(mcp2518fd_timing_ok(20000000u, t.brp, t.tseg1, t.tseg2, t.sjw), "hypery timing check passes");
    CHECK(scrl_fd_timing_500k(40000000u, &t) && t.brp == 2 && t.tseg1 + t.tseg2 + 1 == 40,
          "40 MHz SYSCLK: BRP 2, 40 quanta");
    CHECK(!scrl_fd_timing_500k(1000001u, &t), "no exact rate: refused");
}

static void test_fd_objects(void) {
    uint8_t obj[SCRL_FD_OBJ_LEN];
    CanFrame tx = std_frame(0x3C2u, 8);
    scrl_fd_tx_object(tx, obj);
    CHECK(scrl_fd_le32(&obj[0]) == 0x3C2u, "TX ID word");
    CHECK(scrl_fd_le32(&obj[4]) == 8u, "TX flags: DLC 8, no FDF, BRS, RTR, IDE");
    CanFrame rx;
    CHECK(scrl_fd_rx_frame(obj, &rx), "round trip parses");
    CHECK(rx.id == 0x3C2u && rx.ext == 0 && rx.req == 0 && rx.dlc == 8, "round trip header");
    CHECK(memcmp(rx.data, tx.data, 8) == 0, "round trip data");

    // Extended ID: SID in bits 10:0, EID in bits 28:11.
    uint32_t eid = 0x18DAF110u;
    memset(obj, 0, sizeof(obj));
    scrl_fd_put_le32(&obj[0], ((eid >> 18) & 0x7FFu) | ((eid & 0x3FFFFu) << 11));
    scrl_fd_put_le32(&obj[4], (1u << 4) | 1u);
    CHECK(scrl_fd_rx_frame(obj, &rx) && rx.ext == 1 && rx.id == eid, "extended ID reorder");

    scrl_fd_put_le32(&obj[4], (1u << 7) | 8u);
    CHECK(!scrl_fd_rx_frame(obj, &rx), "CAN FD frame rejected");
    scrl_fd_put_le32(&obj[4], 9u);
    CHECK(!scrl_fd_rx_frame(obj, &rx), "DLC 9 rejected");
}

// ── filters and board identity ────────────────────────────────────────────────

static void test_rx_ids(void) {
    CHECK(SCRL_RX_ID_COUNT == 6u, "six IDs: the MCP2515 has six filters");
    bool has_3c2 = false, has_39b = false, distinct = true;
    for (unsigned i = 0; i < SCRL_RX_ID_COUNT; i++) {
        CHECK(SCRL_RX_IDS[i] <= 0x7FFu, "ID %u is standard", i);
        has_3c2 |= SCRL_RX_IDS[i] == 0x3C2u;
        has_39b |= SCRL_RX_IDS[i] == 0x39Bu;
        for (unsigned j = i + 1; j < SCRL_RX_ID_COUNT; j++) distinct &= SCRL_RX_IDS[i] != SCRL_RX_IDS[j];
    }
    CHECK(has_3c2 && has_39b, "the native frame and the DAS frame pass");
    CHECK(distinct, "no ID twice");
}

static ScrlBoardDesc desc(const char *board) {
    ScrlBoardDesc d;
    memset(&d, 0, sizeof(d));
    d.magic = SCRL_BOARD_DESC_MAGIC;
    d.version = SCRL_BOARD_DESC_VERSION;
    strncpy(d.board, board, sizeof(d.board) - 1);
    strncpy(d.can, "twai", sizeof(d.can) - 1);
    return d;
}

static void test_board_desc(void) {
    ScrlBoardDesc self = desc("m5stack-atom");
    CHECK(scrl_board_desc_match(desc("m5stack-atom"), self), "same board passes");
    CHECK(!scrl_board_desc_match(desc("esp32-generic-twai"), self), "other board on the same chip fails");
    CHECK(!scrl_board_desc_match(desc("m5stack-atom-matrix"), self), "longer name with the same start fails");
    ScrlBoardDesc bad = desc("m5stack-atom");
    bad.magic = 0;
    CHECK(!scrl_board_desc_match(bad, self), "no magic (old image) fails");
    bad = desc("m5stack-atom");
    bad.version = 2;
    CHECK(!scrl_board_desc_match(bad, self), "unknown version fails");
    bad = desc("m5stack-atom");
    memset(bad.board, 'x', sizeof(bad.board));
    CHECK(!scrl_board_desc_match(bad, self), "name with no NUL fails");
    CHECK(sizeof(ScrlBoardDesc) == 56u, "descriptor size");
}

int main(void) {
    printf("test_scrl_spi\n");
    test_mcp2515_cnf();
    test_mcp2515_ids();
    test_mcp2515_frames();
    test_fd_commands();
    test_fd_timing();
    test_fd_objects();
    test_rx_ids();
    test_board_desc();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
