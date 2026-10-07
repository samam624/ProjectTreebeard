# MSV4 outdoor solar charge test — started 2026-10-06

Dated record of a test in progress. Current physical state lives in `MSV3/fleet.json`
(`msv4_boards`), not here.

| | |
|---|---|
| **Started** | **2026-10-06 14:57 PDT** |
| Status | Running |
| Ended | — |
| Board | Board 1, `MSV4-BU-AD95` (`F6:26:7D:76:AD:95`) — the only MSV4 bring-up board seen in a BLE scan at 15:02 |
| Location | Maintainer's home. Called an "outdoor" test at the start; at 22:10 the maintainer said it is sitting in a window, in air, for window charging, with an MSV3 probe beside it |
| Question | Does the panel charge the cell over time, and is the BQ25504 wired correctly? |

## Stated by the maintainer at the start

- The board is at home after oscilloscope checks in the lab, which "all checked out". That scope
  pass is written up in [the board 1 bring-up record](MSV4_BOARD1_BRINGUP_2026-10-01.md), which
  closes its `phase 0/4/8/12` timing item.
- The test is an outdoor solar panel test of battery charging and of the BQ25504 wiring.
- The panel is "the new one in the BOM, the lower voltage small 30x36 one": the 2 V / 60 mA /
  0.12 W, 36 x 30 mm panel.
- The cell has "been dropping for a couple days now".

## Starting conditions

Blank rows were not stated and are not filled in from an older document.

| Item | Value | Basis |
|---|---|---|
| Cell voltage at start | **3.814 V** at 15:03, six minutes after the start mark | measured, `vbat` over BLE |
| 3V3 rail | 3.306 V | measured, `vdd` |
| Tip NTC / die temperature | 26.6 °C / 31.5 °C | measured, `temp` |
| Firmware | `bringup-dietemp`, `app_version=262147` | measured, `info` |
| Last reset | pin reset (`reset_reason=0x1`), uptime 912 s at 15:03, so about 14:48; `boot_count=1` | measured, `info` |
| Panel fitted | 2 V / 60 mA / 0.12 W, 36 x 30 mm (the BOM panel) | maintainer |
| Panel `V_OC` and `I_SC` in direct sun, disconnected from the board | | |
| SP+ zener fitted? | | |
| Placement: orientation, shading, hours of direct sun | | |
| Enclosure / electrode in soil? | | |

## Thresholds this board was built to

Nominal values from the fitted resistor values in
[the BOM](MSV4_PROBE_BOM_2026-09-16.md). **Modelled, not measured on this board.**

| Threshold | Nominal | Over VBIAS 1.21–1.27 V |
|---|---:|---:|
| `VBAT_OV` (charging stops) | 3.889 V | 3.765–3.951 V |
| `VBAT_OK` rising (load switched on) | 3.728 V | 3.61–3.79 V |
| `VBAT_OK` falling (load switched off) | 3.369 V | 3.26–3.42 V |
| `VBAT_UV` | 3.040 V | 2.94–3.09 V |
| MPPT setpoint | 77.9 % of panel `V_OC` | |
| BQ25504 `VIN_DC` recommended maximum | 3.0 V | |
| Panel acceptance `V_OC` | ≤ 2.8 V | |

Modelled charge rate for the 2 V / 60 mA panel in direct sun is 25–30 mA into the cell. The usable
window (3.37–3.89 V) is about 30 mAh of the 60 mAh cell, so from the bottom of that window the cell
would reach `VBAT_OV` in roughly 1.0–1.2 h of full direct sun.

## How each outcome reads

| Observation | Meaning |
|---|---|
| Cell starts below ~3.85 V and rises through sun hours | Charger is harvesting and the boost path is wired correctly |
| Cell rises, then holds at 3.77–3.95 V in continued sun | `VBAT_OV` is working |
| Cell goes above 3.95 V while on the panel | `VBAT_OV` is not limiting. On MSV3 this was caused by panel `V_OC` above the `VIN_DC` limit |
| Cell starts **above** `VBAT_OV` and stays flat or falls | **Not a result.** A charger correctly held off by OV draws nothing, which is what a dead charger also looks like |
| Cell starts below `VBAT_OV` and stays flat or falls in direct sun | Charger is not delivering: check SP+ loaded voltage against the MPPT setpoint, then the BQ25504 wiring |

On 2026-10-01 this cell read 3.99–4.05 V, above `VBAT_OV`, and the bring-up record left
"show the charger actually charging, then stopping at ~3.89 V, using a cell below 3.85 V" open.
At 3.814 V the cell now starts 75 mV below nominal `VBAT_OV`, so the "not a result" row does not
apply: a rise or a fall in direct sun is evidence either way. 3.814 V is still inside the
3.765–3.951 V tolerance band for `VBAT_OV`, so a board whose VBIAS sits at the low end could
already be at its own cutoff.

## Load on the cell

The load changed 27 minutes into the test, so the two parts are not one continuous record.

| From | Image | Behaviour |
|---|---|---|
| Before the start, until 15:24 | `bringup-dietemp` (v262147) | Advertises continuously (100 ms for 30 s, then 500 ms), measures nothing unless asked. The cell fell under this image for "a couple of days" |
| 15:24 onward | `prod-2026-10-06` (v262148), sensor 10 | Wakes once a minute: battery, tip temperature, 5 s advertising window, sleep. Moisture sweep every 10th wake. See [the production firmware record](MSV4_PRODUCTION_FIRMWARE_2026-10-06.md) |

Neither image's current draw has been measured.

## Taking readings

- The production image puts battery mV in every advertisement.
  `python MSV4/scripts/msv4_adv_scan.py --sensor 10 --seconds 0 --csv <file>` appends one row per
  packet for as long as a computer in BLE range is listening.
- The DisplayCenter receiver stored nothing for sensor 10 until 20:03 on 2026-10-06, when it was
  reflashed with MSV4 support. From then on it ingests every packet and uploads it to the cloud
  (see the production firmware record).
- A meter on the cell.

## Readings

| Local time | VBAT (V) | Source | SP+ (V) | Sky / sun on panel | Note |
|---|---:|---|---:|---|---|
| 2026-10-06 14:57 | | | | | test start |
| 2026-10-06 15:03 | 3.814 | `vbat` over BLE | | | first reading; bring-up image still installed |
| 2026-10-06 15:24–15:32 | | | | | board in the bootloader for the firmware transfer (radio on throughout) |
| 2026-10-06 15:33 | 3.825 | advertisement | | | production image, counter 2 |
| 2026-10-06 15:34 | 3.829 | advertisement | | | |
| 2026-10-06 15:35 | 3.825 | advertisement | | | |
| 2026-10-06 15:38 | 3.832 | advertisement | | | |
| 2026-10-06 15:39 | 3.823 | advertisement | | | |

Every packet heard is also appended to `data/sensor_logs/msv4_sensor10_2026-10-06.csv` (not
tracked by Git).

The 15:33–15:39 readings sit 9–18 mV above the 15:03 one. The image, and with it the load at the
moment of each reading, changed in between, so that difference is not evidence of charging.

### First afternoon and evening, 15:33–20:05 (production image throughout)

270 packets, advertisement counter 2 → 274, so the probe ran the whole 4.5 h without a reset and
all but about three packets were heard. Hourly battery, from the CSV:

| Hour (PDT) | Packets | Min (mV) | Mean (mV) | Max (mV) |
|---|---:|---:|---:|---:|
| 15:30–16:30 | 54 | 3821 | 3826.2 | 3832 |
| 16:30–17:30 | 60 | 3825 | 3828.4 | 3834 |
| 17:30–18:30 | 60 | 3825 | 3830.4 | 3834 |
| 18:30–19:30 | 60 | 3825 | 3830.2 | 3836 |
| 19:30–20:05 | 36 | 3825 | 3828.4 | 3836 |

The mean rose 4 mV over the first two hours and came back 2 mV by 20:00. One ADC step is 1.8 mV
and the tip temperature fell from 27.0 to 22.5 °C over the same period, so this shows a cell that
neither charged nor discharged measurably in half an afternoon. It does not answer the charging
question; a full day in sun is needed. How much direct sun the panel had was not recorded.

From 20:03 the DisplayCenter receiver also ingests sensor 10 and uploads it to the cloud.

### Evening, 20:05–21:24

| Time (PDT) | Event |
|---|---|
| 21:12 | Computer-side CSV logging stopped at counter 341, 3825 mV. The cloud history is the record from here on |
| 21:20:14–21:20:59 | Probe updated over BLE to `prod-2026-10-06b` (v262150), 45 s in total with the radio connected. Same duty cycle; the packet now also carries die temperature. The advertisement counter restarted at 1 |
| 21:21–21:24 | 3820–3825 mV, tip 22.75–23.0 °C, die 23.5–24.0 °C |
| 21:40–21:55 | Bring-up console put back for a diagnostic sweep (continuous advertising, several connections, two firmware transfers). `vbat` 3816 mV at 21:42 and 21:46 |
| 21:55 | Production image `prod-2026-10-06c` (v262152) installed; counter restarted at 1 |
| 21:55–22:16 | 3829, then 3834–3844 mV. **Not charge, and not just relaxation as first written here: this boot's ADC calibration read about 13 mV high.** See "The battery reading depended on the boot" in the production firmware record |
| 22:10 | Maintainer: the probe is in air, sitting in a window for a window-charging test, with an MSV3 probe next to it |
| 22:16 | `prod-2026-10-06d` (v262153) installed: the ADC offset is now recalibrated every wake. Counter restarted at 1 |
| 22:17–22:26 | 3818–3827 mV, mean 3821. Back on the same footing as the afternoon's 3826 |

The cell was at 3823–3830 mV from 20:05 to 21:12, so it held within the same 15 mV band from 15:33
to 21:24 through daylight, dusk and two firmware transfers.

## Reading the battery trace

- **Gaps are firmware changes, not power loss.** Every gap on 2026-10-06 (15:24–15:32, 21:20,
  21:41–21:55, 22:16) is one of the transfers listed above. The board has not browned out or
  been cut off by the harvester at any point in this record.
- **Compare battery values only within one firmware boot before 22:16.** Up to then each boot
  carried its own ADC offset. From 22:16 the readings are recalibrated every wake and are
  comparable with each other, and with the 15:33–21:41 readings to within a few mV.
- **Baseline for the window test, from 22:17:** 3821 mV.

## Result

Open.
