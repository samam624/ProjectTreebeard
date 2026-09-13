# Why amplitude sensing rejects temperature and frequency sensing does not

> ## How to read this document
>
> It was written in one session and corrected three times as measurements contradicted earlier
> conclusions. The corrections are kept in place rather than edited away, because the method errors
> are themselves worth not repeating. **Read in this order:**
>
> 1. **[The answer, consolidated](#the-answer-consolidated)** — the mechanism, in one sentence, with
>    the numbers that close it.
> 2. **[Correction 3](#correction-3-probe-4-is-strongly-thermal-and-correction-2-was-a-method-artifact)**
>    — the measured thermal coefficients, and the detrending rule that makes them trustworthy.
> 3. **[Next experiment](#next-experiment-the-same-electrode-in-both-readouts)** — the build that
>    settles the one question still open.
>
> **Settled and trustworthy:**
> - Probe 4 is strongly thermal: **r = -0.928, -0.387 %/C**, five consecutive clean days.
> - Its board contributes only ~10% of that. The rest is the electrode-soil path.
> - The v1.2 is **10 to 50x less thermally sensitive** on the same days. The original premise holds.
> - Probe-to-probe spread is real: probe 4 at -0.387 %/C, probe 8 at +0.064 %/C. **Opposite signs.**
> - **There is no datasheet capacitance floor.** TI's own example tank is 53 pF. Removing C17 buys
>   about 3.5x signal and raises the tank to ~6.3 MHz.
> - MSV3's per-sample noise is **6x quieter** than the v1.2's. The converter is not the problem.
> - The FDC2214 exposes **no loss measurement at all**, only a one-bit amplitude warning, and caps at
>   **10 MHz** excitation.
>
> **Withdrawn:** "temperature is not your main problem" (Correction 2) — false for probe 4.
> **Open:** whether the v1.2's advantage is the modulation scheme or its electrode geometry. The
> models here reproduce probe 4 well but do **not** reproduce the full size of the v1.2's advantage,
> so electrode geometry may be doing part of the work. The next experiment separates them.
>
> **Method rule, learned the hard way:** never assess a daily effect by removing one straight line
> across a multi-day window, and never high-pass with a boxcar as wide as the period of interest.
> Detrend per day, and exclude watering and handling days explicitly.

**Date:** 2026-09-13
**Question:** The Amazon "Capacitive Soil Moisture Sensor v1.2" extracts a cleaner moisture signal
than either of our own generations, and the thermal artefact has followed us across three quite
different architectures. Is that a property of the modulation scheme, and if so, which mechanism?
**Answer:** Yes, and the mechanism is where the soil's *loss* term lands in the transfer function.
Both of our architectures put it somewhere that a single calibration coefficient cannot remove.

This closes the open V4 temperature item left by
[the insertion-settling and temperature plan](MOISTURE_INSERTION_SETTLING_AND_V4_TEMPERATURE_PLAN_2026-08-24.md)
and explains the two results that did not previously fit: the
[32x wet/dry sensitivity difference](MSV3_SOIL_THERMAL_SENSITIVITY_2026-09-04.md) and the
[77% ceiling on causal compensation](MSV3_REAL_POT_THERMAL_COMPENSATION_2026-09-07.md).

## The controlled measurement

Sensors 2, 3 and 8 have shared one Alocasia pot since 2026-09-08, so for the first time all three
architectures see the same soil, the same water, and the same temperature at the same time. 96 hours
were pulled from `msv2_sensor_history` (receiver `MSV2088A3104A7AC`, 2026-09-09 to 2026-09-13,
4,697-4,885 rows each). Probe 8's die sensor supplies the shared temperature: it swings 19.75-28.25 C.

Each series was linearly detrended, then averaged into a repeatable hour-of-day profile, which is the
same scoring method the 2026-09-07 compensation work used. MSV3 counts are doubled to put them in
capacitance units, because the FDC reports a frequency and `f` goes as `1/sqrt(C)`.

| architecture | daily wobble | drying trend | signal per unit wobble | correlation with temperature |
|---|---:|---:|---:|---:|
| **v1.2, AM**, fixed-frequency amplitude (sensor 3) | **0.410%** | 0.0924 %/h | **22.6** | +0.119 |
| **MSV2, FM**, RC/oscillator count (sensor 2) | 1.028% | 0.0259 %/h | 2.5 | **+0.837** |
| **MSV3, FM**, LC resonant via FDC2214 (sensor 8) | **1.956%** | 0.0315 %/h | 1.6 | +0.386 |
| MSV3 board alone, electrodes disconnected | 0.139% | 0.0002 %/h | 0.1 | **-0.996** |

Three things in that table matter more than the headline ratio.

**The board is not the problem.** With the mux disconnecting the electrodes, the MSV3 tank drifts only
0.139% and correlates -0.996 with temperature. The FDC, its reference clock, the 6.8 uH inductor and
the 270 pF C17 together form an excellent, almost perfectly linear thermometer whose amplitude is one
fourteenth of the connected signal's. Every remaining explanation has to live in the electrode-soil
path. This also disposes of the obvious objection to the comparison, that sensor 3's electronics sit
in calmer air than probe 8's: even crediting MSV3 with a perfectly isothermal board removes only
0.139% of its 1.956%, leaving it still 4.4x worse than the v1.2's entire budget.

**The MSV2 RC oscillator is very nearly a thermometer.** At +0.837 correlation it is the most purely
thermal of the three connected signals, which is what the mechanism below predicts for a relaxation
oscillator specifically.

**The v1.2's residual is mostly not thermal at all.** At +0.119 correlation its 0.410% wobble is
largely something else, and the known mains coupling from the receiver PSU sharing the bench outlet
is the obvious candidate. Its true thermal budget is smaller than the number in the table.

## What the v1.2 actually does

The circuit matters, because the usual description of it is wrong. Three independent analyses agree,
and the measured behaviour of our own rig matches:

- A TLC555 runs astable at a **fixed ~1.53 MHz** (1% standard deviation across samples, 34% duty),
  set by onboard `R2`/`R3`/`C3`. The soil electrode is **not** in the timing network.
- Pin 3 drives the blade through a series resistor. The electrode is the **shunt leg of an RC
  low-pass**, so its reactance and that resistor form a voltage divider.
- A diode peak detector and smoothing capacitor turn the attenuated square wave into DC, roughly
  3.0 V dry to 1.2 V wet at 3.3 V supply. Wetter means more capacitance, less reactance, more
  attenuation, lower voltage.

So the oscillator is a clock, not a sensor. Moisture rides on the **amplitude** of a signal
interrogated at a frequency that never moves. Our own bench sketch already described it correctly.

The converse experiment has been run by someone else and is worth knowing about. The Cave Pearl
Project moved the resistors on this exact module so the blade became the timing capacitor, converting
it from AM to FM to gain ADC resolution. The result got worse, not better: the interrogation
frequency now swept 50 kHz dry to 1.5 kHz wet, a 33:1 range that ran straight into electrode
polarization, and the record developed a bend that did not match the known drying cycles. Same
electrode, same soil, same author. Only the modulation scheme changed.

## The mechanism

> **Correction, 2026-09-13 (same day), after maintainer challenge.** The first version of this
> section named the resonant damping term `1/(4Q^2)` as the dominant path. That was incomplete and
> got the sign wrong. The maintainer objected that the electrodes are under soldermask and measured
> **>500 Mohm between the traces at DC, even in salt water**, so no conduction path exists. The DC
> measurement is correct and valuable, and it does not refute the loss coupling, because **the
> soldermask is a series capacitor: an open circuit at DC and a 622 ohm path at 3.2 MHz.** Working
> that through properly showed the dominant term is not damping at all but **Maxwell-Wagner
> interfacial polarization**, in which the mask is not a shield but the polarizing layer. The
> conclusion about sign instability survives; the mechanism attribution is corrected below.

### Why a DC probe cannot see this

The measurement that matters is not the one that was run, and no reading of it could have been
informative:

| | DC | 3.2 MHz |
|---|---:|---:|
| electrode branch impedance | >500 Mohm (measured) | **622 ohm** (from 80 pF) |

That is a ratio of roughly **800,000 to 1** across the same physical structure. A series capacitor
reads open at DC no matter what is on the far side of it, so the probe would have returned
">500 Mohm" in salt water and in dry air alike. If both were in fact measured and agreed, that is
proof the method is blind to the soil, not proof that the soil is non-conductive.

The electrode capacitance used here is not assumed. It is backed out of sensor 8's own counts:
`C_connected/C_bare = (D_bare/D_connected)^2 = 1.2835`, which puts the electrode at **80 pF** on top
of C17 and parasitics, with the tank near **3.2 MHz**.

### Maxwell-Wagner, not damping

Model the branch as the blocking mask capacitance in series with the bulk soil resistance. The
apparent capacitance seen at the terminals is

```text
C_eff = C_mask / (1 + tan^2 d)      where  tan d = w * R_soil * C_mask
```

Raise the soil conductivity and `R_soil` falls, `tan d` falls, and **`C_eff` rises**. Warmer soil
therefore looks like *wetter* soil, the resonance drops, and the count falls. That is a **negative**
count coefficient, which is the sign probes 4 and 5 actually show and which a damping-only account
cannot produce.

How much soil resistance does it take? Very little:

| `tan d` | required `R_soil` | resulting tank Q |
|---:|---:|---:|
| 0.25 | 155 ohm | 17.5 |
| 0.50 | 311 ohm | 8.8 |
| 1.00 | 622 ohm | 4.4 |

Fitting probe 4's measured **-0.1174 %/C** gives `tan d = 0.588`, i.e. a bulk soil path of about
**366 ohm** between the electrodes. That is an entirely ordinary value for a watered pot, and it is
completely invisible to a DC probe through an intact mask.

So the honest statement of the confound is: soil conductivity is real, it is well inside the range
that matters, the soldermask does not block it at 3 MHz, and the mask is in fact what converts it
into apparent capacitance.

### Why the coefficient still changes sign

Three terms act at once, and they do not share a sign in the count domain:

| term | direction on count | notes |
|---|---|---|
| Maxwell-Wagner apparent-permittivity inflation | **negative** | dominant when wet, scales with `tan^2 d` |
| water permittivity falling with temperature | positive | small, roughly -0.36 %/C on `C` |
| resonant damping, the `1/(4Q^2)` term | positive | matters only once Q reaches single digits |

Maxwell-Wagner alone is always negative, yet probe 8 measures **+0.0389 %/C**. So on that probe the
two positive terms are winning. The net coefficient is a difference of comparable terms, which is
exactly why it crosses zero and inverts as the pot wets and dries. That conclusion is unchanged from
the first version of this document, and it is the part that matters for compensation.

### The underlying physics

Soil is not a capacitor. It is an admittance `Y = G + jwC`, and temperature moves the two parts very
differently:

- **`G`, the loss**, rises about **+2%/C**. This is the well-established bulk-EC coefficient, driven
  by falling pore-water viscosity and rising ion mobility in the diffuse double layer.
- **`C`, the permittivity**, falls about **-0.36%/C**, following water's relative permittivity.

`G` is thus the strongly temperature-dependent term, and moisture is what we want out of `C`. The
whole question is how much of `G` each readout lets through. Modelling one identical soil element
into all three readouts, as a function of loss tangent `tan d = G/wC`:

| `tan d` | tank Q | AM divider | FDC resonant |
|---:|---:|---:|---:|
| 0.1 | 30.3 | +0.102 %/C | +0.059 %/C |
| 0.5 | 6.1 | -0.350 | +0.045 |
| 1.0 | 3.0 | -0.748 | **-0.001** |
| 1.5 | 2.0 | -1.001 | -0.083 |
| 2.0 | 1.5 | -1.167 | -0.208 |
| 3.0 | 1.0 | -1.365 | -0.660 |

Read the FDC column carefully, because it is not the simple story. In isolation a *high-Q* resonator
is superb at rejecting the **damping** path: frequency depends on it only through the second-order
`1/(4Q^2)` term, so at `tan d = 0.1` the FDC is roughly 165x less damping-sensitive than the divider.
The resonant architecture is not inherently bad, and damping is not where MSV3 loses.

Where MSV3 loses is the Maxwell-Wagner path derived above, which the resonant readout has no defence
against at all, because it arrives already disguised as capacitance before the resonator ever sees it.
The damping term then adds a second, opposite-signed contribution once a lossy electrode drags Q into
single digits. So in that region:

1. Maxwell-Wagner inflates apparent capacitance as conductivity rises, pushing the count **down**.
2. The `1/(4Q^2)` and permittivity terms push the count **up**.
3. Because these are comparable and opposed, the net coefficient **passes through zero** and reverses.

That last point is the one that matters, and it is confirmed in the field. Three MSV3 probes have now
been fitted, and their temperature coefficients do not agree even in sign:

| probe | state | coefficient (count) | as capacitance |
|---|---|---:|---:|
| 4 | wet pot | -0.1174 %/C | +0.235 |
| 5 | drying soil | -0.0037 %/C | +0.007 |
| 8 | shared pot | +0.0389 %/C | -0.078 |

The 32x spread between probes 4 and 5 was previously attributed to wetness setting the gain. That is
right about the cause and incomplete about the shape: wetness sets `tan d`, and the coefficient is not
merely proportional to it but **changes sign across it**. A probe sitting near the crossing reads a
near-zero coefficient not because it is thermally clean but because two large terms happen to cancel
at that moisture. Move the moisture and the cancellation moves too.

This is why compensation stalled at 77%. A model of the form `k * excess * (T - Tref)` fits a gain that
scales with wetness, but it cannot represent a coefficient that crosses zero and inverts, so the
residual it cannot reach is structural rather than a matter of more training data.

The MSV2 RC relaxation oscillator fails differently and harder. Its period is set by charging a
capacitor toward a threshold, and the soil's conductance sits directly in that charging path, moving
both the time constant and the asymptote **linearly**. Worse, the asymptote is `VCC/(1 + Rt*G)` while
the 555 trips at `2/3 VCC`, so past a certain conductance the node cannot reach the threshold at all
and the oscillator simply stops. Keeping it alive in wet soil forces `Rt` small, which raises the
frequency until comparator propagation delay becomes a large fraction of the period, which is itself
temperature-dependent. That linear, squeezed-from-both-ends coupling is exactly what +0.837
correlation with temperature looks like.

The v1.2 avoids all of this not by being loss-immune, which it is not, but by having **no resonance to
collapse and a fixed interrogation frequency**. There is no `Q` in its transfer function, so no
denominator that blows up when the soil gets lossy, and no sign reversal. The soil is always sampled
at 1.53 MHz no matter how wet it is, so its position on the soil's dispersion curve never moves and
whatever coefficient it has stays put and stays calibratable. In both FM architectures the
interrogation frequency **is** the output, so the measurement condition slides with the measurand.

## What the FDC2214 structurally cannot see

Verified against the datasheet (SNOSCZ5B, rev. October 2024) rather than a summary. The only
per-channel output is the 28-bit conversion result. The words "resistance" and "conductance" appear
nowhere in the electrical specification except junction-to-ambient thermal resistance; there is no Q,
no phase, and no amplitude reading. `CHx_IDRIVE` is write-only configuration, not a converged value
that could be read back as a loss proxy the way TI's LDC parts expose `Rp`. The single loss-related
output in the entire device is a **one-bit per-channel Amplitude Warning**, cleared by reading it.

So the variable that temperature moves hardest is not merely difficult for MSV3 to correct. It is
**unobservable**. It can only ever reach the host disguised as apparent capacitance. Relevant limits
for reference: `VSENSORMIN` 1.2 Vpk, `VSENSORMAX` 1.8 Vpk, `fSENSOR` 0.01-10 MHz, internal clock
temperature coefficient -13 ppm/C mean.

Note also that a soil-loss change alters oscillation amplitude at fixed `IDRIVE`, and the device
interleaves an internal Amplitude Correction phase between conversions. The reading is therefore
affected by loss through a servo we neither observe nor control.

## What this means for V4

Ranked by how much of the mechanism each one removes.

1. **Measure the loss instead of pretending it is capacitance.** Synchronous (I/Q) detection recovers
   `G` and `C` as two numbers from the same excitation. Because the dominant confound is
   Maxwell-Wagner and Maxwell-Wagner is driven by `G`, measuring `G` turns the unobservable into a
   correction input. It doubles as the EC channel the design already wants. This is now clearly the
   highest-value change.
2. **Take the electrode out of the resonator and fix the interrogation frequency.** In MSV3 the
   apparent capacitance depends on `tan d`, `tan d` depends on `w`, and `w` is set by the apparent
   capacitance. That is an implicit loop, and it is why the operating point slides with the measurand.
   Driving at a fixed frequency and reading magnitude breaks it. This is the structural difference
   from the v1.2 and it survives the correction above intact.
3. **Treat the mask thickness as a design lever.** The Maxwell-Wagner term scales with `tan^2 d`, and
   `tan d = w * R_soil * C_mask`. A thicker or lower-permittivity barrier lowers `C_mask`, lowers
   `tan d`, and suppresses the confound quadratically. It also costs moisture sensitivity, so this is
   a trade to measure, not a free win.
4. **Measure higher. This was withdrawn once and is now reinstated with numbers.** An intermediate
   revision of this document withdrew the "go higher" advice on the argument that `tan d` rises
   linearly with frequency. That argument was incomplete: `tan d` does rise, but `C_eff` collapses
   with it and the soil's own capacitance takes over the branch, so the net coefficient *falls*.
   The frequency sweep is in the section below. Third revision of this point, recorded so the
   reasoning is auditable.
5. **Drop C17's dilution only for the reasons that are real.** The 270 pF fixed capacitance attenuates
   the moisture signal and the permittivity thermal term by the same factor, so it does not by itself
   change that ratio. What it does hurt is the moisture signal's margin over the board and clock
   drift, which are not diluted.

Keep the bare-tank reference. It is working exactly as intended, it is cheap, and the measurement
above shows the board term it removes is genuinely small and genuinely linear.

### The frequency sweep, and the hard constraint it exposes

Model the probe as the mask capacitance in series with the soil bulk, now with the soil's own
capacitance in parallel with its resistance, so that permittivity and conduction are separate inputs.
`C_soil` is the moisture signal we want. `R_soil` is the confound. Selectivity is the ratio of the
reading's sensitivity to the first over the second. Using the values fitted above, 108 pF mask,
366 ohm soil, 9.7 pF soil capacitance:

| frequency | `tan d` | `C_eff` | selectivity, moisture over confound | count tempco |
|---:|---:|---:|---:|---:|
| 1.53 MHz, where the v1.2 runs | 0.38 | 93.5 pF | **0.03** | -0.068 %/C |
| 3.2 MHz, where MSV3 runs | 0.79 | 65.5 pF | **0.01** | -0.145 %/C |
| 8 MHz | 1.99 | 26.3 pF | 0.15 | -0.097 %/C |
| 15 MHz | 3.73 | 14.6 pF | 0.64 | -0.038 %/C |
| 30 MHz | 7.45 | 10.4 pF | 2.67 | -0.010 %/C |
| 60 MHz | 14.90 | 9.3 pF | **10.81** | -0.003 %/C |

**At the frequency either sensor actually runs, the reading is 25 to 100 times more sensitive to soil
conductivity than to soil permittivity.** Both generations, and the v1.2 with them, are substantially
conductivity meters. That is not a defect in our implementation. It is where these frequencies sit
relative to the soil's conduction relaxation, and it is the same reason this sensor class is
notoriously salinity-sensitive.

The relaxation frequency is geometry-free, `f = sigma/(2*pi*eps0*epsr)`, so it is set by the soil and
not by the board:

| soil state | sigma (S/m) | relaxation |
|---|---:|---:|
| dry potting mix | 0.005 | 11 MHz |
| drying, probe 5 territory | 0.020 | 24 MHz |
| moist, probe 4 territory | 0.050 | 45 MHz |
| well watered | 0.150 | 108 MHz |

This also gives a second, simpler reading of the wet/dry puzzle: probe 5's drier soil has a lower
relaxation frequency, so it sits closer to the usable regime and behaves better. Wetting the pot
pushes the relaxation *up*, away from the operating point, and the probe degrades.

**The constraint that follows is decisive for V4.** The FDC2214's maximum sensor excitation frequency
is **10 MHz** (datasheet Electrical Characteristics, `fSENSOR` 0.01-10 MHz, verified earlier in this
document). Selectivity does not become usable until roughly 30 MHz, and in a watered pot not until
100 MHz. **The part cannot be operated in the regime where this measurement works.** No choice of
inductor, C17, IDRIVE, RCOUNT or compensation moves that, because it is a ceiling on the excitation
itself. Continuing to tune the FDC2214 design is optimising inside a region the physics has already
ruled out.

## The answer, consolidated

After three corrections, the mechanism reduces to one sentence.

**Frequency modulation puts the soil inside an oscillator. An oscillator's frequency is set by a
phase-balance condition, and how tightly that condition pins the frequency is set by Q. The soil sets
Q. So the soil's temperature-driven loss has a direct, unavoidable path to the output.**

**Amplitude modulation makes the soil a passive load on a clock it cannot influence.** There is no
phase condition to satisfy, so nothing about the soil can move the excitation frequency. The 555's
frequency is set by `R2`, `R3` and `C3`, which never touch the soil.

Two loss-to-frequency terms exist in the resonant case, and both scale inversely with Q:

| tank Q | resonance damping, `1/(4Q^2)` | phase pulling at 10 deg, `dphi/2Q` | sum |
|---:|---:|---:|---:|
| 50 | 0.0002 %/C | 0.0035 %/C | 0.004 |
| 10 | 0.0050 | 0.0175 | 0.023 |
| 3 | 0.0556 | 0.0582 | 0.114 |
| **2** | **0.1250** | **0.0873** | **0.212** |

Probe 4 measures **-0.194 %/C in the frequency domain**. A tank Q of about 2, which is what a lossy
wet-soil electrode across the tank produces, predicts **0.212 %/C**. The mechanism accounts for the
observation without further fitting.

**The trap this creates is structural, not an implementation flaw.** Sensitivity requires coupling the
electrode strongly to the soil. Coupling strongly to lossy soil destroys Q. Low Q is precisely what
lets temperature in. A resonant soil probe therefore cannot be made both sensitive and thermally quiet
at these frequencies. Expensive probes escape by going to 100+ MHz, where soil is far less lossy, and
by using weak coupling with a great deal of gain. That is what the price buys.

The amplitude divider has no Q, so it has no such conflict, which is why a 50-cent part beats a
high-resolution converter at this particular job.

## Correction 3: probe 4 IS strongly thermal, and Correction 2 was a method artifact

> **Read this before Correction 2 below, which it supersedes in part.** The maintainer plotted probe
> 4's raw counts against board temperature and the anti-correlation is visible by eye. It did not
> match the near-zero correlation reported in Correction 2. The plot was right and the analysis was
> wrong.

**The method error.** Correction 2 removed a *single straight line* across a 96-hour window and then
correlated the residual against temperature. A drying curve is not a straight line. The residual was
therefore dominated by the trend's own curvature, which is uncorrelated with temperature and swamped
the real daily signal. Three days carrying watering or handling events, with 16% to 26% swings, added
more uncorrelated variance. A 24-hour boxcar high-pass tried next was worse still: it has a null at
exactly the 24-hour period, so it erased the cycle being measured.

**The correct method** is to remove each day's own straight line, then correlate within that day, and
to drop days whose swing shows a watering or handling event. Probe 4, five consecutive clean days,
2026-09-05 to 09-09:

| day | r | slope, capacitance %/C | daily swing |
|---|---:|---:|---:|
| 09-05 | -0.908 | -0.378 | 2.71% |
| 09-06 | -0.939 | -0.433 | 2.94% |
| 09-07 | -0.918 | -0.426 | 3.08% |
| 09-08 | -0.967 | -0.355 | 2.32% |
| 09-09 | -0.909 | -0.345 | 1.96% |
| **mean** | **-0.928** | **-0.387** | **2.60%** |

**Temperature explains 86% of probe 4's within-day variance**, and the slope is stable across all five
days. The claim in Correction 2 that probe 4 sits at +0.027 and is "essentially zero" is withdrawn.

**The board is not the cause.** Probe 4's own bare tank over the same five days is even more cleanly
thermal but roughly ten times smaller:

| | mean r | slope, capacitance %/C | daily swing |
|---|---:|---:|---:|
| probe 4 connected | -0.928 | **-0.387** | 2.60% |
| probe 4 bare tank | -0.994 | **-0.041** | 0.28% |

The bare tank accounts for about **10%** of the connected slope. The other 90% is in the
electrode-soil path, which is what the rest of this document is about.

**And the v1.2 really is far better.** Same days, same house, same weather, its own pot, using probe
4's die sensor as the thermal reference. Excluding 09-07, which carries an 18% watering swing:

| day | r | slope %/C | daily swing |
|---|---:|---:|---:|
| 09-05 | +0.102 | +0.008 | 1.20% |
| 09-06 | +0.446 | +0.036 | 1.37% |

**The v1.2's thermal slope is roughly 10 to 50 times smaller than probe 4's, and it is barely
correlated with temperature at all.** The maintainer's original premise, that the cheap sensor
extracts moisture with far less thermal contamination, is correct and is now measured properly.

**Probe-to-probe spread is confirmed rather than refuted.** Probe 4 sits at -0.387 %/C with r =
-0.928. Probe 8, by the same per-day method, sits at +0.064 %/C with r = +0.477. Two units of the same
design differ by six times in magnitude and in sign. That is the instability this document's mechanism
section predicts, and it is the reason a single calibration coefficient cannot work.

**What survives from Correction 2:** the C17 dilution arithmetic and the roughly 3.5x signal gain from
removing it, the absence of any datasheet capacitance floor, the per-sample noise advantage, and the
finding that probe 8's slow wander correlates at r = 0.69 with the v1.2 in the same pot. Those are
independent of this error. **What is withdrawn:** "temperature is not your main problem". For probe 4
it is emphatically the main problem.

**Methodological rule for this repo, learned the hard way.** Never assess a daily effect by removing a
single straight line across a multi-day window, and never high-pass with a boxcar whose width equals
the period of interest. Detrend per day, and exclude event days explicitly.

## Correction 2: probe 8's wobble is mostly not temperature

> **Superseded in part by Correction 3 above.** The reasoning below used a single linear detrend and
> understates the thermal term badly, fatally so for probe 4. The dilution, capacitance-floor, noise
> and same-pot-wander findings in this section stand; the "not temperature" conclusion does not.

Prompted by the maintainer asking whether to simply copy the v1.2 architecture, probe 8's detrended
signal was regressed against everything the telemetry carries. The result does not support the
premise that a soil-depth temperature sensor is the missing piece.

| explanatory variable | correlation with probe 8's detrended signal |
|---|---:|
| board temperature, unlagged | +0.163 |
| board temperature, best lag of 3 h | +0.278 |
| board temperature, best EMA time constant of 16 h | **+0.356** |
| battery millivolts | +0.094 |
| bare tank (the board term) versus temperature | **-0.978** |

The best thermal model of any lag or time constant reaches r = 0.356, so **temperature explains about
13% of probe 8's variance**. The bare tank, by contrast, is almost perfectly thermal at -0.978 and
small. The board term is understood and handled. The connected term is not primarily thermal at all.

Decomposing that variance further:

| component | RMS, % of reading |
|---|---:|
| total detrended | 0.468 |
| repeatable daily profile | 0.224 |
| non-daily wander | **0.411** |
| sample-to-sample noise | 0.008 |
| tank-phase asymmetry | 0.001 |

Two things are exonerated outright. **Tank phase is clean at 0.001%**, so the mux asymmetry warned
about elsewhere is not present on this board. **Per-sample noise is 0.0076%**, which is roughly six
times quieter than the v1.2's 0.0431%. The FDC's resolution advantage is real and is being delivered.

Only 22.8% of the detrended variance is a repeatable daily shape. The dominant term is a slow,
non-repeating wander of 0.411% RMS that is neither noise nor a daily cycle.

### Which means the v1.2's advantage is mostly signal, not stability

Set the two side by side on the same window:

| | drying signal over 96 h | detrended RMS | ratio |
|---|---:|---:|---:|
| v1.2 | 8.9% | 0.304% | 29 |
| MSV3 | 3.0% | 0.468% | 6.4 |

The v1.2 is not dramatically quieter. It produces roughly **three times more fractional moisture
signal** for the same soil, and that is most of the gap.

The cause is dilution, and the arithmetic is not in dispute. A fractional change in the electrode's
65.5 pF is scaled by its 0.195 share of the 335 pF tank, then **halved again** by the square-root
frequency readout. Net attenuation is about **10x** before the FDC ever sees it. The v1.2's divider
has no fixed capacitor in parallel and no square root, so it sees the change at full size.

**Removing C17 is therefore the highest-value cheap change, and it is nearly free.** Dropping the
270 pF leaves roughly 95 pF of electrode plus parasitics, which:

- raises the electrode's share from 0.195 to about 0.69, a **3.5x signal gain** with no new parts;
- raises the tank from 3.2 MHz to about **6.3 MHz**, which by the sweep above also improves
  selectivity roughly tenfold and stays inside the FDC's 10 MHz ceiling;
- is consistent with TI's own reference tank of 18 uH and 33 pF, and with the earlier finding that
  this project's "300 pF to 2 nF window" was never in the datasheet.

IDRIVE and tank amplitude must be re-checked after the change, and the inductor's 50 MHz
self-resonance remains comfortable at 6.3 MHz.

### There is no 300 pF floor, and TI's own example is 53 pF

Asked directly, so settled directly from SNOSCZ5B. The `CAPACITIVE SENSOR` block of the Electrical
Characteristics contains exactly two entries:

| parameter | value |
|---|---:|
| `CSENSORMAX`, maximum sensor capacitance | 250 nF |
| `CIN`, sensor pin parasitic capacitance | 4 pF |

**There is a maximum and no minimum.** TI's own worked design in section 8.2.3.1 uses an 18 uH
inductor with a **33 pF** capacitor plus 20 pF of pin, trace and wire parasitic, for **53 pF total**
at 5.15 MHz. That is one sixth of MSV3's present tank. The "300 pF to 2 nF window" is a project
belief with no datasheet basis, as the 2026-08-30 investigation also found.

The one real constraint to respect is `fREF > 4 * fSENSOR`. At the proposed 6.3 MHz that requires
25.2 MHz, and the internal clock runs 43.4 MHz typical, so a divider of 1 satisfies it with margin.

### The wander is the pot, not the instrument

The obvious worry about the decomposition above is that the 0.411% non-daily wander is MSV3 drifting.
It is not. Correlating probe 8's detrended signal against the v1.2's, both in the same pot:

| smoothing | v1.2 vs probe 8, same pot | v1.2 vs probe 4, different pot |
|---:|---:|---:|
| raw | **+0.635** | -0.549 |
| 6 h | +0.664 | -0.611 |
| 12 h | **+0.690** | -0.621 |

At 12 hours the two agree at r = 0.690, so roughly **half of probe 8's wander is demonstrably the
same physical soil behaviour the v1.2 reports**. The FDC is tracking real pot dynamics, not drifting.
Probe 4 sits in a different pot and its negative correlation is two unrelated drying trajectories,
not a finding.

### Probe 4 specifically

Re-running the same decomposition on probe 4 over this window contradicts the assumption that it is
the thermally worst probe:

| | probe 4 | probe 8 |
|---|---:|---:|
| correlation with board temperature | **+0.027** | +0.163 |
| best EMA-filtered temperature | +0.267 | +0.356 |
| variance in repeatable daily profile | **7.8%** | 22.8% |
| detrended RMS | **1.314%** | 0.468% |
| non-daily wander RMS | **1.262%** | 0.411% |
| per-sample noise | 0.0040% | 0.0076% |
| drying signal over the window | 6.0% | 3.0% |

Probe 4 is now **less** temperature-correlated than probe 8, at essentially zero, while carrying three
times the wander. Its measured -0.1174 %/C from 2026-09-04 has not reproduced in this window. That is
further evidence the coefficient is not a stable property of the probe.

Probe 4 is in its own pot, not the shared one, and it is the board with the documented water-ingress
and coating-removal history. Its excess wander is most likely that specific board's known physical
defect rather than an MSV3 architectural property. Do not generalise from it.

## Next experiment: the same electrode in both readouts

This is the one measurement that separates the two remaining explanations. The models here reproduce
probe 4's thermal coefficient well but do not reproduce the full size of the v1.2's advantage, and the
maintainer has noted that the MSV3 electrode is an interdigitated design, physically larger than the
v1.2's blade. So electrode geometry is a live confound and has to be controlled, not argued about.

**Put an MSV3 electrode into a v1.2-style amplitude front end, and stand it in the same pot as a
normal MSV3 probe.** Same geometry, same soil, same weather. Only the readout differs.

### Preferred build: transplant a v1.2 front end onto an MSV3 electrode

There are 10 spare v1.2 modules and 5 bare MSV3 probe PCBs. That makes the cleanest possible
single-variable experiment available with a knife and two wires, and it should be tried before any
breadboard replica.

The v1.2 module is one PCB: oscillator, detector and blade, joined by a narrow neck where the two
traces run down to the copper. **Cut that neck.** The top section is then a complete, known-good,
fixed-frequency amplitude front end with two floating terminals. Solder those to an MSV3 electrode's
two combs.

The result is the *actual* v1.2 electronics driving an *MSV3* electrode. Modulation scheme and
electrode geometry are separated with no chance of a build error in the front end, which a hand-built
replica cannot promise.

Read `AOUT` on the ESP32 exactly as the existing AFE rig already does. Nothing else changes.

Keep the two wires under about 5 cm. Thirty centimetres would add roughly 30 pF against an 80 pF
electrode and re-create the dilution problem the experiment is meant to study.

### Fallback circuit, if the neck cut goes wrong

```text
GPIO15 --[ R_s 1k ]--+--|>|--+-- GPIO4 (ADC1_CH3)
 1.53 MHz            |  BAT54|
 square              |       +-[ C_f 10nF ]- GND
                     |       +-[ R_b 100k ]- GND
              electrode comb A
                     |
                 ~~ soil ~~
                     |
              electrode comb B
                     |
                    GND
```

The electrode is the shunt C of an RC low-pass. More soil capacitance means more attenuation of the
square wave and a lower peak-detected DC. Same polarity as the v1.2: **lower millivolts is wetter.**

### Pins and clock, on a spare ESP32-S3

| function | pin | note |
|---|---|---|
| excitation out | GPIO15 | clear of ADC1, USB and flash pins |
| sense in | GPIO4 | ADC1_CH3, already configured in the AFE sketch |

Stay on ADC1 (GPIO1-10). ADC2 stops working once WiFi is up. No external oscillator is needed; LEDC
generates the excitation from the 80 MHz APB clock:

```cpp
ledcAttach(15, 1530000, 4);   // core 3.x: pin, frequency, resolution bits
ledcWrite(15, 8);             // 8/16 = 50% duty
```

Core 2.x uses `ledcSetup(0, 1530000, 4); ledcAttachPin(15, 0); ledcWrite(0, 8);`. The divider works
out to 3.27, which is valid. Use 1.53 MHz deliberately, to match the v1.2 exactly.

### Values

The MSV3 electrode measures about 80 pF from the FDC counts, so roughly 1300 ohm of reactance at
1.53 MHz.

| part | value | why |
|---|---|---|
| `R_s` | 1 kohm | mid-range on the divider, where sensitivity is highest; try 1.5k and 2.2k |
| diode | BAT54 or 1N5711 Schottky | **not** a 1N4148; needs low Vf and fast recovery at 1.5 MHz |
| `C_f` | 10 nF | with `R_b`, a 1 ms time constant, ~1500 cycles of smoothing |
| `R_b` | 100 kohm | bleed, so the detector can follow the soil downward |

### Three things that will invalidate the test

1. **Lead length.** 30 cm of wire adds roughly 30 pF against an 80 pF electrode, a 38% dilution and
   the same disease C17 already causes. Mount the ESP32 within a few centimetres of the blade.
2. **The TMUX.** Its drain-off capacitance is 32 pF, and driving a 3.3 V square into an *unpowered*
   board forward-biases the mux ESD diodes and part-powers the board through them. Use a bare
   unpopulated probe PCB, or pull the mux from probe 7, which is already sensor-path bricked with a
   dead TMUX1109 and costs nothing.
3. **Using a v1.2 blade.** That reintroduces the exact confound the experiment exists to remove.

### Logging

Clone `MSV2/arduino/MSV2_Moisture_AFE_Test/MSV2_Moisture_AFE_Test.ino`, set `SENSOR_ID` to an unused
number, and replace the module front end with the network above. Everything downstream already works:
it advertises in MSV2 manufacturer-data format, the receiver ingests it, and it lands in
`msv2_sensor_history` alongside everything else.

Keep the 10 second measurement cadence that rig adopted on 2026-09-07 so duty cycle does not become
another variable. Collect four or five clean days with no watering, then score it with the per-day
detrend method from Correction 3.

### Reading the result

| observation | conclusion |
|---|---|
| AM channel quiet, FDC channel swings, same electrode | modulation scheme is the cause; rebuild the front end |
| both swing together | electrode geometry is the cause; thinner mask and more area, not a new topology |
| AM quiet but its drying signal is also tiny | the divider is saturated; re-pick `R_s` before concluding anything |

## Caveats, and what would falsify this

- Sensor 3 carries no temperature channel, so its coefficient is regressed against probe 8's die
  sensor. That is sound for the shared soil path and only approximate for its own electronics.
- Sensor 2 reports a quantized value with six distinct levels over the window. Hourly averaging over
  ~4,700 samples resolves well below one step, but its trend figure is the weakest number here.
- The loss-tangent model is a lumped two-element soil; it reproduces the measured probe coefficients
  and their sign change, which is the claim being made, not an absolute calibration.
- The `R_soil ~ 366 ohm` figure is *fitted* from probe 4's coefficient, not independently measured.
  It is a plausible number that closes the arithmetic, and it is the load-bearing assumption here.

**The test that settles it, and it is cheap.** Measure the electrode branch impedance at 1-3 MHz
instead of at DC. A function generator, a known series resistor, and a two-channel scope give both
magnitude and phase from the divider ratio.

| outcome | meaning |
|---|---|
| phase near -90 deg, magnitude near 622 ohm | maintainer is right, the branch is nearly a pure capacitor and this analysis fails |
| phase -25 to -60 deg | predicted here, `tan d` is order 0.5-1, the loss path is real |
| magnitude and phase move with salinity or temperature | the confound is directly demonstrated |

Run it in air, in tap water, and in salt water. The DC probe returns >500 Mohm for all three, which
is precisely why it cannot arbitrate. Repeating the DC measurement in air is itself informative: if
air also reads >500 Mohm, that alone shows the method is blind to whatever the electrode is in.

## Sources

- [Cave Pearl Project, hacking the v1.2 sensor for frequency output](https://thecavepearlproject.org/2020/10/27/hacking-a-capacitive-soil-moisture-sensor-for-frequency-output/)
- [Capacitive soil moisture sensor theory, calibration and testing (PMC7348898)](https://pmc.ncbi.nlm.nih.gov/articles/PMC7348898/)
- [Capacitive sensor output-stage circuit analysis](https://industrialmonitordirect.com/blogs/knowledgebase/capacitive-soil-moisture-sensor-output-stage-circuit-analysis)
- [FDC2214 datasheet, SNOSCZ5B](https://www.ti.com/lit/ds/symlink/fdc2214.pdf)
- [Effect of temperature on electrical conductivity of soils, role of surface conduction](https://www.sciencedirect.com/science/article/abs/pii/S0013795223001655)
- [Frequency, electrical conductivity and temperature analysis of a low-cost capacitance soil moisture sensor](https://www.sciencedirect.com/science/article/abs/pii/S0022169408000462)
- [Maxwell-Wagner polarization under variable temperature and electrical conductivity](https://www.researchgate.net/publication/37451048_Effects_of_Maxwell-Wagner_polarization_on_soil_complex_dielectric_permittivity_under_variable_temperature_and_electrical_conductivity)
