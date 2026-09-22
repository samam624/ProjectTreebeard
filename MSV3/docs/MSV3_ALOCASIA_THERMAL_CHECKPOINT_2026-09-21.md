# Alocasia pot thermal checkpoint — 2026-09-21

Initial data end: **2026-09-21 17:15 PDT** (2026-09-22 00:15 UTC); a probe 7
follow-up below extends probes 4, 5 and 7 to **18:15 PDT**, and the PC-state
correction includes probe 5 through **18:28 PDT**. The maintainer confirmed on
2026-09-21 that the Alocasia pot, its sensors, electrodes, and power connections had not
changed since the 2026-09-19 fleet verification. The current physical state is recorded in
[`MSV3/fleet.json`](../fleet.json); dated documents below are historical evidence.

## Current setup and immediate reading

| ID | Electrode in this pot | Readout | Power | Insertion |
|---|---|---|---|---|
| 2 | MSV2 interdigitated | MSV2 RC | battery | throughout |
| 3 | v1.2 | AM detector | ESP32, shared outlet | throughout |
| 9 | MSV3 blade | AM detector | ESP32, shared outlet | 09-14 ~18:30 PDT |
| 4 | v1.2 wired to FDC tank; MSV3 blade held in air as tank padding | FDC2214 | battery + solar | 09-18 20:53 PDT |
| 5 | MSV3 blade | FDC2214 | USB from computer, no battery | 09-19 ~20:24 PDT |

All five still report. At matched **17:00 PDT** ten-minute bins on 09-20 and 09-21,
the older AM rigs moved drier: sensor 3 detector output **2401.5 → 2415.5 mV** and
sensor 9 **1876 → 1897 mV**. The FDC counts also rose, which means lower total tank
capacitance: probe 4 **37.030 → 37.187 million** (about **−0.84%** in total tank
capacitance) and probe 5 **35.881 → 36.323 million** (about **−2.42%**). Sensor 2
changed **174 → 173**, just one integer step. The common drier direction supports the
maintainer's observation that watering is due. These raw scales are neither calibrated
volumetric water content nor comparable to one another. Probe 5's large change includes
post-insertion equilibration and should not be treated as a pure pot dry-down.

**Water the plant when needed.** The experiment has already yielded one complete
post-insertion day and most of a second for probe 5. Record the watering time and amount
(and pot mass if practical), leave sensor positions unchanged, and continue logging through
the response and subsequent dry-down. The watering day must be excluded from thermal
coefficient fits. A known watering event will also test whether each channel registers a
real moisture change, which the present dry-run cannot establish.

## Thermal measurements

Cloud raw history was fetched read-only for sensors 2, 3, 4, 5, 6, and 9 from receiver
`MSV2088A3104A7AC`; the ignored local snapshots are
`data/sensor_logs/treebeard_alocasia_s<ID>_2026-09-21.csv`. Reproduce the numerical
analysis with `MSV3/scripts/analyze_alocasia_thermal.py data/sensor_logs`. The script
rejects probe 5's scale-2 rows before its capless firmware fix, uses FDC phase 0 only,
forms ten-minute medians, then fits each local day with a linear time term and a die
temperature term. The table uses probe 4's in-pot, sun-influenced **board die** as the
common temperature proxy. For FDC, the slope is total tank capacitance percent per °C,
derived from `C ∝ 1/count²`; for AM it is absolute detector mV per °C. Positive AM
means drier when warm; positive FDC capacitance means wetter when warm.

| Rig | 09-20 full day, 21.25–26.50 °C | 09-21 to 17:10, 21.00–28.50 °C | Reading |
|---|---:|---:|---|
| sensor 3, v1.2 + AM | **+1.45 mV/°C** | **+1.38 mV/°C** | drier when warm; repeatable in these two days |
| sensor 9, blade + AM | **+5.71 mV/°C** | **+10.10 mV/°C** | drier when warm; about **3.9×** and **7.3×** sensor 3's observed slope |
| probe 4, v1.2 + FDC hybrid | **+0.084%/°C** | **+0.065%/°C** | slightly wetter when warm, despite the overall drying trend |
| probe 5, blade + FDC | **−0.673%/°C** | **−0.054%/°C** | first figure is confounded by rapid insertion settling; second is a small drier-when-warm response |
| sensor 2, MSV2 RC | 1 LSB whole-day span | 2 LSB partial-day span | quantization limited; no defensible thermal coefficient |

**Trend sensitivity:** replacing the linear time term with a quadratic leaves the sign of
the 09-21 responses unchanged: sensor 3 **+1.51 mV/°C**, sensor 9 **+6.72 mV/°C**,
probe 4 **+0.085%/°C**, probe 5 **−0.085%/°C**. It moves the magnitude substantially
for sensor 9 and probe 5. The 09-20 probe 5 count climbed from **33.405 million at
00:00** to **36.086 million at 22:00**, about a **14.3%** drop in inferred tank
capacitance between those endpoint samples;
the thermal slope changes from −0.673 to −0.237%/°C merely by changing
the time-trend shape. Do not use that day to estimate its temperature coefficient.

## What this says about the factors

The architectures expose different thermal paths. The **MSV2 RC** signal depends
on the electrode-loaded charge/discharge timing, so temperature changes in soil
impedance and circuit timing can move the count; its current 1–2 LSB daily span
prevents separating them. The **AM detector** holds excitation near a fixed
frequency and converts electrode loading to voltage; detector gain and supply
are additional thermal paths. The **FDC** measures an LC tank frequency, so its
inductor, clock, board parasitics and any connected electrode all contribute.
The MSV3 blade's soil interface, wetness and contact can change that response
substantially. These are physical mechanisms and candidate explanations, not
coefficients measured independently by this run. The historical
[`AM versus FM analysis`](MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md)
describes the circuits and their earlier pot tests; its reported MSV2 correlation
does not overcome the present quantization limit.

The **within-AM comparison** is the strongest current electrode contrast: same pot,
same days, same detector design and shared outlet, with the blade system repeatedly
showing a larger warm/dry response than the v1.2 system. It is a comparison of whole
rigs, not an intrinsic electrode coefficient. Their detector gains, cable/loading,
operating points, local soil contact and temperature exposure may differ. The changing
ratio (3.9× to 7.3× under the linear fit) argues against assigning either electrode
one permanent coefficient. This extends the 09-17..09-19 result in
[`MSV3_THERMAL_DECOMPOSITION_2026-09-19.md`](MSV3_THERMAL_DECOMPOSITION_2026-09-19.md),
whose cross-pot FDC electrode subtraction was correctly withdrawn.

The **within-FDC comparison** is now in the same pot and on the same days. Probe 4
has a small, repeatable wetter-when-warm term. Probe 5's 09-21 response is small and
opposite in sign, rather than the strong drier-when-warm −0.387%/°C predicted from
probe 4's *former, different pot* blade run. Probe 5 is only about 45 hours past
insertion and still drifting. Probe 5 is on USB while probe 4 is battery isolated,
but the PC-state voltage bands below do not establish a corresponding FDC effect. This is
evidence that the historical blade coefficient does **not transfer directly** to the
new installation; it is not yet proof that the blade lacks a thermal term. The 09-20
vs 09-21 discrepancy demonstrates why waiting for settled days matters. Probe 4's
air-held MSV3 blade also contributes tank padding, so its response cannot be labeled
"v1.2 electrode alone."
Probe 8 previously measured **+0.064%/°C** with a blade and FDC in this pot, but on
a different board and earlier moisture state; that opposite sign also cautions
against transferring one blade coefficient between installations.

The **readout term** is independently real: the older bare-tank, electrode-disconnected
measurement in the thermal decomposition document was **−0.041%/°C** with
`r = −0.994`. It measured that particular tank configuration, not a universal correction
to subtract from today's hybrid. The maintainer's two-factor model (readout and
electrode each contributing noise and temperature response) remains a useful physical
hypothesis. Today's data do not identify additive coefficients, because AM mV and FDC
counts lack a common transfer calibration and the four rigs differ in power and
electronics. The September 19 analysis documents the
[`AM/FDC units and temperature-proxy limits`](MSV3_ELECTRODE_VS_READOUT_THERMAL_2026-09-19.md).

There is **no soil-depth temperature channel**. Probe 4's die reached 28.5 °C on
09-21 while the indoor probe 6 reached 24 °C; sunlight on the head is part of the
proxy. Diurnal soil redistribution, plant uptake, evaporation, electrode contact and
temperature are correlated. Correlations of ten-minute bins cannot be treated as
hundreds of independent replications. A controlled detector capacitance increment,
matched power for probe 5, a soil thermistor, and an electrode crossover are the
next tests that can separate the factors. The older
[`AM versus FM selectivity analysis`](MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md)
and [`MSV4 front-end architecture note`](MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md)
offer physical mechanisms, but their numerical cross-rig percentages are historical,
pot-dependent, and do not supersede the current same-pot observations.

## Probe 4 power side observation

Probe 4's battery reading fell from **3971 mV at 09-20 00:00** to **3881 mV at
09-21 16:00**. There is still no obvious daytime charging ramp in this window.
That does **not** distinguish a failed harvester from the reworked BQ25504 remaining
above its wide restart band. As the
[`solar charging state note`](MSV3_SOLAR_CHARGING_STATE_2026-09-19.md) explains,
the nominal overvoltage threshold is 3.887 V, but resistor tolerance and hysteresis
put restart roughly anywhere from 3.768 to 3.919 V. Watch whether a daytime ramp
returns over the next days; one voltage crossing is not the test. Probe 5's
4290–4380 mV report is the battery-mV telemetry field with no battery attached.

## Follow-up: probe 7 and the September 21 cooling segment

The maintainer re-verified on 2026-09-21 that probe 7 is still a **blade + FDC**
in the **Begonia** pot on the **same shelf**, powered by battery and solar. It has
been deployed there since 09-16. Its six-day raw snapshot is
`data/sensor_logs/treebeard_compare_s7_6d_2026-09-21.csv`; matching snapshots
for probes 4 and 5 end around 18:15 PDT. Run
`MSV3/scripts/analyze_alocasia_thermal.py data/sensor_logs --probe7-comparison`
to reproduce the values below. As above, only phase 0 and ten-minute medians
enter the fits; each day gets its own linear time trend.

| Probe 7 local day | Die temperature range | Total tank C span | Slope versus its own die |
|---|---:|---:|---:|
| 09-17 | 18.75–29.00 °C | 14.27% | +1.019%/°C |
| 09-18 | 19.75–32.50 °C | 15.93% | +0.903%/°C |
| 09-19 | 19.00–28.75 °C | 14.32% | +1.142%/°C |
| 09-20 | 20.00–26.25 °C | 9.57% | +0.789%/°C |
| 09-21 to 18:15 | 19.75–28.50 °C | 8.92% | +0.970%/°C |

All five days have the **same, large wetter-when-warm sign**. Quadratic time
trends preserve that sign and give +0.82 to +1.36%/°C. Against a **common probe 4
die-temperature reference on the same two days**, probe 7 was **+0.858 and
+1.032%/°C**, whereas probe 4 was **+0.084 and +0.070%/°C**. That is about **10×
and 15×** as large in total tank capacitance response. This is compelling
evidence that the FDC board alone does not impose a large daily swing: probe 4
uses an FDC and stays comparatively flat. It is consistent with the MSV3 blade
and its **soil interface** dominating these installations. Probe 7 is in a
different pot, so the numeric ratios do not isolate the blade material or
geometry from Begonia soil, moisture, contact, coating and placement.

Probe 5's newest segment is real enough to take seriously. From **15:00 to
18:00 PDT on 09-21**, its die cooled **2.75 °C** while its phase-0 FDC count
fell, corresponding to a **+1.23%** rise in inferred tank capacitance. This
reversed its previous two-day count rise and is the **wetter-when-cooler**
movement the maintainer noticed. The wake counter progressed, and phase-0/1
hourly medians stayed within about **5,200 counts** of each other, far below
the 1.2-million-count analog-path alarm. A mux phase fault is not apparent.
Over the same interval probe 4 moved **−0.58%** in tank capacitance and probe 7
**−3.22%** as they cooled. Thus **probe 5 and probe 7 have opposite signs**
despite both being blade + FDC. Probe 5's young insertion, soil contact and
local water state remain plausible causes. Its cooling segment supports a
temperature-linked response, but does not yet isolate an intrinsic blade coefficient.

## Correction: probe 5's two voltage bands follow PC sleep

The maintainer identified the cause of probe 5's high battery-chart band on
2026-09-21: **the computer is asleep when the reported voltage is high, and on
when it is low**. Probe 5 is USB powered with **no battery attached**; this is
the `raw_battery_mv` telemetry field, not cell charge and discharge. The cloud snapshot through **18:28 PDT** is
`data/sensor_logs/treebeard_pcstate_s5_2026-09-21.csv` (with probe 4 as a
comparison in the matching `s4` file). For capless scale-3 rows, the low band
has median **4298 mV** (5th–95th percentile **4288–4308 mV**, 1,075 rows) and
the high band median **4377 mV** (**4367–4389 mV**, 1,269 rows).

The 10-minute median band transitions occurred at 09-20 **00:30, 10:10,
13:10, 17:10** and 09-21 **01:10, 10:40, 15:20, 16:50 PDT**. Their FDC-count
changes do **not** have one repeatable sign or a matching step in FDC counts.
For example, at 09-21 **10:40** the reported battery-mV field fell from the high to low band
while probe 5's inferred tank capacitance changed only **+0.019%** across the
surrounding 40 minutes. During the evening reversal the PC entered sleep at
**15:20**, returned on at **16:50**, and probe 5's counts kept falling on both
sides of that second transition. The count reversal therefore cannot be
explained as a simple high-voltage-versus-low-voltage offset.

The voltage-band observation does **not** establish an effect on probe 5's
FDC measurement, so it does **not** demote the observed **+1.23% tank-capacitance
rise during cooling**. The attribution remains provisional because probe 5 is
newly inserted and still settling. Probe 7's repeatable blade + FDC result and
the same-pot AM comparison remain independent supporting evidence.
