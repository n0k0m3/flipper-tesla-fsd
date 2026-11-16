# The shared Scroll Ack code stays in one place: esp32/.firmware and fsd_logic.
# esp32/.firmware goes first on the include path, so "fsd_handler.h" is the
# firmware header, not fsd_logic/fsd_handler.h (same rule as test/Makefile).
set(FW_DIR "${CMAKE_CURRENT_LIST_DIR}/../../esp32/.firmware")
set(LOGIC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../fsd_logic")

set(srcs
    "app_main.cpp"
    "scrl_ble.cpp"
    "scrl_can.cpp"
    "scrl_logbuf.cpp"
    "scrl_ota.cpp"
    "scrl_proc.cpp"
    "scrl_spi.cpp"
    "scrl_store.cpp"
    "${FW_DIR}/fsd_handler.cpp"
)

# One CAN backend per build (Kconfig SCRL_CAN_*, set by the board choice).
if(CONFIG_SCRL_CAN_MCP2515)
    list(APPEND srcs "scrl_can_mcp2515.cpp")
elseif(CONFIG_SCRL_CAN_MCP2518FD)
    list(APPEND srcs "scrl_can_mcp2518fd.cpp")
else()
    list(APPEND srcs "scrl_can_twai.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "${FW_DIR}" "${LOGIC_DIR}"
    PRIV_REQUIRES
        esp_driver_twai esp_driver_gpio esp_driver_spi esp_timer
        nvs_flash bt app_update esp_app_format esp_partition mbedtls
)

# The shared headers print uint32_t with %u. That is correct on the host and on
# the T-2CAN build. On the RISC-V toolchains uint32_t is "long unsigned int" of
# the same size, so the output is right and only the format check complains.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)
