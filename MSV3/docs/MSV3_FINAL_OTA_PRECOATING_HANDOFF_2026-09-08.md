# MSV3 final OTA pre-coating handoff

**Date:** 2026-09-08  
**Decision:** the OTA architecture is qualified for the final pre-coating provisioning of probes
4 and 5. Stop adding bootloader features. The remaining work is to build the two production
applications, provision each probe once over SWD, verify it, and coat it.

## 2026-09-09 fleet and coating disposition

- **Probe 6 is the functional first conformal-coating trial.** Its bootloader, signed application,
  BLE identity/build, advancing telemetry, and on-device rollback floor all passed before coating.
  On 2026-09-13, signed bring-up diagnostic v65561 was installed application-only over BLE after
  coating, preserving bootloader 1.0.3. The USB console showed approximately 20.0M DATA_CH0 in the
  untouched condition and 19.0M with two-hand contact: about a 1.0M-count or 5% decrease. This is
  positive qualitative evidence that moisture/contact sensitivity survived coating, but it does
  not replace an air/soil/watering calibration or the connected-to-bare/phase acceptance check.
  Signed production restore v65562 was built and preflighted before the diagnostic install. It was
  restored on 2026-09-13 through bootloader 1.0.3 after positive USB-console identity on COM21. An
  accidental USB-power interruption at the first transfer's validation step was recovered by the
  updater's second attempt. DIS then reported `2026-09-13-prod6-restore`, and a clean 150-second
  scan advanced two advertisements to wake 8 with valid connected/bare readings, 4466 mV on USB,
  flags 0x1c, and the OTA gate open. The v65561 diagnostic exposed a design gap: it required USB
  to return to recovery. Targeted bring-up builds are now build-enforced to open the same bounded,
  power-gated periodic OTA window as production.
- **Probe 7 is sensor-path bricked, not radio/MCU bricked.** Its MCU, bootloader 1.0.3, signed
  application, BLE/OTA path, and rollback floor work, but the TMUX1109 path does not. Connected and
  supposed bare readings mirror within 0.005% while the two electrode-swap phases differ by about
  6.37%, producing the regular 32k/45k history alternation. Its moisture data are unusable.
- No replacement TMUX1109 will be purchased. The planned salvage is to remove the TMUX1109, then
  permanently bridge `TANK_HOT` to `MS_NODE_A` and `TANK_REF` to `MS_NODE_B`. **Do not install
  those bridges while the mux remains populated**: a selected internal route could oppose the
  hard-wired path and short the FDC tank. After bypassing, use a Probe-7-specific application that
  fixes the phase and reports the bare reference invalid; otherwise the receiver could treat the
  permanently connected second reading as a real reference. Retest the direct FDC count before
  coating.
- Probe 7 may be used as a **sacrificial coating-process trial before repair**, but it cannot prove
  that moisture sensing survived the coating, and cured coating will make later mux removal and
  bridging harder. If Probe 7 is intended to become usable, bypass and retest it before coating.
- **Probe 4 remains the actual battery-powered production unit.** Do not apply its final coating
  until the coating application/cure process has first been exercised on probes 6 and 7 and Probe
  6 has passed the post-cure functional check. Probe 4 still needs bootloader 1.0.3 provisioning
  and its complete pre-coating acceptance sequence.
- Former bench probe 99 is now Probe 8. Its first production scan was stable and showed a healthy
  10.97% connected-to-bare separation; it is not part of this first 6/7 coating trial.

## What is now proven

Probe 99 completed the real failure and recovery sequence end to end:

1. Bootloader 1.0.3 was installed after a verified mass erase. All five HEX segments were written
   and read-back verified.
2. Known-good signed application v65552 (`2026-09-07-rescue52`) installed over BLE. DIS reported
   the exact build, 4310 mV, and an open OTA gate.
3. Signed v65553 (`2026-09-08-guard2ram-hang`) deliberately armed the watchdog and hung before BLE
   initialization.
4. The probe restarted at the expected five-second WDT cadence, then entered AdaDFU by itself at
   `DB:AD:4D:9D:21:BB`. There was no SWD write, reset cable, or power cycle during the escape.
5. Signed v65554 (`2026-09-08-guard2ram-rescue`) recovered the probe entirely over BLE.
6. DIS reported the exact v65554 build. A 150-second scan observed two distinct advertisements
   with the wake counter advancing by one, proving normal operation through healthy probation.
7. A manual cold power replug returned to the normal application with valid sensor data and OTA
   open; it did not falsely enter recovery.

This proves the product behavior that matters: a validly signed application that hangs before
advertising can still return to BLE DFU, and a healthy signed application can recover it remotely.

The dedicated physical nRESET-pin case was not run because GPIO7 was intentionally left unwired.
Manual power cycling was used wherever a physical restart was needed. This is recorded, but it is
not a blocker for the pre-coating decision: real watchdog retention, automatic DFU escape, healthy
probation, and cold power start all passed.

Detailed diagnosis and evidence are in
`MSV3/docs/MSV3_BOOT_GUARD_WATCHDOG_RETENTION_2026-09-08.md`.

## Final design

- **Normal OTA:** the application exposes the reset-safe BLE DFU service. The host verifies probe
  identity, package identity, device type, signing key, signature, and application version before
  requesting DFU.
- **Interrupted/invalid application transfer:** the single-bank bootloader remains in continuous
  BLE recovery when no valid application exists. The transfer can be retried.
- **Bad application that boots but hangs:** Guard 2 retains a failure count across watchdog resets
  and requests BLE DFU after five failed boots.
- **Healthy application:** two completed advertising cycles clear probation.
- **Sensor/I2C fault:** Guard 1 keeps the radio reachable even when moisture measurement fails.
- **Rollback:** bootloader 1.0.2+ records a monotonic application-version floor after successful
  signed BLE activation. Equality remains installable as the rescue path.

Guard 2 no longer uses GPREGRET2. The real WDT test proved that value was gone before the next
application read in this stack. Bootloader 1.0.3 and the application instead reserve the same
eight-byte `.noinit` record at `0x2003FFF8`; inverse words reject random or torn state. Both release
builders verify the final linked map, not merely the source declaration.

Bootloader 1.0.2 and older do not reserve this address. Never enable Guard 2 on those bootloaders.

## Current board state

| Probe | Current bootloader | Current application | OTA state | Next action |
|---|---|---|---|---|
| 4 | `msv3-1.0.1` | `2026-09-05-guard1`; exact installed package was not archived | OTA works, but bootloader is not Guard-2-compatible | Final SWD provisioning before coating |
| 5 | `msv3-1.0.3` | v65555 `2026-09-08-prod5`; Guard 1 + Guard 2 on; serial logs off | DIS and advancing-wake scan passed | Read back settings page over SWD before coating |
| 6 | `msv3-1.0.3` | v65556 `2026-09-08-prod6`; Guard 1 + Guard 2 on; serial logs off | Full pre-coating checks passed | Functional first coating trial; post-cure sensor/BLE check |
| 7 | `msv3-1.0.3` | v65556 `2026-09-08-prod7`; Guard 1 + Guard 2 on; serial logs off | MCU/radio healthy; TMUX sensor path failed | Mux-free bypass before functional use, or sacrificial coating-process trial |
| 8 | `msv3-1.0.3` | v65557 `2026-09-08-prod8`; Guard 1 + Guard 2 on; serial logs off | DIS, advancing wakes, and 10.97% reference separation passed | Continue plotting; settings-page readback before any coating |

`MSV3/fleet.json` is the source of truth for addresses, trust roots, installed bootloaders, and
application-version bookkeeping.

## What is and is not the shipping image

The v65554 probe-99 package is a retained historical qualification artifact, **not** the current
application on that hardware. The former bench board now runs the production Probe 8 image. The
v65554 package contains sensor ID 99, a bench build tag, and serial diagnostics.

Each production application must have:

- its own correct `SensorId`;
- Guard 1 and Guard 2 enabled;
- no `HangTest` flag;
- no `SerialLogs` flag;
- a unique, archived build tag;
- an explicit application version higher than the current shared test maximum (use at least
  65555 unless a later build has already consumed it);
- a signed per-probe OTA ZIP whose embedded identity and build ID are verified before use.

Use the tracked wrappers only. Never compile or upload an MSV3 sketch with a raw Arduino CLI or
the IDE upload button.

## Remaining pre-coating checklist

For each probe, one at a time:

1. Run `MSV3/scripts/arduino_msv3_probe.ps1 -Action doctor`.
2. Build the per-probe production release with `build_msv3_release.ps1`, bootloader release 1.0.3,
   Guard 2 enabled, and neither HangTest nor SerialLogs enabled.
3. Confirm the package reports the intended sensor ID, application version, build ID, device type
   `0x3356`, and the pinned signing key.
4. Start `scripts/swd_flash_hex.py` first, then place the SWD probes when prompted. Use
   `--msv3 1.0.3 --no-reset`; GPIO7/nRESET is not required when a manual power replug is available.
5. Keep SWDIO, SWCLK, and GND stable until every segment reports `verify OK`.
6. Release the SWD probes and manually replug power. A mass-erased board should advertise AdaDFU
   because it has no application.
7. Install that board's signed production application with
   `scripts/ble_ota_update.py --probe N --skip-jump <package>`.
8. Read back the exact DIS build ID. Observe at least two distinct, advancing wake counters with
   `scripts/msv3_fleet_scan.py --probe N --require-progress`.
9. Update `MSV3/fleet.json` with bootloader 1.0.3, the installed application version, build tag,
   and measured evidence.
10. Archive an equality-version rescue package for that exact probe. Do not coat until this file,
    its SHA-256, and the signing-key backup are confirmed.
11. Coat the probe. Future application updates are BLE-only; do not attempt bootloader replacement
    over BLE.

Probe 5 completed the wired provisioning and signed BLE application install on 2026-09-08. Probe 4
still needs the wired session because Guard 2 depends on the 1.0.3 bootloader RAM reservation; an
application OTA alone cannot add that reservation to its current 1.0.1 bootloader. Probe 6 is fully
accepted before coating. Probe 7 is not sensor-functional and has the mux-free salvage disposition
recorded above. Former Probe 99 now runs as Probe 8 and has passed its initial sensor/BLE scan.

## Artifacts retained from the qualification run

- Bootloader:
  `MSV3/bootloader/generated/1.0.3/msv3-e73-recovery-signed-s140-6.1.1.hex`
  - SHA-256: `a6e0ac0836bed2e1e426d24bbeeb33076929ce3ed1d86dd71fc55e148adb760d`
- Deliberate v65553 hang package:
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor099-v65553-signed-ota.zip`
  - SHA-256: `d4b9340e0d95d0043acf2d9c214194ebb9deab837b0dd0da8e1d42134ebc261b`
- Healthy v65554 rescue package:
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor099-v65554-signed-ota.zip`
  - SHA-256: `98d57e4740868d330cb39e56abb36d65e8339882c733f01a763dba0314a2bc1f`
- Probe 5 production/equality-version rescue package:
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor005-v65555-signed-ota.zip`
  - SHA-256: `0198dd469cbc2ba411b73f310c5d0054e84abe9a5bd7c0dc1db60faf46ddf735`
- Probe 6 production/equality-version rescue package:
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor006-v65556-signed-ota.zip`
  - SHA-256: `adfa0910a26874906aed5611c1308c2805c69205cbd319f5fe15620df68d408d`
- Probe 7 production/equality-version rescue package (sensor image is retained, but hardware is
  not deployment-ready):
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor007-v65556-signed-ota.zip`
  - SHA-256: `e40c8f0b712a9f72d322f14a512d4ef2484e76929c5bdd34457beeb617ec6c6f`
- Probe 8 production/equality-version rescue package:
  `MSV3/bootloader/generated/1.0.3/msv3-1.0.3-sensor008-v65557-signed-ota.zip`
  - SHA-256: `9c6955c104fe544c15f11ea24c52f08d01201431e88ff0764c5512815a8bacf5`
- Watchdog/USB capture: `dumps/guard2/retained-ram-watchdog-acceptance.log`
- Healthy probation scan: `dumps/guard2/probe99-v65554-probation.json`
- Cold-start scan: `dumps/guard2/probe99-v65554-cold-start.json`

## Stop point

Probe 6 is the healthy functional coating trial. Probe 7's sensor path is unusable until its mux is
removed and the direct bypass plus matching firmware are installed; coating it first makes it a
sacrificial process trial. Probe 8 (formerly 99) is healthy and plotting under sensor ID 8. Probe 5
is healthy on bootloader 1.0.3 and application v65555, but its final settings-page SWD readback
remains before coating. The 4411 mV Probe 5 verification reading was USB power with no battery
attached, not an overvoltage cell measurement; DMM any cell when one is fitted. Battery-powered
Probe 4 remains the production unit and has not yet received final provisioning or coating. The
working tree contains unrelated receiver/cloud temperature-compensation work as well as these OTA
changes, so no broad commit was made automatically.
