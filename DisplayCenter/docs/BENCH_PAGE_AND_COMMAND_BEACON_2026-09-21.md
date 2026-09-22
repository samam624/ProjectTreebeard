# Bench page and the bench command beacon

**Date:** 2026-09-21
**Status:** built, flashed and **measured working on real hardware.**
**Scope:** live FDC readout and LED identify, both in the **Bench tab of the receiver's web app**
(`http://10.0.0.35/app` on the current network). Flashing probes from the receiver was
deliberately deferred — see "What was deferred, and why" below.

## What this adds

Two things, both bench/demo features, neither of which touches the production probe image:

1. **A Bench tab in the receiver's web app** — a fifth tab beside Status / Setup / Diagnostics /
   History, showing FDC2214 counts, board temperature, battery, mux code, FDC STATUS, RSSI, wake
   counter, packet count and age. Backed by `GET /bench-data` (raw observables as JSON), polled at
   2 Hz **only while the tab is visible**.
2. **An LED identify command** — `POST /bench-identify` with `{"sensorId":N}`, surfaced as an
   Identify button per probe card. Addressed to the selected probe, never broadcast to all.

**It is a TAB, not a separate page and not a display screen.** Two earlier cuts got this wrong and
both are worth recording, because the correction was the same each time — put it where the user
already is:

- first as a **full-screen page on the physical panel**, reached from the brightness overlay. The
  panel shows plant data only. `verify_bench_packet_abi.mjs` now asserts the absence of
  `drawBenchPage`, `benchPageOpen`, `BenchTouchTarget`, `drawBenchChip`, `openBenchPage` and
  `OverlayTouchTarget::Bench` so it cannot quietly come back.
- then as a **standalone `/bench` HTML page**, which was a second copy of a view that already
  belonged in the app and would have drifted from it. `BenchPage.h` is deleted, the verifier
  asserts `TREEBEARD_BENCH_PAGE_HTML` no longer exists, and `/bench` now 302s to `/app` so an
  existing bookmark still lands somewhere useful.

Polling is scoped to the visible tab on purpose: the receiver serves this from the same
single-threaded `loopTask` that drives the display and the cloud uploader, so an always-on 2 Hz
poll would be a permanent tax for a tab nobody is looking at. The verifier pins that too.

Everything presentational — digit grouping, the demo baseline/delta, age formatting, selection —
lives in the browser. The firmware serves raw observables and holds no per-viewer UI state.

## The key discovery: the data path already existed

The receiver has been parsing the full 23-byte MSV3 diagnostic packet out of advertisements for a
while — `rawFdcCounts` (all 32 bits), battery mV, board temperature, tank phase — and discarding
all of it at the UI layer. A live readout therefore needed **no BLE connection, no GATT client and
no central role on the receiver**. It needed a page, and a probe that advertises often enough.

That is why this half was cheap and the flashing half is not.

## Why bring-up probes were invisible, and how they became visible safely

`MSV3_Probe_Bringup_Test.ino` advertises with the magic `"MSV3"`, not production's `"MSV2"`. That
is a **safety property, not an oversight**: a production receiver drops the packet before it ever
looks at the sensor id, so a bench probe sitting in air next to a running receiver cannot
contribute a fabricated reading to a real plant's history or to the cloud.

The Bench page opts back in to seeing those packets through a **separate parse path writing a
separate array**:

- `parseBenchAdvertisement()` → `benchProbes[]`, guarded by its own `benchMux` spinlock
- it never writes `sensors[]`, never enqueues history, never touches the durable outbox, and
  deliberately does **not** set `sensorDataDirty`
- `benchDataJson()` **omits** `fdcCounts` entirely when `readingValid` is false, rather than
  sending 0, so the page cannot render a fabricated reading even by accident

`verify_bench_packet_abi.mjs` enforces that by scanning the function body for those symbols, with
comments stripped first so the function can still explain in prose why it avoids them.

## The bench packet is an ABI across three decoders

| Role | File | Function |
|---|---|---|
| writer | `MSV3/arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino` | `bleStartAdv()` |
| reader | `DisplayCenter/.../Treebeard_ESP32_Display_Runtime.ino` | `parseBenchAdvertisement()` |
| reader | `scripts/ble_periodic_reader.py` | `parse_msv3_manufacturer_data()` |

Layout (23 bytes; the authoritative copy is the comment above `bleStartAdv()`):

```
[0-1]   company id 0xFFFF
[2-5]   magic "MSV3"
[6]     sensor id (99 untargeted, fleet id when targeted)
[7-9]   wake counter, 24-bit LE
[10-13] FDC2214 DATA_CH0, 32-bit LE
[14-15] battery mV, 16-bit LE
[16]    flags: bit0 charging, bit2 board-temp valid, bit4 OTA power gate,
               bit5 reading invalid, bit7 bench live mode
[17-18] board temperature, int16 LE, quarter-degrees C
[19]    BENCH FORMAT VERSION          <- production sends 0 here
[20]    mux code the reading came from
[21-22] FDC2214 STATUS at the moment of the read
```

Bytes 0–18 match the production packet field-for-field, so the temperature field shares one
decoder across both magics. Bytes 19–22 reuse the four bytes production zeroes.

Three details are load-bearing:

- **Byte [19] is a version, not a length check.** Length says the bytes are present; version says
  what they mean. Every v1-only field is gated on it. This is CLAUDE.md's "key every reused byte
  on `build_id`" rule applied here — four readings were mislabelled during the DFU work for
  exactly the want of it.
- **`readingValid` is a parameter, not a global.** `servicePeriodicOtaWindow()` advertises a
  placeholder `DATA_CH0` of 0 without ever reading the FDC. The page renders that as `--`, not as
  a count of zero. A stale global here would be the same shape as the un-re-armed retained trace.
- **bleak strips the 2-byte company ID**, so every index in the Python decoder is the firmware's
  minus 2. `mfg[16]` is `payload[14]`. The verifier pins `len(payload) >= 21` against the
  firmware's 23 for this reason.

The Python decoder was self-tested against synthetic packets before any hardware: full v1 packet,
placeholder/OTA-window packet, legacy 17-byte packet, a 23-byte packet with version 0, and an
out-of-range temperature. It also fixed a real pre-existing bug — `charging` was
`payload[14] != 0`, which reported charging for any probe whose OTA power gate was merely open.

## The command beacon: receiver advertises, probe scans

Advertisements are one-way, so commanding a probe needs the probe to listen. Two options existed:

1. the receiver becomes a BLE **central** and writes a GATT characteristic, or
2. the receiver **advertises** a beacon and the probe scans for it.

**(2) was chosen deliberately.** The receiver is the RAM-constrained device that watches the whole
garden, and its cloud upload path has already failed once for want of contiguous internal RAM
(`RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md`). The bring-up image has ~94% of its RAM
free. The new role belongs on the probe.

Beacon layout (manufacturer data, company `0xFFFF`):

```
[0-1]   company id
[2-5]   magic "TBCM"      <- a third magic, distinct from MSV2 and MSV3
[6]     target sensor id, or 0xFF for all
[7]     opcode (0x01 = identify)
[8-11]  counter, uint32 LE, monotonic WITHIN one receiver boot
[12-13] session id, uint16 LE, randomised once per receiver boot
```

### The session id exists because the counter alone was measurably broken

The first cut had no session id: the probe accepted a command when its counter differed from the
last one seen, and treated a large backwards jump as a receiver reboot.

**That failed on real hardware, and the failure was silent.** After reflashing the receiver its
counter restarted at 1 while probe 8 still held `last = 1` from before, so the equality test
dropped the command. `POST /bench-identify` returned `{"ok":true}` and the probe's
accepted/executed counters stayed at `1/1`. The symptom would have been "identify works, except
the first time after the receiver restarts" — an intermittent nobody would have chased.

The session id fixes it at the root rather than tuning the heuristic: the counter is now only ever
compared **within one receiver boot**, and a different session skips the counter checks entirely.
The old `BENCH_CMD_RESTART_GAP` guess is gone, and the verifier asserts it does not return —
no value of that constant could have handled this case, since the collision was at equality, not
at a rewind.

**Proven by measurement**, not inspection: with the probe holding `last counter = 3`, the receiver
was reset on its own (DTR/RTS toggle on COM16, probe untouched) and the very first identify after
it came back advanced the probe to `4/4`. Under the old logic that was a 2-step backwards jump,
well inside the 1024 window, and would have been rejected.

### This channel is unauthenticated, and that is a bounded decision

Anyone in radio range who knows this layout can blink a bench probe's LED. That is acceptable for
exactly two reasons: **the only opcode is a light**, and **this code is compiled only into the
bring-up image**, never into `MSV3_Moisture_BLE_Sleep`. Do not add an opcode that changes
measurement, power or firmware state — those need a real authenticated channel, not a beacon.

### Three traps found while building it

- **`BLEScanner` does not auto-resume once an rx callback exists.** `BLEScanner.cpp` only calls
  `resume()` on the branch where no callback is installed. A callback that returns without
  resuming stops scanning permanently after the first advertisement the radio hears — immediate,
  on a busy bench. Every path out of `benchCommandScanCallback()` resumes.
- **`setAdvertisementType()` means different things per backend.** It forwards to
  `esp_ble_adv_params_t::adv_type` under Bluedroid and to `ble_gap_adv_params::conn_mode` under
  NimBLE. **This receiver build is NimBLE-backed**, where non-connectable is
  `BLE_GAP_CONN_MODE_NON` (0); Bluedroid's `ADV_TYPE_NONCONN_IND` (3) would compile there and set
  an out-of-range connection mode. The code mirrors the library's own `#if` split.
- **`String(ptr)` would truncate the beacon.** The counter routinely contains zero bytes, so the
  payload is built with `String(ptr, len)`. The failure mode — the probe seeing a short packet and
  dropping it — looks exactly like a radio problem.

### Scanning is tied to bench live mode on purpose

`Bluefruit.begin(1, 1)` enables the central slot (scanning is a central-role activity even with no
connection). But the scanner only runs while `bench on` is active, and stops whenever a connection
is up. Two reasons, both real:

- this sketch's own `sleepbench` and `wakeprofile` commands **measure current draw**, and a
  continuously scanning radio would silently invalidate every one of those numbers;
- a DFU transfer must not compete for radio time — a ~20-minute bootloader recovery is the price
  of getting that wrong.

## Why the browser polls instead of subscribing

The receiver serves this page from the same single-threaded `loopTask` that drives the display and
the cloud uploader. A socket held open per viewer is a cost that loop should not carry for a bench
tool, so the page polls `/bench-data` every 500 ms — well inside the probe's ~4/s live cadence and
cheap enough to be invisible next to the existing `/data` traffic.

Moving the view off the panel removed the surgical-redraw problem entirely rather than solving it:
there is no periodic display write on this path at all, so the bus that
`DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md` traced real corruption to is untouched by it.

## Probe console additions

```
bench on [muxCode]   live broadcast: re-read FDC + re-advertise every 250ms
bench off            stop the live broadcast (probe stays awake)
bench                show live-broadcast state, last mux code, FDC STATUS, scanner state
identify             blink the status LED locally (same action the receiver requests)
```

`bench on` performs **one** `probeWake()`; the live path then re-reads an already-awake FDC per
tick. A full settle per 250 ms tick would both miss the cadence and be a different measurement
from the one `fdcstream` reports. `blestop` also clears bench live mode — without that,
`serviceBenchLive()` would re-arm the advertiser on the next loop pass and `blestop` would appear
not to work.

## An unrelated pre-existing failure found on the way

`verify_msv3_capless_policy.mjs` had been **failing since commit `a85091c`** (the probe-4
inverted-hybrid rig), which blocked every receiver compile. The check asserted the set of
`setMux()` arguments in the production sketch was exactly `{MUX_SLEEP_CODE, phaseUsed}`; the
hybrid commit added `MSV3_HYBRID_V12_TANK ? MUX_HYBRID_MEASURE_CODE : phaseUsed`.

It was widened to admit that **one guarded exception and no more**: the ternary is normalized to
its false branch (what an ordinary build compiles), and two new assertions require that
`MSV3_HYBRID_V12_TANK` still defaults to 0 and is still a validated 0/1 build flag. Without those
two the normalization would be a loophole rather than a guard.

## What was deferred, and why

Flashing a probe from the receiver. It is four costs, not one:

1. the receiver is **scan-only** today — no `BLEClient` anywhere;
2. **RAM** — a central role adds internal-RAM pressure to the pool that already failed mbedTLS
   once;
3. it would mean re-implementing the safety core of `scripts/ble_ota_update.py` (1,531 lines) in
   C — fleet identity, device type `0x3356`, rollback floor, boot-guard pairing, DFU service UUID
   confirmation, receipt-notification guard. A button that skips those is a wrong-target flasher
   pointed at the garden;
4. a DFU session takes the receiver off scanning duty for ~40 s.

Two measurements from this work are encouraging for it, though:

- the receiver build is **NimBLE-backed**, the lighter-RAM option;
- after everything above, it sits at **72% flash (2.27 MB / 3.15 MB) and 21% RAM**.

The signing key must never live on the receiver. It carries pre-signed packages only.

## Verification status

**Measured on hardware, 2026-09-21.** Receiver flashed over USB on COM16 (hash verified); probe 8
flashed over USB on COM9 to application **v65593**, build tag `bench-2026-09-21c`, signed as
device_type `0x3356` with integrity/boot-guard/identity preflight all OK.

Port identification went through the USB composite **parent** instance id, per
`MSV3_COMP_PORT_PROBE_IDENTIFICATION_PITFALLS_2026-09-11.md`: COM9 = `1A2F0613682FD239` = probe 8,
COM7 = `695715B3A7F8217E` = probe 5. Probe 5 was plugged in throughout and deliberately not
touched.

| Check | Result |
|---|---|
| `msv3_fleet_scan.py --probe 8 --require-progress` | healthy; 44 packets, wake advanced +2 |
| `GET /app` | HTTP 200, 180,384 bytes; Bench tab button, view, list and polling all present |
| `GET /bench` | 302 → `/app`, so an old bookmark still lands usefully |
| `GET /bench-data` | HTTP 200; probe 8 `liveMode:true`, `fresh:true`, `formatVersion:1` |
| FDC counts | ~39.449–39.452 M, jittering a few hundred counts |
| Board temperature | 26.0 / 26.25 °C — resolving in quarter-degrees, as the field is defined |
| Mux / FDC STATUS / battery | `muxCode:0`, `fdcStatus:0x8`, `~4307 mV` |
| Live cadence | wake 75 → 85 in ~2 s, i.e. ~4/s as designed |
| `POST /bench-identify` | HTTP 200 `{"ok":true}`; `identifyBroadcasting` true then false after ~2.5 s |
| **Probe-side receipt** | `bench` console counters moved **0/0 → 1/1 → 2/2 → 3/3**, opcode `0x1` |
| **Receiver-only reboot** | probe at `3/3`, receiver reset alone, next identify → **4/4** |

That last row is the one that matters most, and it is why the probe now carries
`g_benchCmdAcceptedCount` / `g_benchCmdExecutedCount`. "The receiver says it broadcast" and "the
probe actually acted" are two separate observations; before those counters existed, the second was
an inference. It also proves the replay guard: the beacon is on air for 2.5 s at a 20 ms interval
— roughly 125 advertisements — and the probe acted **exactly once**.

The dry-air count is a useful independent corroboration. `verify_msv3_capless_policy.mjs` pins
39,400,000 as probe 8's dry-air reference; the live readout landed at ~39.45 M with the probe not
in soil.

**Still not verified by measurement:** the photons. `executed 1` proves `startIdentifyBlink()` ran,
so the LED path was entered, but nobody has watched the board blink. Everything else above was
observed directly.

**Also untested:** more than one bench probe on air at once — slot eviction and the
`BENCH_SLOT_COUNT` = 5 table. A receiver reboot mid-session WAS tested and is the bug described
above.

Probe 8 finished this work on application **v65594**, build tag `bench-session-fix`.
