# Thermal decomposition: testing the two-factor hypothesis (2026-09-19)

**Status: SEE THE CORRECTION IMMEDIATELY BELOW -- the headline 3.7x ratio is withdrawn as cross-pot.
The electrode half of the hypothesis rests on the within-AM 2.9x only. The noise
half is only half-testable. The additive form of the model is NOT established, because the blade's
thermal term is not reproducible across boards. Probe 5 is the discriminator and has 1.5 hours of
overnight data, so this is a checkpoint, not a conclusion.**

The hypothesis under test is the maintainer's, recorded in `MSV3/fleet.json` under
`measurement_model`: the readout and the electrode each contribute their own noise term and their own
temperature term.

| factor | noise | temperature |
|---|---|---|
| FDC readout | low | susceptible |
| v1.2 driving circuit (AM) | noisy | low |
| MSV3 blade electrode | -- | VERY susceptible |
| v1.2 electrode | -- | low |

**It is a hypothesis. The maintainer has said so explicitly.** This document tests it rather than
assuming it.

## CORRECTION 2026-09-19 (maintainer): the 3.7x ratio is CROSS-POT and is withdrawn

**Do not compare a probe's data across a pot change.** Different pot, different soil, different
moisture history, different contact. The maintainer flagged this immediately after the decomposition
below was written.

The decomposition subtracted across exactly that boundary:

| term | source | pot |
|---|---|---|
| readout, -0.041 %/C | probe 4 bare tank | **no soil at all** -- pot-independent |
| blade, -0.346 %/C | probe 4, 2026-09-05..09-09 | **its own pot** |
| v1.2, +0.094 %/C | probe 4, 2026-09-19 | **shared Alocasia pot** |

**The blade and v1.2 electrode terms were measured in different soils, so the 3.7x ratio between them
is withdrawn.** It is not evidence of an electrode property.

This also re-explains what this document called a board reproducibility problem. Probe 4's blade
figure (-0.387 %/C) was its own pot; probe 8's (+0.064 %/C) was the shared pot. That is two POTS, not
two boards -- and `MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md` already said so: *"The
pots behave differently, so a single compensation coefficient cannot cover both."*

**What survives:**

- **The readout term, -0.041 %/C at r = -0.994.** The bare tank has the electrode physically
  disconnected and touches no soil, so no pot confound can reach it.
- **The 2.9x within-AM electrode ratio**, sensor 3 against sensor 9 across 2026-09-17..09-19.
  **VERIFIED CLEAN 2026-09-19 (maintainer):** sensor 3 has been in this pot throughout with no pot
  change, and sensor 9 entered on **2026-09-14 ~18:30**, both well before the analysis window. Same
  pot, same soil, same days. This is now the only surviving electrode evidence in this document.

**What replaces it: probe 4 vs probe 5, from now on.** Both in the shared Alocasia pot, same soil,
same FDC readout, same days, differing only in electrode. No units problem, no pot confound, no
cross-board subtraction. **That is the real electrode measurement.**

**But neither has settled yet.** Insertion dates, confirmed with the maintainer:

| sensor | entered this pot |
|---|---|
| sensor 3 | throughout -- no pot change |
| sensor 2 | throughout |
| sensor 9 | 2026-09-14 ~18:30 |
| probe 4 | 2026-09-18 20:53 |
| probe 5 | 2026-09-19 ~20:24 |

**This puts a settling caveat on probe 4's own v1.2 figure.** The +0.053 %/C used in the
decomposition below comes from the clean window 09-18 20:53 to 09-19 15:13 -- the **first ~18 hours
after insertion**, while soil was still closing around the sensor and moisture equilibrating. It is
not a steady-state measurement of the v1.2 electrode.

There is precedent: `MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` recorded that sensor 9 initially
looked clean and the conclusion had to be reversed once it had run longer -- *"Sensor 9 was not
clean. It had not run long enough."* Sensor 9 had been inserted on 09-14, two days earlier. Expect
the same trap here.

**Re-measure probe 4 and probe 5 after several settled days before treating either number as an
electrode property.**

The decomposition below is kept as the record of what was tried and why it does not hold.

## The one comparison with no units problem

Every cross-readout comparison in this repository is blocked by AM detector millivolts not being
commensurable with FDC counts (see `MSV3_ELECTRODE_VS_READOUT_THERMAL_2026-09-19.md`). Probe 4's own
history sidesteps that entirely: **one board, one readout, one tank, three electrode conditions.**

| condition | tempco (capacitance %/C) | r | daily swing |
|---|---:|---:|---:|
| **FDC readout alone** -- bare tank, electrode disconnected | **-0.041** | **-0.994** | 0.28 % |
| FDC + MSV3 blade in soil (2026-09-05..09-09, own pot) | -0.387 | -0.928 | 2.60 % |
| FDC + v1.2 in soil (2026-09-19, shared pot) | +0.053 | +0.676 | 0.95 % |

Subtracting the readout term isolates each electrode's contribution:

```text
MSV3 blade  =  -0.387 - (-0.041)  =  -0.346 %/C
v1.2        =  +0.053 - (-0.041)  =  +0.094 %/C

|blade| / |v1.2| = 3.7x, and OPPOSITE SIGN
```

**Independent cross-check.** The AM readout gives its own within-readout electrode ratio of **2.9x**
(sensor 3 at ~8.5 mV/C against sensor 9 at ~24 mV/C, per-day across 09-17..09-19). Two different
readouts, measured in two different units, on different boards: **3.7x and 2.9x**.

## What this supports

**1. "The FDC readout is temperature-susceptible" -- CONFIRMED, with a nuance.** The bare tank has
**r = -0.994** with no electrode attached at all. That is an almost perfect thermal response and it
cannot come from soil, the electrode or the interface. But its magnitude is about **8x smaller** than
the blade's contribution. So the readout term is *real, extremely clean, and small*. Both halves
matter: clean enough to compensate for, small enough that it is not what drives the blade rigs.

**2. "The MSV3 blade is VERY temperature-susceptible relative to v1.2" -- SUPPORTED BY ONE
MEASUREMENT ONLY.** ~~3.7x within the FDC readout~~ **withdrawn as cross-pot, see the correction at
the top of this document.** What remains is **2.9x within the AM readout** (sensors 3 and 9, same
pot, same days, verified clean). One measurement chain, not two. The apparent agreement between
"3.7x and 2.9x" was an artifact of subtracting across a pot change.

**3. The sign flip is real.** Within one readout, the blade reads *drier* when warm and the v1.2
reads *wetter*. The electrode does not merely scale the thermal response, it inverts it. Any
compensation model that assumes a single signed coefficient per readout will fail.

## What is only half-testable: the noise axis

Noise hits the same units wall as everything else.

**Clean result:** within the v1.2 driving circuit, sensors 3 and 9 have essentially identical noise
(0.80 vs 0.92 LSB RMS residual after removing a 25-sample rolling median) despite carrying different
electrodes. **The noise is a property of the circuit, not the electrode**, which is consistent with
the hypothesis.

**Not resolvable yet:** ranking the FDC against the v1.2 circuit needs the AM detector's mV-per-pF
slope. Two further obstacles:

- Sensor 3 is **quantization-limited**: its whole daily excursion is 37 LSB, so most consecutive
  samples are identical and any jitter estimate sits on the quantization floor rather than on real
  noise. A first attempt at a median-consecutive-difference noise metric returned exactly 0 for this
  reason and was discarded.
- Normalising noise as a fraction of the reading re-introduces the `3300 - detector_mV` offset trap.

## What does NOT hold: the additive form

If the terms simply add, probe 5 (MSV3 blade + FDC) should read:

```text
-0.041  +  (-0.346)  =  -0.387 %/C capacitance, drier when warm, ~2.6% daily swing
```

**But the blade term is not reproducible across boards:**

| board | pairing | pot | tempco |
|---|---|---|---:|
| probe 4 | blade + FDC | its own | **-0.387 %/C** |
| probe 8 | blade + FDC | **shared** | **+0.064 %/C** |

Same pairing, two boards, **opposite signs and 6x different magnitude**. This was already recorded in
`MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md` as probe-to-probe spread; it is repeated
here because it directly undermines using a single blade coefficient.

So "the blade term" is not yet a stable electrode property. Either it depends on something not being
controlled -- soil contact, insertion depth, blade orientation, local moisture -- or the two terms
interact rather than add.

## The probe 5 test, and what each outcome means

Probe 5 is MSV3 blade + FDC in the shared pot. It has **1.5 hours of data, all overnight**, so there
is no thermal excursion in it yet. One clean sunny day resolves this.

| probe 5 outcome | reading |
|---|---|
| strongly thermal, **drier** when warm, swing ~2.6 % | two-factor model holds, blade term transfers |
| strongly thermal, **wetter** when warm | matches probe 8; the blade's *sign* is board- or contact-dependent |
| near-flat like probe 4 | the blade term is not an electrode property at all; the model needs an interaction term |
| anywhere between probe 8's +0.064 and probe 4's -0.387 | the bracket is wide enough that this is a weak test; see below |

**The honest caveat: the existing blade measurements bracket +0.064 to -0.387 %/C, so almost any
probe 5 result lands inside it.** A single day on probe 5 therefore tests the *sign and rough
magnitude*, not the additive form. Establishing the additive form needs the electrode crossover in
`MSV3_ELECTRODE_VS_READOUT_THERMAL_2026-09-19.md` (next steps A2), which moves one electrode between
boards and holds everything else fixed.

## Method

Bare-tank, blade and probe-8 figures are from
`MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md`, which used per-day detrending with
watering days excluded. The v1.2 + FDC figure is probe 4 over the clean window 2026-09-18 20:53 to
2026-09-19 15:13, 10-minute medians, phase 0 only, over-range band rejected. AM per-day figures are
against probe 6's indoor die temperature. Noise is RMS residual after a 25-sample rolling median on
raw samples.

**All temperature references in this document are board die temperatures in air, not soil
temperature.** No soil-depth temperature measurement exists in this experiment yet.
