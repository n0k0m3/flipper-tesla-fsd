/*
 * scrl_logbuf.cpp — see scrl_logbuf.h.
 */

#include "scrl_logbuf.h"

#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "scrl_board.h"
#include "scrl_log.h"

static SemaphoreHandle_t s_lock;
static ScrlLogRing s_ring;

void scrl_logbuf_init(void) {
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
}

void scrl_log(const char *fmt, ...) {
    char line[SCRL_LOG_LINE_MAX];
    uint32_t now = scrl_millis();
    int n = snprintf(line, sizeof(line), "%u.%03u ", (unsigned)(now / 1000u),
                     (unsigned)(now % 1000u));
    if (n > 0 && (size_t)n < sizeof(line)) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, args);
        va_end(args);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    scrl_log_ring_add(&s_ring, line);
    xSemaphoreGive(s_lock);
}

uint32_t scrl_log_next_seq(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t seq = s_ring.next_seq;
    xSemaphoreGive(s_lock);
    return seq;
}

uint32_t scrl_log_oldest_seq(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t seq = scrl_log_ring_oldest(&s_ring);
    xSemaphoreGive(s_lock);
    return seq;
}

bool scrl_log_read(uint32_t seq, char *out, size_t size) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = scrl_log_ring_get(&s_ring, seq, out, size);
    xSemaphoreGive(s_lock);
    return ok;
}
