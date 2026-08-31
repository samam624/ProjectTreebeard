# MSV3 FDC2214 thermal drift and weak watering response

**Investigation date:** 2026-08-30  
**Status:** Historical pre-remediation analysis; later same-day evidence changed the leading cause  
**Bottom line:** The correct FDC channel and count are being read. The dominant problem is not
missing bits; it is that the high-resolution converter is measuring a thermally sensitive,
unreferenced total impedance in which the water-dependent change is only one small term.

> **Later 2026-08-30 update:** This report freezes the state before diagnostic telemetry was made
> live and before the coating was removed. Board temperature, bare-tank count, and phase subsequently
> arrived end to end; the bare reference removed about half of the first connected-count temperature
> slope. More importantly, liquid pockets were found beneath the nail polish. Removing the polish
> caused the long drift to reverse during drying and then settle to a flat air baseline. That physical
> evidence moves coating/edge water retention and soil/probe equilibration ahead of intrinsic FDC
> thermal drift as the leading field cause. Preserve the electronics analysis below, but use
> [the soil-ingress and coating-remediation record](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md)
> for the current diagnosis and test plan.

## Executive verdict

MSV3 is reading the intended sensor. FDC2214 CH0 is the only channel connected to the tank, the
firmware reads DATA_CH0 MSB before LSB and reconstructs all 28 bits correctly, and the receiver
reassembles the four transmitted bytes correctly. The live count is also in the established
electrode-connected range and shows a correct-sign capacitance response. A wrong channel, endian
bug, truncation, or receiver conversion error is therefore very unlikely.

The live complaint is real. Across 7,398 sensor-4 rows queried from August 24–30, hourly means show
roughly 0.68–1.00% normal daily peak-to-peak variation. After removing each day's mean, the
repeatable time-of-day profile spans about **0.806%**. That is comparable to the only abrupt
watering-adjacent change, **−0.711% in FDC count**, so the water signal is being buried by a real
environmental/front-end component—not by display smoothing alone.

The leading contributors are:

1. temperature-dependent complex permittivity and conductivity in the soil/electrode path at the
   approximately 3 MHz tank frequency;
2. unmeasured thermal drift in the LC/mux/PCB front end, especially an unqualified power inductor
   and substantial TMUX1109 parasitics;
3. dilution by the 270 pF fixed tank capacitance, which makes the watering response a small
   fraction of total resonant capacitance;
4. missing reference/temperature/phase telemetry, so none of those effects can yet be separated
   or compensated;
5. acquisition settings that leave performance and error observability on the table.

A physically bad TMUX is possible—one assembled board already had exactly that fault—but is not
the best explanation for this live series. A healthy TMUX is still an architectural liability.
The 300 ms pre-wake delay is already vastly longer than the switch settling time, and the FDC is
shut down during that delay. More delay there will not remove the mux's capacitance, resistance,
layout loading, or temperature dependence.

## What the live data say

The user-reported watering time was 21:00 PDT on August 29. There is almost no step at 21:00. The
only abrupt nearby transition is at **20:19:59 PDT**:

| Measure | Result |
|---|---:|
| Stable pre-event mean | 18,795,318 counts |
| First abrupt low | 18,599,645 counts |
| Stable post-gap mean | 18,661,647 counts |
| Stable count step | −133,671 counts (−0.7112%) |
| Implied total-capacitance change | +1.4377% |
| Approximate change at nominal 43.4 MHz / 6.8 µH | +5.8 pF |
| Missing receiver/cloud history after the first low | 29.2 minutes |

The probe wake counter continued across that gap, which argues against a probe reset or broken
10-second wake schedule. The event time should be reconciled with watering/handling notes before
the 20:20 transition is labeled definitively as watering. Nevertheless, its direction is correct:
more capacitance produces a lower FDC count.

All live board-temperature, bare-tank, and tank-phase fields are null. The diagnostic design is in
current source, but its own deployment note says it was compile-verified and not deployed. A newer
generated binary exists, but compilation does not prove which probe and receiver firmware are
running. Until all three fields appear in `/data` and cloud history, the system has no way to
separate tank drift, soil drift, and alternating phases. See
[`MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md`](MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md).

## The signal chain is correct

- The PCB connects `TANK_HOT` and `TANK_REF` directly to FDC2214 IN0A/IN0B. CH1–CH3 are grounded.
  The mux connects or redirects the external electrodes; it does not choose a different FDC
  channel. See [`PIN_NET_REFERENCE.md`](../hardware/MSV3_Probe_PCB/reference/PIN_NET_REFERENCE.md)
  and [`COMPONENT_INDEX.md`](../hardware/MSV3_Probe_PCB/reference/COMPONENT_INDEX.md).
- Production code reads register `0x00` first, then `0x01`, and reconstructs
  `((msb & 0x0FFF) << 16) | lsb`, matching TI's coherent-read requirement. See
  [`MSV3_Moisture_BLE_Sleep.ino`](../arduino/MSV3_Moisture_BLE_Sleep/MSV3_Moisture_BLE_Sleep.ino).
- The four-byte BLE field and receiver decoder use the same little-endian order. Raw FDC count is
  stored separately from the derived display value, so direct SQL is examining the real count.
- The required LFRC toolchain doctor passes. The known MSV3 RTC1/LFCLK hang is not a leading
  explanation for these samples.

## Why a more expensive FDC can look worse than the Amazon v1.2 sensor

The FDC2214 does not measure moisture. It measures the frequency ratio

`D = 2^28 × f_sensor/f_ref`, where `f_sensor = 1/(2π√(L C_total))`.

Its excellent digital resolution applies equally to water, soil conductivity, temperature,
inductor permeability/loss, capacitor and PCB drift, cable/electrode movement, and mux parasitics.
It is more sensitive, but not more selective.

At the live pre-event level, nominal component values imply about **403.4 pF total**. The fixed
C17 is already 270 pF. The watering-associated change is only about 5.8 pF, or 1.44% of total.
That is easy for the FDC to resolve, but it is also easy for the combined soil and analog tank to
move by a comparable amount over a day.

The [FDC2214 datasheet](https://www.ti.com/lit/ds/symlink/fdc2214.pdf) does not contain the project's
previously cited “TI-recommended 300 pF–2 nF” tank window. TI's representative tank is 18 µH and
33 pF; the real requirements concern frequency, amplitude, self-resonance margin, drive, and
settling. The 270 pF C0G part is not automatically invalid, but it dilutes the electrode's
fractional contribution.

The LPS4018-682MRC is a shielded ferrite power inductor specified at 6.8 µH, ±20%, 0.15 Ω DCR and
50 MHz SRF. Its available product data do not specify precision resonant temperature coefficient
or Q stability. It must be measured rather than assumed stable.
[Coilcraft product data](https://www.coilcraft.com/en-us/products/power/shielded-inductors/ferrite-drum/lps/lps4018/?skuId=5003)

The Amazon Capacitive Moisture Sensor v1.2 commonly runs continuously near 1.5 MHz with a much
simpler local electrode oscillator. In this installation, that geometry and continuous thermal
equilibrium may give water a larger fraction of its output. It is not intrinsically immune to
temperature, salinity, porosity, manufacturing variation, or corrosion; published testing shows
those confounds clearly. [Peer-reviewed v1.2 sensor analysis](https://pmc.ncbi.nlm.nih.gov/articles/PMC7348898/)

## Thermal mechanisms

The FDC's internal reference oscillator contributes drift but cannot plausibly be the whole daily
waveform. TI specifies a mean internal-oscillator temperature coefficient of about −13 ppm/°C.
Even a 20°C board swing would be only about 260 ppm, versus the observed roughly 8,058 ppm daily
profile. An external low-drift clock is still beneficial, but replacing the clock alone is
unlikely to produce an order-of-magnitude fix.

The larger and less controlled terms are the sensor tank and soil. Published dielectric work
shows that soil's real and especially loss components vary with temperature, texture, salinity,
and frequency. Lower-MHz sensors are more exposed to conductive and interfacial effects than
high-frequency dielectric measurements. See [Seyfried and Grant's 19-soil study](https://digitalcommons.unl.edu/icwdm_usdanwrc/1065/),
[frequency/temperature soil analysis](https://acsess.onlinelibrary.wiley.com/doi/full/10.2136/vzj2015.07.0098),
and the [USDA sensor review](https://www.ars.usda.gov/arsuserfiles/20361500/pdf_pubs/P1878.pdf).

Temperature correlation is still an inference: no actual soil temperature or live board
temperature accompanied these rows. The repeated time-of-day shape is strong evidence, but the
new telemetry is required to assign magnitude to each mechanism.

## TMUX assessment

The checked-in select mapping is correct. Codes 0/1 connect the electrodes to the FDC tank in
opposite orientation; codes 2/3 disconnect them from the resonant tank and route them to the EC
divider. A healthy reference board showed approximately 20.34 M connected versus 22.62 M bare—a
clean 11% step.

One newly assembled board has already demonstrated a real bad-mux signature: all four codes were
within about 0.02%, with Node A pinned independent of code. That makes a deployed mux fault worth
testing, but not assuming. See
[`MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md`](MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md).

Even healthy, the mux is not transparent. At 3.3 V, the
[TMUX1109 datasheet](https://www.ti.com/lit/ds/symlink/tmux1109.pdf) specifies roughly 8.75 Ω
typical on resistance, 38 pF on-capacitance, 7 pF source-off capacitance and 32 pF drain-off
capacitance. Those are material next to a 5.8 pF watering change. TI's FDC datasheet advises
against hot-swapping sensors; TI support later clarified that mux use can work if switching and
bandwidth are controlled, while also recommending direct amplitude/frequency checks. This design
switches while the FDC is shut down, so the literal hot-swap hazard is reduced, but the persistent
parasitics remain. [TI mux discussion](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/508989/fdc2214-external-multiplexers-or-alternative-recommendation)

The electrode swap also appears unnecessary for FDC moisture excitation. TI explains that the
FDC driver itself alternates constant current between INxA and INxB. The mux can remain useful for
a separate EC check, but it should not sit in the precision moisture path on the next design.
[TI drive explanation](https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/836018/fdc2214-fdc2214rght)

## Wake/acquisition assessment

The 300 ms mux delay is not the answer. It runs while FDC `SD` is high, whereas the switch operates
on a nanosecond scale. It can prevent switching a driven tank, but it cannot settle an oscillator
that is not yet running or remove mux R/C loading.

There are real firmware issues to fix in a diagnostic build:

- `RCOUNT_CH0=0x0400` gives only about 0.378 ms conversion time at typical 43.4 MHz—roughly the
  timing associated with 14 effective bits, not the part's headline performance. `0xFFFF` takes
  only about 24.2 ms, which is negligible in a 10-second cadence.
- The code waits a fixed 400 µs, blindly discards two reads, and never polls DRDY or
  CH0_UNREADCONV. The slow I2C and discards probably obtain a fresh normal sample, but do not prove
  it under wet/hot/cold extremes.
- The code reads DATA MSB/LSB and then STATUS. TI documents that reading DATA clears the channel's
  latched status. DATA MSB can carry that conversion's watchdog/amplitude flags when
  `ERROR_CONFIG` is enabled, but current code leaves those outputs disabled. At 100 kHz I2C,
  STATUS can belong to a later conversion. This is a concrete bug in error/sample association
  even though the 28-bit numeric count is correct.
- IDRIVE remains an explicit starting guess. Tank amplitude has not been verified over dry, wet,
  hot, and cold states against TI's 1.2–1.8 Vpk requirement.
- CONFIG selects low-power sensor activation although TI recommends full-current activation for
  robust start-up. The default 33 MHz deglitch filter is also much higher than the approximately
  3 MHz tank; the lowest filter above tank frequency should be evaluated.

These issues can add outliers or conversion noise. A smooth daily waveform remains more consistent
with temperature-dependent analog/soil behavior, so blindly extending wake delay would treat the
wrong mechanism.

## Fastest path to a decisive answer

1. **Make the existing diagnostics genuinely live.** Flash the compatible 23-byte probe and
   receiver builds using the repository scripts, then require non-null `boardTempQuarterC`,
   `bareTankFdcCounts`, and `tankPhase` in `/data` and cloud history.
2. **Add actual soil/tank temperature.** The nRF die temperature is only a board proxy.
3. **Use the paired ratio** `C_connected/C_bare = (D_bare/D_connected)^2`. This cancels the common
   FDC reference clock and inductor to first order.
4. **Run `muxsweep` on this exact deployed board.** Codes 0/1 should agree; codes 2/3 should give
   the known approximately 11% bare step. All codes within approximately 0.02% is the known bad
   mux signature.
5. **Fix measurement observability in a diagnostic build.** Wait on per-channel
   `STATUS.CH0_UNREADCONV` with a timeout and capture STATUS before DATA. If global DRDY is used
   instead, explicitly configure and verify `ERROR_CONFIG.DRDY_2INT` after every shutdown reset;
   otherwise DRDY is not a valid readiness indication. Also read back registers, evaluate 400 kHz
   I2C, compare full-current activation, and scope/tune amplitude.
6. **Increase RCOUNT for the comparison.** Test `0x0400` versus `0xFFFF`; do not expect this alone
   to remove thermal drift.
7. **Run one controlled A/B test on a fixed C0G load:** continuously active FDC versus ten-second
   cold wakes. Repeat with TMUX present and bypassed, across temperature. This separates wake
   behavior from the analog architecture without involving soil.
8. **Repeat direct-electrode testing with the TMUX bypassed.** This quantifies the mux penalty
   instead of debating whether the part has “failed.”

### Reading the results

| Observation | Meaning |
|---|---|
| Bare and connected counts drift together; ratio stays flat | Clock/L/tank common-mode drift. |
| Bare is stable; connected/bare ratio tracks temperature | Soil/electrode/selected mux path. |
| Continuous is clean; cold-wake is noisy | Activation/readiness/drive problem. |
| Bypassing TMUX materially improves water-to-daily ratio | Mux parasitic/loading problem. |
| Phase 0 and phase 1 differ | Mux/wiring/electrode asymmetry; phase-mixed history is invalid for calibration. |
| Bare approximately equals connected | Electrodes are disconnected or mux is stuck in a bare/EC route. |
| Tank amplitude leaves 1.2–1.8 Vpk | IDRIVE/Q/settling problem; environmental conclusions are premature. |

## Recommended next-board architecture

Connect the moisture electrodes directly to FDC CH0. Populate CH1 with a sealed, geometrically
and thermally matched reference LC channel instead of grounding three quarters of the FDC. Use
the same precision-class inductor/capacitor family in both channels, and select components whose
temperature behavior is actually specified. Add a low-drift external reference clock if the
paired-channel test shows residual clock contribution. Put EC measurement on separate electrodes
or a separately budgeted switching path.

That architecture lets the FDC's sensitivity work for the project instead of accurately reporting
every thermal and parasitic change as apparent moisture.
