/*
 * scrl_can_mcp2515.cpp — MCP2515 backend on SPI. See scrl_can_hw.h.
 *
 * One poll task reads the chip: every 1 ms, or at once when the INT pin falls
 * (boards with an INT pin). The CAN task loads TX buffer 0 under the same SPI
 * lock. The six IDs of scrl_rx_ids.h are exact hardware filters: with two
 * receive buffers, the chip cannot hold the full Vehicle CAN rate for 1 ms.
 *
 * Bus-off: the MCP2515 recovers by itself and would then send what still
 * waits in TXB0. On TXBO the task aborts TX and puts the chip in
 * Configuration mode at once.
 */

#include "scrl_can_hw.h"

#include <atomic>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "scrl_board.h"
#include "scrl_mcp2515.h"
#include "scrl_rx_ids.h"
#include "scrl_spi.h"

static const char *TAG = "scrl_mcp2515";

#define SPI_CLOCK_HZ 8000000u
#define POLL_TASK_STACK 3072
#define POLL_TASK_PRIORITY (configMAX_PRIORITIES - 3)
#define MODE_WAIT_MS 10u
// Frames read in one poll before the task looks at the error flags again.
#define RX_BURST_MAX 8

static TaskHandle_t s_task;
static bool s_running;           // the chip is out of Configuration mode (SPI lock)
static bool s_tx_busy;           // TXB0 holds a frame (SPI lock)
static uint32_t s_last_empty_ms; // last poll that found no frame (poll task)

// ── SPI access (hold the lock) ────────────────────────────────────────────────

static void reg_write(uint8_t addr, const uint8_t *data, uint8_t len) {
    uint8_t b[2 + 16];
    b[0] = MCP2515_INSTR_WRITE;
    b[1] = addr;
    memcpy(&b[2], data, len);
    scrl_spi_transfer(b, 2u + len);
}

static void reg_write1(uint8_t addr, uint8_t value) {
    reg_write(addr, &value, 1);
}

static void reg_read(uint8_t addr, uint8_t *out, uint8_t len) {
    uint8_t b[2 + 16] = {};
    b[0] = MCP2515_INSTR_READ;
    b[1] = addr;
    scrl_spi_transfer(b, 2u + len);
    memcpy(out, &b[2], len);
}

static void bit_modify(uint8_t addr, uint8_t mask, uint8_t value) {
    uint8_t b[4] = {MCP2515_INSTR_BITMOD, addr, mask, value};
    scrl_spi_transfer(b, sizeof(b));
}

static bool set_mode(uint8_t mode, bool abort_tx) {
    bit_modify(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP | MCP2515_CANCTRL_ABAT | MCP2515_CANCTRL_OSM,
               (uint8_t)(mode | (abort_tx ? MCP2515_CANCTRL_ABAT : 0u)));
    uint32_t t0 = scrl_millis();
    do {
        uint8_t stat;
        reg_read(MCP2515_CANSTAT, &stat, 1);
        if ((stat & MCP2515_CANSTAT_OPMOD) == mode) return true;
    } while ((uint32_t)(scrl_millis() - t0) <= MODE_WAIT_MS);
    return false;
}

// Off the bus: Configuration mode with every pending TX aborted, and the
// reset pin held low where the board has one.
static void chip_off(void) {
    set_mode(MCP2515_MODE_CONFIG, true);
#if SCRL_PIN_SPI_RST >= 0
    gpio_set_level((gpio_num_t)SCRL_PIN_SPI_RST, 0);
#endif
    s_running = false;
    if (s_tx_busy) {
        s_tx_busy = false;
        scrl_can_note_tx_done(false);
    }
}

// ── poll task ─────────────────────────────────────────────────────────────────

static void read_rx(uint8_t instr, uint32_t stamp) {
    uint8_t b[1 + MCP2515_BUF_LEN] = {};
    b[0] = instr;
    scrl_spi_transfer(b, sizeof(b));  // CS high clears RXnIF
    CanFrame f;
    if (!mcp2515_rx_frame(&b[1], &f)) {
        scrl_can_note_rx_rejected();
        return;
    }
    f.rx_ms = stamp;
    scrl_can_rx_frame(f, nullptr);
}

static void poll_once(void) {
    ScrlSpiLock lock;
    if (!s_running) return;
    for (int i = 0; i < RX_BURST_MAX; i++) {
        uint32_t check_ms = scrl_millis();
        uint8_t fl[2];  // CANINTF, EFLG
        reg_read(MCP2515_CANINTF, fl, 2);
        uint8_t intf = fl[0];
        uint8_t eflg = fl[1];

        if (eflg & MCP2515_EFLG_TXBO) {
            chip_off();
            scrl_can_note_bus_off();
            ESP_LOGW(TAG, "bus-off: chip in Configuration mode");
            return;
        }
        if (intf & MCP2515_INT_TX0) {
            bit_modify(MCP2515_CANINTF, MCP2515_INT_TX0, 0);
            if (s_tx_busy) {
                s_tx_busy = false;
                scrl_can_note_tx_done(true);
            }
        }
        if (eflg & (MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR)) {
            uint32_t lost = ((eflg & MCP2515_EFLG_RX0OVR) ? 1u : 0u) +
                            ((eflg & MCP2515_EFLG_RX1OVR) ? 1u : 0u);
            scrl_can_note_rx_overflow(lost);
            bit_modify(MCP2515_EFLG, MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR, 0);
        }
        if (intf & (MCP2515_INT_ERR | MCP2515_INT_MERR)) {
            if (intf & MCP2515_INT_MERR) scrl_can_note_bus_errors(1);
            bit_modify(MCP2515_CANINTF, MCP2515_INT_ERR | MCP2515_INT_MERR, 0);
        }

        if ((intf & (MCP2515_INT_RX0 | MCP2515_INT_RX1)) == 0u) {
            // Empty now: a frame found later arrived after this moment.
            s_last_empty_ms = check_ms == 0u ? 1u : check_ms;
            return;
        }
        // RXB0 fills first and rolls over into RXB1, so RXB0 holds the older frame.
        if (intf & MCP2515_INT_RX0) read_rx(MCP2515_INSTR_READ_RX0, s_last_empty_ms);
        if (intf & MCP2515_INT_RX1) read_rx(MCP2515_INSTR_READ_RX1, s_last_empty_ms);
    }
}

static void poll_task(void *) {
    for (;;) {
        // The INT pin or a start wakes the task early.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        poll_once();
    }
}

#if SCRL_PIN_SPI_INT >= 0
static void IRAM_ATTR on_int(void *) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken) portYIELD_FROM_ISR();
}
#endif

// ── backend API ───────────────────────────────────────────────────────────────

void scrl_can_hw_init(void) {
#if SCRL_PIN_SPI_RST >= 0
    // Held in reset until a start: off the bus.
    gpio_reset_pin((gpio_num_t)SCRL_PIN_SPI_RST);
    gpio_set_level((gpio_num_t)SCRL_PIN_SPI_RST, 0);
    gpio_set_direction((gpio_num_t)SCRL_PIN_SPI_RST, GPIO_MODE_OUTPUT);
#endif
    scrl_spi_init(SCRL_PIN_SPI_SCK, SCRL_PIN_SPI_MISO, SCRL_PIN_SPI_MOSI, SCRL_PIN_SPI_CS,
                  SPI_CLOCK_HZ);
    {
        // An ESP32 reset does not reset the chip. A board with no reset pin
        // gets the SPI reset here: Configuration mode, TX buffers cleared,
        // off the bus until the first start.
        ScrlSpiLock lock;
        uint8_t reset = MCP2515_INSTR_RESET;
        scrl_spi_transfer(&reset, 1);
    }
    BaseType_t ok = xTaskCreate(poll_task, "mcp2515", POLL_TASK_STACK, nullptr,
                                POLL_TASK_PRIORITY, &s_task);
    configASSERT(ok == pdPASS);
#if SCRL_PIN_SPI_INT >= 0
    gpio_reset_pin((gpio_num_t)SCRL_PIN_SPI_INT);
    gpio_set_direction((gpio_num_t)SCRL_PIN_SPI_INT, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)SCRL_PIN_SPI_INT, GPIO_PULLUP_ONLY);
    gpio_set_intr_type((gpio_num_t)SCRL_PIN_SPI_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)SCRL_PIN_SPI_INT, on_int, nullptr);
#endif
}

bool scrl_can_hw_start(bool listen_only) {
    uint8_t cnf[3];
    if (!mcp2515_cnf_500k(SCRL_SPI_CRYSTAL_HZ, cnf) || CONFIG_SCRL_CAN_BITRATE != 500000) {
        ESP_LOGE(TAG, "no bit timing for this crystal and bit rate");
        return false;
    }
    bool ok;
    {
        ScrlSpiLock lock;
#if SCRL_PIN_SPI_RST >= 0
        gpio_set_level((gpio_num_t)SCRL_PIN_SPI_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(2));
#endif
        uint8_t reset = MCP2515_INSTR_RESET;
        scrl_spi_transfer(&reset, 1);
        vTaskDelay(pdMS_TO_TICKS(10));  // oscillator start-up, as autowp waits

        // An MCP2515 right after its reset: OPMOD Configuration, and CANCTRL
        // with ABAT clear, CLKEN set, CLKPRE 1/8 (the check of hypery's
        // mcp2515_reset_signature()). A missing chip reads 0x00 or 0xFF.
        uint8_t st[2];
        reg_read(MCP2515_CANSTAT, st, 2);
        ok = (st[0] & 0xE0u) == 0x80u && (st[1] & 0x17u) == 0x07u;
        if (!ok) {
            ESP_LOGE(TAG, "not detected on SPI (CANSTAT 0x%02X CANCTRL 0x%02X)", st[0], st[1]);
        }
        if (ok) {
            reg_write(MCP2515_CNF3, cnf, 3);
            uint8_t zeros[14] = {};
            reg_write(MCP2515_TXB0CTRL, zeros, sizeof(zeros));
            reg_write(MCP2515_TXB1CTRL, zeros, sizeof(zeros));
            reg_write(MCP2515_TXB2CTRL, zeros, sizeof(zeros));
            // Filters on (RXM = 00), standard IDs only, exact match.
            reg_write1(MCP2515_RXB0CTRL, MCP2515_RXB0CTRL_BUKT);
            reg_write1(MCP2515_RXB1CTRL, 0);
            uint8_t b[4];
            mcp2515_std_mask(b);
            reg_write(MCP2515_RXM0SIDH, b, 4);
            reg_write(MCP2515_RXM1SIDH, b, 4);
            static const uint8_t filter_reg[SCRL_RX_ID_COUNT] = {
                MCP2515_RXF0SIDH, MCP2515_RXF1SIDH, MCP2515_RXF2SIDH,
                MCP2515_RXF3SIDH, MCP2515_RXF4SIDH, MCP2515_RXF5SIDH};
            for (unsigned i = 0; i < SCRL_RX_ID_COUNT; i++) {
                mcp2515_std_id(SCRL_RX_IDS[i], b);
                reg_write(filter_reg[i], b, 4);
            }
            reg_write1(MCP2515_CANINTE, MCP2515_INT_RX0 | MCP2515_INT_RX1 | MCP2515_INT_TX0 |
                                            MCP2515_INT_ERR | MCP2515_INT_MERR);
            uint8_t clear[2] = {0, 0};
            reg_write(MCP2515_CANINTF, clear, 2);  // CANINTF and EFLG

            uint8_t back[3];
            reg_read(MCP2515_CNF3, back, 3);
            ok = memcmp(back, cnf, 3) == 0;
            if (!ok) ESP_LOGE(TAG, "bit timing read-back failed");
        }
        if (ok) {
            ok = set_mode(listen_only ? MCP2515_MODE_LISTEN : MCP2515_MODE_NORMAL, false);
            if (!ok) ESP_LOGE(TAG, "mode change failed");
        }
        if (ok) {
            uint32_t now = scrl_millis();
            s_last_empty_ms = now == 0u ? 1u : now;
            s_tx_busy = false;
            s_running = true;
        } else {
            chip_off();
        }
    }
    if (ok) {
        xTaskNotifyGive(s_task);
        ESP_LOGD(TAG, "started: %s, crystal %u Hz", listen_only ? "Listen-Only" : "Normal",
                 (unsigned)SCRL_SPI_CRYSTAL_HZ);
    }
    return ok;
}

void scrl_can_hw_stop(void) {
    ScrlSpiLock lock;
    chip_off();
}

bool scrl_can_hw_send(const CanFrame &frame) {
    ScrlSpiLock lock;
    if (!s_running || s_tx_busy) return false;
    uint8_t ctrl;
    reg_read(MCP2515_TXB0CTRL, &ctrl, 1);
    if (ctrl & MCP2515_TXB_TXREQ) return false;
    uint8_t b[1 + MCP2515_BUF_LEN];
    b[0] = MCP2515_INSTR_LOAD_TX0;
    uint8_t n = mcp2515_tx_bytes(frame, &b[1]);
    scrl_spi_transfer(b, 1u + n);
    uint8_t rts = MCP2515_INSTR_RTS_TX0;
    scrl_spi_transfer(&rts, 1);
    s_tx_busy = true;
    return true;
}

int8_t scrl_can_hw_rx_pin(void) {
    return SCRL_PIN_SPI_CS;
}

void scrl_can_hw_probe_swap_rx(void) {}
