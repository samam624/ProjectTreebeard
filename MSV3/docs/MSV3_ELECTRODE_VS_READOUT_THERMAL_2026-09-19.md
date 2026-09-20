# Electrode vs readout: the thermal attribution is not identified (2026-09-19)

**Status: one clean thermal cycle measured, then re-analysed after external review. The headline is
that this data set CANNOT identify electrode-vs-readout, for reasons that are structural rather than
a matter of collecting more of the same. The 2026-09-16 conclusion "the readout was never the
variable" is NOT SUPPORTED by the comparison it rests on, but it is not disproved either.**

Affects the rig table in `MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` lines 31-38 and 452-454, which
is marked DISPUTED rather than superseded.

> **Revision note.** A first draft of this document reported AM slopes as a percentage of stored
> `moisture` and claimed the electrode effect was *smaller* than the repository record. **That claim
> is withdrawn** -- it was an artifact of normalising by an arbitrary offset. See "The offset trap".

## Bottom line

- **Two rig labels in earlier notes were wrong.** Sensor 2 carries the **MSV2 interdigitated**
  electrode, which is large and MSV3-like, not a v1.2 electrode.
- **The within-AM comparison is the best available, but it compares SYSTEMS, not electrodes.**
  In absolute detector millivolts the blade+AM system runs **+7.72 mV/C** against the v1.2+AM
  system's **+2.45 mV/C**: a **3.16x** ratio against the in-pot reference, **6.65x** against the
  indoor one. **That the ratio moves by 2x with the choice of temperature proxy is itself evidence
  of an unresolved confound**, not of one stable electrode coefficient. The 6.65x figure is
  *consistent with the repository's documented 5.9-8.5x under the same analysis convention*; it is
  not independent confirmation of an electrode ratio.
- **No AM-vs-FDC comparison in this repository is currently meaningful**, so the readout term is
  unquantified in either direction.
- **The statistics are far weaker than the correlation coefficients suggest.** After accounting for
  residual autocorrelation the effective sample size across the 18-hour cycle is indicatively
  **n ≈ 2.4 to 7.3**, not the nominal 111. Read that as an alarm bell about generalising from one
  diurnal cycle, not as a literal count of independent observations.
- **Sensor 2 is quantization-limited** at 3 LSB of daily swing. It can support bounds, not a
  coefficient.
- **The fourth corner is live.** Probe 5 entered the pot 2026-09-19 as MSV3 blade + FDC
  (`MSV3_PROBE5_CAPLESS_FIX_2026-09-19.md`). It has no clean data yet.

## Corrected rig table

| rig | electrode | readout | power | notes |
|---|---|---|---|---|
| sensor 9 | MSV3 blade | AM detector | mains | bench rig |
| sensor 3 | v1.2 | AM detector | mains | the original AFE rig |
| sensor 2 | **MSV2 interdigitated** | MSV2 RC | battery | quantization-limited |
| probe 4 | v1.2 | MSV3 FDC | battery + solar | MSV3 blade held in air as tank padding |
| probe 5 | MSV3 blade | MSV3 FDC | **USB** | added 2026-09-19, no clean data |

## The offset trap, and why most percentages here are meaningless

The AM rigs store `moisture = 3300 - detector_mV`. **3300 is an arbitrary display constant.**
Expressing a slope as a percentage of that quantity makes the reported number depend on the constant
rather than on the hardware. The same physical measurement, normalised two ways:

| rig | slope | as % of `moisture` | as % of detector mV |
|---|---:|---:|---:|
| sensor 9 | +7.72 mV/C | **-0.523 %/C** | **+0.424 %/C** |
| sensor 3 | +2.45 mV/C | **-0.263 %/C** | **+0.103 %/C** |

Both columns are "correct" and they disagree on magnitude *and* on the ratio between the two rigs
(1.99x versus 4.12x). **Report AM results in absolute detector mV/C.** Sensors 3 and 9 share a
detector design, so mV/C at least compares like with like between them without an AM-to-FDC transfer
function. That is necessary but not sufficient -- it compares whole systems, not electrodes.

The same trap applies to the FDC in a different form: capacitance %/C is a fraction of **total tank
capacitance**, which includes fixed tank and parasitic capacitance, so it understates the electrode's
own fractional change by an unknown factor.

## Results

### The best available comparison: electrode swap within the AM readout

Same detector design, same pot, same 18-hour window. This avoids the offset trap and needs no
AM-to-FDC transfer function -- but see the limits immediately below, which stop it being clean.

| rig | electrode | slope | daily swing |
|---|---|---:|---:|
| sensor 9 | MSV3 blade | **+7.72 mV/C** | 76 mV |
| sensor 3 | v1.2 | **+2.45 mV/C** | 35 mV |

**System ratio: 3.16x** against probe 4's in-pot die temperature, **6.65x** against probe 6's indoor
board (both computed from unrounded slopes; the rounded 7.72 / 2.45 shown above displays as 3.15x).
Both rigs read **drier when warm**.

**What this does and does not say.** Defensible: *the complete blade+AM system showed 3.16x the
mV/C response of the complete v1.2+AM system against probe 4's temperature.* **Not** defensible:
*the blade electrode is 3.16x more temperature-sensitive.* "Same detector design" does not isolate
the electrode, because:

- nominally identical detectors differ in gain and component tolerance;
- the two electrodes put their detectors at **different operating points**, so if detector response
  is nonlinear the local dV/dC differs;
- different electrode and cable capacitance changes carrier loading, amplitude, Q and demodulator
  behaviour;
- detector-board temperature exposure, placement, mains coupling and local soil all differ.

The 3.16x to 6.65x swing with temperature proxy alone signals differing waveform shape, lag or
locality. **The decisive cheap test is a known-capacitance increment applied at each detector's
actual operating point** -- several increments, both directions, to get local dV/dC and
nonlinearity. **Swapping the two electrodes between the AM detector boards is even more
diagnostic**: if the ratio follows the electrode, that is exceptionally strong evidence; if it
follows the board, the electrode attribution collapses.

### Everything else, with the caveats attached

Sign convention: positive = reads wetter when warm. Probe 4 is in capacitance units, with `r`
converted alongside the slope (an earlier draft reported the slope on capacitance and `r` on counts,
which made them appear to disagree in sign).

| rig | vs probe 4 die (in pot, in sun) | vs probe 6 die (indoor desk) | daily swing |
|---|---:|---:|---:|
| sensor 9 | −0.523 %/C, r = −0.965 | −1.699 %/C, r = −0.893 | 76 mV |
| sensor 3 | −0.263 %/C, r = −0.717 | −0.406 %/C, r = −0.313 | 35 mV |
| sensor 2 | *+0.092 %/C, r = +0.495* | *+0.526 %/C, r = +0.786* | *3 LSB* |
| probe 4 | **+0.053 %/C, r = +0.676** | **+0.260 %/C, r = +0.924** | 0.95 % of tank C |

The AM percentage columns are retained only so nobody re-derives them and believes them; use the
mV/C table above.

### Resolution

| rig | span | distinct values | daily swing in LSB |
|---|---:|---:|---:|
| sensor 9 | 77 | 78 | 77 |
| sensor 3 | 43 | 44 | 37 |
| **sensor 2** | **3** | **4** | **3** |
| probe 4 | 404,724 | 1,160 | 131,576 |

Sensor 2 resolves its whole daily excursion in three integer steps. It cannot support a coefficient.
It **can** still support a bound or an ordinal/quantized fit, so treat it as low-information rather
than discarded -- but note the MSV2-electrode column is currently **unmeasured**, not
measured-as-quiet.

### Effective sample size: the statistics are much weaker than they look

Residual lag-1 autocorrelation, with `n_eff = n(1-rho)/(1+rho)`:

| rig | nominal n | residual rho | **n_eff** |
|---|---:|---:|---:|
| sensor 9 | 111 | +0.876 | **7.3** |
| sensor 3 | 111 | +0.957 | **2.4** |
| sensor 2 | 111 | +0.934 | **3.8** |
| probe 4 | 111 | +0.936 | **3.6** |

**Read these as an alarm bell, not as literal independent-observation counts.** The formula gives the
effective sample size for estimating a *mean* from an AR(1) process; applied to regression residuals,
where both predictor and response are smooth autocorrelated series, it is indicative only and not a
rigorous degree-of-freedom calculation.

What the numbers do establish: **r = -0.965 is strong evidence that sensor 9 and this temperature
proxy co-moved within this window**, and weak evidence for a *generalizable* thermal coefficient,
because the record contains one diurnal cycle. It does not establish a coefficient stable across
days, temperature causation, or a soil-temperature response.

**Consequence: day-level replication is required.** Analyse multiple cycles with the day as the unit
of replication, as the repository's own accepted method already specifies.

## Why the comparison is not identified

### 1. AM and FDC are not commensurable

AM reports detector millivolts; the FDC reports frequency counts convertible only to *total tank*
capacitance. Relating them requires the AM detector's mV-per-pF slope at its operating point, which
has never been measured, plus de-embedding of fixed tank capacitance, inductor tempco and reference
clock drift on the FDC side.

`MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` lines 452-454 compare these quantities directly, and that
table is the basis for "both modulation schemes show it, the readout was never the variable." **That
conclusion is not supported by that comparison.** It is also not disproved. The correct status is
*not identified without transfer calibration or a common physical normalisation.*

**Best available normalisation:** express each rig's thermal excursion as an *equivalent moisture
error* -- thermal drift divided by that same rig's response to a controlled moisture perturbation or
a known capacitance increment. That is dimensionless, physically meaningful, and comparable across
readouts.

### 2. There is no soil temperature measurement anywhere in this experiment

Probe 4's die temperature is the designated pot channel. Measured 2026-09-19 it tracks the indoor
board within ±0.75 C overnight, then steps **+3.75 C in one 30-minute bin at noon**, reaching +6.0 C
above room by 14:00; regression `p4 = 3.79 x p6 − 61.0`, r² = 0.858, swing 9.00 C versus 2.50 C.

It is a **mixed junction temperature** -- sunlight, ambient air, enclosure and panel geometry,
self-heating and nearby soil all contribute. Calling it an insolation sensor overstates it; the
defensible statement is that it is **not a soil temperature measurement** and is heavily
sun-influenced.

**An overcast day is not sufficient** to separate the candidates. It compresses dynamic range while
time, air temperature, soil temperature, evaporation and transpiration all remain correlated. The
interventions that actually break the degeneracy are listed under Next Steps.

### 3. Uncontrolled differences between rigs

Sharing a pot does not equalise exposure. Electrode depth, root proximity, local moisture,
electronics temperature, power source and mains coupling all differ between these five rigs. Soil
moisture itself varies diurnally through evaporation, plant uptake, vertical redistribution and
conductivity change, so "reads drier when warm" is not automatically an artifact.

### 4. The mains/USB confound is live

Sensor 3 has a documented history of cyclic noise bands traced to **mains coupling from the receiver
PSU sharing an outlet**. Sensors 3 and 9 are mains powered and probe 5 is now USB powered in the same
pot. The quantity being measured is a sub-1 % daily wobble.

## The clean window, and why it is only 18.3 hours

**2026-09-18 20:53:20 -> 2026-09-19 15:13:31 PDT.** Both ends are set by real events.

**Start.** Probe 4's history was previously recorded as splitting at 16:38. Wake-counter resets give
the true boundaries: v65566 ran 16:39:31-16:59:21 on mux code 2 and was out of FDC range; v65567
`airpad` started 17:00:10 but ran **on the bench**; the probe entered the pot at **20:53:20**,
visible as a 1.6M-count step with no wake reset. A 16:38 split yields `+9.0 %/C, r² = 0.21`.

**End: an unexplained discontinuity, most likely intervention.** At 15:13:31 -> 15:17:02 probe 4
jumped **+82,831 counts**, dropped wakes 1145-1146 (3.5 min against a steady 70 s cadence), and its
RSSI pattern shifted from −70/−78 to −63/−74, while board temperature moved 0.25 C. A full degree of
genuine thermal response is ~4,600 counts, so the step is ~18x too large to be thermal, and sensor 9
moved smoothly through the same minutes.

**What is established is that it is not a soil thermal response.** Handling is the leading
explanation but is not confirmed: a reset, configuration or mux state change, power event, connector
movement or oscillator glitch would also fit. Recorded as **unknown discontinuity, likely
intervention**. Do not pool data across it.

The airpad configuration is uniquely fragile here -- the MSV3 blade held in air *is* the tank padding
capacitance, so moving the blade, pot or jumpers moves the measurement.

## Consequences for MSV4

MSV4 is committed to a fixed-frequency AM front end and the boards were ordered 2026-09-19. **Nothing
here argues for a respin**, and the AM decision's justification -- BOM cost, fixed interrogation
frequency, firmware-settable phase and frequency -- never depended on thermal data.

The electrode redesign's rationale is **strengthened, not weakened**, by the corrected within-AM
comparison: a 3.2-6.6x electrode effect measured through a single unchanged readout is the cleanest
electrode evidence this project has, and it is consistent with the existing record.

What remains genuinely open is whether the **readout** contributes a term on top of that. If it does
and it favours the FDC, V4 would lose that benefit. Nothing currently measures it. **Do not claim V4
fixes the thermal wobble** until the readout term is quantified.

## Next steps, in priority order

Split by question, because the two threads compete for the same bench time.

### A. Electrode versus readout

1. **Calibrate each readout at its actual operating point with known capacitance increments.**
   Several increments in both directions, to get local dV/dC, linearity and repeatability. This puts
   both readouts in a **common capacitance domain** and tests the operating-point objection above.
   Note what it does *not* do on its own: converting a capacitance-domain drift into an **equivalent
   moisture error** additionally requires A7's independently measured moisture sensitivity. The chain
   is `output drift -> equivalent capacitance drift` (A1), then
   `equivalent capacitance drift -> equivalent moisture error` (A7).
2. **Swap the two AM electrodes between detector channels -- repeatedly.** Run it as an **ABBA-style
   crossover** with settling periods and logged switch times, not a single swap, so time drift and
   disturbance cannot masquerade as the effect. Do not move the buried electrodes if the swap can be
   made at the connector instead. Decision rule, three-way:
   - effect **follows the electrode** -> strong electrode evidence;
   - effect **follows the board** -> the board/readout contribution dominates *this test*; it does
     **not** prove zero electrode contribution;
   - effect **changes non-additively** -> electrode x readout interaction.
3. **Add temperature sensing at electrode depth**, ideally near more than one probe -- a single
   shared-pot temperature still misses spatial gradients.
4. **Electronics-only interventions**: shade the electronics leaving pot illumination unchanged;
   separately heat or cool the electronics with soil roughly stable. Record electronics *and* soil
   temperature. These break the room/sun/soil collinearity that an overcast day alone does not.
5. **Remove or deliberately toggle the USB/mains coupling.**
6. **Collect multi-day data and analyse days as replicates -- but note what is being replicated.**
   This is **temporal replication of these four particular systems**, not replicated hardware. There
   is still exactly **one physical unit per cell** of the 2x2, so board-specific differences remain
   confounded with electrode and readout type. Multi-day data establishes that a coefficient is
   stable for *these boards*; it cannot by itself generalise to the electrode or readout type.
   A2's crossover and A8's spare boards are what attack that confound.
7. **Calibrate physical moisture response.** A watering is *not* automatically a common known
   perturbation -- infiltration, roots and sensor position give each electrode a different water
   history. Without pot mass, controlled distribution and equilibration time, call it a
   response-to-watering experiment, not a moisture calibration.
8. **MSV4 pre-coating characterisation gate -- schedule this alongside A1/A2, not later.** Five MSV4
   boards were ordered 2026-09-19. **Before any of them are coated, potted or deployed:**
   - reserve at least one, preferably several, for precision-capacitor characterisation;
   - measure board-to-board gain, offset, nonlinearity and **electronics-only temperature drift**;
   - if connection geometry permits, test the old and new electrodes on the *same* MSV4 readout;
   - keep test points accessible until that characterisation is complete.

   This is the one item that supplies **hardware replication** rather than more temporal data, and it
   is time-limited: once the boards are coated, the MSV3 electrode/readout confounding becomes
   permanent in MSV4 as well. Conformal coating has already made SWD inaccessible on several MSV3
   probes; do not repeat that pattern before the characterisation exists.

### B. Power and solar health

1. **DMM on probe 4's cell versus `raw_battery_mv`, immediately.** Rules out a telemetry
   recalibration artifact across the 09-14 gap. Cheapest high-value item in either list.
2. **Panel-side measurement during the deployment window.** Three distinct measurements, previously
   listed twice across two documents:
   - `SP+` waveform / open-circuit behaviour, from a 20 s scope capture in sun, long enough to
     separate the 256 ms MPPT open-circuit sample from a steady level;
   - **loaded** panel current;
   - `I_SC`, **only** with the panel safely disconnected from the battery/harvester path, at morning,
     solar noon and late afternoon.

   Battery voltage is not charger current; nothing currently measures the harvester directly.
3. **Watch whether the daytime ramp returns across the `VBAT_OV` band** (~2026-09-21). Judge on the
   ramp returning, not on an exact voltage or date.
4. **Continue post-discontinuity baseline collection on probe 4** -- passive, and it should not
   outrank items 1 and 2.

### C. Lower priority

- Repeat probe 5's stationary phase scan several times; one settled reading does not exclude an
  intermittent fault.
- Give sensor 2 resolution or formally retire it.

### Explicitly not worth doing now

Block-bootstrap or AR-aware intervals (cannot manufacture day-level replication from one day), an
ordinal fit for sensor 2 (formalises weak information without making it decision-useful), and
model-based de-embedding of fixed tank capacitance (the known-capacitance calibration in A1 is more
direct and captures the real transfer chain).

## Method

Per-rig 10-minute medians, joined on nearest bin to the reference series, OLS within the window,
slopes reported in absolute detector mV/C for AM rigs and in capacitance %/C for the FDC
(`dC/C = −2 df/f`, with `r` converted alongside). Probe 4 restricted to `tank_phase` 0 (the airpad
build measures phase 0 only) and to `1e6 < raw_fdc_counts < 5e7` to reject the over-range band.
Effective sample size from residual lag-1 autocorrelation, `n_eff = n(1−rho)/(1+rho)`.

Data pulled with `cloud/scripts/fetch_supabase_history.py --raw` from receiver `MSV2088A3104A7AC`.
Claims about current data are regenerable from date-bounded queries against the live project; claims
about the deleted v65566 block are reproducible only from the checksummed backup and
`MSV3_PROBE4_HISTORY_DELETION_AUDIT_2026-09-19.md`, since those rows no longer exist server-side.

External review of the first draft by Codex (consultation `treebeard-2026-09-19-review`) identified
the offset trap, the `r` sign inconsistency and the autocorrelation problem, all three of which are
reflected above.
