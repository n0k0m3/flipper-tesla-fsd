# Host unit tests for the Tesla FSD protocol core.
# Compiles fsd_logic/fsd_handler.c on the host — no Flipper SDK / SPI needed.
# test_esp32_core does the same for esp32/.firmware/fsd_handler.cpp (no Arduino).
# test_captive_probe covers esp32/.firmware/captive_probe.h (captive-portal logic).
# test_can_mcp2518fd covers esp32/.firmware/can_mcp2518fd.h (T-2CAN FD driver logic).
# test_web_cmd covers esp32/.firmware/web_cmd.h (dashboard command parser).
# test_web_origin covers esp32/.firmware/web_origin.h (whose commands are taken).
# test_can_counters covers esp32/.firmware/can_counters.h (driver counter bookkeeping).
# test_loop_health covers esp32/.firmware/loop_health.h (loop() pass timing).
# test_power_log covers esp32/.firmware/power_log.h (reset reason refinement).
# test_web_page.py covers esp32/gen_web_page.py (the gzip copy of the dashboard).
# test_page_send covers esp32/.firmware/page_send.h (the page's send budget).
# test_scrl_settings and test_scrl_status cover the BLE board (ble-board/main):
# the settings codec, the status packers and the event log ring.
# test_scrl_scroll_ext covers the BLE board additions to Scroll Ack (wheel,
# tick order, fixed interval).
# test_scrl_spi covers the BLE board SPI backends (MCP2515, MCP2518FD packing
# and timing), the hardware filter list and the board check of an update.
#
#   make -C test check

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O0 -g
CXX     ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -g
ROOT    := ..
ESP32   := $(ROOT)/esp32/.firmware
SRC     := test_fsd_core.c $(ROOT)/fsd_logic/fsd_handler.c $(ROOT)/fsd_logic/fsd_profile.c $(ROOT)/fsd_logic/fsd_power.c
BLEBOARD := $(ROOT)/ble-board/main
ESP32_SRC := test_esp32_core.cpp $(ESP32)/fsd_handler.cpp

test_fsd_core: $(SRC) $(ROOT)/fsd_logic/fsd_handler.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_checksum.h $(ROOT)/fsd_logic/fsd_can_ops.h $(ROOT)/fsd_logic/fsd_perf.h $(ROOT)/fsd_logic/fsd_state.h $(ROOT)/fsd_logic/fsd_capture.h $(ROOT)/fsd_logic/fsd_profile.h $(ROOT)/fsd_logic/fsd_events.h $(ROOT)/fsd_logic/fsd_blackbox_summary.h $(ROOT)/fsd_logic/fsd_blackbox_filter.h $(ROOT)/fsd_logic/fsd_capability.h $(ROOT)/fsd_logic/fsd_profile_db.h $(ROOT)/fsd_logic/fsd_ota.h $(ROOT)/fsd_logic/fsd_autopark.h $(ROOT)/fsd_logic/fsd_tpms.h $(ROOT)/fsd_logic/fsd_power.h $(ROOT)/fsd_logic/fsd_steer.h $(ROOT)/fsd_logic/fsd_bms.h $(ROOT)/fsd_logic/fsd_freshness.h
	$(CC) $(CFLAGS) -I$(ROOT)/fsd_logic $(SRC) -o $@ -lm

# esp32/.firmware goes first on the include path so "fsd_handler.h" is the
# firmware header, not fsd_logic/fsd_handler.h.
test_esp32_core: $(ESP32_SRC) $(ESP32)/fsd_handler.h $(ESP32)/config.h $(ESP32)/can_signals.h $(ESP32)/blackbox_flush.h $(ESP32)/blackbox_ram.h $(ESP32)/blackbox_name.h $(ESP32)/gear_tx_route.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_state.h $(ROOT)/fsd_logic/fsd_checksum.h $(ROOT)/fsd_logic/fsd_can_ops.h $(ROOT)/fsd_logic/fsd_ota.h $(ROOT)/fsd_logic/fsd_autopark.h  $(ROOT)/fsd_logic/fsd_perf.h $(ROOT)/fsd_logic/fsd_cp_status.h $(ROOT)/fsd_logic/fsd_tpms.h $(ROOT)/fsd_logic/fsd_steer.h $(ROOT)/fsd_logic/fsd_bms.h $(ROOT)/fsd_logic/fsd_freshness.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) -I$(ROOT)/fsd_logic $(ESP32_SRC) -o $@

test_can_capture_filter: test_can_capture_filter.cpp $(ESP32)/can_capture_filter.h $(ESP32)/can_driver.h $(ESP32)/fsd_handler.h $(ESP32)/config.h $(ROOT)/fsd_logic/fsd_types.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) -I$(ROOT)/fsd_logic test_can_capture_filter.cpp -o $@

test_captive_probe: test_captive_probe.cpp $(ESP32)/captive_probe.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_captive_probe.cpp -o $@

test_can_mcp2518fd: test_can_mcp2518fd.cpp $(ESP32)/can_mcp2518fd.h $(ESP32)/can_capture_filter.h $(ESP32)/can_driver.h $(ESP32)/fsd_handler.h $(ESP32)/config.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_can_ops.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) -I$(ROOT)/fsd_logic test_can_mcp2518fd.cpp -o $@

test_web_cmd: test_web_cmd.cpp $(ESP32)/web_cmd.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_web_cmd.cpp -o $@

test_web_origin: test_web_origin.cpp $(ESP32)/web_origin.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_web_origin.cpp -o $@

test_can_counters: test_can_counters.cpp $(ESP32)/can_counters.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_can_counters.cpp -o $@

test_loop_health: test_loop_health.cpp $(ESP32)/loop_health.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_loop_health.cpp -o $@

test_power_log: test_power_log.cpp $(ESP32)/power_log.h $(ROOT)/fsd_logic/fsd_power.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_power_log.cpp -o $@

test_page_send: test_page_send.cpp $(ESP32)/page_send.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) test_page_send.cpp -o $@

# test_rx_whitelist covers fsd_logic/fsd_rx_whitelist.h (the derived RX ID
# whitelist — the safety gate for the opt-in acceptance-filter mode).
test_rx_whitelist: test_rx_whitelist.c $(ROOT)/fsd_logic/fsd_rx_whitelist.h $(ROOT)/fsd_logic/fsd_state.h $(ROOT)/fsd_logic/fsd_blackbox_filter.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_perf.h $(ROOT)/fsd_logic/fsd_tpms.h
	$(CC) $(CFLAGS) -I$(ROOT)/fsd_logic test_rx_whitelist.c -o $@ -lm

# test_scroll_ack covers esp32/.firmware/scroll_ack.h (the DAS Scroll Ack state
# machine — trigger values, output bytes, freshness, idle validation, one pair).
test_scroll_ack: test_scroll_ack.cpp $(ESP32)/scroll_ack.h $(ESP32)/can_driver.h $(ESP32)/fsd_handler.h $(ESP32)/config.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_state.h
	$(CXX) $(CXXFLAGS) -I$(ESP32) -I$(ROOT)/fsd_logic test_scroll_ack.cpp -o $@

# The BLE board (ble-board/, ESP32-H2) keeps its settings codec and its status
# packers in ESP-IDF-free headers. They include scroll_ack.h, so the include
# path is the same as for test_scroll_ack.
SCRL_DEPS := $(ESP32)/scroll_ack.h $(ESP32)/can_driver.h $(ESP32)/fsd_handler.h $(ESP32)/config.h $(ROOT)/fsd_logic/fsd_types.h $(ROOT)/fsd_logic/fsd_state.h

test_scrl_settings: test_scrl_settings.cpp $(BLEBOARD)/scrl_settings.h $(BLEBOARD)/scrl_passkey.h $(SCRL_DEPS)
	$(CXX) $(CXXFLAGS) -I$(BLEBOARD) -I$(ESP32) -I$(ROOT)/fsd_logic test_scrl_settings.cpp -o $@

test_scrl_scroll_ext: test_scrl_scroll_ext.cpp $(BLEBOARD)/scrl_scroll_ext.h $(BLEBOARD)/scrl_settings.h $(SCRL_DEPS)
	$(CXX) $(CXXFLAGS) -I$(BLEBOARD) -I$(ESP32) -I$(ROOT)/fsd_logic test_scrl_scroll_ext.cpp -o $@

test_scrl_spi: test_scrl_spi.cpp $(BLEBOARD)/scrl_mcp2515.h $(BLEBOARD)/scrl_mcp2518fd.h $(BLEBOARD)/scrl_rx_ids.h $(BLEBOARD)/scrl_board_desc.h $(ESP32)/can_mcp2518fd.h $(ESP32)/can_capture_filter.h $(SCRL_DEPS) $(ROOT)/fsd_logic/fsd_can_ops.h
	$(CXX) $(CXXFLAGS) -I$(BLEBOARD) -I$(ESP32) -I$(ROOT)/fsd_logic test_scrl_spi.cpp -o $@

test_scrl_status: test_scrl_status.cpp scrl_sample.h $(BLEBOARD)/scrl_status.h $(BLEBOARD)/scrl_log.h $(SCRL_DEPS)
	$(CXX) $(CXXFLAGS) -I$(BLEBOARD) -I$(ESP32) -I$(ROOT)/fsd_logic test_scrl_status.cpp -o $@

# scrl_vectors prints what the BLE board packers make and what its settings
# reader accepts. The control app (repo tsl6-webble, tools/scrl-fixture.mjs)
# runs it to check itself against this firmware. It is a tool, not a test.
scrl_vectors: scrl_vectors.cpp scrl_sample.h $(BLEBOARD)/scrl_status.h $(BLEBOARD)/scrl_settings.h $(BLEBOARD)/scrl_passkey.h $(BLEBOARD)/scrl_ble.h $(SCRL_DEPS)
	$(CXX) $(CXXFLAGS) -I$(BLEBOARD) -I$(ESP32) -I$(ROOT)/fsd_logic scrl_vectors.cpp -o $@

check: test_fsd_core test_esp32_core test_can_capture_filter test_captive_probe test_can_mcp2518fd test_web_cmd test_web_origin test_can_counters test_loop_health test_power_log test_page_send test_rx_whitelist test_scroll_ack test_scrl_settings test_scrl_status test_scrl_spi test_scrl_scroll_ext
	./test_fsd_core
	./test_esp32_core
	./test_can_capture_filter
	./test_captive_probe
	./test_can_mcp2518fd
	./test_web_cmd
	./test_web_origin
	./test_can_counters
	./test_loop_health
	./test_power_log
	./test_page_send
	./test_rx_whitelist
	./test_scroll_ack
	./test_scrl_settings
	./test_scrl_status
	./test_scrl_spi
	./test_scrl_scroll_ext
	CXX="$(CXX)" python3 test_web_page.py

clean:
	rm -rf test_fsd_core test_fsd_core.dSYM test_esp32_core test_esp32_core.dSYM test_can_capture_filter test_can_capture_filter.dSYM test_captive_probe test_captive_probe.dSYM test_can_mcp2518fd test_can_mcp2518fd.dSYM test_web_cmd test_web_cmd.dSYM test_web_origin test_web_origin.dSYM test_can_counters test_can_counters.dSYM test_loop_health test_loop_health.dSYM test_power_log test_power_log.dSYM test_page_send test_page_send.dSYM test_rx_whitelist test_rx_whitelist.dSYM test_scroll_ack test_scroll_ack.dSYM test_scrl_settings test_scrl_settings.dSYM test_scrl_status test_scrl_status.dSYM test_scrl_spi test_scrl_spi.dSYM test_scrl_scroll_ext test_scrl_scroll_ext.dSYM scrl_vectors scrl_vectors.dSYM

.PHONY: check clean
