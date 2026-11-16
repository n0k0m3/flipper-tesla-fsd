/*
 * scrl_can_mcp2518fd.cpp — MCP2518FD backend on SPI, classic CAN 2.0 at
 * 500 kbit/s (LilyGO T-2CAN FD, can1). See scrl_can_hw.h.
 *
 * Start, as hypery's T-2CAN FD driver does it:
 *   1. Configuration mode, SPI reset, oscillator ready, RAM read-back.
 *   2. Probe start in Listen-Only on the undivided clock. Measure the crystal
 *      with the time base counter. Nothing reaches the bus, whatever the
 *      crystal is.
 *   3. A crystal that is not the build's crystal keeps the chip off the bus
 *      for good: a wrong bit rate in normal mode puts error frames on the bus.
 *   4. Real start: SYSCLK 20 MHz, nominal timing 80 % (ACAN2517FD search).
 *
 * The chip has no INT pin wired that the build may use (GPIO9 is INT0/XSTBY),
 * so one task polls it every 1 ms. The six IDs of scrl_rx_ids.h are exact
 * filters. Bus-off: the chip recovers by itself after 128 x 11 recessive bits
 * (2.8 ms) and would then send what waits in the TX FIFO. The task reads
 * CiTREC on every poll and puts the chip in Configuration mode with ABAT on
 * TXBO.
 */

#include "scrl_can_hw.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "scrl_board.h"
#include "scrl_mcp2518fd.h"
#include "scrl_rx_ids.h"
#include "scrl_spi.h"

static const char *TAG = "scrl_fd";

// Before the oscillator is known to run, as the library and hypery reset the
// chip. After that, in spec for SYSCLK down to 10 MHz (hypery's raw access clock).
#define SPI_SLOW_HZ 800000u
#define SPI_FAST_HZ 4000000u
#define POLL_TASK_STACK 3072
#define POLL_TASK_PRIORITY (configMAX_PRIORITIES - 3)
#define RX_BURST_MAX 8
#define BDIAG_PERIOD_MS 100u
// Bounded waits (library and hypery values).
#define CONFIG_WAIT_MS 2u
#define OSC_WAIT_MS 10u
#define MODE_WAIT_MS 10u

#if SCRL_SPI_CRYSTAL_HZ == 40000000u
#define OSC_DIVIDE true   // 40 MHz / 2 = SYSCLK 20 MHz
#elif SCRL_SPI_CRYSTAL_HZ == 20000000u
#define OSC_DIVIDE false
#else
#error "MCP2518FD crystal must be 20 or 40 MHz"
#endif
#define SYSCLK_HZ 20000000u

static TaskHandle_t s_task;
static bool s_running;            // out of Configuration mode (SPI lock)
static bool s_tx_pending;         // the TX FIFO holds a frame (SPI lock)
static bool s_crystal_ok;         // measured once and matched
static bool s_crystal_wrong;      // measured and not the build's crystal: never start
static uint32_t s_last_empty_ms;  // last poll that found the RX FIFO empty
static uint32_t s_bdiag_ms;
static uint32_t s_spi_hz;

// ── SPI access (hold the lock) ────────────────────────────────────────────────

static void spi_clock(uint32_t hz) {
    if (hz != s_spi_hz) {
        scrl_spi_set_clock(hz);
        s_spi_hz = hz;
    }
}

static uint32_t read32(uint16_t addr) {
    uint8_t b[6] = {};
    mcp2518fd_cmd(MCP2518FD_OP_READ, addr, b);
    scrl_spi_transfer(b, sizeof(b));
    return scrl_fd_le32(&b[2]);
}

static uint8_t read8(uint16_t addr) {
    uint8_t b[3] = {};
    mcp2518fd_cmd(MCP2518FD_OP_READ, addr, b);
    scrl_spi_transfer(b, sizeof(b));
    return b[2];
}

static void write32(uint16_t addr, uint32_t v) {
    uint8_t b[6];
    mcp2518fd_cmd(MCP2518FD_OP_WRITE, addr, b);
    scrl_fd_put_le32(&b[2], v);
    scrl_spi_transfer(b, sizeof(b));
}

static void write8(uint16_t addr, uint8_t v) {
    uint8_t b[3];
    mcp2518fd_cmd(MCP2518FD_OP_WRITE, addr, b);
    b[2] = v;
    scrl_spi_transfer(b, sizeof(b));
}

static void read_block(uint16_t addr, uint8_t *out, uint8_t len) {
    uint8_t b[2 + SCRL_FD_OBJ_LEN] = {};
    mcp2518fd_cmd(MCP2518FD_OP_READ, addr, b);
    scrl_spi_transfer(b, 2u + len);
    memcpy(out, &b[2], len);
}

static void write_block(uint16_t addr, const uint8_t *data, uint8_t len) {
    uint8_t b[2 + SCRL_FD_OBJ_LEN];
    mcp2518fd_cmd(MCP2518FD_OP_WRITE, addr, b);
    memcpy(&b[2], data, len);
    scrl_spi_transfer(b, 2u + len);
}

static bool wait_mode(uint8_t mode, uint32_t limit_ms) {
    uint32_t t0 = scrl_millis();
    do {
        if (scrl_fd_opmod(read32(MCP2518FD_REG_CON)) == mode) return true;
    } while ((uint32_t)(scrl_millis() - t0) <= limit_ms);
    return false;
}

// Configuration mode with every pending TX aborted: off the bus.
static void chip_off(void) {
    write8(MCP2518FD_REG_CON + 3, MCP2518FD_CON_REQ_CONFIG);
    wait_mode((uint8_t)MCP2518FD_OPMOD_CONFIG, CONFIG_WAIT_MS);
    s_running = false;
    if (s_tx_pending) {
        s_tx_pending = false;
        scrl_can_note_tx_done(false);
    }
}

static bool ram_check(void) {
    for (uint32_t bit = 1; bit != 0u; bit <<= 1) {
        write32(SCRL_FD_RAM_START, bit);
        if (read32(SCRL_FD_RAM_START) != bit) return false;
    }
    return true;
}

// Reset and set up the chip, then request `mode`. divide: SYSCLK = crystal / 2.
static bool configure(uint8_t mode, bool divide) {
    spi_clock(SPI_SLOW_HZ);
    // Configuration mode first: a controller still running from before an
    // ESP32 reset ends its frame and aborts the rest.
    write8(MCP2518FD_REG_CON + 3, MCP2518FD_CON_REQ_CONFIG);
    wait_mode((uint8_t)MCP2518FD_OPMOD_CONFIG, CONFIG_WAIT_MS);
    uint8_t reset[2] = {};
    mcp2518fd_cmd(MCP2518FD_OP_RESET, 0, reset);
    scrl_spi_transfer(reset, sizeof(reset));

    uint32_t t0 = scrl_millis();
    uint32_t osc = read32(MCP2518FD_REG_OSC);
    while (!mcp2518fd_osc_ready(osc) && (uint32_t)(scrl_millis() - t0) < OSC_WAIT_MS) {
        vTaskDelay(1);
        osc = read32(MCP2518FD_REG_OSC);
    }
    Mcp2518fdReadback rb = {};
    rb.con = read32(MCP2518FD_REG_CON);
    rb.osc = osc;
    rb.devid = read32(MCP2518FD_REG_DEVID);
    if (mcp2518fd_spi_state(rb.con, rb.osc) != MCP2518FD_SPI_CLOCK_OK || !ram_check()) {
        ESP_LOGE(TAG, "not detected: CiCON 0x%08X OSC 0x%08X DEVID 0x%08X - %s",
                 (unsigned)rb.con, (unsigned)rb.osc, (unsigned)rb.devid,
                 mcp2518fd_readback_text(rb));
        return false;
    }

    // No PLL. The library writes SCLKDIV and waits for nothing (it waits only
    // for the PLL). Give the divider one tick; the RAM read-back below checks
    // the link at the fast clock.
    write8(MCP2518FD_REG_OSC, divide ? SCRL_FD_OSC_SCLKDIV : 0u);
    vTaskDelay(1);
    spi_clock(SPI_FAST_HZ);
    if (!ram_check()) {
        ESP_LOGE(TAG, "RAM read-back failed at %u Hz", (unsigned)SPI_FAST_HZ);
        return false;
    }
    uint8_t zeros[SCRL_FD_OBJ_LEN] = {};
    for (uint16_t a = SCRL_FD_RAM_START; a < SCRL_FD_RAM_END; a += SCRL_FD_OBJ_LEN) {
        write_block(a, zeros, sizeof(zeros));
    }

    write8(SCRL_FD_REG_IOCON + 3, 0x03);    // PM1/PM0 stay GPIO (reset values), no SOF, push-pull
    write8(MCP2518FD_REG_CON + 0, 1u << 6); // PXEDIS
    write32(SCRL_FD_REG_TDC, 1u << 25);     // EDGFLTEN, no TDC (nominal rate only)
    write8(MCP2518FD_REG_CON + 2, 0x01);    // RTXAT: FIFO TXAT counts; no TXQ, no TEF

    // FIFO1 receives: 32 objects of 8 bytes.
    write8(scrl_fd_fifocon(SCRL_FD_RX_FIFO) + 3, (uint8_t)(SCRL_FD_RX_FIFO_SIZE - 1u));
    write8(scrl_fd_fifocon(SCRL_FD_RX_FIFO) + 0, 0x09);  // RXOVIE, TFNRFNIE (flags only)
    // FIFO2 transmits: one object, unlimited retransmission (TXAT = 2), priority 0.
    write8(scrl_fd_fifocon(SCRL_FD_TX_FIFO) + 2, (uint8_t)(2u << 5));
    write8(scrl_fd_fifocon(SCRL_FD_TX_FIFO) + 3, (uint8_t)(SCRL_FD_TX_FIFO_SIZE - 1u));
    write8(scrl_fd_fifocon(SCRL_FD_TX_FIFO) + 0, 0x90);  // TXEN, TXATIE

    for (uint8_t i = 0; i < SCRL_RX_ID_COUNT; i++) {
        write32(scrl_fd_mask(i), SCRL_FD_MASK_STD_EXACT);
        write32(scrl_fd_fltobj(i), SCRL_RX_IDS[i]);  // EXIDE 0: standard frames
        write8(scrl_fd_fltcon(i), (uint8_t)(0x80u | SCRL_FD_RX_FIFO));
    }

    ScrlFdTiming t;
    if (!scrl_fd_timing_500k(SYSCLK_HZ, &t)) {
        ESP_LOGE(TAG, "bit timing rejected");
        return false;
    }
    write32(SCRL_FD_REG_NBTCFG, scrl_fd_btcfg(t));
    write32(SCRL_FD_REG_DBTCFG, scrl_fd_btcfg(t));

    write8(MCP2518FD_REG_CON + 3, mode);
    if (!wait_mode(mode, MODE_WAIT_MS)) {
        ESP_LOGE(TAG, "mode %u not reached", (unsigned)mode);
        return false;
    }
    return true;
}

// One time base counter sample: two reads that agree, and the µs time after.
static bool sample_tbc(uint32_t *us, uint32_t *ticks) {
    uint32_t t0 = (uint32_t)esp_timer_get_time();
    uint32_t first = read32(MCP2518FD_REG_TBC);
    *ticks = read32(MCP2518FD_REG_TBC);
    *us = (uint32_t)esp_timer_get_time();
    return *us - t0 < 200u && mcp2518fd_tbc_steady(first, *ticks);
}

// SYSCLK from the time base counter over 10 ms. 0 = no clean pair of samples.
static uint32_t measure_sysclk_hz(void) {
    write32(MCP2518FD_REG_TSCON, MCP2518FD_TSCON_TBCEN | (MCP2518FD_TBC_PRESCALE - 1u));
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t t0, c0, t1, c1;
        if (!sample_tbc(&t0, &c0)) continue;
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!sample_tbc(&t1, &c1)) continue;
        return mcp2518fd_sysclk_hz(c1 - c0, t1 - t0);
    }
    return 0;
}

static bool check_crystal(void) {
    if (s_crystal_ok) return true;
    if (s_crystal_wrong) return false;
    // Listen-Only on the undivided clock: SYSCLK is the crystal.
    if (!configure(SCRL_FD_MODE_LISTEN, false)) return false;
    uint32_t hz = measure_sysclk_hz();
    chip_off();
    if (hz == 0u) {
        ESP_LOGE(TAG, "crystal check failed (time base counter)");
        return false;
    }
    if (!mcp2518fd_osc_matches(hz, SCRL_SPI_CRYSTAL_HZ)) {
        s_crystal_wrong = true;
        ESP_LOGE(TAG, "crystal measured %u kHz, build expects %u kHz - left off the bus",
                 (unsigned)(hz / 1000u), (unsigned)(SCRL_SPI_CRYSTAL_HZ / 1000u));
        return false;
    }
    s_crystal_ok = true;
    return true;
}

// ── poll task ─────────────────────────────────────────────────────────────────

static void poll_once(void) {
    ScrlSpiLock lock;
    if (!s_running) return;
    uint32_t now = scrl_millis();

    if (read32(SCRL_FD_REG_TREC) & SCRL_FD_TREC_TXBO) {
        chip_off();
        scrl_can_note_bus_off();
        ESP_LOGW(TAG, "bus-off: chip in Configuration mode");
        return;
    }
    if (s_tx_pending) {
        uint8_t sta = read8(scrl_fd_fifosta(SCRL_FD_TX_FIFO));
        if (sta & SCRL_FD_STA_TX_EMPTY) {
            s_tx_pending = false;
            scrl_can_note_tx_done(true);
        } else if (sta & SCRL_FD_STA_TXATIF) {
            s_tx_pending = false;
            scrl_can_note_tx_done(false);
        }
    }
    if ((uint32_t)(now - s_bdiag_ms) >= BDIAG_PERIOD_MS) {
        s_bdiag_ms = now;
        uint32_t bdiag0 = read32(MCP2518FD_REG_BDIAG0);
        uint32_t errors = mcp2518fd_bdiag0_errors(bdiag0);
        if (errors) {
            scrl_can_note_bus_errors(errors);
            write32(MCP2518FD_REG_BDIAG0, 0);
        }
    }

    for (int i = 0; i < RX_BURST_MAX; i++) {
        uint32_t check_ms = scrl_millis();
        uint8_t sta = read8(scrl_fd_fifosta(SCRL_FD_RX_FIFO));
        if (sta & SCRL_FD_STA_RXOV) {
            scrl_can_note_rx_overflow(1);
            write8(scrl_fd_fifosta(SCRL_FD_RX_FIFO), 0);  // clears RXOVIF; the other bits are read-only
        }
        if ((sta & SCRL_FD_STA_NOT_EMPTY_OR_FULL) == 0u) {
            // Empty now: a frame found later arrived after this moment.
            s_last_empty_ms = check_ms == 0u ? 1u : check_ms;
            return;
        }
        uint16_t addr = (uint16_t)(SCRL_FD_RAM_START + read32(scrl_fd_fifoua(SCRL_FD_RX_FIFO)));
        uint8_t obj[SCRL_FD_OBJ_LEN];
        read_block(addr, obj, sizeof(obj));
        write8(scrl_fd_fifocon(SCRL_FD_RX_FIFO) + 1, SCRL_FD_CON_UINC);
        CanFrame f;
        if (!scrl_fd_rx_frame(obj, &f)) {
            scrl_can_note_rx_rejected();
            continue;
        }
        f.rx_ms = s_last_empty_ms;
        scrl_can_rx_frame(f, nullptr);
    }
}

static void poll_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        poll_once();
    }
}

// ── backend API ───────────────────────────────────────────────────────────────

void scrl_can_hw_init(void) {
    scrl_spi_init(SCRL_PIN_SPI_SCK, SCRL_PIN_SPI_MISO, SCRL_PIN_SPI_MOSI, SCRL_PIN_SPI_CS,
                  SPI_SLOW_HZ);
    s_spi_hz = SPI_SLOW_HZ;
    {
        // An ESP32 reset does not reset the chip: Configuration mode with
        // ABAT, off the bus until the first start.
        ScrlSpiLock lock;
        write8(MCP2518FD_REG_CON + 3, MCP2518FD_CON_REQ_CONFIG);
    }
    BaseType_t ok = xTaskCreate(poll_task, "mcp2518fd", POLL_TASK_STACK, nullptr,
                                POLL_TASK_PRIORITY, &s_task);
    configASSERT(ok == pdPASS);
}

bool scrl_can_hw_start(bool listen_only) {
    if (CONFIG_SCRL_CAN_BITRATE != 500000) {
        ESP_LOGE(TAG, "only 500 kbit/s is supported");
        return false;
    }
    bool ok;
    {
        ScrlSpiLock lock;
        ok = check_crystal() &&
             configure(listen_only ? SCRL_FD_MODE_LISTEN : SCRL_FD_MODE_NORMAL20, OSC_DIVIDE);
        if (ok) {
            uint32_t now = scrl_millis();
            s_last_empty_ms = now == 0u ? 1u : now;
            s_bdiag_ms = now;
            s_tx_pending = false;
            s_running = true;
        } else {
            chip_off();
        }
    }
    if (ok) {
        xTaskNotifyGive(s_task);
        ESP_LOGD(TAG, "started: %s", listen_only ? "Listen-Only" : "Normal CAN 2.0");
    }
    return ok;
}

void scrl_can_hw_stop(void) {
    ScrlSpiLock lock;
    chip_off();
}

bool scrl_can_hw_send(const CanFrame &frame) {
    ScrlSpiLock lock;
    if (!s_running || s_tx_pending) return false;
    if ((read8(scrl_fd_fifosta(SCRL_FD_TX_FIFO)) & SCRL_FD_STA_NOT_EMPTY_OR_FULL) == 0u) {
        return false;  // full
    }
    uint16_t addr = (uint16_t)(SCRL_FD_RAM_START + read32(scrl_fd_fifoua(SCRL_FD_TX_FIFO)));
    uint8_t obj[SCRL_FD_OBJ_LEN];
    scrl_fd_tx_object(frame, obj);
    write_block(addr, obj, sizeof(obj));
    write8(scrl_fd_fifocon(SCRL_FD_TX_FIFO) + 1, SCRL_FD_CON_UINC | SCRL_FD_CON_TXREQ);
    s_tx_pending = true;
    return true;
}

int8_t scrl_can_hw_rx_pin(void) {
    return SCRL_PIN_SPI_CS;
}

void scrl_can_hw_probe_swap_rx(void) {}
