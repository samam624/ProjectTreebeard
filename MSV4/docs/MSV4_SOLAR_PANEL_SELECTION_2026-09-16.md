# MSV4 solar panel selection and measured recharge budget (2026-09-16)

**Decision:** keep the BQ25504 and its existing 78% MPPT network. For the next small-batch V4
prototype, use **two 1.5 V / 60 mA / 30 x 30 mm panels in parallel**. For production, source one
panel of approximately **2.5 V / 60 mA / 150 mW** in the existing 30 x 54 mm envelope. Keep the
solar-input clamp footprint, but the 1.5 V parallel array should never drive it into conduction.

This record supersedes the earlier assumption that a clamped copy of the existing 6 V / 30 mA panel
has ample recharge margin. It is electrically safe, but the measured fleet data shows that its
energy margin is thin.

## The requirement is constant area and power, not constant current

The existing panel measured about 5 V loaded and 30 mA short-circuit in direct sun, approximately
150 mW. If voltage is halved while current is held at 30 mA, available power is also halved. The
same-area lower-voltage replacement must trade series cells for wider cells or parallel strings:

```
existing:       5.0 V x 30 mA = 150 mW
desired single: 2.5 V x 60 mA = 150 mW
```

That is the physical target. A lower-voltage panel does not inherently cost power when the same
active silicon area is rearranged correctly.

## What probe 4 actually harvested

Source window: probe 4 after 2026-08-30 through the unplug, excluding the three labelled USB
interventions. `data/sensor_logs/probe4_battery_mv.png` is the saved plot of the Supabase raw
`raw_battery_mv` export. The night-only load calculation is documented in
`MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`.

The 150 mAh LiCoO2 curve gives, with its already-documented +/-30-40% model uncertainty:

| quantity | probe 4 result |
|---|---:|
| recent load | **3.03 mAh/day**, 528 uW average |
| old-panel gross charge delivered to the cell | **about 8-9 mAh/day** typical |
| old-panel net cell gain | **about 5-6 mAh/day** |
| ordinary uninterrupted gross range | approximately **5.8-12 mAh/day** |

This predicts about 21-27 days to move a 150 mAh cell through most of its usable capacity, consistent
with the maintainer's observed two-to-three-week recharge.

Scaling the old result to the existing panel behind a 2.75 V clamp leaves only about 45-55% of the
old harvest. Gross charge becomes about 3.6-4.8 mAh/day; after the 3.03 mAh/day load, only about
0.6-1.8 mAh/day remains. Weak days are flat or negative. That is energy-positive in the observed
window on average, but it is not a production margin.

The 60 mAh EHAO 301020 cell therefore does not make the clamped 30 mA panel more energy-neutral; it
only reduces the energy buffer to about 20 no-sun days. Panel power must be restored.

## Small-array cost comparison

Prices are snapshots on 2026-09-16 and exclude tax/shipping.

| option | wiring / rating | envelope | small-lot material cost per probe | useful power into BQ25504 | verdict |
|---|---|---:|---:|---:|---|
| existing 6 V / 30 mA plus clamp | one panel | 30 x 54 mm | about $1 historical | about 55-65 mW | safe, recharge margin too thin |
| commodity 3 V / 50 mA | one panel | 30 x 40 mm | about $2-2.50 | about 105 mW behind the clamp | workable, not full old margin |
| **two 1.5 V / 60 mA in parallel** | **1.5 V / 120 mA, 180 mW nameplate** | **30 x 60 mm** | **$1.27-$1.96** | **about 140 mW; BQ input-current limited in strongest sun** | **small-batch recommendation** |
| BSP-002-style single | 2.5 V / 60 mA, 150 mW | 21 x 55 mm | quote; published source MOQ 1000 | about 140 mW | production electrical target |
| 3 V / 70 mA single plus clamp | 3 V / 70 mA, 210 mW nameplate | 30 x 55 mm | quote | about 150 mW at the clamped 2.15 V operating point | production fallback |
| two 3 V / 50 mA in parallel | 3 V / 100 mA, 300 mW | at least 30 x 80 mm | about $4-5 | about 215 mW | larger and unnecessary |

Commodity 1.5 V / 60 mA panels:

- AliExpress price snapshot: 10 for $6.37, or **$1.27 per two-panel probe**:
  <https://ms.pricearchive.org/aliexpress.com/item/1005012748330851>
- Walmart marketplace snapshot: 10 for $9.79, or **$1.96 per two-panel probe**:
  <https://www.walmart.com/browse/home-improvement/renewable-energy/drevon/1072864_1087352/YnJhbmQ6RHJldm9u>

The production-sized single-panel references are:

- Blue Solaria BSP-002: 2.5 V at MPP, 60 mA, 3.0 V `V_OC`, 21 x 55 x 2.3 mm, 150 mW; published
  MOQ 1000 and no posted price:
  <https://www.bluesolaria.com/2-5V-60mA-Mini-Solar-Panel.html>
- Huaxu catalogue: 55 x 30 mm / 3 V / 70 mA and 60 x 30 mm / 3 V / 80 mA epoxy options:
  <https://www.huaxusolar.com/download/solar-panel-specifications/>

## Why the prototype panels go in parallel

Two 1.5 V panels in series would produce about 3 V at 60 mA, but their combined open-circuit voltage
would likely exceed the BQ25504's 3 V recommended input limit and would make the clamp set the sampled
MPP again. Parallel connection instead gives approximately 1.5 V at 120 mA. The BQ25504 is a boost
harvester and operates down to 130 mV after cold start, so this is its natural operating region.

With the existing 78% MPPT ratio, an expected approximately 1.8 V open circuit becomes about 1.4 V
loaded, close to the panels' useful point. The BQ25504 accepts at most 100 mA average input current
(230 mA typical peak inductor current), so the pair clips in the strongest direct sun at about
140 mW. In window light the panel current is lower and the IC uses the full available array. The
clipping also holds maximum battery charge near 30 mA after conversion, about 0.5 C for a 60 mAh
cell; the cell datasheet must still explicitly permit that rate.

Expected performance, using probe 4's real placement and allowing conversion loss, is roughly
7-8 mAh/day gross and 4-5 mAh/day net after the probe load. A 60 mAh cell would recover from a deep
discharge in roughly two weeks under the same lighting, instead of one-to-three months with the
clamped 30 mA panel.

The array's costs are a 6 mm taller carrier, one additional encapsulated part and two additional
solder joints. In a manufactured product the single panel is preferable for assembly, sealing and
reliability; in a five-probe prototype batch the array is dramatically cheaper and immediately
available.

## V4 implementation

1. Extend the prototype panel carrier from 54.4 mm to at least 60.4 mm while keeping its 30.4 mm
   width.
2. Wire both panel positives to `SP+` and both negatives to `GND`. Do not put the panels in series.
3. Retain the low-leakage clamp footprint as DNP insurance. A healthy 1.5 V array cannot turn it on.
4. Before connection, measure each panel's direct-sun `V_OC` and `I_SC`; reject mismatched panels.
5. Measure the parallel pair. Expected direct-sun `V_OC` stays equal to one panel and `I_SC`
   approximately doubles.
6. Confirm the 60 mAh cell permits at least a 30 mA charge current.
7. Log `raw_battery_mv` for at least three complete window days and compare gross/net daily charge
   with the probe 4 baseline above.

For production quotes, ask for a single panel with approximately 150 mW at MPP, `V_OC` no higher
than 2.7 V across the specified cold-temperature range, and an envelope no larger than 30 x 55 mm.
The BSP-002 is the electrical/layout reference, not an approved part: its 3.0 V `V_OC` at 25 C has no
cold margin and still needs the clamp unless the supplier guarantees the maximum over temperature.
