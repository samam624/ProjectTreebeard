# MSV4 outdoor solar charge test — started 2026-10-06

Dated record of a concluded test. Current physical state lives in `MSV3/fleet.json`
(`msv4_boards`), not here.

| | |
|---|---|
| **Started** | **2026-10-06 14:57 PDT** |
| Status | **Concluded.** Maintainer, 2026-10-08 about 13:35 PDT: "we can now say that the solar is fully verified on MSV4" |
| Ended | 2026-10-08 about 13:35 PDT |
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
| 22:55 | `prod-2026-10-06e` (v262154) installed: lighter advertising, shorter measurement wake. **The load is lower from here** (modelled 1.2–1.3 mAh/day against 2.1–2.4 before). Counter restarted at 1 |
| 22:55–23:06 | 3814–3820 mV |

The cell was at 3823–3830 mV from 20:05 to 21:12, so it held within the same 15 mV band from 15:33
to 21:24 through daylight, dusk and two firmware transfers.

### 2026-10-07, midday

Heard on the maintainer's computer. Where the board was sitting was not re-stated on this day.

| Time (PDT) | Event |
|---|---|
| 11:43–11:44 | 3825, 3829 mV at counters 769 and 770, so `prod-2026-10-06e` had run from its 22:55 boot without a reset |
| 11:55:56–11:56:32 | Probe updated over BLE to `prod-2026-10-07` (v262155), 36 s of transfer. **The nRF52840's DC/DC regulator is on from here** (from the end of the first wake after each boot), so **the load is lower again**: modelled about 0.4 mAh/day less, no current measured. Counter restarted at 1. See "Result on board 1" in the production firmware record |
| 11:57–12:06 | 3827–3832 mV, mean 3829, at counters 2–11; tip 24.0–24.25 °C |
| 12:07–12:11 | 3823–3829 mV at counters 12–16; tip 24.25–25.0 °C |
| about 12:35 | **Maintainer: "solar panel in direct sunlight through window for MSV4 is reading 2.11V."** At about 13:30: "2.11 is panel connected to board". The instrument was not stated, nor the panel's open-circuit voltage in the same light |
| 12:36–12:39 | 3823–3827 mV, mean 3825, at counters 41–44; tip 26.5–26.75 °C |
| 12:47–12:58 | 3821–3829 mV, mean 3824, at counters 52–63 (11 heard); tip 27.25–27.75 °C |
| 13:06:40–13:07:35 | Probe updated over BLE to `prod-2026-10-07b` (v262157), 52 s of transfer: warm-start fit, so 11 measurement wakes in 12 are about 2 s shorter. **The load is lower again** (modelled about 0.24 mAh/day less, no current measured). Counter restarted at 1 |
| 13:07:37 | **3850 mV** at counter 1, the first reading of the new boot. Not charge: the next reading, 57 s later, was 3825 |
| 13:08–13:19 | 3823–3829 mV, mean 3826, at counters 2–13; tip 27.0–27.75 °C, die 29.0–29.5 °C |

### What the stored history shows, 2026-10-06 23:10 to 2026-10-07 13:34

860 readings from the cloud history, one a minute, run through
`MSV4/scripts/msv4_battery_trend.py` (slopes are least squares with the outliers below left out;
single readings scatter by 2.2 mV). The maintainer saw the same rise on the History chart at
about 13:30 ("its going up since this morning").

| Period (PDT) | Firmware | Slope | Fitted start → end | Tip thermistor |
|---|---|---:|---|---|
| 23:10–06:29, dark | 262154 | −0.17 ± 0.05 mV/h | 3817.4 → 3816.1 mV | 23.1 → 21.75 °C |
| 06:30–08:59 | 262154 | −0.11 ± 0.28 mV/h | 3815.9 → 3815.6 mV | 21.5–21.7 °C |
| **09:00–11:56** | 262154 | **+3.60 ± 0.20 mV/h** | **3815.7 → 3826.3 mV** | 22.4–23.0 °C |
| 11:57–13:07, direct sun reported at 12:35 | 262155 | −4.4 ± 0.9 mV/h | 3828.4 → 3823.4 mV | 24.0 → 27.75 °C |
| 13:08–13:34, direct sun | 262157 | +8.2 ± 2.9 mV/h | 3825.5 → 3829.0 mV | 27.0–28.25 °C |

- **The cell gained 10.6 mV between 09:00 and 11:56.** Same firmware throughout, the tip
  thermistor's hourly means within 0.6 °C, 176 readings. That is charge going in, and it is the first row of
  the outcome table: the charger is harvesting and the boost path works. By the thermistor it was
  before the sun reached the board: its hourly mean is 23.0 °C for 11:00 and 26.4 °C for 12:00.
- **In the dark the cell fell 1.3 mV in 7.3 hours.**
- **Since noon it has stayed between about 3823 and 3829 mV.** Hourly medians 3825, 3825, 3827.
  The two blocks either side of the 13:07 update have opposite slopes and both began with a
  firmware transfer; the board also warmed 4–5 °C, which the ADC's specified gain drift
  (0.02 %/°C) alone turns into as much as 4 mV either way. No net rise can be claimed from them.
- **Direct sun has not produced the rise it should.** Even 5 mA into a 60 mAh cell is 8 % of
  its capacity an hour, tens of mV. The average current into the cell since noon is well under
  1 mA.
- Yesterday's daylight readings (15:33–20:05) sat at 3826–3830 mV, the same level.

**The 2.11 V was read with the panel connected.** If the charger were harvesting it would hold
the panel at 77.9 % of open circuit, which would put open circuit at 2.71 V. The BOM's estimate
for this 4-cell panel is about 2.4 V open circuit in sun (the listing gives none), and 2.11 V is
what a 4-cell panel reads unloaded when it is warm and behind glass. So the reading looks like a
panel that was **not being loaded** when it was taken. That is an inference: the panel's
open-circuit voltage in the same light has not been measured.

**The reading that fits all of it, inferred and not confirmed:** the charger works, and it stops
at an indicated 3.83 V, which is this board's overvoltage limit as the probe's own ADC sees it.
The cell rose to that level in weak light, stopped there in direct sun with the panel left
unloaded, and sat at the same level in yesterday's daylight. The design value is 3.889 V with a
3.765–3.951 V tolerance band, and the probe's reading itself carries the SAADC's ±3 % gain
tolerance (±115 mV), so an indicated 3.83 V does not say the limit is low. Two readings would
settle it: a meter on the cell while it sits at this level, and the panel's open-circuit voltage
with it unplugged in the same sun.

**Readings that stand apart.** Eleven of the 860 are more than 10 mV from their neighbours, each
for a single wake:

| When | Reading |
|---|---|
| 23:53, 03:54, 04:25, 05:41 (dark) | +14 to +18 mV |
| 08:41, 08:49 | −12, −19 mV |
| 10:47 | +18 mV |
| 12:32, 12:44, 13:02 (direct sun, converter on) | +16 to +19 mV |
| 13:07, first wake after a boot | +25 mV (3850) |

Four of them fall in the 35 minutes of direct sun, against four in the seven dark hours. Too
few to say whether that is the sun, the converter, or chance; the ones in the dark cannot be
charge pulses.

### Night of 2026-10-07: drawing the cell down for a day of charging

The maintainer asked for a firmware that discharges the cell overnight, so that the next day's
charging starts well below the level where the cell has been sitting. What that image does, and
how it stops itself at 3700 mV, is in the production firmware record under "Discharge burn test
build".

| Time (PDT) | Event |
|---|---|
| 13:34–23:09 | Not listened to on the computer; the stored history has it. 3834 mV at 23:09:36, counter 603: `prod-2026-10-07b` had run from 13:07 without a reset |
| 23:17:42–23:18:28 | Burn image `burn-2026-10-07` (v262158) installed over BLE, 45 s of transfer. **The load is about 6–7 mA (modelled) from here until the burn ends.** Counter restarted at 1 |
| 23:18–23:26 | 3825, 3829, 3830, 3823, 3825, 3821, 3823, 3821, 3818 mV at counters 1–9, every packet marked as burning. Fitted −1.1 mV/min |
| 23:56 | 3795 mV at counter 39; all 39 packets heard marked as burning |
| 2026-10-08 00:27 | 3772 mV at counter 70; all 68 packets heard marked as burning. About 0.74 mV/min over the last half hour |
| about 00:35 | The maintainer restarted the computer that was logging packets, so the computer-side log ends here. The burn does not depend on it; the receiver and the cloud history carry on, without the burn flag |

Every packet from 23:18 is also in `data/sensor_logs/msv4_sensor10_burn_2026-10-07.csv` (not
tracked by Git) with its burn flag, for as long as the maintainer's computer stays awake.

### 2026-10-08: the burn's end, and the first charge from well below the limit

Stated by the maintainer at about 13:20 PDT: "after using the LED burner last night, this morning
right when the sun fully hit the panel the battery voltage shot right back up to full. We were
capped on battery yesterday and thats why it was only going up a little bit".

833 readings from the cloud history, 23:19 to 13:19, one a minute, through
`python MSV4/scripts/msv4_battery_trend.py <csv> --since "2026-10-07 23:19" --edges 01:47 07:10 09:26 09:35 11:48 12:16`.
The board did not reset at any point (advertisement counter 850 at 13:27, 850 minutes after the
23:18 boot).

| Period (PDT) | What | Slope | Fitted start → end | Tip thermistor |
|---|---|---:|---|---|
| 23:19–01:46 | burn running | **−47.3 ± 0.5 mV/h** | 3825.4 → 3709.6 mV | 22.0–24.25 °C |
| 01:47–07:09 | burn over, dark | +1.32 ± 0.10 mV/h | 3703.1 → 3710.2 mV | 21.0–23.25 °C |
| 07:10–09:25 | daylight, nothing going in | +1.19 ± 0.27 mV/h | 3711.4 → 3714.0 mV | 21.0–22.5 °C |
| 09:26–09:30 | light reaches the panel | +21 mV in 4 minutes | 3714 → 3735 mV | 22.5–22.75 °C |
| 09:35–11:47 | weak light | **+10.4 ± 0.4 mV/h** | 3727.8 → 3750.7 mV | 22.75–24.0 °C |
| 11:48–12:15 | sun coming fully onto the panel | **+171 ± 9 mV/h** | 3744.9 → 3822.0 mV | 24.0–26.5 °C |
| 12:16–13:19 | direct sun, at the limit | −6.0 ± 1.4 mV/h | 3826.5 → 3820.2 mV | 26.75–28.5 °C |

**How the burn ended.** The first two readings in a row at or below 3700 mV are 3700 at 01:46:29
and 3698 at 01:47:30, which is the image's stop rule (a single 3698 at 01:44:30 only pauses it).
From there the readings stay between 3693 and 3704 mV for 25 minutes and then drift up 1.3 mV/h,
so the fall stopped at 01:47, 2 h 29 min after the boot. At the modelled 6–7 mA that is
15–17 mAh taken out, about a quarter of the 60 mAh cell; the current was not measured. The
lowest reading of the night is 3693 mV, 0.32 V above the level where the BQ25504 would have
switched the load off. The cloud history does not carry the burn flag. **Observed at 13:27–13:28:**
`msv4_adv_scan.py` heard counters 850 and 851 with the burn flag clear and the DC/DC converter on.

**The rise in the sun, minute by minute.** 3748 mV flat until 11:47; 3755 at 11:48; 3771 at
11:56, then held at 3771–3772 for six minutes; 3778 at 12:03, 3788, 3792, 3800, **3820 at
12:07**; 3809 at 12:08; then 3811–3825 and level from 12:14. From 12:16 to 13:19 every reading
is between 3816 and 3841 mV with a median of 3822–3827.

What this shows:

- **The limit is real and it repeats.** On 2026-10-07 the cell came up to it from 10 mV below
  and stopped; today it came up from 110 mV below at fifty times the rate and stopped at the
  same reading: 3820–3827 mV today against 3823–3829 mV at the same hour and the same 27–28 °C
  yesterday. That is rows one and two of the outcome table, and it turns yesterday's inference
  (the charger stops at an indicated 3.83 V, this board's overvoltage limit as its own ADC reads
  it) into something measured on two days. 3.82–3.83 V is inside the 3.765–3.951 V the design
  allows.
- **Yesterday's small rise was charge, not temperature and not the reading.** The cell began
  that day at 3816 mV, 10 mV under the level it stops at. It gained 10.6 mV with the tip
  thermistor steady, went on to 3835–3839 mV by 16:00–18:00, and was still at 3834–3837 mV at
  23:09, more than four hours after sunset, with the tip back down from 28 to 23.75 °C. A
  temperature effect would have gone back with the temperature.
- **Charge rate, inferred from the burn.** The rise in direct sun is 3.6 times as fast as the
  burn's fall. If this cell moves the same number of mV per mAh in both directions, that is
  22–25 mA into the cell, against the 25–30 mA modelled, and about 1.4 mA in the weak light
  before it. It rests on the burn's modelled current, and it overstates the sun figure: 48 mV in
  the five minutes to 12:07 is more than 25 mA can put into a 60 mAh cell in that time (2 mAh),
  so part of that step is the cell's terminal voltage lifting while current flows into it.
- **Not shown: that everything the burn took out is back.** 27 minutes at no more than 30 mA is
  at most 13.5 mAh, and the two hours of weak light before it added about 3 mAh, against
  15–17 mAh removed. At 12:15 the cell had reached the limit's voltage with at most about what
  it lost, possibly a good deal less; the charger goes on topping it up for as long as the sun
  stays on the panel. The comparison that settles it is this evening's level in the dark
  against the 3834–3837 mV it held last evening.
- **At the limit the reading falls as the board warms.** −6.0 mV/h today while the tip went
  from 26.75 to 28.5 °C; −4.4 mV/h over the same hour yesterday (24 to 27.75 °C), after which it
  came up to 3835–3839 mV as the tip cooled to 24–25 °C. Either the limit or the ADC reading
  moves a few mV per °C, or the cell goes on filling inside the charger's restart band. This
  record cannot separate them.
- **Readings that stand apart come about ten times as often at the limit in sun.** Six in the 63
  minutes from 12:16 (+11 to +20 mV, 3834–3841 mV) against five in the 7.6 dark hours after the
  burn (+13 to +20 mV). Yesterday: four in 35 minutes of sun. The BQ25504 datasheet says that
  with more input than the cell can take, VBAT ripples between the limit and 18/35/89 mV below
  it, and a once-a-minute reading would catch that as readings like these. They are the same
  size as the ones in the dark, so on their own they do not prove it.

**The production image went back on at 13:48.** The maintainer, about 13:35: "take it off the
burn and we can now say that the solar is fully verified on MSV4".

| Time (PDT) | Event |
|---|---|
| 13:34:32–13:48:31 | The receiver stored nothing (counters 858–870 missing). The board kept counting: 857 at 13:34:32, 871 at 13:48:31 |
| 13:48:31 | Counter 871, the connectable wake the updater had been waiting for; it read `MSV4-BUILD:burn-2026-10-07` as the running build |
| about 13:48:35–13:49:18 | `prod-2026-10-07c` (v262159) installed over BLE, 43 s of transfer, first session. Byte-identical to 262157 apart from its label, with the burn compiled out. Counter restarted at 1 |
| 13:50 | DIS read back: `MSV4-BUILD:prod-2026-10-07c` / `262159` / `Cp 8.74 pF` |
| 13:51:20–13:56:21 | Counters 3, 5, 6, 7, 8 heard: 3823, 3821, 3829, 3823, 3823 mV, tip 29.0 °C, converter on, burn flag clear |

**Can the cell go past the limit? (asked by the maintainer the same afternoon.)** After first
reaching the limit on 2026-10-07 the reading went on from 3825–3827 mV (12:00–14:30, tip
27–28 °C) to 3839 mV by 17:30 (tip 24.5 °C), then stayed at 3836–3839 mV until the burn began
at 23:18. That is 12 mV in three hours, inside even the smallest restart band the datasheet
gives (18 mV), and 50 mV under the 3.889 V design value by this board's own reading. The path
that took MSV3 cells to 4.44–4.49 V does not exist here: there the 6 V panel sat above the cell
and fed it through the boost stage's body diode whatever `VBAT_OV` said
(`MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`). This panel read 2.11 V
connected, against a 3.8 V cell, so the only way charge reaches the cell is the boost converter
that `VBAT_OV` disables. Not separated by that one afternoon: whether the rise stopped at the
top of the band or because the light went, since it levelled off towards dusk while the board
cooled.

## Reading the battery trace

- **Gaps are firmware changes, not power loss.** Every gap on 2026-10-06 (15:24–15:32, 21:20,
  21:41–21:55, 22:16) is one of the transfers listed above. The board has not browned out or
  been cut off by the harvester at any point in this record.
- **Compare battery values only within one firmware boot before 22:16.** Up to then each boot
  carried its own ADC offset. From 22:16 the readings are recalibrated every wake and are
  comparable with each other, and with the 15:33–21:41 readings to within a few mV.
- **Baseline for the window test, from 22:17:** 3821 mV. (From the stored history: 3817 mV at
  23:10, after the evening's transfers, and 3816 mV at 06:30.)
- **Do not judge a trend from an hour of readings.** On 2026-10-07 the 11:57–13:19 readings
  were first written up here as "the cell has not risen"; that was true of those 80 minutes
  and missed the 10.6 mV gained before them. Fit the stored history instead.

## Result

**Both parts of the question are answered, as far as the probe's own readings can answer them.**

- **The charger puts charge into the cell** (2026-10-07: +10.6 mV between 09:00 and 11:56 in
  weak window light; 2026-10-08: +108 mV between 09:26 and 12:15 after the overnight burn, 77 mV
  of it in the 27 minutes the sun took to come fully onto the panel).
- **It stops at its overvoltage limit**, which this board's ADC reads as 3.82–3.83 V in sun at
  27–28 °C (3.835–3.84 V the evening before at 24 °C). Reached from 10 mV below on one day and
  110 mV below on the next, same level both times.

Not measured: that level on a meter (the ADC's gain tolerance is ±3 %), the current into the
cell, and the panel's open-circuit voltage.

**The evening after (added 22:25 on 2026-10-08).** Hourly medians: 3821 mV at 14:00 (tip
29.5 °C, at the limit in sun), 3828 at 15:00, then 3830 mV from 16:00 to 18:00 and 3827 mV at
19:00 and 20:00 (tip 24.25–25.75 °C), against 3835–3839 mV over the same hours the evening
before. So the cell sat 7–9 mV lower than the night before the burn. It is not a like-for-like
evening: the maintainer took the board out of the window at about 15:35 for epoxy work, so it
missed the late-afternoon hours in which the reading crept up 12 mV the day before, and it took
two firmware transfers. On the burn's slope 8 mV is about 1 mAh of the 15–17 mAh taken
(inferred). Whether the creep past the first cutoff levels off on its own was not answered
today for the same reason.
