/* Door, frunk and trunk latches (0x102 in m4, 0x103, 0x2E1) and the 0x1F9
   frame edit that writes door fields. Stock 0xD45A..0xDB8E.

   Latch states (VCLEFT/VCRIGHT door status): 1 = open, 2 = closed,
   3 and 4 = moving. Raw latch bytes: g_latch_raw[0..4] = BF8B..BF8F
   (FL, FR, RL, RR, trunk). Gauge copies: BF86 FL, BF87 FR, BF88 RL, BF89 RR.

   0x1F9 fields: A = data[0] bits 7:5, B = data[1] bits 2:0,
   C = data[1] bits 5:3, D = data[1] bits 7:6 + data[2] bit 0 (bit 2 of D),
   E = data[2] bits 3:1. Values: 6, 3 or 2 (from the latch state). */
#include "tsl6.h"
#include "m3.h"

/* Stock 0xD45A. */
uint32_t doors_all_closed(void)
{
    return g_door_fl == 2 && g_door_rl == 2 && g_door_fr == 2 && g_door_rr == 2;
}

/* Stock 0xD492..0xD4B6 (timer callbacks, not in the Ghidra function list):
   0x1F9 commands 20..23 after 250 ms. */
void door_cb_a(uint8_t *arg)
{
    g_1f9_cmd = 20;
}

void door_cb_b(uint8_t *arg)
{
    g_1f9_cmd = 21;
}

void door_cb_c(uint8_t *arg)
{
    g_1f9_cmd = 22;
}

void door_cb_d(uint8_t *arg)
{
    g_1f9_cmd = 23;
}

/* Stock 0xD4C2. Sets data[0] bit 1 (sel = 0) or bit 0 (sel != 0) in the
   cached 0x3B3 frame and sends it two times. */
void can_3b3_send_bit(uint32_t sel)
{
    m3_frame_t buf;

    frame_slot_copy(1, 8, &buf);
    if (buf.dlc == 0)
        return;
    buf.data[0] |= (uint8_t)((sel == 0) + 1);
    can_tx(&buf);
    can_tx(&buf);
}

/* Stock 0xD4FE (also a timer callback). Runs the pending door command:
   1..19 go to the 0x1F9 edit, 24/26 send a 0x3B3 bit, 25 runs 0xF540. */
void door_cmd_run(uint8_t *arg)
{
    uint32_t c = g_door_cmd;

    if (c == 24) {
        can_3b3_send_bit(0);
    } else if (c > 24) {
        if (c == 25) {
            vc273_frunk_req();
        } else if (c == 26) {
            can_3b3_send_bit(1);
        } else {
            g_door_cmd = 0;
            RETURN_A0(1);
        }
    } else {
        if ((uint8_t)(c - 1) <= 18)
            g_1f9_cmd = (uint8_t)c;
        g_door_cmd = 0;
        RETURN_A0(arg);
    }
    g_door_cmd = 0;
}

/* Stock 0xD556. Hold timer: returns cond once it has been nonzero for secs
   seconds, else 0. *st: 0 idle, 1 timing (start time in *t0), 2 fired. */
uint32_t hold_check(uint32_t secs, uint32_t cond, uint8_t *st, uint32_t *t0)
{
    if (secs == 0)
        return 0;
    switch (*st) {
    case 0:
        if (cond) {
            *t0 = clock_ms();
            *st = 1;
        }
        return 0;
    case 1:
        if (cond == 0)
            break;
        if (clock_ms() - *t0 < secs * 1000)
            return 0;
        *st = 2;
        return cond;
    case 2:
        if (cond)
            return 0;
        break;
    default:
        return 0;
    }
    *st = 0;
    return 0;
}

/* Stock 0xD5D0. Field value 3. Except on variants 7 and 8, starts cb in
   250 ms. */
uint32_t door_field_release(m3_timer_cb_t cb)
{
    if ((uint8_t)(g_20004188 - 7) > 1)
        app_timer_start(cb, 0, 250, 0);
    return 3;
}

/* Latch state to field value: open 6, SNA 3, moving 2, else 3. */
static uint32_t latch_to_field(uint32_t st)
{
    if (st == 1)
        return 6;
    if (st == 0)
        return 3;
    return ((uint8_t)(st - 3) > 1) + 2;
}

/* Stock 0xD5FC. Variants 7 and 8: field value from the latch state.
   Others: value 3, and cb in 250 ms. */
uint32_t door_field_value(uint32_t st, m3_timer_cb_t cb)
{
    if ((uint8_t)(g_20004188 - 7) > 1) {
        app_timer_start(cb, 0, 250, 0);
        return 3;
    }
    return latch_to_field(st);
}

/* Stock 0xD628. No caller found. */
uint32_t door_field_timer(uint32_t unused, m3_timer_cb_t cb)
{
    app_timer_start(cb, 0, 250, 0);
    return 3;
}

/* Stock 0xD644. One door latch value v (from 0x102/0x103): stores the raw
   value; for open (1) or closed (2) also the gauge copy. The first open door
   gives event 0x68 (and 0xFB32(1) with config byte 6 bit 4, then 0xAF1E);
   all doors closed again gives event 0x69 (and 0xFB32(0), but not in R
   when config byte 6 bit 7 is set). */
void door_update(uint8_t *raw, uint8_t *gauge, uint32_t v)
{
    uint32_t c;

    if (*raw != v)
        *raw = (uint8_t)v;
    if ((uint8_t)(v - 1) > 1)
        return;
    if (*gauge != v) {
        *gauge = (uint8_t)v;
        gauge_push();
    }
    if (doors_all_closed()) {
        if (!g_door_open_flag)
            return;
        g_door_open_flag = 0;
        c = g_cfg[6];
        if ((c & 0x10) && ((int8_t)c >= 0 || gear_get() != 2))
            high_beam_timer_switch(0);
        event_fire(0x69);
        return;
    }
    if (g_door_fl != 1 && g_door_rl != 1 && g_door_fr != 1 && g_door_rr != 1)
        return;
    if (g_door_open_flag)
        return;
    g_door_open_flag = 1;
    if (g_cfg[6] & 0x10)
        high_beam_timer_switch(1);
    event_fire(0x68);
    hvac_override_stop(0);
}

/* Stock 0xD724. With config byte 6 bit 7: on runs 0xFB32(1); off runs
   0xFB32(0), but with config byte 6 bit 4 only when all doors are closed. */
void door_output_set(uint32_t on)
{
    uint32_t c = g_cfg[6];

    if ((int8_t)c >= 0)
        return;
    if (on) {
        high_beam_timer_switch(1);
        return;
    }
    if ((c & 0x10) && !doors_all_closed())
        return;
    high_beam_timer_switch(0);
}

/* Stock 0xD762. 0x2E1 (VCFRONT status), mux 0 (data[0] bits 2:0):
   frunk latch in data[0] bits 6:3. Open state change gives event 0xC4
   (open) or 0xC5 (closed). */
void can_2e1_frunk(m3_frame_t *f)
{
    uint32_t d0 = f->data[0];
    uint32_t v;
    uint32_t open;

    if (d0 & 7)
        return;
    v = (d0 >> 3) & 0xF;
    if (v - 1 > 3)
        return;
    open = v != 2;
    if (g_frunk_open == open)
        return;
    g_frunk_open = (uint8_t)open;
    gauge_push();
    event_fire((g_frunk_open == 0) + 0xC4);
}

/* Stock 0xD7B6. 0x103 (VCRIGHT door status): FR latch data[0] bits 3:0,
   RR latch data[0] bits 7:4, trunk latch data[7] bits 3:0.
   FR changes go to 0xE992 (events 0x74/0x75; with config byte 1 bits 4:3
   = 2 also action 0x17450(5 or 6)); RR changes to 0xE992 (0x7C/0x7D).
   Trunk open state change gives event 0xC6 (open) or 0xC7. */
void can_103_doors(m3_frame_t *f)
{
    uint32_t v;
    uint32_t open;

    door_update(&g_latch_raw[1], &g_door_fr, f->data[0] & 0xF);
    door_update(&g_latch_raw[3], &g_door_rr, f->data[0] >> 4);
    if (trigger_on_change(&g_103_sw0, f->data[0] & 0xF, 0x74, 0x75, 1, 2)
        && (g_cfg[1] & 0x18) == 0x10) {
        if (g_103_sw0 == 1)
            seat_recall(5);
        else if (g_103_sw0 == 2)
            seat_recall(6);
    }
    trigger_on_change(&g_103_sw1, f->data[0] >> 4, 0x7C, 0x7D, 1, 2);
    v = f->data[7] & 0xF;
    if (g_latch_raw[4] != v)
        g_latch_raw[4] = (uint8_t)v;
    if (v - 1 > 3)
        return;
    open = v != 2;
    if (g_trunk_open == open)
        return;
    g_trunk_open = (uint8_t)open;
    gauge_push();
    event_fire((g_trunk_open == 0) + 0xC6);
}

static void set_a(m3_frame_t *f, uint32_t v)
{
    f->data[0] = (f->data[0] & 0x1F) | (uint8_t)(v << 5);
}

static void set_b(m3_frame_t *f, uint32_t v)
{
    f->data[1] = (f->data[1] & 0xF8) | (v & 7);
}

static void set_c(m3_frame_t *f, uint32_t v)
{
    f->data[1] = (f->data[1] & 0xC7) | (uint8_t)((v & 7) << 3);
}

static void set_d(m3_frame_t *f, uint32_t v)
{
    f->data[1] = (f->data[1] & 0x3F) | (uint8_t)((v & 3) << 6);
    f->data[2] = ((v >> 2) & 1) | (f->data[2] & 0xFE);
}

/* Stock 0xD8BA. 0x1F9 edit: applies the pending command (1..24) to the door
   fields and sends the frame. Commands above 24 only send the frame. */
void can_1f9_door_edit(m3_frame_t *f)
{
    uint32_t cmd = g_1f9_cmd;
    uint32_t u;

    if (cmd == 0)
        return;
    switch (cmd) {
    case 1:
        set_a(f, door_field_value(g_latch_raw[0], door_cb_a));
        break;
    case 2:
        set_a(f, 6);
        break;
    case 3:
        set_a(f, door_field_release(door_cb_a));
        break;
    case 4:
        set_b(f, door_field_value(g_latch_raw[1], door_cb_b));
        break;
    case 5:
        set_b(f, 6);
        break;
    case 6:
        set_b(f, door_field_release(door_cb_b));
        break;
    case 7:
        set_c(f, door_field_value(g_latch_raw[2], door_cb_c));
        break;
    case 8:
        set_c(f, 6);
        break;
    case 9:
        set_c(f, door_field_release(door_cb_c));
        break;
    case 10:
        set_d(f, door_field_value(g_latch_raw[3], door_cb_d));
        break;
    case 11:
        f->data[1] = (f->data[1] & 0x3F) | 0x80;
        f->data[2] |= 1;
        break;
    case 12:
        set_d(f, door_field_release(door_cb_d));
        break;
    case 13:
        /* A, B, C, D, E = 6. */
        f->data[0] = (f->data[0] & 0x1F) | 0xC0;
        f->data[1] = 0xB6;
        f->data[2] = (f->data[2] & 0xF0) | 0xD;
        break;
    case 14:
        /* A, B, C, D = 3; E stays. */
        f->data[0] = (f->data[0] & 0x1F) | 0x60;
        f->data[1] = 0xDB;
        f->data[2] &= 0xFE;
        break;
    case 15:
        /* A, B, C, D, E = 2. */
        f->data[0] = (f->data[0] & 0x1F) | 0x40;
        f->data[1] = 0x92;
        f->data[2] = (f->data[2] & 0xF0) | 4;
        break;
    case 16:
    case 17:
        /* C and D (16: also B) from the RL latch on variants 7 and 8, else 3. */
        u = 3;
        if ((uint8_t)(g_20004188 - 7) <= 1)
            u = latch_to_field(g_latch_raw[2]);
        if (cmd == 16)
            f->data[1] = (f->data[1] & 0xF8) | (u & 7);
        f->data[1] = (f->data[1] & 7) | (uint8_t)((u & 7) << 3) | (uint8_t)((u & 3) << 6);
        f->data[2] = (uint8_t)((u >> 2) | (f->data[2] & 0xFE));
        break;
    case 18:
        /* C = 6, D = 6. */
        f->data[1] = (f->data[1] & 7) | 0xB0;
        f->data[2] |= 1;
        break;
    case 19:
        /* C = 3, D = 3. */
        f->data[1] = (f->data[1] & 0xC7) | 0xD8;
        f->data[2] &= 0xFE;
        break;
    case 20:
        set_a(f, 3);
        break;
    case 21:
        set_b(f, 3);
        break;
    case 22:
        set_c(f, 3);
        break;
    case 23:
        f->data[1] |= 0xC0;
        f->data[2] &= 0xFE;
        break;
    case 24:
        /* E from the trunk latch. */
        f->data[2] = (f->data[2] & 0xF1) | (uint8_t)(latch_to_field(g_latch_raw[4]) << 1);
        break;
    default:
        break;
    }
    g_1f9_cmd = 0;
    can_tx(f);
}

/* Stock 0xDB70. Runs door_cmd_run after ms when a command is pending. */
void door_cmd_schedule(uint32_t ms)
{
    if (g_door_cmd == 0)
        return;
    app_timer_start(door_cmd_run, 0, ms, 0);
}
