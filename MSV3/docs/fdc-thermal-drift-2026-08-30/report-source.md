# FDC2214 thermal-drift investigation — canonical research source

**Date:** 2026-08-30  
**Audience:** Project Treebeard engineering  
**Scope:** MSV3 FDC2214/TMUX1109 moisture path, production firmware, receiver/cloud transport,
live sensor-4 history from 2026-08-24 through 2026-08-30, component datasheets, and relevant
soil dielectric literature.  
**Primary question:** Is MSV3 reading the intended sensor and correct FDC count, and are the
large daily swings caused by a failed mux, cold-wake behavior, or the sensing architecture?

## Direct answer

The intended FDC2214 channel and 28-bit count path are correct in checked-in hardware and
software. Live data are also incompatible with a permanently bare/disconnected tank: the count
is in the electrode-connected range and moved in the correct direction when effective
capacitance increased. The previously known nRF52840 LFCLK failure is not present in the
installed build environment (`arduino_msv3_probe.ps1 -Action doctor` passed on 2026-08-30).

There is, however, a real measurement-quality problem. The live series contains a repeatable
approximately 0.8% daily count waveform—similar to or larger than the observed watering step.
The leading explanation is a combination of real temperature sensitivity in the lossy
soil/electrode impedance and thermal drift of the unreferenced LC/mux/PCB front end. The design
uses a large fixed 270 pF tank capacitor, a power inductor whose precision thermal behavior is
not specified, an analog mux with capacitances comparable to sensor-scale changes, the FDC's
internal oscillator, and no populated reference channel. The FDC's resolution cannot distinguish
water from temperature or parasitic capacitance; greater sensitivity increases response to all of
them.

A physically damaged or stuck TMUX1109 remains plausible because this project has already found
one failed unit. It is not the leading explanation for sensor 4, because its live count and
watering-associated response indicate that electrodes are connected. A healthy TMUX can still
harm the measurement through its 38 pF on-capacitance and off-state parasitics. The existing
300 ms delay is far longer than switch settling and occurs before the FDC is activated, so adding
more pre-wake delay is not a credible fix.

Cold-wake acquisition is not fully validated. Production code neither polls fresh-data status nor
associates the FDC amplitude/watchdog flags with the exact transmitted conversion. It also uses a
short `RCOUNT=0x0400`, untuned drive current, low-power sensor activation, and the default 33 MHz
deglitch filter. These can admit marginal samples and leave performance unused, but they do not
by themselves explain a smooth time-of-day waveform.

## Assumptions and evidence boundaries

- The analyzed production source is the current checked-in source. A generated 2026-08-29 build
  contains the diagnostic extension, but repository evidence does not prove which probe and
  receiver binaries are actually running.
- The direct live query returned null for board temperature, bare-tank count, and tank phase on
  every analyzed row. Therefore no thermal attribution or phase separation is possible yet.
- Absolute capacitance estimates assume the typical 43.4 MHz internal reference and nominal
  6.8 µH inductance. Ratios and percentage changes do not require those assumptions.
- The user's stated watering time, 2026-08-29 21:00 PDT, does not align with the only abrupt live
  transition, at approximately 20:20 PDT. The event time must be reconciled before treating that
  transition as definitively caused by watering.

## Claim-to-source ledger

| ID | Claim | Evidence | Confidence / limitation |
|---|---|---|---|
| C1 | CH0 is the correct and only connected FDC channel. | [`COMPONENT_INDEX.md`](../../hardware/MSV3_Probe_PCB/reference/COMPONENT_INDEX.md), lines 109–136; [`PIN_NET_REFERENCE.md`](../../hardware/MSV3_Probe_PCB/reference/PIN_NET_REFERENCE.md), lines 44–45; PCB netlist. | High. CH1–3 are grounded. |
| C2 | The mux truth table and firmware select-bit ordering agree. | Component index lines 138–149; [`MSV3_Moisture_BLE_Sleep.ino`](../../arduino/MSV3_Moisture_BLE_Sleep/MSV3_Moisture_BLE_Sleep.ino), lines 215–219; [TMUX1109 datasheet](https://www.ti.com/lit/ds/symlink/tmux1109.pdf). | High. |
| C3 | DATA_CH0 is assembled correctly as 28 bits and transported byte-correctly. | Production firmware lines 171–205 and 505–536; receiver decoder in `MSV2_ESP32_Display_Runtime.ino`, lines 6563–6569; [FDC2214 datasheet](https://www.ti.com/lit/ds/symlink/fdc2214.pdf). | High. A flashed-binary mismatch remains possible but live count scale is consistent. |
| C4 | The old LFCLK/RTC1 unsafe state is not active in the installed toolchain. | `MSV3/scripts/arduino_msv3_probe.ps1 -Action doctor`, run 2026-08-30; [`MSV3_LFCLK_RTC1_HANG_2026-08-21.md`](../MSV3_LFCLK_RTC1_HANG_2026-08-21.md). | High for future builds with the guarded script; not a binary attestation. |
| C5 | Live sensor 4 has a repeatable diurnal component near 0.8% peak-to-peak after day normalization. | Direct Supabase query of `msv2_sensor_history.raw_fdc_counts`, 7,398 rows, 2026-08-24 through 2026-08-30; hourly residual calculation. | High for the queried interval. Temperature was not recorded, so “thermal” is an inference from timing, not a direct regression. |
| C6 | The only abrupt event near the stated watering was at 20:20 PDT, not 21:00 PDT. | Same live query: pre-event mean 18,795,318; post-gap mean 18,661,647; abrupt low at 20:19:59 PDT followed by a 29.2-minute history gap. | High for timestamps/counts; medium that watering caused it. |
| C7 | That step is physically plausible and has the correct moisture sign. | FDC transfer relation in datasheet; `D` fell 0.711%, implying total effective capacitance rose 1.438%. At nominal L/fref this is about +5.8 pF. | High for ratios; absolute pF is approximate. |
| C8 | Internal oscillator drift alone is too small to explain the daily waveform. | FDC datasheet: internal oscillator mean temperature coefficient −13 ppm/°C; measured daily component ~8,058 ppm peak-to-peak. | High that it is not sufficient alone; oscillator still contributes and external clock is preferable. |
| C9 | The design is not operated at maximum FDC precision. | Production firmware lines 188–194 (`RCOUNT=0x0400`, low-power activation, untuned IDRIVE); FDC datasheet conversion-time/ENOB and activation guidance. | High. Longer RCOUNT reduces conversion noise, not temperature selectivity. |
| C10 | Error flags are not reliably tied to the transmitted conversion. | Firmware reads DATA MSB/LSB and only then STATUS, lines 200–210; FDC datasheet says a DATA read clears that channel's latched status. DATA MSB can expose per-conversion flags when `ERROR_CONFIG` is enabled, but current code leaves its default disabled. | High. At 100 kHz I2C, later conversions can occur during the sequence. |
| C11 | Fresh-data readiness is not checked. | `probeWake()`, lines 368–391; no DRDY/CH0_UNREAD polling; FDC datasheet STATUS definitions. | High. Contribution to daily drift is unproven. |
| C12 | A failed TMUX is possible but not required. | [`MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md`](../MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md), lines 110–125: one real board had no mux-dependent count change. Live sensor-4 count is electrode-connected and reacts to capacitance. | High that failures exist; medium/low that this deployed mux is failed. |
| C13 | A healthy TMUX materially loads the tank. | TMUX1109 datasheet: about 38 pF on-capacitance, 7 pF source-off and 32 pF drain-off capacitance, and 8.75 Ω typical on resistance at 3.3 V. | High. Exact effective differential loading is layout/state dependent. |
| C14 | External muxing is not a clean precision architecture for this sensor. | FDC datasheet advises against hot-swapping; TI support gives nuanced guidance but repeatedly points to mux bandwidth/parasitics and recommends checking tank amplitude/frequency: [TI discussion 1](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/508989/fdc2214-external-multiplexers-or-alternative-recommendation), [TI discussion 2](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/1050489/fdc2214-capacitance-jumps-and-active-shielding), [TI discussion 3](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/1333666/fdc2214-active-shield). | Medium/high. TI's answers are not perfectly consistent; this design switches while shutdown, avoiding the literal hot-swap case. Persistent parasitics remain. |
| C15 | Electrode polarity swapping is redundant for the FDC measurement itself. | TI explains that the FDC drive current alternates between INxA and INxB: [TI FDC2214 drive discussion](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/836018/fdc2214-fdc2214rght). | High for FDC excitation; the mux may still serve the separate DC EC test. |
| C16 | Soil temperature/conductivity can change a capacitance/frequency sensor independent of water content. | [Seyfried & Grant, 2007](https://digitalcommons.unl.edu/icwdm_usdanwrc/1065/); [Viscosity/temperature study](https://acsess.onlinelibrary.wiley.com/doi/full/10.2136/vzj2015.07.0098); [USDA review](https://www.ars.usda.gov/arsuserfiles/20361500/pdf_pubs/P1878.pdf). | High in principle; magnitude for this exact soil/frequency must be measured. Lower MHz operation increases sensitivity to loss and conductivity. |
| C17 | The cheap continuous sensor is not inherently more accurate, but its geometry and continuous equilibrium can yield a larger useful fraction of its signal here. | [Capacitive Soil Moisture Sensors Do Not Work Correctly…](https://pmc.ncbi.nlm.nih.gov/articles/PMC7348898/): v1.2 devices operate near 1.5 MHz and remain sensitive to porosity/salinity and construction. | Medium. A direct side-by-side controlled test is required. |
| C18 | The paired bare-tank ratio cancels reference-clock and inductor common mode to first order. | Algebra from FDC transfer equation: `C_connected/C_bare = (D_bare/D_connected)^2` when L and fref are common; diagnostic implementation described in [`MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md`](../MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md). | High first-order discriminator. The mux's off capacitance remains in the “bare” reading. |
| C19 | The diagnostic data needed for that separation are not live. | Direct Supabase query: board temperature, bare count, and phase all null; diagnostic document lines 3–7 says implemented/compile-verified but not deployed. | High for queried rows. Could be old probe firmware, old receiver firmware, or both. |

## Live-data calculations

### Watering-adjacent transition

- User-reported time: 2026-08-29 21:00 PDT = 2026-08-30 04:00 UTC.
- Stable pre-transition mean: 18,795,318 counts.
- First abrupt low: 18,599,645 at 2026-08-29 20:19:59 PDT.
- History gap: approximately 1,749.8 seconds. Probe wake counter advanced from 403 to 428,
  which argues for receiver/history loss rather than a probe reboot.
- Stable post-gap mean: 18,661,647 counts.
- Count change: −133,671, or −0.7112%.
- From `D ∝ 1/sqrt(C)`: `(18,795,318 / 18,661,647)^2 − 1 = +1.4377%` total
  effective capacitance.
- Approximate absolute values at nominal 43.4 MHz and 6.8 µH: 403.4 pF to 409.2 pF,
  approximately +5.8 pF.
- At 21:00 PDT itself, adjacent ten-minute means changed by only roughly 1,000–2,000 counts.

### Daily component

Peak-to-peak range of hourly raw-count means by local calendar day:

| Date (PDT) | Peak-to-peak counts | Fraction of daily level |
|---|---:|---:|
| 2026-08-24 | 188,214 | 0.99% |
| 2026-08-25 | 165,602 | 0.88% |
| 2026-08-26 | 169,229 | 0.90% |
| 2026-08-27 | 187,344 | 1.00% |
| 2026-08-28 | 127,387 | 0.68% |
| 2026-08-29 | 217,454 | 1.16% (contains the event/outlier) |

After subtracting each day's mean, the recurring local-hour profile spans about 8,058 ppm
(0.806%). This is comparable to the watering-associated count step.

Battery/charging is not a good explanation: only one queried row reported charging; daily-demeaned
level correlation with battery was about 0.19 and first-difference correlation about −0.04. The
front end also runs from a regulated 3.3 V rail.

## Root-cause synthesis

The FDC measures resonant frequency, not “water.” Its count contains every term that changes the
resonator or reference:

`D = 2^28 × f_sensor/f_ref`, with `f_sensor = 1/(2π√(LC_total))`.

The deployed level implies roughly 403 pF total under nominal assumptions. C17 alone is 270 pF,
so the external electrode, mux, PCB, and FDC pin path contribute only the remainder while all
thermal and conductive changes act on the same frequency. The observed watering step is about
5.8 pF—well above the converter's count resolution, but no larger than the combined daily change
of the surrounding physical network. This is a selectivity/reference problem, not a raw bit-depth
problem.

The FDC datasheet does not prescribe the project's previously cited “300 pF–2 nF recommended
window.” Its representative tank is 18 µH/33 pF, and it requires only that sensor frequency,
amplitude, inductor self-resonance margin, and settling be valid. A large C0G capacitor is not
automatically invalid, but it dilutes fractional electrode changes and forces the remaining LC
components and parasitics to be extremely stable.

The 6.8 µH Coilcraft LPS4018-682MRC is specified as a shielded power inductor (±20%, 0.15 Ω DCR,
50 MHz SRF); the manufacturer page does not specify temperature coefficient or Q stability for
precision resonant sensing. It should not be assumed thermally negligible without a fixed-C
thermal test. [Coilcraft product data](https://www.coilcraft.com/en-us/products/power/shielded-inductors/ferrite-drum/lps/lps4018/?skuId=5003)

## Decisive experiments

1. Deploy and verify the existing 23-byte probe + compatible receiver diagnostic path. Reject
   “deployed” until `/data` and cloud rows contain non-null board temperature, bare-tank count,
   and tank phase.
2. Plot connected count, bare count, `(D_bare/D_connected)^2`, phase, actual soil temperature,
   and board temperature. The ratio is the fastest discriminator available on existing hardware.
3. Correct acquisition observability in a diagnostic build: poll DRDY/CH0_UNREAD with timeout;
   capture STATUS before DATA or enable `ERROR_CONFIG` and retain DATA MSB error bits; log register
   readback; use 400 kHz I2C; compare full-current activation with current low-power activation.
4. Scope IN0A/IN0B and tune IDRIVE so the tank remains in the datasheet's 1.2–1.8 Vpk range for
   dry, wet, hot, and cold conditions. Treat amplitude flags as part of the exact sample.
5. Compare `RCOUNT=0x0400` with `0xFFFF`. At typical 43.4 MHz, maximum RCOUNT is only about
   24.2 ms—trivial in a 10-second cadence—and should reduce conversion noise materially.
6. Run `muxsweep` on the exact deployed board. Healthy: codes 0/1 agree and codes 2/3 show the
   established approximately 11% bare-tank step. Known failed-mux signature: all codes agree
   within approximately 0.02%.
7. Run a fixed-C0G A/B thermal test: same FDC/tank continuously active versus cold-started every
   ten seconds; then repeat with the TMUX bypassed. This cleanly separates duty-cycle acquisition
   from analog-front-end thermal drift.
8. Repeat direct-electrode measurements with the TMUX bypassed. If fractional water response
   grows or thermal residual shrinks, the mux path is a material contributor.

## Interpretation matrix

| Result | Interpretation |
|---|---|
| Connected and bare move together; ratio stable | Common fref/L/tank thermal drift; external clock and/or matched reference channel should help. |
| Bare stable; connected/bare ratio follows temperature | Soil/electrode/coating or selected mux-path effect. |
| Continuous mode is stable; cold-wake mode is not | Wake/readiness/activation/drive problem. |
| TMUX bypass improves fractional water response or drift | Mux R/C/parasitic path is materially degrading the signal. |
| Phase 0 and phase 1 diverge | Mux/wiring/electrode asymmetry; old phase-mixed history is not suitable for calibration. |
| Tank amplitude leaves 1.2–1.8 Vpk or flags assert | IDRIVE/Q/settling must be corrected before environmental conclusions. |
| Bare approximately equals connected | Electrodes are disconnected or mux is stuck in an EC/bare route. |

## Architecture direction

For the next board, connect the moisture electrodes directly to FDC CH0 without the TMUX in the
resonant path. Populate CH1 with a geometrically and thermally matched sealed/reference LC channel,
using the same precision-class inductor and capacitor family. Use a stable external reference
clock if the reference-channel residual shows oscillator contribution. Keep EC measurement on
separate electrodes/path, or switch it with a part and layout chosen from an explicit parasitic
budget. The FDC's own alternating differential drive makes electrode swapping unnecessary for
the FDC measurement.

This preserves what the FDC is good at—fine frequency resolution—while adding what the present
design lacks: a way to distinguish moisture-dependent capacitance from temperature, oscillator,
inductor, mux, and PCB movement.
