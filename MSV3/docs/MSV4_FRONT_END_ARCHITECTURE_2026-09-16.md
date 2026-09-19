# MSV4 front end: software lock-in, no FDC (2026-09-16)

**Status: architecture decided. Parts verified electrically, not for price or stock. Nothing built.**

The FDC2214 comes out. V4 is a fixed-frequency amplitude front end with a single chopper switch whose
**phase and frequency are both firmware parameters** — a software lock-in amplifier. Electrode
geometry is in `MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md`.

## Why the FDC goes — the reason changed on 2026-09-16

**The thermal evidence that originally justified removing the FDC has reversed. The decision stands;
its justification does not.** Recorded in full because the superseded version is the kind of table
that gets re-cited later.

### What this section used to say, and why it is void

| | electrode | coating | readout | result |
|---|---|---|---|---|
| sensor 9 | MSV3 | uncoated | fixed-frequency AM | ~~clean~~ **wrong — see below** |
| probe 8 | MSV3 | uncoated | FDC resonant | 1.956% daily wobble |

That comparison fired `MSV3_AM_FM_BENCH_TEST_CHECKLIST_2026-09-14.md`'s decision rule — *"transplant
flat, sensor 8 swings -> modulation scheme"* — and concluded the readout was the variable.

**Sensor 9 was not clean. It had not run long enough.** Observed by the maintainer on 2026-09-16, the
hybrid rig now shows the daily thermal swing clearly, and it is the **same anti-thermal signature
seen from MSV3 in this same pot** — warm afternoon reads drier, cool early morning reads wetter.

So the corrected table is:

| | electrode | coating | readout | thermal swing |
|---|---|---|---|---|
| sensor 9 | MSV3 | uncoated | fixed-frequency AM | **yes** |
| probe 8 | MSV3 | uncoated | FDC resonant | **yes** |

**Both modulation schemes show it, through the same electrode, in the same pot. The readout was never
the variable.** The FDC, the LC tank, the oscillator and the driving circuit are all exonerated — as
the glass-of-water experiment below had already implied, and this now confirms independently.

**The cause is the electrode geometry and the soil-probe interface.** The sensor is not malfunctioning;
it is accurately reporting something that is really happening at a 0.48 mm sensing depth.

### The decision still stands, on different grounds

V4 drops the FDC anyway, for reasons that never depended on the thermal data (maintainer, 2026-09-16):

- **BOM cost.** The FDC2214 plus its LC tank and I2C pull-ups is replaced by two signal switches and
  four passives — well under a dollar. This is the dominant motivation now.
- **AM over FM for long-term deployment.** A fixed interrogation frequency does not slide with the
  measurand. `MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md`'s objection to FM — *"the
  interrogation frequency IS the output"* — is an architectural argument, not a thermal measurement,
  and it survives this reversal untouched.
- **Flexibility.** Frequency and phase become firmware parameters, so the probe can characterise its
  own impedance in situ. The FDC cannot do that.

**What must not be claimed any more:** that removing the FDC fixes the daily thermal wobble. It does
not. **The electrode redesign is the fix**, and it is now the load-bearing change in V4 rather than a
parallel improvement — see `MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md`. Going from 0.48 mm to 2.90 mm
of penetration averages interface movement over several millimetres instead of a fraction of one,
which is precisely the geometry argument that motivated it.

### A third mechanism, which neither planned test catches

The analysis so far has considered two candidates. There are three:

1. **Conductivity tempco** — ion mobility rises ~2 %/C. The salty-water test separates this.
2. **Thermally-driven moisture migration** — temperature gradients genuinely move water in the pot.
   The salty-water test separates this too.
3. **Soil-probe interface movement** — the soil physically moves against the blade as it warms,
   dries, shrinks and settles. **A glass of water has no interface to move, so no water test can see
   this one at all.** At 0.48 mm sensing depth, tens of microns of gap change is a large fractional
   change in what the field actually samples; at 2.90 mm it is diluted roughly 6x.

Mechanism 3 is the maintainer's reading and it is the only one of the three that the electrode
redesign directly addresses.

**The sign is a discriminator, and it argues against mechanism 1.** Warmer reads *drier* on this rig.
Rising ion mobility raises apparent (Maxwell-Wagner) permittivity, which should read *wetter* with
temperature — the opposite of what is observed. Mechanisms 2 and 3 both predict the observed
direction: water migrating away from the warm zone, or a gap opening as soil shrinks.

**Confirm the polarity chain before leaning on this.** It depends on the full transfer from soil
permittivity through each readout to the displayed value, and the two rigs invert differently (the
FDC's raw `DATA_CH0` falls as capacitance rises). Worth doing — it is nearly free and it would retire
one of the three candidates.

**Correction recorded 2026-09-16:** during this session it was briefly concluded that probes 4, 5 and
6 were all conformally coated, which would have confounded the above. That was wrong. **Only probe 6
is coated** (maintainer, and `fleet.json` probe 6 notes: *"Conformal-coated by 2026-09-12"*; probe 4's
notes say *"coating remain deferred"*). The error came from reading `swd_accessible: false` as
"coated" — that field means "pads reachable right now," which is also false for a probe buried in a
pot. **Do not infer coating status from `swd_accessible`.**

Consequence: **the entire AM-vs-FM thermal dataset was collected on uncoated probes**, including
probe 4's -0.387 %/C. None of it needs revisiting, and coating never contaminated it.

## The glass-of-water experiment (maintainer, 2026-09-15)

Uncoated probe, battery, by a window, full day, three media:

| medium | result |
|---|---|
| pure water | **flat** |
| air | **flat** |
| moist soil in a pot | **huge thermal wobble** (probe 4) |

Rules out the board, the FDC clock, the inductor, the battery, and temperature acting on the
electronics — all of which saw the same daily swing in all three cases. The air case is the strongest:
a probe in air tracks air temperature closely and with a large excursion, and still read flat.

**The wobble requires the soil matrix.** Air has no dissolved ions. Distilled water has almost none.
Soil pore water does, and **ion mobility rises ~2 %/C** — roughly 5x the temperature coefficient of
water's permittivity (-0.4 %/C). That conductance is "loss": current leaking through rather than
being stored and returned.

### Still to run: the salty-water test

**Put the probe in a glass of salty water by the same window**, salted until the reading lands near
what it reads in soil so the conductivity is comparable.

- **wobbles like soil** -> conductivity tempco, no soil structure required. A measurement artefact;
  the water content is not changing, only the reading.
- **flat like pure water** -> the soil *matrix* is required, pointing at **thermally-driven moisture
  migration** — temperature gradients genuinely move water inside a pot. That is not an artefact and
  no electronics change fixes it.

These demand completely different responses and nothing so far separates them. Control: confirm from
the die sensor that the probe saw a comparable temperature swing in all media — water's thermal mass
is large, so "flat in water" may partly mean "the water barely moved."

## Architecture: one switch, phase swept in firmware

```
  PWM  ──[ Rs ]──┬──── 1D+ ────────┐ U1 ch1     D+ ───────── electrode A
   (f, phase θ)  │     2D+ ── GND ─┤
                 │     1D− ── GND ─┤ U1 ch2     D− ───────── electrode B
                 │     2D− ────────┤
                 │                  S  ← GPIO  (A/B select)
                 │                  OE ← GPIO  (float both = OPEN reference)
                 │
            SENSE NODE
                 │
                 ├──── U2 ch1 ───[ 100k ]──┬──── ADC1     (measurement)
                 │     chopper,            │
                 │     select at f, θ    [ 1nF ]
                 │                         │
                 │                        GND
                 │
  drive node ────┴──── U2 ch2 ───[ 100k ]──┬──── ADC2     (drift monitor)
  (before Rs)                              │
                                        [ 1nF ]
                                           │
                                          GND
```

**The sense node and the drive node are the same point** — the junction of Rs, U1's inputs and the
chopper tap. The mux routes that node to whichever electrode is currently hot.

### Why a swept single switch beats fixed I/Q hardware

The dominant error in a fixed-quadrature demodulator is **I/Q gain and phase mismatch** — separate
switches, RCs and ADC channels, each drifting independently. Sweeping phase on **one** switch
eliminates it structurally: the "channels" are the same hardware sampled at different times, so there
is nothing to mismatch and no calibration reference is needed to correct it.

At LO phase θ the measurement is `V(θ) = P + A·cos(θ − φ)`. Three unknowns, so any N >= 3 phases
recovers pedestal, amplitude and phase. Four phases at 0/90/180/270 collapse to exactly the
four-switch result:

```
P = (V0+V1+V2+V3)/4
I = (V0−V2)/2 = A·cos φ
Q = (V1−V3)/2 = A·sin φ
```

**The pedestal falls out of the fit**, so the balanced/complementary chopper branch is unnecessary
too. Take 8 or 16 phases occasionally and the **least-squares residual becomes a health check** —
large residual means harmonics, nonlinearity or drift. A two-point I/Q measurement has no residual
and no way to know it is wrong.

Cost is settle time x N. At 4 phases that is ~12 ms, about +8% on the measured 3.03 mAh/day. Run 4
routinely, 16 as an occasional diagnostic.

### One phase or four is a firmware decision

Take a single phase and it is the fixed-frequency AM detector that sensor 9 proved works. Take four
and it is an IQ demodulator. **Same board.** The IQ question does not have to be settled before
layout — which is the right way to defer it, given the salty-water test has not run.

## Operating frequency: 1–3 MHz, and do NOT auto-track

**Frequency is also a firmware parameter**, so the probe can measure its own impedance versus
frequency, in situ, in the real pot, across a wetting cycle. That replaces the LCR/VNA sweep every
analysis in these docs has been waiting on — the relaxation frequency, the optimal operating point,
and the real selectivity all become measured rather than fitted.

**Warning: sweep to characterise, then lock.** If firmware auto-tunes frequency to keep the divider
mid-range as the soil wets, it rebuilds the exact failure that killed the FDC —
`MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md`: *"In both FM architectures the
interrogation frequency IS the output, so the measurement condition slides with the measurand."*
Frequency is a commissioning and diagnostic parameter, not a runtime control loop. Pick it per probe,
record it, hold it.

**Caveat on using it as a spectrometer:** a square-wave LO responds at f, 3f/3, 5f/5 …, so every
reading is a weighted sum over odd harmonics. Fine for mapping the shape of the dispersion and
finding where phase turns over; not fine for quoting a permittivity at a stated frequency without
correcting for it.

### Why 1–3 MHz

Two independent constraints land in the same window.

**Switch timing** (below) caps the chopper at 1–2 MHz.

**Electrode impedance** — 80 pF dry to ~200 pF wet:

| freq | \|Z\| dry -> wet |
|---:|---|
| 1.5 MHz | 1300 -> 530 ohm |
| 3 MHz | 663 -> 265 ohm |
| 8 MHz | 249 -> 100 ohm |

At 8 MHz the mux R_on, GPIO output impedance and trace parasitics all start to matter. At 1.5–3 MHz
the divider sits in a comfortable range. The v1.2 runs at 1.53 MHz.

The only argument for 8+ MHz was the selectivity sweep, which rested entirely on the fitted
`R_soil`/`C_soil`. **Build for 1–3 MHz and measure selectivity rather than deriving it.**

## Generating f and theta on the nRF52840

**Decided 2026-09-16: V4 stays on the nRF52840 / E73 module.** The low-power architecture is the
whole reason this project exists on that part, and the sleep budget (10-15 uA, 528 uW average) is not
reproducible on an ESP32. Earlier drafts of this document specified ESP32 `LEDC` with phase set via
`hpoint`; that is replaced by the following, which is at least as good.

**TIMER + PPI + GPIOTE, one counter, two output pins.** One TIMER runs free at 16 MHz with a
compare shorted to CLEAR as the period wrap. Four compare values toggle two GPIOTE channels through
PPI: two edges make the excitation square wave, two more make the chopper select at an arbitrary
offset. Both waveforms come off the **same counter**, so their phase relationship is exact and
jitter-free by construction — better than two independent LEDC channels, not worse.

```
TIMER (16 MHz, N ticks/period)
  CC[3] = N       -> SHORTS: CLEAR (period wrap)   + PPI -> GPIOTE[0].OUT   (excitation toggle)
  CC[0] = N/2                                      + PPI -> GPIOTE[0].OUT   (excitation toggle)
  CC[1] = k                                        + PPI -> GPIOTE[1].OUT   (chopper toggle)
  CC[2] = k + N/2                                  + PPI -> GPIOTE[1].OUT   (chopper toggle)

  phase theta = k * 360/N degrees
```

Four CC registers and four PPI channels, so **TIMER1 or TIMER2 is enough**; TIMER3/TIMER4 have six
CCs if headroom is wanted. Avoid TIMER0 — the SoftDevice reserves it. Check the S140 resource table
for the reserved PPI channels before allocating.

Frequency grid is `f = 16 MHz / N`, phase step is `360/N`:

| N | frequency | phase steps | step | exact quadrature |
|---:|---:|---:|---:|---|
| 8 | 2.000 MHz | 8 | 45 deg | yes |
| 12 | 1.333 MHz | 12 | 30 deg | yes |
| **16** | **1.000 MHz** | **16** | **22.5 deg** | **yes** |
| 20 | 800 kHz | 20 | 18 deg | yes |
| 32 | 500 kHz | 32 | 11.25 deg | yes |
| 64 | 250 kHz | 64 | 5.6 deg | yes |

**Operating point: N = 16, f = 1.000 MHz.** Three constraints land on it at once — the four routine
phases 0/90/180/270 are ticks 0/4/8/12, *exact* with no rounding; the occasional 16-phase diagnostic
sweep this document asks for is exactly the full grid; and 1 MHz is where the TS3USB221A's 18 ns
t_ON/t_OFF asymmetry costs the least (6.5 deg, versus 9.9 deg at 1.53 MHz). Electrode |Z| at 1 MHz is
~1990 -> ~795 ohm dry to wet (the table above is at 1.5 MHz) — higher than the 1.5 MHz figures and
still a sane divider range, but it is the one thing 1 MHz costs, and it is what sets Rs.

**The coarser grid is not a coarser measurement.** The nRF's 16 MHz timebase gives 5x fewer phase
steps than LEDC's 80 MHz at the same frequency, which looks like a loss and is not. `V(theta) =
P + A*cos(theta - phi)` is *fitted*; the precision of the recovered `phi` is set by the SNR and the
number of points, not by the step size. Sixteen exact points at 22.5 deg recover phase far finer than
22.5 deg. Step size would only matter if a single phase had to be placed at an arbitrary angle, which
this architecture never needs — quadrature is exact on any N divisible by 4.

**Run the excitation off HFXO, never HFINT.** Excitation and chopper share one clock, so a clock
error does *not* corrupt the phase relationship — but it does move the interrogation frequency, and
the internal RC is +/-6 % with a temperature coefficient. A frequency that wobbles with temperature,
against a soil impedance that is frequency-dependent, injects a thermal artefact — the same class of
error as `MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md`'s *"the interrogation frequency IS
the output."* The E73's 32 MHz crystal is already there. Use it, and treat this as a hard rule rather
than a default.

**ADC: SAADC, PPI-triggered from the same TIMER**, so the settle delay between setting a phase and
sampling is deterministic rather than software-timed. Oversample heavily (up to 256x in hardware) —
the post-chopper node is a slow DC level behind a 100 k / 1 nF filter, so there is nothing to lose
and the SAADC needs the averaging. Run `CALIBRATEOFFSET` at wake; the drift-monitor channel covers
the rest.

**Implementation gotchas:**

- `k + N/2 >= N` wraps the chopper's second edge past the period boundary and inverts the set/clear
  ordering. A phase beyond 180 deg is the sub-180 deg waveform inverted, so firmware can fold it
  rather than special-case it.
- Avoid a compare value of 0. Whether `COMPARE[0]` fires immediately after a CLEAR task is not worth
  relying on; the scheme above puts the wrap compare itself on the toggle instead.
- The whole chain runs on PPI with the CPU asleep. HFCLK must be on for the measurement window
  (~12 ms at 4 phases), which is already inside the measured active budget.

## Switch selection

**R_on and capacitance trade directly** — low R_on means a physically large FET, and a large FET means
large capacitance. Verified from datasheets this session:

| part | R_on | C_off | C_on | R x C |
|---|---:|---:|---:|---:|
| TS3A24159 (dual SPDT, 0.3 ohm) | 0.3 | 90 pF | **224 pF** | 67 |
| TS3A44159 (quad SPDT) | ~1 | 52 pF | **164 pF** | — |
| TMUX1109 (MSV3's current) | 8.75 | 32 pF | 38 pF | 333 |
| **TS3USB221A** | 3 | **3.5 pF** | **6 pF** | **18** |

**Deliberately buy a higher-R_on part.** R_on costs nothing here — in the A/B swap it is in series
with a several-hundred-ohm Rs; in the chopper it is in series with 100k. Capacitance is the only
figure that matters, and the family to shop is **USB / HDMI / DisplayPort signal switches**, not
"precision analog mux" or "low-Ron switch."

### TS3USB221A is a DPDT, not an SPDT

Pinout verified: `D+`(8) and `D−`(7) are two independent commons; `1D+/1D−`(1,2) and `2D+/2D−`(3,4) are
the two positions; `S`(9) selects both channels together; `OE`(6) disables both.

**One part does the entire A/B swap on one GPIO**, wired crossed as in the diagram. `OE` gives a third
state — both electrodes floating — which is a free **OPEN reference** and also isolates the electrode
during sleep, killing the parked-mux leakage path the firmware currently works around (`setMux(
MUX_SLEEP_CODE)`, the ~33 uA EC-check leak).

**The two channels share one select pin.** So U2 cannot be chopper-plus-slow-reference-selector; both
of its channels chop together. That is why the drift monitor is the drive node rather than a switched
reference impedance.

### The chopper frequency ceiling — the important finding

TS3USB221A switching characteristics at VCC = 3.3 V: **t_ON (S to D) = 30 ns, t_OFF (S to D) = 12 ns.**

| freq | on-window | t_ON as % | phase error from the 18 ns asymmetry |
|---:|---:|---:|---:|
| 1.00 MHz | 500 ns | 6% | 6.5 deg |
| **1.53 MHz** | 327 ns | 9% | **9.9 deg** |
| 3.00 MHz | 167 ns | 18% | 19.4 deg |
| 5.00 MHz | 100 ns | 30% | 32.4 deg |
| 8.00 MHz | 62 ns | **48%** | **51.8 deg** |

The **30 ns fixed delay is harmless** — a constant phase offset, calibrated out. The **18 ns asymmetry
(t_ON != t_OFF) is the limit**: it shortens the on-window relative to the select window, and both
times drift with temperature and VDD, so the error moves. A temperature-varying phase error is
precisely what corrupts a loss measurement.

**Usable as a chopper to roughly 1–2 MHz. Not 8.** The A/B swap part is unaffected — it toggles once
per 10 s.

**Datasheet trap worth recording:** the headline *"900 MHz bandwidth"* is the **signal** path, not the
select path. The analog channel passes 8 MHz easily; the control input cannot be driven that fast.
Reading the bandwidth figure and concluding the part is fast enough is the natural mistake.

If a higher chopper frequency is genuinely needed later, verify a candidate's t_ON/t_OFF from its
datasheet rather than assuming — 30 ns was slower than expected and this class of part varies widely.

## Topology details

**One Rs, shared between both swap states.** Because the mux routes the *node* to whichever electrode
is hot, the same resistor is in circuit either way, so any error in Rs is perfectly common-mode across
the swap. That matters because the A/B swap is also the self-consistency check (probe 8 records 0.008%
phase difference between phases). With separate Rs_A and Rs_B their mismatch would be
indistinguishable from a real asymmetry. **A difference between swap states is then always something
real.**

**Mux cost in the measurement:** R_on ~3 ohm per channel (~6 ohm through both) in series with the
electrode, ~2% and fixed. C_off ~3.5 pF from each electrode common to the grounded throw — that is the
parallel dilution, so ~3.5 pF per electrode.

**The drive-node drift monitor.** U2 ch2 chops the drive node (tapped *before* Rs, a low-impedance
point) through its own RC into a second ADC channel. It must be **chopped, not DC-read** — the point is
that it traverses the identical chain, so drift is common.

Read it at **quadrature (90 or 270 deg), where it sits at exactly VDD/2** — mid-scale, no attenuator
needed. The chopped drive node is a *triangle* versus phase: 3.3 V at 0 deg, 1.65 V at 90 deg, 0 V at
180 deg. It rails at the in-phase points, so a full sweep of it *would* need a 2:1 tap divider; a
single quadrature reading does not. Leave the divider footprint, do not populate.

It catches VDD drift, ADC gain and reference drift, and duty/phase error in the excitation output. It does
not catch chopper R_on or RC value drift, because the averaging filter has unity DC gain regardless of
either — neither of which has ever been a problem here.

**Prior art on whether this is needed at all:** MSV3's bare-tank reference was abandoned because
*"bare-reference normalization removed only 11.7% of the final day's daily harmonic"*
(`MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md`). The board term is 0.139% against a 1.956% wobble
and per-sample noise is 0.0076%. The new monitor corrects a different set of errors (the demod chain,
not the tank) but they are already an order of magnitude below what is being chased. **Lay out the
pads and the track; do not populate initially.**

## Bill of materials, front end

| ref | part | role |
|---|---|---|
| U1 | TS3USB221A | A/B electrode swap (both channels, one select) + OPEN via OE |
| U2 | TS3USB221A | chopper (ch1) + drive-node drift monitor (ch2, unpopulated initially) |
| Rs | 1 off, value set at commissioning | divider series resistor, shared across swap states |
| R,C | 2 x 100k + 2 x 1nF | post-chopper averaging filters |

GPIOs: excitation + chopper select (both from one TIMER via PPI/GPIOTE), A/B select, U1 `OE`, U2 `OE`.
ADC: 2 channels (measurement, drift monitor).

## Open

- [ ] **Salty-water test.** Separates conductivity tempco from thermally-driven moisture migration.
      Highest-value experiment outstanding; needs a glass, salt and a day.
- [x] Confirm sensor 9 and probe 8 are seeing comparable conditions. **Done 2026-09-16, and it
      reversed the conclusion** — sensor 9 shows the same anti-thermal swing. See the corrected table
      at the top of this document.
- [x] Work out the polarity chain on both rigs and check the sign of the thermal coefficient against
      each mechanism. **Code/mapping sign audit done 2026-09-18 — see "Polarity chain, measured" below.**
      Physical attribution stays OPEN: the post-09-14 slopes use room-temperature proxies, and the
      FDC loss-sign question needs a controlled bench test.
- [ ] Mechanism 3 (interface movement) is untestable in a glass. If it needs separating from
      mechanism 2, the test is mechanical, not chemical — e.g. the same probe in the same pot at two
      insertion depths, or a probe in a medium that cannot shrink.
- [ ] Price and stock on TS3USB221A; only electricals have been verified.
- [ ] Once built: sweep frequency and measure selectivity, relaxation frequency and the optimal
      operating point on a real electrode in a real pot. Replaces every fitted parameter in these docs.
- [ ] Pick Rs at commissioning from the measured |Z| at the chosen frequency.

## Polarity chain, measured (2026-09-18)

**Algebra, from the code, per readout.** On every rig a rise in soil conductivity reads *wetter*:

| rig | raw quantity | wetter means | conductivity rises -> |
|---|---|---|---|
| MSV3 FDC (probes 4, 8) | `DATA_CH0` ∝ f = 1/(2π√LC) | C up -> counts **down**; receiver `moisture = (ref − counts)/k` | Maxwell-Wagner raises C_eff -> counts down -> **wetter** |
| AM divider (sensor 3 = v1.2 electrode, sensor 9 = MSV3 electrode) | detector mV (`raw_moisture_khz` field) | pad \|Z\| down -> mV **down** (`MSV2_Moisture_AFE_Test.ino`: "lower mV = wetter"; receiver inverts `ceiling − raw`) | G up -> \|Z\| down -> mV down -> **wetter** |

**The thermal doc's damping sign is disputed, not settled.** `MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY`
lists the resonant-damping `1/(4Q²)` term as pushing counts *up*. For an ideal zero-phase oscillator
on a parallel-loss tank the frequency does not move with loss, and series loss lowers it, which
would make the term ~0 or negative. The FDC2214's sustaining loop is not an ideal zero-phase
oscillator, so treat the direction as OPEN until a controlled bench test (tank + known parallel R,
swept) measures it. The "conductivity reads wetter" column below is a first-order model on that basis.

**Measured, per-day detrended (repo rule), cloud data pulled 2026-09-18.** Shared Alocasia pot. The
pot lost its only temperature channel on 2026-09-14 (probe 8 went to the C17-removal diagnostic
image, which the receiver ignores), so 09-15 onward uses probe 6's and probe 7's die temperatures
as room proxies. Sign is robust; slopes against a proxy are indicative only.

Correlations are descriptive: 10-min medians are autocorrelated, so no significance is claimed.

| series | days | r (reading vs temp) | slope, % of reading per °C | reads when warm |
|---|---|---|---|---|
| sensor 3, v1.2 electrode, AM | 09-12..09-18 (09-14 watering excluded) | +0.55 .. +0.93 | +0.02 .. +0.15 | **drier** |
| sensor 9, MSV3 electrode, AM | 09-15..09-18 | +0.31 .. +0.90 | +0.14 .. +1.26 | **drier** |
| probe 8, MSV3 electrode, FDC | 09-12..09-13 (probe 8 temp) | +0.35 .. +0.51 | +0.03 .. +0.04 (counts) | **drier** |

Conclusions:
1. Directly supported: in the shared pot every rig's raw reading moved toward *drier* as
   temperature rose. Under the first-order model that is the opposite of the conductivity sign, so
   conductivity is unlikely to be the dominant term there. Provisional for 09-15 onward (room proxy).
2. On the same days against the same temperature reference, sensor 9's observed coefficient was
   **5.9-8.5x** sensor 3's (probe 6 ref: 6.0, 7.8, 6.7, 8.5; probe 7 ref: 5.9, 7.3). Both are AM
   front ends of the same design in the same soil, so the electrode is the leading suspect for the
   magnitude, consistent with the 0.48 mm sensing-depth argument. The two AM builds are separate
   boards, so an electronics difference is not fully excluded.
3. Probe 4's own pot reads *wetter* when warm (r ≈ −0.93 on counts): the conductivity-consistent
   sign, not proof of it. Dielectric, geometry and thermal-lag effects remain alternatives. The pots
   behave differently, so a single compensation coefficient cannot cover both.

Open: the salty-water test still separates migration from interface movement. The shared pot's
temperature channel is **back since 2026-09-18 evening**: probe 4, the inverted-hybrid rig, now sits
in that pot and reports its board die temperature. Re-score sensors 3 and 9 against probe 4's
temperature once a few clean days accrue, replacing the room-proxy slopes above.
Scoring script: per-day linear detrend, 10-min medians, days with a >5 % bin-to-bin step excluded.
