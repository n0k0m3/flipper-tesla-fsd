/*
 * tsl6_stubs.c — the TSL6 hardware that the board does not have: buzzer, LED,
 * RF light link (2.4 GHz to the ambient light receivers), boot flag and chip
 * reset. Also the chip ID (UID14) and the activation state.
 *
 * The stock code declares some of these with different return types in
 * different modules. All of them return a 32-bit value here; a caller that
 * declares void ignores it.
 */
#include <stdint.h>
#include <string.h>

#include "tsl6_port.h"

static void say(const char *line)
{
    if (port_hal && port_hal->log)
        port_hal->log(line);
}

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
uint32_t led_set_hold(uint32_t a) { return a; }

/* ---- Activation: the port is "activated", so every TSL6 command runs. The
   vendor activation commands (AD, AE, AF) and its code are not imported. The
   TSL6 scroll tick stays off by a patch of tick_start. ---- */
uint32_t uid_crc_ok(void) { return 1; }

/* ---- Boot flag (TSL6 boot loader state): status bytes 1-2 report 0 ---- */
uint64_t bootflag_read(void) { return 0; }

/* ---- Chip ID: the board gives 14 bytes (hal->uid14). A0 reports them; A4,
   A5 and F0 check them, as the TSL6 checks its chip UID. ---- */
void uid14_get(uint8_t *buf)
{
    memset(buf, 0, 14);
    if (port_hal && port_hal->uid14)
        port_hal->uid14(buf);
}

uint32_t uid14_check(const uint8_t *buf, uint32_t len)
{
    uint8_t id[14];
    if ((len & 0xFF) != 14)
        return 1;
    uid14_get(id);
    return memcmp(id, buf, 14) != 0;
}

/* ---- Chip reset: A4 (factory reset), A5 (restart) and the car-key factory
   reset reset the TSL6 chip. The board does not reboot for the engine:
   tsl6_poll() starts the engine again from the saved settings. ---- */
uint8_t port_reinit_wanted;

uint32_t system_reset(void)
{
    say("TSL6 reset: engine restarts");
    port_reinit_wanted = 1;
    return 0;
}
