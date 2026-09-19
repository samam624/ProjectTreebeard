# MSV4 solar panel selection and measured recharge budget (2026-09-16)

> **Superseded on the panel choice (2026-09-17).** The maintainer ordered a single 2 V / 60 mA /
> 0.12 W, 36 x 30 mm panel instead of the two-panel 1.5 V array, keeping the V3 30 x 55 mm envelope.
> The probe 4 recharge budget below still stands. Selection, expected harvest and acceptance test:
> `MSV4_PROBE_BOM_2026-09-16.md`, panel section.

**Decision:** keep the BQ25504 and its existing 78% MPPT network. The best low-volume economic
A/B-test part is one commodity **1.5 V / 160 mA / 0.24 W / 60 x 30 mm epoxy panel** per probe.
Walmart listed a Uxcell ten-pack for $15.99 with free US shipping on 2026-09-16: $1.60 per probe, or
$31.98 before tax for 20. This is 6 mm longer than the current approximately 30 x 54 mm panel, so it
requires a small carrier/enclosure extension but no extra solar-panel wiring or series loss.

The BQ25504 transfers at most 100 mA average input current. Near the commodity panel's approximately
1.4-1.5 V loaded operating point, that deliberately clips strongest-sun input around 140-150 mW,
approximately the old panel's measured 150 mW. In weaker light the panel is below the current limit
and its extra available current remains useful. Remove the 1N4728A for this panel test and leave the
existing MPPT divider unchanged initially.

The $8.72 ANYSOLAR panel remains a well-specified electrical reference, not a cost-acceptable MSV4
part. The preferred eventual custom/OEM geometry is about 45 x 34 mm, 2.0 V / 90 mA at MPP and no
more than 2.4 V open circuit, if a supplier will quote it at $2 or less in lots of 20.

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
| **commodity 1.5 V / 160 mA single** | **0.24 W nameplate** | **60 x 30 x about 3 mm** | **$1.60 delivered in Walmart ten-pack** | **about 140-150 mW; BQ input-current limited in strongest sun** | **small-batch recommendation** |
| Huaxu HX45-34E or equivalent | 2.0 V / 90 mA MPP; 2.4 V `V_OC`; 180 mW | 45 x 34 x 2.5 mm | quote required | about 165-175 mW | preferred OEM geometry if quote is at most $2 |
| ANYSOLAR KXOB201K04TF-TR | 2.23 V / 78.7 mA at MPP; 2.76 V `V_OC`; 175.6 mW | 23 x 42 x 1.5 mm | $8.72 at DigiKey | approximately 160 mW before battery-side loss | excellent electrically, rejected on cost |
| two 1.5 V / 60 mA in parallel | 1.5 V / 120 mA, 180 mW nameplate | 30 x 60 mm | $1.27-$1.96 | about 140 mW; BQ input-current limited in strongest sun | obsolete versus the single 160 mA panel |
| BSP-002-style single | 2.5 V / 60 mA, 150 mW | 21 x 55 mm | quote; published source MOQ 1000 | about 140 mW | production electrical target |
| 3 V / 70 mA single plus clamp | 3 V / 70 mA, 210 mW nameplate | 30 x 55 mm | quote | about 150 mW at the clamped 2.15 V operating point | production fallback |
| two 3 V / 50 mA in parallel | 3 V / 100 mA, 300 mW | at least 30 x 80 mm | about $4-5 | about 215 mW | larger and unnecessary |

Immediately purchasable commodity 1.5 V / 160 mA panels:

- Walmart marketplace Uxcell ten-pack: **$15.99 delivered**, or $1.60 each; 1.5 V, 160 mA,
  0.24 W and 60 x 30 mm:
  <https://www.walmart.com/ip/1427730385>
- CQRobot single: **$1.88 before shipping**, in stock; the listing identifies the panel as
  polycrystalline, epoxy encapsulated and rated at 0.24 W:
  <https://www.cqrobot.com/index.php?product_id=2539&route=product/product>

The marketplace panel has no independent curve or useful lot-tolerance data. Buy one ten-pack first,
measure all ten, and treat the second ten-pack as contingent on the sample passing. The advertised
rating is not production acceptance evidence.

Older two-panel 1.5 V / 60 mA fallback sources:

- AliExpress price snapshot: 10 for $6.37, or **$1.27 per two-panel probe**:
  <https://ms.pricearchive.org/aliexpress.com/item/1005012748330851>
- Walmart marketplace snapshot: 10 for $9.79, or **$1.96 per two-panel probe**:
  <https://www.walmart.com/browse/home-improvement/renewable-energy/drevon/1072864_1087352/YnJhbmQ6RHJldm9u>

The production-sized single-panel references are:

- Huaxu HX45-34E: 2.0 V / 90 mA at MPP, 2.4 V `V_OC`, 180 mW, 45 x 34 x 2.5 mm and -40 to
  +80 C. This is a published specification but has no posted small-quantity price:
  <https://www.huaxusolar.com/wp-content/uploads/2023/05/Epoxy-solar-panel-45x34MM-2V-90mA.pdf>
- ANYSOLAR KXOB201K04TF-TR: 2.23 V at MPP, 78.7 mA, 2.76 V `V_OC`, 23 x 42 x 1.5 mm,
  175.6 mW, active and stocked in cut tape. At the existing 77.9% MPPT ratio, its sampled target is
  2.15 V, only 3.6% below the published 2.23 V maximum-power voltage:
  <https://www.digikey.com/en/products/detail/anysolar-ltd/KXOB201K04TF-TR/13999209>
- Blue Solaria BSP-002: 2.5 V at MPP, 60 mA, 3.0 V `V_OC`, 21 x 55 x 2.3 mm, 150 mW; published
  MOQ 1000 and no posted price:
  <https://www.bluesolaria.com/2-5V-60mA-Mini-Solar-Panel.html>
- Huaxu catalogue: 55 x 30 mm / 3 V / 70 mA and 60 x 30 mm / 3 V / 80 mA epoxy options:
  <https://www.huaxusolar.com/download/solar-panel-specifications/>

## This is a known BQ25504 panel-matching problem

Other BQ25504 users have run into solar-panel voltage being too high for `VIN_DC`, although no
credible public report was found that exactly reproduces the MSV3 observation that `VIN_DC` stayed
high while `VIN_DC > VSTOR > VBAT_OV`. In a resolved TI E2E thread where a panel could exceed the
5.5 V absolute maximum, TI recommended clamping the input with a Zener and changing the MPPT
setting:

<https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/625021/bq25504-output-voltage-of-solar-panel-exceeds-input-voltage-of-abs-max>

In a later thread, a user measured 3.2 V at `VIN_DC` from a 3.4 V-open-circuit panel and could not
charge properly. TI said that panel voltage was too high for the 3 V recommended input limit and
told the user to enable the resistor-divider MPPT network outdoors:

<https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1421057/bq25504-battery-not-charging-mppt-disabled>

TI's current BQ25504 datasheet specifies 3.0 V maximum recommended `VIN_DC`, 5.5 V absolute maximum,
300 mW maximum input power after cold start, and says the configured `VBAT_OV` must be higher than
`VIN_DC`. It also says the charger is disabled at `VBAT_OV`; that does not prove that the undocumented
input pull-down will safely absorb this panel under the exact MSV3 condition:

<https://www.ti.com/lit/ds/symlink/bq25504.pdf>

The forum Zener answer prevents destructive overvoltage, but it does not promise good harvesting
efficiency. The 1N4728A used on MSV3 is an ordinary 3.3 V power Zener whose voltage is poorly defined
at the panel's small current. The measured approximately 2.82 V sample forces the BQ25504 to regulate
near 2.2 V, far to the left of the old panel's useful operating point. That is consistent with the
observed approximately 47% harvest rather than evidence that a lower-voltage panel must lose power.

## Why passive voltage dropping is the wrong fix

- A series resistor sized to lose 3 V at 30 mA is about 100 ohms and burns about 90 mW. In weak
  light the current falls, the drop disappears and `VIN_DC` rises again.
- Series silicon or Schottky diodes also lose power, and during the BQ25504 open-circuit MPPT sample
  their current approaches zero, so their forward drop collapses and the charger can still sample
  almost the full panel voltage.
- An LDO converts the unwanted voltage to heat and cannot recover the lost current.
- A Zener or precision shunt is appropriate as normally-off protection. If it conducts through
  normal full-sun operation, it necessarily throws away part of the panel's energy.
- A solar-aware buck or buck-boost converter could preserve the energy, but adding one ahead of the
  BQ25504 complicates cold start and MPPT. If retaining the 6 V panel is a product requirement, a
  future PCB should replace the BQ25504 with a buck-boost harvester such as the SPV1050 rather than
  cascade two harvesters. The SPV1050 supports MPPT and up to 18 V input after cold start:
  <https://www.st.com/en/power-management/spv1050.html>

## Why the selected panel is one 160 mA panel

The selected 60 x 30 mm panel internally devotes more of its active area to parallel current and
less to series voltage. Its 0.24 W nameplate rating is more than the BQ25504 can accept at 1.5 V,
but that surplus is useful rather than harmful: the charger's 100 mA average-input ceiling limits
strongest sun, while lower-irradiance operation can use the larger panel current. One panel also
avoids the assembly, mismatch and sealing cost of two separate panels.

Its six extra millimetres of length are the lowest-cost mechanical tradeoff found. The 45 x 34 mm
Huaxu part is the more compact technical target, but it is not an immediately purchasable $1-2
small-lot part until a sample quote proves otherwise.

## Why the older fallback prototype panels go in parallel

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

## Commodity-panel A/B test

1. Buy one Walmart ten-pack, not all 20 panels, for the first reversible bench comparison.
2. With every cell disconnected, measure direct-sun `V_OC` and `I_SC`; record the range and reject
   outliers. Do not assume the marketplace label is a guaranteed curve. `V_OC` must remain below
   3.0 V, and useful full-sun current should reach at least 100 mA.
3. Remove the conducting 1N4728A. Connect the new panel directly to `SP+`/ground with the existing
   77.9% MPPT divider unchanged.
4. With the battery below 3.89 V, verify loaded `SP+`, panel input current and battery charge current.
   Expected loaded `SP+` is roughly 1.3-1.5 V, but the measured curve controls acceptance. Strong-sun
   input current should approach but not exceed the BQ25504's 100 mA average ceiling.
5. With `VSTOR`/battery above the 3.89 V `VBAT_OV` threshold, verify that active charging stops. `SP+`
   may rise toward panel `V_OC`, but it must remain below the battery so the boost high-side body diode
   cannot conduct into the battery.
6. Confirm the installed cell's datasheet permits the measured peak charge current. Theoretical
   full-sun battery-side current is roughly 29-34 mA at 3.9 V after conversion loss.
7. Log `raw_battery_mv` for at least three complete window days and compare gross/net daily charge
   with the probe 4 baseline above.
8. Cold-soak one sample and remeasure `V_OC` before production approval. If it can exceed 3.0 V,
   reject that lot rather than making a shunt conduct during ordinary sun.
9. Extend the carrier from 54.4 mm to at least 60.4 mm while retaining about 30.4 mm width.

For production quotes, ask for a single panel with approximately 150 mW at MPP, `V_OC` no higher
than 2.7 V across the specified cold-temperature range, and an envelope no larger than 30 x 55 mm.
The BSP-002 is the electrical/layout reference, not an approved part: its 3.0 V `V_OC` at 25 C has no
cold margin and still needs the clamp unless the supplier guarantees the maximum over temperature.
