#pragma once
/*
 * scrl_spi.h — the SPI link to an external CAN controller (MCP2515 or
 * MCP2518FD). Mode 0, one device, CS driven by the SPI driver.
 *
 * Two tasks use the controller: the backend poll task and the CAN task (TX).
 * Hold scrl_spi_lock() around every sequence of transfers that must not be
 * split.
 */

#include <stddef.h>
#include <stdint.h>

/** Set up the bus and the device at clock_hz. Call once. */
void scrl_spi_init(int sck, int miso, int mosi, int cs, uint32_t clock_hz);

/** Change the SPI clock (the device is added again). Hold the lock. */
void scrl_spi_set_clock(uint32_t clock_hz);

/** Full-duplex transfer of len bytes in place: buf is sent, then holds what came back. */
void scrl_spi_transfer(uint8_t *buf, size_t len);

void scrl_spi_lock(void);
void scrl_spi_unlock(void);

struct ScrlSpiLock {
    ScrlSpiLock() { scrl_spi_lock(); }
    ~ScrlSpiLock() { scrl_spi_unlock(); }
};
