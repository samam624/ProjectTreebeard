# Water-now region outdoors: sensor 1 said WATER NOW after a week of rain (2026-11-19)

**Report (maintainer, 2026-11-19 ~22:00 PST):** sensor 1 (MSV2, outdoors in the thaumatophilum pot
per `MSV3/fleet.json`) showed WATER NOW while reading high, after rain all week.

## Measured

The card number is 500 - kHz (higher = wetter). The probe moves in steps of 7-9 counts.

| | number |
|---|---|
| wettest this week (11-18 10:00, rain) | 370 |
| reading at the report (11-19 21:00-22:00) | 308 |
| stored WATER NOW line (0 %) | 312 |
| stored 100 % | 330 |
| where the pot sits dry (10-30..11-02, 11-12/13) | 278 |

The stored line (`msv2_sensor_calibration_reset`, set 11-17 23:10 UTC, line hour 11-16 15:00 PST)
had a drop of 19.5, the mean of two taught waterings:

- 11-13 11:00, from 278, settled 316: drop 38 (a real watering from the dry level);
- 11-16 15:00, rain, from 315 against a scale of 316: drop **1**.

Second fault: in span mode a watering was a step of 0.15 x drop = 2.9 counts, under one of this
probe's steps, so the cloud counted **34 waterings** in six weeks (every step and the nightly
278 <-> 289), and days-since-watering never passed one.

Hourly step against the stored drop (current hour vs the median of the three before):

| | step / drop |
|---|---|
| sensor 1 one-step wiggles | 0.15-0.29 |
| sensor 1 rain and waterings | 0.32-2.2 (each event has an hour >= 0.5) |
| sensor 10, every real watering | 0.74-1.30 |
| sensor 11, every real watering | 0.94-1.06 |

## Changed (maintainer: "change the rule, they must work outdoors")

`supabase/functions/receiver-snapshot/region.ts`, rules 8 and 9:

- **Rule 8:** a watering teaches the line only if the pot had dried by at least `TEACH_MIN_DRY`
  (0.5) of what that watering put back. Rain on wet soil still counts as a watering (it resets
  the clock, as the maintainer said rain does) but teaches nothing. The region is now fixed at
  the first watering that completes the teaching (`>=` instead of exactly the second).
- **Rule 9:** in span mode a watering is a step of `STEP_UP_SPAN` (0.4) of the drop. Share mode
  keeps 0.15.

Tests: `region.test.ts` 18/18 (3 new; fixture `fixtures/region-sensor1-outdoors-hourly.json`).
Live check after: sensors 7, 10, 11 list the same waterings and the same region as before;
sensor 9 (out of soil) drops from 4 waterings to 1. Deployed `receiver-snapshot` 2026-11-19.

## Sensor 1's line set by hand (maintainer approved 289 / 278)

`region_drop` 19.5 -> 44.3, `region_top` 312 -> 289, `region_bottom` 308 -> 278, `region_full`
330 (unchanged), `region_line_hour` 498575 (unchanged). Undo: set them back to 19.5 / 312 / 308.
44.3 = (330 - 289) / 0.925, so the watering step on this probe is 0.4 x 44.3 = 17.7 counts
(about two of its steps).

Result (`region_live_check.mjs`): sensor 1 at 308 reads **46 %**. It now has **9 waterings**:
the five real wettings 10-10, 10-22, 11-03, 11-13, 11-15, plus the rains of 11-16, 11-18 (x2)
and 11-19.

## Timer fallback

The row the receiver pulls already carries `daysSinceLastWatering` / `lastWateredAtMs`. With
rule 9 they now follow real wettings and rain, not wiggles. The receiver treats them as
informational and does not draw them on the card.
