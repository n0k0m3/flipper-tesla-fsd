#pragma once
/*
 * scrl_log.h — ring of the last 64 short event lines, for the BLE Log
 * characteristic. Header-only and free of ESP-IDF, so the host test covers it
 * (test/test_scrl_status.cpp). The ring has no lock: the caller holds one.
 *
 * Each line has a sequence number that counts up from 0. A reader keeps the
 * number of the next line it wants and asks for it until it reaches the end.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCRL_LOG_LINES 64u
#define SCRL_LOG_LINE_MAX 80u  // with the terminator

struct ScrlLogRing {
    char lines[SCRL_LOG_LINES][SCRL_LOG_LINE_MAX];
    uint32_t next_seq;  // number of the next line to write
};

/** Add one line. A line that is too long loses its end. */
static inline void scrl_log_ring_add(ScrlLogRing *ring, const char *text) {
    char *slot = ring->lines[ring->next_seq % SCRL_LOG_LINES];
    strncpy(slot, text, SCRL_LOG_LINE_MAX - 1u);
    slot[SCRL_LOG_LINE_MAX - 1u] = '\0';
    ring->next_seq++;
}

/** Number of the oldest line that the ring still holds. */
static inline uint32_t scrl_log_ring_oldest(const ScrlLogRing *ring) {
    return ring->next_seq > SCRL_LOG_LINES ? ring->next_seq - SCRL_LOG_LINES : 0u;
}

/** Copy line `seq` to out. Returns false when the ring does not hold it. */
static inline bool scrl_log_ring_get(const ScrlLogRing *ring, uint32_t seq, char *out,
                                     size_t size) {
    if (size == 0u || seq >= ring->next_seq || seq < scrl_log_ring_oldest(ring)) return false;
    strncpy(out, ring->lines[seq % SCRL_LOG_LINES], size - 1u);
    out[size - 1u] = '\0';
    return true;
}
