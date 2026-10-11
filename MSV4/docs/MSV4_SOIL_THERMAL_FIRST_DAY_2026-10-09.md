# MSV4 board 1: temperature and the moisture number, first day in soil (2026-10-09)

The maintainer, 2026-10-09 ~15:15 PDT: "it looks like the temperature is still effecting the
moisutre reading, i do see a good curve behavior for the air temp vs soil temp where the bulk soil
medium is lagging the air temp, but it seems its still not compensating properly".

Checked against the stored history. **Confirmed: the correction is being applied exactly as
written, and it does not remove the temperature swing.** Numbers below.

## What was measured, and on what

- Board 1 (sensor 10), in the Alocasia pot since 2026-10-08 ~21:47 PDT, test image 262161
  measuring every minute (`MSV3/fleet.json`, `msv4_boards`, verified 2026-10-08). Where the pot
  sits, how wet the soil is and how deep the blade is are not stated. No watering since insertion
  has been stated.
- 1016 measurements, 2026-10-08 22:08 to 2026-10-09 15:29 PDT, from the cloud (fetched 15:30):
  `data/sensor_logs/msv4_sensor10_soil_2026-10-09.csv`.
- **This is the first 17 hours after insertion.** The blade is still settling (see the overnight
  rows), so nothing here is a settled coefficient.

```
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --since-hours 32 --csv data/sensor_logs/msv4_sensor10_soil_2026-10-09.csv
python MSV4/scripts/msv4_soil_thermal_check.py data/sensor_logs/msv4_sensor10_soil_2026-10-09.csv --since "2026-10-08 22:08" --edges "2026-10-08 22:18" 01:30 09:00 11:30 12:30 15:00 15:19 --window "2026-10-09 11:00" "2026-10-09 15:20"
```

## The correction is applied as designed (measured)

The stored moisture number equals `10000 × log10(400 kΩ / Rsoil@25°C)` recomputed from the stored
`Rsoil` and tip temperature to within 1 unit on all 1016 rows. Nothing is lost or skipped between
the probe and the database; the leftover swing is the correction itself.

## Today, 30-minute medians (measured)

"Moist (raw)" is the same formula with no temperature correction; "moist" is what is stored and
shown.

| PDT | Tip °C | Die °C | `Rsoil` Ω | `Rsoil`@25 Ω | Moist (raw) | Moist | `Cm` pF | `Cd` pF |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 09:00 | 21.99 | 22.80 | 16,228 | 15,254 | 13,918 | 14,187 | 23.0 | 21.82 |
| 10:00 | 22.25 | 22.83 | 16,172 | 15,283 | 13,933 | 14,179 | 23.3 | 21.81 |
| 11:00 | 22.51 | 23.13 | 15,934 | 15,137 | 13,997 | 14,220 | 23.6 | 21.85 |
| 11:30 | 22.70 | 24.48 | 15,924 | 15,204 | 14,000 | 14,202 | 23.6 | 21.91 |
| 12:00 | 23.23 | 26.08 | 16,021 | 15,489 | 13,974 | 14,120 | 23.4 | 21.90 |
| 12:30 | 24.11 | 27.28 | 15,826 | 15,477 | 14,027 | 14,124 | 23.5 | 21.92 |
| 13:00 | 24.90 | 27.82 | 15,403 | 15,381 | 14,145 | 14,150 | 24.2 | 22.04 |
| 13:30 | 25.52 | 28.07 | 14,929 | 15,094 | 14,280 | 14,232 | 24.9 | 22.13 |
| 14:00 | 25.96 | 27.88 | 14,683 | 14,971 | 14,352 | 14,268 | 25.8 | 22.18 |
| 14:30 | 26.26 | 27.53 | 14,598 | 14,963 | 14,378 | 14,270 | 25.9 | 22.17 |
| 15:00 | 26.27 | 27.01 | 14,197 | 14,560 | 14,499 | 14,389 | 26.6 | 22.22 |

The die (board end, in air) peaked at 28.07 °C at about 13:30 and is falling; the tip was still at
its highest at 15:00. That is the lag the maintainer described.

## Change across each stretch (measured; each end is the median of the readings within ±10 min)

| Stretch | Tip | Die | `Rsoil` | per °C of tip | `Rsoil`@25 | Moist (raw) | Moist | `Cm` | `Cd` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 01:30 → 09:00 | −1.62 °C | 0.00 | +8.9 % | −5.5 % | +5.3 % | −370 | −222 | +7.3 % | −0.80 pF |
| 09:00 → 11:30 | +0.50 | +1.00 | +0.2 % | – | +1.6 % | −8 | −68 | −0.2 % | 0.00 |
| 11:30 → 12:30 | +1.25 | +3.25 | −0.4 % | −0.4 % | +1.6 % | +19 | −70 | −0.6 % | +0.05 |
| 12:30 → 15:00 | +2.50 | +0.25 | −9.0 % | −3.6 % | −4.2 % | +410 | +184 | +12.0 % | +0.28 |

What that shows:

1. **The resistance moves later than the tip thermistor.** From 11:30 to 12:30 the tip rose
   1.25 °C and `Rsoil` did not move (−0.4 %, inside the 0.9 % single-reading scatter). The
   correction acted on the thermistor anyway, so the moisture number fell 70 with nothing
   happening in the resistance. That dip is made by the correction.
2. **Once it moves, it moves by more than 2 %/°C.** From 12:30 to 15:00, −9.0 % for +2.50 °C is
   3.6 %/°C of tip. The correction took out about 5 % and left 4.2 %, which is the +184 on the
   moisture number.
3. **Net for the warm-up so far, by the 30-minute medians:** uncorrected the number rose 581
   from 09:00 to 15:00; corrected it fell 67 to the 12:00 trough and then rose 269. The
   correction roughly halves the swing and changes its shape. `Rsoil` was still falling at 15:29 with the tip flat, so the
   afternoon is not finished.
4. **`Cm` moves with it, the other way.** In the afternoon `Rsoil` fell 9.0 % and `Cm` rose
   12.0 %, so `Rsoil × Cm` stayed at 0.37–0.38 µs. In the model the correction assumes (a fixed
   mask capacitance in series with a soil resistance) warming changes `Rsoil` alone.
5. **This is not the fit trading one parameter for the other.** Reading to reading, steps in
   `ln Cm` and `ln Rsoil` are uncorrelated (r = −0.03 over consecutive readings).
6. **The overnight stretch is not the afternoon run backwards.** `Rsoil` rose 5.5 % per °C of
   cooling, `Cm` rose with it instead of against it, and `Cd` fell 0.80 pF against +0.28 pF for
   a larger temperature change in the afternoon. Something other than temperature was still
   moving in the first twelve hours after insertion. It is not a thermal measurement.

## Is the afternoon change contact? (asked by the maintainer, 2026-10-09 ~15:30)

"I see Cm rising over time as well, is it just that? It thinks the contact is getting better
stil?"

In the fitted branch, more of the blade touching soil lowers `Rsoil` and raises `Cm` in
proportion, and leaves `Rsoil × Cm` where it was. Measured, same ±10 minute medians:

| Stretch | Tip | `Rsoil` | `Cm` | `Rsoil × Cm` | `Cd` |
|---|---:|---:|---:|---:|---:|
| 22:18 → 01:30, just planted | −1.62 °C | −20.0 % | +28.8 % | +3.1 % | +0.88 pF |
| 01:30 → 09:00, night | −1.62 | +8.9 % | +7.3 % | +16.9 % | −0.80 |
| 09:00 → 12:30, morning | +1.75 | −0.2 % | −0.9 % | −1.1 % | +0.05 |
| 12:30 → 15:19, afternoon | +2.50 | −11.5 % | +14.8 % | +1.6 % | +0.36 |

- **As the fit reports it, yes.** The afternoon is the same kind of change as the settling
  after planting: `Rsoil` down, `Cm` up in proportion, `Rsoil × Cm` unmoved, `Cd` up. The fit
  is reporting about 15 % more contact, not a soil that conducts better through the same
  contact. A 2 %/°C correction on `Rsoil` cannot take that out.
- **It is not the settling simply carrying on.** `Cm` sat between 23.0 and 23.8 pF for six
  hours, 06:30 to 12:30, with `Rsoil` flat. It began rising at about 12:45, an hour after the
  tip began to warm, and gained 15 % in under three hours.
- **The overnight rise in `Cm` is a different movement.** `Rsoil` rose with it, `Rsoil × Cm`
  rose 16.9 % and `Cd` fell. Better contact does none of those.
- **Not established: whether the contact really changed.** Inferred alternative: warmer, more
  conductive soil lets the current reach more of the mask, and a single R-C branch reports
  that as more contact. The record that splits them is the cooling that follows: `Cm` back
  toward 23.5 pF and `Rsoil` toward 15.9 kΩ as the tip returns to 22 °C is temperature; both
  staying near 26.7 pF and 14.1 kΩ is contact.

## Evening: the stored number rises while the resistance is flat (to 19:44)

The maintainer, ~19:40 PDT: "Soil moisture continues to rise over the last couple hours,
polarization?" Refetched at 19:45 (1271 measurements). Measured, ±10 minute medians:

| 16:00 → 19:34 | Tip | Die | `Rsoil` | `Rsoil`@25 | Moist (raw) | Moist | `Cm` | `Rsoil × Cm` | `Cd` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| | −1.75 °C | −2.88 °C | +0.1 % | −3.5 % | −6 | +156 | +4.8 % | +4.9 % | −0.02 pF |

- **The rise in the stored number is the correction.** `Rsoil` did not move (13,891 → 13,911 Ω).
  The tip cooled 1.75 °C, the correction lowered `Rsoil`@25 by 3.5 %, and that is the +156.
  Uncorrected the number is flat.
- **`Cm` is still rising:** 17.0 pF at 22:18 on 2026-10-08, 28.4 pF at 19:34. It has risen in
  every stretch except 06:30–12:30, through cooling and warming alike. `Cd` has stayed between
  22.13 and 22.25 pF since 13:30.
- **The resistance has not come back with the cooling.** The afternoon fits with a lag of
  90 minutes or less predicted 14,400–15,300 Ω by 19:34; it read 13,910 Ω. Fits with a lag of
  120–180 minutes predicted 13,670–14,120 Ω and are not yet excluded.
- **Two readings of the flat resistance, not separated by this record.** (a) Cooling raised it
  and contact lowered it by about as much: 2 %/°C over 1.75 °C is +3.5 %, a contact gain equal
  to the +4.8 % in `Cm` is −4.6 %, net −1.1 % against the measured +0.1 %. Then the rise in the
  stored number is contact, not water. (b) The resistance is not following the tip temperature
  and the correction is supplying the whole rise.
- **Against a polarization caused by the measurement (measured):** at the same one sweep a
  minute, `Cm` and `Rsoil` held still for six hours, 06:30–12:30.
- **Against an ionic layer showing in `Cm` (from the circuit, not measured):** the electrodes
  are under solder mask and the mask is in series, so a double layer of the order of µF/cm²
  (textbook) in series with tens of pF changes nothing the fit can see; the sweep is 250 kHz to
  2.67 MHz; U3 floats both electrodes between sweeps (`SWAP_nOE` pulled up). The electrodes are
  DC-coupled to the drive through R15 and U3 with no blocking capacitor
  (`MSV4/hardware/reference/PIN_NET_REFERENCE.md`), so this rests on the mask being intact
  everywhere soil touches copper. Nothing in this record tests that.
- For scale: the field solve's `Cm` for moist soil in full contact is 671 pF (modelled). 28.4 pF
  is 4 % of it; the first reading in soil was 15.2 pF.

## Was it temperature at all? (to 20:46)

The maintainer, ~20:50 PDT: "are we able to tell now that it actually wasnt the temperature all
along that was doing this swing then? it seems like there is no coorleation". **Not yet. It
leans that way, and the reading on the morning of 2026-10-10 decides it.** 1333 measurements.

```
python MSV4/scripts/msv4_soil_thermal_check.py data/sensor_logs/msv4_sensor10_soil_2026-10-09.csv --since "2026-10-08 22:08" --window "2026-10-09 11:00" "2026-10-09 15:20" --match "2026-10-09 09:00" "2026-10-09 15:20"
```

Correlation of `ln Rsoil` with the tip temperature (measured): −0.93 while warming (09:00–15:20,
354 readings), **+0.06 while cooling** (15:20–20:46, 327 readings). Over both together it is
−0.76 with the tip and −0.91 with the clock.

The same tip temperature on the way up and on the way down (measured):

| Tip | Warming, at | `Rsoil` | `Cm` | Cooling, at | `Rsoil` | `Cm` | `Rsoil` change | `Cm` change |
|---:|---|---:|---:|---|---:|---:|---:|---:|
| 26.0 °C | 14:17 | 14,683 Ω | 25.8 pF | 16:17 | 13,972 Ω | 27.2 pF | −4.8 % | +5.6 % |
| 25.5 | 13:44 | 14,936 | 24.9 | 17:25 | 14,064 | 27.8 | −5.8 % | +11.6 % |
| 25.0 | 13:19 | 15,351 | 24.4 | 18:35 | 13,946 | 27.9 | −9.2 % | +14.3 % |
| 24.5 | 12:59 | 15,573 | 23.7 | 19:25 | 13,933 | 28.4 | −10.5 % | +19.8 % |
| 24.0 | 12:41 | 15,861 | 23.5 | 20:22 | 14,166 | 28.3 | −10.7 % | +20.4 % |

- **The resistance has not retraced.** At 24.0 °C it is 10.7 % below where it was at 24.0 °C
  on the way up.
- **That alone does not clear temperature**, because a slow response draws the same loop.
  Fitted over the whole of today (09:00–20:46), `ln Rsoil` against the tip through a 120 or
  180 minute lag gives 4.4 or 5.0 %/°C with 1.38 or 1.45 % left over, against 1.10 % of plain
  scatter this evening and 2.44 % for a straight line in time.
- **Those slow fits are starting to miss.** Their lagged temperature peaked at 16:57 (120 min)
  or 18:16 (180 min) and has since fallen 0.89 or 0.48 °C, which should have raised `Rsoil`
  3.9 or 2.4 %. Measured over the same spans: −0.4 % and +0.5 %.
- **And their size is not the correction's physics.** 4.4–5.0 %/°C is more than twice what ion
  mobility gives, and a body that lags a thermometer swings less than it, not more.
- **`Cm` is not following temperature.** It rose while the pot cooled on the first night, rose
  while it warmed this afternoon, and is rising while it cools now; +1.6 %/h on average since
  01:30.
- **The test:** this morning at a tip of 22.0 °C `Rsoil` read 16,057 Ω. If temperature drives
  it, every slow fit puts it back at 15,800–16,200 Ω once the pot is at 22.0–22.5 °C again. If
  the afternoon was a one-way change it stays near 14,000 Ω. The two are 14 % apart.
- **Result, added 2026-10-10:** the pot bottomed at 22.5 °C and `Rsoil` read 15,635–15,672 Ω
  from 08:00 to 11:00, 1.5 % below its value at 22.5 °C on the way up. It was temperature. `Cm`
  stayed at 28.5 pF, so `Rsoil × Cm` rose 20 % and is not the contact-free quantity this record
  treats it as. See `MSV4_SOIL_THERMAL_SECOND_NIGHT_2026-10-10.md`.

## Why `Cm` is 28 pF and not 671 pF

Asked by the maintainer, ~19:50 PDT. The 671 pF is the field solve's value for moist soil
(er 15, 0.02 S/m) pressed against a 20 µm mask with **no gap anywhere**
(`MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md`). The same solve gives −50 % for a 5 µm air gap and
−67 % for 10 µm, which is a mask worth 5 µm of air in series with the gap:
`Cm = 671 pF × 5 / (5 + gap in µm)`. Carried past the two solved points (an extrapolation):

| Reading | `Cm` | Uniform air gap that gives it | Or share of the mask in full contact |
|---|---:|---:|---:|
| Field solve, moist soil, no gap (modelled) | 671 pF | 0 | 100 % |
| Field solve, 5 µm / 10 µm gap (modelled) | 335 / 221 pF | 5 / 10 µm | |
| Hand on the blade, 2026-10-01 (measured) | ~53 pF | 58 µm | 8 % |
| Damp towel, 2026-10-01 (measured) | 26.7 pF | 121 µm | 4.0 % |
| Soil, first firm reading, 10-08 21:57 (measured) | 15.2 pF | 216 µm | 2.3 % |
| Soil, 10-09 19:34 (measured) | 28.4 pF | 113 µm | 4.2 % |

- An average standoff of a tenth of a millimetre between the mask and anything conducting is
  enough. The rise from 15.2 to 28.4 pF is that standoff going from about 0.22 to 0.11 mm.
- It is not the soil and not the epoxy of 2026-10-08: the towel and the hand read the same range
  a week earlier. **Nothing has read above about 53 pF on this board**, so the 671 pF end has
  never been checked, and the 20 µm mask thickness in it is an assumption, not a measurement.
- `Cd` points the same way. `Cp + Cd` went from 22.4 pF in air to 30.9 pF in soil, +8.5 pF.
  On the solve's lossless table (6 pF air, 12 pF at er 4, 23 pF at er 10) that is a medium of
  about er 5 against the blade, where the modelled moist soil was er 15. Inferred from the
  model's table, not measured.
- Soil wetness at planting was not stated; the pot's last recorded watering is 2026-09-22.
  A watering is the measurement that tests the top of the range: water fills the gaps and is
  the mask capacitor's second plate.

## Lag fit, 11:00 to 15:20 (measured fit; 260 readings)

`ln Rsoil` against the tip temperature passed through a first-order lag, no drift term:

| Lag time constant | Coefficient | Residual sd |
|---:|---:|---:|
| 0 min | 2.65 %/°C | 1.57 % |
| 20 min | 2.77 | 1.25 % |
| 30 min | 2.88 | 1.17 % |
| 45 min | 3.07 | 1.12 % |
| 60 min | 3.29 | 1.10 % |
| 90 min | 3.80 | 1.10 % |
| 120 min | 4.38 | 1.11 % |
| 180 min | 5.77 | 1.13 % |

For scale: the sd of `ln Rsoil` over that window is 4.09 % as sent and 1.82 % after the
receiver's correction; one reading scatters 0.91 %.

- **A lag is needed.** No lag leaves 1.57 %; any time constant from 45 to 180 minutes leaves
  1.10–1.13 %.
- **One afternoon cannot separate the lag from the coefficient.** 3.1 %/°C with 45 minutes and
  5.8 %/°C with 180 minutes fit equally. The coefficient is above 2 %/°C at every lag.
- With a linear drift term added the fit is unstable (the temperature rose almost steadily, so
  drift and temperature are the same shape); the script prints it and it is not used here.
- The die temperature, lagged, fits no better than the tip (1.22 % at 240 minutes).

## What is and is not established

Measured: the correction is applied as written; the resistance lags the tip thermistor; over the
part of the afternoon where it moved, it moved 3.6 %/°C of tip; `Cm` moves with it; the fit is
not responsible.

Inferred, not measured: why. Two readings both fit one afternoon. The soil the electrodes sense
may warm later and by a different amount than the tip thermistor does (the thermistor sits at one
point, epoxied since 2026-10-08, on a board whose other end is in warmer air). Or the board's own
measurement may carry a temperature term on top of the soil's, as `measurement_model` in
`MSV3/fleet.json` describes for the earlier generations. The die and tip temperatures rose
together today, so this record does not split them.

Not yet available: a settled coefficient (the blade went in 17 hours before the end of this
record), a complete day-night cycle after settling, and a watering, without which the size of
this swing has nothing to be compared against.
