# Probe 4 FDC settling OTA experiment

**Prepared:** 2026-09-10  
**Target:** Probe 4, `MSV3-MOISTURE-004`, `F2:16:32:1B:2B:47`  
**Purpose:** Separate FDC2214 per-conversion settling from a longer cold-wake transient without
mixing in the newer deglitch-filter change.

## Baseline boundary

The existing battery/solar Probe 4 history is the baseline. Its installed application identifies
itself over DIS as `2026-09-05-guard1`; the exact old package was not archived, so its application
version is unknown. The relevant deployed FDC settings are:

- `SETTLECOUNT_CH0 = 0x0064` (100)
- two discarded fresh conversions after each FDC shutdown/wake
- the FDC2214 power-on 33 MHz deglitch setting
- `RCOUNT_CH0 = 0xFFFF`

The 24-hour baseline analysis found about 12,030 ppm (1.203% peak-to-peak) daily amplitude in the
reference-normalized connected signal, with a strong temperature relationship and temperature
leading by about 20-30 minutes. The hard-wired USB Probe 8 had a much smaller roughly 3,403 ppm
(0.340% peak-to-peak) daily amplitude despite a similar board-temperature cycle.

Read-only BLE health immediately before the attempted update, at 2026-09-10 16:52 local time:

- wake counter 6998
- battery 4178 mV and OTA gate open
- mean RSSI -67 dBm over 14 packets
- die temperature 25.50 C
- connected `DATA_CH0` 20,014,706; bare reference 22,471,641
- flags `0x1e` (valid temperature, valid bare reference, OTA allowed)

## One-variable stages

All packages retain sensor ID 4, the same BLE/cloud payload and cadence, `RCOUNT_CH0=0xFFFF`, the
old 33 MHz deglitch behavior, serial logs off, and Guard 2 off. Guard 2 must remain off because
Probe 4's bootloader is 1.0.1 and does not reserve its retained-RAM record.

| Stage | App version / build ID | SETTLECOUNT | Discards | Purpose |
|---|---|---:|---:|---|
| A | v65558 / `2026-09-10-p4-settle1024` | 1024 | 2 | Change only the FDC's settling interval |
| B | v65559 / `2026-09-10-p4-discard8` | 100 | 8 | Restore settling; change only time/conversions since cold wake |
| Control/restore | v65560 / `2026-09-10-p4-control` | 100 | 2 | Return to baseline FDC settings under the reproducible current source |

The signed OTA artifacts are:

- `MSV3/bootloader/generated/1.0.1/msv3-1.0.1-sensor004-v65558-signed-ota.zip`  
  SHA-256 `747c16074dea03cee3d123f68f4111052130f965a5448845183bab9972097bca`
- `MSV3/bootloader/generated/1.0.1/msv3-1.0.1-sensor004-v65559-signed-ota.zip`  
  SHA-256 `fb0f7eedf7324ae66ccbb0be88438c211b5f7dd004e566ce73a9e341e89b2a31`
- `MSV3/bootloader/generated/1.0.1/msv3-1.0.1-sensor004-v65560-signed-ota.zip`  
  SHA-256 `932d998d98fc413a05c59fe10444607fb7ff076eb38922543b7ab8c198208586`

Each package passed the target-specific OTA preflight: P-256 signature against Probe 4's pinned
1.0.1 trust root, device type `0x3356`, application version, sensor identity, and Guard-2 pairing.
The complete 1.0.1 release directory also passed `verify_msv3_release_archive.py`.

## Run sequence

Do not water, move the probe/pot, alter the solar illumination schedule, or change the nearby heat
load within a stage. Run each stage for a full 24 hours, preferably 48 hours if successive days do
not repeat. Record the actual OTA completion timestamp; the wake-counter reset is the cloud-side
epoch boundary.

Install stage A first from the repository root:

```powershell
.\MSV3\.arduino-build\python-portable\runtime\python.exe .\scripts\ble_ota_update.py `
  --probe 4 --prn 4 `
  .\MSV3\bootloader\generated\1.0.1\msv3-1.0.1-sensor004-v65558-signed-ota.zip
```

After installation, read back DIS and observe at least two advancing advertisement counters before
starting the 24-hour clock. Record `installed_application_version = 65558` in `MSV3/fleet.json`
only after both checks pass. Repeat with v65559 after the stage-A data window. Use v65560 after
stage B, or earlier only if ending the experiment and restoring the baseline FDC settings. Keeping
the restore image at the highest version preserves a forward-only recovery path.

## Interpretation

- If stage A sharply reduces the daily swing but stage B does not, `SETTLECOUNT_CH0=100` is too
  short for the thermally/power-loaded field tank even though discarded conversions follow it.
- If stage B sharply reduces the swing but stage A does not, the error depends on elapsed
  conversions/time after the complete FDC shutdown and reconfiguration, not the programmed
  settling interval alone.
- If both reduce it, startup settling is involved, and a smaller follow-up sweep should find the
  lowest robust setting rather than shipping the largest diagnostic value.
- If neither reduces it, the evidence favors real temperature/power/soil-path sensitivity over an
  FDC startup artifact. Compare the connected count, bare reference, connected/bare ratio, board
  temperature, battery voltage, and lagged temperature correlation separately before deciding.

The USB-versus-battery comparison by itself cannot prove settling because the production firmware
fully shuts down, reconfigures, and discards conversions from the FDC on both power sources. This
controlled sequence changes the two remaining settling mechanisms one at a time.

## Current deployment status

Stage A was installed successfully on 2026-09-11. The successful updater was run outside the
workspace sandbox; passive advertisements worked inside it, but every active WinRT GATT open there
failed with `E_ABORT` before the DFU write. Outside the sandbox, the application connected in
2.92 s, reported the old `2026-09-05-guard1` build, acknowledged the DFU jump, and its pinned
bootloader appeared at the registered application address. The 132,560-byte signed image
transferred at negotiated MTU 247 with PRN 4, then passed Verify, Validate, Activate & Reset and
reported `DFU Complete` in 50 s (2.61 kB/s).

DIS readback reported exactly `2026-09-10-p4-settle1024`; the updater refused a redundant reinstall
before another jump. A 150-second acceptance scan started at 2026-09-11 21:45:56 PDT and observed
26 packets across two distinct advancing wakes (+1, ending at wake 4), mean RSSI -71 dBm, 4229 mV,
flags `0x1e`, valid temperature and bare reference, connected count 19,470,723 and bare count
22,480,580. `MSV3/fleet.json` now records installed application version 65558. Use that first
post-flash wake-counter epoch as the Stage-A observation boundary.
