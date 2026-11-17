# BLE Scroll Ack board

Firmware for a small board that runs Scroll Ack with BLE control and no Wi-Fi.

| Item | Value |
|---|---|
| Hardware | Every hypery ESP32 board, plus the M5Stack NanoH2 + Unit Mini CAN. See [Boards](#boards) |
| Bus | One bus only: Vehicle CAN, 500 kbit/s. On a T-2CAN the other channel stays off the bus |
| Control | BLE only. No Wi-Fi |
| Framework | ESP-IDF v5.5 (built with v5.5.6) |
| Status | **All builds compile. Never ran on hardware, on any board.** |

The board sends one frame ID only: `0x3C2`, the Scroll Ack tick.
The Scroll Ack logic is not copied. This app includes `esp32/.firmware/scroll_ack.h` and compiles `esp32/.firmware/fsd_handler.cpp` from their own place.

## Boards

One source tree, one build per board. The pins come from hypery (`esp32/platformio.ini`, `esp32/.firmware/config.h`), so a board uses the same wires with both firmwares. The board ID goes into the image, and a BLE update refuses an image for another board.

| Build ID | Board ID | Chip | CAN controller | Button |
|---|---|---|---|---|
| `nanoh2-minican` | same | ESP32-H2 | TWAI, Grove GPIO1/2 (see [NanoH2](#nanoh2-before-the-first-power-up-with-the-grove-cable)) | GPIO9 |
| `m5stack-atom` | same | ESP32 | TWAI, TX 22, RX 19 | GPIO0 |
| `m5stack-atom-matrix` | same | ESP32 | TWAI, TX 22, RX 19 | GPIO39 |
| `m5stack-atom-swap-pins` | same | ESP32 | TWAI, TX 19, RX 22 | GPIO0 |
| `esp32-generic-twai` | same | ESP32 | TWAI, TX 21, RX 22 | GPIO0 |
| `esp32-generic-twai-sniffer` | same | ESP32 | TWAI, TX 21, RX 22. Never transmits | GPIO0 |
| `esp32-mcp2515` | same | ESP32 | MCP2515, SCK 18, MISO 19, MOSI 23, CS 5, no INT, 8 MHz crystal | GPIO0 |
| `esp32-lilygo` | same | ESP32 | TWAI, TX 27, RX 26. GPIO23 low (Rs), GPIO16 high (ME2107) | GPIO0 |
| `ttgo-tdisplay` | same | ESP32 | MCP2515, SCK 33, MISO 32, MOSI 25, CS 26, no INT, 8 MHz crystal. LCD off | GPIO0 |
| `lilygo-t2can` | same | ESP32-S3 | can0: TWAI, TX 7, RX 6. MCP2515 held in reset | GPIO0 |
| `lilygo-t2can-can1` | `lilygo-t2can` | ESP32-S3 | can1: MCP2515, SCK 12, MISO 13, MOSI 11, CS 10, INT 8, RST 9, 16 MHz | GPIO0 |
| `lilygo-t2can-fd` | same | ESP32-S3 | can0: TWAI, TX 7, RX 6. The MCP2518FD gets the Configuration mode request over SPI at boot | GPIO0 |
| `lilygo-t2can-fd-can1` | `lilygo-t2can-fd` | ESP32-S3 | can1: MCP2518FD, same SPI pins, polled, 40 MHz crystal | GPIO0 |
| `waveshare-s3-can` | same | ESP32-S3 | TWAI, TX 15, RX 16 | GPIO0 |

T-2CAN channel: can0 is the TWAI on the CAN B terminal, can1 is the SPI controller on the CAN A terminal. The car-tested T-2CAN install had Vehicle CAN on can0 (CAN B), so the plain T-2CAN builds use can0. Hypery's wiring table puts Vehicle CAN on can1: use a `-can1` build for that wiring.

MCP2515 crystal: hypery assumes 8 MHz on a generic module. Read the crystal. For a 16 MHz crystal, set `MCP2515 crystal` in menuconfig.

The LEDs stay dark. The firmware does not drive the addressable LEDs.

MCP2515 and MCP2518FD builds load the 6 IDs of `main/scrl_rx_ids.h` as hardware filters. The MCP2515 has only two receive buffers and cannot hold the full bus rate. So on these builds the Status count `rx[0]` ("all") and the "bus silent" test of the update gate see only those 6 IDs. TWAI builds take every frame.

## Build

1. Install ESP-IDF v5.5 with the targets `esp32`, `esp32s3` and `esp32h2`.
2. Run `. $IDF_PATH/export.sh`.
3. Run `tools/build-boards.sh` in this directory to build every board, or `tools/build-boards.sh <build-id> ...` for some of them.

Each build has its own directory and sdkconfig in `build/<build-id>/`. The script writes two files for each build:
- `build/out/scrl-<build-id>.bin`: the app image, for a BLE update.
- `build/out/scrl-<build-id>-full.bin`: bootloader, partition table and app in one image at offset 0, for the first flash over USB.

The options come from `sdkconfig.defaults` (all boards), `sdkconfig.defaults.<target>` (console, BLE mode) and `boards/<build-id>.defaults` (target and board). `sdkconfig` is not in git.
A build directory keeps its `sdkconfig`. After a change of a defaults file, delete `build/<build-id>/` to apply it.
To change an option of one board, run `idf.py -B build/<build-id> -D SDKCONFIG=build/<build-id>/sdkconfig menuconfig`, then build that board again.

## NanoH2: before the first power-up with the Grove cable

The M5Stack sources disagree on which Grove pin is CAN TX (GPIO1 or GPIO2).
A wrong choice puts two outputs against each other.

The default build (`Not checked yet`) is safe with each wiring:
- It attaches no TX pin. Both Grove pins are inputs.
- It runs in Listen-Only and cannot go to Active.
- It listens on one Grove pin for 3 s, then on the other one, until frames arrive.
- It then writes a line to the event log: `CAN  frames arrive on GPIOx: RX = GPIOx, TX = GPIOy`.

To get a build that can go to Active:
1. Measure continuity with nothing powered: NanoH2 GPIO1 and GPIO2 (Grove pins) to TJA1051T/3 pin 1 (TXD) and pin 4 (RXD) on the Mini CAN. TWAI TX goes to TXD.
2. Compare the result with the log line of the pin probe. They must agree.
3. Run `idf.py -B build/nanoh2-minican -D SDKCONFIG=build/nanoh2-minican/sdkconfig menuconfig`, open `Scroll Ack BLE board`, and set `Grove pins for TWAI TX and RX`.
4. Run `tools/build-boards.sh nanoh2-minican` and flash.

In Listen-Only the TX pin is never attached, in every build.

The other boards have known pins and no probe. In Listen-Only the TWAI backend never attaches the TX pin, and the SPI backends use the listen-only mode of the controller.

## Flash (bench only)

1. Connect the board with a USB data cable. NanoH2: do not feed HPWR+ at the same time.
2. Run `idf.py -B build/<build-id> -D SDKCONFIG=build/<build-id>/sdkconfig flash monitor`. Or write `build/out/scrl-<build-id>-full.bin` at offset 0 with any ESP flasher.
3. Read the line `BLE passkey: nnnnnn` on the console. Write the 6 digits on the label of the board.

The passkey is random. The board makes it at the first boot and keeps it in NVS. It is not in the source code.
The board never changes it by itself. A factory reset erases the settings, the counters and the bonds, and keeps the passkey, so the label stays correct. A full flash erase over USB makes a new random one.

To change the passkey, use the Board page of the control app (command `06`). Only a phone that is paired can do it.
- The new passkey counts from the next new pairing.
- The phones that are paired stay paired. To remove them, use "Forget bonds" too.
- Write the new number on the label. The board shows it again only on the USB console.
In the car, use a charge-only cable.

## Defaults

- Scroll Ack off.
- Listen-Only.
- Prompt mode, pattern `-1` then `+1`, delay 600 ms, gap 100 ms, interval 2500 ms.
- Left wheel (`lr` 0), random Timer interval (`rand` 1).

## Ported TSL6 settings

Since firmware 0.3.0 the board has some settings of the TSL6 module. Each one is a key in the Settings JSON. The key is the TSL6 setting ID, and the value is the TSL6 raw value, so the control app maps them by name. The app shows a setting only when the board reports its key.

| Key | Values | What it does |
|---|---|---|
| `lr` | 0 left wheel (volume), 1 right wheel (set speed), 2 alternate | Wheel of the tick pair. Both ticks of a pair use the same wheel. TSL6 value 3 sends right-wheel tilt bits, not ticks: not ported |
| `rand` | 0 exact, 1 random | Timer mode: 0 = each interval is the mean, 1 = mean +/- 20 % |
| `pattern` 4, 5 | 4 alternate, 5 random | Tick order (TSL6 `jj` 2 and 3). 4 = the first tick changes direction each pair. 5 = a random first direction |

A value above the maximum of a key is refused, not clamped. The values go in one NVS blob (`feat`), in the order of `SCRL_FEATURES` in `main/scrl_settings.h`. Add new keys only at the end.
The shared `scroll_ack.h` of the T-2CAN does not change. `main/scrl_scroll_ext.h` changes the pattern before each step, moves a tick to the right wheel, and sets a fixed Timer interval.

## TX gates

The board sends a tick only when all of these are true. They are the gates of the T-2CAN build.

| Gate | Source |
|---|---|
| Scroll Ack is on | Setting `enabled` |
| Op mode is Active | Setting `op_mode`, and nothing holds the board in Listen-Only (see below) |
| No Autopark pause, no Tesla OTA | `fsd_can_transmit()`; reads `0x39B`, `0x257`, `0x318` |
| `ap_ok` | AP-First, Soft Engage, Abort Guard. All three are off and allow TX. The board has no setting for them yet |
| HW4 | Fixed at build time. A valid `0x39B` must also be seen |
| Vehicle CAN | No `0x370` in the last 2 s |
| Native frame fresh | The source `0x3C2` frame is at most 25 ms old. TWAI: the receive interrupt stamps the frame. MCP2515 and MCP2518FD: the stamp is the last poll (1 ms) that found the controller empty, so it is early, never late |
| DAS sample fresh | The last `0x39B` is at most 1 s old, AP state 6 |

The board stays in Listen-Only, whatever the setting says, while one of these is true:
- 3 bus-off events since power-on. A Settings write that names `op_mode` 1 releases it. The count stays, so the next bus-off locks again.
- A BLE update runs.
- A new image runs and is not confirmed.
- The build has no TX path: the NanoH2 pins are not checked, or the build is `esp32-generic-twai-sniffer`.

`0x318` is not confirmed on Vehicle CAN. If it is absent, the Tesla OTA gate never closes. The status flag `OTA_BLIND` reports this.

## BLE

Name `SCRL-xxxx`. One service, base UUID `e591xxxx-51ba-4d0b-8422-5cb9dc778552`.

| UUID | Name | Properties | Content |
|---|---|---|---|
| `0001` | Service | | |
| `0002` | Info | Read | JSON: `fw`, `build`, `built`, `idf`, `board` (board ID), `chip` (IDF target), `can` (`twai`, `mcp2515` or `mcp2518fd`), `id`, `rst_log` |
| `0003` | Status | Read | JSON, at most 512 bytes. Layout in `main/scrl_status.h` |
| `0004` | Live | Read, Notify (1 Hz) | 19-byte record. Layout in `main/scrl_status.h` |
| `0005` | Settings | Read, Write | JSON. Rules in `main/scrl_settings.h` |
| `0006` | Command | Write | One byte: `01` reset counters, `02` restart, `03` forget bonds, `04` factory reset, `05` confirm image. Or `06` and 6 ASCII digits: set a new passkey |
| `0007` | Log | Notify | Event lines. The last 64 lines come first |
| `0008` | OTA control | Write, Notify | Protocol in `main/scrl_ota.h` |
| `0009` | OTA data | Write without response | Image chunks |

Info is open. Each other characteristic needs an encrypted and authenticated link.

Security:
- LE Secure Connections only, passkey entry, bonding. Pairing without the passkey is refused.
- The board accepts a new bond only in the first 5 minutes after power-on, or for 2 minutes after a press of the button (see [Boards](#boards)).
- A restart without a power cycle (command, update, crash) does not open the window. The one exception: a board with no bond opens it after each start.
- The board keeps 2 bonds. It never deletes a bond by itself. With 2 bonds, a third phone cannot bond.
- Hold the button for 5 s to delete all bonds. The command `03` does the same.
- The board disconnects a phone that does not reach an authenticated link in 30 s.

## BLE update

1. Park the car. The board refuses an update when the car moves or AP is engaged.
2. Send the image (`build/out/scrl-<build-id>.bin`) with the protocol in `main/scrl_ota.h`.
3. The board checks the size, the SHA-256, the image, the chip and the board ID, then restarts into the new image. An image for another board fails with status `07` and the log line `UPD  image is for board ...`.
4. Connect again and write command `05` within 10 minutes. The new image stays in Listen-Only until then.
5. With no confirm, the board restarts and the bootloader goes back to the old image.

Secure Boot is off on purpose. It burns eFuses for good.
If both slots fail, flash the board over USB on the bench.

## Control app

The control app for the board is the TSL6 web app (repo `tsl6-webble`). It shows the board as one more device type: the same tabs as for a TSL6, with only the rows that the board has. This repo has no web page of its own.

| Item | Value |
|---|---|
| Browsers | Chrome or Edge on Android and desktop. Bluefy on iPhone |
| Device name | `SCRL-xxxx` in the Bluetooth list |
| Status | **Checked against a mock board only. The app never talked to a real board.** |

The app repo checks itself against this firmware. Its tool `tools/scrl-fixture.mjs` reads the `#define` values in `main/*.h`, the UUIDs in `scrl_ble.cpp` and the slot size in `partitions.csv`, and it runs `test/scrl_vectors` from this repo (the real status packers, settings reader and passkey reader). After a change of the BLE protocol here, run that tool in the app repo and commit the new fixture there.

## CAN timing

| Controller | Clock, quanta per bit at 500 kbit/s | Sample point | SJW | Source |
|---|---|---|---|---|
| TWAI on ESP32 and ESP32-S3 | 80 MHz, 20 | 80 % | 3 | Same as hypery `TWAI_TIMING_CONFIG_500KBITS()` |
| TWAI on ESP32-H2 | 32 MHz, 16 | 75 % | 2 | ESP-IDF standard timing |
| MCP2515, 8 MHz crystal | 8 quanta | 62.5 % | 1 | autowp-mcp2515 table, as hypery |
| MCP2515, 16 MHz crystal | 16 quanta | 56.25 % | 1 | autowp-mcp2515 table, as hypery |
| MCP2518FD, 40 MHz crystal / 2 | 20 MHz, 40 | 80 % | 8 | ACAN2517FD search, as hypery |

On the H2, 16 quanta cannot make 80 %. The next step is 81.25 %.
The MCP2518FD backend measures its crystal in Listen-Only before the first start, as hypery does. A wrong crystal keeps the chip off the bus.
Watch the event log on the bench and in the car: a line `CAN  bus errors N` that keeps coming can mean a wrong timing.

## Differences from the design note

The design is "4. Firmware plan" in the private note `ble-board/design.md`.

| Topic | Design | This firmware | Why |
|---|---|---|---|
| Grove pin direction | Continuity check before the first power-up | Continuity check, plus a Listen-Only pin probe with both pins as inputs | The probe cannot drive a pin, and it gives a second answer |
| Bus-off | Copy the recovery of `can_driver.cpp` | Delete the controller, wait 250 ms, start it again | A recovery in place would later send ticks that still wait in the controller |
| Bonds | Keep at most 2 | Keep at most 2 and never delete one automatically | The stock handler drops the oldest bond. A stranger could use that to push the owner out |
| Update gate "parked" | Speed 0, no AP state ≥ 3 | Speed 0 from a fresh `0x257`, AP state not 3 to 9. A silent bus passes. A bus that never showed `0x257` has no speed gate | `0x257` is not confirmed on Vehicle CAN, and the bench has no bus |
| New image | Confirm within 10 minutes | Same, and the image stays in Listen-Only until the confirm | An image that nobody checked must not send |
| Info characteristic | Read | Read, open (no pairing) | It holds only the version and the board ID |
| Command | Reset counters, reboot, forget bonds, factory reset | The same, plus `05` confirm image and `06` set passkey | The design names the confirm but gives it no place. The owner asked for a way to change the passkey |
| Passkey | Static, on a label | Random, made at first boot. A factory reset keeps it. A paired phone can set a new one | A new passkey after a factory reset shows only on the USB console, and in the car that locks the owner out of the board |
| Status: bus errors | — | In the event log, not in the Status JSON | The longest Status JSON is 509 of 512 bytes |

## Source layout

| File | Job |
|---|---|
| `main/app_main.cpp` | Start-up, CAN task, housekeeping task, button |
| `main/scrl_can.*` | CAN glue: state, counters, bus-off rule, TX guard |
| `main/scrl_can_hw.h` | Interface to the CAN backend, receive stamp rule |
| `main/scrl_can_twai.cpp` | TWAI backend: receive stamp in the interrupt, NanoH2 pin probe |
| `main/scrl_can_mcp2515.cpp`, `scrl_mcp2515.h` | MCP2515 backend and its register packing (host-tested) |
| `main/scrl_can_mcp2518fd.cpp`, `scrl_mcp2518fd.h` | MCP2518FD backend and its register packing (host-tested) |
| `main/scrl_spi.*` | SPI link for the two SPI backends |
| `main/scrl_rx_ids.h` | The 6 IDs that the frame logic reads: hardware filters of the SPI backends |
| `main/scrl_proc.*` | Frame logic: a cut-down `process_frame()` with the T-2CAN gates |
| `main/scrl_settings.h` | Settings, the ported TSL6 settings and their JSON codec (host-tested) |
| `main/scrl_scroll_ext.h` | Wheel, tick order and fixed interval around the shared Scroll Ack code (host-tested) |
| `main/scrl_status.h` | Status JSON and Live record (host-tested) |
| `main/scrl_log.h`, `scrl_logbuf.*` | Event log ring (host-tested) and its lock |
| `main/scrl_store.*` | NVS: settings with the T-2CAN keys, the ported settings blob, boot record, passkey |
| `main/scrl_passkey.h` | Reader for the set-passkey command (host-tested) |
| `main/scrl_ble.*` | NimBLE service, pairing rules |
| `main/scrl_ota.*` | BLE update and rollback |
| `main/scrl_board.h` | Pin map of each board, ms clock |
| `main/scrl_board_desc.*` | Board ID in the image, checked by a BLE update (host-tested) |
| `main/Kconfig.projbuild`, `boards/` | Board choice and the options of each build |
| `tools/build-boards.sh` | Builds every board |

## Tests

Run `make -C test check` in the repo root. It builds and runs on the host:
- `test_scroll_ack`: the shared Scroll Ack state machine.
- `test_scrl_settings`: the settings codec, the ported settings and the passkey reader.
- `test_scrl_scroll_ext`: the right wheel, the alternate wheel, the alternate and random tick order, and the fixed interval.
- `test_scrl_status`: the status JSON, the Live record and the event log ring.
- `test_scrl_spi`: MCP2515 and MCP2518FD packing and bit timing, the filter list, and the board check of an update.

`test/scrl_vectors` is a host tool, not a test. It prints what the firmware packers make and what the firmware readers accept. The control app repo uses it (see "Control app"). Build it with `make -C test scrl_vectors`.

## Not done yet

- A test of the control app with a real board, on Android, on desktop and in Bluefy.
- A bench replay against the T-2CAN output, and every test on hardware, on every board. The MCP2515 and MCP2518FD backends are new code, not a copy of a library.
- The addressable LEDs (ATOM, T-CAN485, Waveshare) and the T-Display LCD show nothing.
- Settings for AP-First, Soft Engage and Abort Guard.
- Sleep, if the glovebox port stays on while the car sleeps.
