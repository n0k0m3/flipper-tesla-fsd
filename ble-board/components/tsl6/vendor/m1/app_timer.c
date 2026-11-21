/* Software timers on one TMOS task (stock 0x9096..0x9236).
   Each of the 15 timers owns one event bit of the task. */
#include "tsl6.h"
#include "app_timer.h"

/* Stock 0x81AC. TMOS ticks are 0.625 ms. */
uint32_t clock_ms(void)
{
    return TMOS_GetSystemClock() * 10 >> 4;
}

/* Stock 0x9096. Returns the tmos_stop_task status, or 1 when cb has no timer. */
uint32_t app_timer_stop(app_timer_cb_t cb)
{
    for (uint32_t i = 0; i < APP_TIMER_N; i++) {
        app_timer_t *t = &g_app_timers[i];
        if (t->cb != cb)
            continue;
        if (t->arg)
            tmos_msg_deallocate(t->arg);
        t->cb = 0;
        t->arg = 0;
        return tmos_stop_task(g_app_timer_task, (uint16_t)(1u << i));
    }
    return 1;
}

/* Stock 0x910A. ms = 0 runs cb at once (in a free slot) when a slot is free.
   Otherwise the timer of cb restarts, or a free slot takes it.
   reload != 0 makes the timer repeat. Returns 1 when no slot is free. */
uint32_t app_timer_start(app_timer_cb_t cb, uint32_t reload, uint32_t ms, uint8_t *arg)
{
    uint32_t i;

    if (ms == 0) {
        for (i = 0; i < APP_TIMER_N; i++) {
            app_timer_t *t = &g_app_timers[i];
            if (t->cb)
                continue;
            t->cb = cb;
            t->reload = 0;
            t->arg = arg;
            return tmos_set_event(g_app_timer_task, (uint16_t)(1u << i));
        }
    }
    for (i = 0; i < APP_TIMER_N; i++) {
        app_timer_t *t = &g_app_timers[i];
        if (t->cb != cb)
            continue;
        if (t->arg)
            tmos_msg_deallocate(t->arg);
        t->reload = (uint8_t)reload;
        t->arg = arg;
        goto start;
    }
    for (i = 0; i < APP_TIMER_N; i++) {
        app_timer_t *t = &g_app_timers[i];
        if (t->cb)
            continue;
        t->cb = cb;
        t->reload = (uint8_t)reload;
        t->arg = arg;
        goto start;
    }
    return 1;

start:;
    uint32_t ticks = ms * 1000 / 625;
    if (reload == 0)
        return tmos_start_task(g_app_timer_task, (uint16_t)(1u << i), ticks) == 0;
    return tmos_start_reload_task(g_app_timer_task, (uint16_t)(1u << i), ticks);
}

/* Stock 0x9000. TMOS task of the app timers. Runs the callback of the lowest
   event bit (0..14). A one-shot timer frees its slot before the call. */
uint16_t app_timer_task(uint8_t task, uint16_t events)
{
    for (uint32_t i = 0; i < APP_TIMER_N; i++) {
        app_timer_t *t;
        if (((events >> i) & 1) == 0)
            continue;
        t = &g_app_timers[i];
        if (t->cb) {
            app_timer_cb_t cb = t->cb;
            uint8_t *arg = t->arg;
            if (t->reload == 0) {
                t->cb = 0;
                t->arg = 0;
            }
            cb(arg);
        }
        return (uint16_t)((1u << i) ^ events);
    }
    return 0;
}

/* Stock 0x9060. */
void app_timer_init(void)
{
    tmos_memset(g_app_timers, 0, sizeof(g_app_timers));
    g_app_timer_task = TMOS_ProcessEventRegister((void *)app_timer_task);
}
