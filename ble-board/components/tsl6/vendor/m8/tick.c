/* Scroll tick timer (stock 0x163C2..0x1665E).
   One TMOS task with three events:
   - event 1: the scroll tick (reschedule, then scroll_tick),
   - event 2: the auto-off timer (config byte 7 bits 2:0, in 10 min steps),
   - event 4: a gauge update 50 ms after each schedule.
   Many stock functions here are "void", but they end in a tail call, so a0
   holds the callee's result. The rewrite returns that value too. */
#include "tsl6.h"
#include "m8.h"

#define TICK_EV_TICK 1
#define TICK_EV_OFF  2
#define TICK_EV_GAUGE 4

/* Stock 0x163C2. Milliseconds until the next tick, plus 50. */
uint32_t tick_ms_left(void)
{
    return (tmos_get_task_timer(g_tick_task, TICK_EV_TICK) * 10 >> 4) + 50;
}

/* Stock 0x163E6. Starts event 1 after the interval of the "time" setting
   (config byte 3 bits 3:0; 15 counts as 1):
   0-1: 2000 ms, 2: 3000 ms, t >= 3: (t - 2) * 5000 ms.
   With "rand" on (config byte 4 bit 0) it adds rand % (1001, 2001, 5001) ms.
   Nothing happens when the tick is off. With time 15 on a ready channel 1,
   nothing happens either, unless config byte 8 bit 4 is set and byte
   0x200040D8 is not 0. */
uint32_t tick_schedule(void)
{
    uint32_t r, t, ms, extra;

    r = tick_is_on();
    if (r == 0)
        return 0;
    if ((g_cfg[3] & 0xf) == 0xf) {
        r = bus_alive(1);
        if (r != 0) {
            /* Stock returns the result of the last call in a0. */
            if ((g_cfg[8] & 0x10) == 0)
                return r;
            if (feature_d8_get() == 0)
                return 0;
        }
    }
    t = g_cfg[3] & 0xf;
    if (t == 0xf)
        t = 1;
    r = tmos_rand();
    if (t <= 1) {
        extra = r % 1001;
        ms = 2000;
    } else if (t == 2) {
        extra = r % 2001;
        ms = 3000;
    } else {
        ms = (t - 2) * 5000;
        extra = r % 5001;
    }
    if (g_cfg[4] & 1)
        ms += extra;
    tmos_start_task(g_tick_task, TICK_EV_TICK, ms * 1000 / 625);
    return tmos_start_task(g_tick_task, TICK_EV_GAUGE, 80);
}

/* Stock 0x164C0. Runs a tick now when the tick is on. */
uint32_t tick_now(void)
{
    if (tick_is_on() == 0)
        return 0;
    return tmos_set_event(g_tick_task, TICK_EV_TICK);
}

/* Stock 0x164E4. Turns the tick on or off. "on" needs byte 0x200040D3.
   notify != 0 also calls 0x9774(on). Off stops both timers and queues a
   gauge update. On starts the auto-off timer when config byte 7 bits 2:0
   are set (n * 10 min). */
uint32_t tick_start(uint32_t on, uint32_t notify)
{
    uint32_t r, n;

    if (on != 0) {
        return 0; /* port: the board's Scroll Ack sends the ticks */
    }
    g_tick_on = (uint8_t)on;
    r = tick_schedule();
    if (notify != 0)
        r = buzzer_beep_onoff(on);
    if (on == 0) {
        tmos_stop_task(g_tick_task, TICK_EV_OFF);
        tmos_stop_task(g_tick_task, TICK_EV_TICK);
        app_timer_start((void *)status_push, 0, 20, 0);
        return active_off(0);
    }
    n = g_cfg[7] & 7;
    if (n == 0)
        return r;   /* stock leaves the last call's result in a0 */
    /* 960000 ticks of 0.625 ms = 10 min. */
    return tmos_start_task(g_tick_task, TICK_EV_OFF, n * 960000);
}

/* Stock 0x1657E. TMOS event handler of the tick task.
   Event 1: reschedule, then one scroll tick. The tick gap jg is config
   byte 8 bits 3:0. With time 15 on platform 1 it is at most 3.
   Event 2: auto-off. It turns the tick off, unless config byte 4 bit 4 is
   set and AP is active.
   Event 4: gauge update. */
uint32_t tick_event(uint32_t task, uint32_t events)
{
    if (events & TICK_EV_TICK) {
        uint32_t jg;

        tick_schedule();
        jg = g_cfg[8] & 0xf;
        if ((g_cfg[3] & 0xf) == 0xf && platform_get() == 1 && jg > 3)
            jg = 3;
        scroll_tick((uint8_t)jg);
        return events ^ TICK_EV_TICK;
    }
    if (events & TICK_EV_OFF) {
        if ((g_cfg[4] & 0x10) == 0 || ap_active() == 0)
            tick_start(0, 1);
        return events ^ TICK_EV_OFF;
    }
    if (events & TICK_EV_GAUGE) {
        status_push(0);
        return events ^ TICK_EV_GAUGE;
    }
    return 0;
}

/* Stock 0x16612. Clears g_tick_on, registers the task, and turns the tick
   on when config byte 6 bit 0 is set. */
uint32_t tick_init(void)
{
    uint8_t id;

    tmos_memset(&g_tick_on, 0, 1);
    id = TMOS_ProcessEventRegister((void *)tick_event);
    g_tick_task = id;
    if ((g_cfg[6] & 1) == 0)
        return id;
    return tick_start(1, 0);
}
