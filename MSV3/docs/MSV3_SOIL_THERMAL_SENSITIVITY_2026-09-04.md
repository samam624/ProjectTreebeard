# MSV3 probe 4 soil drift: measured as temperature, not soaking — 2026-09-04

**Complaint.** Probe 4 has been sitting in a pot overnight and its reading moves over the day. It
looked like the probe soaking in. The same shape appeared again after the windows were opened.
Probe 5, in drying soil, has behaved correctly since insertion. The apparent pattern was "only
probes with solar do this".

**Measured.** Soaking is real but small; the dominant term is **die temperature**, and it is not
the solar panel and not the board electronics. Those parts are quantified below and are solid.

**Leading hypothesis, not yet proven:** that what sets the 32x difference in thermal sensitivity
between the two probes is how much liquid water is coupled to the electrodes, with the sun acting
only as the thing that supplies the temperature swing. This fits every number here and the
probes' histories, but it rests on two boards differing in several ways at once. "What would
confirm this, cheaply" below gives three tests that would falsify it.

This is not a new fault. It is the same effect
[the 2026-08-30 investigation](MSV3_FDC_THERMAL_DRIFT_ROOT_CAUSE_2026-08-30.md) measured as
0.68–1.00% daily peak-to-peak. Today it measured 0.80%. What is new is that the diagnostic
telemetry now lets it be *apportioned* instead of just observed.

## The measurement

1,295 readings from the two live probes, 2026-09-04 02:00–14:46 PDT, from
`msv2_sensor_history` (`raw_fdc_counts`, `bare_tank_fdc_counts`, `board_temp_quarter_c`,
`raw_battery_mv`). The window starts at 02:00 so probe 4's midnight reinsertion step is excluded.

Each probe's connected count was fit to `raw = a + b·temperature + c·hours`, separating a
temperature response from a genuine one-way trend:

| | probe 4 (in the pot) | probe 5 (drying soil) |
|---|---:|---:|
| readings | 657 | 638 |
| die-temperature swing | 7.00 °C | 4.75 °C |
| **temperature term** | **−0.1174 %/°C** | **−0.0037 %/°C** |
| …across that swing | **−0.822 %** | −0.018 % |
| **time term** | **+0.0140 %/h** | +0.0082 %/h |
| …across 12.8 h | +0.179 % | +0.105 % |
| R² | 0.528 | 0.608 |

For probe 4 the temperature term is **4.6x** the time term. That ratio compares the two
*contributions* — −0.822% across the observed 7.00 °C swing against +0.179% across the observed
12.8 h — not the raw coefficients, which are in different units (%/°C and %/h) and cannot be
compared directly. For probe 5 the temperature contribution is negligible and the slow drying
trend is essentially all of it, which is exactly what "correct behaviour in drying soil" should
look like.

The two probes' **temperature sensitivities differ by a factor of 32** (−0.1174 vs
−0.0037 %/°C), while their temperature *swings* differ by only 1.5x. So the swing is not the
explanation. The sensitivity is.

## It is not the board

Probe 4 sat in **air** on 08-31 and 09-01 with the same firmware and the same electronics. Fitting
those two days the same way gives the board-only baseline:

| probe 4 | in air (08-31/09-01) | in the pot (09-04) | ratio |
|---|---:|---:|---:|
| temperature term | −0.0171 %/°C | −0.1174 %/°C | **6.9x** |
| time term | +0.0009 %/h | +0.0140 %/h | 15x |
| daily peak-to-peak | 0.096–0.098 % | 0.800 % | 8x |
| R² | 0.965 | 0.528 | |

In air the probe is a clean, almost perfectly thermal instrument: R² 0.965, no time trend at all.
Put the same board in wet soil and its temperature coefficient grows sevenfold and the fit gets
much noisier, because a second, messier physical path has been added.

The on-board reference says the same thing from the other direction. The bare-tank count — taken
with the mux disconnecting the electrodes, so the FDC oscillates on L2/C17 alone — correlates
**−0.9933** with die temperature on probe 4 today, reproducing the −0.9946 the 2026-08-30 work
found. But its slope is only −0.0198 %/°C, so **the board's own thermal drift accounts for about a
fifth of the connected count's slope**. Dividing it out leaves 0.816% of the original 0.885% still
moving. The reference is working correctly and is not the problem.

## Why wetness would set the gain, if the hypothesis holds

Water is what makes the difference between the two probes, and it acts on the sensor in a strongly
temperature-dependent way: the ionic conductivity of pore water rises roughly 2%/°C, and the loss
it presents to a ~3 MHz tank rises with it. More liquid water coupled to the electrodes therefore
means a larger `d(count)/d(temperature)`, independent of how much the temperature actually moves.

Probe 4 is the wetter of the two by history as well as by pot: it is the **water-immersion board**,
the one that spent 24 h submerged and then had its nail-polish coating removed after
[liquid pockets were found beneath it](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md).
It was reinserted into a watered pot around midnight. Probe 5 is in soil that is drying out.

## Why "only the solar ones" is a coincidence of siting

The solar panel does not couple into the measurement. What it does is mark which probe is in the
sun, and the sun is what drives the temperature:

- Probe 4's die temperature swings 4.6–7.0 °C daily; probe 5's swings 1.5–2.6 °C.
- Probe 4's battery climbs 3936 → 3990 mV through the daylight hours, so it is harvesting.
- Opening the windows put more light on it, which is why the shape reappeared today.

So the correlation with solar is real but indirect, and it is the weaker half of the story: 1.5x
more thermal drive on top of 32x more thermal sensitivity.

## Two telemetry defects found along the way

- **The charging flag is not trustworthy on either probe.** Probe 4 never asserts it (0% of rows
  at every hour) even while its battery visibly climbs through the day, so it is missing real
  harvesting. Probe 5 asserts it in ~50% of rows at *every* hour of the day and night, flipping
  682 times in 1498 consecutive readings, at 4280–4441 mV — a charger hovering at its regulation
  threshold and toggling, not a day/night pattern. It does not track `tank_phase` (50.3% agreement
  is chance), so it is not the phase bit leaking. Do not use bit0 as evidence of harvesting.
- Probe 5's resting battery of 4280–4441 mV is above a single LiPo's ceiling, which is worth
  confirming against what is actually fitted to that board.

## What would confirm this, cheaply

The claim is falsifiable and the test costs nothing but time:

1. **Let probe 4's pot dry out** and refit. If wetness sets the gain, its −0.117 %/°C should fall
   toward probe 5's −0.004 %/°C as the soil dries, with no firmware or hardware change.
2. **Water probe 5** and refit. Its coefficient should rise. If neither moves, wetness is not the
   variable and the difference is something specific to probe 4's hardware or its coating
   remediation.
3. **Shade probe 4 for a day.** Its temperature swing should collapse and the daily peak-to-peak
   should shrink roughly in proportion, while the time term stays put. That separates drive from
   sensitivity directly.

## What this means for the moisture reading

Unchanged from 2026-08-30 and worth restating because the numbers now make it concrete: a −0.822%
daily thermal excursion is **larger than the −0.711% step measured for an actual watering**. Until
that is compensated, a warm afternoon and a watering can look the same to the receiver, and on
probe 4 specifically the daily swing will keep being read as the plant getting wetter and drier.

The bare-tank reference is necessary but not sufficient — it removes about a fifth. The rest is in
the electrode/soil path, which is where a real fix has to act: either a soil-temperature input, or
an electrode/front-end design whose response to pore-water conductivity is smaller. That remains
the open V4 item recorded in
[the insertion-settling and temperature plan](MOISTURE_INSERTION_SETTLING_AND_V4_TEMPERATURE_PLAN_2026-08-24.md).

## Direction of the sign, so the receiver display is not misread

Counts and displayed moisture move in opposite directions. Higher FDC count = lower capacitance =
**drier**. So probe 4's count falling through the afternoon (−0.822% as it warms) is displayed as
moisture **rising**. If the observed complaint is "the displayed value rises through the day", that
is this effect. The +0.014 %/h time term runs the other way — count rising, displayed moisture
slowly falling — which is a slow dry-down, not soaking in.
