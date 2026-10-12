# MSV4 board 1: the tip thermistor against the chip's own temperature (2026-10-11)

Dated record. Where the board is now is in `MSV3/fleet.json` (`msv4_boards`).

The maintainer, 2026-10-11 ~14:40 PDT: "Do some analysis of the ntc thermistor vs the on chip
die, is the ntc worth it? Or is it not necessary for good compensation?"

**The thermistor is what makes the correction work as it stands: 2 %/°C of the tip, with no
delay, takes the resistance's daily wobble from 22 g of water to 8 g, in dry soil and in moist.
The die as it is read cannot do that (10 to 28 g). A die reading averaged over the last two
hours can (8 g), in this pot.** What the thermistor then still gives is the level after a
watering (0.6 °C), agreement between probes (about ±0.4 °C against ±5 °C, by the data sheets),
and a right answer from the first reading after a restart.

## What was measured, and on what

- Board 1 (sensor 10) in the Alocasia pot since 2026-10-08 ~21:47 PDT, watered 2026-10-10 19:14
  (`MSV3/fleet.json`: `msv4_boards` and `pots.shared-alocasia`, verified 2026-10-08 and
  2026-10-10). Where the pot sits, and what sun reaches the pot or the board, are not stated.
- Two temperatures in every packet. **Tip:** the NTC at the blade tip (R17), in the soil,
  epoxied on 2026-10-08. **Die:** the nRF52840's own sensor, at the board end in the air, read
  at the start of the wake before the sweep and the radio warm the chip. Both arrive in steps
  of 0.25 °C.
- 3,793 conducting readings, 2026-10-08 21:57 to 2026-10-11 14:42 PDT, one a minute, from the
  cloud: `data/sensor_logs/msv4_sensor10_all_2026-10-11.csv`. Three boots on 10-11 (updates at
  11:52 and 13:03).

```
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --since-hours 116 --csv data/sensor_logs/msv4_sensor10_all_2026-10-11.csv
python MSV4/scripts/msv4_ntc_vs_die.py data/sensor_logs/msv4_sensor10_all_2026-10-11.csv
python MSV4/scripts/msv4_ntc_vs_die.py --self-test
```

Three stretches, none across the watering:

| Stretch | Hours | What it is |
|---|---|---|
| Settling | 10-08 22:57 to 10-09 19:14 | the day after planting; the soil was still closing on the blade |
| Dry, settled | 10-09 19:14 to 10-10 19:14 | the whole day before the watering |
| Moist | 10-10 23:14 to 10-11 14:42 | from four hours after the watering; 15.5 hours, one night and one noon warm-up |

"Wobble" below is the measure the moisture competition uses: the range of the hourly medians
about a straight line through the stretch (the line takes the drying out). Grams are at the
moisture number's own 17.4 units per gram of water, from the pot's weighings.

## The two temperatures (measured)

| Stretch | Tip | Die |
|---|---:|---:|
| Settling | 21.75–26.50 °C | 21.25–28.50 °C |
| Dry, settled | 22.25–27.50 °C | 22.25–30.25 °C |
| Moist | 20.75–25.50 °C | 20.25–29.25 °C |

- **The die swings further and turns sooner.** On 10-10 it peaked at 30.25 °C at 13:59; the tip
  peaked at 27.50 °C at 14:26. From 10:30 the die had risen 7.0 °C and the tip 4.75 °C. On
  10-11, 10:30 to 14:42: die +6.25 °C, tip +4.0 °C.
- **The tip is the die two hours late.** A first-order lag of 120 minutes on the die, with no
  change of scale (×1.00) and −0.39 °C, gives the tip to 0.17 °C over the settled dry day.
  Fitted on each stretch alone the lag comes out 105, 120 and 135 minutes.
- **That filter, run unchanged on the other two stretches:**

  | Stretch | Filtered die minus tip |
  |---|---|
  | Settling | +0.13 °C on average, sd 0.15 °C |
  | Dry, settled (where it was fitted) | 0.00 °C, sd 0.17 °C |
  | Moist | **+0.60 °C** on average, sd 0.25 °C, from +0.02 to +1.10 °C |

- **Moist soil sits cooler than the board says.** After the watering the tip runs 0.6 °C below
  what the dry-soil relation gives. The watering itself did not step the tip (25.25 °C in the
  ten minutes before and after).

## The coefficient each one asks for (measured)

`ln Rsoil = a + b·time + k·temperature`, fitted on each stretch. In brackets, the sd of what is
left; one reading scatters 0.9 %.

| Stretch | Tip alone | Die alone | Both: tip, die |
|---|---:|---:|---:|
| Settling | −1.36 %/°C (3.84 %) | +0.28 %/°C (4.25 %) | −2.81, +1.88 %/°C (3.10 %) |
| Dry, settled | **−2.05 %/°C** (1.29 %) | −0.93 %/°C (2.42 %) | −2.31, +0.31 %/°C (1.24 %) |
| Moist | **−1.92 %/°C** (0.87 %) | −1.38 %/°C (0.83 %) | −0.91, −0.77 %/°C (0.70 %) |

- **The tip asks for 2 %/°C in dry soil and in moist.** That is the figure the receiver already
  uses, and the figure for ion mobility in water.
- **It wants no delay worth having.** The tip made 30, 60 or 120 minutes late leaves 1.22, 1.30
  and 1.52 % on the dry day against 1.29 % as read, and 1.06, 1.21 and 1.44 % against 0.87 % in
  the moist stretch.
- **The die has no coefficient of its own.** −0.93 %/°C on one stretch, −1.38 on the next, the
  wrong sign on the settling day.
- **On the full dry day the die adds nothing once the tip is in:** 1.24 % left against 1.29 %,
  with a die term of +0.31 %/°C. So no separate board-temperature term shows in the resistance
  there. In the moist stretch the two share the coefficient between them; fifteen hours with
  one cooling and one warming cannot split two temperatures that move together.
- **This replaces an inference of 2026-10-10.** `MSV4_SOIL_THERMAL_SECOND_NIGHT_2026-10-10.md`
  has "the tip thermistor is not reading the temperature of whatever sets the resistance",
  drawn from 10-09. That was the settling day. On settled soil the resistance follows the tip
  in step at 2 %/°C.

## What each correction leaves (measured)

| Correction | Dry, settled | Moist |
|---|---:|---:|
| None | 8.90 %, 22.1 g | 8.55 %, 21.3 g |
| **Tip, 2 %/°C (what the receiver does)** | **3.25 %, 8.1 g** | **3.23 %, 8.0 g** |
| Die as read, 2 %/°C | 11.26 %, 28.0 g | 4.63 %, 11.5 g |
| Die as read, the dry day's best (0.93 %/°C) | 7.81 %, 19.4 g | 4.13 %, 10.3 g |
| Die as read, the moist stretch's best (1.38 %/°C) | 9.20 %, 22.9 g | 2.28 %, 5.7 g |
| Die filtered 120 min, 2 %/°C | 3.28 %, 8.2 g | 3.19 %, 7.9 g |

- **The die as read is worse than no correction on the dry day at 2 %/°C** (28 g against 22 g),
  and its own best coefficient there still leaves 19 g.
- **The 5.7 g is the one number here fitted to the hours it is judged on**, and they are not a
  whole day. The same die with the dry day's coefficient leaves 10.3 g there, and its moist
  coefficient leaves 22.9 g on the dry day.
- **The filtered die matches the tip on both**, with a filter fitted only on the dry day.
- On the settling day nothing works: 34 g uncorrected, 41 g from the tip, 49 g from the die,
  42 g from the filtered die. That day's movement was not temperature.
- The 8 g that the tip leaves is not temperature either sensor sees.

## What the thermistor gives that a filtered die does not

- **The level after a watering.** 0.6 °C is 1.2 % of resistance, 52 units, 3 g (the offset is
  measured, the rest follows from 2 %/°C).
- **Agreement between probes** (data sheets, not measured on these boards):

  | Sensor | Stated accuracy | In the moisture number at 2 %/°C |
  |---|---|---:|
  | nRF52840 die (`nRF52840_PS_v1.1`, 6.28.2.1) | accuracy ±5 °C; offset at 25 °C ±2.5 °C; resolution 0.25 °C | 24 g; 12 g |
  | NTC NCP15XH103F03RC, 10 kΩ ±1 %, B25/50 3380 K ±1 %, against a 1 % 10 kΩ | about ±0.4 °C near room temperature (0.26 °C for each resistor, 0.1 °C for B at 10 °C from 25) | 2 g |

  This is a fixed offset per board. It does not show in one probe's own trace; it is a shift in
  level between two probes read on one scale. Board 1's die happens to sit within 0.4 °C of its
  tip (the filter's −0.39 °C).
- **No history.** A two-hour filter is wrong until it has two hours of unbroken readings behind
  it. Board 1 restarted twice on 10-11, and the receiver loses packets in bursts.
- **Nothing of the board's own.** The die is at the end in the air: whatever warms it at midday
  moves it half as far again as the soil (above), and the chip's own work shows in it (28.75 °C
  on the first wake after the 13:03 update against 27.75 °C before).

Not tested: outdoors, direct sun on the pot, a pot of another size (the two hours are this
pot's), water much colder than the room.

## What it costs to keep

By the design BOM (`MSV4_PROBE_BOM_2026-09-16.md`): the thermistor ($0.027), a 10 kΩ 1 %
reference and two 1 nF C0G bypass capacitors on its traces. On the board it takes one GPIO
(`TEMP_READ`) and one ADC input (AIN1). Per wake: a 1 ms settle and two 16-sample reads. The
maintainer epoxied over it by hand on 2026-10-08.

## Limits

- One board, one pot, indoors, 2.7 days: one settled dry day and 15.5 moist hours. The filter
  was fitted on one day and tried on 20 and 15.5 other hours.
- The moist stretch is not a whole day; a second moist day (from 19:14 on 10-11) repeats this
  on a full cycle.
- Both temperatures are sent in 0.25 °C steps, and one step is 21.7 units of the moisture
  number, 1.2 g. The tip could be sent finer (the ADC resolves it); the die cannot (0.25 °C is
  its own resolution).
