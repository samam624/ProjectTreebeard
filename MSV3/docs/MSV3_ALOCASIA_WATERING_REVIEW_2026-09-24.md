# Multi-probe pot test reviewed across its first watering — 2026-09-24

> **Audited the same day. Read these first:**
> [`MSV3_ALOCASIA_WATERING_REVIEW_AUDIT_2026-09-24.md`](MSV3_ALOCASIA_WATERING_REVIEW_AUDIT_2026-09-24.md)
> and [`MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md`](MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md).
>
> - **Stands:** every Alocasia rig detects the watering. Blade + AM (sensor 9) is the worst
>   Alocasia rig. The electrode and readout interact, though the correct test compares
>   differences, not ratios.
> - **Confounded:** "the blade is net better through the FDC" compares probe 4 with probe 5, and
>   probe 5 is the only Alocasia rig on the shade side of the pot (maintainer). Its die swings
>   5.3–6.0 °C against probe 4's 7.5–9.5 °C.
> - **Does not stand:** the soil-water section. Its sign flip, "every in-soil rig" and the
>   retirement of the probe 4 vs probe 8 puzzle come from a per-day estimator that the post-watering
>   dry-down contaminates.
> - **Does not stand: probe 7's +2.99 step and the ratios built on it** (427.6 %, SNR 0.27,
>   90×). With the sun modelled out, probe 7's step is +5.1 logC%, and it did see the watering.
> - **The daily artifact** is the noon sun reaching the shelf, acting mainly on sensor 9 and
>   probe 7 (findings doc). Probe 7 was flipped to face the soil on 2026-09-24 ~23:17 PDT;
>   see [`MSV3_PROBE7_FLIP_TEST_2026-09-24.md`](MSV3_PROBE7_FLIP_TEST_2026-09-24.md).

Data window **2026-09-16 18:30 → 2026-09-24 16:30 PDT**, cloud raw history fetched read-only
for sensors 2, 3, 4, 5, 6, 7 and 9 from receiver `MSV2088A3104A7AC`. Reproduce every number
below with:

```
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor N --raw \
    --since-hours 190 --csv <dir>/treebeard_alocasia_sN_2026-09-24.csv
python MSV3/scripts/analyze_alocasia_watering.py <dir>
```

Current physical state is [`MSV3/fleet.json`](../fleet.json). This document is history from
its date and is not retro-edited.

## The watering

**2026-09-22, 10:50–10:55 PDT.** Located from the data: sensors 2, 3, 9, probe 4 and probe 5
all step inside one 5-minute bin, and probe 6 on the desk does not move at that instant.
The maintainer confirms this was the only watering of the test, and that **the Begonia pot
(probe 7) was watered at the same time.**

Probe 7's step cannot be found by inspecting its trace, and that is the single most useful
result in this run — see [Same electrode, same readout, different pot](#same-electrode-same-readout-different-pot).

The watering is also what makes this review possible. It is the first common physical input
every rig saw at once, so it supplies the transfer calibration the earlier thermal work
lacked: AM detector millivolts, FDC capacitance and MSV2 RC counts each divided by that
rig's **own** step response become directly comparable, which they never were in raw units.

## Setup during this window

| ID | Electrode in soil | Readout | Pot | Power |
|---|---|---|---|---|
| 2 | MSV2 interdigitated | MSV2 RC | shared Alocasia | battery |
| 3 | v1.2 | AM detector | shared Alocasia | ESP32, shared outlet |
| 9 | MSV3 blade | AM detector | shared Alocasia | ESP32, shared outlet |
| 4 | v1.2 across the tank, blade held in air as padding | FDC2214 | shared Alocasia | battery + solar |
| 5 | MSV3 blade | FDC2214 | shared Alocasia | USB, no battery |
| 7 | MSV3 blade | FDC2214 | **Begonia** | battery + solar |
| 6 | none — not in soil | FDC2214 | none, desk | battery |

All seven reported throughout. Probe 4 entered the pot 09-18 20:53 and probe 5 09-19 ~20:24;
probe 5's 09-20 is excluded everywhere below as post-insertion settling.

## Step response

Positive means wetter. The FDC unit is `100·ln(C/C_ref)`, a **log** ratio — it equals percent
only for small changes, and these steps are not small. A log ratio is the right scale here
anyway: a slope expressed in it means the same thing at any operating point.

| Rig | peak | at 24 h | predawn-matched | unit |
|---|---:|---:|---:|---|
| sensor 2, RC | +16.00 | +15.00 | **+14.00** | LSB |
| sensor 3, v1.2 + AM | +531.50 | +491.50 | **+506.00** | mV |
| sensor 9, blade + AM | +115.75 | +225.00 | **+226.75** | mV |
| probe 4, v1.2 + FDC | +31.61 | +17.86 | **+18.54** | log-cap % |
| probe 5, blade + FDC | +136.49 | +51.98 | **+52.84** | log-cap % |
| probe 7, blade + FDC, Begonia | +7.99 | +1.84 | **+2.99** | log-cap % |
| probe 6, control, no soil | +0.16 | +0.01 | **−0.06** | log-cap % |

`predawn-matched` is 03:00–06:00 on 09-23 minus 03:00–06:00 on 09-22. Both windows sit at the
daily temperature minimum — probe 4's die 21.25 °C then 20.75 °C, probe 7's own die 20.00 °C
then 19.75 °C — so the thermal term very largely cancels. It is the only step estimator that
survives a rig whose thermal swing exceeds its watering response, and everything below uses it.

Notes that matter:

- **Sensor 3 rings for an hour** after watering (1921 → 2550 → 2946 → 1856 mV) while sensor 9,
  in the same soil on the same detector design, steps once and settles. Probably the pour and
  the maintainer's proximity; either way sensor 3's *peak* is not a usable number, its
  predawn step is.
- **Sensor 9 keeps moving wetter** for two days — its 24 h value exceeds its peak. Water
  reaching the blade is not instantaneous.
- **Probe 6, in no soil, gives −0.06** log-cap % across the same windows. That is the null
  result the whole comparison rests on.

## Cross-architecture figure of merit

Each rig's daily thermal artifact and short-term noise divided by its own predawn step.
Two artifact measures, because they bracket the truth: `hipass` is |slope| × die range with
both series high-passed at ±3 h, which is immune to any slow trend but attenuates a smooth
24 h swing, so it is a **floor**; `diurnal` is the detrended peak-to-trough of the day, which
is what the chart actually shows but lets a curved dry-down leak in, so it is a **ceiling**.

| Rig | step | hipass | diurnal | noise | SNR |
|---|---:|---:|---:|---:|---:|
| sensor 3, v1.2 + AM | 506.00 | 1.3% | 2.5% | 0.09% | **40.2** |
| probe 5, blade + FDC | 52.84 | 1.7% | 4.5% | 0.09% | **22.1** |
| probe 4, v1.2 + FDC | 18.54 | 2.6% | 5.5% | 0.05% | **18.3** |
| sensor 2, MSV2 RC | 14.00 | 4.8% | 10.4% | 0.92% | **9.6** |
| sensor 9, blade + AM | 226.75 | 9.5% | 26.7% | 0.27% | **3.7** |
| probe 7, blade + FDC, Begonia | 2.99 | 76.9% | 366.8% | 1.21% | **0.27** |

SNR is the step divided by the diurnal artifact, the pessimistic measure. Below about 1, one
day of sun moves the reading as far as a whole watering does. Control: probe 6 sits at
+0.026 %/°C, 0.27 log-cap % of diurnal excursion per day.

The MSV2 RC rig is worth a second look. It is quantization-limited at 1 LSB and has by far
the worst noise on this scale, but the watering moved it 14 quanta, and it ends up **2.6×
better** than the blade-on-AM hybrid. Coarse is not the same as bad.

## Head to head, one variable at a time

### Electrode, through the FDC readout

Probe 4 and probe 5, shared pot, 09-23 and 09-24 — the only days both are settled and in the
same post-watering soil. This is the comparison `fleet.json` called "the REAL electrode
measurement".

| | step | diurnal | as % of step |
|---|---:|---:|---:|
| probe 4, v1.2 | 18.54 | 1.34 | **7.24%** |
| probe 5, blade | 52.84 | 2.51 | **4.75%** |

Blade vs v1.2: signal **×2.85**, thermal artifact **×1.87**, net **×0.66**.

**The blade is net better through the FDC.** It is genuinely more temperature-susceptible, as
the repo has said all along — but it buys more signal than it costs in artifact. Treating the
blade's thermal susceptibility as a liability, without dividing by what it pays for, has been
the wrong frame.

### Electrode, through the AM readout

Sensors 3 and 9, shared pot, all four clean days.

| | step | diurnal | as % of step |
|---|---:|---:|---:|
| sensor 3, v1.2 | 506.00 | 12.59 | **2.49%** |
| sensor 9, blade | 226.75 | 60.56 | **26.71%** |

Blade vs v1.2: signal **×0.45**, thermal artifact **×4.81**, net **×10.73**.

**The blade is net 10.7× worse through the AM detector.** It loses signal *and* gains
artifact — the only pairing in the test that is bad on both counts.

### The additive two-factor model is refuted

If the electrode contributed its own term independent of the readout, the two net ratios
above would match. They are **×0.66 and ×10.73**, a factor of 16 apart, from the same two
electrode types in the same soil on the same days. The readout and the electrode **interact**;
they do not add. `fleet.json`'s `measurement_model` listed the additive form as
"NOT ESTABLISHED" — this run moves it to refuted, and any compensation scheme built on one
signed coefficient per readout plus one per electrode will fail.

**Measured** above. The rest of this subsection is **inferred**: the likely mechanism is that
the blade over-loads the AM detector. Sensor 9 sits at 1864 mV predawn where sensor 3 sits at
2422 mV, i.e. more heavily loaded, and its gain relative to sensor 3 is not constant — over
the 6-day dry-down s9 moved +142 mV against s3's +168 mV (ratio 0.85), but over the watering
s9 moved −227 mV against s3's −506 mV (ratio 0.45). The blade's gain collapses at the wet,
heavily-loaded end, exactly where a moisture sensor needs it. Two regimes is evidence, not
proof; a swept-load bench measurement of the AM detector would settle it.

### Same electrode, same readout, different pot

Probe 5 and probe 7 are both MSV3 blade + FDC2214. Electrode and readout are held fixed.

| | step | diurnal | as % of step |
|---|---:|---:|---:|
| probe 5, Alocasia | 52.84 | 2.51 | **4.75%** |
| probe 7, Begonia | 2.99 | 12.79 | **427.6%** |

**A 90× spread with the electrode and the readout identical.** Probe 7's own die swings
19.4–29.6 °C, comparable to probe 4's 20.5–29.25 °C, so this is not simply "probe 7 gets more
sun". Its watering response is 18× smaller in absolute terms *and* its thermal artifact is 5×
larger. Both directions at once.

The practical consequence is the one the maintainer already observed: probe 7 was watered and
its trace does not show it. On this evidence **"MSV3 blade + FDC" is not a single thing**, and
no coefficient measured on one installation of it transfers to another. That, not board
variation, is the better explanation of the old probe 4 (−0.387 %/°C) versus probe 8
(+0.064 %/°C) disagreement.

## The dominant thermal path is the soil, not the board

Own-die slope, %/°C, per day. The no-soil control is the discriminator.

| Rig | 09-20 | 09-21 | *watering* | 09-23 | 09-24 |
|---|---:|---:|:---:|---:|---:|
| probe 4, v1.2 + FDC | +0.084 | +0.085 | → | +0.137 | +0.229 |
| probe 5, blade + FDC | −0.141\* | −0.111 | → | **+0.545** | +0.478 |
| probe 7, blade + FDC, Begonia | +0.792 | +0.956 | → | +1.007 | +2.061 |
| probe 6, **no soil** | −0.232 | +0.066 | → | +0.070 | +0.088 |

\* excluded elsewhere as post-insertion settling.

Every probe with an electrode in soil got a **larger** coefficient across the watering —
probe 4 about ×2.2, probe 7 about ×1.8, probe 5 flipping sign outright. The one probe with no
soil did not move: +0.066 → +0.070 → +0.088 %/°C, which is also the floor the board itself
contributes. Adding water to the soil changed the thermal response of three independent
boards and did not change the one that could not see the water.

Two things follow.

**The sign puzzle is resolved.** Probe 5 reads −0.11 %/°C dry and +0.55 %/°C wet, same board,
same electrode, same pot, four days apart. The sign is a function of **soil water**, not a
property of the board or the electrode. The long-running "the blade inverts the sign, and it
is board- or contact-dependent" hypothesis can be retired: dry soil leaves a small soil term
with the residual board terms of either sign showing through, and wet soil lets a large
positive term dominate.

**The direction is physically right, and it was predicted.**
[`MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md`](MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md)
derived the Maxwell–Wagner route eleven days before this watering and wrote down its
consequence: warmer soil → conductivity up → bulk R down → `C_eff = C_mask/(1 + tan²δ)` rises
→ resonance falls → **count falls**, a negative count coefficient, "dominant when wet".

A falling count is a rising capacitance, which is positive in this document's wetter-is-positive
convention. So that write-up predicted both the **sign** and the **wet-dominance**, and this run
delivers both: all three in-soil rigs go positive after watering, the no-soil control does not
move, and probe 5's coefficient changes sign precisely as its soil goes from dry to wet. That is
a prediction made in advance and confirmed, not a story fitted afterwards — which is worth more
than the size of any single coefficient here.

## Dry-down

Percent of the predawn step already given back at each predawn.

| Rig | 09-23 | 09-24 |
|---|---:|---:|
| sensor 2, RC | 0.0% | 7.1% |
| sensor 3, v1.2 + AM | 0.0% | 10.2% |
| sensor 9, blade + AM | 0.0% | −5.6% |
| probe 4, v1.2 + FDC | 0.0% | 22.2% |
| probe 5, blade + FDC | 0.0% | 13.9% |
| probe 7, blade + FDC, Begonia | 0.0% | 111.3% |

The three Alocasia readings that have real dynamic range disagree about the drying rate by a
factor of two (10%, 14%, 22% per day) while looking at the same soil. Probe 7's 111% is not a
dry-down measurement — its step is smaller than its own daily wobble, so the figure is
dominated by artifact. Sensor 9's negative value is the continuing soak-in noted above.

## What this changes

Measured, from this run:

1. Every rig in the Alocasia pot detects a watering unambiguously. Probe 7 in the Begonia
   does not, despite being watered.
2. Through the FDC readout the blade is a **net win** (×0.66 normalized artifact).
3. Through the AM readout the blade is a **net loss** (×10.73). Blade + AM is the worst
   pairing in the fleet and should not be carried into MSV4.
4. The electrode and readout **interact**; the additive two-factor model is refuted.
5. Installation dominates both: 90× spread with electrode and readout fixed.
6. The dominant thermal path is in the **soil**, and its coefficient grows with water content.
   The no-soil control proves the board is not the main contributor.
7. The apparent sign inversion is a **moisture effect**, not a board or electrode property.

Inferred, and worth one bench measurement each:

- The blade over-loads the AM detector (swept-load measurement of detector gain vs load).
- Probe 7's installation has both weak soil coupling and strong thermal coupling; a lift-and-
  reseat with a pot-mass reference would say which.

Open, and not answerable from this run:

- Water volumes were not recorded for either pot, so no step here is a calibrated sensitivity.
  Record volume and pot mass at the next watering; it is the cheapest missing input.
- One watering gives one step per rig. Every normalized figure above has a sample size of one
  event, and the SNR ordering — though its spread is far larger than any plausible error —
  should be re-derived at the next watering before it is treated as settled.
- There is still **no soil-temperature channel**. Every coefficient here is against a board
  die, and probe 4's die sees sun on the head.
