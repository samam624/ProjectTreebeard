# Known issues and enforced fixes

This is the portable index for hardware/toolchain problems that are easy to lose when moving the repository to another PC. Detailed investigations remain in their linked documents; this page records what a fresh clone must do.

## DisplayCenter: do not restore per-reading flash writes or queue-based state starvation

**Status:** Resolved, deployed, and repo-enforced as of 2026-09-01.

**Affected:** `DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime`, especially BLE history ingress,
`cloudSyncTask()`, `runCloudAutoCalPull()`, and `runCloudSnapshotUploader()`.

**Symptoms:** Learned percentage cards revert to raw values after restart; the physical display
pauses every few seconds; cloud history becomes intermittent; HTTPS may misleadingly report
`connection refused`. Adding sensors makes the symptoms more frequent.

**Causes:** Three receiver-side errors combined: any nonempty queue blocked the auto-calibration
pull; every unique wake forced a LittleFS flush; and a batch-sized JSON allocation fragmented heap
before mbedTLS reserved its contiguous buffers. Supabase was not dropping accepted requests.

**Portable enforcement:** Build and upload the receiver only through:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action compile
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COMx
```

The wrapper runs `verify_receiver_cloud_recovery.mjs` first. Do not bypass a failure from that
check. The required invariants are: 64-row checksum-protected RTC staging, direct healthy-path
upload/acknowledgement, LittleFS only for outage spillover, state-pull blocking only at the 75%
emergency threshold, cached learned ranges loaded at boot, TLS connection before streamed payload
serialization, no five-second backlog upload loop, and no BLE-callback filesystem/network work.

Supported load is 10-16 sensors at the intended roughly one unique row/minute cadence, including
one three-second bench sensor plus 15 normal sensors. Sixteen probes all producing unique rows every
three seconds indefinitely is unsupported. See
[the complete root cause, architecture, capacity math, durability boundary, and live evidence](../DisplayCenter/docs/RECEIVER_PERCENT_RANGES_DISPLAY_FREEZE_AND_UPLOAD_FIX_2026-09-01.md).

## MSV3: soil-exposed blade coating and routed-edge water retention

**Status:** Confirmed physical failure mode on the current field blade; repair process defined but
not yet validated after re-entry into soil.

**Affected:** MSV3 outer-layer sensing electrodes under solder mask, and potentially any MSV2/MSV3
FR-4 blade that relies on a hand-applied face coating while leaving cut FR-4 edges unsealed.

**Symptom:** After insertion, the moisture-connected count can show a monotonic multi-day shift and
a daily wave larger than the immediate watering response. The signal may later appear stable after
weeks, but that can be saturation/equilibrium rather than a healthy calibrated state.

**Evidence:** On 2026-08-30, visible water pockets were found beneath the MSV3 blade's nail polish.
After the polish was removed, the prior drift reversed as the blade dried and then settled to a flat
air baseline. The correct FDC channel and full 28-bit count had already been verified, and the new
bare-tank reference removed only part of the connected path's temperature slope.

**Portable handling rule:** Do not use nail polish as the production dielectric/moisture barrier.
Dry a previously wet blade until its raw air baseline is stable for 24-48 hours before sealing. Seal
the complete routed FR-4 perimeter first with a narrow, electrically insulating, immersion-rated
epoxy bead that wraps onto both solder-mask faces. After full cure, apply a thin, controlled face
coating and establish a new air baseline. A repaired unit is not accepted until it survives an air ->
soil -> watering -> return-to-air test without retaining a baseline offset.

For the current prototype, the selected materials are MG Chemicals 832HD epoxy on the cut edge and
MG Chemicals 419D acrylic on the faces. V4 should preserve the fact that the electrodes are already
outer copper under solder mask, keep copper and vias away from the soil-exposed perimeter, be fully
depanelized before sealing, and specify post-routing dielectric edge encapsulation. Do not request
conductive edge plating. JLCPCB does not currently list epoxy edge encapsulation as a standard
service; use a confirmed custom process or a secondary coating contractor. See
[the complete field finding, repair process, safety notes, and V4 drawing note](../MSV3/docs/MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md).

## MSV3: missing 32.768 kHz crystal versus stock PCA10056 variant

**Status:** Resolved and repo-enforced.

**Affected:** All `MSV3/arduino/MSV3_*` sketches, the E73-2G4M08S1C module, and Adafruit nRF52 builds using `adafruit:nrf52:pca10056`.

**Symptom:** `Bluefruit.begin()` or RTC1-dependent code hangs; the watchdog resets the MCU; native USB serial disconnects, re-enumerates, and prints the startup command list again. This can look electrical or I2C-related.

**Cause:** The stock PCA10056 Arduino variant selects `USE_LFXO`. MSV3 has no external 32.768 kHz crystal and repurposes `P0.00`/`P0.01`. Both the Arduino core and Bluefruit choose their LFCLK configuration from that variant at compile time.

**Portable enforcement:** Always build with:

```powershell
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 -Action compile
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 -Action upload -Port COMx
```

The wrapper finds the active installed Adafruit nRF52 core, repairs/verifies `variants/pca10056/variant.h` as `USE_LFRC`, and forces a clean build so a stale LFXO object cannot survive. The sketch independently uses `#error` when `USE_LFRC` is absent or `USE_LFXO` remains active. Run `-Action doctor` for a fast environment-only check.

Do not rely on an earlier manual edit under Arduino15: Board Manager upgrades and another PC will not inherit it. See [the complete LFCLK/RTC1 investigation](../MSV3/docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md).

## Adafruit Wire polling loops

**Status:** Observed upstream-core weakness; not required for the resolved MSV3 clock fault and not patched automatically.

Adafruit nRF52 core 1.7.0 contains unbounded TWIM event waits in `Wire_nRF52.cpp`. A temporary local timeout patch was tested during the LFCLK investigation, but it did not change the failure and was not the root cause. A fresh clone intentionally does not mutate that library. If a future trace proves execution is actually stuck inside a Wire call, implement a tracked, version-checked patch or replace the dependency--do not assume the old PC-local experiment exists.

## FreeRTOS tickless idle

**Status:** Enabled; the earlier disable was a discarded diagnostic experiment.

Setting `configUSE_TICKLESS_IDLE=0` did not fix the MSV3 hang. It is not part of the solution and should not be carried to a new PC. Correct LFRC selection makes RTC1/tickless operation viable without changing this installed-core setting.
