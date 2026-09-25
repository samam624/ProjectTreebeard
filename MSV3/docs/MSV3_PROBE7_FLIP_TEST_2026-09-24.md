# Probe 7 flip test: does facing the soil fix the Begonia probe? — 2026-09-24

**Status: running. Flipped 2026-09-24 ~23:17 PDT.** The maintainer flipped probe 7 so its sensing
face points into the Begonia soil instead of the pot wall. The maintainer reported it at 23:19. In
the telemetry, the last pre-flip row is 23:16:38, followed by an off-cadence wake at 23:17:18 where
counts step 34.36M → 36.84M (about −14 logC%, typical of reinsertion air gaps). Recorded in
`fleet.json` under `probes[7].physical_state.orientation_change`.

The first sun arrival after the flip (09-25 ~11:50) falls 12.5 h in, inside the 24 h settling
window, so it is reported as provisional only. **09-26 and 09-27 are the first days that count.**

## Why

The MSV3 blade is one-sided. On 2026-09-24 the maintainer observed that probe 7 appears to face
its pot wall, not the soil inside the pot. Probe 7 has the worst behaviour in the fleet:

- **A small moisture response.** Its 09-22 watering step is +5.1 logC% once the sun is modelled
  out, about a tenth of probe 5's
  ([findings §3](MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md#3-probe-7-did-register-the-begonia-watering)).
- **A large, slow response to the noon sun**, larger than its watering response.

Facing the wall predicts both at once: less soil in front of the blade, and a sun-warmed pot wall
in its place. A second explanation is still open. Probe 7 is one of the two blade + FDC probes that
were actively charging from solar when their sun response was large. The flip changes orientation
only, so it tests the first explanation. If the sun response survives the flip, cover the panel
next (see [findings](MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md#why-is-probe-5-better-power-and-facing-direction)).

## Related physical fact: probe 5 is on the shade side

The maintainer reports that probe 5 sits on the opposite side of the Alocasia pot from the sun,
because of its USB cable, and every other Alocasia probe sits on the sun side. The data agree.
On the same sunny days probe 5's die swings **5.3–6.0 °C** and probe 4's swings **7.5–9.5 °C**.
Probe 5's flatness is therefore partly its position. Every comparison of probe 5 against sensors
2, 3, 9 or probe 4 mixes electrode/readout with pot side. That includes the watering review's "the
blade is net better through the FDC", which compares probe 4 with probe 5.

## Baseline (before the flip)

Per sunny day, from
`analyze_multiprobe_sun_response.py <dir> --per-day 7` on data through 2026-09-24 22:55 PDT.
*Response* is the mean change 1.5–2.5 h after the sun reaches the shelf, with the pre-arrival trend
removed. *Sun drive* is probe 4's die minus the desk die over the same window. Dividing by it puts
cloudy and sunny days on one scale.

| date | response, logC% | sun drive, °C | **per °C of sun** | own die range | daily span, logC% |
|---|---:|---:|---:|---:|---:|
| 09-19 | +10.11 | 5.73 | +1.76 | 9.75 °C | 12.46 |
| 09-20 | +2.80 | 3.05 | +0.92 | 6.25 °C | 7.05 |
| 09-21 | +6.24 | 3.96 | +1.57 | 8.75 °C | 9.62 |
| 09-23 | +9.48 | 4.75 | +2.00 | 9.62 °C | 12.31 |
| 09-24 | +9.78 | 4.32 | +2.26 | 9.50 °C | 13.90 |

**Before the flip: +0.92 to +2.26 per °C of sun (mean +1.70), daily span 7.1–13.9 logC%.** For
scale, probe 5 is −0.06 to +0.30 per °C and probe 4 is −0.09 to +0.18 per °C on the same days.

## How to read the result

After the flip, the probe needs to settle in the soil. The analysis skips any sun arrival within
24 h of the flip time. Then, per sunny day after that:

| per °C of sun, every post-flip day | reading |
|---|---|
| below +0.92 (the pre-flip minimum) | **improved**; facing direction matters |
| near probe 5's −0.1..+0.3 | facing was essentially the whole effect |
| inside +0.92..+2.26 | no change; facing was not the cause, so cover the panel next |

The moisture half of the theory, that facing the soil makes the watering response larger, can
only be tested at the next watering. At that watering, record the water volume for **both** pots
and the time. The baseline to beat is **+5.1 logC%**.

## Running it after the flip

```
python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor N --raw \
    --since-hours 300 --csv <dir>/treebeard_alocasia_sN_<date>.csv      # N = 2 3 4 5 6 7 9
python MSV3/scripts/analyze_multiprobe_sun_response.py <dir> --suffix <date> --no-figures \
    --per-day 7 --split "2026-09-24 23:17"
```

Probes 4 and 6 must be in the fetch, because their dies supply the sun drive. The flip itself
should show as a handling transient in probe 7's trace. Use it to confirm the recorded time, not
to replace it.
