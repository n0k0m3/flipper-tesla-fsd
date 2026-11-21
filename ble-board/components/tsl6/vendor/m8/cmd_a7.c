/* BLE command A7 "control" (stock 0x15F14).
   The command dispatcher (0x14534) calls it with the first data byte as
   "code". Each code runs one action. Most actions are in other modules and
   have no names yet, so the cases name the stock function.
   Codes 52..71 are the seat memory: even = recall slot (code - 52) / 2,
   odd = save slot (code - 53) / 2 with a 2-byte reply {1, saved}.
   Code 255 replies with BD {ok}. */
#include "tsl6.h"
#include "m8.h"

/* Stock 0x15F14. Nothing happens when the module is off (config byte 4
   bit 3). Stock returns the a0 of the action, or conn for an unknown code. */
uint32_t cmd_a7_control(uint32_t conn, uint32_t cmd, uint32_t code)
{
    uint8_t reply[2];
    uint32_t ok;

    if (g_cfg[4] & 8)
        return conn;
    switch (code) {
    case 0: return action_when_ready(1);
    case 1: return action_when_ready(4);
    case 2: return action_when_ready(7);
    case 3: return action_when_ready(10);
    case 4: return heat_request_set(0);
    case 5: return heat_request_set(1);
    case 6: return cp_request(0);
    case 7: return cp_request(1);
    case 8: return frunk_open_when_ready();
    case 9: return action_18_when_ready();
    case 10: return action_1a_run();
    case 11: return vc273_d3_req((uint8_t)(vc102_d6_is2() ^ 1));
    /* Manual seat moves (seat 1 axes 0/1), 200 x 100 ms. */
    case 12: return seat_pulse(1, 200, 1);
    case 13: return seat_pulse(2, 200, 1);
    case 14: return seat_pulse(0x20, 200, 1);
    case 15: return seat_pulse(0x10, 200, 1);
    case 16: return seat_pulse(0, 3, 1);       /* stop */
    case 17: return vc273_d3b7_pulse(conn);   /* 0xF37A returns its a0 */
    case 18: return seat_cycle_set(0, 1, 0);
    case 19: return seat_cycle_set(1, 0, 0);
    case 20: return seat_cycle_set(0, 2, 0);
    case 21: return seat_cycle_set(2, 0, 0);
    case 22: return vc273_lock_req_set(1);
    case 23: return vc273_lock_req_set(3);
    case 24: return can_2f3_bf0f_set(0);
    case 25: return can_2f3_bf0f_set(1);
    /* Manual seat moves (seat 0 axes 0/1). */
    case 26: return seat_pulse(4, 200, 1);
    case 27: return seat_pulse(8, 200, 1);
    case 28: return seat_pulse(0x80, 200, 1);
    case 29: return seat_pulse(0x40, 200, 1);
    case 30: return front_fog_strobe_set(0, 1);
    case 31: return front_fog_strobe_set(1, 1);
    case 32: return front_fog_burst_set(0, 1);
    case 33: return front_fog_burst_set(1, 1);
    case 34: return rear_fog_strobe_set(0, 1);
    case 35: return rear_fog_strobe_set(1, 1);
    case 36: return rear_fog_burst_set(0, 1);
    case 37: return rear_fog_burst_set(1, 1);
    case 38: return flash_mode_set(0, 1);
    case 39: return flash_mode_set(1, 1);
    case 40: return bf32_horn_set(0);
    case 41: return bf32_horn_set(1);
    case 42: return ap_engage(0);
    case 43: return ap_engage(1);
    case 44: return sport_mode_set(0);
    case 45: return sport_mode_set(1);
    case 46: return vc334_d3_bit1_set(0);
    case 47: return vc334_d3_bit1_set(1);
    case 48: return vc2b6_app_set(0);
    case 49: return vc2b6_app_set(1);
    case 50: return vc334_d3_bit0_set(0);
    case 51: return vc334_d3_bit0_set(1);
    case 52: case 54: case 56: case 58: case 60:
    case 62: case 64: case 66: case 68: case 70:
        return seat_recall((uint8_t)((int32_t)(code - 52) >> 1));
    case 53: case 55: case 57: case 59: case 61:
    case 63: case 65: case 67: case 69: case 71:
        reply[0] = 1;
        reply[1] = (uint8_t)seat_save((uint8_t)((int32_t)(code - 53) >> 1));
        return ble_send_any(conn, cmd, reply, 2);
    case 255:
        if (platform_get() == 1 && gear_get() != 2 && gear_get() != 3 && gear_get() != 4) {
            send_339(0);
            ok = 1;
        } else {
            ok = 0;
        }
        return ble_reply_bd(conn, ok);
    default:
        return conn;
    }
}
