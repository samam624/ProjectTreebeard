# MSV4 probe 10: is the 2 %/C temperature correction right, and is the daily wobble temperature? (2026-10-27)

The maintainer asked on 10-27 at about 23:00: "can we tell that our temperature calibration
coefficient is not correct? Would a better coefficient work better to eliminate the wobble?"

Probe 10 is MSV4 board 1 in the Alocasia pot. It is weighed on a kitchen scale, which is the only
direct measure of water here. The receiver turns the probe's soil resistance into its moisture
number at 25 C using a fixed 2 %/C on the blade-tip thermistor (`MSV4_EC_TEMPCO_PER_C`).

## Short answer

- **The 2 %/C is not shown to be wrong.**
  - Measured against the scale over 30 weighing intervals, the coefficient is 2.02 +/- 0.16 %/C in
    cycle 1 and 1.59 +/- 0.38 %/C in cycle 2. Both cycles together give 1.93 +/- 0.18 %/C.
  - Over those intervals the number misses the scale least at 2.0 %/C in cycle 1. In cycle 2, 1.5
    and 2.0 %/C are level, both at the scale's own repeatability.
- **Most of the daily wobble is the pot drying, not temperature.** The scale alone shows that 53-54 %
  of each day's water leaves between 11:00 and 15:00. The tip warms fastest in those same hours.
- **A lower coefficient would look smoother but be less accurate.** At 1 %/C the daily wobble
  roughly halves (16.9 g to 8.5 g in cycle 1), but the miss against the scale doubles (5.3 g to
  11.6 g). The smoothness comes from cancelling real noon drying.
- **The plot's capacitance coefficients are too weak, which is a separate problem.** The scale gives
  5.9-6.0 g of water per C for every capacitance. The plot corrects only 1.7-3.2 g/C, because those
  coefficients were fitted on the 10-09 settling day.

Reproduce:

    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw \
        --since-hours 490 --csv data/sensor_logs/msv4_sensor10_all_2026-10-27.csv
    python MSV4/scripts/msv4_tempco_check.py data/sensor_logs/msv4_sensor10_all_2026-10-27.csv
    python MSV4/scripts/msv4_tempco_check.py --self-test

The outputs are in `data/sensor_logs/msv4_tempco_check_2026-10-27.txt` and
`msv4_tempco_check_selftest_2026-10-27.txt`.

**Data used:**
- 27,234 readings from 10-08 21:57 to 10-27 22:54.
- 121 weighings, up to the 4590 g at 22:54 on 10-27.
- The waterings at 10-10 19:14 and 10-19 00:36.
- Every reading and weighing taken within 30 h after a watering is left out. Every reading decays
  through that time.

## Two ways to fit a coefficient, and why they disagree

| | against the scale | within the day |
|---|---|---|
| cycle 1 (after 10-10) | 2.02 +/- 0.16 %/C (17 intervals) | 1.08 %/C |
| cycle 2 (after 10-19) | 1.59 +/- 0.38 %/C (13 intervals) | 0.52 %/C |
| both | 1.93 +/- 0.18 %/C (30 intervals) | |

**Within the day** means fitting reading = slow drying trend + k * tip.
- **Measured:** k is the same for every trend knot spacing from 2 h to 48 h.
- **Measured:** k also came out low on single days earlier: 1.0 %/C on 10-11/12 and 0.8 %/C on 10-12/13.

**Against the scale** compares each weighing interval: the change in reading against the grams the
scale lost and the change in tip temperature. The scale counts the water whenever it left, so the
hour of drying cannot leak into k.

**Why within-day fits come out low (measured on the scale alone):**

| | 11:00-15:00 | rest of the day | share of a day's loss in those 4 h |
|---|---|---|---|
| cycle 1 | 6.86 g/h | 1.22 g/h | 53 % |
| cycle 2 | 5.13 g/h | 0.87 g/h | 54 % |

The sun warms the tip and dries the pot in the same hours. A fit with a smooth drying trend
therefore reads the noon drop as a temperature effect, and k comes out low.

**Self-test** (`--self-test`): six synthetic pots with a true 2.00 %/C, losing 5.0 g/h in
11:00-15:00 and 0.9 g/h otherwise, with sun-driven tip warming:
- The scale fit gives 1.97 on average (1.61-2.26 for the individual pots).
- The within-day fit gives 1.13, close to the 1.08 measured on cycle 1.

The within-day number is a bias of the method, not a property of the soil.

## What other coefficients would do to the moisture number (measured)

The number was recomputed from the stored Rsoil and tip at each coefficient. At 2 %/C the
recomputed number matches the stored one to within 0.5 count.

| coefficient | cycle 1 miss vs scale | cycle 1 daily wobble | cycle 2 miss vs scale | cycle 2 daily wobble |
|---|---|---|---|---|
| 0 | 19.2 g | 21.7 g | 13.3 g | 6.2 g |
| 0.5 %/C | 15.9 | 14.6 | 10.8 | 6.7 |
| 1.0 %/C | 11.6 | 8.5 | 8.8 | 11.2 |
| 1.5 %/C | 7.3 | 10.8 | 7.8 | 17.2 |
| **2.0 %/C (in use)** | **5.3** | **16.9** | **8.1** | **22.9** |
| 2.5 %/C | 7.0 | 21.6 | 9.2 | 30.7 |

- **Miss vs scale:** the rms over weighing intervals of the number's change minus the scale's
  change, in grams. The scale repeats to 8 g.
- **Daily wobble:** the median over days of the hourly range about a straight line through the day.

What the synthetic pots show with no error except the coefficient:

| | at 2.0 %/C (exact) | at 1.0 %/C (wrong) |
|---|---|---|
| daily wobble | 12.8 g | 5.2 g (the minimum) |

A straight line through a day counts the noon drying as wobble. So the coefficient that makes the
line smoothest is the wrong one, in the synthetic pot and in cycle 1 alike.

## The other candidates on the moisture-vs-mass plot

Fitted against the scale over both cycles. The interval miss is scored out of sample: cycle 1 is
corrected with cycle 2's fit and the reverse.

| candidate | scale fit | plot uses | miss, no correction | miss, plot's | miss, other cycle's fit |
|---|---|---|---|---|---|
| soil resistance, no temp. correction | 5.8 g/C | 6.2 | 19.2 / 13.3 | 5.5 / 8.4 | 7.0 / 8.4 |
| soil resistance behind the mask | 5.9 | 6.2 | 19.0 / 13.9 | 5.2 / 8.5 | 6.0 / 8.5 |
| direct capacitance above air | 6.0 | 2.9 | 20.3 / 14.6 | 14.2 / 10.5 | 9.4 / 9.7 |
| soil capacitance (derived) | 6.0 | 3.2 | 20.3 / 14.9 | 13.4 / 10.5 | 9.5 / 9.9 |
| contact capacitance Cm | 5.9 | 1.7 | 21.1 / 12.9 | 17.8 / 10.6 | 11.3 / 10.0 |
| Cm + Cd - air | 5.9 | 1.9 | 21.0 / 13.0 | 17.3 / 10.5 | 10.8 / 9.9 |

Misses are in grams, given as cycle 1 / cycle 2.

- **Measured:** the plot's capacitance coefficients are 1.9-3.5 times too weak. They come from the
  first settled day, 10-09/10. The scale-fitted ones cut the cycle 1 miss by 4-7 g, out of sample,
  and the cycle 2 miss by 0.5-1 g.
- **Measured:** this is the same finding as the 10-17 thermal refit
  (`MSV4_MOISTURE_SLOPE_AND_THERMAL_FIT_2026-10-17.md`). The plot's default has not been changed.
- **Measured:** the plot's resistance coefficient (6.2 g/C, about 2.05 %/C) is right.
- **Measured:** every candidate moves by about 6 g of water per C, whatever it is made of.

## Limits

- **Scope:** one pot, one board, two cycles and 30 intervals.
- **Cycle 2 alone allows a somewhat lower coefficient.** Its fit is 1.59 +/- 0.38 %/C, and its
  misses at 1.5 and 2.0 %/C are level.
- **Inferred, not tested: a bias in the other direction.** Water that leaves preferentially in warm
  intervals without passing the blade would push the scale fit up. Plant tissue and the dish are
  examples. Such a mass effect would hit every candidate equally, and every candidate does come
  out at the same ~6 g/C. Real soil temperature acting on one resistance-dominated fit would do the
  same. Against it: a 6 g/C, 5 C day would mean a 30 g daily swing in water the probe cannot see.
  Also, the 2 %/C matches the textbook electrolyte value and the 1.6-1.75 %/C measured on board 2
  in tap water on 10-15, where nothing dries.
- **The 11:00-15:00 window** comes from the September multi-probe record, which says the sun reaches
  the shelf at about 12:00. The sun time at the pot's current spot is not measured.
- **Nights alone do not settle it.** The tip cools almost linearly overnight, so a per-night fit
  cannot separate it from steady drying. Cycle 2 nights gave anywhere from -2.0 to +2.3 %/C.

## What this supersedes

- The single-day fits of 10-12 to 10-14 (1.0 and 0.8 %/C) in the moisture-metric notes. Those
  are this noon-drying bias.
- The 10-27 probe 11 record's "this probe measures about 1.5 %/C, so the correction overshoots"
  (`MSV4_SENSOR11_FIRST_CYCLE_2026-10-27.md`). That figure was fitted within the day. Probe 11's
  pot is not weighed, so its coefficient cannot be checked against a scale, and it may be low by
  the same bias.
