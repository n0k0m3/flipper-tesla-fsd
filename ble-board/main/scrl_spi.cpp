/*
 * scrl_spi.cpp — see scrl_spi.h.
 */

#include "scrl_spi.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "scrl_spi";

// The longest transfer: an MCP2518FD RAM word block (2 command bytes + 16).
// Polling transfers below this size need no DMA.
#define SCRL_SPI_MAX_TRANSFER 64

static spi_device_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static int s_cs = -1;

static void add_device(uint32_t clock_hz) {
    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = (int)clock_hz;
    dev.spics_io_num = s_cs;
    dev.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_dev));
}

void scrl_spi_init(int sck, int miso, int mosi, int cs, uint32_t clock_hz) {
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    spi_bus_config_t bus = {};
    bus.sclk_io_num = sck;
    bus.miso_io_num = miso;
    bus.mosi_io_num = mosi;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = SCRL_SPI_MAX_TRANSFER;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));
    s_cs = cs;
    add_device(clock_hz);
    ESP_LOGD(TAG, "SPI2: SCK %d MISO %d MOSI %d CS %d, %u Hz", sck, miso, mosi, cs,
             (unsigned)clock_hz);
}

void scrl_spi_set_clock(uint32_t clock_hz) {
    ESP_ERROR_CHECK(spi_bus_remove_device(s_dev));
    add_device(clock_hz);
}

void scrl_spi_transfer(uint8_t *buf, size_t len) {
    configASSERT(len <= SCRL_SPI_MAX_TRANSFER);
    spi_transaction_t t = {};
    t.length = len * 8u;
    t.tx_buffer = buf;
    t.rx_buffer = buf;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_dev, &t));
}

void scrl_spi_lock(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void scrl_spi_unlock(void) {
    xSemaphoreGive(s_lock);
}
