# MSV4 `spectrum`: conductance-only, and why the four-parameter fit had to go (2026-09-23)

**Verdict: `Cs` removed from the firmware fit. It was not merely unidentifiable — keeping it made
the whole estimator ill-posed, and the `Rsoil` it reported moved up to 5.7 % with the optimiser's
starting seed alone.** The three-parameter fit is exactly determined, seed-stable, and 5.8x more
selective for moisture. `Rsoil` is now the headline output, temperature-corrected with the tip NTC.

Everything here is **modelled**. No MSV4 board has measured anything; the hardware arrived
2026-09-23. See [Before any of this means anything](#before-any-of-this-means-anything).

Evidence and regression test: `MSV4/scripts/test_msv4_spectrum_fit.py`
Field-solve output: `MSV4/generated/spectrum_fit_validation_2026-09-23.txt`

## The finding

The old model had four parameters:

```
Ye(w) = jw*Cd + 1 / ( 1/(jw*Cm) + 1/(Gs + jw*Cs) )
```

Written as a rational function of `s = jw`:

```
Ye(s) = s*[ Gs*(Cd+Cm) + s*(Cd*Cs + Cd*Cm + Cm*Cs) ] / [ Gs + s*(Cs+Cm) ]
```

Divide numerator and denominator by `Gs` and only **three** coefficients survive:

```
(Cd+Cm),    (Cd*Cs + Cd*Cm + Cm*Cs)/Gs,    (Cs+Cm)/Gs
```

**Four unknowns, three observables.** That is an exact one-dimensional family of parameter sets
that fit any data equally well — not a conditioning problem, a structural one.

Measured, not argued. These two parameter sets reproduce the same nine-frequency sweep to
**0 ppm in magnitude and 0 millidegrees in phase**:

| | Cm | Rsoil | Cs | Cd |
|---|---:|---:|---:|---:|
| A | 671.0 pF | 355.0 ohm | 14.90 pF | 5.00 pF |
| B | 656.4 pF | 370.9 ohm | 0.00017 pF | 19.58 pF |

Both return `rms ~1e-16`. The three identifiable combinations agree to 0.00000 %.

(Corrected 2026-09-24: row B originally printed `Cm 656.6, Rsoil 370.8, Cd 19.41`, which misses
row A by 138 ppm in `|H|`. The family keeps `Cd + Cm·Cs/(Cm + Cs)` fixed at 19.58 pF, so row B's
`Cd` must be 19.58. The values above reproduce row A to 0.000 ppm.)

**Consequence:** which one the LM returned depended on where it started. Across four seeds on
byte-identical data, the reported `Rsoil` ranged 350.3 to 370.9 ohm — a 5.7 % spread driven by
nothing physical. Any moisture trend built on that carries several percent of arbitrary jitter.

## The fix

Fix `Cs = 0`. Three unknowns against three observables, and the map inverts uniquely:

```
X = Cm/Gs,   Y = Cd*Cm/Gs,   Z = Cd+Cm   ->   Cd = Y/X,   Cm = Z - Y/X,   Gs = Cm/X
```

Seed spread after the change: **0.000 %**. Same 370.9 ohm from every start.

## What it cost, and what it bought

**Cost — a moisture-dependent bias.** Against a truth that really does have a `Cs`, the
three-parameter `Rsoil` reads +1.2 % high when dry and +9.1 % high when saturated.

**That bias is not caused by this change.** The four-parameter fit carried the same bias, and at
the wet end returned bit-identical numbers. It is a property of the lumped model against
distributed physics. Trend *steps* are preserved to ~3 %, so a per-pot calibration still works.

**Bought — selectivity.** Truth generated from the 2D field solve (distributed physics, so it
carries the ~0.2-1 % of drive model error that makes `Cs` unidentifiable in the first place):

| condition | 4-param dRsoil | 3-param dRsoil | rms (both) |
|---|---:|---:|---|
| er +10 % (must NOT move) | +3.61 % | **+0.55 %** | 9.47e-03 |
| sigma +10 % (must move) | -8.32 % | -7.36 % | 8.53e-03 |
| 5 um air gap | -8.34 % | **+15.51 %** | 4.80e-03 |

1. **`rms` is identical to three significant figures on every row.** The fourth parameter buys
   literally zero fit quality against real physics.
2. **Selectivity (response to conductivity over spurious response to permittivity) goes 2.3x to
   13.4x.** Dropping `Cs` made the moisture channel 5.8x cleaner — the opposite of the worry that
   a floating parameter might be protecting `Rsoil` by absorbing model error.
3. **The gap row is the important one.** The three-parameter fit reproduces the documented
   signature (`Cm` -49 %, `Rsoil` +15.5 %). The four-parameter fit made a contact gap look like
   **wetting** (-8.34 %) — a probe losing soil contact would have read as someone watering the
   plant, the one direction no downstream logic could catch.

## Other changes in the same commit

**`N = 4` (4 MHz) dropped from the sweep.** It existed to chase permittivity. It is also the
worst-conditioned point: `M = 4` is the fewest phases, so square-wave aliasing leans hardest there
(3rd-harmonic weight 1/9 against 1/81 at `M = 8`) and **17.97 % of that reading is not the
fundamental at all**. Removing it changes recovered `Rsoil` by 0.0 % across dry/moist/saturated and
leaves noise scatter unchanged. `drive 4` still reaches 4 MHz by hand. Sweep is now 250 kHz-2.67 MHz,
eight points.

**Conductance-first reporting.** `Rsoil` is labelled MOISTURE and printed first, then `Cm`
(contact quality), then `Cd` (nuisance). `Cs` is gone from the output entirely.

**Temperature correction.** `Rsoil @25C = Rsoil * (1 + 0.02*(T-25))`, the standard EC correction,
using the NTC at the blade tip. This is the channel MSV3 never had — its die sensor read the dry end.

**New `moisture` command.** One line, both orientations, temperature-corrected:

```
moisture Rsoil25=... Rsoil=... T=... Cm=... Cd=... rms=...
```

That is the reading to log over time.

## `Cm` is the series pair of BOTH masks (maintainer, 2026-09-23)

The physical current path is `plate A -> mask A -> soil -> mask B -> plate B`. There are two
interface capacitances, not one. Series impedances add and commute, so

```
1/(jw*CmA) + Rsoil + 1/(jw*CmB)  ==  Rsoil + 1/(jw*Cm),   1/Cm = 1/CmA + 1/CmB
```

and the two-mask topology is **bit-identical** to the one-mask form (verified: 0.0e+00 difference
in `|H|` at every sweep frequency). No code change. Only the interpretation moves:

- A fitted `Cm` of ~671 pF means each plate's own mask is ~1342 pF.
- A gap that halves ONE plate's mask capacitance drops `Cm` by 33 %; halving BOTH drops it by
  50 %. This is **not** a one-face/two-face discriminator on its own. A larger gap under one plate
  can drop `Cm` by more than 50 %, and smaller gaps under both can drop it by less than 33 %, so a
  single `Cm` reading confounds gap size with the number of plates affected. (Corrected
  2026-09-24.)
- **Which plate is faulty is not recoverable.** The series pair is symmetric in `CmA`/`CmB`, so
  `CmA 671/CmB 1342` and `CmA 1342/CmB 671` give identical `|H|`. The A/B swap cannot expose mask
  asymmetry either. Do not read an A/B difference as "electrode A is the bad one."

## Why separating `Cm` from `Rsoil` is the point

`Rsoil` alone is ambiguous between two situations that demand opposite actions:

| what happened | Rsoil | Cm |
|---|---|---|
| the pot actually dried out | up | steady |
| the blade lost soil contact | up | **collapses** |

Without `Cm`, a probe that has pulled away from the soil reads "thirsty." Soil shrink-back is slow,
so a multi-week trend drifts drier with nothing in the data to say why. `Cm` is the validity flag on
the number that gets acted on.

It is also the discriminator for the thermal investigation: a daily swing in `Cm` means the
interface is moving; a daily swing in `Rsoil` with steady `Cm` means conductivity/temperature.

Related, from the insertion-depth work the same day: insertion depth scales `Cm` and `Gs`
*together* while a gap moves `Cm` alone, so `Cm/Gs` separates "the blade moved" from "the soil
pulled away." That one is inferred from the lumped model, not yet measured.

## What this still cannot do

- **Soil permittivity.** `Cs` becomes visible only where `w*Cs ~ Gs`, i.e. ~30 MHz. At the 2.67 MHz
  ceiling it contributes ~13 % as much as conduction at the very top of the sweep and ~1 % at the
  bottom, against 0.2-1 % model error. This matches the literature: 50-70 MHz for EC-independent
  permittivity (TEROS 12, CS616).
- **Which plate has a gap** — see the symmetry note above.
- **Absolute water content.** `Rsoil` is a proxy and needs a per-pot calibration, and it carries
  the +1.2 % to +9.1 % model bias across its range.

## Before any of this means anything

**Zero hardware measurements exist.** Every number in this document comes from the 2D field solve
and the lumped model. The MSV4 boards arrived 2026-09-23 and nothing has been powered.

First real test is `spectrum open` on a bare board: does `Cp` land near the 12 pF the firmware
assumes? If it does not, the assumptions underneath all of the above need revisiting before the soil
numbers mean anything. `spectrum open` is RAM-only — it must be re-run after every reboot, or the
sweep silently falls back to the 12 pF default.

## Files

| file | role |
|---|---|
| `MSV4/arduino/MSV4_Probe_Bringup_BLE/MSV4_Probe_Bringup_BLE.ino` | the change; the comment block above `specNodeH` carries the proof |
| `MSV4/scripts/test_msv4_spectrum_fit.py` | regression test. `ident` (instant) proves the degeneracy and seed-stability; `field` (~20 min, 104 field solves to 132 MHz) regenerates the table above |
| `MSV4/generated/spectrum_fit_validation_2026-09-23.txt` | saved `field` output |
| `MSV4/docs/MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md` | annotated: its `Rsoil`/`Cs` sensitivity figures are one arbitrary point on the degenerate family. Its `Cm` gap conclusion survives |

Run `python MSV4/scripts/test_msv4_spectrum_fit.py ident` before touching the estimator. It exits
non-zero if either claim stops holding.
