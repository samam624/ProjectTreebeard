# MSV4 electrode architecture (2026-09-15)

**Status: design decided, not yet drawn. Supersedes the MSV3 interdigitated electrode.**

Two symmetric electrodes inside a guard ring, mirrored front and back, on an 18 mm × 110 mm blade.
Reaches **2.9 mm** into soil against MSV3's present **0.48 mm**, keeps electrode swapping, and drops
the coating from 14.5% of the sensed field to 2.6%.

Visual reference (drawings to scale, field decay, dimensioned blade):
<https://claude.ai/artifact/LsGnvQqxJycer2YNNZSL75>

## The two rules everything follows from

```
penetration depth  =  lambda / 2*pi
coating share      =  1 - exp(-2*pi*t / lambda)
```

`lambda` is the full spatial period — **twice the electrode centre-to-centre spacing**. `t` is the
coating thickness.

**Both depend on pitch and nothing else.** Finger count, finger length and board area do not appear
in either. This is the fact that reshapes the probe: MSV3 spent its width on 50 fine fingers to buy
capacitance, and paid for it with a 1.5 mm pitch that made the sensor blind below half a millimetre.

## Why the present electrode fails

> **The penetration row below is superseded (2026-09-23).** Those depths come from `lambda/2pi`,
> which overstates MSV4 because the plate-to-guard field is shallow — a caveat
> `MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md` already raised for MSV4 but never applied to the v1.2.
> Solving both geometries the same way **reverses the ranking**: MSV4 as drawn reaches 50 % of its
> response within **1.0 mm**, the v1.2 within **~1.4 mm**. MSV4 is still ~5x deeper than MSV3,
> which was the design goal, and the guard ring is the accepted cost of the difference.
>
> Also corrected: MSV4 single-sided is **0.75x** the v1.2 per unit length, not the 0.49x below.
> Mirrored it is **1.21x** the v1.2 lossless and **~1.45x** in real conductive soil at equal
> inserted length — i.e. the narrower traces are more than paid for, and no capacitance was traded
> away for depth. Numbers and a reproducible runner:
> `MSV4/scripts/msv4_electrode_compare.py`, `MSV4/generated/electrode_compare_2026-09-23.json`.

| | MSV3 now | v1.2 | MSV4 |
|---|---:|---:|---:|
| pattern | 0.5/1.0 interdigitated | G3.5 / 2 / H9.5 / 2 / G3.5 | A4.9 / 4.2 / B4.9 + guard |
| lambda | 3.0 mm | 17.0 mm | 18.2 mm |
| **penetration** | **0.48 mm** | 2.71 mm | **2.90 mm** |
| coating share (75 um) | **14.5%** | 2.7% | **2.6%** |
| soil volume | 537 mm3 | 3605 mm3 | ~2x v1.2 (mirrored) |
| electrode swap | yes | **no** | **yes** |

**The coating number is the important one.** A conformal coating absorbs water, its permittivity
climbs, and then it **saturates** — a large signal that stops responding. At 14.5% of the field it
dominates the reading; at 2.6% it is a rounding error. This is the mechanism behind every coated
MSV3 probe that stopped moving once wet, and it is a geometry problem, not a coating-material
problem. No change of coating chemistry fixes a 0.48 mm sensor.

## Geometry

18.0 mm across, symmetric:

```
guard 1.0 | clearance 1.0 | A 4.9 | gap 4.2 | B 4.9 | clearance 1.0 | guard 1.0
```

Length 110 mm. **Length buys capacitance and does not change depth** — width is the constraint,
length is free. 75 -> 110 mm is +47% capacitance for nothing.

### Why not the v1.2's three-prong G-H-G

For G-H-G there is a clean identity, `lambda = W - W_ground`, which appears to say depth is bought
by thinning the grounds. It breaks down, because the identity assumes the ground actually terminates
the field:

| W_ground | W_high/W_ground | depth | C rel | termination |
|---:|---:|---:|---:|---|
| 3.5 | 2.0 | 2.31 mm | 1.11x | good |
| 2.5 | 3.6 | 2.47 mm | 1.08x | good — **honest 3-prong ceiling at 18 mm** |
| 2.0 | 5.0 | 2.55 mm | 1.05x | marginal |
| 1.0 | 12.0 | 2.71 mm | 0.91x | poor — field escapes the board edge |
| 0.5 | 26.0 | 2.79 mm | 0.76x | poor, and capacitance now falling too |

Keep **W_ground >= gap** and **W_high/W_ground <= ~4.5**. Past that the field is no longer captured
by the ground, the probe begins responding to the pot wall and nearby objects, and below ~1.5 mm the
capacitance falls as well — the trade stops existing.

So an honestly-terminated three-prong at 18 mm tops out at **2.47 mm**, short of the v1.2.

The symmetric A|B layout avoids the failure entirely: **A and B are the same width and terminate
each other**, and the guard ring does the perimeter job the wide ground was doing. The wide ground
has not disappeared — it moved to the edge, where it no longer eats into the A-to-B separation that
sets depth. That is the whole reason A|B reaches 2.90 mm where a sound G-H-G reaches 2.47 mm.

The guard ring costs 0.82 mm of depth (3.72 -> 2.90 unguarded vs guarded). It buys field closure,
insertion-depth insensitivity, and shielding against the mains coupling already documented on this
bench. Keep it.

## Mirror front and back

Identical A|B pattern on both faces, front-A tied to back-A and front-B to back-B.

Field energy splits between regions in proportion to permittivity. Adding a second soil region
without adding a second FR4 region raises the useful fraction as well as the total:

| | soil share, wet (er 15) | soil share, dry (er 4) |
|---|---:|---:|
| single-sided | 77% | 48% |
| **mirrored** | **87%** | **65%** |

Roughly **2x absolute capacitance** and a better signal fraction, most at the dry end. This recovers
essentially all of the capacitance given up by going symmetric (0.49x v1.2 single-sided). Penetration
is unchanged — pitch is untouched, so each face independently reaches 2.90 mm.

First-order estimate. Copper on both faces may capture somewhat more FR4 field than the single-sided
case; the direction is solid, the exact figures want a 2D field solve before being quoted.

### Vias: one pair at the top, above the soil line

**Do not stitch vias down the length.** A plated through-hole is a hole in the coating barrier — a
water path into the laminate and a galvanic contact point, manufactured into the one region that has
to stay sealed. On a probe whose open problem is coating integrity, 20 stitching vias are 20 new
failure points.

They are not needed. A and B are each a single net, and at 6 MHz the wavelength in FR4 is about 24 m,
so a 110 mm trace is lambda/218 — electrically a point. **One via pair at the dry end is
electrically identical to fifty down the blade.**

If vias in the wet zone are unavoidable for fab reasons they must be **tented or filled-and-capped**,
never open barrels.

## Soil temperature sensor on the same blade

Board temperature is not soil temperature, and the 77% ceiling on causal compensation
(`MSV3_REAL_POT_THERMAL_COMPENSATION_2026-09-07.md`) was reached using a die sensor at the dry end.
A sensor at depth is worth having.

**Part: an 0402 NTC thermistor** near the blade tip. Two terminals (a plain non-polarised resistor),
no supply rail, low enough to coat over. A digital part (TMP117, DS18B20) needs three or four traces
plus decoupling and is too tall.

**Size: 1.0 x 0.5 x 0.5 mm.** NTC ceramic bodies are taller than a generic 0402 resistor's 0.35 mm —
Murata NCP15 and TDK NTCG103 are both 0.5 mm. With solder fillet, ~0.6 mm proud; with a protective
epoxy blob, ~1.0-1.1 mm. Verify against the part actually ordered.

**Specify two numbers, not one:** `R25` = 10 kohm and `B` = 3380 K. R25 anchors the curve, B sets its
slope (`R(T) = R25*exp(B*(1/T - 1/298.15))`, T in kelvin; B is the conduction activation energy over
Boltzmann's constant, ~0.29 eV here). Sensitivity is `B/T^2` = 3.8 %/K at 25 C. **Use the B value
specified over a range covering 0-40 C** (`B25/50`, not `B25/85`) — they differ by 1-2% for the same
part and the error grows with distance from 25 C.

With a 10 kohm reference the divider sits at 0.45-0.55 of supply across the pot's observed
19.75-28.25 C band. Error budget with 1% parts: ~0.26 C at 25 C, ~0.32 C at 0 C, ~0.41 C at 50 C.
For compensation work **prioritise B tolerance and long-term drift over R25 tolerance** — an absolute
offset only shifts the fitted intercept, whereas a slope error or a drift corrupts the coefficient.
Cheap NTCs can drift 1-5% per year under humidity and thermal cycling, which is the sleeper risk for
a part living in wet soil under a coating.

**The trick that makes the routing free: RF-bypass the thermistor traces to the guard net.**

Place a capacitor (~1 nF) from each thermistor trace to guard, at the top of the blade. At 6 MHz that
is ~26 ohm — effectively a short, so the traces become part of the guard as far as the sensing field
is concerned. At DC it is an open, so the thermistor reading is untouched.

Then the traces can sit immediately inside the guard ring, which was already dead space for sensing.
Route one trace down each edge so the structure stays symmetric, meeting at the thermistor at the tip:

```
guard 1.0 | Tsense 0.4 | clearance 0.8 | A 4.6 | gap 4.4 | B 4.6 | clearance 0.8 | Tsense 0.4 | guard 1.0
```

lambda 18.0, **depth 2.86 mm** — 0.04 mm of loss for a soil-depth temperature channel.

**Only one new trace is needed, not two.** The guard ring is already ground and already runs the full
length of the blade past the tip, so one thermistor terminal lands on it locally. The other gets a
single sense trace up to a divider at the dry end:

```
dry end:   GPIO --[ R_ref 10k ]--+-- ADC
                                  |
                          sense trace, down one edge inside the guard
                                  |
near tip:                    [ NTC 10k ]
                                  |
                          guard ring (already present)
```

Source impedance at the divider midpoint is ~5 kohm, low enough for a 110 mm run inside a grounded
guard. The 1 nF RF bypass to guard still applies, so the sensing field never sees the trace. The
slight one-edge asymmetry is electrically irrelevant once bypassed; a dummy guard-tied trace on the
other edge restores physical symmetry for free if wanted.

Notes:
- **Read it in short pulses, not continuously.** At 3.3 V across 10k + NTC the thermistor dissipates
  ~0.27 mW, which is ~0.14 C of self-heat if left on — half the error budget for nothing. The 0402's
  thermal time constant is seconds, so a few-ms GPIO-driven read makes it ~0. Same pattern as
  `VBAT_DIV_EN`.
- **Ratiometric read.** The divider runs off the probe's own supply, so the ADC result is a ratio and
  supply drift cancels. Do not reference it to an absolute voltage.
- **Place it 5-10 mm up from the tip, not at the point.** The tip is what gets pushed through soil and
  a ~1 mm epoxy bump there acts as a plough. Still deep in the root zone, still inside the guard
  region and out of the A|B field, and the tip stays a clean insertion edge.
- **Epoxy blob over the part is worth it** — far lower water-vapour transmission than acrylic or
  urethane conformal coating, over the one component whose joints cannot be inspected later. Thermal
  lag is a non-issue (~5 s time constant against soil dynamics measured in hours). **The failure mode
  is the epoxy/soldermask interface, not the bulk**: water travels along it by capillary action
  straight to the joints, and the blob then hides it. Clean with IPA and dry fully before dispensing,
  pick an epoxy with stated soldermask adhesion, and keep the blob small and domed rather than a
  thick slab so blade flex during insertion does not crack it or fatigue the joints. Order of
  operations: solder, clean, epoxy, cure, then conformal coat over the top — confirm the coating
  adheres to the cured epoxy first.
- **Copper conducts heat from the dry end down the blade.** Estimated conductance of the A/B copper
  along 110 mm is ~6e-4 W/K against ~2e-2 W/K of soil coupling to the blade faces — soil wins by
  roughly 30x, so the tip should track soil rather than the electronics. Rough estimate; worth
  confirming before trusting the absolute reading.
- Both thermistor terminals sit at guard potential at RF, so the part itself is a small grounded
  object in the field. Fixed perturbation, calibrates out.

## Open

- [ ] 2D field solve to confirm the mirrored soil-fraction figures before they are quoted anywhere.
- [ ] Confirm the tank inductor. Lower electrode capacitance pushes resonance toward the FDC2214's
      10 MHz ceiling; 6.8 -> 15 uH drops every frequency by 1.5x and removes the constraint. Do not
      let the FDC ceiling dictate electrode geometry.
- [ ] Mux capacitance. TMUX1109 is 38 pF C_on against a ~18 pF electrode — the C17 dilution problem
      in a new costume. TS3USB221A is 6 pF and lifts the electrode's share of the tank from ~51% to
      ~80%. See `MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md` for the other BOM changes queued.
- [ ] Verify the copper thermal-short estimate on a real blade before trusting absolute soil temps.
- [ ] If the ~3 mm shell still proves too shallow after V4, the next rung is two physically separated
      rods at ~25 mm spacing, which senses a volume on the order of the separation. That abandons the
      thin single-blade form factor the maintainer values, so it is a deliberate later decision, not
      a tweak.
