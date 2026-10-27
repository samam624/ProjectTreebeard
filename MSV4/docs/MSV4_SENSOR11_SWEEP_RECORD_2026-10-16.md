# MSV4 probe 11: the first day of cloud raw sweeps (2026-10-15 20:36 to 2026-10-16 16:46)

> **Superseded in part, 2026-10-27:** `MSV4_SENSOR11_FIRST_CYCLE_2026-10-27.md` covers a full
> watering cycle. Its temperature coefficients replace the provisional ones below (for example,
> Rsoil -1.48 %/C, not -2.24). It also finds that "CPE best on every sweep" holds only for the soil
> as first inserted: after a watering, the RC line fits best when wet and when driest.

This is the first full record from the raw-sweep feed built on 10-15
(`MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md`, "Raw sweeps to the cloud"). Probe 11 (MSV4 board 2,
Rs 3300) ran v262172 in its own unweighed pot of moist soil the whole time. The pot's state comes
from `MSV3/fleet.json`, last verified 10-15. The pot has no weight record, so these numbers score
the models on fit, temperature, drift, steps and noise. They do not score which model follows the
water.

Reproduce:

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 24 --rs 3300 --cp 8.75 --cd0 13.71 \
        --out data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-16.csv
    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 11 --raw \
        --since-hours 24 --csv data/sensor_logs/msv4_sensor11_history_2026-10-16.csv
    python MSV4/scripts/msv4_sweep_record.py --fits data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-16.csv \
        --history data/sensor_logs/msv4_sensor11_history_2026-10-16.csv \
        --step "2026-10-16 11:20" --flat "2026-10-16 04:00" "2026-10-16 11:20"

The full output is in `data/sensor_logs/msv4_sensor11_sweep_record_2026-10-16.txt`.

## The feed (measured)

- **Sweeps:** 121 of the 122 expected, one every 10 minutes. The one missing sweep is wake 41 on
  10-15, which fell inside the receiver's v3 flash.
- **Fidelity:** refitting the cloud copy reproduces the probe's own stored Rsoil to a median of
  +0.013 %, worst 0.48 %.
- **Temperature:** the tip ranged 19.25-24.00 C. It cooled overnight, sat near 19.3-20.5 C from
  04:00 to 11:00, then warmed in the afternoon.

## Fit quality over the day (measured)

Residual is in 1e-4 of drive. The air floor is 55-63 (10-15).

| model | median | p10 | p90 | 10-15 soil capture (15 sweeps) |
|---|---|---|---|---|
| CPE, Cd fixed | **60** | 41 | 68 | 74 |
| two-branch, Cd fixed | 93 | 71 | 104 | 106 |
| lumped, Cd free (the firmware's) | 141 | 133 | 149 | 148 |
| RC line, Cd fixed | 177 | 172 | 183 | 189 |
| lumped, Cd fixed | 281 | 263 | 291 | 338 |

- **Ranking:** the ranking from the single 10-15 capture holds over all 121 sweeps. The CPE is
  below the firmware's fit on every sweep, at a median of 0.43 times its residual.
- **The CPE is at the floor:** its median of 60 sits inside the air floor of 55-63. In this soil it
  fits as well as the front end can measure.
- **The CPE's series R is not identified.** It has a median of 53 ohm, a p10 of 0.1 and a p90 of
  357, and it sits at the 0.1 ohm lower bound on 49 of 121 sweeps.
  - The 375 ohm quoted on 10-15 came from one averaged capture and is not a stable number.
  - The CPE's usable outputs are its mask C and n.
- **n:** it held at 0.596-0.615 all day. It ran higher when warm: about 0.615 at 22.5-23.6 C and
  0.597 at 19.3 C.

## A step at 11:16-11:26 on 10-16 (measured; cause NOT KNOWN)

Every model moved in one 10-minute interval, between the sweeps at 11:16:39 and 11:26:39. The
comparison is the median of the last 3 sweeps before against the first 3 after.

| quantity | change |
|---|---|
| lumped R (firmware's) | +9.9 % |
| lumped mask C (the soil-coupled reading) | -11.1 % |
| lumped Cd | -1.8 % |
| CPE mask C | -8.3 % |
| CPE n | +0.013 |
| RC-line mask C | -17.6 % |
| RC-line R | -1.9 % |
| two-branch R | +9.3 % |
| two-branch mask C | -4.7 % |

- **Tip:** unchanged at 20.50 C through the step.
- **Board end:** the board-end (die) temperature dipped 22.0 -> 20.75 C between 11:17 and 11:24,
  then recovered.
- **Packets:** wakes 907 and 908 (11:22-11:23) were not heard.
- **Probe 10 (Alocasia pot) at the same time:** its board end also dipped 24.25 -> 22.75 C between
  11:14 and 11:20, and it lost wake 6644 at about 11:22. Its soil readings did not step: R held at
  7.39-7.53 kohm and the coupled reading at 78.9-81.7 pF from 10:40 to 12:10.
- **Inferred:** whatever cooled the room was shared by both boards, and the step was probe 11's
  alone. The sign matches the one-way contact steps seen in the cup tests (mask C down, R up), and
  it is the opposite of a watering. What happened at the pot was not stated, and fleet.json has no
  entry after 10-15.
- **Size:** the step is as large as about 20 hours of the overnight drift.

## Temperature and drift (PROVISIONAL)

From the joint fit ln(p) = a + step + b*tip + c*hours over all 121 sweeps:

| quantity | tip, %/C | drift, %/h | drift at a near-constant tip, 04:00-11:20 (19.25-20.50 C), %/h | sweep-to-sweep scatter, % |
|---|---|---|---|---|
| lumped R (firmware's) | -2.24 +/- 0.19 | +0.61 | +0.52 | 1.39 |
| lumped mask C | +1.40 +/- 0.12 | -0.36 | -0.30 | 0.77 |
| lumped Cd | +0.50 +/- 0.04 | -0.17 | -0.15 | 0.32 |
| CPE mask C | +2.12 +/- 0.18 | -0.57 | -0.44 | 1.40 |
| RC-line mask C | +1.59 +/- 0.17 | -0.19 | -0.06 | 1.32 |
| two-branch mask C | +1.20 +/- 0.27 | -0.26 | +0.03 | 2.81 |

Why these are provisional:
- Tip temperature and time correlate inside each leg: -0.76 before the step and +0.75 after it.
- Fitting each leg separately does not agree. The lumped R gives -1.64 %/C before the step and
  -0.92 after it.
- After the step, every mask C drifted UP (lumped +0.7 %/h, CPE +1.2 %/h) where it had drifted down
  all night. Inferred: this is recovery after the step, which a single drift term cannot hold.
- The firmware R's -1.6 to -2.2 %/C brackets the -1.6 to -1.75 %/C measured in tap water on 10-15.

What the table does show:
- **Overnight drift:** at a near-constant tip, every reading moved in the drying direction, with R
  up and mask C down. Inferred: drying or settling after the 10-15 17:26 insertion. The pot is not
  weighed, so this cannot be checked.
- **The best fit is not the steadiest number.** The CPE's mask C carries the larger temperature
  coefficient, the larger drift and about 1.8 times the sweep-to-sweep scatter of the firmware's
  lumped mask C.
- **What this record cannot say** is which model's reading follows the water. That needs sweeps
  from the weighed pot, where probe 10 sits on firmware that does not send them.
