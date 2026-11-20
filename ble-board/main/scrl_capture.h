#pragma once
/*
 * scrl_capture.h — the capture core: an ID table of the bus and a stream of
 * received frames for the phone. Header-only and free of ESP-IDF, so the host
 * test covers it (test/test_scrl_capture.cpp). scrl_capture.cpp holds the one
 * instance and the BLE side.
 *
 * A capture never sends a frame: while it runs, the board holds the controller
 * in Listen-Only (SCRL_INHIBIT_CAPTURE).
 *
 * THREADS
 *   - The CAN task is the only writer of the ring head and of the ID table
 *     (scrl_cap_feed).
 *   - The sender task is the only reader of the ring (scrl_cap_pack_frames)
 *     and the only writer of the ring tail.
 *   - The ID table is read without a lock. A table packet can hold a count
 *     and data from two different frames of one ID. The stream is exact.
 *
 * PACKETS (one BLE notification each, little-endian, at most the MTU - 3 bytes)
 *   header, 8 bytes: type u8, count u8, seq u16, ring_lost u32
 *     seq        +1 for each packet that went out. A gap in seq = lost packets.
 *     ring_lost  stream records lost because the ring was full, since start.
 *   type 1 FRAMES  count records of 16 bytes:
 *     t_ms u32, id u16, dlc u8, flags u8 (bit 0: records were lost just
 *     before this one), data[8]
 *   type 2 IDS     count entries of 24 bytes (the whole table every 5 s, and
 *                  once more after the stop, before the last STATE):
 *     id u16, dlc u8, 0 u8, frames u32, first_ms u32, last_ms u32, data[8]
 *   type 3 MARK    one record: t_ms u32, step u8, 0 u8 x3
 *   type 4 STATE   one record of 20 bytes:
 *     t_ms u32, running u8, mode u8, ids u16 (ID table entries; 0.4.0 sent
 *     u8 and 0, so the same read works),
 *     rx_all u32 (frames that the capture saw), rx_queue_lost u32 (frames that
 *     the CAN task queue lost since power-on), ext u32 (extended-ID frames,
 *     counted only)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

#define SCRL_CAP_HDR_LEN      8u
#define SCRL_CAP_REC_LEN      16u
#define SCRL_CAP_ID_REC_LEN   24u
#define SCRL_CAP_MARK_LEN     8u
#define SCRL_CAP_STATE_LEN    20u
#define SCRL_CAP_RING         2048u  // records, a power of 2: 32 KB, about 4 s at 500 frames/s
#define SCRL_CAP_MAX_IDS      511u   // ID table entries (index 0xFFFF = none). A Model 3/Y Vehicle CAN has more than 255 IDs
#define SCRL_CAP_LIST_MAX     64u    // IDs in a start command with an ID list
#define SCRL_CAP_STD_IDS      2048u

enum ScrlCapType : uint8_t {
    SCRL_CAP_FRAMES = 1,
    SCRL_CAP_IDS = 2,
    SCRL_CAP_MARK = 3,
    SCRL_CAP_STATE = 4,
};

// Start modes (byte 1 of SCRL_CMD_CAPTURE_START).
enum ScrlCapMode : uint8_t {
    SCRL_CAP_MODE_PLAN = 0,  // the IDs of SCRL_CAP_PLAN_IDS
    SCRL_CAP_MODE_ALL = 1,   // every standard ID
    SCRL_CAP_MODE_LIST = 2,  // the IDs in the command: u16 each, little-endian
};

// The input IDs of the TSL6 feature port (plan phase 2), and the frames that
// show the car variant. The ID table always covers every ID.
static const uint16_t SCRL_CAP_PLAN_IDS[] = {
    0x102, 0x103, 0x118, 0x129, 0x229, 0x243, 0x249, 0x257, 0x25D, 0x273, 0x2F3, 0x333, 0x334, 0x339,
    0x370, 0x39B, 0x39D, 0x3B3, 0x3C2, 0x3C3, 0x3E9, 0x3F5, 0x4E2, 0x4E3, 0x4F3, 0x678, 0x7FF,
};
#define SCRL_CAP_PLAN_COUNT (sizeof(SCRL_CAP_PLAN_IDS) / sizeof(SCRL_CAP_PLAN_IDS[0]))

struct ScrlCapIdEntry {
    uint16_t id;
    uint8_t dlc;
    uint32_t frames;
    uint32_t first_ms;
    uint32_t last_ms;
    uint8_t data[8];
};

struct ScrlCapture {
    std::atomic<bool> running;
    uint8_t mode;
    uint8_t stream[SCRL_CAP_STD_IDS / 8u];  // bit set = stream this ID
    uint16_t index[SCRL_CAP_STD_IDS];       // ID -> table entry, 0xFFFF = none
    ScrlCapIdEntry ids[SCRL_CAP_MAX_IDS];
    std::atomic<uint32_t> id_count;
    uint8_t ring[SCRL_CAP_RING][SCRL_CAP_REC_LEN];
    std::atomic<uint32_t> head;       // next record to write (CAN task)
    std::atomic<uint32_t> tail;       // next record to send (sender task)
    std::atomic<uint32_t> ring_lost;  // records lost on a full ring
    bool lost_mark;                   // flag the next record (CAN task)
    std::atomic<uint32_t> rx_all;
    std::atomic<uint32_t> ext;
    uint16_t seq;                     // sender task
};

static inline void scrl_cap_put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void scrl_cap_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static inline void scrl_cap_stream_set(ScrlCapture *c, uint16_t id) {
    if (id < SCRL_CAP_STD_IDS) c->stream[id >> 3] |= (uint8_t)(1u << (id & 7u));
}

static inline bool scrl_cap_streams(const ScrlCapture *c, uint16_t id) {
    return id < SCRL_CAP_STD_IDS && (c->stream[id >> 3] & (1u << (id & 7u))) != 0u;
}

/**
 * Clear the state and start. Call while the capture is stopped (no feed and no
 * pack runs). ids: the u16 list for SCRL_CAP_MODE_LIST, little-endian.
 * Returns false (and stays stopped) for a bad mode or list.
 */
static inline bool scrl_cap_start(ScrlCapture *c, uint8_t mode, const uint8_t *ids, size_t ids_len) {
    if (mode > SCRL_CAP_MODE_LIST) return false;
    if (mode == SCRL_CAP_MODE_LIST) {
        if (ids_len == 0u || (ids_len & 1u) != 0u || ids_len / 2u > SCRL_CAP_LIST_MAX) return false;
        for (size_t i = 0; i < ids_len; i += 2u) {
            if ((uint32_t)(ids[i] | (ids[i + 1u] << 8)) >= SCRL_CAP_STD_IDS) return false;
        }
    } else if (ids_len != 0u) {
        return false;
    }
    c->running.store(false);
    c->mode = mode;
    memset(c->stream, mode == SCRL_CAP_MODE_ALL ? 0xFF : 0x00, sizeof(c->stream));
    if (mode == SCRL_CAP_MODE_PLAN) {
        for (size_t i = 0; i < SCRL_CAP_PLAN_COUNT; i++) scrl_cap_stream_set(c, SCRL_CAP_PLAN_IDS[i]);
    } else if (mode == SCRL_CAP_MODE_LIST) {
        for (size_t i = 0; i < ids_len; i += 2u) scrl_cap_stream_set(c, (uint16_t)(ids[i] | (ids[i + 1u] << 8)));
    }
    memset(c->index, 0xFF, sizeof(c->index));
    c->id_count.store(0);
    c->head.store(0);
    c->tail.store(0);
    c->ring_lost.store(0);
    c->lost_mark = false;
    c->rx_all.store(0);
    c->ext.store(0);
    c->seq = 0;
    c->running.store(true);
    return true;
}

static inline void scrl_cap_stop(ScrlCapture *c) {
    c->running.store(false);
}

/** CAN task: one received frame. ext = extended ID (counted, not kept). */
static inline void scrl_cap_feed(ScrlCapture *c, uint32_t id, bool ext, uint8_t dlc,
                                 const uint8_t *data, uint32_t t_ms) {
    if (!c->running.load(std::memory_order_acquire)) return;
    c->rx_all.fetch_add(1, std::memory_order_relaxed);
    if (ext || id >= SCRL_CAP_STD_IDS) {
        c->ext.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (dlc > 8u) dlc = 8u;

    uint16_t k = c->index[id];
    if (k == 0xFFFFu) {
        uint32_t n = c->id_count.load(std::memory_order_relaxed);
        if (n < SCRL_CAP_MAX_IDS) {
            k = (uint16_t)n;
            ScrlCapIdEntry &e = c->ids[k];
            e.id = (uint16_t)id;
            e.frames = 0;
            e.first_ms = t_ms;
            c->index[id] = k;
            c->id_count.store(n + 1u, std::memory_order_release);
        }
    }
    if (k != 0xFFFFu) {
        ScrlCapIdEntry &e = c->ids[k];
        e.frames++;
        e.last_ms = t_ms;
        e.dlc = dlc;
        memset(e.data, 0, sizeof(e.data));
        memcpy(e.data, data, dlc);
    }

    if (!scrl_cap_streams(c, (uint16_t)id)) return;
    uint32_t head = c->head.load(std::memory_order_relaxed);
    uint32_t tail = c->tail.load(std::memory_order_acquire);
    if (head - tail >= SCRL_CAP_RING) {
        c->ring_lost.fetch_add(1, std::memory_order_relaxed);
        c->lost_mark = true;
        return;
    }
    uint8_t *r = c->ring[head & (SCRL_CAP_RING - 1u)];
    scrl_cap_put32(&r[0], t_ms);
    scrl_cap_put16(&r[4], id);
    r[6] = dlc;
    r[7] = c->lost_mark ? 1u : 0u;
    memset(&r[8], 0, 8u);
    memcpy(&r[8], data, dlc);
    c->lost_mark = false;
    c->head.store(head + 1u, std::memory_order_release);
}

static inline void scrl_cap_header(ScrlCapture *c, uint8_t *out, uint8_t type, uint8_t count) {
    out[0] = type;
    out[1] = count;
    scrl_cap_put16(&out[2], c->seq);
    scrl_cap_put32(&out[4], c->ring_lost.load(std::memory_order_relaxed));
}

/** Records waiting in the ring. */
static inline uint32_t scrl_cap_pending(const ScrlCapture *c) {
    return c->head.load(std::memory_order_acquire) - c->tail.load(std::memory_order_relaxed);
}

/**
 * Sender task: pack waiting records into out[size]. Returns the packet length,
 * or 0 when no record waits or size holds no record. The records stay in the
 * ring until scrl_cap_commit(): a failed notify sends the same packet again.
 */
static inline size_t scrl_cap_pack_frames(ScrlCapture *c, uint8_t *out, size_t size, uint8_t *count_out) {
    if (size < SCRL_CAP_HDR_LEN + SCRL_CAP_REC_LEN) return 0u;
    uint32_t tail = c->tail.load(std::memory_order_relaxed);
    uint32_t n = c->head.load(std::memory_order_acquire) - tail;
    uint32_t room = (uint32_t)((size - SCRL_CAP_HDR_LEN) / SCRL_CAP_REC_LEN);
    if (room > 255u) room = 255u;
    if (n > room) n = room;
    if (n == 0u) return 0u;
    for (uint32_t i = 0; i < n; i++) {
        memcpy(&out[SCRL_CAP_HDR_LEN + i * SCRL_CAP_REC_LEN], c->ring[(tail + i) & (SCRL_CAP_RING - 1u)],
               SCRL_CAP_REC_LEN);
    }
    scrl_cap_header(c, out, SCRL_CAP_FRAMES, (uint8_t)n);
    *count_out = (uint8_t)n;
    return SCRL_CAP_HDR_LEN + n * SCRL_CAP_REC_LEN;
}

/** Sender task: the packet of scrl_cap_pack_frames() went out. */
static inline void scrl_cap_commit(ScrlCapture *c, uint8_t count) {
    c->tail.store(c->tail.load(std::memory_order_relaxed) + count, std::memory_order_release);
    c->seq++;
}

/**
 * Sender task: one ID table packet from entry *cursor on. Returns the length.
 * On success the caller sets *cursor = the returned next entry and calls
 * scrl_cap_sent(). Returns 0 when *cursor is at the end.
 */
static inline size_t scrl_cap_pack_ids(ScrlCapture *c, uint32_t cursor, uint8_t *out, size_t size,
                                       uint32_t *next) {
    uint32_t total = c->id_count.load(std::memory_order_acquire);
    if (cursor >= total || size < SCRL_CAP_HDR_LEN + SCRL_CAP_ID_REC_LEN) return 0u;
    uint32_t room = (uint32_t)((size - SCRL_CAP_HDR_LEN) / SCRL_CAP_ID_REC_LEN);
    uint32_t n = total - cursor;
    if (n > room) n = room;
    for (uint32_t i = 0; i < n; i++) {
        const ScrlCapIdEntry &e = c->ids[cursor + i];
        uint8_t *p = &out[SCRL_CAP_HDR_LEN + i * SCRL_CAP_ID_REC_LEN];
        scrl_cap_put16(&p[0], e.id);
        p[2] = e.dlc;
        p[3] = 0u;
        scrl_cap_put32(&p[4], e.frames);
        scrl_cap_put32(&p[8], e.first_ms);
        scrl_cap_put32(&p[12], e.last_ms);
        memcpy(&p[16], e.data, 8u);
    }
    scrl_cap_header(c, out, SCRL_CAP_IDS, (uint8_t)n);
    *next = cursor + n;
    return SCRL_CAP_HDR_LEN + n * SCRL_CAP_ID_REC_LEN;
}

/** Sender task: a packet other than FRAMES went out. */
static inline void scrl_cap_sent(ScrlCapture *c) {
    c->seq++;
}

/** Sender task: a step mark. out holds SCRL_CAP_HDR_LEN + SCRL_CAP_MARK_LEN bytes. */
static inline size_t scrl_cap_pack_mark(ScrlCapture *c, uint8_t step, uint32_t t_ms, uint8_t *out) {
    scrl_cap_header(c, out, SCRL_CAP_MARK, 1u);
    uint8_t *p = &out[SCRL_CAP_HDR_LEN];
    scrl_cap_put32(&p[0], t_ms);
    p[4] = step;
    p[5] = p[6] = p[7] = 0u;
    return SCRL_CAP_HDR_LEN + SCRL_CAP_MARK_LEN;
}

/** Sender task: the state. out holds SCRL_CAP_HDR_LEN + SCRL_CAP_STATE_LEN bytes. */
static inline size_t scrl_cap_pack_state(ScrlCapture *c, uint32_t t_ms, uint32_t rx_queue_lost, uint8_t *out) {
    scrl_cap_header(c, out, SCRL_CAP_STATE, 1u);
    uint8_t *p = &out[SCRL_CAP_HDR_LEN];
    scrl_cap_put32(&p[0], t_ms);
    p[4] = c->running.load() ? 1u : 0u;
    p[5] = c->mode;
    scrl_cap_put16(&p[6], c->id_count.load(std::memory_order_acquire));
    scrl_cap_put32(&p[8], c->rx_all.load(std::memory_order_relaxed));
    scrl_cap_put32(&p[12], rx_queue_lost);
    scrl_cap_put32(&p[16], c->ext.load(std::memory_order_relaxed));
    return SCRL_CAP_HDR_LEN + SCRL_CAP_STATE_LEN;
}

// ── sender: the order of the packets ──────────────────────────────────────────
//
// The sender task owns ScrlCapSender, except the mark queue: the BLE task
// writes marks (scrl_cap_mark_push), the sender task reads them.
// Order of each call: a step mark; after the stop and an empty ring, one last
// ID table walk, then the last state; the state once per second; then frames,
// with the ID table walk every 5 s taking turns with the frames.

#define SCRL_CAP_IDS_PERIOD_MS   5000u
#define SCRL_CAP_STATE_PERIOD_MS 1000u
#define SCRL_CAP_MARK_QUEUE      8u

enum class ScrlCapNext : uint8_t { None, Frames, Ids, Mark, State };

struct ScrlCapSender {
    std::atomic<bool> active;    // from the start until the last packet went out
    ScrlCapNext next;            // kind of the packet that scrl_cap_next() built
    uint8_t next_count;          // records in a FRAMES packet
    uint32_t ids_cursor;         // next ID table entry of the walk
    uint32_t ids_next;           // cursor after the IDS packet that waits
    bool ids_walk;               // a walk runs
    uint32_t ids_at_ms;          // start of the last walk
    uint32_t state_at_ms;        // time of the last state packet, 0 = none yet
    bool final_ids;              // the walk after the stop is done
    bool final_state;            // the last state went out
    bool was_frames;             // the last packet was FRAMES
    uint8_t mark_step[SCRL_CAP_MARK_QUEUE];
    uint32_t mark_ms[SCRL_CAP_MARK_QUEUE];
    std::atomic<uint32_t> mark_head, mark_tail;
};

/** Sender state for a new capture. The first ID table goes out at once. */
static inline void scrl_cap_sender_reset(ScrlCapSender *s, uint32_t now_ms) {
    s->mark_tail.store(s->mark_head.load());
    s->next = ScrlCapNext::None;
    s->next_count = 0;
    s->ids_cursor = 0;
    s->ids_next = 0;
    s->ids_walk = true;
    s->ids_at_ms = now_ms;
    s->state_at_ms = 0;
    s->final_ids = false;
    s->final_state = false;
    s->was_frames = false;
}

/** BLE task: queue a step mark. False when the queue is full. */
static inline bool scrl_cap_mark_push(ScrlCapSender *s, uint8_t step, uint32_t t_ms) {
    uint32_t head = s->mark_head.load();
    if (head - s->mark_tail.load() >= SCRL_CAP_MARK_QUEUE) return false;
    s->mark_step[head % SCRL_CAP_MARK_QUEUE] = step;
    s->mark_ms[head % SCRL_CAP_MARK_QUEUE] = t_ms;
    s->mark_head.store(head + 1u);
    return true;
}

/**
 * Sender task: the next packet into out[size], or 0 when nothing waits. When
 * the capture stopped and everything went out, it clears s->active.
 */
static inline size_t scrl_cap_next(ScrlCapture *c, ScrlCapSender *s, uint8_t *out, size_t size, uint32_t now_ms,
                                   uint32_t rx_queue_lost) {
    if (!s->active.load()) return 0u;
    if (size < SCRL_CAP_HDR_LEN + SCRL_CAP_ID_REC_LEN) return 0u;
    bool running = c->running.load();
    size_t len = 0;

    uint32_t mt = s->mark_tail.load();
    if (mt != s->mark_head.load()) {
        s->next = ScrlCapNext::Mark;
        return scrl_cap_pack_mark(c, s->mark_step[mt % SCRL_CAP_MARK_QUEUE], s->mark_ms[mt % SCRL_CAP_MARK_QUEUE], out);
    }
    bool drained = !running && scrl_cap_pending(c) == 0u;
    if (drained && !s->final_ids) {
        if (!s->ids_walk) {
            s->ids_walk = true;
            s->ids_cursor = 0;
        }
        len = scrl_cap_pack_ids(c, s->ids_cursor, out, size, &s->ids_next);
        if (len) {
            s->next = ScrlCapNext::Ids;
            return len;
        }
        s->ids_walk = false;
        s->final_ids = true;
    }
    if ((running && (s->state_at_ms == 0u || (uint32_t)(now_ms - s->state_at_ms) >= SCRL_CAP_STATE_PERIOD_MS)) ||
        (drained && !s->final_state)) {
        s->next = ScrlCapNext::State;
        return scrl_cap_pack_state(c, now_ms, rx_queue_lost, out);
    }
    if (drained) {
        s->next = ScrlCapNext::None;
        s->active.store(false);
        return 0u;
    }
    if (running && !s->ids_walk && (uint32_t)(now_ms - s->ids_at_ms) >= SCRL_CAP_IDS_PERIOD_MS) {
        s->ids_walk = true;
        s->ids_cursor = 0;
        s->ids_at_ms = now_ms;
    }
    if (s->ids_walk && (s->was_frames || scrl_cap_pending(c) == 0u)) {
        len = scrl_cap_pack_ids(c, s->ids_cursor, out, size, &s->ids_next);
        if (len) {
            s->next = ScrlCapNext::Ids;
            return len;
        }
        s->ids_walk = false;
    }
    uint8_t count = 0;
    len = scrl_cap_pack_frames(c, out, size, &count);
    if (len) {
        s->next = ScrlCapNext::Frames;
        s->next_count = count;
        return len;
    }
    s->next = ScrlCapNext::None;
    return 0u;
}

/** Sender task: the packet of scrl_cap_next() went out. */
static inline void scrl_cap_next_sent(ScrlCapture *c, ScrlCapSender *s, uint32_t now_ms) {
    switch (s->next) {
    case ScrlCapNext::Frames:
        scrl_cap_commit(c, s->next_count);
        s->was_frames = true;
        break;
    case ScrlCapNext::Ids:
        s->ids_cursor = s->ids_next;
        scrl_cap_sent(c);
        s->was_frames = false;
        break;
    case ScrlCapNext::Mark:
        s->mark_tail.store(s->mark_tail.load() + 1u);
        scrl_cap_sent(c);
        break;
    case ScrlCapNext::State:
        s->state_at_ms = now_ms ? now_ms : 1u;
        if (!c->running.load()) s->final_state = true;
        scrl_cap_sent(c);
        break;
    case ScrlCapNext::None:
        break;
    }
    s->next = ScrlCapNext::None;
}

/** Sender task: drop what waits (the link is gone and the capture stopped). */
static inline void scrl_cap_drop(ScrlCapture *c, ScrlCapSender *s) {
    c->tail.store(c->head.load());
    s->next = ScrlCapNext::None;
    s->active.store(false);
}

// ── runtime (scrl_capture.cpp) ────────────────────────────────────────────────

// Why a start request was refused.
enum class ScrlCapRefuse : uint8_t { None, BadRequest, Busy, Update, NotTwai };

/**
 * BLE task: ask for a start. The CAN task applies it, and the board goes to
 * Listen-Only at once. Refused while a capture runs or still sends, during a
 * BLE update, and on an SPI controller (it filters by ID).
 */
ScrlCapRefuse scrl_capture_request_start(uint8_t mode, const uint8_t *ids, size_t ids_len);

/** Any task: ask for a stop. Safe when no capture runs. */
void scrl_capture_request_stop(void);

/** BLE task: queue a step mark. Returns false when no capture runs or the queue is full. */
bool scrl_capture_mark(uint8_t step);

/** CAN task: apply a start or stop request. Call once per loop. */
void scrl_capture_can_tick(void);

/** CAN task: one received frame. */
void scrl_capture_can_feed(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data, uint32_t t_ms);

/** True from the start request until the last packet went out. */
bool scrl_capture_active(void);

/** Sender task: the link is gone. Stop, and drop what was not sent. */
void scrl_capture_link_lost(void);

/**
 * Sender task: build the next packet into out[size]. Returns its length, or 0
 * when nothing waits. Call scrl_capture_sent() after the notify worked. A
 * packet that did not go out comes back on the next call.
 */
size_t scrl_capture_next_packet(uint8_t *out, size_t size, uint32_t now_ms, uint32_t rx_queue_lost);
void scrl_capture_sent(void);
