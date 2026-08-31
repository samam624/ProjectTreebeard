# MSV3 soil-ingress finding and coating remediation

**Recorded:** 2026-08-30  
**Status:** Leading physical cause strongly supported; epoxy/acrylic repair still needs a controlled
soil re-entry test.  
**Current physical state:** The nail polish was completely removed. The probe is out of soil,
drying on the desk, and was reported to have reversed its prior drift and settled to a very flat
air baseline.

## Executive conclusion

The original suspicion that MSV3 was reading the wrong FDC channel, truncating the count, or mostly
reporting a wake-timing artifact did not survive the signal-chain audit. CH0 is the connected channel,
the complete 28-bit FDC2214 result is reconstructed correctly, and the BLE/receiver/cloud byte order
is consistent. Firmware acquisition defects were found and corrected, but they do not explain the
smooth multi-day field behavior by themselves.

The most important physical observation came later: visible water pockets had penetrated beneath the
hand-applied nail-polish layer. After the nail polish was removed, the long drift reversed as the
blade dried and then became flat in air. The user also observed that the strange behavior appears
when probes enter soil. Together, those observations move coating/edge water retention and soil/probe
equilibration ahead of intrinsic FDC temperature drift as the leading explanation for the large
multi-day artifact.

This is strong evidence, not yet a completed proof. A repaired probe must return to its pre-insertion
air baseline after a controlled soil/watering exposure before the ingress mechanism can be called
closed.

## Field chronology

1. On 2026-08-29 at approximately 21:00 PDT, the pot was watered from fully dry to fully wet. The
   immediate moisture-connected count change was small relative to the recurring daily movement,
   which exceeded roughly 100,000 counts.
2. The connected CH0 count, nRF52840 die temperature, disconnected bare-tank count, and mux phase
   were then made visible end to end. The bare tank strongly tracked die temperature and a same-row
   ratiometric trace removed about half of the first observed connected-count temperature slope.
3. FDC acquisition was hardened: `RCOUNT_CH0` was increased from `0x0400` to `0xFFFF`, full-current
   activation was selected, fresh CH0 data is awaited, and STATUS is captured before DATA clears a
   latched condition. The speculative 300 ms mux delay was removed because it occurred while the FDC
   was shut down and could not settle the oscillator.
4. The first fresh-data implementation incorrectly polled global `STATUS.DRDY`. Because
   `ERROR_CONFIG.DRDY_2INT` resets disabled after each hardware shutdown, that bit never became the
   expected readiness indication and advertisements stopped. Production firmware was corrected to
   poll the per-channel `STATUS.CH0_UNREADCONV` bit; transmissions returned after reflashing.
5. A displayed battery value near 3% was not a sudden cell discharge. The probe measured about
   3.74 V, but the receiver's old cross-probe 400 mV charging correction pushed that voltage into the
   bottom of the LiPo lookup table. That inferred correction was removed; battery percentage now uses
   measured voltage directly while retaining the charging flag as diagnostic data.
6. The probe was removed from soil. Water pockets were found underneath the nail polish. The polish
   was stripped with acetone followed by isopropyl-alcohol cleanup.
7. While drying in air, the earlier drift reversed and the signal settled to a very flat level. No
   numerical export of this final dry-air interval has been frozen yet, so the observation must be
   retained as qualitative field evidence until the history is exported.

## Why trapped water can look like temperature drift

The MSV3 electrodes are outer-layer copper covered by factory solder mask. They were never bare
copper. The problematic stack was approximately:

```text
soil -> irregular nail-polish layer/pockets -> solder mask -> outer copper -> FR-4
```

Water held immediately above the electrode is inside the intended electric field. The FDC cannot
distinguish that retained water from water in the surrounding soil; both increase effective
capacitance. A pocket can remain after bulk soil conditions change, creating long time constants,
hysteresis, and a weak apparent watering step.

The cut FR-4 perimeter is a second path. Solder mask protects the faces but is not normally wrapped
around the edge created during final routing. Water can enter exposed glass/epoxy at the perimeter,
around a via, or through a coating defect. Daily heating and cooling can then redistribute trapped
water, change contact pressure, and change dielectric loss. A time-of-day-shaped signal can therefore
be thermally driven without the FDC IC itself being the dominant thermal element.

The hypothesis that older MSV2 blades have simply become saturated is plausible but unproven. A
saturated blade may look stable because it reached equilibrium, while also being biased, less
sensitive, and vulnerable to seasonal hysteresis or corrosion. Stability alone is not evidence that
saturation is an acceptable production state.

## Repair procedure for the current probe

### 1. Dry before sealing

- Leave the uncoated blade in air or a dry desiccant container until the connected FDC air baseline
  remains stable for at least 24 hours; 48 hours is preferable after visible water pockets.
- Do not bake an attached LiPo. If a low-temperature bake is considered later, remove the cell and
  verify every component's rating first.
- Do not seal the edge merely because the surface feels dry. Epoxy would trap any remaining internal
  moisture.

### 2. Seal the routed FR-4 edge

The selected small-run material is **MG Chemicals 832HD-25ML**, a solvent-free, electrically
insulating 1:1 epoxy potting compound. Its current TDS reports a dielectric constant of 2.5 at 1 MHz,
0.3% weight gain after 30 days in water at 25 C, and a 24-hour room-temperature cure.

1. Clean the completely dry routed edge with 99% IPA and allow it to evaporate.
2. Dispense and mix the two parts 1:1 by volume gently, avoiding bubbles.
3. Apply with a toothpick or fine disposable brush around the complete soil-exposed perimeter.
4. Wrap the bead approximately 0.5-1.0 mm onto the solder mask on both faces. The resulting shallow
   U-shape is intentional: it bridges the edge-to-face seam instead of leaving a capillary path.
5. Keep the bead narrow and uniform. It is a stable dielectric but it still participates in the
   FDC fringe field.
6. Inspect the tip, corners, soil-line transition, and any remaining holes under magnification.
7. Cure at room temperature for at least 24 hours; use 48 hours before the next coating step for
   this test.

Do not substitute five-minute hardware epoxy, conductive or metal-filled epoxy, hot glue, or UV
craft resin. Uncured epoxy requires nitrile gloves and eye protection and must be handled according
to its SDS.

### 3. Coat the faces

Use the already-purchased **MG Chemicals 419D acrylic conformal-coating aerosol** as the controlled
prototype face coating. It is a reasonable prototype material, not a guaranteed lifetime immersion
barrier. MG markets it as a moisture barrier for humid environments, recommends a 25-75 micrometre
finished thickness, and rates its chemical resistance as poor. Wet fertilized soil is more demanding
than humidity.

1. Mask connectors, contacts, the solar-cell optical surface, and anything that must remain
   serviceable. Coat the soil-exposed blade faces and the cured edge seal.
2. Spray outdoors in a covered, well-ventilated area away from flame, sparks, direct hot sun, and
   occupied air intakes. The aerosol is highly flammable and its vapor can cause dizziness.
3. Use splash goggles, nitrile gloves, and an organic-vapor respirator with P95/P100 particulate
   filtration. An N95 alone does not protect against organic-solvent vapor.
4. Shake thoroughly, hold the board about 8-10 inches away at roughly 45 degrees, and use light
   passes rather than one wet coat.
5. Wait about three minutes between passes and rotate the probe 90 degrees between passes. Avoid
   runs, bubbles, bridges, and liquid pockets.
6. Allow at least the manufacturer's 24-hour room-temperature full cure. For this experiment, wait
   48 hours before inserting it into soil.

Because MG does not publish this exact 832HD-under-419D edge stack as a qualified system, make a
scrap-FR-4 coupon with the same solder mask and both materials. Soak and inspect that coupon before
calling the combination production-qualified.

### Material-selection disposition

The 419D purchase was reasonable for this prototype. Acrylic is comparatively easy to spray in a
thin layer, inspect, remove, and rework. Its limitation is long-term wet, ionic soil rather than an
inability to reject any moisture at all.

- **MG 4223F urethane:** stronger chemical, abrasion, corrosion, and moisture protection than 419D,
  but the current thermoset product calls for approximately two hours at 100 C. That is inconvenient
  for an assembled probe and unacceptable with an attached LiPo unless the battery is removed and
  every component/process limit is reviewed.
- **Professional Parylene C:** the strongest thin-film option considered. Vacuum deposition produces
  a highly conformal, nearly pinhole-free barrier with much lower water-vapor transmission than
  ordinary acrylic. It is expensive, requires professional masking/processing, changes the fixed
  sensor capacitance, and is difficult to repair. Approximately 15-25 micrometres is the starting
  range to discuss with a coating vendor, not an unverified final specification.
- **Full epoxy face encapsulation:** can provide an immersion-capable barrier, but a hand-applied
  layer is difficult to keep thin and bubble-free, is permanent, and can reduce soil coupling. Use
  the epoxy on the routed edge for this experiment rather than replacing the entire active face with
  an uncontrolled casting.

No coating is a substitute for process control. A pinhole, unsealed routed edge, via, or trapped
water pocket can dominate an otherwise superior bulk material.

## Controlled validation after repair

Keep insertion depth, soil, pot mass, water volume, and handling written down. Preserve the raw
connected count, bare count, die temperature, mux phase, battery voltage, and timestamps.

| Stage | Minimum observation | Decisive result |
|---|---|---|
| Fully dried, uncoated air | 24-48 hours | Establish the true recovered baseline and temperature response. |
| Epoxy-sealed air | 24 hours after full cure | Quantify the fixed edge-seal offset and verify no continuing cure drift. |
| 419D-coated air | 24-48 hours after full cure | Establish the final baseline used for every later comparison. |
| Dry-soil insertion | 24 hours, fixed marked depth, untouched | Separates insertion/contact settling from watering. |
| One measured watering | At least 3-7 days | Watering must be visible above quiet-period movement and must not become a monotonic coating-uptake ramp. |
| Return to air | Until dry and stable | Final air baseline should return within the pre-insertion noise band; a persistent offset indicates retained moisture. |

Use the paired diagnostic quantity:

```text
reference excess (%) = 100 * ((D_bare / D_connected)^2 - 1)
```

It cancels common FDC reference-clock and inductor movement to first order. It does not cancel water
inside the coating, soil conductivity, contact geometry, or soil-temperature effects. Those residuals
are exactly what this test is intended to expose.

## V4 design and manufacturing disposition

Do not describe the current construction as exposed copper. The recommended baseline V4 geometry is
still outer-layer sensing copper beneath factory solder mask. Moving the electrodes to internal
layers would add FR-4 to the sensing path and could further weaken the already-small watering signal;
that change requires an A/B coupon rather than an assumption.

For V4:

- Keep at least a 1.0 mm project-level copper keepout from the soil-exposed routed perimeter, even
  though JLCPCB's ordinary routed-edge minimum is smaller.
- Avoid vias in the active blade if possible. If unavoidable, specify nonconductive epoxy-filled and
  capped vias and keep them out of the soil-line and highest-field region.
- Use smooth radiused corners and a routed final outline. Do not leave mouse-bite fracture surfaces
  on the sensing perimeter.
- Fully depanelize before environmental sealing; otherwise every broken tab creates a new unsealed
  edge.
- After routing and cleaning, encapsulate the complete cut edge with a void-free, electrically
  insulating immersion-rated epoxy, wrapping 0.5-1.0 mm onto both solder-mask faces.
- Apply a characterized, controlled face coating after the edge seal cures. Include a witness coupon
  in every coating lot and calibrate only after all materials have fully cured.

Suggested drawing note:

> Outer-layer sensing electrodes shall remain fully covered by solder mask. Maintain 1.0 mm minimum
> copper clearance from all soil-exposed routed edges. Following final routing and depanelization,
> completely encapsulate the exposed FR-4 perimeter with a continuous, void-free, electrically
> insulating, low-water-absorption edge sealant rated for continuous wet exposure. No edge plating,
> exposed copper, or unsealed breakaway tabs are permitted.

As of 2026-08-30, JLCPCB does not advertise routed-edge epoxy encapsulation as a standard service.
Its listed options are electrically conductive ENIG edge plating, nonconductive epoxy filling for
vias, and acrylic conformal coating for Standard PCBA orders. JLCPCB states that its conformal coat is
normally 50-200 micrometres, has a 17 x 17 mm minimum spray range, may be nonuniform, and is not
guaranteed to provide complete insulation or moisture coverage. Ask support for a custom secondary
operation, but do not treat an order remark as acceptance without written confirmation and a sample.
For early V4 runs, perform the edge seal after JLCPCB assembly or use a dedicated environmental-
coating contractor.

## Related project records

- [Original FDC thermal/signal-chain investigation](MSV3_FDC_THERMAL_DRIFT_ROOT_CAUSE_2026-08-30.md)
- [Bare-tank reference formula and first live window](MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md)
- [Temperature and bare-tank telemetry design](MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md)
- [Earlier insertion-settling investigation](MOISTURE_INSERTION_SETTLING_AND_V4_TEMPERATURE_PLAN_2026-08-24.md)
- [Multi-day drift follow-up checklist](MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md)

## External references retained for purchasing and process decisions

- MG Chemicals 419D TDS:
  <https://mgchemicals.com/downloads/tds/tds-419d-a.pdf>
- MG Chemicals 419D product page and removal options:
  <https://mgchemicals.com/products/conformal-coating/acrylic-conformal-coatings/circuit-board-protective-coating/>
- MG Chemicals SDS/TDS database; select `419D-340G` aerosol for the can-specific SDS:
  <https://mgchemicals.com/sds-tds/>
- MG Chemicals 832HD TDS:
  <https://www.mgchemicals.com/downloads/tds/tds-832hd-2parts.pdf>
- MG Chemicals 4223F urethane product page:
  <https://mgchemicals.com/products/conformal-coatings/polyurethane-conformal-coatings/urethane-conformal-coating/>
- Specialty Coating Systems overview of Parylene barrier properties:
  <https://scscoatings.com/parylene-coatings/properties/>
- JLCPCB conformal coating service:
  <https://jlcpcb.com/help/article/conformal-coating-spray-service>
- JLCPCB edge plating description, which is not dielectric edge sealing:
  <https://jlcpcb.com/help/article/pcb-design-instructions-for-edge-plating>
- JLCPCB via covering and epoxy-filled-via options:
  <https://jlcpcb.com/help/article/pcb-via-covering>
