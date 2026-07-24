# Moisture Sensor Firmware Fix — Debugging Report

**Date:** 2026-07-24
**Scope:** `arduino/MSV2_Moisture_Probe_Test/MSV2_Moisture_Probe_Test.ino` only.
**Status:** Bench-verified working. Not yet ported into `MSV2_Moisture_BLE_Sleep.ino` (that sketch still sends a hardcoded `fakeMoisture` value — this report exists so that port can be done correctly next).

## Symptom that started this

Capacitance reading was stable with the probe sitting on the counter, but jumped around erratically once inserted into a plant pot, eventually pegging at the max/timeout value and never recovering.

## Root causes found, in the order we found them

1. **10kΩ pulldown was too small.** Real probe capacitance (dry/bare) is roughly 170-380pF. `10kΩ × ~200pF ≈ 2µs` discharge time constant — faster than a single `digitalRead()`/`micros()` call executes. Every one-shot timing measurement read back as `0` regardless of moisture, because the node had already discharged before the code could check it.
2. **10MΩ (over-correcting) was too large.** Baseline dry discharge sat right at ~98ms with a 100ms timeout — almost no headroom, and any added moisture pushed it straight into saturation (same failure mode as the original bug, just from the opposite direction).
3. **The one-shot precharge/discharge architecture itself is fragile.** Even after tuning R and using a short partial-charge pulse to land discharge time in a good window, back-to-back samples (averaging N samples per report, no rest in between) drifted upward over successive samples — the discharge loop only runs until the logic threshold is crossed, not down to true 0V, so each subsequent precharge pulse started from a creeping residual voltage instead of a clean baseline. Any one-shot RC timing design needs to either force a hard reset to 0V between samples or use partial-charge pulses carefully — both are fiddly to get right.
4. **The real architectural fix: stop doing one-shot timing entirely.** This project's own proof-of-concept paper (`Amezcua, "Design and Experimental Characterization of a Capacitive Soil Moisture Sensor Using a 555 Timer Architecture"`, MSV1) already validated the correct approach: a 555 astable oscillator that free-runs continuously between two thresholds, where **frequency** (not a single timed edge) is the measured quantity. That paper's own experimental data:
   - Real sensor capacitance measured (not estimated): **~270-384pF dry**, rising to **~1.2-1.3nF in saturated soil**.
   - Astable used **R_A = R_B = 50kΩ** (R_A + 2R_B = 150kΩ total) — the right resistance scale for this capacitance range is **tens of kΩ**, not the 10kΩ or 10MΩ extremes tried on the XIAO.
   - The oscillator itself never saturated across the full moisture range (27kHz dry → 7.5kHz saturated, clean and monotonic). What *did* saturate in that paper's design was a downstream monostable+LPF+comparator stage tuned for the wrong (originally-estimated, too-small) capacitance range — the same class of bug as our one-shot XIAO code, just in the analog domain instead of firmware.

## Final working architecture (what's in the sketch now)

- **100kΩ pulldown** on the sensing node (A0/IO0) — matches the paper's proven ~50-150k range for this capacitance.
- **Free-running relaxation oscillator in software**: drive the pin OUTPUT HIGH until it reads HIGH, immediately switch to floating INPUT and let the 100k pulldown discharge it until it reads LOW, immediately switch back to OUTPUT HIGH, repeat continuously. No fixed precharge duration to tune, no discrete "reset to 0V" step needed — the pin never stops cycling.
- **Frequency = cycle count / window duration.** Lower frequency = wetter (higher effective capacitance) — same direction as the 555 astable.
- Bench-verified: stable, jitter-free readings; responds instantly to touch/moisture with no jumping and no saturation, across a 33kHz (touched/high-capacitance) to 83kHz (dry) range in early testing.

## Low-power adaptation (also bench-verified)

- Original continuous-sampling loop (200ms cadence, always-on) was replaced with a **periodic wake/measure/sleep pattern**, matching `MSV2_Moisture_BLE_Sleep.ino`'s existing wake-cycle style: wake, do a short measurement burst, sleep for `WAKE_INTERVAL_MS` (currently 3000ms), repeat.
- Red LED indicator on **A1** is lit for the exact duration of each measurement burst — useful for visually/scope-verifying how long the active-power window actually is.
- **Window-duration sweep experiment** (50/20/10/5/2/1ms bursts, run back-to-back and compared): found that shrinking a single burst below ~10ms starts producing occasional large outlier spikes (one single 2ms reading came back 67% high, a 5ms reading 20% high, a 10ms reading 10% high). This is almost certainly rare RTOS/scheduler tick jitter delaying the burst's own `millis()`-based cutoff check — invisible on a 50ms burst, a large fraction of a 2-5ms one.
- **Fix: median-of-5 outlier rejection**, not a longer burst. Each report now takes 5 independent 1ms bursts and reports the **median** cycle count. A median-of-5 silently drops up to 2 outliers per report, so a rare single-sample scheduler glitch never reaches the output — confirmed live on the bench (`raw:79 81 82 80 163` → median still reported cleanly as `81`, the 163 outlier fully absorbed). Total active measurement time per report is **5ms** (5 × 1ms), a ~10x reduction from the original 50ms continuous-window design.

## Key numbers to reuse when porting to `MSV2_Moisture_BLE_Sleep.ino`

- Pulldown resistor: **100kΩ** on the sensing node.
- `SAMPLE_WINDOW_MS = 1`, `MEDIAN_SAMPLES = 5` (5ms total active measurement time per reading).
- `OSC_CYCLE_TIMEOUT_US = 5000` — safety cap per half-cycle so a disconnected/open probe can't hang a burst.
- Direction convention: **lower frequency = wetter**. Don't invert this by accident when wiring up watering-threshold logic.
- The measurement itself (`countOscCycles()` + `medianOf()`) is self-contained and has no dependency on BLE/Bluefruit — should drop into the BLE sketch's wake cycle directly in place of the `fakeMoisture` line, replacing `int fakeMoisture = 500 + (wakeCount % 100);` with a real reading, then encoding the resulting frequency (or a derived moisture percentage) into the existing `manufacturerData` BLE payload instead of the placeholder.
- Still untested: real soil (all bench verification so far used touch/hand proximity as a moisture stimulus, consistent with the paper's own "hands on probe" experiment, but not yet re-verified in the actual pot before deployment).

## Open question raised during this session, not yet resolved

Whether the 100kΩ resistor could be lowered (e.g. back to 10kΩ) now that the oscillator/frequency-counting architecture is immune to the one-shot timing bug. Measured 33-83kHz at 100kΩ implies capacitance swinging roughly 170-440pF over that test. Dropping to 10k would push frequency towards 330kHz-830kHz, at which point fixed per-cycle software overhead (`pinMode()`/`digitalWrite()`/`digitalRead()` call cost, not physics) becomes a much larger fraction of each cycle and would compress sensitivity — plausible but unverified without directly measuring the loop's raw maximum cycle rate (e.g. by shorting the sensing node to ground and observing the frequency ceiling that reveals). Worth testing empirically before committing to a resistor value in the final probe design.
