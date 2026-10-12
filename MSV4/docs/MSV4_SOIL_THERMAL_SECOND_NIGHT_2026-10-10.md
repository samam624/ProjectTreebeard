# MSV4 board 1: the resistance came back overnight (2026-10-10)

The maintainer, 2026-10-10 ~11:20 PDT: "analyze the data from last night and see whats going on,
stil really not liking what i am seeing the mositure seems to be tracking temperature at least
visually exactly like before".

**Confirmed. The resistance followed the pot's temperature down and back up, so the swing is
temperature and not a one-way change. The receiver's correction took out 16 % of it.** This
closes the test left open in `MSV4_SOIL_THERMAL_FIRST_DAY_2026-10-09.md`.

## What was measured, and on what

- Board 1 (sensor 10), in the Alocasia pot since 2026-10-08 ~21:47 PDT (`MSV3/fleet.json`,
  `msv4_boards`, verified 2026-10-08). **Nothing about the pot or the probe has been stated since
  then.** No watering and no handling are assumed, not known. Where the pot sits is not stated.
- 2209 measurements, 2026-10-08 22:08 to 2026-10-10 11:23 PDT, from the cloud (fetched 11:24):
  `data/sensor_logs/msv4_sensor10_soil_2026-10-10.csv`.

```
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --since-hours 64 --csv data/sensor_logs/msv4_sensor10_soil_2026-10-10.csv
python MSV4/scripts/msv4_soil_thermal_check.py data/sensor_logs/msv4_sensor10_soil_2026-10-10.csv --since "2026-10-08 22:08" --edges "2026-10-09 09:00" "2026-10-09 16:00" "2026-10-09 21:00" "2026-10-10 01:00" "2026-10-10 08:00" "2026-10-10 11:00" --window "2026-10-09 09:00" "2026-10-10 11:30" --apply "2026-10-09 01:30" "2026-10-09 09:00" --match "2026-10-09 09:00" "2026-10-09 15:20"
```

The instrument, checked first (measured):

- The wake counter has not restarted since 22:07:55 on 2026-10-08 (2237 at 11:23 on 10-10, one a
  minute). Same boot, so the same image (262161) and the same `Cp`; the mask-check build 262162
  is not on the board.
- The stored moisture number equals the receiver's formula recomputed from the stored `Rsoil`
  and tip temperature to within 1 unit on all 2209 rows.
- The fit residual sent with each reading is 1.19–1.27 % in every half hour from 06:00 on 10-09.
  The cell read 3807–3820 mV.

## Yesterday's test, answered (measured)

The same tip temperature on the way up on 10-09 and on the way back down:

| Tip | Warming, at | `Rsoil` | `Cm` | Cooling, at | `Rsoil` | `Cm` | `Rsoil` change | `Cm` change |
|---:|---|---:|---:|---|---:|---:|---:|---:|
| 24.5 °C | 12:59 | 15,573 Ω | 23.7 pF | 19:25 | 13,933 Ω | 28.4 pF | −10.5 % | +19.8 % |
| 24.0 | 12:41 | 15,861 | 23.5 | 23:12 | 14,502 | 28.2 | −8.6 % | +20.0 % |
| 23.5 | 12:26 | 16,000 | 23.3 | 03:33 | 15,049 | 28.4 | −5.9 % | +21.9 % |
| 23.0 | 12:05 | 15,903 | 23.4 | 05:28 | 15,271 | 28.6 | −4.0 % | +22.2 % |
| 22.5 | 11:10 | 15,868 | 23.7 | 09:02 | 15,635 | 28.5 | −1.5 % | +20.3 % |

- **`Rsoil` came back.** Yesterday's record set the test: 15,800–16,200 Ω once the pot was at
  22.0–22.5 °C again meant temperature, staying near 14,000 Ω meant a one-way change. The pot
  bottomed at 22.5 °C and `Rsoil` read 15,635–15,672 Ω from 08:00 to 11:00.
- **`Cm` did not.** It has read 27.9–28.9 pF in every half hour since 17:30 on 10-09, through
  3 °C of cooling, and is 20 % above where it was at the same temperature a day earlier.

## What the stored number did (the trace on the History chart)

Each end is the median of the readings within ±10 minutes. "Moist (raw)" is the formula with no
temperature correction; "moist" is what is stored and shown. 43 units is 1 % of resistance.

| Stretch | Tip | Die | `Rsoil` | per °C of tip | Moist (raw) | Moist | `Cm` | `Cd` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 10-09 09:00 → 16:00 | +4.25 °C | +3.00 | −13.2 % | −3.1 % | +616 | +240 | +15.5 % | +0.40 pF |
| 16:00 → 21:00 | −2.50 | −2.50 | +1.9 % | −0.8 % | −81 | +136 | +4.6 % | −0.08 |
| 21:00 → 10-10 01:00 | +0.50 | +1.00 | +3.0 % | – | −128 | −172 | −1.4 % | −0.18 |
| 01:00 → 08:00 | −1.75 | −1.25 | +6.5 % | −3.7 % | −274 | −116 | +2.0 % | −0.43 |
| 08:00 → 11:00 | +0.25 | +0.25 | +0.3 % | – | −14 | −38 | +0.9 % | −0.05 |

Over 10-09 09:00 to 10-10 11:23, by 30-minute medians:

| | Low | High | Peak to peak |
|---|---|---|---:|
| No correction | 13,917 at 09:30 | 14,577 at 19:30 | 661 |
| As stored (2 %/°C of the tip, no lag) | 14,120 at 12:00 | 14,672 at 21:00 | 552 |

1. **The correction removed 16 % of the swing** and moved its peak from 19:30 to 21:00.
2. **Last night's fall is the resistance coming back up.** From 21:00 to 08:00 the stored number
   fell 370 (14,672 to 14,302, 30-minute medians); uncorrected it fell 483.
3. **The resistance moves hours after the tip thermistor.** The tip peaked at 15:00. `Rsoil` sat
   at its lowest from 16:00 to 21:00 (+1.9 %) while the tip cooled 2.50 °C; over that stretch the
   stored number rose 136 while the uncorrected one fell 81, so that rise was made by the
   correction. `Rsoil` then rose 3.0 % from 21:00 to 01:00 while the tip was rising 0.50 °C.
4. **Where both moved steadily it is 3.7 %/°C, not 2.** 01:00 to 08:00: +6.5 % for −1.75 °C.
5. **Flat when the tip was flat.** 08:00 to 11:00: tip +0.25 °C, `Rsoil` +0.3 %.

## One size and one delay fit the whole cycle (measured fit, 1557 readings)

`ln Rsoil` against the tip temperature passed through a first-order lag, 10-09 09:00 to
10-10 11:23. "Number swing" is the peak-to-peak of the moisture number if that fit were the
correction. "First night" is the same fit run unchanged over 10-09 01:30–09:00.

| Lag | Coefficient | Left over | Number swing | First night: mean miss, sd |
|---:|---:|---:|---:|---:|
| 0 min | 3.02 %/°C | 3.24 % | 571 | −0.42 %, 1.81 % |
| 60 | 3.78 | 2.22 % | 392 | −0.14 %, 1.83 % |
| 120 | 4.51 | 1.57 % | 241 | +0.84 %, 2.39 % |
| 150 | 4.86 | 1.48 % | 197 | +1.43 %, 2.60 % |
| 180 | 5.20 | 1.53 % | 197 | +2.02 %, 2.77 % |
| 240 | 5.89 | 1.80 % | 268 | +3.17 %, 2.99 % |

For scale: `ln Rsoil` has an sd of 4.89 % as sent and 3.44 % after the receiver's correction;
one reading scatters 0.90 %.

- **Best: 150 minutes and 4.9 %/°C**, leaving 1.48 % and a swing of 197 where 661 went in.
- **It is not validated.** One cycle sets two numbers (how big, how late) and leaves nothing to
  check them against. Run over the first night, the 120–180 minute fits miss by more (2.4–2.8 %)
  than no lag at all (1.8 %). That night the blade was still settling (`Cm` 22.1 → 23.8 pF, `Cd`
  −0.8 pF), so it is not a fair test either way. The warm-up of 10-10 is the first independent
  one.
- **What the 150-minute fit says for 10-10**, if the tip repeats 10-09 (22.5 → 26.3 °C by
  15:00): `Rsoil` at 13.8–14.0 kΩ from 16:00 to 19:00, and the stored number repeating
  yesterday's shape, down about 100 around noon and up to about 14,650 by 21:00.
- The die temperature, lagged, fits worse at every lag (2.26 % at 240 minutes).

### Result of that test, added 19:15 on 10-10: the lag fit failed, and the stored number held

The maintainer at ~14:15: "trend is looking better this afternoon". It was. Measured, 396
readings from 12:00 to 19:12 (`--apply "2026-10-10 12:00" "2026-10-10 19:12"`):

| | Predicted above | Measured |
|---|---|---|
| `Rsoil`, 16:00–19:00 | 13.8–14.0 kΩ | 15.0–15.4 kΩ |
| 150-minute fit run over those hours | | misses by +7.3 % on average, sd 4.9 % |
| Stored number, 11:00–19:12 | down about 100, then up towards 14,650 | 14,117–14,282, peak to peak 166 |
| Uncorrected number, same hours | | peak to peak 309 |

- **The 150-minute, 4.9 %/°C fit is refuted.** Every lag in the table above misses the 10-10
  afternoon by 6–8 %.
- **On 10-10 the resistance moved with the tip thermistor, in step:** 12:00 → 14:30, tip
  +4.25 °C, `Rsoil` −7.1 % (1.7 %/°C); 14:30 → 19:00, tip −2.12 °C, `Rsoil` +5.1 % (2.4 %/°C).
  Fitted over 11:00–19:12 with no lag: 1.46 %/°C leaving 0.96 %, which is the scatter of single
  readings (0.90 %).
- **So the receiver's 2 %/°C correction did its job on 10-10:** 166 left of 309, against 552 of
  661 on 10-09.
- **What made 10-09 different is not established.** That was the first full day after
  planting, and `Cm` rose 20 % in those same hours; on 10-10 it rose 8 % and gave 3 % back.
  One day each way. The statements above that the resistance "moves hours after the tip
  thermistor" and "by more than 2 %/°C" describe 10-09 only.

## `Cm` and `Cd`

- **The `Cm` rise of the 10-09 afternoon was one-way and `Rsoil` did not keep it.** More of the
  blade in contact would have left `Rsoil` about 17 % lower at the same temperature. It is 1.5 %
  lower. So that rise was not contact in the sense that lowers the resistance, unless the soil
  dried by as much over the same day.
- **`Rsoil × Cm` went from 0.373 to 0.448 µs (+20 %).** Yesterday's record used it as the
  quantity that contact cannot move. It carried the whole `Cm` step, so it is not a
  temperature-free moisture number either.
- **`Cd` is higher when warm and lower a day later.** 21.81–21.88 pF at 22.0–22.5 °C on the
  morning of 10-09, 22.25 pF at the warm end, 21.53–21.57 pF at 22.5 °C on the morning of 10-10:
  −0.3 pF in 23 hours at the same or a warmer tip, which is 3.5 % of the 8.5 pF the soil adds
  over air.

## What is and is not established

Measured: `Rsoil` returned to within 1.5 % at the same tip temperature a day apart; the stored
number swung 552 against 661 uncorrected; the resistance moves hours after the tip thermistor
and by more than 2 %/°C; `Cm` stepped up once and stayed; the instrument did not restart and its
fit residual did not change.

Inferred, not measured:

- **The tip thermistor is not reading the temperature of whatever sets the resistance.** It
  peaked 1.5 hours after the die at 80 % of the die's swing; the resistance turned hours after
  that. A thermistor on the board, epoxied at the tip, can be warmed down the board from the end
  in air faster than the soil around the electrodes warms. *(2026-10-11: not borne out. That
  was the settling day. On the settled dry day and after the watering the resistance follows
  the tip in step at 2 %/°C: `MSV4_NTC_VS_DIE_TEMPERATURE_2026-10-11.md`.)*
- **3.7–4.9 %/°C is about twice what ion mobility in water gives** (the 2 %/°C the correction
  assumes). With 15 kΩ and 28 pF where the field solve's wet, full-contact soil gives 355 Ω and
  671 pF, the current is not flowing through bulk wet soil against the whole mask, and nothing
  says a film or a few contact points should follow the bulk coefficient.

Not available:

- A second cycle, to test the size and the delay on hours they were not fitted to.
- **A watering.** Until one is recorded the 552 has nothing to be measured against: it is 13.6 %
  of resistance, and what fraction of a watering that is has not been measured on this board.
  That fraction is the number the MSV3 rigs were compared on (`measurement_model` in
  `MSV3/fleet.json`: 2.6–29.7 % of a watering per day).
- Where the pot sits and whether the sun reaches the pot or the board. The die rose 4.9 °C
  between 11:00 and 13:30 on 10-09.
