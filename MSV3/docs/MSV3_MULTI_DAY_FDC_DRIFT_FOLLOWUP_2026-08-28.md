# MSV3 Multi-Day FDC Drift Follow-Up

**Recorded:** 2026-08-28

## 2026-08-30 field result

- Visible liquid pockets were found underneath the nail-polish protection after the probe was
  removed from soil.
- The nail polish was completely stripped. The previous drift then reversed while the blade dried
  and settled to a very flat air baseline.
- This partially executes the intent of the coating test without completing the controlled
  fixed-depth-water A/B experiment. It strongly elevates coating/edge water retention, but does not
  yet separate face-coating pockets from routed-edge/FR-4 uptake or soil-contact settling.
- Do not seal the blade until the recovered air baseline remains stable for 24-48 hours. Then epoxy
  the cut FR-4 perimeter, cure, apply thin 419D acrylic coats, cure, and perform the staged air -> dry
  soil -> measured watering -> return-to-air validation in
  [MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md).
- The unpowered-in-soil, fixed-capacitor, pre-soaked-versus-dry, and cadence A/B tests remain open.

Use this checklist to investigate the multi-day initialization rise without prematurely attributing
it to C17.

## Test order

1. [ ] **Run the unpowered-in-soil test** for the multi-day initialization rise.
2. [ ] **Run the bare-tank/fixed-capacitor thermal test** to evaluate L2, FDC acquisition, mux
   behavior, and board parasitics.
3. [ ] **Measure actual soil and blade temperature** and correlate those measurements with raw FDC
   counts separately for each electrode phase.
4. [ ] **Test dry versus pre-soaked blades in fixed-depth covered water** to isolate nail-polish and
   FR-4 absorption.
5. [ ] **If the bare tank moves, investigate:**
   - MSV3 phase offset
   - Untuned IDRIVE
   - Mux settling
   - Correlation with the solar harvester

## C17 disposition

C17 remains questionable only if the BOM describes the intended component but the physical board
was assembled with a different substitute. Otherwise, Claude's specific C17 hypothesis should be
deprioritized.
