# MSV3 software-only board temperature + bare-tank reference

**Date:** 2026-08-25
**Status:** Implemented and originally compile-verified; deployed end to end on 2026-08-30.
This is the first real implementation to come out of a live two-agent (Claude +
Codex) design session run through `tools/agent-supervisor`'s new local live-pairing server
(`src/live-server.ts`) - see that session's converged design brief for the original task text.

## 2026-08-30 deployment and field update

- The 23-byte diagnostic packet reached the receiver and cloud with non-null die temperature,
  bare-tank count, and phase. The History page gained conditional charts for die temperature, raw
  connected FDC count, bare-tank count, and the experimental same-row capacitance-above-bare ratio.
- The first live 269-row window is analyzed in
  [MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md](MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md).
  The bare tank tracked die temperature closely and removed about half of the first observed
  connected-count temperature slope, but did not remove the electrode/soil/coating residual.
- A first fresh-data firmware revision mistakenly waited on global `STATUS.DRDY`. The FDC2214's
  reset-default `ERROR_CONFIG.DRDY_2INT=0` prevents that bit from providing the assumed readiness
  indication, so the probe stopped advertising. Production firmware was corrected to wait on
  per-channel `STATUS.CH0_UNREADCONV`, after which transmissions returned.
- The probe was subsequently removed from soil and water pockets were found underneath the nail
  polish. Removing the polish caused the prior drift to reverse while drying and then become flat
  in air. Current physical interpretation and repair steps are recorded in
  [MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md).
- The original compile-only language farther below is retained as the historical pre-deployment
  gate list. It must not be read as the current deployment state.

## What this adds

Two raw observables, added without any hardware change (no blade reshape, no new wire/connector -
this was explicitly designed to satisfy the same "no reshape, no wire" constraint from
`docs/MOISTURE_INSERTION_SETTLING_AND_V4_TEMPERATURE_PLAN_2026-08-24.md`'s follow-up discussion):

1. **Board (die) temperature** - the nRF52840's internal temperature sensor, read via the
   SoftDevice's `sd_temp_get()` with an explicit, balanced HFXO clock request around it (see
   `readBoardTempQuarterC()` in `MSV3_Moisture_BLE_Sleep.ino`). This is **MCU die temperature**,
   not soil or blade temperature - it lives at the E73 module, physically distant from the buried
   blade tip.
2. **Bare-tank FDC reference** - one additional FDC2214 CH0 reading per advertising wake, taken
   with the TMUX1109 mux on EC-check code `2|phase` (disconnecting the electrodes), so the FDC is
   oscillating on the bare L2/C17 tank alone with no soil/electrode capacitance in the loop. This
   is a **board/tank-adjacent reference**, not a soil reading either.

Neither value is soil temperature. Neither is used to compute or compensate anything yet - both
are carried as raw values all the way to Supabase and left there for later analysis.

## Packet layout (23 bytes, up from 17)

First 17 bytes are byte-for-byte unchanged from the prior MSV3 wire format - an older receiver
(or an MSV3 probe still on older firmware) keeps working exactly as before with no diagnostics.

| Bytes | Field | Notes |
|---|---|---|
| 0-1 | `companyId` | unchanged |
| 2-5 | magic `"MSV2"` | unchanged |
| 6 | `sensorId` | unchanged |
| 7-9 | wake counter, 24-bit LE | unchanged |
| 10-13 | moisture-connected FDC `DATA_CH0`, 32-bit LE | unchanged |
| 14-15 | battery mV, 16-bit LE | unchanged |
| 16 | flags | bit0 charging (unchanged meaning); bit1 phase used (0/1); bit2 board-temp valid; bit3 bare-tank valid; bits4-7 reserved/0 |
| 17-18 | board temperature, signed int16 LE, quarter-degrees C | 0 when bit2 clear |
| 19-22 | bare-tank FDC `DATA_CH0`, uint32 LE | 0 when bit3 clear |

23 bytes exactly fills the 31-byte legacy advertising-data payload alongside the Flags (3B) and
TX Power (3B) AD structures already sent: `3 + 3 + (2 + 23) = 31`. `startAdv()` checks every
`Bluefruit.Advertising.add*()` return value and logs (when `ENABLE_SERIAL_LOGS`) if any add call
fails, rather than silently advertising with no manufacturer data.

## Bare-tank mux sequence

Only taken on advertising wakes (not every 10-second check), to limit extra FDC energy and
electrode DC exposure while keeping the reference temporally paired with every transmitted
moisture sample:

1. Normal moisture measurement, battery read, charger read, and the first `probeSleep()` for this
   wake have already completed (FDC in shutdown, mux at `MUX_SLEEP_CODE`/0).
2. `setMux(2 | phaseUsed)` - phase 0 selects EC-check code 2, phase 1 selects code 3.
3. `probeWake(WAKE_DISCARD_CONVERSIONS)` (the same wake/settle sequence the normal reading uses).
4. Read `DATA_CH0` + FDC status.
5. I2C/read failure or `ERR_WD` invalidates the reading; `ERR_AHW`/`ERR_ALW` are logged (bench
   builds only) but the raw result is retained, same policy as the moisture-connected reading.
6. `probeSleep()` runs again, unconditionally (success or failure) - this immediately returns the
   mux to code 0 and re-shuts the FDC down. Mux code 2/3 is never left selected across sleep, BLE
   advertising, or any error path, because this step is unconditional and there is no early return
   between step 2 and it.

## Reserved formula - not enabled

Later receiver-side analysis may want the ratio between the moisture-connected and bare-tank
readings, since tank frequency relates to capacitance as `f ~ 1/sqrt(L*C)`:

```
C_connected / C_bare = (D_bare / D_connected)^2
```

where `D_connected`/`D_bare` are the two `DATA_CH0` counts. **This is not computed or applied
anywhere in this implementation.** Both raw counts reach Supabase unmodified; any compensation
using this formula is future work, gated on the bench validation below.

## Required validation before unattended/production use

None of this has run on real hardware yet. Compile-only so far (probe, receiver) plus local
migration/test validation (cloud) - see the "Verification results" section of the implementing
session below. Before leaving a probe unattended on this firmware:

1. **Dry-air serial test** - temperature plausible, bare tank near the previously observed
   ~22.62M region, moisture count remains in its previous region, both phases work, packet length
   is 23 with manufacturer data present.
2. **Fixed-capacitor thermal test** - log moisture count, bare count, q4 temperature, phase, and
   FDC status across a ~10/20/30/40°C sweep; determine whether the ratio reduces front-end drift.
3. **Wet and saline-soil DC-exposure test** - confirm EC-check codes 2/3 are selected only
   briefly, alternate polarity as designed, and check for polarization/baseline disturbance/
   cumulative drift.
4. **Power test** - confirm sleep current returns to the existing 10-15uA region, and measure the
   average-current increase from one extra reference acquisition per advertisement.
5. **Thermal-placement test** - compare nRF die temperature against a reference thermometer at
   the E73 and another at L2/C17, across battery-only/USB-charging/solar-light/post-BLE-activity
   conditions. Do not claim improved accuracy until this is characterized.

## Files changed

- `arduino/MSV3_Moisture_BLE_Sleep/MSV3_Moisture_BLE_Sleep.ino` - `readBoardTempQuarterC()`, the
  bare-tank acquisition block in `loop()`, the 23-byte `startAdv()`.
- `arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino` - `MSV3_DIAGNOSTIC_PACKET_LEN`,
  new `SensorReading`/`PendingHistoryReading` fields, `onResult()` decode + receiver-side
  independent range re-check, `enqueueHistoryReading()` copy-through, three JSON serialization
  sites (`/data`, cloud snapshot, history batch), serial diagnostics.
- `supabase/functions/receiver-snapshot/msv3-diagnostics.ts` (new) + `.test.ts` - shared, tested
  validation/normalization used by both the batch and legacy single-row ingestion paths.
- `supabase/functions/receiver-snapshot/index.ts` - `PlantReading.raw` extended; both ingestion
  paths call `parseMsv3Diagnostics()`.
- `supabase/migrations/035_msv3_temperature_tank_reference_history.sql` - nullable history
  columns, updated `msv2_store_sensor_reading`/`_batch`, updated both history readers.
- `docs/SUPABASE_SETUP.md` - migration index updated to 035 (required by
  `scripts/verify_supabase_migrations.mjs`).

## Verification results

- Probe: `scripts\arduino_msv3_probe.ps1 -Action doctor` passed, `-Action compile -Sketch
  arduino/MSV3_Moisture_BLE_Sleep` succeeded - 127,288 bytes flash (15%), 15,572 bytes RAM (6%).
- Receiver: `scripts\arduino_display.ps1 -Action compile` succeeded - 2,209,856 bytes flash
  (70%), 83,668 bytes RAM (25%).
- Cloud: `node scripts/verify_supabase_migrations.mjs` passes (35 contiguous migrations, setup
  doc references 035). `node --test supabase/functions/receiver-snapshot/*.test.ts` passes,
  23/23, including 7 new tests covering valid/invalid/boundary temperature values, phase
  restricted to 0/1, bare-tank range bounds, an absent extension yielding nulls not zeros, and
  batch values surviving into the RPC input object.
- Not done: no firmware upload, no Supabase migration deploy, no function deploy. All three
  require explicit separate authorization per the implementing session's own instructions.

## Remaining physical bench gates

Everything in "Required validation before unattended/production use" above. Additionally, the
existing MSV3 firmware's own unresolved bench item is unchanged by this work: the electrode-swap
mux-switch-during-shutdown timing assumption (see the file's own header comment and
`docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md`) is still unproven on real hardware, and this
change adds a *second* mux switch per advertising wake (to EC-check code, then back) using the
same shutdown-time-switch assumption - worth specifically watching for in the dry-air serial test
above, not just the original electrode-swap phase.
