# MSV4 sensor 10: why the moisture candidates miss the scale, and a temperature fit from the weighings (2026-10-17)

The setup is the moisture competition in `MSV4_EXPERIMENTAL_MOISTURE_2026-10-10.md`: sensor 10
(MSV4 board 1) is in the shared Alocasia pot, and the pot is weighed. On 2026-10-16 the
regenerated moisture-vs-mass plot made every candidate look high at the dry end and low at the wet
end. The maintainer asked whether that was systematic or a thermal-calibration problem. It is
mostly systematic. Temperature comes second and is fixable.

Data:
- Weighings: `pots.shared-alocasia.mass.log` in `MSV3/fleet.json`, the latest 4705/4702/4700 g
  (median 4702) at ~22:46 PDT on 10-16.
- One watering on record, at 19:14 on 10-10.
- 16 weighings scored since that watering, and 2 before it.

Reproduce:

    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw \
        --since-hours 240 --csv data/sensor_logs/msv4_sensor10_all_2026-10-17.csv
    cd MSV4/scripts
    python msv4_moisture_slope_diagnosis.py ../../data/sensor_logs/msv4_sensor10_all_2026-10-17.csv
    python msv4_thermal_refit_plot.py ../../data/sensor_logs/msv4_sensor10_all_2026-10-17.csv

The outputs as run are in `data/sensor_logs/msv4_moisture_slope_diagnosis_2026-10-17.txt` and
`data/sensor_logs/msv4_thermal_refit_2026-10-17.txt`. The figure is
`data/sensor_logs/msv4_thermal_refit.png`. Both scripts have a `--self-test`.

## 1. A one-way shift across the first watering (measured; the largest effect)

The driest weighing before the watering (10-10 19:14) and the driest since (10-16 22:46) hold
nearly the same water and sit at nearly the same tip temperature:
- water: 0 g and 28 g
- tip: 25.38 and 25.75 C

Every candidate nonetheless reads much wetter now. The table gives how much wetter, in grams on
the slope fitted after the watering:

| candidate | shift |
|---|---|
| soil resistance, no temperature correction | +246 g |
| contact capacitance Cm | +238 g |
| moisture number in use | +228 g |
| soil-coupled capacitance (Cm + Cd - air) | +212 g |
| soil resistance behind the mask (derived) | +171 g |
| direct capacitance above air | +76 g |
| soil capacitance (derived) | +48 g |

- **Not temperature:** the tip differs by 0.4 C, so temperature cannot account for the shift.
- **Contact:** the readings most tied to contact moved most. This is the sign of the one-way
  contact steps seen in the cup tests and in this pot on 10-09/10-10.
- **The tilted line:** the competition fits one line through every weighing, and the two
  pre-watering points tilt it. That tilt is the "high when dry, low when wet" pattern. With the
  line fitted through the post-watering weighings only, the moisture number in use reads 18 g
  low at the dry end rather than high.

Leave-one-out miss against the scale, in rms grams:

| candidate | every weighing, line | + temperature | since the watering, line | + temperature |
|---|---|---|---|---|
| moisture number in use | 69.3 | 73.3 | 23.5 | 25.0 |
| soil resistance, no temp. correction | 71.3 | 73.2 | 27.0 | 24.7 |
| direct capacitance above air | 29.4 | 28.2 | 20.5 | 13.3 |
| contact capacitance Cm | 73.2 | 74.9 | 31.6 | 29.1 |
| soil-coupled capacitance | 66.6 | 68.0 | 29.4 | 26.3 |
| soil capacitance (derived) | 22.8 | 20.0 | 19.9 | **12.1** |
| soil resistance behind the mask (derived) | 54.4 | 55.5 | 25.1 | 22.4 |

The repeated weighings on the scale differ by up to 8 g.

**Not known: settling or ratchet.** The pre-watering weighings fell 40-45 h after planting
(10-08 21:47). The data cannot yet say which of these the shift is:
- **Insertion settling, once.** The soil was still closing on the blade. If so, the 21 h
  `INSERTION_SETTLE_HOURS` in `msv4_moisture_competition.py` is too short.
- **A ratchet at every watering.** If so, a fixed calibration drifts and has to be re-anchored
  each cycle.

The next watering decides it. With a weighing just before it, compare the reading at that dry
weight with the 10-16 22:46 reading.

## 2. Temperature: the dry-day coefficient is too weak for the capacitances (measured)

`msv4_moisture_tempcorr.py` fits each coefficient on one settled dry day, with a straight line in
time standing for the drying. Day/night drying follows the sun just as the temperature does, so
that fit cannot separate the two. This was already seen on 10-12 to 10-14: three dry days gave
three different coefficients.

The refit uses the scale directly. Between consecutive weighings since the watering:
change in candidate = s x grams + k x change in tip, with no intercept. That gives 16 intervals.

| candidate | k, g/C, from the weighings | k, g/C, dry-day fit | rms miss per interval with k | without k |
|---|---|---|---|---|
| moisture number in use (fixed 2 %/C built in) | -1.1 +/- 1.0 (residual) | - | 12.0 | 12.1 |
| soil resistance, no temp. correction | +8.3 +/- 1.0 | +9.6 | 11.9 | 37.7 |
| direct capacitance above air | +7.6 +/- 0.8 | +3.4 | 9.8 | 33.1 |
| contact capacitance Cm | +9.7 +/- 1.2 | +3.0 | 14.5 | 46.6 |
| soil-coupled capacitance | +9.4 +/- 1.1 | +3.1 | 13.3 | 44.0 |
| soil capacitance (derived) | +7.4 +/- 0.8 | +3.6 | 9.7 | 31.9 |
| soil resistance behind the mask (derived) | +8.0 +/- 0.9 | +9.0 | 10.9 | 35.4 |

- **Capacitances:** every one needs +7.4 to +9.7 g/C, and the dry-day fit gave about 2.5 times
  less.
- **Resistances:** the dry-day fit was about right.
- **Moisture number in use:** the firmware's fixed 2 %/C is right within error.

**The refit, capacitances only.** In `msv4_thermal_refit_plot.py`, both corrections are converted
to grams by a line through the weighings since the watering, so the two differ only in k:

| candidate | k, g/C, old -> new | between weighings, rms g | at a weighing left out, rms g |
|---|---|---|---|
| soil capacitance (derived) | +3.4 -> +7.0 | 17.0 -> **9.7** | 13.9 -> 11.5 |
| direct capacitance above air | +3.2 -> +7.1 | 18.6 -> **9.8** | 15.1 -> 12.6 |
| soil-coupled capacitance | +2.6 -> +7.7 | 28.9 -> 13.3 | 26.5 -> 24.7 |
| contact capacitance Cm | +2.4 -> +7.9 | 31.5 -> 14.5 | 29.0 -> 27.3 |

Intervals with the largest temperature changes, for the derived soil capacitance (grams lost):

| interval | tip, C | scale | no k | dry-day k | weighing k |
|---|---|---|---|---|---|
| 10-12 10:30 -> 15:39 | 21.25 -> 26.50 | -35 | +17 | -7 | -26 |
| 10-12 21:30 -> 10-13 08:22 | 23.75 -> 21.00 | -11 | -37 | -22 | -9 |
| 10-14 09:48 -> 17:33 | 21.00 -> 26.75 | -25 | +9 | -16 | -35 |
| 10-14 23:04 -> 10-15 09:09 | 25.50 -> 22.00 | -15 | -46 | -26 | -10 |
| 10-16 10:30 -> 14:53 | 22.75 -> 27.75 | -25 | +10 | -12 | -29 |

- **Day/night:** with the weighing k, the derived soil capacitance and the direct capacitance
  follow each day/night change to about 10 g.
- **Level at a weighing:** this improves by only 1.5-2.5 g. That error belongs to sections 1
  and 3.
- **Overshoot on hot afternoons:** soil-coupled capacitance and Cm overshoot. On 10-16 the scale
  lost 25 g and they read -41 and -42 g. One coefficient fits them worse than the derived and
  direct capacitances.

Caveats:
- **In-sample:** k was fitted on the same 16 intervals it is scored on. The between-weighings
  column is in-sample, and the next weighings are the test.
- **Unseen water:** a k fitted to the scale also takes up any daytime water loss the probe cannot
  see (the saucer, the leaves, the far side of the pot). A day/night swing with the soil and
  saucer sealed under plastic, so the scale stays flat, would separate that from the true
  temperature coefficient.

## 3. Curvature (measured, not yet confirmed)

After the watering, with temperature fitted:
- **Moisture number in use and soil-coupled capacitance:** about 3.4-3.8 times more change per
  gram at 30 g than at 430 g. The residuals arch: -38 and -34 g at the two wettest weighings,
  +20 to +30 g in the middle, -14 to -18 g at the dry end.
- **Derived soil capacitance:** 1.35 times. **Direct capacitance:** 1.5 times.
- **Confounded:** the two wettest weighings fell within 15 h of the watering, while the porous
  saucer still held water that the scale counts and the probe does not. One drying cycle cannot
  separate curvature from saucer water. Several weighings through the first day after the next
  watering would.

## Where this leaves the candidates

- **Derived soil capacitance:** it shifted least across the watering (+48 g) and is the nearest
  to linear (1.35 times). Since the watering, with a weighing-fitted k, it misses a left-out
  weighing by 11.5-12.1 g, against the scale's 8 g repeatability.
- **Moisture number in use:** its temperature correction is right. It carries the second-largest
  shift and strong curvature.
