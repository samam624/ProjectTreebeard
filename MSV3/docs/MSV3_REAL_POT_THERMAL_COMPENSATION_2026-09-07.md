# MSV3 probe 4 real-pot thermal compensation -- first causal result

> **Historical scale-2 analysis (superseded 2026-09-14).** Its final-day result showed bare-tank
> normalization removed only 11.7% of the daily harmonic. That evidence now supports removing C17
> and prohibiting production bare-tank measurements; see
> [MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md](MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md).

**Date:** 2026-09-07  
**Status:** Offline proof of feasibility from real-pot data. Not deployed to DisplayCenter moisture,
alerts, or calibration.

## Terminology correction

The connected FDC history from probe 4 is a **valid real-pot moisture-bearing signal with thermal
loading**, not a temperature measurement and not bad probe data. Temperature is a nuisance input
superimposed on a genuine multi-day drying trend. The preferred label for the first-stage derived
observable is therefore **reference-normalized moisture signal (experimental; higher = wetter)**.

It is not yet volumetric water content. Calling it a moisture signal describes its purpose and
direction without claiming a soil-specific absolute calibration.

## Data used

The analysis queried the linked Supabase `msv2_sensor_history` table for receiver
`MSV2088A3104A7AC`, sensor 4, over the trailing 72 hours ending near 2026-09-07 12:29 PDT:

- 3,697 complete rows, median spacing 70.003 seconds;
- board-temperature range 18.75-26.50 degrees C;
- connected count range 19,136,465-20,039,875;
- bare-tank count range 22,464,174-22,500,581; and
- all used rows had connected count, bare count, board temperature, and tank phase.

This is the real probe in the real pot. The first 48 hours were used for model selection and
coefficient fitting. The final 24 hours (1,235 rows) were held out and were not used to fit the
reported result.

## Causal model

Start with the physically referenced moisture signal in log-capacitance space:

```text
S = 2 * (ln(Dbare) - ln(Dconnected))
  = ln(Cconnected / Cbare)
```

This removes shared reference-clock and inductor movement without a learned coefficient. Board
temperature is passed through a causal first-order low-pass filter, representing the lag from the
exposed electronics to the buried sensing environment. The training search selected a **2-hour
thermal time constant**.

Thermal sensitivity changed as the pot dried, so the better basic model scales temperature gain by
a slow causal estimate of the capacitance excess:

```text
Eslow = EMA_48h(exp(S) - 1)
Teffective = EMA_2h(Tboard)

Scorrected = S
           - k * Eslow * (Teffective - Tref)
           - tankPhaseOffset
```

The first 48 hours learned `k = 0.01259 / degrees C` per unit of capacitance excess. The tank-phase
offset was only about 0.0049% in log-capacitance units. Both filters are causal and require only a
few scalar states, so the model is practical on the ESP32-S3.

## Held-out result

All entries below are measured on the untouched final 24 hours after removing only a straight-line
slow trend for scoring. Values are in comparable log-capacitance percent units.

| Signal | Fitted 24-hour peak-to-peak | Detrended RMS | P95-P05 spread |
|---|---:|---:|---:|
| Raw connected FDC | 0.583% | 0.412% | 1.150% |
| After bare-reference normalization | 0.515% | 0.387% | 1.152% |
| After causal temperature correction | **0.134%** | **0.117%** | **0.365%** |

The bare reference alone removed 11.7% of the final day's daily harmonic. The learned temperature
stage then removed **74.1% of what remained**. End to end, the daily harmonic fell by approximately
**77.1% versus the raw connected signal**. Detrended RMS fell by 71.5% versus raw and 69.7% versus
the bare-normalized signal.

The correction did not flatten the real drying trajectory. Mean capacitance above bare, by
successive 24-hour block, was:

| 24-hour block | Before temperature correction | After temperature correction |
|---|---:|---:|
| 1 | 34.97% | 34.88% |
| 2 | 31.13% | 31.19% |
| 3 | 27.89% | 27.93% |

The corrected endpoint change, comparing robust three-hour endpoint windows, remained -6.58% in
log-capacitance units. The algorithm therefore preserved the dominant direction and magnitude of
the three-day dry-down while substantially shrinking the cyclic false wetting/drying indication.

## What did not generalize

A model initialized from only the first 24 hours did not generalize to the following 48 hours. The
pot's fitted thermal coefficient changed materially over the window, approximately 0.59, 0.50, and
0.42% log capacitance per degree C over the three successive 24-hour blocks. The first day was the
wettest and also had a different slow transient. Treating its gain as fixed overcorrected the later,
drier data.

A linear "gain changes with elapsed time" model was also rejected: despite a better training fit,
it made the held-out daily harmonic about 7.9% worse. Elapsed time is not the physical state. Scaling
gain with the slow moisture signal produced the best held-out result.

## DisplayCenter direction

This supports a small real-time experimental path, separate from production moisture:

1. calculate `S` from each paired connected/bare row;
2. maintain the 2-hour temperature EMA and 48-hour moisture-state EMA;
3. apply the moisture-scaled temperature correction and phase offset;
4. expose it as **Temperature-compensated moisture trend (experimental; higher = wetter)**; and
5. continue retaining and transporting all raw values unchanged.

Do not let a fresh probe self-calibrate from one day. Use a seeded coefficient or wait for at least
two complete thermal cycles, and clamp any later adaptation. This 72-hour window contains a strong
dry-down but no labeled watering event, so it does not yet prove preservation of watering-step
magnitude or recovery timing. The next known watering should remain a required chronological test
before this signal drives alerts or the main watering recommendation.

## Reproduction

From the repository root, with the Supabase CLI already linked and authenticated:

```powershell
node MSV3/scripts/analyze_msv3_thermal_compensation.mjs --hours 72 --holdout-hours 24
```

The script performs read-only queries and does not modify cloud history.
