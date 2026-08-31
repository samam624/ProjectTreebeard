# MSV3 Four-Board Mux Triage (2026-08-31)

## Why

EC-check (the resistive `DIV_NODE`/`R17` soil-conductivity divider) is invalidated — see
`docs/STATUS_HANDOFF.md`'s 2026-08-31 entries: soldermask over the electrodes (soon to be
conformal coat too) blocks the galvanic contact a DC resistance measurement needs, and no
resistor value fixes that. The EC-check bodge wire (`P0.03`→`VDIV`) is being removed from all
four boards for a clean seal under conformal coat — this is unrelated to the test below, which
never reads `DIV_NODE`/`divnode`.

What's still real and needs verifying per-board before coating: the **electrode-swap function**
(polarization-cancellation on the actual moisture tank) and the **bare-tank reference reading**
(disconnects the electrodes for a parasitic-only capacitance baseline). Both depend on the
TMUX1109 actually switching. One board was already found with a dead mux during the 2026-08-28
four-board bring-up (`muxsweep` showed zero switching); this triage checks all four before
deciding which get the mux-bypass hardware fix.

**No new firmware needed** — every command below already exists in
`MSV3/arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino`. If a board somehow isn't
running this sketch, reflash it via `scripts/arduino_msv3_probe.ps1` — never a raw `arduino-cli`
call, per this repo's MSV3 build rule (LFCLK guard).

## Per-board procedure

Run one board at a time, powered normally over USB (this is the board's own FDC/mux operation —
unrelated to the fully-powered-off external bench jig used for the EC-check investigation).

1. `fdcid` — confirm the FDC2214 responds over I2C at all before doing anything else.
2. `fdcinit` — configure CH0 free-running conversion.
3. `muxsweep` — cycles all 4 mux codes with one `fdcread` each. Record the four `DATA_CH0` values
   and confirm the printed `MUX_A1=.. MUX_A0=.. -> ...` labels resolve as expected (this alone
   confirms the GPIO select lines are toggling, independent of whether the analog switch itself
   is actually moving).
4. For a cleaner, averaged version of the same comparison (reduces single-sample noise):
   - `mux 0`, then `fdcstream 10` — record the average `DATA_CH0` (electrode-swap phase, real
     tank + electrodes).
   - `mux 2`, then `fdcstream 10` — record the average `DATA_CH0` (EC-check phase, electrodes
     disconnected — bare/reference tank only).
5. Compute `% step = |avg(code 0/1) - avg(code 2/3)| / avg(code 0/1) * 100`.

## Pass/fail criteria (reusing this project's own already-validated threshold)

Per the 2026-08-28 four-board bring-up findings: **a healthy board shows a clean ~11% step**
between electrode-swap and EC-check codes. **The one confirmed-dead mux showed all 4 codes within
~0.02% of each other** (effectively no step at all) — confirmed independently by multimeter
(Node A pinned regardless of mux code). Use that real precedent rather than an arbitrary new
threshold:

- **~8-15% step, both `muxsweep` and the averaged `fdcstream` comparison agree, node-switching
  confirmed:** mux is healthy. Node A/B are genuinely reaching the FDC tank in electrode-swap
  phase and are genuinely disconnected in EC-check phase — both surviving mux functions
  (swap-for-polarization, bare-tank-reference) are usable on this board. No hardware change
  needed; proceed to bodge-wire removal and conformal coat as planned.
- **<~1% step, all four codes read essentially identical, multimeter confirms Node A/B pinned
  regardless of mux code:** dead mux, same signature as the board already found 2026-08-28.
  Candidate for removal + hard-wire bypass (below).
- **Anything in between, or inconsistent between `muxsweep` and the averaged comparison:** don't
  triage it as clearly good or dead from this alone — flag for a closer look (multimeter directly
  on `MS_NODE_A`/`MS_NODE_B` vs `TANK_HOT`/`TANK_REF` while stepping through mux codes by hand)
  before deciding.

## Results

| Board | `muxsweep` codes 0/1 | `muxsweep` codes 2/3 | % step | Averaged `fdcstream` % step | Verdict | Action |
|---|---|---|---|---|---|---|
| 1 | ~20.28M | ~22.62M | 11.6% | — (muxsweep alone was clean) | **Healthy.** Previously marked "broken" from EC-check misbehavior, which we now know fails on every board regardless of health — false positive, reclassified working. | None — proceed to bodge-wire removal/coat. |
| 2 | ~20.30M | ~21.84M | 7.6% | — | **Healthy**, but with more codes-0/1 asymmetry (6.4%) than boards 1/3 — plausibly the already-known ~8mm Node A/B trace-length mismatch (`MOISTURE_SENSOR_FDC2212_FRONTEND.md`), not a new fault. Was already marked working from before — confirmed. | None — proceed to bodge-wire removal/coat. |
| 3 | ~20.50M | ~22.81M | 11.25% (muxsweep) / 11.26% (averaged) | 11.26% | **Healthy.** `muxsweep`'s codes 0/1 and 2/3 each came back bit-identical, which looked like a possible stale-read artifact — verified against an averaged `fdcstream` (normal ~0.007% sample jitter) and the step matched almost exactly, confirming it was coincidence, not a bug. | None — proceed to bodge-wire removal/coat. |
| 4 | 0xFFFFFFF (all 4 codes, every code identical) | (same) | ~0% (stuck at max) | — | **Initially dead — root cause found: the tank inductor (`L2`) was physically seated sideways.** I2C/`fdcid` worked throughout (chip alive, channel 0 conversion never completed — stuck at the 28-bit max sentinel `0x0FFFFFFF`). Ruled out before finding the real cause: `idrive` sweep (0-31, no change, and the sweep's own "read back one conversion" was shown to be a timing artifact — a separate clean `fdcinit`+`fdcread` afterward reproduced the same stuck value, confirming the fault was real and stable); a full hardware reset via `sleep`/`wake` (FDC2214 SD-pin shutdown, "registers cleared," full reconfigure on wake) — no change, ruling out a soft/stuck-state explanation; `L2` DC resistance measured normal/consistent with healthy boards (a misaligned-but-still-contacting part can still pass a DC continuity check, so this didn't contradict the eventual finding). **Fixed by reseating the inductor** — re-test after reseating: 11.37% step, 0/1 asymmetry 0.007%, matching the other three boards. | Fixed by reseating `L2`. No further action — proceed to bodge-wire removal/coat like the others. |

**All four boards healthy as of this reseat.** None require the mux-bypass hardware fix — that section below is now moot for this batch, kept for reference in case a future board needs it.

## Mux-bypass wiring reference (for any board triaged as dead mux — don't do this pre-emptively)

Verified against the real netlist (`MSV3/hardware/MSV3_Probe_PCB/reference/PIN_NET_REFERENCE.md`):

- `TANK_HOT` (FDC2214 tank hot side) = `C17-1`, `L2-2`, `MS_IC-9` — **hardwired directly to the
  FDC, never through the mux**. Also present at mux throw pins `MUX-4`/`MUX-12`.
- `TANK_REF` (FDC2214 tank reference side) = `C17-2`, `L2-1`, `MS_IC-10` — same, hardwired
  directly to the FDC. Also present at mux throw pins `MUX-5`/`MUX-13`.
- `MS_NODE_A` (electrode A) and `MS_NODE_B` (electrode B) are both broken out as **bare test
  points** on the board (`MS_NODE_A-1`, `MS_NODE_B-1`), normally routed to mux common pins
  `MUX-8`/`MUX-9`.

Once a dead TMUX1109 is removed, bridging `MS_NODE_A` (test point) directly to `TANK_HOT`
(available at the now-vacant `MUX-4`/`MUX-12` pad, or `L2-2`/`C17-1`) and `MS_NODE_B` to
`TANK_REF` (`MUX-5`/`MUX-13` pad, or `L2-1`/`C17-2`) restores basic single-polarity FDC moisture
sensing on that board — permanently fixed at one electrode-swap orientation, no
polarization-cancellation swap and no bare-tank reference anymore, but a working moisture reading
instead of a dead board. Confirm exact pad accessibility on the physical board before committing
to this per-board; not yet bench-verified this session.
