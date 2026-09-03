# MSV3 Production Firmware — First Build (2026-08-22)

> **2026-08-30 current-state update:** This document remains the history of the first production
> build, not the current field definition. The later probe packet is 23 bytes and includes die
> temperature, disconnected bare-tank count, and phase. Current production source also uses
> `RCOUNT_CH0=0xFFFF`, full-current activation, `STATUS.CH0_UNREADCONV` readiness, and STATUS-before-
> DATA ordering. A temporary DRDY-polling build suppressed advertisements and was replaced. The
> current environmental failure and repair plan are documented in
> [MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md);
> the telemetry and ratiometric analysis are in
> [MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md](MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md)
> and [MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md](MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md).
> A complete application-only BLE OTA update has since been demonstrated on the real MSV3 board;
> its reset-safe implementation, live evidence, and required battery/bootloader qualification are
> documented in [MSV3_BLE_OTA_PROOF_2026-09-02.md](MSV3_BLE_OTA_PROOF_2026-09-02.md). OTA is not
> yet part of this production image.

**Status:** The 2026-08-22 production probe and matching receiver were flashed and observed live.
The review-remediated 2026-08-23 source compiles but has not been reflashed, so the physical probe
must not yet be assumed to have the power-safe defaults below. Sections that say "not yet flashed,"
`/1000`, ceiling `25000`, or "raw counts are not in Supabase" describe the initial draft and are
superseded by the deployment update.

## Production power defaults (review-remediated 2026-08-23)

The tracked production source now has `OVERNIGHT_LED_BURN_TEST = false` and
`ENABLE_SERIAL_LOGS = false`. The temporary 2026-08-22 overnight discharge build kept D2 on and
waited for a serial host; that configuration is historical and must not be used for normal
solar/battery deployment. Reflash is required before assuming the physical probe has these
production defaults.

## Current receiver mapping and raw-history update

The nail-polish-covered probe established an approximately 20,000,000 dry to 19,000,000 wet
FDC range. The initial `/1000` mapping retained exactly 1,000 derived steps across that signal.
Receiver scale version 2 now uses:

`clamp_u16((24,000,000 - rawFdcCounts) / 100)`

This maps 20M dry -> 40,000 and 19M wet -> 50,000, retaining 10,000 steps. The roughly
3,000-count bench noise maps to roughly 30 derived steps for the receiver presentation filter to
remove. The raw 28-bit value is also uploaded and stored unchanged in Supabase
`raw_fdc_counts`; it is no longer restricted to BLE, serial, and local `/data`.

Migration 029 versions each series with `moisture_scale`. The first scale-2 row automatically
starts a new calibration epoch, and history readers return only the latest scale while retaining
older rows. Receiver build `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2` is live on
COM16; observed raw `20,243,803` maps to `37,561` exactly as designed.
**New files:** `arduino/MSV3_Moisture_BLE_Sleep/MSV3_Moisture_BLE_Sleep.ino`
**Changed files:** `arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino`,
`arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino`

This is the first real deployment firmware for the MSV3 probe — not another bring-up console, the
sketch actually meant to ship. It exists because, as of 2026-08-21/22, the probe's hardware is
considered bench-verified enough to move on: cold start, the VSTOR↔VBAT self-latch, sleep current
(10-15µA after the `gpioDisconnect()` fix), FDC2214/mux/I2C, and battery/charger reads were all
confirmed on real hardware across the 8/20-8/22 sessions (see
[[msv3_lfclk_rtc1_corruption_root_cause]], [[msv3_sleep_current_leak_fixed]],
[[msv3_solar_harvester_first_test]] in memory, and `docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md`).

## What's in the new probe sketch

`MSV3_Moisture_BLE_Sleep.ino` is structured like `MSV2_Moisture_BLE_Sleep.ino` — periodic
wake/measure/advertise/sleep, BLE-advertise-only (no GATT peripheral, no connections), raw values
on the wire with all calibration left to the receiver (same "raw-values rework" philosophy MSV2
already established). Every function is ported, not re-derived, from the bench-verified bring-up
sketch:

- FDC2214 register init/read (`fdcInit`, `fdcReadChannel0`) — TI SNOSCZ5B-derived register values.
- `setMux()` and the TMUX1109 electrode-swap/EC-check code labels.
- `readVbatMv()` — all three 2026-08-21 fixes: divider polarity (drive `VBAT_DIV_EN` LOW, not
  HIGH), ADC scaling (12-bit @ `AR_INTERNAL_3_0`, not the wrong 14-bit/3.3V assumption), and the
  25ms RC settle (5 time constants against the R29‖R30/C16 filter, not the original 5ms/1 time
  constant).
- `readChargerPins()` — the internal-pull-up-during-read-only dance for BQ25170's STAT/PG, which
  have no board pull-up.
- `probeSleep()`/`probeWake()` — including the 2026-08-21 sleep-current-leak fix
  (`gpioDisconnect()` on `VBAT_DIV_EN`/`VBAT_DIV`/`CHG_STAT`/`CHG_PG`, not just `INTB`/`DIV_NODE`)
  that took sleep current from ~65-71µA down to 10-15µA.
- The LFCLK-verify-not-configure watchdog pattern and the same compile-time `#error` guard
  requiring `USE_LFRC` (see `docs/KNOWN_ISSUES.md`).

**Assumptions made, all easy to change, all called out in the file's own comments:**

| Constant | Value | Basis |
|---|---|---|
| `SENSOR_ID` | 5 (was 4 until 2026-08-31) | 1=Scindapsus, 2=Alocasia, 3=AFE bench board, 4=first MSV3 field unit (see `MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md`). This constant and `DEVICE_NAME` are edited in place per-board, not kept as separate per-board sketch files like MSV2 - reflashing the id=4 unit requires manually setting both back first. |
| `WAKE_PERIOD_MS` / `ADVERTISE_INTERVAL_MS` | 10000 / 60000 | Mirrors MSV2's proven cadence |
| BLE TX power | +8 dBm | Staged 2026-09-02 after a fixed-position A/B test improved mean RSSI from -69.45 dBm at +4 dBm to -66.25 dBm at +8 dBm (49/48 packets). Estimated incremental draw at the 500 ms/minute advertising duty cycle is about 1.6-2.1 uA average. |
| `MOISTURE_SWING_THRESHOLD` | 300000 raw counts (~1.5% of dry baseline) | **Provisional** — no real soil range exists yet, same status MSV2's own threshold had before real data |
| `WAKE_DISCARD_CONVERSIONS` | 2 | Measured on real hardware via `wakeprofile`, not guessed |

Build/flash the same way as the bring-up sketch — same wrapper, different `-Sketch`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action compile -Sketch arduino/MSV3_Moisture_BLE_Sleep
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action upload -Sketch arduino/MSV3_Moisture_BLE_Sleep -Port COMx
```

## Receiver changes — additive only, by design

MSV2's raw BLE packet has a 2-byte raw-sensor field (RC-oscillator kHz, max ~65k). FDC2214's
`DATA_CH0` is a 28-bit register — real values on this board are ~18-22 million, nowhere close to
fitting. The field had to widen to 4 bytes for MSV3.

**Wire format** (bytes 0-9 identical to MSV2's existing raw packet):

| Bytes | Field | Notes |
|---|---|---|
| 0-1 | `companyId` | `0xFFFF`, same as MSV2 |
| 2-5 | magic | `"MSV2"` — real production magic, not the bring-up sketch's deliberate `"MSV3"` test magic |
| 6 | `sensorId` | |
| 7-9 | `wake` | 24-bit LE |
| 10-13 | raw `DATA_CH0` | **32-bit LE — this is the widened field, was 2 bytes/bytes 10-11 in MSV2** |
| 14-15 | raw battery mV | 16-bit LE — shifted two bytes later than MSV2's packet |
| 16 | charging flag | shifted two bytes later |

Total 17 bytes vs. MSV2's 15-byte raw packet.

**Extension approach:** reused the receiver's *existing* length-based format-dispatch pattern —
`hasRaw = len >= MSV2_RAW_PACKET_MIN_LEN` already distinguishes legacy-calibrated probes from
raw-values probes. Added a second, wider threshold, `MSV2_RAW_PACKET_WIDE_LEN = 17`, checked
**before** the narrower one (a 17-byte packet also satisfies `>= 15`, so checking narrow first
would misparse a wide packet using the old field offsets — see the in-code comment on
`MSV2_RAW_PACKET_WIDE_LEN`, this ordering is load-bearing, not stylistic).

**Deliberately did not widen `SensorReading.moisture` (`uint16_t`).** That field is read at a
dozen-plus call sites across the display-rendering code (card draws, detail views, the API JSON
serializer), all built assuming 16-bit — widening it would have meant auditing and changing every
one of those sites in a file actively serving real deployed sensors, for a benefit (full 28-bit
precision surviving all the way to the percent calculation) the moisture-to-percent mapping doesn't
actually need. Instead:

1. The raw `DATA_CH0` value is kept in full precision in a new field
   (`SensorReading.rawFdcCounts`, `uint32_t`) for BLE, receiver serial, and local `/data` API
   visibility. Nothing is lost on the wire or in the receiver's local state, but Supabase history
   stores only the derived moisture and battery fields, not `rawFdcCounts`.
2. Before calibration, it's rescaled into the same `uint16_t`-safe range the kHz sensors already use:
   `scaleMsv3RawForCeiling()` divides by 1000. That divisor isn't arbitrary — the FDC2214's own
   measured noise floor is ~0.015% of ~20.3M (~3000 raw counts, see
   `docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md`'s `fdcstream` bench data), so at a 1000 divisor each
   scaled unit is ~3x finer than the sensor's own noise — real variation still spreads across many
   discrete values instead of collapsing onto a coarse comb, the exact failure mode
   `OSC_SAMPLE_WINDOW_MS` fixed for the kHz sensors on 2026-07-30 (see
   `msv2_moisture_quantization_rtc_gate` in memory).
3. The scaled value is handed to the **existing, completely unmodified** `calibrateRawMoisture()` /
   `MOISTURE_INVERT_CEILING_BY_SENSOR[]` pipeline, with a new table entry:
   `MOISTURE_INVERT_CEILING_BY_SENSOR[4] = 25000` (scaled units — ~20340 scaled at the ~20.34M-raw
   dry baseline, so ~4600 scaled units of headroom before clamping to 0).

Net effect: **zero changes to any display-rendering code path.** Only the BLE parse dispatch, two
new `SensorReading` fields (`wideRaw`, `rawFdcCounts`), and the JSON raw-value block changed
(`raw.fdcCounts` added alongside the existing `raw.moistureKHz`, mutually exclusive per sensor by
`wideRaw`).

**Also fixed while in there:** the on-screen "Raw osc" value formatter hardcoded a `%ukHz` suffix
onto whatever raw value it was given — would have displayed "20341523kHz" for an MSV3 sensor.
Now branches on `wideRaw` and omits the (wrong) unit suffix for FDC-counts sensors.

Both sketches compile clean:
- `MSV3_Moisture_BLE_Sleep`: 128072 bytes (15% flash), 15572 bytes (6% RAM).
- `Treebeard_ESP32_Display_Runtime`: 2131812 bytes (67% flash), 58380 bytes (17% RAM) — unchanged from
  before this session's edits, i.e. the additive changes cost effectively nothing measurable.

## Bring-up sketch addition: `sensesweep <n>`

Two related gaps found while reviewing the bring-up sketch for what's actually been exercised on
real hardware, both closed by one new command:

1. The electrode-swap mux codes (0/1) had only ever been read individually (`mux`, `muxsweep`) —
   never actually *alternated* cycle-to-cycle, which is what would cancel DC-bias-driven drift
   between the two physical electrodes (same reasoning as MSV2's `driveIsPlateA` plate swap).
2. The EC-check path (codes 2/3, `DIV_NODE`) had never been read as part of any repeated/periodic
   cycle — only ever a single `divnode` call at rest, never alternated between its two polarities.

`sensesweep <n>` (default 10 cycles) alternates both together, using the mux's own address-bit
structure (tank-sensing codes 0/1 and EC-check codes 2/3 share the same low "phase" bit) so one
toggle drives both alternations in lockstep rather than adding a second independent toggle. Each
cycle reports the LC-tank reading (with electrode-swap phase label) and the `DIV_NODE` EC-check
reading (with EC-check phase label). Parks the mux back on phase 0 when done.

**Not folded into the production sketch** — matches this project's established pattern of proving
new behavior on the bring-up console against real hardware before it goes into what ships. Real
bridging-the-electrodes validation (Test 5 in the bench test plan) is still open; `sensesweep`
proves the alternation *mechanism* works, not the EC-check sensor's actual response to conductive
soil.

## Historical initial-draft checklist (superseded where noted by the status at top)

1. **Neither new/changed sketch has been flashed or run on real hardware yet.** Everything above is
   compile-verified only. First real test: flash `MSV3_Moisture_BLE_Sleep`, confirm the receiver
   picks it up as sensor 4 (watch the `[Treebeard debug]` serial line for `rawFdcCounts=...` and a
   sane `moisture` value), and run `sensesweep` on the bring-up sketch to confirm the alternation
   behaves sanely before trusting it.
2. **Backend/Supabase/Netlify frontend untouched.** The new `raw.fdcCounts` JSON field and sensor 4
   generally haven't been checked against whatever cloud-sync/history/frontend code assumes about
   fields per sensor — deliberately out of scope for tonight given the receiver change alone was
   already real risk surface to touch carefully in one sitting.
3. **No real soil calibration.** Both `MOISTURE_INVERT_CEILING_BY_SENSOR[4]` (25000) and
   `MOISTURE_SWING_THRESHOLD` (300000 raw) are bench-estimate placeholders — same "provisional,
   needs real field data" status every other calibration constant in this project carries before
   its own dedicated session.
4. **This exact compiled binary's real current draw hasn't been re-measured** — it reuses the exact
   functions already current-probed on the bring-up sketch (10-15µA sleep), but the specific
   `MSV3_Moisture_BLE_Sleep` binary itself hasn't been on a current probe yet.
5. **Overnight 2026-08-22 battery drain test** (see `MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 3) is
   running in parallel on the *existing* bring-up sketch (`led on`, ~1-3mA, USB unplugged) —
   independent of the firmware work above, sets up tomorrow's real daylight harvester-recharge test.

## Addendum (2026-08-22, later same day): electrode-swap alternation folded into production

**Corrects the "Not folded into the production sketch" statement above.** That statement was about
`sensesweep` specifically and is now stale in spirit: the *mechanism* `sensesweep` proved
(alternating TMUX codes 0/1) is now used by `MSV3_Moisture_BLE_Sleep.ino` itself, though not by
calling `sensesweep`'s own code path — production has its own, simpler, per-wake version.

**What changed, exactly:** `loop()` now keeps a persistent `currentTankPhase` (0 or 1) and calls
`setMux(phaseUsed)` once per wake, *before* `probeWake()` deasserts `SD` — i.e. the mux is switched
while the FDC2214 is still in shutdown, not while it's actively oscillating. After each completed
measurement attempt (success or failure), the phase toggles for next time. This is **one phase per
wake**, not both phases read-and-averaged every wake — i.e. successive wakes alternate which
electrode drives vs. senses, the same DC-bias-cancellation intent as MSV2's `driveIsPlateA`, applied
at wake granularity instead of oscillator-half-cycle granularity (TMUX-mux-controlled hardware, not
software-controlled RC-oscillator plates, so this is the correct granularity for this design).

**No new fixed delay was added.** The bring-up sketch's `doMuxSweep()`/`doHotColdStream()`/
`doSenseSweep()` all use a measured `delay(300)` after switching mux — but that delay was measured
for switching mux while the FDC was already free-running (a live tank switch). Production switches
mux while the FDC is in shutdown, a different physical condition, and relies on the existing,
already-verified wake-settle path (`tSDWAKEUP` + CONFIG-last reinit in `fdcInit()` +
`FDC_ACTIVATION_US` + `WAKE_DISCARD_CONVERSIONS=2` discards, all already inside `probeWake()`) as the
settle margin for whichever phase was just selected.

**This assumption (shutdown-time mux switching needs no extra settle) is NOT hardware-validated as
of this writing.** Do not assume `wakeprofile` in the bring-up sketch can validate it:
`doWakeProfile()` calls `probeSleep()` as its first action, which unconditionally calls
`setMux(MUX_SLEEP_CODE)` (phase 0) before the cold-wake sequence runs — so `wakeprofile` can only
ever profile phase 0's cold-settle behavior, never a genuine "was in shutdown at phase 1, switched to
phase 0 (or vice versa), then woke" case. The bring-up sketch was deliberately left unmodified for
this change (keeps this edit focused), so there is currently no tool that can pre-validate this
specific transition. **The required substitute is a bench-observation window on production's own
serial output**, described below.

**Status-bit handling (new):** every wake now checks `fdcReadStatus()`'s `ERR_WD`/`ERR_AHW`/
`ERR_ALW` bits (bits 11/10/9, same positions as the bring-up sketch's `appendFdcStatus()`) alongside
gating advertising on `probeWake()` and `fdcReadChannel0()` both succeeding
(`measurementOk = wakeOk && readOk`, and `measurementOk` — not just `wakeOk` — now controls
`shouldAdvertise`). `ERR_WD` is treated as an invalid reading (skip advertising, same as an I2C
failure). `ERR_AHW`/`ERR_ALW` are logged (`[WARN]` lines) but the reading is still advertised —
deliberately, since amplitude flags are exactly the condition most likely to fire right after
watering, and fully suppressing them risks losing the overnight test's most important data point.
`IDRIVE` (`0x8000` in `fdcInit()`) remains un-tuned for soil; persistent `ERR_AHW`/`ERR_ALW` in the
overnight log is a signal to retune it in a future bench session, not itself a failure of this run.

**Deliberately NOT changed in this pass:** `ENABLE_SERIAL_LOGS` stays `true` (this is the first time
this exact binary has ever run on real hardware — flip it off only in a later revision once
confirmed stable, per the file's own existing comment). The "skip Bluefruit/BLE entirely if runtime
LFCLK verification fails" hardening idea was considered and explicitly deferred (see the comment in
`initWatchdogSafety()`) — its failure mode (silent, unrecoverable non-advertising, no watchdog
either) needs a separately-designed recovery path. EC-check (mux codes 2/3, `DIV_NODE`) remains
excluded from production; unchanged from before this addendum, still blocked on the documented
`P0.00`-has-no-analog-capability hardware defect.

### Exact flash order (receiver first)

The receiver's wide-packet parser (`MSV2_RAW_PACKET_WIDE_LEN = 17`, `rawFdcCounts`,
`scaleMsv3RawForCeiling()`, `MOISTURE_INVERT_CEILING_BY_SENSOR[4] = 25000`) is itself uncommitted
working-tree state and may not be on the physical receiver unit yet. An older receiver binary would
treat this probe's 17-byte packet as the legacy 15-byte format and misread the battery/charging
bytes. **Flash the receiver before the probe:**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 -Action upload -Port COM_DISPLAY
```

Then, in a **separate test cup/vessel with similar soil — not the final overnight pot**, flash and
run the bring-up sketch's wet checks (`fdcinit`, `sensesweep 10`, `vbat`, `chgstatus`) to sanity
check the hardware is still behaving as it did during the 2026-08-21/22 bench sessions:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action upload -Port COM_PROBE
```

Keeping this preflight in a separate vessel matters: running it in the actual overnight pot would
pre-wet the electrodes before the overnight test's own "dry baseline, then watering event" comparison
even starts.

Then flash production:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action upload -Sketch arduino/MSV3_Moisture_BLE_Sleep -Port COM_PROBE
```

### Required bench-observation window (before leaving it unattended)

With the probe still on USB serial (production's own `ENABLE_SERIAL_LOGS = true` output, 115200
baud), place it in its final pot (dry soil, before watering) and watch several wake cycles —
enough to see `phase=0` and `phase=1` each appear at least twice. For each, confirm:

- No `[WARN] FDC status ERR_WD` lines (would mean that phase's reading was discarded).
- Ideally no `[WARN] FDC status ERR_AHW/ERR_ALW` lines in dry soil; if present, note it but proceed
  — a real fix requires re-tuning `IDRIVE` in a future bench session, not something to chase
  tonight.
- `DATA_CH0` values for a given phase stay in a stable, plausible band across repeats (roughly
  consistent with the ~20.3M dry-air baseline from `MSV3_BENCH_TEST_PLAN_2026-08-21.md`, allowing
  for soil vs. air being a different physical environment).

**If a phase's reading is visibly unstable, garbage, or repeatedly flags `ERR_WD` right after that
phase's mux switch** (the one condition this design has not bench-proven), the fallback is to add a
short fixed delay (start with 300ms, matching the bring-up sketch's already-proven live-switch
figure) between `setMux(phaseUsed)` and the rest of `probeWake()` in `loop()`, reflash, and re-run
this observation window before proceeding. Only unplug USB and leave the probe unattended once this
window looks clean.

### Raw FDC counts: where they actually live (correction)

`rawFdcCounts`/`DATA_CH0` full-precision values are only ever available over BLE (the wire packet),
the receiver's own serial debug log (`[Treebeard debug] ... rawFdcCounts=...`), and the receiver's local
`/data` JSON (`raw.fdcCounts`) — **not** in Supabase history. `snapshotJson()`/the Supabase
upload path stores only the *derived* `moisture` (post `scaleMsv3RawForCeiling()` +
`calibrateRawMoisture()`) and `batteryPercent`, same as every other sensor, not the raw counts
themselves. If raw-count-level analysis of the overnight run is wanted afterward, the receiver's
   own serial log (kept running for the full window per step 3 below) is the durable source for that
   analysis — capture/redirect it, since Supabase will not have the raw counts.

### Overnight watered-pot validation checklist

1. Collect at least 10-15 minutes of dry baseline in the final pot (after the bench-observation
   window above), electrodes undisturbed.
2. Water at a recorded timestamp without moving the electrodes.
3. Unplug the probe's USB (battery-only for the overnight portion); keep the receiver powered and
   on USB/serial for the full window, with its serial output captured/logged to a file (not just
   watched live) — this is the only durable record of raw `DATA_CH0`/status-flag detail, since
   Supabase only ever receives the derived values (see above). Before walking away, confirm
   cloud sync is active on this physical unit: require
   `[Treebeard cloud] bootstrap ok; cloud sync enabled`, then observe at least two
   `[Treebeard cloud] upload ok: HTTP 2xx` results after sensor-4 packets arrive. Also confirm sensor 4's
   derived `moisture` and `batteryPercent` are visible remotely before leaving the setup unattended.
4. Leave it at least 8 hours.
5. **Pass criteria:**
   - Receiver serial log shows `[Treebeard debug]`-style lines for sensor 4 continuing in ~500ms
     advertisement bursts on the existing ~60s cadence (or ~30s when a real swing is detected) —
     not "continuous" advertising, and not silence for extended stretches.
   - The probe's own `wake` counter (visible via `rawFdcCounts`/wake fields the receiver logs)
     increases monotonically across the run, with **zero unexplained watchdog resets** (a reset
     mid-run is a failed validation, not an acceptable one-off).
   - No gap between consecutive sensor-4 updates longer than the receiver's own configured
     staleness/gap-detection window (check the receiver source for its current threshold rather
     than assuming a specific number here).
   - A visible step in `DATA_CH0`/derived moisture at or shortly after the recorded watering
     timestamp.
   - Battery mV/percent trends plausibly across the window (no sudden unexplained multi-percent
     drop); remember the charging flag reflects only the BQ25170 **USB** charger path, not
     BQ25504/solar activity, so on battery-only overnight it is expected to read "not charging"
     throughout.
   - `MOISTURE_INVERT_CEILING_BY_SENSOR[4] = 25000` and `MOISTURE_SWING_THRESHOLD = 300000` remain
     provisional — success for this run is a coherent, explainable trend (dry -> wet step ->
     gradual dry-down), not a specific calibrated moisture percentage.
   - The receiver's serial log shows repeated `[Treebeard cloud] upload ok: HTTP 2xx` results, and sensor
     4's derived `moisture`/`batteryPercent` history is visible remotely in Supabase through the
     morning. Missing 2xx confirmations or missing remote sensor-4 history fails the requested
     end-to-end overnight test even if the local BLE path remains healthy; preserve the receiver log
     so a cloud-path failure can be distinguished from a probe/receiver failure.
6. If any pass criterion above fails, capture the receiver's serial log and the last known probe
   state before investigating — do not immediately reflash over the evidence.
