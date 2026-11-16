# BLE Scroll Ack board

Firmware for a small board that runs Scroll Ack with BLE control and no Wi-Fi.

| Item | Value |
|---|---|
| Hardware | M5Stack NanoH2 (ESP32-H2, 4 MB flash) + M5Stack Unit Mini CAN (TJA1051T/3), one Grove cable |
| Power | USB into the NanoH2 USB-C port. Leave the Mini CAN HPWR+ terminal open |
| Bus | Vehicle CAN only, 500 kbit/s |
| Framework | ESP-IDF v5.5 (built with v5.5.6) |
| Status | **Builds. Never ran on hardware.** |

The board sends one frame ID only: `0x3C2`, the Scroll Ack tick.
The Scroll Ack logic is not copied. This app includes `esp32/.firmware/scroll_ack.h` and compiles `esp32/.firmware/fsd_handler.cpp` from their own place.

## Build

1. Install ESP-IDF v5.5 with the `esp32h2` target.
2. Run `. $IDF_PATH/export.sh`.
3. Run `idf.py build` in this directory.

`sdkconfig.defaults` sets the target and all options. `sdkconfig` is not in git.
After a change of `sdkconfig.defaults`, run `idf.py set-target esp32h2` to apply it.

## Before the first power-up with the Grove cable

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
3. Run `idf.py menuconfig`, open `Scroll Ack BLE board`, and set `Grove pins for TWAI TX and RX`.
4. Build and flash.

In Listen-Only the TX pin is never attached, in every build.

## Flash (bench only)

1. Connect the NanoH2 with a USB-C data cable. Do not feed HPWR+ at the same time.
2. Run `idf.py flash monitor`.
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
| Native frame fresh | The source `0x3C2` frame is at most 25 ms old. The receive interrupt stamps the frame |
| DAS sample fresh | The last `0x39B` is at most 1 s old, AP state 6 |

The board stays in Listen-Only, whatever the setting says, while one of these is true:
- 3 bus-off events since power-on. A Settings write that names `op_mode` 1 releases it. The count stays, so the next bus-off locks again.
- A BLE update runs.
- A new image runs and is not confirmed.
- The build does not name the CAN pins.

`0x318` is not confirmed on Vehicle CAN. If it is absent, the Tesla OTA gate never closes. The status flag `OTA_BLIND` reports this.

## BLE

Name `SCRL-xxxx`. One service, base UUID `e591xxxx-51ba-4d0b-8422-5cb9dc778552`.

| UUID | Name | Properties | Content |
|---|---|---|---|
| `0001` | Service | | |
| `0002` | Info | Read | JSON: `fw`, `build`, `built`, `idf`, `board`, `chip`, `id`, `rst_log` |
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
- The board accepts a new bond only in the first 5 minutes after power-on, or for 2 minutes after a press of the NanoH2 button.
- A restart without a power cycle (command, update, crash) does not open the window. The one exception: a board with no bond opens it after each start.
- The board keeps 2 bonds. It never deletes a bond by itself. With 2 bonds, a third phone cannot bond.
- Hold the button for 5 s to delete all bonds. The command `03` does the same.
- The board disconnects a phone that does not reach an authenticated link in 30 s.

## BLE update

1. Park the car. The board refuses an update when the car moves or AP is engaged.
2. Send the image (`build/scrl-ble-board.bin`) with the protocol in `main/scrl_ota.h`.
3. The board checks the size, the SHA-256 and the image, then restarts into the new image.
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

| Item | This board | T-2CAN |
|---|---|---|
| Clock, quanta per bit at 500 kbit/s | 32 MHz, 16 | 80 MHz, 20 |
| Sample point | 75 % | 80 % |
| SJW | 2 quanta (12.5 %) | 3 quanta (15 %) |

16 quanta cannot make 80 %. The value 75 % is the ESP-IDF standard timing for this chip. The next step is 81.25 %.
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
| `main/scrl_can.*` | TWAI glue: receive stamp in the interrupt, Listen-Only, bus-off rule, TX guard |
| `main/scrl_proc.*` | Frame logic: a cut-down `process_frame()` with the T-2CAN gates |
| `main/scrl_settings.h` | Settings and their JSON codec (host-tested) |
| `main/scrl_status.h` | Status JSON and Live record (host-tested) |
| `main/scrl_log.h`, `scrl_logbuf.*` | Event log ring (host-tested) and its lock |
| `main/scrl_store.*` | NVS: settings with the T-2CAN keys, boot record, passkey |
| `main/scrl_passkey.h` | Reader for the set-passkey command (host-tested) |
| `main/scrl_ble.*` | NimBLE service, pairing rules |
| `main/scrl_ota.*` | BLE update and rollback |
| `main/scrl_board.h` | Pin map, ms clock |

## Tests

Run `make -C test check` in the repo root. It builds and runs on the host:
- `test_scroll_ack`: the shared Scroll Ack state machine.
- `test_scrl_settings`: the settings codec and the passkey reader.
- `test_scrl_status`: the status JSON, the Live record and the event log ring.

`test/scrl_vectors` is a host tool, not a test. It prints what the firmware packers make and what the firmware readers accept. The control app repo uses it (see "Control app"). Build it with `make -C test scrl_vectors`.

## Not done yet

- A test of the control app with a real board, on Android, on desktop and in Bluefy.
- A bench replay against the T-2CAN output, and every test on hardware.
- Settings for AP-First, Soft Engage and Abort Guard.
- Sleep, if the glovebox port stays on while the car sleeps.
