# BLE Scroll Ack board firmware. ESP-IDF v5.5 app for every hypery board and
# the NanoH2. The board is a Kconfig choice: build one with
#   tools/build-boards.sh <board-id>
# (it applies boards/<board-id>.defaults), or all with tools/build-boards.sh.
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(scrl-ble-board)
