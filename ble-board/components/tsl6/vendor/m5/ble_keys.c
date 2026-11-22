/* Remote key events: a key code (from a linked BLE remote or a broadcast
   remote) runs the action of the matching key table entry
   (stock 0x1104A..0x11238). The key task (id 0x2000BFE4) times the presses:
   event 1 = long press time reached, event 2 = key released (no repeat for
   300 ms), events 8/16 = report the key to the phone. */
#include "tsl6.h"
#include "m5.h"
#include "ble_remote.h"

/* Stock 0x1104A. Runs the action of every key table entry with the current
   key code. released = 1 runs action 0xFF (key up). When the phone asked to
   learn a key (0x2000BFE5), it ends the learn mode instead. */
M5_CALLED void key_action(uint32_t released)
{
    if (g_2000bfe8 == 0)
        return;
    if (g_2000bfe5 != 0) {
        g_2000bfe5 = 0;
        g_2000bffc = 0;
        scan_if_needed();
        if (released == 0)
            tmos_start_task(g_2000bfe4, 8, 32);
        return;
    }
    for (uint32_t i = 0; i < KEY_N; i++) {
        key_entry_t *k = &g_20004078[i];
        if ((((uint32_t)k->code_hi << 16) | k->code_lo) != g_2000bfe8)
            continue;
        if (released == 1) {
            button_action(0xFF);
        } else {
            button_action(g_2000bfee ? k->act_long : k->act_short);
            tmos_start_task(g_2000bfe4, 0x10, 160);
        }
    }
}

/* Stock 0x11136. Key task event handler. */
uint32_t key_task_event(uint32_t task, uint32_t ev)
{
    if (ev & 2) {
        key_action(1);
        g_2000bfef = 0;
        return ev ^ 2;
    }
    if (ev & 1) {
        key_action(0);
        return ev ^ 1;
    }
    if (ev & 0x10) {
        ba_send_later(&g_2000bfe8, 7);
        return ev ^ 0x10;
    }
    if (ev & 8) {
        ba_send_later(&g_2000bfe8, 6);
        RES32(0x58, uint8_t, (uint32_t, uint32_t, uint32_t), task, 4, 2);  /* tmos_start_task */
        return ev ^ 8;
    }
    if (ev & 4) {
        g_2000bfe5 = 0;
        g_2000bfef = 0;
        return ev ^ 4;
    }
    return 0;
}

/* Stock 0x111D6. A key code came in from a broadcast remote (0 = the same
   advert again). A code != 0 runs its action at once (the long action when
   long_ is set). Every call restarts the 300 ms release time (event 2). */
M5_CALLED void key_broadcast(uint32_t code, uint32_t num, uint32_t long_)
{
    if (code != 0) {
        g_2000bfef = 1;
        g_2000bfe8 = code;
        g_2000bfec = (uint16_t)num;
        g_2000bfee = (uint8_t)long_;
        key_action(0);
    }
    tmos_start_task(g_2000bfe4, 2, 480);
}
