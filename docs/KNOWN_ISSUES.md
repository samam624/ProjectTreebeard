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

## MSV3: capless production must never attempt a bare-tank conversion

**Status:** Decided, repo-enforced, compiled, and signed as application v65564 on 2026-09-14; not
yet deployed because the capless-aware DisplayCenter must be installed first.

**Affected:** Every healthy MSV3 running the capless production image. C17 is DNP/removed. Probe 7
is excluded until its separate TMUX bypass repair is complete.

**Failure mode:** With the electrodes disconnected, the C17-free tank is only about 25-30 pF and
resonates around 11-12 MHz. Probe 8 returned exact FDC over-range `0x0FFFFFFF` and `ERR_WD` even at
minimum IDRIVE. Raising drive does not correct a frequency-range violation. An old production image
would still select mux 2/3 on each advertising wake and manufacture an invalid reference reading.

**Portable enforcement:** Production selects only the connected phase (0/1) and sleep code 0,
sets capless flag bit 6, clears legacy bare-valid bit 3, and zeros bytes 19-22. DisplayCenter maps
bit 6 to scale 3, accepts the higher valid connected counts, starts a new calibration epoch, and
forces reference-based temperature compensation off. `verify_msv3_capless_policy.mjs` is part of
the standard receiver compile/upload gate and fails if those invariants regress. The production
IDRIVE and all release-build defaults are code 16.

**Rollout rule:** receiver first, then signed capless application, then remove all power and remove
C17, then verify both connected phases and amplitude/frequency margins before recalibration. Never
install capless firmware on a still-legacy receiver, and never leave capless hardware running an
old image. See
[the complete evidence, package hashes, and rollout checklist](../MSV3/docs/MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md).

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

## MSV3 Probe 7: TMUX1109 path failure

**Status:** Confirmed from live two-phase telemetry on 2026-09-08. Probe 7 is not
deployment-ready. Its MCU, bootloader, signed application, BLE/OTA path, and rollback record are
healthy; “bricked” here means the moisture-sensing path is unusable, not that the board cannot boot
or be updated.

**Evidence:** Phase 0 connected/bare counts were 19,502,047/19,502,915 and phase 1 counts were
20,784,705/20,784,074. The supposed bare reading follows the connected reading within 0.005% in
both phases, proving that MUX1/A1 has no observable selection effect. The connected phases differ
by 1,282,658 counts (about 6.37%), repeatedly triggering swing advertisements and producing the
32k/45k dashboard alternation.

**Disposition:** No replacement TMUX1109 will be ordered. To salvage Probe 7, first remove the
TMUX1109, then bridge `TANK_HOT` to `MS_NODE_A` and `TANK_REF` to `MS_NODE_B`. Never add those
bridges around a populated mux; an internal selected route can oppose the hard wiring and short the
FDC tank. The bypass build must hold one phase and advertise the bare reference as invalid. Retest
the direct count before coating. If Probe 7 is coated first as a process test, treat it as
sacrificial: it cannot validate moisture sensing and the cured coating will obstruct the planned
repair.

Probe 6 and Probe 7 are the first conformal-coating process trials. Probe 6 is the functional
post-cure validation unit; Probe 7 is process-only unless repaired first. Battery-powered Probe 4
is the actual production unit and remains uncoated until those trials establish the application and
cure process.

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

## MSV3: every fleet-targeted application must expose a periodic OTA window

**Status:** Enforced in the guarded build as of 2026-09-13.

**Affected:** Any `MSV3_Moisture_BLE_Sleep` or `MSV3_Probe_Bringup_Test` build made with
`-SensorId`. An untargeted wired bench build may omit the window, but must never be signed and
installed on a probe whose USB/SWD access may disappear.

**Failure that established the rule:** Probe 6's signed post-coating bring-up diagnostic v65561
had a USB `dfu` command but no application-side GATT DFU service and no automatic advertisement.
After its sensitivity test, BLE-only production restore was impossible even though bootloader
1.0.3 remained intact; the board had to be reconnected by USB data solely to enter recovery.

**Portable enforcement:** `MSV3/scripts/arduino_msv3_probe.ps1` inspects every targeted build and
refuses it unless it contains `ResetSafeBLEDfu`, DIS build identification, a periodic bounded
advertising cadence, the USB/battery authorization gate, the deferred ATT-response handling, and
the reset-safe handoff. The targeted bring-up sketch now reuses production's proven DFU service and
opens a non-blocking five-second window every 60 seconds. It publishes the exact build through DIS
and retains the USB `dfu` command as an immediate fallback. Its diagnostic advertisement keeps the
separate `MSV3` magic so a receiver cannot ingest placeholder diagnostic data; the guarded updater
recognizes that prefix only for identity and OTA-power checks.

Targeted diagnostic windows initialize the SoftDevice and therefore are not the lowest-current
bench configuration. Omit `-SensorId` for wired sleep-floor experiments; use `-SensorId` for any
image that could remain on a coated or otherwise inaccessible fleet probe.

## MSV3: the bootloader accepts an OLDER signed image -- rollback is not prevented on the device

**Status:** Measured 2026-09-05 on bootloader 1.0.1. Fixed in bootloader 1.0.2 and retained in
1.0.3. Probes 5-8 now run 1.0.3. Probe 5 installed signed v65555 over BLE, with its final settings
readback still pending. Probes 6 and 7 installed signed v65556 and passed settings-page readback at
floor 65556. Former Probe 99 now runs as Probe 8 on signed v65557, with its settings readback still
pending. Probe 4 still runs 1.0.1 and needs final 1.0.3 SWD provisioning; until then, its host-side
version bookkeeping is its only rollback guard.

**The 1.0.2 fix.** `MSV3/bootloader/patches/msv3-1.0.2-anti-rollback.patch`, applied and sentinel-
verified by `build_msv3_release.ps1`. It stores a monotonic version floor in the bootloader settings
page and refuses any application below it. Three design points are load-bearing:

- **The check runs AFTER the init packet's P-256 signature verifies**, so an unauthenticated peer
  cannot use the response to probe the stored floor.
- **Equality passes.** Reinstalling the current version is the recovery path after a refused
  rollback erases the application, so it is `<` and not `<=`.
- **The floor advances only at `DFU_UPDATE_APP_COMPLETE`**, which is reached only after
  `dfu_init_postvalidate()` has checked the full image length, SHA-256 and signature. An
  interrupted or corrupted higher-version transfer therefore cannot raise the floor above an image
  that still works. It is folded into the ONE existing settings save, because
  `pstorage_callback_handler()` ends the whole update on any store completion arriving while
  `m_update_status == BOOTLOADER_SETTINGS_SAVING` -- a second save would arm that gate twice, which
  is the known dual-bank defect.
- **An absent or torn floor fails OPEN** (accepts anything) rather than closed. A settings page
  written by 1.0.0/1.0.1, or a freshly erased one, reads as "not recorded". Failing closed on an
  unreadable floor would let one interrupted write turn a coated probe into one that refuses every
  package including its own rescue image.

A rollback refusal is reported as `BLE_DFU_RESP_VAL_NOT_SUPPORTED` at the **Init Packet** stage.
That value is otherwise unreachable there -- its only other source is `dfu_start_pkt_handle()`, i.e.
Start DFU -- so it means "below the floor" and cannot mean anything else. That is deliberate: it
makes the acceptance test read a specific rejection instead of inferring one from the generic
`OPER_FAILED` that a bad signature or a corrupt package also produces.

### 1.0.2's accepted limitations — read before relying on it

**It is not power-fail monotonic, and the failure is quiet.** `bootloader_settings_save()` erases
the single settings page before storing the replacement. Flash writes proceed low address to high,
and the version tuple is appended at offsets 28–39, *after* `bank_0`. So a brownout part-way through
the 40-byte store can leave **a valid `bank_0` and an incomplete floor tuple**: the probe boots its
application normally while the reader sees no valid magic and returns floor 0, silently reverting to
1.0.1 acceptance behaviour until the next successful update rewrites the record.

The first version of this note claimed the same erase always destroys `bank_0` too, so the probe
would land in obvious BLE recovery. **That is wrong** — it is one of two possible outcomes, and the
quiet one is the other. The power-cut acceptance matrix must check for both.

Accepted rather than engineered around, deliberately. Redundancy means a second settings page and a
second store, and any store completion arriving while `m_update_status == BOOTLOADER_SETTINGS_SAVING`
ends the update without checking handle or which write finished — the completion-gate defect that
made dual-bank fail on this exact hardware. Trading a ~100 ms window during an attended update for a
second arming of a known-defective gate, in the one component that can never be re-flashed, is the
worse risk. **The mitigation is that the host-side floor is not optional:** keep
`installed_application_version` accurate in `MSV3/fleet.json` for every probe, because after a torn
write it is the only thing still refusing an old package.

**The floor must be seeded deliberately; a fresh install does not seed it.** After a mass-erase and
SWD install of SoftDevice + bootloader, the settings page is erased and the floor reads 0. Nothing
advances it except a completed `DFU_UPDATE_APP_COMPLETE`, which only a **BLE DFU** install produces.
Flashing the application over SWD leaves a probe that accepts every older signed package. So
provisioning must finish with a signed OTA install, and the settings page must be read back over SWD
to confirm magic, floor and complement before the pads are covered.

Seed each production probe to an explicit version above that target's recorded on-device floor.
The bootloader does not enforce sensor identity — that is a host-side
check — so a package built for another probe would be cryptographically acceptable and could push a
floor above that probe's own rescue image. Archive a correctly identified equality-version rescue
package for each probe before coating; equality is accepted so that package remains installable.

**The UF2 path is unreachable in this build, and that is a security condition, not a UI detail.**
`src/usb/msc_uf2.c` is compiled in for the nRF52840 and can reach `DFU_UPDATE_APP_COMPLETE` with no
signature and no version check. It is unreachable only because `DEFAULT_TO_OTA_DFU` is set:
`main.c` runs `if (!valid_app || dfu_start) { _ota_dfu = 1; }` immediately before the DFU branch
guarded by exactly `if (dfu_start || !valid_app)`, so every entry into DFU takes the BLE path and
`usb_init()` is never called. **Do not build without `DEFAULT_TO_OTA_DFU=1`.** (For probes 4 and 5
the coating also physically covers the USB connector, but that is a second line, not the first.)

**1.0.2 does NOT fix erase-before-authenticate.** The application is still erased at Start DFU,
before any init packet is seen, so even a refused rollback costs the running firmware. Deferring the
erase would require Start to complete before erasing, Init completion to wait on an asynchronous
erase, and the flash-completion handler to distinguish which command it is completing -- the same
state-machine restructuring implicated in the completion-gate defect above. Recovery from the erase
is proven; a bricked bootloader is not recoverable at all. Deliberately out of scope.

**Measured, not inferred.** Probe 99 was installed with application version 65540 and its build id
read back over BLE. It was then offered version 65539 -- one lower, same identity, same signing
key. It transferred in 35 s and booted it, and the readback reported the older build.

**Why.** `dfu_init.c` carries an explicit empty template where the check would be: *"To support
application versioning, this check should be updated. This template allows for any application to
be installed."*

**What it is and is not.** A freshness limitation, not an integrity failure: a rollback image must
still carry a valid signature from the trusted key, and every mutated-image result still holds
(`MSV3/docs/MSV3_TAMPERED_IMAGE_REJECTION_2026-09-05.md`). What it means in practice is that **any
package ever signed for a probe can be reinstalled on it at will** -- and that is not harmless,
because rolling back past guard 1 restores the defect where a sensor fault closes the radio and
makes a coated probe unreachable. A rollback can re-introduce a fixed unrecoverability bug.

**Mitigation, host-side only.** `MSV3/fleet.json` records `installed_application_version` per probe
and `ble_ota_update.py` refuses anything below it without `--allow-downgrade`. Update that field
after every successful transfer; `0` means "not recorded" and disables the check. This cannot stop
anyone who does not run the tool -- it stops the likely failure, which is reaching for a stale
package by mistake.

**A real fix** persists a monotonic version in the bootloader settings page and refuses anything
lower. That is a bootloader change, so installing it safely needs SWD attached. The *test* needed
no SWD; the *fix* does.

## MSV3: the production sketch will not build without an explicit sensor identity

**Status:** Enforced in the build as of 2026-09-05.

**Affected:** `MSV3/arduino/MSV3_Moisture_BLE_Sleep` only. The bring-up and OTA-test sketches have
no `SENSOR_ID` and are unaffected.

**What a fresh clone must do:** pass the identity, never edit it.

```powershell
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 `
    -Action compile -Sketch arduino/MSV3_Moisture_BLE_Sleep -SensorId 4
# bench board, with USB logging and a distinguishable build id:
    ... -SensorId 99 -SerialLogs -BuildTag 2026-09-05-bench
```

Omitting `-SensorId` is a hard error from the wrapper, and the sketch itself `#error`s without
`MSV3_SENSOR_ID`, so a stale identity cannot reach a probe by omission.

**Why it is enforced rather than documented.** `SENSOR_ID` used to be a constant edited in place.
Committed source sat at id 5 — a probe that cannot even accept an OTA — so every build for the
deployed id-4 probe began with an uncommitted edit that then had to be reverted from memory. The
cost was not hypothetical: **probe 4's running firmware cannot be rebuilt from this repository**,
because it was built from a working tree no commit describes and its package went to the gitignored
`.arduino-build/` path that later builds overwrote. `(commit, sensor id, build tag)` now reproduces
an image exactly.

`ENABLE_SERIAL_LOGS` moved the same way (`-SerialLogs`, default off), so committed source always
describes a deployable image. `-BuildTag` passes an alphanumeric token that the sketch stringifies —
a quoted string cannot be relied on to survive PowerShell, `--build-property` and the compiler
intact, and a build id that silently reverted to the default would defeat the one thing it is for.

## MSV3: never read `NRF_POWER->RESETREAS` directly — the Arduino core already consumed it

**Status:** Fixed in the application and enforced in the build, 2026-09-06. This is the **third**
instance of the same class in this subsystem.

**Affected:** any `MSV3/arduino/*` code wanting the reset cause.

**Symptom:** the reset cause reads `0x0` on every boot, after a watchdog reset and after
`NVIC_SystemReset` alike. Nothing crashes; a value is simply always zero, which is why this one
survived two days as a documented *hardware* fact and was nearly used to justify rebuilding the
bootloader.

**Cause:** the Adafruit core's `init()` runs before `setup()`, reads the register, write-1-to-clears
it, and saves the value for the application (`cores/nRF5/wiring.c:35-40`, exposed as
`readResetReason()` at line 72). The bootloader does **not** consume it — `src/main.c:307` says so
explicitly. The earlier write-up in
[`MSV3/docs/MSV3_UNREACHABLE_PROBE_GUARDS_2026-09-04.md`](../MSV3/docs/MSV3_UNREACHABLE_PROBE_GUARDS_2026-09-04.md)
blamed the bootloader and is corrected in place.

**Fix:** `uint32_t reason = readResetReason();` — declared in `wiring.h`, reached via `Arduino.h`.

**Portable enforcement:** `MSV3/scripts/arduino_msv3_probe.ps1` fails the build (including
`-Action doctor`) on any direct `NRF_POWER->RESETREAS` access under `MSV3/arduino/`, comments
excluded. The check is deliberately narrow: `bootGuardCheck()` still writes `GPREGRET` directly to
request DFU before `Bluefruit.begin()`. The failure counter no longer uses GPREGRET2; bootloader
1.0.3 and the application reserve a shared `.noinit` RAM record instead.

**The pattern worth remembering** — reaching for a register the framework has already mediated:

| instance | symptom | mediator that was bypassed |
|---|---|---|
| guard 2, first build | reset every ~76 s | `sd_power_gpregret_*` SVCs, with the SoftDevice up |
| FDC `ERROR_CONFIG`/`DRDY` | every conversion read "fails" | per-channel `CH0_UNREADCONV`, not global `DRDY` |
| `RESETREAS` | cause always `0x0` | core's `readResetReason()` |

## MSV3: never poll `STATUS.DRDY` on the FDC2214 — it never updates

**Status:** Recurring. Fixed in the production sketch 2026-08-30 and in the bring-up sketch
2026-09-03. This is the **third** time the project has hit it.

**Affected:** any FDC2214 read path in `MSV3/arduino/*`.

**Symptom:** every conversion read fails while the I2C bus is provably healthy. In the bring-up
sketch this surfaces as a stream of `I2C read failed` even with nothing attached to the board; in
the production sketch it silently suppressed every BLE advertisement. The tell is that **`fdcid`
still returns correct values** (`MANUFACTURER_ID=0x5449`, `DEVICE_ID=0x3055`) while every
`fdcread`/`fdcstream`/`muxsweep` fails — a broken bus cannot read two registers correctly.

**Cause:** `ERROR_CONFIG` (0x19) resets to all zeros, and neither sketch's `fdcInit()` writes it.
Per Table 7-37, `DRDY_2INT=0` *prevents `STATUS.DRDY` from being updated at all*. A DRDY poll
therefore always times out, and because that timeout shares a return path with genuine I2C errors,
it reports itself as an I2C failure.

**Fix:** poll `STATUS.CH0_UNREADCONV` (bit 3, `0x0008`) instead — the per-channel flag, which does
update. Do not "simplify" it back to `STATUS.DRDY` without also explicitly writing and verifying
`ERROR_CONFIG.DRDY_2INT` after every hardware shutdown, since shutdown resets the register.

**Also affected by the same register — corrected and resolved 2026-09-05.** Both sketches now write
`ERROR_CONFIG = 0x0020` (`WD_ERR2INT` only) in `fdcInit()`, immediately before the `CONFIG` write
that starts conversions. Full record:
[`MSV3/docs/MSV3_FDC_ERROR_CONFIG_2026-09-05.md`](../MSV3/docs/MSV3_FDC_ERROR_CONFIG_2026-09-05.md).

The earlier wording here grouped three flags together and was wrong about two of them:

- **`STATUS.ERR_WD` (bit 11) was gated** by `WD_ERR2INT` and could not set. Measured on the bench:
  under an induced fault, and after any previously latched status had been cleared, `ERR_WD` was
  reported on 6 of 6 settled reads with the bit set, was not newly reported with it clear (0 of 6),
  and returned on 6 of 6 when the bit was set again.
- **`STATUS.ERR_AHW` (10) and `ERR_ALW` (9) are not gated by this register.** `ERROR_CONFIG` has no
  `AH_WARN2INT`/`AL_WARN2INT` bit at all; `AH_WARN2OUT`/`AL_WARN2OUT` control reporting into
  `DATA_CHx.CHx_ERR_AW`, a different destination. That is a statement about the configuration
  semantics — neither amplitude warning was induced on hardware, so their end-to-end operation is
  not demonstrated here.
- The three `*2OUT` bits stay 0 deliberately: their destination is `DATA_CHx` bits 13/12, which
  `fdcReadChannel0()` masks off with `& 0x0FFF`, and everything they would report is already in
  `STATUS`.

**`ERR_WD` was never the detector for a permanently dead sensor**, which the old wording implied. A
watchdog timeout does not produce an unread conversion, so `CH0_UNREADCONV` never sets and the read
fails on the readiness timeout — a path that always worked. What was lost was the *reason*, because
`fdcWaitForCh0UnreadConversion()` discarded the `STATUS` it had already read. Since 2026-09-05 it
OR-accumulates every flag it observes and returns it even on a timeout (OR, not last-value:
`STATUS` is read-to-clear, so the polling loop consumes the flags it looks past).

**That is a behaviour change, not only a diagnostic one.** On the timeout path nothing moves. But a
successful read now returns the union of what was seen while polling, so a watchdog event during
the poll reaches `probeAndAdvertise()`'s `errWd` branch and that one reading is discarded. Measured:
the first completed conversion after a fault cleared came back as `STATUS=0x808` with a valid
count. This is the conservative direction and it costs at most one sample per fault, because
`STATUS` is read-to-clear — but it changes what gets stored.

**Do not treat the healthy-path evidence as complete.** No false `ERR_WD` appeared in 58 healthy
bench measurements including six cold sleep/wake cycles, which is a bench sample on one board —
not a statement about temperature, supply or long-run behaviour. The change is staged on probe 99;
probe 4 gets it after a longer run. A false positive here silently takes a *healthy* probe off the
air, which is worse than the fault it detects.

## Adafruit Wire polling loops

**Status:** Observed upstream-core weakness; not required for the resolved MSV3 clock fault and not patched automatically.

Adafruit nRF52 core 1.7.0 contains unbounded TWIM event waits in `Wire_nRF52.cpp`. A temporary local timeout patch was tested during the LFCLK investigation, but it did not change the failure and was not the root cause. A fresh clone intentionally does not mutate that library. If a future trace proves execution is actually stuck inside a Wire call, implement a tracked, version-checked patch or replace the dependency--do not assume the old PC-local experiment exists.

## FreeRTOS tickless idle

**Status:** Enabled; the earlier disable was a discarded diagnostic experiment.

Setting `configUSE_TICKLESS_IDLE=0` did not fix the MSV3 hang. It is not part of the solution and should not be carried to a new PC. Correct LFRC selection makes RTC1/tickless operation viable without changing this installed-core setting.
