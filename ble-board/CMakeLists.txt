# BLE Scroll Ack board: M5Stack NanoH2 (ESP32-H2) + Unit Mini CAN.
# ESP-IDF v5.5 app. Build: idf.py build (the target comes from sdkconfig.defaults).
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(scrl-ble-board)
