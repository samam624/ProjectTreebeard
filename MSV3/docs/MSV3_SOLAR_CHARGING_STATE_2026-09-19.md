# Solar charging state of probes 4, 6 and 7 (2026-09-19)

**Status: probe 4's charging suppression is measured against its own eight-day baseline and is strong
evidence. Its CAUSE is not yet proven -- a working `VBAT_OV` and a dead harvester produce identical
telemetry. The discriminator is whether the daytime ramp RETURNS as the cell falls through the
harvester restart band of roughly 3.77-3.92 V, which it enters around 2026-09-20/21.**

> **CORRECTION 2026-09-19 (later the same day).** An earlier version of this document said the test
> was "the 3.89 V crossing". That was wrong, and it conflated three different numbers. See
> "Threshold semantics" below. The authoritative source,
> `MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md`, **exists only on `main` (commit `edc9705`) and not on
> this branch**, which is why it was reasoned around rather than read.

Corrects `MSV3_PROBE7_ZENER_CHARGING_REASSESSMENT_2026-09-16.md`, whose harvest estimate for probe 7
is not supported by three further days of data.

## Probe 6 is on a desk and proves nothing about clamps

**Probe 6 has not been in sunlight since approximately 2026-09-16** (maintainer, 2026-09-19). It sits
indoors on a desk.

Any analysis treating probe 6's flat or declining battery as evidence that a clamp or threshold is
working is void. A probe with no illumination and a constant load declines; that is arithmetic, not a
result. It declines at **-22.73 mV/day (r2 = 0.992)** since 09-16, which is a clean load measurement
and nothing more.

Probe 6 remains useful for two things: it is a **thermally clean indoor temperature reference**
(2.50 C swing on 09-19, against 9.00 C for a sun-exposed board), and its 14-day trace records the
overvoltage history below.

### Probe 6's overvoltage history, for the record

| window | battery | interpretation |
|---|---|---|
| 09-13 -> 09-14 11:00 | **pinned 4460-4470 mV**, day and night | body-diode runaway, far above any healthy ceiling |
| 09-14 midday | drops ~200 mV during a data gap | the R11/R14 rework to 3.887 V nominal |
| 09-15 | 4248 -> 4296 in daylight (**+48 mV**) | still gaining ~360 mV ABOVE the new threshold |
| 09-16 onward | monotonic decline | no illumination; says nothing either way |

The 09-15 row is the interesting one: after the rework the board still charged well above its
programmed `VBAT_OV`. Whether that stopped because a clamp was fitted or because the probe moved
indoors **cannot be separated from this data**.

## Probe 4: the charging ramp is gone, in the most sun it has ever seen

Probe 4 is in the shared Alocasia pot with its solar panel. Its panel works the 11:00-16:00 window.
Insolation proxy is the board die-temperature excursion.

| day | 11:00 -> late | ramp | insolation dT |
|---|---|---:|---:|
| 2026-09-06 | 4021 -> 4075 | **+54 mV** | 6.50 C |
| 2026-09-07 | 4063 -> 4088 | **+25 mV** | 7.00 C |
| 2026-09-08 | 4081 -> 4110 | **+29 mV** | 7.00 C |
| 2026-09-09 | 4097 -> 4135 | **+38 mV** | 6.00 C |
| 2026-09-10 | 4118 -> 4167 | **+49 mV** | 7.00 C |
| 2026-09-11 | 4151 -> 4241 | **+90 mV** | 5.50 C |
| 2026-09-12 | 4216 -> 4291 | **+74 mV** | 6.62 C |
| 2026-09-13 | 4261 -> 4307 | **+46 mV** | 5.25 C |
| **2026-09-19** | **3994 -> 3988** | **-6 mV** | **8.62 C** |

**2026-09-19 carried the largest board-temperature excursion of the entire series and produced a
negative ramp.** Note the wording: die temperature is a proxy, not a validated irradiance
measurement, so "most sun" is an inference rather than a measurement.

Supporting detail:

- The hourly shape changed, not just the total. On 09-11 the trace bends upward at 12:00 and climbs
  +18 mV/h for five hours. On 09-19 it runs downhill through those same hours at -2.7 mV/h.
- **Wake cadence is identical across both eras**, 70 s/wake and 51.43 wakes/h measured on 09-10,
  09-12 and 09-19, so this is not a duty-cycle artifact. Identical cadence does not prove identical
  load *energy* -- transmit duration, retry count, radio current and regulator behaviour could still
  differ -- but it removes the simplest explanation.
- Peak observed was **4310 mV on 09-13**, matching the documented as-built ~4.26 V ceiling.
- The probe went off the air on **2026-09-14** -- the R11/R14 rework date -- and returned on
  2026-09-18 at 4034 mV.
- It is now discharging at **-53.6 mV/day (r2 = 0.982)**, faster than its old ~30 mV/day night rate
  because 3.99 V sits on a steeper part of the cell curve than 4.2 V.

### What this does and does not establish

**Established:** same board, same panel, same pot, more sunlight, **net daytime battery gain
changed sign**. That is a strong observational comparison against the probe's own baseline and it is
not explained by weather, load or duty cycle.

**Note the limit of the measurement: battery voltage is not charger current.** Nothing here measures
the harvester directly. Alternatives not yet excluded include battery temperature effects, voltage
relaxation, panel orientation or new shading at the pot position, charger thermal behaviour, RF retry
current, and -- importantly -- **a change in the battery-sense divider calibration during the
2026-09-14 rework**. If the telemetry scale moved, part of the 4277 -> 4034 mV step across the gap is
an artifact rather than discharge. **A single DMM reading on the cell compared against
`raw_battery_mv` settles that and is the cheapest open item in this document.**

**Not established:** the cause. Two hypotheses fit identically at 3.99 V:

- **(a)** the divider was reworked to 3.887 V nominal `VBAT_OV`, the cell is ~100 mV above it, and the
  IC is correctly refusing to charge;
- **(b)** the divider is still as-built at 4.26 V and the harvester is not delivering at all.

**The discriminator is whether the daytime ramp returns as the cell descends through the restart
envelope.** Under (a) charging resumes somewhere in it and the cell then floats at the ceiling. Under
(b) it sinks straight through with no ramp.

Note this is *supporting* evidence, not proof that the resistors are fitted as recorded. The stronger
diagnostic is the rework doc's own plateau/ripple check on VSTOR, quoted below.

### Threshold semantics

Four distinct numbers, from `MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` (`main`, `edc9705`). Earlier
drafts of this document collapsed them into one:

| quantity | value | what it is |
|---|---|---|
| `VBAT_OV` nominal | **3.887 V** | design target, datasheet VBIAS **1.25 V typ** |
| `VBAT_OV` on this board | **≈3.897 V** | **same resistors**, VBIAS **1.2532 V** backed out of probe 4's own 4.27 V / 4.259 V measurement |
| `VBAT_OV` corner band | **3.857-3.937 V** | the **ceiling** with ±1% resistors -- *not* the restart band |
| restart, nominal | **≈3.862 V** | `VBAT_OV` − `VBAT_OV_HYST`, 35 mV typ |

**3.887 and 3.897 are not a discrepancy and neither is "the" ceiling.** Both come from the same
fitted resistors, `R11` = 4.42 MΩ and `R14` = 4.12 MΩ, through
`VBAT_OV = 1.5 · VBIAS · (1 + R11/R14)`. They differ only in which VBIAS is used:

```text
1.5 × 1.2500 × (1 + 4.42/4.12) = 3.8865 V   <- datasheet VBIAS typ
1.5 × 1.2532 × (1 + 4.42/4.12) = 3.8965 V   <- VBIAS measured on this board
```

Quote 3.887 V when describing the design, 3.897 V when predicting *this* board. Never quote either
bare as "the nominal ceiling".

`VBAT_OV_HYST` has a range of **18 / 35 / 89 mV**, so the restart threshold inherits both the ceiling
corner band and the hysteresis spread:

```text
restart_min = 3.857 - 0.089 = 3.768 V
restart_max = 3.937 - 0.018 = 3.919 V
```

**This is a conservative worst-case envelope, ~3.77-3.92 V and about 151 mV wide**, formed by
combining independent extrema -- it assumes the resistor and hysteresis corners can be taken together
and that datasheet limits apply under these operating conditions. It is not a measured or
statistically expected band. Either way, watching for a crossing "at 3.89 V" is meaningless
precision.

At the observed -53.6 mV/day and 3988 mV on 09-19 15:00, probe 4 reaches the top of the envelope in
~1.1 days and the bottom in ~4.0 days, giving a **monitoring window of roughly 2026-09-20 to 09-24**.
That is an extrapolation of the current linear discharge rate, not a guaranteed window: solar input,
load, measurement error and a changing discharge rate can all move it.

### Why probe 4 currently sits ABOVE its own ceiling

Probe 4 reads 3973-3984 mV against a ≈3.897 V harvester ceiling. That is not a contradiction and it
is worth stating explicitly, because it looks like one:

**The rework did not touch the USB charge path.** `R8` = 27 kΩ on the BQ25170 still tops the cell to
**4.20 V**. Probe 4 was off the air 2026-09-14 to 09-18 being reworked and handled, on USB. So it was
charged above the harvester ceiling by a different path and returned at 4034 mV. It has been
descending through the harvester's dead band ever since, which is exactly what hypothesis (a)
predicts and is *itself* consistent with the harvester correctly refusing to charge.

**Hypothesis (a) is a documented maintainer record, not an inference.**
`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` carries a per-board status table:

| Board | Swapped? |
|---|---|
| All field probes except `MSV3-MOISTURE-007` | **Yes**, 2026-09-14 (maintainer) |
| `MSV3-MOISTURE-007` | **No** -- still `R11`=5.62 MΩ / `R14`=4.42 MΩ, ceiling ≈4.26 V |

Probe 4 is a field probe, so it was reworked. An earlier draft of this document downgraded that to a
"leading hypothesis" **because the doc is on `main` (`edc9705`) and not on this branch**, so it was
invisible from the working tree.

Note the limit of what this upgrades: it establishes the **documented intent and record** of the
rework, not an independent physical verification of probe 4's fitted resistors. The rework doc itself
supplies the physical check -- *"Power it from the panel with a cell attached and watch VSTOR: it
should plateau at ≈3.90 V and ripple down to ≈3.86 V before charging resumes. A probe still stopping
near 4.26 V has the resistors reversed or unswapped."*

### Registry correction

`fleet.json` probe 7's notes cited probe 4 as a live same-divider comparison. That is now wrong:
probe 4's 4309 mV cap is a **2026-09-13 observation**, the day before the rework. **Probe 7 is the
only board known to be on the as-built divider and its 4.26 V ceiling has no contemporaneous peer.**
Corrected in `MSV3/fleet.json` and `docs/STATUS_HANDOFF.md` on 2026-09-19.

## Probe 7: the zener is not killing the harvest

`MSV3_PROBE7_ZENER_CHARGING_REASSESSMENT_2026-09-16.md` estimated that the temporary 1N4728A left
probe 7 near **0.7-1.2 mAh/day net**, i.e. "a nearly flat battery", and warned the soft knee could eat
the window current. Three further days contradict the magnitude:

| day | daytime slope (09-18h) | night slope | 10-17h gain |
|---|---:|---:|---:|
| 2026-09-17 | **+7.94 mV/h** | +1.41 | +52 mV |
| 2026-09-18 | **+15.82 mV/h** | +4.07 | +106 mV |
| 2026-09-19 | **+6.36 mV/h** | -2.19 | +42 mV |

**4002 -> 4121 mV over 09-16..09-19, +40.6 mV/day net.** The clamp is not halving the harvest to
nothing. That estimate was built on a single sunset hour of data and should not be re-cited.

This does not rehabilitate the 1N4728A as a part choice. Everything the reassessment says about its
soft knee, its 76 mA characterisation, and the superiority of a characterised precision shunt
(ATL431B) or a lower-voltage panel stands. It means only that the harvest penalty was overestimated.

**Probe 7 reaches its as-built ~4.26 V ceiling around 2026-09-23, and that is the real clamp test:**
capping near probe 4's old 4309 mV means the clamp works; continuing toward probe 6's unclamped
4.44-4.49 V means it does not.

## Measurements still outstanding

The reassessment doc's test list is unchanged and still unrun. The two that would retire the most
uncertainty:

1. **A 20 s scope capture of `SP+` in direct sun**, distinguishing the 256 ms MPPT open-circuit
   sample from a steady level. This is what separates "the BQ25504 input pull-down is ineffective"
   from "a DMM update landed in the sample window".
2. **`I_SC` at the deployment window**, morning / solar noon / late afternoon, disconnected from the
   board. Every harvest estimate in these documents is currently scaled from probe 4 rather than
   measured.

## Method

Hourly and 30-minute medians of `raw_battery_mv` from `public.msv2_sensor_history`, receiver
`MSV2088A3104A7AC`, pulled with `cloud/scripts/fetch_supabase_history.py --raw`. Slopes are ordinary
least squares within the stated window. Insolation proxy is the per-day excursion of
`board_temp_quarter_c / 4`, which is a board-head temperature in air and responds to direct sun
rather than to air temperature -- see `MSV3_ELECTRODE_VS_READOUT_THERMAL_2026-09-19.md` for the
evidence that it steps +3.75 C at noon while an indoor board moves 2.5 C all day.
