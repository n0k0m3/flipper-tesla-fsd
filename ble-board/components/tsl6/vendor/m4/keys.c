/* Key (button) events (stock 0xDF0A..0xE9EA).
   key_track turns raw key codes into click counts and hold phases.
   key_event maps (key, clicks, phase) to shortcut triggers (event_fire, the
   AB table), car functions and service functions.
   Phases: 0 press, 1 release (short), 2 clicks done (500 ms idle),
   3 hold (500 ms), 4 release after a hold.
   Key compares use the full 32-bit register as in stock; the jump tables and
   range tests use the low byte (rule 6).
   key_event and key_track are void in effect, but the test compares a0. So
   they return the value that stock leaves in a0 (the result of the last
   call, or a constant that stock loads before a branch). */
#include "tsl6.h"
#include "m4.h"

/* Phase 0, key held with b352 set and one click: seat and shortcut keys. */
static inline __attribute__((always_inline)) void key_press_first(uint32_t key)
{
    switch ((uint8_t)key) {
    case 43: seat_pulse_long(8); break;
    case 44: seat_pulse_long(4); break;
    case 45: seat_pulse_long(0x800); break;
    case 46: seat_pulse_long(0x400); break;
    case 47: seat_pulse_long(0x8000); break;
    case 48: seat_pulse_long(0x4000); break;
    case 49: seat_pulse_long(0x40); break;
    case 50: seat_pulse_long(0x80); break;
    case 55:
        event_fire(42);
        app_timer_start(hazard_toggle, 0, 500, 0);
        break;
    case 71: event_fire(40); break;
    case 72: event_fire(41); break;
    default: break;
    }
}

/* Phase 2, one click. Returns a0 as stock leaves it. */
static inline __attribute__((always_inline)) uint32_t key_click1(uint32_t key)
{
    uint32_t t;

    switch (key) {
    case 8:
        flash_cancel();
        if ((g_cfg[6] & 0x40) && !g_20004130)
            flash_start3();
        t = 76;
        break;
    case 14: t = 26; break;
    case 15: t = 21; break;
    case 51: t = 180; break;
    case 52: t = 184; break;
    case 53: t = 188; break;
    case 54: t = 192; break;
    case 56: t = 60; break;
    case 68:
        if ((g_cfg[6] & 0x40) && !g_20004130)
            flash_start3();
        t = 70;
        break;
    default:
        /* a0 as stock leaves it: a preloaded trigger or the c250_is_2 result. */
        return (key > 15 && key < 51) ? 180 : 0;
    }
    return event_fire(t);
}

/* Phase 2, two clicks (stock jump table 0x191F8 on the low byte). */
static inline __attribute__((always_inline)) uint32_t key_click2(uint32_t key)
{
    uint32_t t;

    switch ((uint8_t)key) {
    case 5: t = 2; break;
    case 8: t = 77; break;
    case 13:
        event_fire(3);
        return ap_auto_mode(1);
    case 14: t = 27; break;
    case 15: t = 22; break;
    case 19: t = 38; break;
    case 21: t = 39; break;
    case 23: t = 58; break;
    case 25: t = 59; break;
    case 27: t = 30; break;
    case 28: t = 31; break;
    case 29: t = 32; break;
    case 30: t = 33; break;
    case 31: t = 200; break;
    case 32: t = 201; break;
    case 33: t = 202; break;
    case 34: t = 203; break;
    case 51: t = 181; break;
    case 52: t = 185; break;
    case 53: t = 189; break;
    case 54: t = 193; break;
    case 56: t = 61; break;
    case 68: t = 71; break;
    case 71: t = 9; break;
    case 72: t = 8; break;
    case 73: t = 7; break;
    case 74: t = 6; break;
    case 87:
        return tick_start((uint8_t)(tick_is_on() ^ 1), 1);
    default:
        return 0;
    }
    return event_fire(t);
}

/* Phase 2, three clicks. */
static inline __attribute__((always_inline)) uint32_t key_click3(uint32_t key)
{
    uint32_t t;

    switch (key) {
    case 8: t = 78; break;
    case 14: t = 28; break;
    case 15: t = 23; break;
    case 19: t = 66; break;
    case 21: t = 67; break;
    case 23: t = 68; break;
    case 25: t = 69; break;
    case 56: t = 62; break;
    case 68: t = 72; break;
    default:
        /* a0 as stock leaves it: the trigger preloaded before the last compare. */
        if (key < 14)
            return 78;
        if (key < 21)
            return 66;
        if (key < 25)
            return 68;
        return 72;
    }
    return event_fire(t);
}

/* Phase 3 (hold), one click. */
static inline __attribute__((always_inline)) uint32_t key_hold1(uint32_t key)
{
    uint32_t t;

    switch (key) {
    case 8:
        return highbeam_toggle();
    case 14: t = 29; break;
    case 15: t = 24; break;
    case 51: t = 182; break;
    case 52: t = 186; break;
    case 53: t = 190; break;
    case 54: t = 194; break;
    case 55: t = 20; break;
    case 56: t = 63; break;
    case 68: t = 73; break;
    case 71: t = 10; break;
    case 72: t = 12; break;
    case 73: t = 14; break;
    case 74: t = 16; break;
    default:
        /* a0 as stock leaves it: the trigger preloaded before the last compare. */
        if (key < 14)
            return 29;
        if (key > 15 && key < 51)
            return 182;
        if (key > 56 && key < 71)
            return 73;
        if (key > 74)
            return 16;
        return 0;
    }
    return event_fire(t);
}

/* Phase 3 (hold), two clicks (stock jump table 0x1936C on the low byte). */
static inline __attribute__((always_inline)) uint32_t key_hold2(uint32_t key)
{
    uint32_t t;

    switch ((uint8_t)key) {
    case 14: t = 79; break;
    case 15: t = 25; break;
    case 27: t = 34; break;
    case 28: t = 35; break;
    case 29: t = 36; break;
    case 30: t = 37; break;
    case 31: t = 204; break;
    case 32: t = 205; break;
    case 33: t = 206; break;
    case 34: t = 207; break;
    case 51: t = 183; break;
    case 52: t = 187; break;
    case 53: t = 191; break;
    case 54: t = 195; break;
    case 56: t = 65; break;
    case 68: t = 75; break;
    case 71: t = 11; break;
    case 72: t = 13; break;
    case 73: t = 15; break;
    case 74: t = 17; break;
    default:
        return 0;
    }
    return event_fire(t);
}

/* Phase 0, after c250_is_2: actions that do not wait for the click count.
   Returns a0 as stock leaves it (some paths keep a constant that stock
   loads before a compare). */
static inline __attribute__((always_inline)) uint32_t key_press(uint32_t key, uint32_t count)
{
    uint32_t t;
    uint32_t r;

    if (c250_is_2()) {
        g_20004132 = 1;
        switch (key) {
        case 1: return func_hold_start(2);
        case 3: return func_hold_start(3);
        case 8: return func_hold_start(1);
        case 56: return func_hold_start(0);
        case 68: return func_hold_start(1);
        case 19: return event_fire(50);
        case 20: return event_fire(51);
        case 21: return event_fire(52);
        case 22: return event_fire(53);
        case 23: return event_fire(54);
        case 24: return event_fire(55);
        case 25: return event_fire(56);
        case 26: return event_fire(57);
        case 35: return seat_pulse_long(2);
        case 36: return seat_pulse_long(1);
        case 37: return seat_pulse_long(0x200);
        case 38: return seat_pulse_long(0x100);
        case 39: return seat_pulse_long(0x2000);
        case 40: return seat_pulse_long(0x1000);
        case 41: return seat_pulse_long(0x10);
        case 42: return seat_pulse_long(0x20);
        default:
            break;
        }
        /* Constants that stock leaves in a0. */
        if (key < 3)
            return 2;
        if (key < 19)
            return 50;
        if (key < 35)
            return 2;
        return 0;
    }
    switch (key) {
    case 8:
    case 68:
        g_20004130 = flash_active_get() ? 1 : c28e_field_state() != 0;
        return flash_cancel();
    case 9: t = 4; break;
    case 10: t = 19; break;
    case 11: t = 5; break;
    case 12: t = 18; break;
    case 56:
        return swc_modes_off();
    case 61:
        if ((uint8_t)(count - 1) <= 1)
            return ap_auto_mode(2);
        return 0;
    case 69:
        return highbeam_toggle();
    default:
        return 0;   /* c250_is_2 result */
    }
    if (g_2000412a)
        return 0;
    r = c24f_is_2();
    if (r)
        return r;
    return event_fire(t);
}

/* Phase 0, after key_press: seat recall stop, AP keys, brake hold.
   r = a0 so far; returns a0 as stock leaves it. */
static inline __attribute__((always_inline)) uint32_t key_press_tail(uint32_t key, uint32_t r)
{
    if ((uint8_t)(key - 35) <= 30) {
        uint32_t bit = 1u << (uint8_t)(key - 35);
        if (bit & 0x76000000)       /* keys 60, 61, 63, 64, 65 */
            g_2000412f = (uint8_t)(r = gear_get());
        else if (bit & 0xffff)      /* keys 35..50 */
            r = seat_recall_cancel(3);
    } else if (key == 76) {
        event_fire(90);
        goto ap_cancel;
    }

    if (key == 63)
        goto ap_cancel;
    if (key > 63) {
        if (key == 77) {
            r = event_fire(91);
            goto brake;
        }
        if (key > 77) {
            if (key == 78) {
                r = event_fire(100);
                goto brake;
            }
            if (key == 79) {
                event_fire(98 + (g_2000412a != 0));
                r = event_fire(101);
                goto brake;
            }
            goto ap_keys;
        }
        if (key == 76)
            goto ap_cancel;
        goto ap_keys;
    }
    if (key == 13)
        goto ap_cancel;
    if (key < 13)
        goto ap_keys;
    if ((uint8_t)(key - 58) > 1)
        goto ap_keys;
ap_cancel:
    ap_state_update_v12(0);
    reengage_off();
    r = ap_off();
ap_keys:
    if (key == 64) {
        g_2000412b = 1;
        bf1d_set(1);
        door_output_set(1);
        r = event_fire(111);
        goto brake;
    }
    if (key > 64 ? key <= 66 : key == 63) {
        uint32_t t;
        if (g_2000412b) {
            g_2000412b = 0;
            bf1d_set(0);
        }
        door_output_set(0);
        if (key == 65)
            t = 112;
        else if (key == 66)
            t = 113;
        else if (key != 64)
            t = 110;
        else
            t = 111;   /* not reached: key 64 takes the branch above */
        r = event_fire(t);
    }
brake:
    /* "Auto park brake" setting (config byte 0 bit 4). */
    if (g_cfg[0] & 0x10) {
        if (key == 63)
            return park_timer_set(1);
        if (key > 63 && key <= 66)
            r = park_timer_set(0);
    }
    if (key > 61) {
        if ((uint8_t)(key - 68) > 1)
            return r;
    } else if (key < 60) {
        if ((uint8_t)(key - 1) > 15)
            return r;
    }
    return tick_schedule();
}

/* Stock 0xDF0A. One key event. key: key code, count: clicks, phase: 0..4.
   Returns the stock a0 (no meaning). */
uint32_t key_event(uint32_t key, uint32_t count, uint32_t phase)
{
    uint32_t r;

    switch (phase) {
    case 0:
        g_2000412a = ap_active();
        if (c24f_is_2()) {
            g_20004131 = 1;
            if (count == 1)
                key_press_first(key);
        }
        r = key_press(key, count);
        return key_press_tail(key, r);

    case 1:
        return button_action(0xff);

    case 2:
        if (g_20004186 == 0) {
            if ((uint8_t)(key - 60) <= 1 && (uint8_t)(count - 1) <= 1 &&
                gear_get() == 4 && g_2000412f == 4 && !brake_ev_get() &&
                ((uint8_t)(g_20004188 - 1) > 1 || speed_get())) {
                ap_state_update_v12(1);
                if ((g_cfg[7] & 8) && c284_bit8_get())
                    rstalk_request(3, 1, 0);
            }
        } else if ((g_cfg[7] & 8) && key == 13 && count == 1 && c284_bit8_get() &&
                   !g_2000412a) {
            ap_engage_second_press(0);
        }
        r = c250_is_2();
        if (!r && !g_20004132) {
            if (count == 2)
                r = key_click2(key);
            else if (count == 3)
                r = key_click3(key);
            else if (count == 1)
                r = key_click1(key);
        }
        g_20004131 = 0;
        g_20004132 = 0;
        return r;

    case 3:
        r = c24f_is_2();
        if (!r && !g_20004131) {
            switch ((uint8_t)(count - 1)) {
            case 0:
                r = key_hold1(key);
                break;
            case 1:
                r = key_hold2(key);
                break;
            case 3:
                /* Key 2, four clicks and hold: factory reset (config, AB and
                   BA tables), a melody, then a chip reset. */
                if (key == 2) {
                    cfg_reset();
                    ab_table_reset();
                    ba_table_reset();
                    r = buzzer_play(tune_falling3, system_reset, 100);
                }
                break;
            case 5:
                /* Key 2, six clicks and hold: a melody, then a chip reset. */
                if (key == 2)
                    r = buzzer_play(tune_wobble, system_reset, 1000);
                break;
            case 9:
                /* Key 2, ten clicks and hold: UID CRC, boot flag CRC, melody. */
                if (key == 2)
                    r = tune_scale(0); /* port: no vendor activation */
                break;
            default:
                break;
            }
        }
        g_20004131 = 0;
        g_20004132 = 0;
        if (key - 60 <= 1) {    /* full 32-bit compare, as in stock */
            tick_schedule();
            return feature_d8_arm();
        }
        return r;

    case 4:
        r = button_action(0xff);
        if (key == 56) {
            swc_modes_off();
            return event_fire(64);
        }
        if (key == 68) {
            flash_cancel();
            return event_fire(74);
        }
        if (key == 55)
            return app_timer_start(hazard_toggle, 0, 200, 0);
        return r;

    default:
        return key;
    }
}

/* Stock 0xE896. Key state machine. key: code now, idle: code for "no key".
   st[0] = state (0 idle, 1 pressed, 2 held), st[1] = clicks (0xFF = a
   different key came in the click series), st[2] = key, st+4 = time (ms).
   Keys 58..61 always start a new click. Returns the stock a0 (no meaning). */
uint32_t key_track(uint32_t key, uint32_t idle, uint8_t *st)
{
    uint32_t *t = (uint32_t *)(st + 4);
    uint32_t n, r;

    switch (st[0]) {
    case 0:
        n = st[1];
        if (key == idle) {
            if (!n)
                return key;
            r = clock_ms() - *t;
            if (r <= 499)
                return r;
            if (st[1] != 0xff)
                r = key_event(st[2], st[1], 2);
            st[1] = 0;
            return r;
        }
        if ((uint8_t)(key - 58) <= 3 || (n != 0xff && (n == 0 || st[2] == key))) {
            n = (uint8_t)(n + 1);
            st[2] = (uint8_t)key;
            st[1] = (uint8_t)n;
            st[0] = 1;
            key_event(key, n, 0);
        } else {
            st[1] = 0xff;
        }
        return *t = clock_ms();

    case 1:
        if (st[2] != key) {
            st[0] = 0;
            return key_event(st[2], st[1], 1);
        }
        r = clock_ms() - *t;
        if (r <= 499)
            return r;
        st[0] = 2;
        return key_event(st[2], st[1], 3);

    case 2:
        if (key != idle)
            return key;
        r = key_event(st[2], st[1], 4);
        st[0] = 0;
        st[1] = 0;
        return r;

    default:
        return key;
    }
}

/* Stock 0xE992. Fires trig_a when val becomes a, trig_b when it becomes b.
   Only when car_frames_seen is true (else it only stores val). Returns the
   car_frames_seen result when it fires, else 0. */
uint32_t trigger_on_change(uint8_t *last, uint32_t val, uint32_t trig_a, uint32_t trig_b,
                           uint32_t a, uint32_t b)
{
    uint32_t r;

    if (val != a && val != b)
        return 0;
    r = car_frames_seen();
    if (!r) {
        *last = (uint8_t)val;
        return 0;
    }
    if (*last == val)
        return 0;
    *last = (uint8_t)val;
    event_fire(val == a ? trig_a : trig_b);
    return r;
}
