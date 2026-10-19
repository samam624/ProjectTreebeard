# MSV4 board 2: electrode models on raw sweeps, and a tap-water temperature swing (2026-10-15)

This is the test planned at the end of `MSV4_BOARD2_CUP_WATER_SALT_TEST_2026-10-14.md`. The cloud
stores only the firmware's fitted values, so five electrode models were compared on raw sweeps
from the bring-up console.

Setup:
- **Probe:** probe 11 (MSV4 board 2, R15 = 3.3 kohm).
- **Image:** console v262170 (`bringup-rawsweep-2026-10-15`). The plan said 262169, but the
  10-14 ADC-fix image had already taken that number.
- **Sweeps:** each is 8 points, 250 kHz to 2.67 MHz, both electrode orientations.
- **Moves:** every move the maintainer made is in `MSV3/fleet.json` (board 2, `active_test`).

Afterwards production v262171 (`prod-adcfix-rs3300-2026-10-15`, Rs 3300, Cp 8.74 pF) was put back
at 17:35. It booted in the moist soil.

## What it adds up to

- **Resistance is honest but sensitive.** In water the firmware's R is reversible with temperature
  (-1.6 to -1.75 %/C, closing to 0.1 % after a 13 C swing), so the electronics add no thermal
  error of their own. It also carries salt: one pinch took it down 57 %.
- **The mask capacitance is the robust channel, except for contact.** It moved +0.2 %/C (a tenth
  of R) and only +1.4-4 % with a pinch of salt, but it took one-way steps of 2-5 % from handling
  and, by the maintainer's reading, bubbles. This is the same failure seen in the pot on 10-09 and
  10-10, where the soil-coupled sum ratcheted up through Cm.
- **Soil is not water.** The firmware's lumped shape fits soil twice as badly as a CPE (residual 148
  vs 74). Its R in soil is a model parameter, not a physical resistance: the CPE puts the series R
  at 375 ohm where the lumped fit says 9.9 kohm. Cup tests cannot choose the model.
  (Annotated 2026-10-16: across 121 cloud sweeps the CPE's series R is NOT identified. It has a
  median of 53 ohm and sits at its 0.1 ohm bound on 49 of them. The 375 ohm came from one capture;
  see `MSV4_SENSOR11_SWEEP_RECORD_2026-10-16.md`.)
- **The probe's own limit** is a systematic front-end error of about 0.6 % of drive. The
  repeat-to-repeat noise is 20 times smaller.

## What was captured

All in `data/sensor_logs/msv4_rawsweep_2026-10-15/`:

| file | medium | sweeps | time (PDT) | tip |
|---|---|---|---|---|
| open.txt | electrodes switched out | 10 | 13:53 | |
| air.txt | blade dry, on the desk | 15 | 13:54 | 25.4 C |
| tap.txt | one cup of room-temperature tap water, 20 min after the dunk | 15 | 14:32:45-14:33:40 | 26.0 C |
| tap_presalt_monitor.txt | the same cup after the swing, just before the salt | 16 | 16:55-17:03 | 26.4 C |
| salt1.txt | the same cup plus one pinch, mixed and flat | 15 | 17:13:54-17:15:11 | 26.1 C |
| soil.txt | moist soil (container and soil not stated) | 15 | 17:31:47-17:33:21 | 22.7 C |
| monitor.txt | a logger: one sweep every 30 s, 13:56-17:31, with timestamps | ~400 | | |

Notes on the captures:
- **Cp:** the open sweeps give Cp 8.75 pF at Rs 3300 ohm, the same as the 8.74 pF compiled into
  the production images.
- **Air check before flashing:** cloud history from 17:00 on 10-14 to 13:47 on 10-15 read
  Rsoil 80-95 kohm, Cm 2.6-2.9 pF and Cd 13.71-13.95 pF. The pre-test baseline was 85 kohm /
  2.6 / 13.6 pF, so the 10-14 salt and hot bath left no lasting change in air.
- **Tap reference for the salt:** the pre-salt sweeps (16:55-17:03) are used, not tap.txt. They
  were taken 10 minutes before the salt at nearly the same temperature, so creep and the swing
  stay out of the salt comparison.

Reproduce:

    python MSV4/scripts/msv4_raw_sweep_models.py --rs 3300 --open open.txt air=air.txt tap=tap_presalt_monitor.txt salt1=salt1.txt soil=soil.txt
    python MSV4/scripts/msv4_rawsweep_swing.py monitor.txt --rs 3300 --cp 8.75 --cd0 13.71 --cache swing_fits.csv

## Noise and the floor (measured)

- **Repeat noise:** one sweep repeats to 2.6-3.0e-4 of drive in every medium. That is 17 times
  less than the 50e-4 assumed when planning, so 15 sweeps were far more than enough.
- **The floor:** in air every model leaves 55-63e-4, 70-80 times the averaged noise. The air
  residual is systematic, not noise.
- **Inferred:** in air the electrode admittance is tiny, so the floor is most likely in the
  front-end measurement model (Cp, the harmonic folding, the chopper), not in the electrode.

## Model results (measured)

Residual is the rms over the averaged sweep, in 1e-4 of drive. Mask C is the capacitance each
model gives the mask; the lumped model's mask C is Cm + Cd - Cd0, with Cd0 = 13.71 pF.

| model | air | tap | 1 pinch | soil |
|---|---|---|---|---|
| lumped, Cd free (the firmware's) | 63 | 47 | 26 | 148 |
| lumped, Cd fixed at Cd0 | 63 | 198 | 64 | 338 |
| CPE, Cd fixed | 58 | 109 | 34 | **74** (n 0.62) |
| RC line, Cd fixed | 60 | 41 | 27 | 189 |
| two-branch, Cd fixed | 55 | 40 | **20** | 106 |

| model | mask C, tap | mask C, 1 pinch | change |
|---|---|---|---|
| lumped, Cd free | 783 pF | 816 pF | +4.2 % |
| lumped, Cd fixed | 685 pF | 788 pF | +15 % |
| CPE | 480 pF | 724 pF | +51 % |
| RC line | 801 pF | 812 pF | **+1.4 %** |
| two-branch | 790 pF | 823 pF | +4.2 % |

Findings:
- **Water and soil rank the models in opposite orders.**
  - In water, the RC line holds the mask steadiest under salt and fits as well as the free
    lumped model, with Cd held at the board's air value. Two-branch fits salt best but moves its
    mask as much as the lumped model, and its R is loose (±3-5 % between sweeps). The CPE fits
    worst of the Cd-fixed models.
  - In soil, the order reverses. The CPE fits best, with a strongly dispersive exponent (n 0.62),
    and comes within 1.2-1.35 times the air floor. The RC line is the worst Cd-fixed model.
- **Inferred:** soil carries a dispersion that tap and salt water do not. A cup of water is not
  a proxy for soil when choosing the model; only soil sweeps can.
- **Fixing Cd at the air value with a plain lumped interface fails everywhere.** Something besides
  the board's own capacitance always needs the extra Cd: 117 pF in tap, 173 pF with salt,
  24 pF in soil.
- **Caveat on the salt step:** the maintainer knocked bubbles off the blade while mixing, so part
  of every model's mask-C change may be contact area, not salt. These data cannot separate the
  two.
- **The salt result disagrees with 10-14:** the lumped soil-coupled sum went up 4.2 % with one
  pinch today, where on 10-14 it went down 3 %. The base water differed (tap today, Brita then)
  and so did the bubbles.

## The temperature swing (measured)

Sequence: settled in room-temperature tap (14:12-14:54); glass into a 125 F (51.7 C) bowl at
14:54; tip 25.5 -> 38.7 C, levelling at 38.3-38.7 C by 15:04; glass out of the bowl at 15:16 to
cool in room air; back to 27.0 C at 16:48.

The cooling fits a room temperature of 25.55 C and a 45-minute time constant.

**The joint fit:**
- Form: ln(value) = a + b (T - 25) + c t + step.
- Data: the settled pre-swing window and the 90-minute cooling leg. The heating leg is left out
  because heating ran at 2 C/min and the tip visibly lagged the water there. At 33 C, R read 3 %
  lower going up than coming down.
- The step term sits at the 15:16 glass move.

| value | per C | per min | step | scatter | closure at 25.56 C |
|---|---|---|---|---|---|
| R, lumped | -1.63 % | +0.015 % | -1.4 % | 0.23 % | 576.8 -> 577.6 ohm (+0.1 %) |
| R, RC line | -1.87 % | +0.007 % | +2.9 % | 0.19 % | 457.6 -> 474.8 ohm (+3.8 %) |
| mask C, lumped | +0.21 % | -0.002 % | +4.8 % | 0.13 % | 747.4 -> 781.3 pF (+4.5 %) |
| mask C, RC line | +0.17 % | -0.000 % | +4.8 % | 0.14 % | 762.1 -> 799.4 pF (+4.9 %) |
| mask C, two-branch | +0.23 % | -0.001 % | +4.7 % | 0.14 % | 752.9 -> 788.2 pF (+4.7 %) |

What this shows:
- **R (the water):** reversible, -1.6 to -1.9 %/C, as water's conductivity should be. The
  firmware's lumped R closes the loop to 0.1 %. This answers the open instrument check from
  10-10: Rsoil follows the tip at about 2 %/C, not 4-5 %/C.
- **Mask C, reversible part:** about +0.2 %/C on the cooling leg, a tenth of R's coefficient.
  Over those 90 minutes it did not drift at all with time.
- **Mask C, one-way part:** it rose about 5 % during the 22 heated minutes (14:54-15:16) and
  stayed up. Settling was still running before the swing (Cm +0.08 %/min at 14:46-14:52) and
  during the hot hold (+0.05 %/min at 38.5 C), and stopped afterwards. Whether the heat caused
  the rise or only finished settling that was coming anyway, these data cannot tell.
  - On 10-14, the 45 C bath on salted Brita water gave +15 % one-way. Today, +13 C on tap
    water gave about +5 %.
- **The glass move at 15:16 is a step of its own,** most clearly in Cd: 120.5 -> 117.9 pF with no
  temperature change, then about 113 pF for the rest of the cooling. Lumped Cd went 117.2 pF
  before the swing to 113.1 pF after it.
- **Correction:** an interim fit during the session, while the cooling leg was only 25 minutes
  long, gave mask C +0.36-0.41 %/C with 0.03 %/min of creep, and blamed the creep for the
  closure gap. The full cooling leg does not support that. The table above supersedes it.

**The maintainer's reading (2026-10-15, after the session):** "All the one way stuff is bubbles on
the probe." That covers the ~5 % rise while heated, the step at the glass move and part of the
salt step. Bubbles were seen and knocked off at the 17:08 mixing, and mask C jumped up at that
moment. The direction fits bubbles leaving the blade, since leaving adds wetted area. Not
separately measured.

## Figures

From `MSV4/scripts/msv4_rawsweep_plot.py data/sensor_logs/msv4_rawsweep_2026-10-15 --out-dir data/sensor_logs`:
- `msv4_rawsweep_models_2026-10-15.png`: each model's residual by medium, and its mask C change
  with one pinch.
- `msv4_rawsweep_timeline_2026-10-15.png`: tip, water R, mask C and Cd for every logged sweep,
  from the dunk to the salt.
- `msv4_rawsweep_loop_2026-10-15.png`: lumped R and mask C against the tip through the swing.
  Its slopes come from the cooling leg alone (-1.75 %/C and +0.23 %/C), so they differ slightly
  from the joint fit above, which also uses the pre-swing window and a time term.

## Settling after the dunk (measured, tap water at constant 26 C)

Cm rose 3.7 %/min at 3.5 min after the dunk, 1.5 %/min at 7.5 min, 0.53 %/min at 14 min and
0.23 %/min at 20 min. It was still creeping 0.08 %/min after 40 min. Rsoil and Cd were flat within
about 10 minutes. Any wet capture should wait at least 20 minutes after the blade goes in.

## Raw sweeps to the cloud (built 2026-10-15 evening)

The maintainer asked for probe 11 to send its raw sweeps to Supabase, so models can be fitted to
a soil record. Probe 11 sits in its own pot, which is not weighed (maintainer, ~20:00). The
transport was the maintainer's choice: the probe's scan response, with the receiver scanning
actively.

| piece | where | what |
|---|---|---|
| probe | `MSV4_Moisture_BLE_Sleep.ino`, comment above `SWEEP_RSP_LEN` | on a wake that measured, the scan response is Service Data under UUID 0x4D34, carrying layout 1, the sensor, the counter's low byte, and 8 points of 12+12 bits in 1/2048 of drive; other long-window wakes keep the name |
| PC check | `msv4_adv_scan.py --sweep-csv` | decodes the sweep; self-test covers the round trip and saturation |
| receiver | `Treebeard_ESP32_Display_Runtime.ino`, `handleMsv4SweepScanResponse()` | active scan; keeps the latest sweep per sensor, only against the wake whose counter low byte matches, and sends it as `raw.msv4.sweep {wake, ageMs, hex}` in both latest-value uploads; the history journal is untouched |
| cloud | migration 042 `msv4_raw_sweeps`, `receiver-snapshot/raw-sweeps.ts` | validated rows, stored through `msv4_store_raw_sweeps()` with repeats ignored; best effort, after the history acknowledgement |
| analysis | `msv4_cloud_sweeps.py` | fetches, decodes and fits every model to every sweep |

Notes:
- **Service Data, not manufacturer data.** Bleak on Windows merges the advertisement and the scan
  response into one dict keyed by company ID. A second 0xFFFF record would have hidden the
  production packet from the OTA tool.
- **Active scanning changes delivery for every scannable probe** (NimBLE in arduino-esp32
  3.3.11). The report waits for the scan response, or for the end of the 30 s scan session if
  none arrives, and every scannable probe now answers one scan request per advertisement it is
  asked about.
- **It cost sensor 1 packets at first (measured).** Sensor 1 is the MSV2 outdoors, at about
  -91 dBm. It fell from 10-13 packets per 15 minutes to 1-6 after the flash at 20:33. The cause
  is in the library's code: `BLEScan::stop()` cancels discovery without the completion event that
  hands over held reports, `beginCloudRadioSlot()` stops the scan for every upload, and the scan
  task then clears the results. Receiver build `msv4-raw-sweep-v3` (flashed 20:57) hands every
  stored report to the callbacks before clearing (`BleScanFlush.h`). The other four sensors were
  unaffected (0.8-0.97 per minute before and after).
- **v3 fixed it (measured over 20:59-21:27, 28 minutes):**

  | sensor | per minute on v3 | per minute before the switch | RSSI |
  |---|---|---|---|
  | 1 | 0.75 | 0.77 | -89 dBm |
  | 4 | 0.82 | 0.83 | |
  | 7 | 0.82 | 0.85 | |
  | 10 | 0.96 | 0.98 | |
  | 11 | 0.96 | 1.00 | |

  Sweeps were stored at wakes 21, 31, 51 and 61. Wake 41 (20:56) fell inside the v3 flash.
- **Proof so far:** the first two sweeps were stored at wakes 21 (20:36:37) and 31 (20:46:37).
  Refitting the cloud copy of wake 21 gives R 12,996 ohm against the 13,001 ohm the probe's own
  fit stored for that wake.

## Open

- The air floor (55-63e-4) is larger than every water residual. Until the front-end model is
  checked against it, no electrode model can be judged at the noise.
- Soil is the case that matters (the working range is dry to slightly moist soil). This is one
  soil at one water content. A CPE needs sweeps across a dry-down before it could replace the
  firmware's lumped fit.
- Receiver: the air-baseline entries for sensor 11's boots of 2026-10-14 23:28:49 (262169, Cd
  13.74 pF measured in air) and 2026-10-15 17:35:51 (262171, value carried) are in both tables,
  and the decode gate passes. The DisplayCenter has not been reflashed with them, so its
  soil-coupled card is blank for sensor 11's recent boots.
