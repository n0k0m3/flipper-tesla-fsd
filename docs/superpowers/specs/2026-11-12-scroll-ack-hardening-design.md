# Scroll Ack design

Status: implemented on branch `scroll-ack-modes` (commits `71c75d3`..`ec2c0a1`).
Validated on 2026-11-12 with build `v2.17-beta.6-scroll-modes-2` on a 2023 Model Y
HW4, firmware 2026.38, LILYGO T-2CAN with only Vehicle CAN connected (X179 9/10
on CANB, `can0`). The feature stays experimental, HW4-only, opt-in, and default OFF.

This document replaces the earlier "hardening" design of 2026-11-12. That design
used compile-time bus routing and one `+1` tick per prompt. The routing caused the
failed car tests (see "History").

## Intent

Clear the Autopilot hands-on prompt with left scroll-wheel ticks on `0x3C2`
(`VCLEFT_switchStatus`). The ticks must be byte-identical to a thumb input, must go
only to Vehicle CAN, and must never overwrite a real driver input.

## Evidence

All captures are from the owner's car on 2026-11-11 (Listen-Only, candump format).

- A manual left tick (`+1` or `-1`) changed the `0x39B` hands-on state from 3 to 1
  in 33-36 ms. AP state was 6 the whole time.
- The installed EVAAM TSL6 sent one left pair every 2.0-3.0 s (median 2.55 s, 23
  pairs in 57 s) while AP state was 6: `-1`, then `+1` about 100 ms later. It did
  not wait for the prompt. Hands-on stayed at 1 or 2 and never reached 3.
- TSL6 frames: `29553f0000000080` (`-1`) and `2955010000000080` (`+1`). The native
  idle page-1 frame is `2955000000000080`. Each tick is an extra frame sent right
  after a native frame.
- Vehicle CAN carries both `0x3C2` and `0x39B`. Chassis CAN carries a forwarded copy
  of `0x3C2` and also `0x370`. Party CAN has no `0x3C2`.

## Goals

- Send ticks only on Vehicle CAN, with any wiring of a one- or two-channel board.
- Offer two triggers: on the prompt, and on a timer (TSL6 pattern).
- Offer four tick patterns, and random timing around configurable means.
- Never overwrite a driver input on the wheel.
- Keep every existing TX gate.
- Show why the feature does not send, through status diagnostics.

## Non-goals

- No change to the Flipper-only ScrollPress AP feature.
- No `0x370` torque injection. The torque nag killer caused stability faults on the
  same car.
- No manual bus-mapping setting. The bus rule finds Vehicle CAN by itself.

## Anti-goals

- Do not send on a bus that carries `0x370` (Chassis or Party CAN).
- Do not generate a `0x3C2` frame from nothing. Always copy a fresh native frame.
- Do not send a tick while the native frame shows any wheel input.

## Constraints

- Keep the Listen-Only, OTA, Autopark, AP-first, Soft Engage, abort-guard,
  freshness, and send-failure gates.
- Copy only a fresh (`fsd_tx_fresh`, 25 ms), idle, page-1 native `0x3C2` frame.
  Change only the left-tick field (byte 2 bits 5:0). Send on the bus the native
  frame came from.
- Keep default behavior unchanged while Scroll Ack is OFF.
- The state machine (`esp32/.firmware/scroll_ack.h`) has no Arduino dependency, so
  host tests run it directly.

## Design

### Bus rule

- `scroll_ack_note_epas()` stamps each bus that receives `0x370`.
- A bus is a Vehicle bus when it showed no `0x370` in the last 2000 ms
  (`SCROLL_ACK_EPAS_WINDOW_MS`).
- `scroll_ack_step()` ignores `0x3C2` frames from any other bus completely. They do
  not move the episode, the timer, or the diagnostics.
- This rule works with Vehicle CAN on either T-2CAN channel and on single-channel
  boards. The diagnostics show the chosen bus as `vehicle_bus`.

### DAS snapshot

- Stamp DAS only after a standard DLC-8 HW4 `0x39B` parse, on any bus.
- Latch the frame's receive time (`frame.rx_ms`) together with the AP state and the
  hands-on state from the same parse.
- DAS is fresh for 1000 ms (`SCROLL_ACK_DAS_FRESH_MS`).

### Gates for any tick

Enabled, TX allowed (`fsd_can_transmit()` and the AP injection gates), fresh
native frame, HW4, fresh DAS, AP state 6. The first failing gate is the `block`
reason in the diagnostics.

### Trigger modes (`scroll_ack_mode`)

| Mode | Value | When an episode starts |
|---|---|---|
| On prompt (default) | 0 | Hands-on state 3, 4 or 5, after a random delay. One episode per prompt level: a change of level after the episode (the nag escalates, for example 3 → 4) starts a new delay and one more episode. A change during the delay or the pair does not restart them. It rearms after hands-on leaves 3–5. A prompt cleared during the delay sends nothing. A lost gate cancels the delay. (Hands-on 4 and 5 and the escalation were added on 2026-11-18, from the TSL6 prompt build v2) |
| Timer (TSL6) | 1 | While AP state is 6, one episode per interval, whatever the hands-on state. The first episode comes one interval after AP engages. A lost gate stops the timer, and it restarts with a full interval |

### Tick patterns (`scroll_ack_pattern`)

| Pattern | Value | Ticks |
|---|---|---|
| `-1` then `+1` (default, TSL6) | 0 | Pair, volume unchanged |
| `+1` then `-1` | 1 | Pair, volume unchanged |
| `+1` once | 2 | One tick, volume up one step |
| `-1` once | 3 | One tick, volume down one step |

### Timing

Each value is drawn uniformly around its mean, per episode.

| Setting | Range | Default | Spread |
|---|---|---|---|
| Prompt delay (`scroll_ack_delay_ms`) | 0-5000 ms | 600 ms | +/-50 % |
| Gap between ticks (`scroll_ack_gap_ms`) | 0-2000 ms | 100 ms | +/-50 % |
| Timer interval (`scroll_ack_interval_ms`) | 1000-35000 ms (10000 before BLE board 0.3.2) | 2500 ms | +/-20 % (2.0-3.0 s, as measured on the TSL6) |

Timer episodes are timed start to start. The second tick goes on the first idle
native frame after its gap. Native page-1 frames come every 100 ms, so the real
gap rounds up to the next frame.

### Second tick

- It waits while the native frame shows a wheel input. The driver input always wins.
- It gives up 2000 ms after its due time (`SCROLL_ACK_SECOND_TIMEOUT_MS`).
- A lost gate ends the pair. Each pair that ends without its second tick adds to
  `pairs_aborted`.
- A failed first-tick send ends the episode with no retry.

### Episode lifecycle

Phases: `Armed`, `Delay` (On prompt only), `SecondPending`, `EpisodeDone`.
A switch change or a mode change resets the episode and the timer, and increments
`episode_generation`. A completion that arrives after a reset counts the send but
does not change the phase.

### Settings

Stored in NVS: `scrAck` (enable), `scrMode`, `scrPat`, `scrDly`, `scrGap`, `scrInt`.
Out-of-range stored values fall back to the defaults.

### Diagnostics and dashboard

- `/api/status` reports `scroll_ack`, `scroll_ack_mode`, `scroll_ack_pattern`, the
  three timing means, `scroll_ack_sent_count`, and `scroll_ack_diag`.
- `scroll_ack_diag` holds `phase`, `block`, `native_age_ms`, `das_age_ms`,
  `das_ap_state`, `das_hands_on`, `vehicle_bus`, `tx_allowed`, `pairs_aborted`, and
  `timer_armed`.
- `block` values: `disabled`, `tx_denied`, `stale_native`, `not_hw4`, `stale_das`,
  `das_ap_state`, `das_hands_on`, `native_idle`, `episode_done`, `pair_aborted`,
  `timer`, `delay`, `gap`.
- The dashboard has the Scroll Ack switch, a Mode selector, a Ticks selector, and
  sliders for the three timing means.
- The black box records `0x3C2`, so a capture shows the native frames and the
  board's own ticks.

## History

1. First design (2026-11-11): one balanced pair when hands-on reached 3, primary
   channel only.
2. Hardening design (2026-11-12): compile-time routing, with T-2CAN DAS on `can0`
   and Vehicle CAN on `can1`, and one `+1` tick per prompt.
3. Car tests of design 2 sent ticks (`can1.tx` counted them), but the prompt did not
   clear. Logs from those tests show `0x370` on `can1`, so `can1` was Chassis CAN
   in that wiring. The ticks went to the forwarded Chassis copy of `0x3C2`, and the
   car ignored them. Earlier tests in the same session sent nothing, because a
   stalled CAN loop made every native frame fail the 25 ms freshness gate. The fix
   for that was the RX ID whitelist plus the web server on the second core.
4. Current design: the `0x370` bus rule replaces the board rule, plus the modes,
   patterns, and timing above. It cleared the prompt on the car.

## Tests

- `test/test_scroll_ack.cpp` (294 checks): exact tick bytes for each pattern,
  prompt delay and its cancellation, gap timing and the second-tick wait and
  timeout, Timer intervals and restart, the AP state and hands-on gates, DAS
  freshness and ingest, the bus rule (including a Chassis copy that must get no
  tick), the idle checks, send failures, switch and mode resets, wrap-safe timers,
  setting clamps, and the diagnostics JSON.
- `test/test_fsd_core.c`: `0x3C2` is in the black-box key set.
- `test/test_web_page.py`: the dashboard has every Scroll Ack control, command, and
  JSON field.
- Release check: `make -C test check`, `pio run -e lilygo-t2can`, and
  `esp32/ci_check_stack.py`. All 11 ESP32 build targets compile.

## Open items

- Only the T-2CAN is tested on a car. The other boards compile but are not tested.
  The sniffer build cannot transmit.
- Only one car (2023 Model Y HW4, 2026.38) is tested.
- Power the board from a car USB port. Harness OBD pin 16 is the cabin camera feed
  (X962 pin 3, `INTERIOR_CAMERA`), and ESP32 boards have tripped its eFuse.
