# BLE Scroll Ack board

Firmware for a small board that runs Scroll Ack with BLE control and no Wi-Fi.

| Item | Value |
|---|---|
| Hardware | Every hypery ESP32 board, plus the M5Stack NanoH2 + Unit Mini CAN. See [Boards](#boards) |
| Bus | One bus only: Vehicle CAN, 500 kbit/s. On a T-2CAN the other channel stays off the bus |
| Control | BLE only. No Wi-Fi |
| Framework | ESP-IDF v5.5 (built with v5.5.6) |
| Status | **All builds compile. The NanoH2 (`nanoh2-minican`, TX GPIO2, RX GPIO1, 0.3.2) passed every check of [Validation](#validation) on 2026-11-20, in the car. Other boards: only BLE pairing ran (a T-2CAN on the bench).** |

The board sends one frame ID only: `0x3C2`, the Scroll Ack tick.
The Scroll Ack logic is not copied. This app includes `esp32/.firmware/scroll_ack.h` and compiles `esp32/.firmware/fsd_handler.cpp` from their own place.

## Boards

One source tree, one build per board. The pins come from hypery (`esp32/platformio.ini`, `esp32/.firmware/config.h`), so a board uses the same wires with both firmwares. The board ID goes into the image, and a BLE update refuses an image for another board.

| Build ID | Board ID | Chip | CAN controller | Button |
|---|---|---|---|---|
| `nanoh2-minican` | same | ESP32-H2 | TWAI, Grove: TX GPIO2, RX GPIO1 (see [NanoH2](#nanoh2-before-the-first-power-up-with-the-grove-cable)) | GPIO9 |
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

## Flash site

`flash-site/` is a small web page that flashes the full image over USB (Web Serial, esptool-js 0.7.0), resets the board, reads its USB console and shows the BLE passkey in large digits. It also checks the board ID in the passkey line. "Read passkey only" does the same for a board that has the firmware. It needs Chrome or Edge on a computer.

The same page also updates a board over Bluetooth (Web Bluetooth). It worked from Chrome on a computer (2026-11-20). Chrome on Android is not tested. In Bluefy on an iPhone the board got none of the data writes and the link then blocked, so the page does not send from an iPhone or iPad. There it can still connect and confirm a new image. It sends the app image with the protocol of `main/scrl_ota.h`, connects again after the restart, and shows the frame count, the CAN pin state and the time until the rollback. "Confirm the new image" makes the image permanent. `flash-site/scrl.js` is a copy of the codec of the control app (tsl6-webble `public/lib/scrl.js`). `flash-site/ble-update.js` holds the update loop of the app.

1. Run `tools/build-boards.sh`.
2. Run `tools/make-flash-site.py`. It writes `build/flash-site/` with every full image and app image, their SHA-256 and the board ID. It stops when a build has no image or no text in `flash-site/boards.json`, or when the UUIDs, the commands or the OTA values in `flash-site/scrl.js` differ from `main/scrl_ble.h` and `main/scrl_ota.h`.
3. Deploy `build/flash-site/` (preview: `surge build/flash-site scrl-flash-preview.surge.sh`).

The site is public to anyone who has the address. It publishes the compiled images, not the source.
"Erase the whole flash first" is on by default: for a first install the board then makes a new passkey. The page checks the SHA-256 of the download and the chip before it writes.

## NanoH2: before the first power-up with the Grove cable

The M5Stack sources disagree on which Grove pin is CAN TX (GPIO1 or GPIO2).
A wrong choice puts two outputs against each other.

The `nanoh2-minican` build uses **TX = GPIO2, RX = GPIO1**. This comes from a Unit Mini CAN on the NanoH2 Grove port (2026-11-20): the pin probe, a Listen-Only test of both pin choices, and the full validation in the car.

With another cable or another unit:
1. Measure continuity with nothing powered: NanoH2 GPIO2 must go to TJA1051T/3 pin 1 (TXD), and GPIO1 to pin 4 (RXD) on the Mini CAN.
2. If GPIO1 goes to TXD, do not flash `nanoh2-minican`. Run `idf.py -B build/nanoh2-minican -D SDKCONFIG=build/nanoh2-minican/sdkconfig menuconfig`, open `Scroll Ack BLE board`, set `Grove pins for TWAI TX and RX` to `TX = GPIO1, RX = GPIO2`, and run `tools/build-boards.sh nanoh2-minican`.
3. If you are not sure, set `Not checked yet` instead. That build attaches no TX pin, makes both Grove pins inputs, and cannot go to Active. It listens on one Grove pin for 3 s, then on the other one, until frames arrive. It then writes a line to the event log: `CAN  frames arrive on GPIOx: RX = GPIOx, TX = GPIOy`.
4. After the first flash, stay in Listen-Only and make sure that frames arrive. A wrong pin choice receives nothing. Do not set Active then: in Active it drives the TX pin against the RXD output of the transceiver.

In Listen-Only the TX pin is never attached, in every build.

The other boards have known pins and no probe. In Listen-Only the TWAI backend never attaches the TX pin, and the SPI backends use the listen-only mode of the controller.

## Flash (bench only)

1. Connect the board with a USB data cable. NanoH2: do not feed HPWR+ at the same time.
2. Run `idf.py -B build/<build-id> -D SDKCONFIG=build/<build-id>/sdkconfig flash monitor`. Or write `build/out/scrl-<build-id>-full.bin` at offset 0 with any ESP flasher.
3. Read the line `BLE passkey: nnnnnn (board <id>)` on the console. The line repeats every 3 s in the first 2 minutes after a boot and while the pairing window is open. The USB flash site reads it by itself. Write the 6 digits on the label of the board.

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
  Prompt mode acts on hands-on 3, 4 and 5. It sends one pair for each prompt level, and one more pair when the nag escalates (3 → 4 → 5), as the TSL6 prompt build v2. The owner tested levels 4 and 5 on the car with the TSL6 (2026-11-18). This firmware is not car-tested yet.
- Left wheel (`lr` 0), random Timer interval (`rand` 1).

## Ported TSL6 settings

Since firmware 0.3.0 the board has some settings of the TSL6 module. Each one is a key in the Settings JSON. The key is the TSL6 setting ID, and the value is the TSL6 raw value, so the control app maps them by name. The app shows a setting only when the board reports its key.

| Key | Values | What it does |
|---|---|---|
| `lr` | 0 left wheel (volume), 1 right wheel (set speed), 2 alternate | Wheel of the tick pair. Both ticks of a pair use the same wheel. TSL6 value 3 sends right-wheel tilt bits, not ticks: not ported |
| `rand` | 0 exact, 1 random | Timer mode: 0 = each interval is the mean, 1 = mean +/- 20 % |
| `pattern` 4, 5 | 4 alternate, 5 random | Tick order (TSL6 `jj` 2 and 3). 4 = the first tick changes direction each pair. 5 = a random first direction |

A value above the maximum of a key is refused, not clamped. The values go in one NVS blob (`feat`), in the order of `SCRL_FEATURES` in `main/scrl_settings.h`. Add new keys only at the end.
The board uses the shared `scroll_ack.h` of the T-2CAN as it is. `main/scrl_scroll_ext.h` changes the pattern before each step, moves a tick to the right wheel, and sets a fixed Timer interval.

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
| `0006` | Command | Write | One byte: `01` reset counters, `02` restart, `03` forget bonds, `04` factory reset, `05` confirm image. Or `06` and 6 ASCII digits: set a new passkey. Capture (0.4.0): `07` + mode (0 plan IDs, 1 all IDs, 2 + u16 ID list) starts, `08` stops, `09` + step marks a step |
| `0007` | Log | Notify | Event lines. The last 64 lines come first |
| `0008` | OTA control | Write, Notify | Protocol in `main/scrl_ota.h` |
| `0009` | OTA data | Write without response | Image chunks |
| `000A` | Capture | Notify | Capture packets (0.4.0, TWAI boards). The board stays in Listen-Only while a capture runs. Layout in `main/scrl_capture.h` |

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
| `flash-site/`, `tools/make-flash-site.py` | Flash site (USB flash, Bluetooth update): page, board texts, generator |
| `validate-site/`, `tools/make-validate-site.py` | Validation site: page, status rules, generator |

## Tests

Run `make -C test check` in the repo root. It builds and runs on the host:
- `test_scroll_ack`: the shared Scroll Ack state machine.
- `test_scrl_settings`: the settings codec, the ported settings and the passkey reader.
- `test_scrl_scroll_ext`: the right wheel, the alternate wheel, the alternate and random tick order, and the fixed interval.
- `test_scrl_status`: the status JSON, the Live record and the event log ring.
- `test_scrl_spi`: MCP2515 and MCP2518FD packing and bit timing, the filter list, and the board check of an update.

The rule engine of the validation site has its own test. It runs on the output of `test/scrl_vectors`: run `make -C test scrl_vectors && node ble-board/validate-site/engine.test.mjs` in the repo root.

`test/scrl_vectors` is a host tool, not a test. It prints what the firmware packers make and what the firmware readers accept. The control app repo uses it (see "Control app"). Build it with `make -C test scrl_vectors`.

## Validation

Do these checks in order on each board, one group at a time. The [validation site](#validation-site) shows the next check, ticks the checks that the board status can prove, and keeps a tick for the other checks.

Rules:
- Each ID is stable. The site keeps the result under the ID. Do not rename an ID. Add new rows with new IDs.
- The Board column is `all`, or the board IDs (Info `board`, `SCRL_BOARD_ID` in `main/Kconfig.projbuild`) that the row is for, with commas between them.
- Go to Active only in the control app. The site only reads the board.
- Do not connect two boards to the bus at the same time. Two Active boards send double ticks.

### Bench

No car. USB power from a computer.

| ID | Board | Action | Pass when |
|---|---|---|---|
| `nanoh2-continuity` | nanoh2-minican | With nothing powered, measure continuity from NanoH2 GPIO1 and GPIO2 to TJA1051T/3 pin 1 (TXD) and pin 4 (RXD) on the Mini CAN. Write down the GPIO that goes to TXD | GPIO2 goes to TXD and GPIO1 to RXD, as in the `nanoh2-minican` build. Else see [NanoH2](#nanoh2-before-the-first-power-up-with-the-grove-cable) |
| `usb-flash` | all | Flash the full image over USB with the flash site, or with `idf.py flash` | The board reads its Info over BLE with the firmware version of this README |
| `passkey-label` | all | Read the line `BLE passkey: nnnnnn (board <id>)` on the USB console. Write the 6 digits on the label | The label has the passkey |
| `ble-pair` | all | Pair a phone in the first 5 minutes after power-on. Type the passkey | A protected read (Status) works |
| `defaults-safe` | all | Read the settings of a new board | Scroll Ack is off and the op mode is Listen-Only |
| `pairing-window` | all | Wait more than 5 minutes after power-on. Try to pair a second phone. Then press the button and try again | The board refuses the bond before the press, and accepts it in the 2 minutes after the press |
| `forget-bonds` | all | Hold the button for 5 s | The phone must pair again |
| `factory-reset` | all | Send a factory reset from the control app. Pair again with the same label | The passkey on the label still works. The settings are the defaults |
| `ble-update` | all | Send an app image over BLE. Connect again and confirm it in 10 minutes | The board runs the new image and the image is confirmed |
| `update-rollback` | all | Send an app image over BLE and do not confirm it | After 10 minutes the board runs the old image again |
| `update-wrong-board` | all | Send an app image for another board | The update stops with status `07` and the log line `UPD  image is for board ...` |
| `bench-replay` | all | Replay a car capture into the board with a USB CAN adapter. Record the bus with a second adapter. Compare the `0x3C2` ticks with the T-2CAN output for the same replay | The ticks agree with the T-2CAN ticks |

### Car, Listen-Only

The board replaces the T-2CAN on the same Vehicle CAN wires. USB power from the glovebox port, with a charge-only cable.

| ID | Board | Action | Pass when |
|---|---|---|---|
| `car-install` | all | Unplug the T-2CAN. Connect this board to the same Vehicle CAN wires. Power it with a charge-only cable | The board is on the bus and the T-2CAN is off the bus |
| `can-running` | all | Wake the car. Connect with the site | The CAN controller runs |
| `frames-arrive` | all | Keep the car awake | Frames arrive |
| `nanoh2-rx-pin` | nanoh2-minican | Read the receive pin in the status | The status names GPIO1 or GPIO2 |
| `nanoh2-pins-agree` | nanoh2-minican | Compare the receive pin with the continuity result | The receive pin is the GPIO that goes to RXD, and frames arrive on it |
| `vehicle-bus` | all | Keep the car awake for some minutes | Many frames arrive and no `0x370` arrives. A `0x370` means a wrong bus: unplug the board |
| `see-3c2` | all | Use a steering wheel scroll wheel | `0x3C2` frames arrive |
| `see-39b` | all | Drive with FSD on | A valid `0x39B` arrives (HW4 seen) |
| `see-257` | all | Drive | `0x257` arrives. This confirms that the Autopark pause can read the speed on Vehicle CAN |
| `see-318` | all | Drive. Watch the `0x318` count | `0x318` arrives, so the Tesla OTA gate can close. If it never arrives in a full drive, tick it and write "absent" in the note |
| `drive-listen` | all | Drive once with FSD on, in Listen-Only. Compare the status with the T-2CAN diagnostics of earlier drives | AP state 6 shows while the board is in Listen-Only |
| `no-bus-errors` | all | Read the status after 10 minutes or more on the bus | No bus-off event and no dropped frame |
| `nanoh2-pins-build` | nanoh2-minican | Make sure that the board runs a build that names the CAN pins: `nanoh2-minican`, or your own build with the checked pin choice | The status says that the build names the CAN pins |

### Car, Active

| ID | Board | Action | Pass when |
|---|---|---|---|
| `go-active` | all | In the control app, turn Scroll Ack on and set Active | The effective op mode is Active |
| `tx-allowed` | all | Drive with FSD on | All TX gates are open at the same time |
| `timer-ticks` | all | Set Timer mode. Drive with FSD on | The board sends ticks in Timer mode |
| `prompt-3` | all | Set Prompt mode. Drive with FSD on until a hands-on prompt shows | The board sends ticks at hands-on 3 |
| `prompt-4` | all | Let a prompt go on to level 4 | The board sends ticks at hands-on 4 |
| `prompt-5` | all | Let a prompt go on to level 5 | The board sends ticks at hands-on 5 |
| `prompt-clears` | all | Look at the screen at each prompt | The prompt clears after the ticks. On each new level (3 → 4 → 5) one more pair goes out |
| `tx-no-fail` | all | Read the status after the drive | Ticks went out and no tick failed |
| `car-no-alerts` | all | After the drive, look for new alerts and faults on the car screen | No new alert and no new fault |
| `sleep-check` | all | Let the car sleep. See if the glovebox port stays on | You know if the board stays powered while the car sleeps |

### Control app

| ID | Board | Action | Pass when |
|---|---|---|---|
| `app-android` | all | Connect with the control app in Chrome on Android. Read Status, change one setting, read the log | Each step works |
| `app-desktop` | all | Do the same in Chrome or Edge on a computer | Each step works |
| `app-bluefy` | all | Do the same in Bluefy on an iPhone | Each step works |

### Validation site

`validate-site/` is a web page for the list above. It needs Chrome or Edge with Web Bluetooth, or Bluefy on an iPhone.

1. Run `tools/make-validate-site.py`. It reads the tables of this section, `validate-site/rules.json` (the checks that the board status can prove) and the `SCRL_FLAG_*` bits in `main/scrl_status.h`. It writes `build/validate-site/`. It stops when a rule names an ID that is not in a table, or a flag that does not exist.
2. Deploy `build/validate-site/` (preview: `surge build/validate-site scrl-validate-preview.surge.sh`).

The page reads Info, Status and Settings, and the Live record once each second. It writes nothing to the board.
A check that the status proves gets a pass with the firmware version and the time. The pass stays when the counters reset at the next power-on. A pass from older firmware shows as old.
The other checks have a tick and a note.
The results stay in the browser. Use Export and Import to move them between the computer (bench) and the phone (car).

## Not done yet

- The validation above, on every board except the NanoH2. The MCP2515 and MCP2518FD backends are new code, not a copy of a library.
- The addressable LEDs (ATOM, T-CAN485, Waveshare) and the T-Display LCD show nothing.
- Settings for AP-First, Soft Engage and Abort Guard.
- Sleep, if the glovebox port stays on while the car sleeps.
