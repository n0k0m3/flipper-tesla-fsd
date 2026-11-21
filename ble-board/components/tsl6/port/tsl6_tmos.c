/*
 * tsl6_tmos.c — the part of the WCH TMOS (BLE OS) API that the TSL6 feature
 * code uses: event tasks, one-shot and reload timers, messages, helpers.
 *
 * A task handler gets its pending events and returns the events it did not
 * handle (TMOS rule). port_tmos_run() calls the handlers until no event is
 * left, then returns the time to the next timer.
 */
#include <stdlib.h>
#include <string.h>

#include "tsl6_port.h"

#define TASK_MAX 12
#define LOOP_MAX 64  /* handler calls per task in one run: stops a handler that never clears its event */

typedef uint32_t (*task_fn_t)(uint32_t task, uint32_t events);

typedef struct {
    task_fn_t fn;
    uint16_t pending;
    uint16_t active;       /* bit = timer runs */
    uint32_t due[16];      /* tick of the next fire */
    uint32_t reload[16];   /* 0 = one-shot */
} task_t;

static task_t s_tasks[TASK_MAX];
static uint8_t s_ntasks;
static uint32_t s_rand = 0x2545F491u;

const tsl6_hal_t *port_hal;

static uint32_t now(void) { return port_hal ? port_hal->now_ticks() : 0u; }

void port_tmos_reset(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    s_ntasks = 0;
}

uint8_t port_TMOS_ProcessEventRegister(void *cb)
{
    if (s_ntasks >= TASK_MAX)
        return 0xFF;
    s_tasks[s_ntasks].fn = (task_fn_t)cb;
    return s_ntasks++;
}

uint32_t port_TMOS_GetSystemClock(void) { return now(); }

uint8_t port_tmos_set_event(uint8_t task, uint16_t ev)
{
    if (task >= s_ntasks)
        return 1;
    s_tasks[task].pending |= ev;
    return 0;
}

uint8_t port_tmos_clear_event(uint8_t task, uint16_t ev)
{
    if (task >= s_ntasks)
        return 1;
    s_tasks[task].pending &= (uint16_t)~ev;
    return 0;
}

static uint8_t start(uint8_t task, uint16_t ev, uint32_t ticks, uint32_t reload)
{
    if (task >= s_ntasks || ev == 0)
        return 1;
    uint32_t t = now();
    for (int i = 0; i < 16; i++) {
        if (!(ev & (1u << i)))
            continue;
        s_tasks[task].due[i] = t + ticks;
        s_tasks[task].reload[i] = reload;
        s_tasks[task].active |= (uint16_t)(1u << i);
    }
    return 0;
}

uint8_t port_tmos_start_task(uint8_t task, uint16_t ev, uint32_t ticks) { return start(task, ev, ticks, 0); }

uint8_t port_tmos_start_reload_task(uint8_t task, uint16_t ev, uint32_t ticks)
{
    return start(task, ev, ticks, ticks ? ticks : 1u);
}

uint8_t port_tmos_stop_task(uint8_t task, uint16_t ev)
{
    if (task >= s_ntasks)
        return 1;
    s_tasks[task].active &= (uint16_t)~ev;
    s_tasks[task].pending &= (uint16_t)~ev;
    return 0;
}

uint32_t port_tmos_get_task_timer(uint8_t task, uint16_t ev)
{
    if (task >= s_ntasks)
        return 0;
    for (int i = 0; i < 16; i++) {
        if ((ev & (1u << i)) && (s_tasks[task].active & (1u << i))) {
            int32_t left = (int32_t)(s_tasks[task].due[i] - now());
            return left > 0 ? (uint32_t)left : 0u;
        }
    }
    return 0;
}

uint8_t *port_tmos_msg_allocate(uint32_t len) { return (uint8_t *)calloc(1, len ? len : 1); }
uint8_t port_tmos_msg_deallocate(uint8_t *msg) { free(msg); return 0; }
/* Messages between tasks are only used by the BLE parts, which are not ported. */
uint8_t port_tmos_msg_send(uint8_t task, uint8_t *msg) { (void)task; free(msg); return 1; }
uint8_t *port_tmos_msg_receive(uint8_t task) { (void)task; return NULL; }

uint32_t port_tmos_rand(void)
{
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return s_rand ^ now();
}

int port_tmos_memcmp(const void *a, const void *b, uint32_t len) { return memcmp(a, b, len) == 0; }

int port_tmos_isbufset(const uint8_t *buf, uint8_t val, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (buf[i] != val)
            return 0;
    return 1;
}

void port_tmos_memset(void *dst, uint8_t val, uint32_t len) { memset(dst, val, len); }
void port_tmos_memcpy(void *dst, const void *src, uint32_t len) { memmove(dst, src, len); }

uint32_t port_resident_trap(uint32_t slot)
{
    if (port_hal && port_hal->log) {
        static const char hex[] = "0123456789abcdef";
        char line[40] = "TSL6 resident slot 0x000 not ported";
        line[21] = hex[(slot >> 8) & 15];
        line[22] = hex[(slot >> 4) & 15];
        line[23] = hex[slot & 15];
        port_hal->log(line);
    }
    return 0;
}

uint32_t port_tmos_run(void)
{
    uint32_t t = now();
    for (uint8_t k = 0; k < s_ntasks; k++) {
        task_t *tk = &s_tasks[k];
        for (int i = 0; i < 16; i++) {
            uint16_t bit = (uint16_t)(1u << i);
            if (!(tk->active & bit) || (int32_t)(t - tk->due[i]) < 0)
                continue;
            tk->pending |= bit;
            if (tk->reload[i]) {
                tk->due[i] += tk->reload[i];
                if ((int32_t)(t - tk->due[i]) >= 0)
                    tk->due[i] = t + tk->reload[i];  /* late: no burst of catch-up events */
            } else {
                tk->active &= (uint16_t)~bit;
            }
        }
        for (int n = 0; tk->pending && tk->fn && n < LOOP_MAX; n++) {
            uint16_t ev = tk->pending;
            tk->pending = 0;
            tk->pending |= (uint16_t)(tk->fn(k, ev) & 0xFFFFu);
        }
    }
    uint32_t next = UINT32_MAX;
    t = now();
    for (uint8_t k = 0; k < s_ntasks; k++) {
        if (s_tasks[k].pending)
            return 0;
        for (int i = 0; i < 16; i++) {
            if (!(s_tasks[k].active & (1u << i)))
                continue;
            int32_t left = (int32_t)(s_tasks[k].due[i] - t);
            uint32_t ms = left <= 0 ? 0u : ((uint32_t)left * 5u + 7u) / 8u;  /* 0.625 ms ticks */
            if (ms < next)
                next = ms;
        }
    }
    return next;
}
