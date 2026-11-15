#pragma once
/*
 * scrl_board.h — M5Stack NanoH2 pin map and the ms clock.
 *
 * GPIO 8, 9 and 25 are strapping pins. Only GPIO9 has a job here (the button),
 * and only as an input.
 */

#include <stdint.h>
#include "esp_timer.h"
#include "sdkconfig.h"

// Grove port. The direction is a build option (see Kconfig.projbuild).
#if defined(CONFIG_SCRL_CAN_PINS_TX1_RX2)
#define SCRL_PIN_CAN_TX 1
#define SCRL_PIN_CAN_RX 2
#elif defined(CONFIG_SCRL_CAN_PINS_TX2_RX1)
#define SCRL_PIN_CAN_TX 2
#define SCRL_PIN_CAN_RX 1
#else
#define SCRL_PIN_CAN_TX (-1)  // not checked yet: the CAN controller stays off
#define SCRL_PIN_CAN_RX (-1)
#endif

#define SCRL_PIN_GROVE_A 1
#define SCRL_PIN_GROVE_B 2
#define SCRL_PIN_IR_LED 3     // drive low
#define SCRL_PIN_BLUE_LED 4   // drive low
#define SCRL_PIN_BUTTON 9     // active low, boot strap pin
#define SCRL_PIN_RGB_PWR 10   // drive low: the WS2812 has no power

// Milliseconds since boot, from the 64-bit µs timer. The CAN interrupt calls it
// while the flash cache is off, so it must never become a function in flash.
__attribute__((always_inline)) static inline uint32_t scrl_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
