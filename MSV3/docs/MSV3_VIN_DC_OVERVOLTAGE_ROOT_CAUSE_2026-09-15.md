# MSV3 VBAT_OV never worked: the panel is 6 V and the boost has a body diode (2026-09-15)

**Status: root cause identified from the datasheet and confirmed against fleet telemetry. Fix is a
one-part bodge. Not yet installed.**

The BQ25504's battery overvoltage protection has never bounded the cell on any MSV3 probe. It is
not a resistor value, not a solder joint, and not contamination. The 6 V solar panel exceeds the
part's input rating, and a boost converter physically cannot stop current flowing from a higher
input to a lower output.

## The two datasheet violations

From SLUSAH0G rev G (August 2023), page 6, **7.3 Recommended Operating Conditions** — read off the
rendered page, not extracted text, because this table's footnotes do not survive extraction:

| parameter | spec | this design |
|---|---|---|
| `V_IN(DC)` into VIN_DC | MIN 0.13, **MAX 3 V** | **6 V** panel `V_OC` |
| Input voltage abs max (7.1) | **5.5 V** | **6 V** |
| footnote (2) | **"VBAT_OV setting must be higher than VIN_DC"** | 3.887 V < 6 V |
| `R_OV1 + R_OV2` | **MIN 9 / NOM 10 / MAX 11 MΩ** | 8.54 MΩ after the 2026-09-14 rework |

The panel figure is this project's own measurement, `MSV3_BENCH_TEST_PLAN_2026-08-21.md`:
*"Measured in real sun. Bare panel: V_OC = 6V, I_SC = 30mA."*

The BQ25504 periodically open-circuits the input to sample `V_OC` for MPPT, so VIN_DC really does
reach the full 6 V, repeatedly — above the **absolute maximum**, which is a permanent-damage rating.

## The mechanism: a boost converter cannot buck

The BQ25504 is a boost converter. Power path is `VIN_DC -> LBST -> switch node -> VSTOR`, and the
high-side synchronous rectifier is a PMOS with an intrinsic body diode from the switch node to
VSTOR.

When `VIN_DC > VSTOR + Vf`, current flows:

```
panel -> VIN_DC -> inductor -> PMOS body diode -> VSTOR -> internal PFET -> VBAT
```

That path is **passive**. No control loop, no comparator, no enable bit. The OV comparator can stop
the switching; it cannot open a diode. The programmed `VBAT_OV` is simply bypassed.

This is why TI's own CAUTION in section 8.3.3 says that in this condition the part "pulls VIN_DC to
ground through a small resistance" and requires the source impedance to exceed 20 Ω. Shorting the
source is the only move available to the silicon, because it cannot open the path.

### What this predicts, and what the telemetry shows

| prediction | observed |
|---|---|
| ceiling is set by the panel, not by `VBAT_OV` | probe 6 charged at 4.25 V with a 3.887 V ceiling programmed |
| changing `VBAT_OV` changes nothing | 4.26 V -> 3.887 V rework had no effect on charge behaviour |
| charging tapers with sunlight, not at a threshold | 09-15 afternoon: +11.0, +14.5, +13.6, +6.3, +0.3 mV/hr |
| every other function is unaffected | cold start, MPPT at 78 %, daily charging all work normally |
| affects every probe with this panel | probes 4 and 6 both |
| the cell keeps climbing day over day | probe 6: 4365 -> 4481 -> 4487 mV across 09-08 to 09-14 |

**The cell reached 4.49 V.** What stopped it was the panel running out of current, not any
protection. With 6 V in, the diode path can in principle drag VBAT toward ~5.4 V.

The battery ADC is trustworthy here: DMM read 4.25 V against a telemetry reading of ~4293 mV at the
same moment, so `raw_battery_mv` is about **+1.0 %** high. Probe 4 independently agrees (capped at
4309 mV for a nominal 4.259 V ceiling, +1.2 %).

## Measured at SP+ (2026-09-16): 5.0 V in sun, 4.1 V at the window

Maintainer, DMM on `SP+` with the panel **connected to the board** (loaded):

| condition | loaded `SP+` = VIN_DC |
|---|---:|
| outdoors, direct sun | **5.0 V** |
| indoors, against the window the fleet is actually deployed at | **4.1 V** |

**This closes the direct-confirmation item.** VIN_DC is not near the 3 V recommended maximum, it is
1.4x to 1.7x above it, and the 6 V `V_OC` seen at every MPPT open-circuit sample is above the 5.5 V
absolute maximum. The root cause is confirmed at the pin.

**The window number is the one that changes the picture.** Every previous figure in this project was
a full-sun figure, and the argument had an implicit "this only happens when the probe goes outside."
It does not. 4.1 V at the window is above any state of charge a 1S cell can reach, so the PMOS body
diode path is forward-biased **all day, every day, in normal indoor deployment**. Probe 6 did not
need to see direct sun to reach 4.49 V.

It also means the BQ25504 has been run continuously at ~1.4x its recommended input maximum since
deployment, not intermittently. That raises the prior on the "damaged part" branch of the
post-clamp verification below.

### The 4.1 V is a spot reading, and the cell already recorded the peak

4.1 V was one DMM reading at one moment; the window gets brighter through the day and the loaded
`SP+` rises with it. **The peak does not need to be measured — probe 6's battery is a peak-hold
recorder of it.**

Probe 6 reached 4487 mV telemetry. Correcting the ADC's known +1.0 % gives a true cell voltage of
~4442 mV, and the body-diode path holds VBAT at roughly `SP+` minus 0.3 V, so:

```
SP+ peak at the window  ~=  4.44 V + 0.3 V  ~=  4.75 V
```

**The window peaks at ~4.75 V, against 5.0 V in direct sun.** Indoors is not a milder condition; it
is ~95 % of the full-sun condition. The whole "this only bites outdoors" framing is wrong, and 4.1 V
understates the daily exposure by about 0.65 V.

**Confirmed the same day: 4.7 V measured at the window in brighter light.** The prediction was
4.7-4.8 V. The window runs at ~94 % of the direct-sun condition.

### The ceiling is arithmetic, and it closes the loop

The two mechanisms are not competing — one sets the other. MPPT holds VIN_DC at its programmed
fraction of the sampled `V_OC`, and the body diode then drags the cell up to just under that:

```
VIN_DC       =  MPPT ratio x V_OC     =  0.78 x 6.0 V  =  4.68 V     (measured 4.70 V)
VBAT ceiling =  VIN_DC - Vf(body)     =  4.68 - 0.3    =  4.38 V     (measured ~4.44 V)
```

Both within 60 mV of measurement. **The cell's resting ceiling on this hardware is
`0.78 x V_OC - 0.3 V`**, and it has nothing whatever to do with `VBAT_OV`. That single expression
reproduces every charge observation in this document, and it is the quantitative form of the root
cause.

It also prices the "use a lower-`V_OC` panel" item exactly: a 3 V panel gives a 2.34 V input and a
2.04 V diode ceiling, harmlessly below `VBAT_OV`, which is why that change removes the failure mode
outright rather than merely reducing it.

### The clamp interacts with MPPT — the power table below is optimistic

**Non-obvious consequence, and it matters for part choice.** The BQ25504 sets its operating point by
periodically open-circuiting VIN, letting it rise to `V_OC`, and storing a fraction of *that* on
`VREF_SAMP`. A shunt zener is across the panel, so it clamps the open-circuit excursion too — which
means the part samples `V_OC` = `Vz`, not 6 V, and then regulates down to 78 % of the clamp:

| clamp `Vz` | sampled `V_OC` | actual operating point | diode-path ceiling |
|---:|---:|---:|---:|
| 3.3 V | 3.3 V | **2.57 V** | 2.27 V |
| 3.0 V | 3.0 V | **2.34 V** | 2.04 V |
| 2.5 V | 2.5 V | **1.95 V** | 1.65 V |

The diode ceiling lands far below `VBAT_OV` in every row, so the fix works — the cell charges through
the boost under `VBAT_OV` control, as designed. **But the panel ends up operating at 78 % of the clamp
voltage, not at it**, so the harvest table further down overstates post-clamp power by that factor:

| clamp | table below says | actually | margin vs 528 uW load, in sun |
|---:|---:|---:|---:|
| 2.5 V | 75 mW | **58 mW** | 111x |
| 3.0 V | 90 mW | **70 mW** | 133x |
| 3.3 V | 96 mW | **77 mW** | 146x |

Irrelevant in sunlight. **Possibly not irrelevant at the window**, which is the whole point of the
missing `I_SC` measurement: at 2.57 V, a window delivering 1 mA gives 2.6 mW (~4.9x the load), and a
window delivering 300 uA gives 771 uW — **1.5x, which is not a margin.** This is now the real risk in
the clamp plan, and it is decidable with one DMM reading.

**So: re-choose the MPPT divider when the clamp goes in.** Once a clamp is fitted, MPPT is no longer
tracking a real maximum power point — it is tracking the zener, and the 78 % ratio is simply throwing
away 22 % of the voltage for nothing. Raising the ratio recovers it directly: at 90 % a 3.3 V clamp
operates at 2.97 V instead of 2.57 V, +16 % harvest, with the diode ceiling still only 2.67 V. The
`RMPPT1/2/3` parts are already on the board.

### The MPPT divider is also a fallback fix on its own

If no suitable zener is to hand, the same arithmetic offers a resistor-only route: **lower the MPPT
ratio** so the regulated input never exceeds `VBAT_OV`. At 50 %, VIN_DC = 3.0 V, the diode is
reverse-biased against a 3.887 V cell, and `VBAT_OV` takes control again.

**It is the weaker fix and should not be the only one.** It leaves VIN_DC at the full 6 V during every
`V_OC` sample, so the 5.5 V *absolute maximum* violation continues — roughly a 256 ms sample every
16 s, about 1.6 % duty, forever. The zener is across the panel and kills that excursion too, which is
the one thing only it can do. Fit the zener; treat the divider as the harvest-recovery knob that goes
with it, or as a stopgap if parts are unavailable.

### Which mechanism sets the loaded 4.1 V — and how to tell

Two candidates, and this measurement alone does not separate them:

1. **MPPT is setting it.** The divider samples `V_OC` and regulates VIN to ~78 % of it. 5.0 / 0.78
   = 6.4 V `V_OC` in sun (matches the 6 V measurement); 4.1 / 0.78 = 5.3 V `V_OC` at the window.
2. **The battery is setting it** through the conducting body diode, i.e. `SP+` ~ VBAT + Vf. At the
   window that implies a cell at ~3.7-3.8 V at the moment of measurement.

A starved panel collapses instead of doing either — under the bright-LED proxy on 2026-08-21, loaded
`SP+` read 0.74 V against a 4.62 V `V_OC`. So the window panel is **delivering real current**; it is
not in the starved regime.

**Discriminator, free:** read `raw_battery_mv` from telemetry at the same moment as the `SP+`
reading. `SP+` - VBAT ~ 0.3-0.4 V means the diode path is conducting and the panel is being clamped
by the cell — the mechanism observed in situ. A gap much larger than that means MPPT is in control
and the diode is off at that instant. Either way the fix is the same; only the daily exposure
estimate changes.

### Consequence for the clamp: the harvest margin was computed in sunlight

The 142x-190x margin table below uses `I_SC` = 30 mA, a **direct-sun** number, against a fleet that
lives at a window. The two indoor current measurements on record are not the window:
`V_OC` = 2.7 V / `I_SC` = 50 uA was taken at a deliberately *dim* indoor spot, and the window must be
far brighter than that to hold 4.1 V loaded.

So the one number still missing is **panel `I_SC` at the window**, and it decides two things:

- **Break-even.** Load is 528 uW / 3.03 mAh per day. Clamped at 3.3 V, break-even is ~160 uA
  continuous; replacing a day's charge over ~8 h of useful light needs ~380 uA average. If the window
  gives >= ~1 mA the clamp is free, as it is in sun.
- **Sub-knee leakage — a real effect, but second-order.** Zeners below ~5.5 V break down by
  *tunnelling*, not avalanche, and tunnelling current is a smooth exponential in field. The knee is
  therefore rounded: a 3.3 V part specified at 1 mA can still pass tens of microamps at 2.5-2.8 V.
  Low-voltage zeners have the softest knees of any zener, so the ones usable here are the worst
  offenders.

  **But this only costs energy at low light.** Once the panel is bright enough to be clamped, the
  zener is at its knee and conducting on purpose — that is the job. Leakage is only a loss when the
  panel's natural loaded point is *below* `Vz` and the diode should be invisible: dawn, dusk, overcast.
  That is the tails of the day, not the bulk of it. Flagging it so the part is chosen with open eyes,
  not because it threatens the design.

  Practical consequence: **specify reverse leakage near `Vz`, not just `Vz`.** Datasheet trap — parts
  in this class quote `I_R` at 1 V, which says nothing about behaviour at 2.5-3.0 V. Because the
  published numbers are close to useless here, the honest move is to **bench the actual part**: bias
  it from a supply at 2.0 / 2.5 / 3.0 V and read the current on a DMM in microamp range. Five minutes,
  settles it for the part in hand.

  The red-LED fallback is better on exactly this axis (sharp turn-on, negligible sub-threshold
  leakage) but pins the panel at ~1.9 V, giving up well over half the window's available power now
  that the window is known to run at 4.1-4.75 V. It is a get-out-of-jail part, not the choice.
- The `TLVH431B` shunt-regulator footprint already reserved in `MSV4/docs/MSV4_PROBE_BOM_DRAFT.md`
  has a much harder knee, but needs ~55-80 uA of cathode bias to regulate plus divider current. In
  sun that is free; at the window it is the same order as the entire harvest. **Do not carry that
  footprint forward as the default without the window `I_SC` number.**

Clamping below the operating point costs power roughly in proportion to voltage, not catastrophically
— a PV cell held below its MPP sits near `I_SC`. Clamping the window's 4.1 V point to 3.3 V keeps
~80 % of the available power. Leakage, not the clamp voltage, is the risk indoors.

## The fix: clamp VIN_DC below the battery

No resistor value and no register setting can defeat a body diode. The **only** fix is to stop
VIN_DC from ever exceeding VBAT.

A shunt zener from `SP+` to GND. Two numbers decide the value:

- **Diode-path ceiling = Vz − 0.3 V.** This is the highest VBAT the uncontrolled path can ever
  reach, regardless of sunlight or whether `VBAT_OV` works at all. It must be a safe cell voltage.
- **Vz × 1.05 must stay below `VBAT_OV` minus its own tolerance.** `VBAT_ACCURACY` is ±5 % even with
  0.1 % resistors, so with `VBAT_OV` = 3.887 V nominal the floor is **3.69 V**, giving **Vz ≤ 3.5 V**.

### Picking from whatever is in the lab

| zener | +5 % worst case | diode-path ceiling | verdict |
|---:|---:|---:|---|
| 2.4 V | 2.52 V | 2.1 V | works, ~135x power margin |
| 2.7 V | 2.84 V | 2.4 V | works |
| 3.0 V | 3.15 V | 2.7 V | **in-spec choice** (inside the 3 V recommended max) |
| **3.3 V** | **3.46 V** | **3.0 V** | **the choice** — 230 mV guaranteed margin to `VBAT_OV` |
| 3.6 V | 3.78 V | 3.3 V | **NO** — worst case exceeds the 3.69 V `VBAT_OV` floor |
| 3.9 V+ | — | — | **NO** — same failure, worse |
| 4.7 / 5.1 V | — | 4.4 / 4.8 V | protects the **chip** only; cell can still be overcharged |

**Anything from 2.4 V to 3.3 V works. Do not go above 3.5 V — and use 3.3 V.**

*Updated 2026-09-16.* The 3.0 V and 3.3 V options used to be near-tied, 3.0 V having the merit of
sitting inside TI's 3 V recommended maximum. The window measurements break the tie: the panel's
operating point indoors is 4.1-4.75 V, so **every 100 mV the clamp gives up is harvest given up in
the condition the fleet actually lives in**, and 3.3 V is the highest legal value. The tolerance
stack that sets the 3.5 V ceiling is untouched by any of the new data — it comes from `VBAT_OV`
accuracy, not from the panel — so the upper bound does not move either.

**Size to order: 3.3 V, 5 %, SOD-123 or larger.** Worst-case dissipation is 100 mW (`Vz` x the 30 mA
direct-sun `I_SC`), so SOD-123's 500 mW is a 5x margin; SOT-23 at 350 mW also clears it. SOD-323 at
~200 mW is thin inside a warm enclosure. Prefer a part that specifies reverse leakage above 1 V; if
none is to hand, bench the one that is.

If no suitable zener is on hand, in rough order of preference:

1. **One red LED** across SP+/GND. `Vf` ≈ 1.8–2.0 V, a very sharp knee and negligible sub-threshold
   leakage. Clamps to ~1.9 V, diode-path ceiling ~1.6 V, still ~110x power margin.
2. **Four to five 1N4148 in series.** ~2.8 V at low current rising to ~3.7 V at 30 mA — a soft knee
   and a poor tempco, but entirely adequate as a bodge and present in every lab.
3. Two red LEDs in series (~3.7 V) — **only** if `VBAT_OV` is raised first; at the present 3.887 V
   setting this fails the same tolerance stack as a 3.6 V zener.

**Package:** see the dissipation section below — the clamp conducts at only a 1.6 % duty cycle, so
99 mW peak in full sun averages to **1.6 mW**. SOD-123 (500 mW) is enormous overkill and SOT-23
(350 mW) clears the *peak* outright. SOD-323 at ~200 mW is also fine. Package is not a constraint.

### What the clamp actually costs: verified against SLUSAH0G rev G

Section 8.3.1, page 16: *"The MPPT circuit obtains a new reference voltage every 16 s (typical) by
periodically disabling the charger for 256 ms (typical) and sampling a fraction of the harvester's
open-circuit voltage."*

That timing is the whole answer, because **the zener is off during normal operation.** MPPT holds
VIN_DC at 0.78 x 3.3 V = 2.57 V, which is below the 3.3 V knee. The clamp only conducts during the
256 ms `V_OC` sample, when VIN_DC is released and tries to climb to 6 V.

| | full sun |
|---|---:|
| conducting voltage x current | 3.3 V x 30 mA = **99 mW peak** |
| duty (256 ms / 16 s) | **1.6 %** |
| **average dissipation** | **1.6 mW** |
| **harvest lost to the clamping action** | **zero** |

The harvest figure is the non-obvious one and it is exact, not an approximation: during the sample
window **the charger is disconnected and harvesting nothing anyway**, so the zener is burning energy
the BQ25504 was already discarding. The clamping action itself is free.

**The real cost is the operating point moving, not the zener conducting.** With the clamp fitted the
part samples `V_OC` = 3.3 V and regulates to 2.57 V, against 4.68 V unclamped:

| | operating point | panel power in sun | fraction of MPP |
|---|---:|---:|---:|
| unclamped (present) | 4.68 V | 126 mW | 100 % |
| clamped, 78 % ratio | 2.57 V | 77 mW | **61 %** |
| clamped, ratio raised to 90 % | 2.97 V | 89 mW | **71 %** |

**And the only continuous loss is leakage.** At night VIN_DC is ~0 and the zener sees nothing, so it
is a daylight-only tax. Budget it against the 528 uW load: at the 2.57 V operating point, **20 uA of
leakage is 51 uW, about 10 % of the load.** That is the number to hold a candidate part to.

**Tempco:** zeners below ~5 V have a negative coefficient, about −1.5 mV/°C at 3.3 V, so −60 mV over
a 40 °C swing. Does not threaten the margin.

### Lab checklist: what to look for in the parts bin

Ordered by preference. Anything in the first two rows can go straight on.

| find | verdict |
|---|---|
| **3.3 V zener**, any package SOD-323 or larger | **fit it** — the target part |
| 3.0 V or 2.7 V zener | fit it; slightly lower operating point, still fine |
| 2.4 V zener | works, ~110x margin, more harvest given up |
| 3.6 V or higher zener | **no** — fails the conservative tolerance stack |
| one red LED | usable bodge; clamps ~1.9 V, gives up over half the window's power |
| nothing suitable | fall back to lowering the MPPT divider ratio (above) |

**Marking codes are not standardised across zener vendors** — unlike the resistors, where the value is
in the part number. A bare SOD-123 marked `W7` or similar tells you nothing without that vendor's
table. **Measure, do not read the marking.**

**The five-minute bench check, which settles both questions at once:**

1. Series a resistor (1 k is fine) from a bench supply to the zener, cathode to supply, anode to GND.
2. Sweep the supply and record the voltage *across the zener* at roughly 1 mA. That is `Vz` — accept
   2.4-3.3 V, reject 3.6 V and up.
3. Back the supply down until the zener sits at **2.5-2.6 V** and read the current. **This is the
   leakage number**, the one no datasheet in this class states usefully. Under ~20 uA is comfortable
   (~10 % of the 528 uW load, daylight only); tens of microamps is acceptable; hundreds means pick a
   different part.
4. Optional: push to ~30 mA and read the voltage. Expect it to sit well above nominal `Vz`. **That is
   fine and slightly helpful** — see the soft-knee section above. It is not a reason to reject.

**Fit it between `SP+` and GND**, cathode to `SP+`. The netlist confirms `SP+` carries only the input
caps, L1, the MPPT divider top and BQ25504 pin 2, so there is no other load to disturb.

### Why losing harvest is not a concern

Measured average load is **528 µW** (below). Panel power against clamp voltage:

| clamp | panel power | margin vs load |
|---:|---:|---:|
| 2.5 V | 75 mW | 142x |
| 3.0 V | 90 mW | 170x |
| 3.3 V | 96 mW | 181x |
| MPP, unreachable | 126 mW | 239x |

The whole range is 142x to 190x margin. The difference between a 3.0 V and a 3.6 V clamp is 10 mW —
nineteen times the entire average consumption of the probe.

**The problem was never too much energy. It is too much voltage.** The same failure would occur with
a 6 V panel delivering 1 mA.

### Verification after fitting

With VIN_DC clamped and the cell above `VBAT_OV`, there must be **zero** charging. Any upward hour in
`raw_battery_mv` during sun is a fail, and would then point at a damaged BQ25504 rather than a
configuration error — plausible after weeks of absolute-maximum excursions on the input pin.

### The clamp cannot touch sleep current (structural, not a matter of degree)

Asked and answered 2026-09-16, because it is the one thing that would have disqualified the fix.

**The zener is on the panel side of the boost, so it is not in the battery's discharge path at all.**
Net checked against `MSV3_sch.NET`:

```
NET SP+ : C4-2, C8-1, L1-1, R9-2, SP+1-1, SP_IC-2
```

Two input caps, the 22 uH boost inductor, the MPPT divider top (R9), the panel pad and BQ25504 pin 2
(VIN_DC). Nothing else. **USB is not on this net** — `VBUS` enters at `Q1-1` and joins `VSTOR`, a
separate net, so the clamp never sees 5 V from USB.

For the cell to drain into the zener, current would have to run VBAT -> VSTOR -> back through the
boost -> L1 -> SP+. That last step is *backwards through the high-side PMOS body diode*, whose anode
is the switch node and cathode is VSTOR. It is blocked. **The same body diode that causes this entire
overcharge problem is what prevents reverse drain** — the asymmetry works in our favour on the way
out.

And at night the panel is dark, VIN_DC sits at ~0 V, so the zener has ~0 V across it. Zener leakage
at zero bias is zero. A zener is also passive: no bias current, no enable, nothing quiescent. (The
`TLVH431B` alternative *does* need 55-80 uA of bias — but even that sits on the panel side and would
be a harvest tax, not a sleep-current hit.)

**Keep the two budgets separate.** The ~20 uA leakage figure quoted above is a *daylight harvest tax*
— it subtracts from what the panel puts in. It never adds to what the battery takes out. Sleep
current is untouched, exactly, by construction.

Junction capacitance is likewise a non-issue: a low-`Vz` zener's ~100 pF sits across C4 + C8, already
14.7 uF.

### The soft knee is mostly a benefit here, not a cost

Follows from the MPPT interaction above and is worth recording because it inverts the earlier concern.

Under load the clamp sits *above* its nominal `Vz` — dynamic resistance times the panel current, which
at 30 mA can be several hundred millivolts. That raises the sampled `V_OC`, which raises the operating
point to 0.78x the higher figure, which **recovers harvest.** The knee softness works the right way
around at high current.

It is also safe, because the 78 % MPPT ratio is a free safety factor the original tolerance stack did
not take credit for. The diode-path ceiling is not `Vz - 0.3` but:

```
ceiling = 0.78 x V_clamp(at load) - 0.3 V
```

Setting that below the 3.69 V `VBAT_OV` floor allows `V_clamp` up to ~5.1 V, against the 3.5 V limit
derived earlier without the MPPT term. **Keep specifying 3.3 V** — the conservative limit is the right
one to design to, since it holds even if MPPT is disabled or `VREF_SAMP` leaks — but a part that
measures 3.8-4.0 V at 30 mA is not a failure, and should not be rejected for it.

## V4: fix the panel, do not carry the clamp forward

**The clamp is a bodge for boards that already exist. It is the wrong thing to design in.** Every
cost it has — the 39 % operating-point haircut, the leakage tax, the 99 mW peak, a part whose knee
has to be bench-characterised — exists only because a 6 V panel is being forced into a 3 V input. On
a new board, buy the right panel instead and all of it disappears at once.

**Target: `V_OC` <= 2.7 V at 25 C.** Two constraints set that:

- The BQ25504's `V_IN(DC)` recommended maximum is **3000 mV** (page 6, verified). Staying under it at
  *open circuit* means the `V_OC` sample is legal too, which is the one thing the clamp exists to fix.
- **Panel `V_OC` rises as temperature falls**, roughly -0.3 %/C for silicon. A 2.7 V panel at 25 C is
  ~2.9 V at 0 C. Specifying 3.0 V at 25 C would put the cold-weather `V_OC` back out of spec — the
  exact trap `MSV4_PROBE_BOM_DRAFT.md` already flagged as *"cold-weather maximum VOC remain unknown."*

Also satisfies footnote (2), `VBAT_OV` > VIN_DC, with ~1.2 V to spare.

**This costs nothing in power.** Panel voltage is set by how many cells are in series; for the same
die area, fewer cells in series gives proportionally lower `V_OC` and higher `I_SC` at the same power.
It is a sourcing decision, not a performance trade — and MPPT then tracks the *real* maximum power
point instead of tracking a zener, so V4 gets **100 % of MPP where a clamped V3 gets 61 %.**

### Low light is fine, and the datasheet says so

Verified from the electrical tables, page 6-7:

| parameter | spec | with a 2.7 V panel |
|---|---|---|
| `VIN(DC)` operating, cold start done | **130 mV** min | operates at ~2.1 V, 16x over |
| `VIN(CS)` cold-start voltage | **600 mV typ, 700 mV max** | clears even in poor light |
| `PIN(CS)` min cold-start power | **15 uW** | — |
| `PIN` min for normal charging | **10 uW** | against a 528 uW load |
| `I_Q` (VSTOR, UV shutdown) | **330 nA typ / 750 nA max** | — |

**Correction worth recording: `VIN(CS)` is 600 mV typical / 700 mV max, not 330 mV.** TI raised it in
a later revision of this datasheet (the revision history logs *"Increased VIN(CS) From: TYP = 330 mV
and MAX = 450 mV To: TYP = 600 mV and MAX = 700 mV"*). The 330 nA figure that looks similar is the
**quiescent current**, an unrelated parameter. `MSV3_BENCH_TEST_PLAN_2026-08-21.md` already uses
600 mV correctly; do not let the old number back in.

**Keep the BQ25504.** 330 nA quiescent, operation down to 130 mV, and a 10 uW minimum input power are
genuinely excellent low-light numbers, and none of this failure was the harvester's fault — it was a
panel/input mismatch. Changing the IC would throw away working layout and firmware to solve a problem
the IC does not have.

### Two things that follow for the V4 BOM

- **Size the panel from the measured window `I_SC`, not from full sun.** Every margin figure in this
  project is a direct-sun figure for a fleet that lives at a window. Sun should be the easy case that
  falls out, not the case the design is dimensioned against.
- **Keep the clamp footprint, DNP.** `MSV4_PROBE_BOM_DRAFT.md` already reserves `TLVH431B` + 2
  resistors. Retain it as insurance against a panel whose real `V_OC` exceeds its datasheet, but with
  a correct panel it should never be populated — and note the TLVH431's ~55-80 uA of bias current is
  itself the same order as an indoor harvest, so it is insurance, not a plan.

## Measured power budget (new, 2026-09-15)

Derived from probe 4's night-only discharge (local 21:00–05:00, skipping the first three hours after
sunset so surface charge relaxes), slope-fitted per night, converted through a LiCoO2 OCV–SOC curve
so `dQ/dV` is taken at each night's actual voltage rather than a flat coefficient. 150 mAh cell.

| period | nights | current | power | daily | runtime, no sun |
|---|---:|---:|---:|---:|---:|
| **recent (09-05 onward)** | 9 | **126 µA** (median 109) | **528 µW** (median 441) | 3.03 mAh | **50 days** |
| early (through 08-28) | 5 | 693 µA | 2707 µW | 16.6 mAh | 9 days |

Uncertainty is ±30–40 %, dominated by the assumed OCV–SOC curve. Call it 90–180 µA / 350–750 µW.

Self-consistent with the documented 10–15 µA sleep: the remaining ~113 µA implies ~140 ms of active
time per 10 s cycle at ~8 mA, which matches wake + FDC + advertise.

**Something cut consumption 5.5x around 2026-08-29.** Candidates: `779b34e` "Add mux settle margin to
MSV3 production firmware before solar test" (08-29) and `97b2897` "Invalidate MSV3 EC-check" (08-31);
the firmware's own comment flags a parked EC-check code as "a continuous 3V3->soil->VDIV->R17->GND
leak, ~33 µA". Not yet attributed.

## Corrections to existing documents

**`MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` (commit `edc9705`, on `main`, not on this branch).**
Its justification "Divider sum drops 10.04M->8.54M, still inside TI's <=10M rule" is **wrong**. That
rule comes from the body text of section 8.3.3, which states only an upper bound. The Recommended
Operating Conditions table on page 6 specifies **9 / 10 / 11 MΩ with a minimum**, so 8.54 MΩ is
out of spec. The as-built 10.04 MΩ was in spec.

More importantly, that document's premise — that lowering `VBAT_OV` would lower the float voltage —
is void on this hardware. The rework was correctly executed and changed nothing measurable, because
the ceiling was never being set by `VBAT_OV`.

*Maintainer decision, 2026-09-15: the divider stays at 8.54 MΩ for now. The low storage target is
preferred over exact cutoff accuracy, and the clamp is what actually bounds the cell.*

**`MSV3_Probe_PCB/reference/COMPONENT_INDEX.md`.** Its flagged open question about `R_OV1`/`R_OV2`
orientation is **resolved**. Equation 3, read off the rendered page 13, is
`VBAT_OV = (3/2) · VBIAS · (1 + R_OV2/R_OV1)`, and probe 4's clean 4309 mV cap on the as-built
divider confirms empirically that `R_OV2` is the VRDIV-side resistor (R11) and `R_OV1` the GND-side
(R14). The documented orientation was right.

**`MSV3_BENCH_TEST_PLAN_2026-08-21.md`.** Its checked item *"Measure panel V_OC and I_SC under the
actual indoor light you plan to deploy in"* records **2.7 V / 50 uA** and labels it *"real deploy
condition."* It is not. The fleet is deployed **at a window**, which holds `SP+` at 4.1 V loaded — a
panel at 2.7 V `V_OC` cannot do that, so the dim-indoor spot is a different light level entirely.
Every harvest-margin argument that leans on 50 uA is arguing about the wrong room. The window's
`I_SC` has never been measured.

**Marking-code reference** for the megohm parts, from the LCSC order — the code is embedded in each
manufacturer part number, and there is no 4.24 MΩ part in this project's inventory:

| marking | value | MPN | designators |
|---|---|---|---|
| `4424` | 4.42 MΩ | RTT054424FTP | R11 (post-rework), R9 |
| `4124` | 4.12 MΩ | WR08W4124FTL | R14 (post-rework), R15 |
| `5624` | 5.62 MΩ | RTT055624FTP | R11 (as-built) |
| `5904` | 5.9 MΩ | CRCW08055M90FKEA | R12 |
| `5604` | 5.6 MΩ | FRC0805F5604TS | R13 |
| `1005` | 10 MΩ | CRCW080510M0FKEA | R10 |

Two traps recorded because both were hit on 2026-09-15: **the old R14 value (4.42 MΩ) is the new R11
value**, so `4424` appears in both the before and after picture and reading one part in isolation
proves nothing — check which designator it sits on. And an 0805 marking read from the wrong end
gives a plausible-looking wrong answer (`4424` reads as `4244`); R12 is a Vishay part whose body and
marking style differ from the RALEC/Walsin neighbours and can look unpopulated.

## Open

- [x] Measure VIN_DC at `SP+` in full sun with a DMM. **Done 2026-09-16: 5.0 V loaded in direct
      sun, 4.1 V loaded at the deployment window.** Root cause confirmed at the pin.
- [ ] Measure panel `I_SC` **at the deployment window** — the only missing number in the clamp
      decision. Everything below is computed against the 30 mA direct-sun figure.
- [x] Confirm the window peak. **Done 2026-09-16: 4.7 V**, against a 4.7-4.8 V prediction derived
      from probe 6's own cell voltage. `VBAT ceiling = 0.78 x V_OC - 0.3 V` reproduces every charge
      observation in this document to within 60 mV.
- [ ] Re-choose the `RMPPT1/2/3` divider ratio as part of fitting the clamp — at 78 % the panel ends
      up operating at 78 % of `Vz`, giving away 22 % of the clamped voltage for no benefit once a
      clamp is setting the sampled `V_OC`.
- [ ] Fit the clamp on every probe before the panel next sees sun.
- [ ] After clamping, confirm zero charging above `VBAT_OV`. Failure implicates a damaged BQ25504.
- [ ] Probe 6's cell has been held near 4.44–4.49 V for days. Inspect for swelling; treat capacity
      and safety as degraded.
- [ ] Probe 4 has been offline since 2026-09-14 18:48 UTC and is still marked `deployed: true`.
- [ ] Attribute the 5.5x power reduction around 2026-08-29.
- [ ] Next PCB revision: use a panel with `V_OC` under 3 V, which removes the whole failure mode.
