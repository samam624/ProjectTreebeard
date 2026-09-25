# Audit of the 2026-09-24 watering review

Checks [`MSV3_ALOCASIA_WATERING_REVIEW_2026-09-24.md`](MSV3_ALOCASIA_WATERING_REVIEW_2026-09-24.md)
against the same raw data it used. Its numbers reproduce exactly from
`analyze_alocasia_watering.py`, so the problems below are in the method and the conclusions, not in
arithmetic. Reproduce the audit with:

```
python MSV3/scripts/audit_alocasia_watering.py <dir with treebeard_alocasia_s<ID>_2026-09-24.csv>
```

It imports the review's own binning and rejection code, so both scripts see identical inputs.

A later, model-free look at the full data through 09-24 22:55 is in
[`MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md`](MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md). It
finds that the daily artifact is the noon sun on the shelf, acting mainly on sensor 9 and probe 7.

**Premise, stated by the maintainer 2026-09-24 and recorded in `fleet.json`:** the Alocasia pot is
small and saturates uniformly, so every Alocasia sensor receives the same water and each rig's
watering step is a valid common calibration. The Begonia (probe 7) was watered at the same time. A
first draft of this audit argued the opposite, citing different drying rates. That contradicted
`fleet.json` and has been withdrawn: drying rates can differ between spots in a pot that was
watered uniformly.

## What holds up

- Every Alocasia rig registers the watering, and probe 7 in the Begonia does not.
- **The blade is net 10× worse through the AM** (×10.05–10.73). It holds on matched dry days,
  matched wet days, and in any unit. The FDC figure (×0.66, blade better) is just as
  numerically robust, **but it is confounded by position**. The maintainer reported later the same
  day that probe 5 is the only Alocasia rig on the shade side of the pot, and its die swings
  5.3–6.0 °C against probe 4's 7.5–9.5 °C. See
  [`MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md`](MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md).
- **The electrode and readout interact.** The review tested this the wrong way; see
  [Additivity](#additivity-right-conclusion-wrong-test). The correct test gives the same answer.
- **The SNR ranking.** It rests on one watering event, which the review already says.
- **Probe 5's thermal coefficient rose after watering**, from about +0.04 to +0.43 log-cap %/°C in a
  joint fit. That rise is larger than any movement in the no-soil control.
- Probe 5, on USB and the shared outlet, is as quiet as battery-powered probe 4 relative to its
  step (0.09 % vs 0.05 %). The shared mains outlet does not explain sensor 9.
- **Probe 5's +136 spike at 10:50 is the watering itself.** The blade is momentarily saturated
  while the water passes through. This is not handling (maintainer, recorded in `fleet.json`).
- The "blade over-loads the AM detector" inference stays the leading inference. It is still
  unmeasured, and a swept-load bench test would settle it. With uniform saturation, unequal water is
  no longer an alternative. Open oddity: sensor 9 kept reading wetter for two days after the
  watering, which over-loading alone does not explain.

## Additivity: right conclusion, wrong test

The review argued that under an additive model the blade/v1.2 **ratio** would match through both
readouts. That is the prediction of a *multiplicative* model. Once every rig is on the
watering-step scale, an additive model (artifact = readout term + electrode term) predicts the
blade − v1.2 **difference** matches instead. On matched wet days 09-23/24:

| readout | v1.2 | blade | difference | ratio |
|---|---:|---:|---:|---:|
| FDC | 7.24 % | 4.75 % | **−2.49 pp** | ×0.66 |
| AM | 2.68 % | 26.93 % | **+24.25 pp** | ×10.05 |

The differences disagree in sign and by a factor of ten. The ratios disagree too. **Both separable
forms are rejected**, so the review's conclusion ("the readout and electrode interact") stands. Only
its stated reasoning needed fixing.

## Errors that stand

### 1. The soil-water section uses the estimator the review's own script says is contaminated

The script's `high_pass()` docstring says that after watering, the curved dry-down "inflate[s] the
apparent thermal slope" of a linear detrend. The soil-water table, which carries the sign-flip
headline, is built from exactly that linear-detrend estimator. The same coefficients from other
estimators:

| rig | review (per-day detrend) 09-21 → 09-23 | hipass 09-21 → 09-23 | joint fit PRE → POST |
|---|---:|---:|---:|
| probe 4 | +0.085 → +0.137 | +0.074 → **+0.018** | +0.074 → +0.171 |
| probe 5 | **−0.111** → +0.545 | −0.026 → +0.149 | **+0.041** → +0.433 |
| probe 7 | +0.956 → +1.007 | +0.506 → +0.312 | +1.232 → +1.516 |
| probe 6, no soil | +0.066 → +0.070 | +0.087 → +0.067 | **−0.045 → +0.078** |

(Joint fit: the temperature coefficient fitted together with a multi-day trend, linear before the
watering and exponential after. This avoids both per-day detrending and the ±3 h rolling-median
high-pass, which passes well under 10 % of a smooth 24-hour cycle.)

- **There is no sign flip.** Probe 5's dry-soil value is −0.026 with r = −0.09 (hipass), or +0.041
  (joint fit). "Negative when dry" rests on one estimator on one day.
- **Probe 4's rise depends on the estimator.** The hipass estimator shows none. In the joint fit the
  no-soil control moved +0.12, as much as probe 4's +0.10.
- **Probe 7 barely changed** (×1.2 in the joint fit). Its coefficient was already about +1 %/°C in
  pre-watering soil on every day from 09-17 to 09-21 (checkpoint doc). That contradicts "dry soil
  leaves a small soil term."
- **The control's "did not move" leaves out 09-20.** The review's own table prints probe 6 at
  −0.232 that day. The trend-immune estimators put it at +0.06, so the −0.232 is the detrend
  estimator failing on a 1.75 °C die range. The same estimator produced the headline numbers.
- **09-24 is a partial day** (data end 16:30). Truncating other days to 16:30 moves probe 7's
  detrend slope from +0.79/+0.96/+1.01 to +1.38/+1.18/+1.77. So the "doubling" to +2.06 on 09-24
  is mostly truncation.

What is measured: **probe 5's coefficient rose several-fold after watering.** "The sign is set by soil
water", "every in-soil rig", and retiring the probe 4 vs probe 8 puzzle all go beyond that.

### 2. "Every in-soil rig reads wetter when warm" (in `fleet.json`) is false for the AM rigs

Sensors 3 and 9 read **drier** when warm, before and after the watering. Joint fit against probe 4's
die, wetter-positive: s3 −1.13 → −1.56, s9 −8.7 → −7.8 mV/°C. Sensor 9 is an MSV3 blade in the same
soil, and its coefficient did not grow. So "soil water dominates the temperature term" does not hold
for the blade in general. At most it holds for blade + FDC.

### 3. Probe 7's step is inside its own night-to-night scatter

Probe 7 was watered (settled; see `fleet.json`). The issue is what its sensor recorded. Its predawn
medians from 09-17 to 09-24 are +3.19, +5.61, −2.52, +0.73, +3.80, +1.04, +4.03 and +0.70. The
night-to-night change with no watering has sd 4.19 and ranges from −8.13 to +3.25. The change
across the watering, +2.99, is inside that. Applying the review's "peak" estimator at 10:50 on
non-watering days gives +1.8 to +7.2, against +7.99 on the watering day.

The review's conclusion is correct: probe 7 does not show the watering. The numbers built on it
are not. "+2.99 step", "427.6 %", "SNR 0.27", "18× smaller response" and "90× spread" all divide by
a value that is noise.

Also, "electrode and readout identical, so installation dominates" leaves out that probe 7 is
**potted** (`fleet.json` mods) and probe 5 is not. The potting compound sits in the sensing path's
thermal environment and is a known physical difference between the two boards.

### 4. "A prediction made in advance and confirmed" is right about the sign only

The Maxwell–Wagner text is genuinely in the 2026-09-13 commit (`fab6357`), so the positive
capacitance sign was predicted. The wet-dominance was not. In that doc's own model
`C_eff = C_mask/(1 + tan²δ)` with `tan δ = ωR_soil·C_mask`, the log-temperature sensitivity scales
with `tan²δ/(1 + tan²δ)`. That **falls** as the soil wets and R_soil drops. The absolute contribution
peaks at `tan δ = 1`. At the operating point that doc fitted for a wet pot (`tan δ = 0.588`), the
model predicts watering **reduces** the term. The phrase "dominant when wet, scales with tan²δ" in
the 09-13 doc contradicts itself.

The same doc's cleanest blade measurement also contradicts the review's retirement of the sign
puzzle. Probe 4 read **−0.387 %/°C** (drier when warm) with r = −0.928 on five consecutive days.
That is a large negative term, and the review's model has no negative term that size.

## Minor

- `fleet.json` → `pots.begonia.watering_events`: "roughly 20 log-capacitance percent per day". The
  measured diurnal span is 12.3–13.3 (09-23/24) and 7.1–9.6 (09-20/21).
- The banner added to
  [`MSV3_ALOCASIA_THERMAL_CHECKPOINT_2026-09-21.md`](MSV3_ALOCASIA_THERMAL_CHECKPOINT_2026-09-21.md)
  said "the additive form is refuted and the blade is net better". The first stands. The second
  is now marked as position-confounded there. The checkpoint's own body is careful and holds up.
- [`MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md`](../../MSV4/docs/MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md):
  the algebra and both claims are verified (`test_msv4_spectrum_fit.py ident` passes; Cs = 0 gives
  370.9 Ω from every seed). Two small errors:
  - **Row B of its example table is misprinted.** The quantity `Cd + Cm·Cs/(Cm + Cs)` is identical
    across the degenerate family. It is 19.58 pF for row A, so row B's `Cd` should be ≈19.58 pF,
    not 19.41 pF.
  - **"A gap under one plate drops Cm by 33 %, under both by 50 %"** holds only for a gap that
    exactly halves each affected plate's mask capacitance. Gap size and one-vs-two plates are
    confounded, so it is not a one-face/two-face discriminator.
