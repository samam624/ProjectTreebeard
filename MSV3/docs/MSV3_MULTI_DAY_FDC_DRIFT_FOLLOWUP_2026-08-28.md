# MSV3 Multi-Day FDC Drift Follow-Up

**Recorded:** 2026-08-28

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

