#pragma once
/*
 * scrl_board.h — pin map of each board and the ms clock.
 *
 * The pins are copied from hypery (esp32/platformio.ini and
 * esp32/.firmware/config.h), so one board uses the same wires with both
 * firmwares. The NanoH2 is not a hypery board.
 *
 * Per board:
 *   SCRL_BOARD_NAME         text for the console
 *   SCRL_PIN_BUTTON         active low. SCRL_BUTTON_PULLUP 0 = the board has its own pull-up
 *   SCRL_BOOT_LOW_PINS      outputs driven low at boot (LEDs, display, held-off controller)
 *   SCRL_BOOT_HIGH_PINS     outputs driven high at boot
 *   TWAI backend:           SCRL_PIN_CAN_TX, SCRL_PIN_CAN_RX (-1 = pins not checked)
 *   MCP2515 / MCP2518FD:    SCRL_PIN_SPI_SCK/MISO/MOSI/CS, SCRL_PIN_SPI_INT (-1 = polled),
 *                           SCRL_PIN_SPI_RST (-1 = none), SCRL_SPI_CRYSTAL_HZ
 *   SCRL_PIN_IDLE_INPUTS    pins of the controller that the build does not use: inputs with pull-up
 */

#include <stdint.h>
#include "esp_timer.h"
#include "sdkconfig.h"

#define SCRL_PIN_LIST(...) {__VA_ARGS__}
#define SCRL_BUTTON_PULLUP 1

#if defined(CONFIG_SCRL_BOARD_NANOH2)
  // GPIO 8, 9 and 25 are strapping pins. Only GPIO9 has a job here (the button),
  // and only as an input.
  #define SCRL_BOARD_NAME "M5Stack NanoH2 + Unit Mini CAN"
  #define SCRL_PIN_GROVE_A 1
  #define SCRL_PIN_GROVE_B 2
  #if defined(CONFIG_SCRL_CAN_PINS_TX1_RX2)
    #define SCRL_PIN_CAN_TX 1
    #define SCRL_PIN_CAN_RX 2
  #elif defined(CONFIG_SCRL_CAN_PINS_TX2_RX1)
    #define SCRL_PIN_CAN_TX 2
    #define SCRL_PIN_CAN_RX 1
  #else
    #define SCRL_PIN_CAN_TX (-1)   // not checked yet: Listen-Only pin probe
    #define SCRL_PIN_CAN_RX (-1)
    #define SCRL_CAN_PIN_PROBE 1
  #endif
  #define SCRL_PIN_BUTTON 9                      // boot strap pin: input only
  #define SCRL_BOOT_LOW_PINS SCRL_PIN_LIST(10, 3, 4)  // WS2812 power off, IR LED, blue LED

#elif defined(CONFIG_SCRL_BOARD_M5STACK_ATOM) || defined(CONFIG_SCRL_BOARD_M5STACK_ATOM_MATRIX) || \
      defined(CONFIG_SCRL_BOARD_M5STACK_ATOM_SWAP_PINS)
  // The SK6812 LED (GPIO27) is dark after power-on. The firmware leaves it alone.
  #if defined(CONFIG_SCRL_BOARD_M5STACK_ATOM_MATRIX)
    #define SCRL_BOARD_NAME "M5Stack ATOM Matrix + ATOMIC CAN Base"
    #define SCRL_PIN_BUTTON 39                   // input only, pull-up on the PCB
    #undef SCRL_BUTTON_PULLUP
    #define SCRL_BUTTON_PULLUP 0
  #else
    #define SCRL_BOARD_NAME "M5Stack ATOM Lite + ATOMIC CAN Base"
    #define SCRL_PIN_BUTTON 0                    // hypery's default for the ATOM Lite
  #endif
  #if defined(CONFIG_SCRL_BOARD_M5STACK_ATOM_SWAP_PINS)
    #define SCRL_PIN_CAN_TX 19
    #define SCRL_PIN_CAN_RX 22
  #else
    #define SCRL_PIN_CAN_TX 22
    #define SCRL_PIN_CAN_RX 19
  #endif

#elif defined(CONFIG_SCRL_BOARD_ESP32_GENERIC_TWAI) || defined(CONFIG_SCRL_BOARD_ESP32_GENERIC_TWAI_SNIFFER)
  #define SCRL_BOARD_NAME "Classic ESP32 DevKit + SN65HVD230"
  #define SCRL_PIN_CAN_TX 21
  #define SCRL_PIN_CAN_RX 22
  #define SCRL_PIN_BUTTON 0

#elif defined(CONFIG_SCRL_BOARD_ESP32_MCP2515)
  #define SCRL_BOARD_NAME "Generic ESP32 + MCP2515 module"
  #define SCRL_PIN_SPI_SCK 18
  #define SCRL_PIN_SPI_MISO 19
  #define SCRL_PIN_SPI_MOSI 23
  #define SCRL_PIN_SPI_CS 5
  #define SCRL_PIN_SPI_INT (-1)
  #define SCRL_PIN_SPI_RST (-1)
  #define SCRL_PIN_BUTTON 0

#elif defined(CONFIG_SCRL_BOARD_ESP32_LILYGO)
  #define SCRL_BOARD_NAME "LilyGO T-CAN485"
  #define SCRL_PIN_CAN_TX 27
  #define SCRL_PIN_CAN_RX 26
  #define SCRL_PIN_BUTTON 0
  // GPIO23 = SN65HVD230 Rs: low for TX and RX. GPIO16 = ME2107 boost enable,
  // set high as hypery does. The SK6812 (GPIO4) stays dark.
  #define SCRL_BOOT_LOW_PINS SCRL_PIN_LIST(23)
  #define SCRL_BOOT_HIGH_PINS SCRL_PIN_LIST(16)

#elif defined(CONFIG_SCRL_BOARD_TTGO_TDISPLAY)
  #define SCRL_BOARD_NAME "TTGO T-Display + MCP2515"
  // hypery moves the MCP2515 off the LCD SPI pins. MISO needs a 5 V -> 3.3 V divider.
  #define SCRL_PIN_SPI_SCK 33
  #define SCRL_PIN_SPI_MISO 32
  #define SCRL_PIN_SPI_MOSI 25
  #define SCRL_PIN_SPI_CS 26
  #define SCRL_PIN_SPI_INT (-1)
  #define SCRL_PIN_SPI_RST (-1)
  #define SCRL_PIN_BUTTON 0                      // right button
  #define SCRL_BOOT_LOW_PINS SCRL_PIN_LIST(4, 22)  // LCD backlight off, LCD power off

#elif defined(CONFIG_SCRL_BOARD_LILYGO_T2CAN) || defined(CONFIG_SCRL_BOARD_LILYGO_T2CAN_FD)
  // ESP32-S3-WROOM-1U N16R8. The octal PSRAM takes GPIO 35-37; nothing here uses them.
  #define SCRL_PIN_BUTTON 0                      // BOOT button
  #if defined(CONFIG_SCRL_BOARD_LILYGO_T2CAN_FD)
    #define SCRL_BOARD_NAME "LilyGO T-2CAN FD"
  #else
    #define SCRL_BOARD_NAME "LilyGO T-2CAN"
  #endif
  #if defined(CONFIG_SCRL_T2CAN_BUS_CAN1)
    // can1 on SPI. can0 (TWAI) is not used: its TX pin stays recessive.
    #define SCRL_PIN_SPI_SCK 12
    #define SCRL_PIN_SPI_MISO 13
    #define SCRL_PIN_SPI_MOSI 11
    #define SCRL_PIN_SPI_CS 10
    #define SCRL_PIN_IDLE_INPUTS SCRL_PIN_LIST(7, 6)
    #if defined(CONFIG_SCRL_BOARD_LILYGO_T2CAN_FD)
      // MCP2518FD: no reset pin. GPIO9 goes to INT0/GPIO0/XSTBY and GPIO3 (DIP
      // switch) to INT1/GPIO1. The driver polls over SPI and drives neither.
      #define SCRL_PIN_SPI_INT (-1)
      #define SCRL_PIN_SPI_RST (-1)
      #define SCRL_SPI_CRYSTAL_HZ 40000000u
    #else
      #define SCRL_PIN_SPI_INT 8
      #define SCRL_PIN_SPI_RST 9
      #define SCRL_SPI_CRYSTAL_HZ 16000000u
    #endif
  #else
    // can0 on TWAI. The SPI controller on can1 stays off the bus. The
    // MCP2515 is held in reset (GPIO9 low). The MCP2518FD has no reset pin
    // (GPIO9 is its INT0/XSTBY: never driven), and an ESP32 reset does not
    // reset it, so app_main sends it the Configuration mode request over SPI
    // at boot (SCRL_QUIET_MCP2518FD).
    #define SCRL_PIN_CAN_TX 7
    #define SCRL_PIN_CAN_RX 6
    #if defined(CONFIG_SCRL_BOARD_LILYGO_T2CAN_FD)
      #define SCRL_QUIET_MCP2518FD 1
      #define SCRL_PIN_SPI_SCK 12
      #define SCRL_PIN_SPI_MISO 13
      #define SCRL_PIN_SPI_MOSI 11
      #define SCRL_PIN_SPI_CS 10
    #else
      #define SCRL_BOOT_LOW_PINS SCRL_PIN_LIST(9)
      #define SCRL_BOOT_HIGH_PINS SCRL_PIN_LIST(10)
    #endif
  #endif

#elif defined(CONFIG_SCRL_BOARD_WAVESHARE_S3_CAN)
  #define SCRL_BOARD_NAME "Waveshare ESP32-S3-RS485-CAN"
  #define SCRL_PIN_CAN_TX 15
  #define SCRL_PIN_CAN_RX 16
  #define SCRL_PIN_BUTTON 0
  // The LED on GPIO46 (a strapping pin) is left alone.

#else
  #error "No board selected (Kconfig SCRL_BOARD)"
#endif

#if defined(CONFIG_SCRL_CAN_MCP2515) && !defined(SCRL_SPI_CRYSTAL_HZ)
  #if defined(CONFIG_SCRL_MCP2515_CRYSTAL_16MHZ)
    #define SCRL_SPI_CRYSTAL_HZ 16000000u
  #else
    #define SCRL_SPI_CRYSTAL_HZ 8000000u
  #endif
#endif

#if defined(CONFIG_SCRL_CAN_TWAI) && !defined(SCRL_PIN_CAN_TX)
  #error "TWAI backend without TWAI pins"
#endif
#if (defined(CONFIG_SCRL_CAN_MCP2515) || defined(CONFIG_SCRL_CAN_MCP2518FD)) && !defined(SCRL_PIN_SPI_CS)
  #error "SPI backend without SPI pins"
#endif

// 1 when the build knows its TX path and may go to Active.
#if defined(SCRL_CAN_PIN_PROBE)
  #define SCRL_PIN_CAN_TX_CHECKED 0
#else
  #define SCRL_PIN_CAN_TX_CHECKED 1
#endif

// Name of the CAN controller, for the Info characteristic.
#if defined(CONFIG_SCRL_CAN_MCP2518FD)
  #define SCRL_CAN_KIND "mcp2518fd"
#elif defined(CONFIG_SCRL_CAN_MCP2515)
  #define SCRL_CAN_KIND "mcp2515"
#else
  #define SCRL_CAN_KIND "twai"
#endif

// Milliseconds since boot, from the 64-bit µs timer. The CAN interrupt calls it
// while the flash cache is off, so it must never become a function in flash.
__attribute__((always_inline)) static inline uint32_t scrl_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
