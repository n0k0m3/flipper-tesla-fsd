#ifndef APP_TIMER_H
#define APP_TIMER_H
#include "types.h"

#define APP_TIMER_N 15

typedef void (*app_timer_cb_t)(uint8_t *arg);

typedef struct {
    uint8_t reload;        /* 0 = one shot */
    app_timer_cb_t cb;     /* 0 = free slot */
    uint8_t *arg;          /* TMOS message, freed on stop or restart */
} app_timer_t;

extern app_timer_t g_app_timers[APP_TIMER_N];  /* 0x2000BE28 */
extern uint8_t g_app_timer_task;               /* 0x200040D2, TMOS task id */

uint32_t clock_ms(void);
uint16_t app_timer_task(uint8_t task, uint16_t events);
void app_timer_init(void);
uint32_t app_timer_stop(app_timer_cb_t cb);
uint32_t app_timer_start(app_timer_cb_t cb, uint32_t reload, uint32_t ms, uint8_t *arg);
#endif
