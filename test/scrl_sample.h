#pragma once
/*
 * scrl_sample.h — one sample status of the BLE board, shared by
 * test_scrl_status.cpp and scrl_vectors.cpp. The web page test
 * (ble-board/web/test/page.test.mjs) decodes the bytes that the firmware
 * packers make from this sample, so the values here are its expected values.
 */

#include <string.h>

#include "scrl_status.h"

static ScrlStatus sample_status(void) {
    ScrlStatus st;
    memset(&st, 0, sizeof(st));
    st.scroll.phase = ScrollAckPhase::SecondPending;
    st.scroll.last_block = ScrollAckBlock::Gap;
    st.scroll.das39b_ap_state = 6;
    st.scroll.das39b_hands_on = 3;
    st.scroll.sent_count = 0x01020304u;
    st.scroll.pairs_aborted = 7;
    st.scroll.last_native_age_ms = 2;
    st.scroll.last_das_age_ms = 40;
    st.scroll.last_vehicle_bus1 = 1;
    st.scroll.last_tx_allowed = true;
    st.scroll.timer_armed = true;
    st.op_mode = 1;
    st.flags = SCRL_FLAG_TX_ALLOWED | SCRL_FLAG_ACTIVE | SCRL_FLAG_ENABLED | SCRL_FLAG_HW4_SEEN;
    st.rx_all = 1000;
    st.rx_3c2 = 10;
    st.rx_39b = 20;
    st.rx_370 = 0;
    st.rx_257 = 30;
    st.rx_318 = 40;
    st.rx_129 = 50;
    st.rx_dropped = 1;
    st.tx_ok = 8;
    st.tx_failed = 2;
    st.tx_refused = 3;
    st.bus_off_count = 1;
    st.reset_reason = 1;
    st.power_on_count = 42;
    st.uptime_s = 3600;
    st.rx_pin = 2;
    return st;
}
