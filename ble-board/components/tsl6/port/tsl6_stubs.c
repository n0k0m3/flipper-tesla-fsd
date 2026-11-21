/*
 * tsl6_stubs.c — the TSL6 parts that the board does not have: the TSL6 BLE
 * link, buzzer, LED, RF light link, CAN filters, UID check and chip reset.
 *
 * The stock code declares some of these with different return types in
 * different modules. All of them return a 32-bit value here; a caller that
 * declares void ignores it.
 */
#include <stdint.h>

#include "tsl6_port.h"

static void say(const char *line)
{
    if (port_hal && port_hal->log)
        port_hal->log(line);
}

/* ---- BLE: the board has its own BLE service; the TSL6 replies go nowhere ---- */
uint32_t adv_restart(uint32_t a, uint32_t b) { (void)a; (void)b; return 0; }
uint32_t ble_reply_bd(uint32_t conn, uint32_t v) { (void)conn; (void)v; return 0; }
uint32_t ble_send_any(uint32_t conn, uint32_t cmd, void *data, uint32_t len)
{
    (void)conn; (void)cmd; (void)data; (void)len;
    return 0;
}
uint32_t status_push(uint32_t a) { (void)a; return 0; }
uint32_t gauge_push(uint32_t a) { (void)a; return 0; }
/* 0x401 frames go to the second TSL6 link (CAN repeater); the board has none. */
uint32_t link_forward_401(uint8_t *frame) { (void)frame; return 0; }

/* ---- RF light link (ambient light receivers): not on the board ---- */
uint32_t rf_send(uint32_t id, uint32_t chan, uint32_t type, void *data, uint32_t len, void *done)
{
    (void)id; (void)chan; (void)type; (void)data; (void)len; (void)done;
    return 1;  /* busy: the light queue keeps its records */
}

/* ---- Buzzer and LED: not on the board ---- */
uint32_t buzzer_beep_onoff(uint32_t on) { (void)on; return 0; }
uint32_t buzzer_beeps(uint32_t n) { (void)n; return 0; }
/* Play a melody, then call done. The board has no buzzer: call done now. */
uint32_t buzzer_play(void *tune, void (*done)(void), uint32_t ms)
{
    (void)tune; (void)ms;
    if (done)
        done();
    return 0;
}
uint32_t tune_up(uint32_t a) { (void)a; return 0; }
uint32_t tune_down(uint32_t a) { (void)a; return 0; }
uint32_t tune_scale(uint32_t a) { (void)a; return 0; }
uint32_t tune_three(uint32_t a) { (void)a; return 0; }
uint32_t tune_two_low(uint32_t a) { (void)a; return 0; }
uint32_t tune_falling3(uint32_t a) { (void)a; return 0; }
uint32_t tune_wobble(uint32_t a) { (void)a; return 0; }
uint32_t led_on(uint32_t a) { return a; }
uint32_t led_off(uint32_t a) { return a; }
uint32_t led_toggle(uint32_t a) { return a; }

/* ---- CAN filters: the board reads every frame and gives all of them to the engine ---- */
uint32_t can_filter_platform(uint32_t platform) { (void)platform; return 0; }

/* ---- UID check: 0 keeps the TSL6 scroll tick off. The board has its own
   Scroll Ack, and two tick sources on the bus must not happen. ---- */
uint32_t uid_crc_ok(void) { return 0; }

/* ---- Chip reset: the stock code resets after a factory reset by the car
   keys. The board does not reboot for the engine; tsl6_poll() starts the
   engine again from the saved settings. ---- */
uint8_t port_reinit_wanted;

uint32_t system_reset(void)
{
    say("TSL6 reset: engine restarts");
    port_reinit_wanted = 1;
    return 0;
}
