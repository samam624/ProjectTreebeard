# Sensor 11 (Begonia): which fitting method tracks the scale, and does it survive a watering? (2026-11-06)

Script: `MSV4/scripts/msv4_begonia_model_competition.py` (run after refreshing
`data/sensor_logs/cache/sensor11_raw.csv` and `sensor11_sweep_fits.csv`, commands in its docstring).
Data: 13 weighings from `msv4_boards` board 2 `physical_state.mass` in `MSV3/fleet.json`. Ten are in
drying cycle 1 (10-28 15:48 to 11-05 14:50). Three are in cycle 2, after the 11-05 ~14:50 watering:
1347 g right after it, then 1333 and 1308. 2,265 raw sweeps are joined to their history rows.

## Scorer fix in this revision

Until now the scorer paired each weighing with every reading within 30 min, so the 1068 g
pre-watering weighing and the 1347 g post-watering one each took readings from both sides of the
11-05 watering. That alone put every candidate at 56-62 g in-sample. Now a reading pairs only
inside its own cycle. For readings, a cycle begins at the probe's first sweep after the water
(11-05 14:57, 6.5 min after it), because the sweep runs every 10th wake. The post-watering weighing
is left out of the fits, as in the mass plot.

Cross-check: fitted on cycle 1, the number in use reads +57 g at 11-05 21:59 and +41 g at 11-06
10:23. `msv4_begonia_mass_plot.py`, a separate pairing, gives ~60 and ~45 g.

## Inside cycle 1 (MEASURED, 10 weighings, 1068-1349 g)

Leave-one-out miss, grams:

| tier | candidates | LOO g |
|---|---|---|
| best | two-branch R 9.4, lumped R 12.7, raw R 12.4, RC-line mask C 12.9, R behind mask 13.6, number in use 13.7, RC-line R 14.3 | 9-14 |
| middle | CPE mask C 17.6, two-branch mask C 19.9, lumped mask C 22.3 | 18-22 |
| worse | lumped Cd 27.7, Cm 30.2, soil-coupled 32.0, direct C 49.8, derived soil C 52.5 | 28-53 |
| useless | CPE n | 122 |

The tiers separate. Within the best tier, the differences of a few grams cannot be separated with
ten weighings.

## Fitted on cycle 1, predicting cycle 2 (MEASURED, 2 settled weighings)

Predicted minus scale, grams. Lower |mean| is better. `change` is the predicted move from 21:59 to
10:23, when the scale moved -25 g.

| candidate | 21:59 | 10:23 | mean | change |
|---|---|---|---|---|
| lumped mask C | +23 | +15 | +19 | -33 |
| two-branch mask C | +33 | +20 | +26 | -38 |
| RC-line mask C | +30 | +25 | +27 | -30 |
| two-branch R | +47 | +34 | +41 | -38 |
| lumped R (firmware's) | +60 | +35 | +48 | -50 |
| number in use | +57 | +41 | +49 | -42 |
| CPE mask C | +80 | +40 | +60 | -66 |
| Cm / soil-coupled | +99 / +102 | +49 / +49 | +74 / +75 | -75 / -77 |
| derived soil C | +132 | +53 | +93 | -104 |

- Every candidate reads WETTER in cycle 2 than cycle 1 did at the same weight. Sensor 10 showed the
  same in its second cycle.
- The sweep fits' mask capacitances carry the smallest offset (+19 to +27 g) and the closest drying
  change (-30 to -38 g against -25). The resistances and the number in use sit at +41 to +55 g; the
  capacitance candidates at +60 to +93 g.
- Every offset shrank overnight, the number in use from +57 to +41 g.
- The tip cooled from 20.75 to 18.50 C over that night. The number in use carries a temperature
  correction and still read -42 g against -25, so the excess drying is not only temperature
  (inferred).

## Why this does not settle the cross-watering ranking yet

- Only two settled weighings, 12 h apart: one interval of 25 g.
- Both are 7-20 h after the watering. Cycle 1's first weighing was 27 h after the 10-27 watering, so
  cycle 1 has no weighing in the window the cycle-2 points sit in. The test extrapolates in time
  since watering, though not in grams (1308-1333 is inside cycle 1's range).
- So the offsets could be first-day settling that cycle 1 never sampled, or a lasting
  per-watering offset like sensor 10's. The data cannot tell the two apart yet. If the gaps keep
  shrinking after ~27 h (from 11-06 ~18:00), it is settling.

What decides it: weighings from 11-06 evening onward, so cycle 2 overlaps cycle 1's time since
watering. Then the next watering, weighed right before it, gives a third cycle and a second
transfer test.

## Update 2026-11-07: the first weighing past 27 h (MEASURED)

1251 g at 11-07 ~11:03, 44 h after the watering: inside cycle 1's grams range and inside its
time-since-watering range (its first two weighings were 27 h and 58 h after the 10-27 watering).
Same script, sweeps refitted (2,264), 14 weighings.

Fitted on cycle 1, predicted minus scale, grams. `change` is the predicted move from 11-05 21:59
to 11-07 11:03, when the scale moved -82 g.

| candidate | 21:59 | 11-06 10:23 | 11-07 11:03 | change |
|---|---|---|---|---|
| lumped mask C | +23 | +15 | +12 | -93 |
| two-branch mask C | +33 | +20 | +15 | -99 |
| RC-line mask C | +30 | +26 | +24 | -87 |
| two-branch R | +47 | +32 | +10 | -119 |
| RC-line R | +64 | +35 | +9 | -136 |
| lumped R (firmware's) | +60 | +36 | +13 | -129 |
| number in use | +57 | +41 | +20 | -119 |
| CPE mask C | +80 | +40 | +8 | -155 |
| Cm / soil-coupled | +99 / +102 | +49 / +49 | +4 / +3 | -177 / -181 |
| direct C / derived soil C | +129 / +132 | +53 / +53 | -4 / -5 | -215 / -219 |

- The resistances, the number in use and the capacitances kept closing after 27 h: the resistances
  and capacitances are now a quarter or less of their 21:59 gap, the number in use about a third,
  the lumped and two-branch mask C about half. That is the settling test this record set, and it
  passed.
- The capacitances have reached zero and are still falling faster than the scale (-177 to -219 g
  predicted against -82 g). Whether they stop at zero or go on to read drier than the scale is not
  known until the next weighing.
- RC-line mask C did not close: +30, +26, +24 g. Its predicted change (-87 g) is the closest of
  any candidate to the scale's -82 g. So it tracks the drying at cycle 1's slope with a fixed
  offset of about +25 g, the same offset-not-gain shape sensor 10 showed in its second cycle.
- With one line through all 13 settled weighings of both cycles, RC-line mask C has the smallest
  leave-one-out miss (14.9 g), then two-branch R 16.0, two-branch mask C 18.6, lumped mask C 19.0,
  lumped R 19.6 and the number in use 19.8. Capacitances 30-44 g, CPE n 106.
- The stored number reads +20 g on the cycle-1 line; the mass plot's separate pairing shows the
  same (~1273 against 1251 g).
