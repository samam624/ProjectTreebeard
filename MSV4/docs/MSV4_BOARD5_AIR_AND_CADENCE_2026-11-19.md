# Board 5 air calibration and reporting cadence, November 19–20, 2026

Board 5 is sensor 14 at `C8:48:67:8B:9F:A3`. The maintainer confirmed a 5.6 kΩ R15,
a successful hand-on/hand-off test earlier in the day, and a blade in air on the desk.
The #5 label was still on the probe during the bring-up sweeps below.

## What the two firmwares reported

| Measurement | Bring-up console, v262152 | Hourly firmware, v262173 |
| --- | --- | --- |
| R15 used by the fit | 1 kΩ | 5.6 kΩ |
| Cp, floating electrodes | 50.20 pF as displayed, 8.96 pF when fit using the fitted R15 | DIS showed `Cp 0.00 pF default` on wake 2 |
| Rsoil, blade connected | 23.55 kΩ as displayed, 131.9 kΩ on the 5.6 kΩ scale | 127.5 kΩ from the first measurement |
| Cp + Cd | (50.20 + 86.00) / 5.6 = 24.32 pF | 0.00 + 24.39 = 24.39 pF |
| Blade fit RMS, fraction of drive | 0.00836 | 0.0086 |

The console's `spectrum open` command disconnects the electrodes and measures the SENSE-node
parasitic capacitance. Production firmware starts with its compiled Cp. On a valid blade
measurement it runs the floating-electrode sweep only if the fitted Rsoil reaches 400 kΩ and
the RMS residual is below 0.0255. Wake 1 passed the residual test and missed the resistance
threshold. This build had no stored Cp, so its DIS field retained zero and its Cd field
included the node parasitic. The spectrum and default-selection paths are in the
[bring-up sketch](../arduino/MSV4_Probe_Bringup_BLE/MSV4_Probe_Bringup_BLE.ino) and
[production sketch](../arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino).

## Open sweep before the update

| Frequency (kHz) | Gain | Phase (°) |
| ---: | ---: | ---: |
| 250.000 | 0.9643 | −4.67 |
| 333.333 | 0.9611 | −6.23 |
| 500.000 | 0.9519 | −9.23 |
| 666.667 | 0.9380 | −12.12 |
| 1000.000 | 0.9087 | −17.76 |
| 1333.333 | 0.8694 | −22.63 |
| 2000.000 | 0.7837 | −30.88 |
| 2666.667 | 0.7047 | −36.90 |

Fitting these eight printed complex points with the firmware's harmonic weighting reproduced
50.199 pF at 1 kΩ and gave 8.964 pF at 5.6 kΩ. Both fits had RMS 0.02061. The open divider
response depends on the product of R15 and Cp. The tape's contribution to that measurement
was not independently measured.

## Hourly test

The signed package used `-MoistureEveryNWakes 60 -RsOhms 5600 -SensorId 14`.
Its SHA-256 was `9fdafa39acca90de8fafc400ffb571658cc4392304f87d566c37efa2e65f60c0`.
BLE Device Information returned `MSV4-BUILD:hourly-rs5600-2026-11-19` and version 262173.
The scanner observed consecutive wakes 2, 3 and 4. At wake 4 the radio switched from the
startup connectable windows to short bursts. The maintainer removed the tape and left the
blade in air for the next fresh measurement. The [fleet registry](../../MSV3/fleet.json)
records the installed image and the current test.

With the label off, wake 61 at 23:56:34 PST reported 128.458 kΩ, Cm 25.1 pF,
Cd 23.71 pF and a fit RMS of 0.0085. The automatic air threshold was still unmet.

## Thirty-minute test configuration

The maintainer requested one fresh reading every 30 minutes with the radio off between
reports. Build `fresh30-rs5600-2026-11-19` (262174) uses
`-SensorId 14 -RsOhms 5600 -MoistureEveryNWakes 30 -FreshOnly -ReportBootOpenCp`.
Its signed OTA package has SHA-256
`28589fd5a1918c0ea7ddcc9a3343e52c3443e495775a72778a1e5327e1e8d56d`.
Each scheduled cycle measures the blade, battery and temperature and opens one five-second
BLE advertising window when OTA is available. A failed blade sweep sets the moisture-invalid
flag for that report. A radio start failure retries in one minute.

On the first cycle the firmware also measures Cp with the electrodes disconnected. The DIS
hardware revision reports this as `boot open` alongside the Cp applied to the moisture fit.
This build has a 0.00 pF compiled default. Only the existing automatic air calibration can
change the applied value.

The signed transfer completed in 40 seconds. At the first reporting window, DIS on the same
board returned `MSV4-BUILD:fresh30-rs5600-2026-11-19`, software revision `262174` and
`Cp 0.00 pF default; boot open 8.94 pF`. The earlier refit with the tape on gave
8.96 pF. The first blade report,
wake 1 at 23:57:25 PST, carried the fresh and valid flags, 134.334 kΩ, Cm 25.6 pF,
Cd 23.74 pF and RMS 0.0089. Both the old image's wake 61 and the new image's wake 1
returned all eight complex blade sweep points.

Wake 2 arrived at 00:27:23 PST on November 20, **29 minutes 58 seconds** after wake 1.
It reported a valid fresh blade sweep of 134.185 kΩ, Cm 17.9 pF, Cd 23.64 pF and RMS
0.0082, with battery 4015 mV and tip 22.75 °C. The continuous BLE capture recorded just
those two reporting windows and their raw sweeps. Wall-clock times here are when the
scanner first heard a packet from each window.

## First ten hours on the 30-minute build

Cloud history for sensor 14, wakes 1–21 of 262174, 23:57 PST on November 19 to 09:57 PST on
November 20. The physical state used here is the fleet registry's (desk, blade in air, verified
November 19). It was not re-confirmed for this window.

| Quantity | Result |
| --- | --- |
| Reports | 21 of 21, every one fresh and valid; intervals 1795.6–1804.1 s |
| VBAT | 3996–4022 mV, flat |
| Tip temperature | 23.00 → 21.75 °C overnight |
| Cd (includes the 8.94 pF node Cp, applied Cp is 0) | 23.64–23.93 pF, SD 0.07 pF |
| Rsoil | 124.6–135.4 kΩ, mean 132.1, SD 3.4 kΩ |
| Cm | 17.7–63.9 pF, SD 9.6 pF (one wake at 63.9) |
| Stored moisture number | 4916–5334, mean 5077, SD 121 |

Measured: the stored number follows Rsoil (r = −0.97). Rsoil does not follow the tip temperature
(r = +0.17). Lower-Rsoil wakes have the larger fit residuals (r = −0.77). Removing the tape lowered
Cd from 24.39 to 23.71 pF.

Inferred: the 132 kΩ branch is a weak conductance of about 7.6 µS at 1 MHz. The same frequency
carries about 93 µS through Cd net of Cp. That conductance is close to the fit's resolution in
air, which explains the ±4 % Rsoil scatter and why Cm is poorly determined. Board 2 showed the same
kind of air branch, 87 kΩ in series with 2.3 pF
([board 2 bring-up](MSV4_BOARD2_BRINGUP_2026-10-13.md)), while board 1 reads open in air. The
branch keeps Rsoil below the 400 kΩ automatic-calibration threshold, so this board will not apply
an air Cp on its own. Whether the branch comes from the blade's surface or from residue left by
the rework has not been tested.

## Compared with the other boards (November 20)

| Board | R15 | Blade in air | Source |
| --- | --- | --- | --- |
| 1 | 1 kΩ | open in 292 of 293 production readings, Oct 6–8 | cloud, sensor 10 |
| 2 | 3.3 kΩ | never open: 847 desk readings, median 87.5 kΩ (50–144); 66 outdoor, median 92.5 kΩ | cloud, sensor 11 |
| 3, 4 | 1 kΩ | open (4e16 Ω, 4.7e14 Ω), bring-up console | [boards 3/4](MSV4_BOARDS3_4_FULL_BRINGUP_2026-10-29.md) |
| 5 | 1 kΩ on Oct 29 | open, 9.59e15 Ω | [retest](MSV4_BOARD5_FULL_BRINGUP_RETEST_2026-10-29.md) |
| 5 | 5.6 kΩ now | never open, 124.6–135.4 kΩ | above |

A finite air branch shows only on the two boards whose R15 was swapped by hand to a larger value.
The data cannot separate two explanations. A larger R15 makes the same leak 3.3 or 5.6 times more
visible against the fit's residual, which could also hide one on the 1 kΩ boards. Alternatively,
the hand swap left residue.

The electrodes-disconnected (`spectrum open`) sweeps were refitted with a conductance to ground
on the SENSE node added to Cp. The fit uses the firmware's harmonic sum and an unweighted
complex residual:

| Sweep | Cp only: Cp, RMS | Cp + G: Cp, G, RMS |
| --- | --- | --- |
| Board 5, 8 points, Nov 19, at 5.6 kΩ | 8.964 pF, 0.0292 | 8.948 pF, **5.42 µS**, 0.0141 |
| Board 2, 3 points, Oct 13, at 3.3 kΩ | 8.471 pF, 0.0253 | 8.471 pF, −0.13 µS, 0.0253 |

Measured: board 5's node shows about 5.4 µS (about 185 kΩ) with the blade disconnected. Board 2's
node shows none. Inferred: most of board 5's 7.6 µS air branch is on the SENSE node (R15, U3, U4
or the trace between them), not on the blade. Board 2's branch is on the electrode side. So the
two boards share a symptom but probably not a location. Board 2's figure rests on only three
points.

### What the leak could be (inference, November 20)

The fit model is H = 1/(1 + Rs·(Y + jωCp)). An error that is fixed in H shows up as a false
admittance of about δH/Rs. Model truncation, edge shape and ADC error are all of that kind. They
would read smaller on a 5.6 kΩ board than on a 1 kΩ board, which is the opposite of what is seen.
A real admittance of fixed size, by contrast, moves H in proportion to Rs. A 5 µS conductance moves
H by 0.005 at 1 kΩ, which is inside the roughly 0.02 open-fit residual on boards 1, 3 and 4. It
moves H by 0.017 at 3.3 kΩ and 0.028 at 5.6 kΩ. So the pattern fits a real conductance of a few
µS that every board may carry.

Candidates:

1. **The chopper's filter load.** U4 connects SENSE to R18 (100 kΩ into C18, 1 nF) for half of
   each cycle and parks R18 on ground for the other half. That is about 0.5/100 kΩ = 5 µS on
   SENSE, by design, on every board, present with the electrodes disconnected. The fit model does
   not include it. Board 5's electrodes-disconnected refit found 5.4 µS, with the residual lowest
   between 5 and 6 µS. The load is time-varying, so a constant G is only an approximation of it.
2. **Dielectric loss in the laminate and soldermask** around the electrode feeds and blade. It
   is on every board and grows with frequency. A fit renders it as R in series with C, which is
   the shape board 2 shows (87 kΩ + 2.3 pF). Dry FR4 at a tan δ of about 0.02 gives about 2 µS on
   15 pF at 1 MHz, and more if the laminate has taken up water.
3. **Residue from hand rework.** It would be board-specific and vary with humidity, and it is the
   only candidate that an IPA scrub would remove.

Board 2's three-point electrodes-disconnected sweep can neither confirm nor exclude 5 µS. Its
Cp-only residual is already 0.025, and adding G only raises it, because the 2.67 MHz point pulls
the other way.

## One-minute build (262176): steadier readings, first 17 minutes

Wakes 2–17, 10:29–10:44 PST on November 20: Rsoil 133.8–136.2 kΩ, SD about 0.5 kΩ (0.4 %),
against 3.4 kΩ (2.5 %) for the 30-minute readings overnight. Fit residual 0.0081–0.0083 on every
wake.

Measured:

- **The fit is not the cause.** Five live sweeps (wakes 13–17), captured with
  `msv4_adv_scan.py --sweep-csv`, were run through the firmware replica in
  `test_msv4_warm_fit.py` with Rs 5600. All four cold starts landed on the same answer on each
  sweep, 133.6–134.7 kΩ. Wake 13 was the firmware's own full fit and gave 134.0 kΩ, the same as
  the warm fits.
- **The scattered 30-minute readings come from worse sweeps.** Every 30-minute reading below
  about 133 kΩ has a residual of 0.0087–0.0101. The readings near 134.5 kΩ, and every one-minute
  reading so far, have 0.0081–0.0083. The disturbed readings change the sweep's shape, which a
  shift in the leak's size would not do. Which SAADC calibration variant a wake used does not
  line up with the disturbed wakes.

Not separated yet: 17 minutes of one-minute data against 10 hours of 30-minute data. The cause is
either something that settles between 1 and 30 minutes after a wake, or a slow room cycle that
30-minute sampling turns into scatter. Taking every 30th one-minute reading over several hours
distinguishes the two.

## Switched R18 load test (November 20)

Script: [msv4_chopper_load_fit.py](../scripts/msv4_chopper_load_fit.py). It solves the circuit
exactly in periodic steady state, with one matrix exponential per switching segment. The model
has DRV through R15 into SENSE, the node capacitance and the Cm/Gs branch, and U4 connecting R18
(100 kΩ into C18, 1 nF) to SENSE during the chopper-low half and to ground otherwise. The output
is the period mean of C18 over the firmware's phases. `--self-test`: with the load removed, the
simulator matches the divider's full harmonic sum to within 8.8e-6. RMS below is the unweighted
complex residual, which is √2 times the firmware's figure.

| Input | Firmware model | With switched R18 |
| --- | --- | --- |
| Synthetic open board (Cp 8.94, Cd 14.8 pF), Rs 1k / 3.3k / 5.6k | reads 127.8 / 160.6 / **164.5 kΩ**, Cd 23.86–23.99 pF | open by construction |
| Board 5 `spectrum open`, Nov 19, Cp only | 8.964 pF, RMS 0.0292 | 8.868 pF, RMS **0.0147** (a branch adds almost nothing: 0.0135) |
| Board 5 live blade sweeps, wakes 13–17 (air) | 133.6–134.7 kΩ, Cm 18–23 pF, Cd 23.65–23.72 pF, RMS 0.0113–0.0117 | **40.5–44.0 kΩ in series with 1.6–1.7 pF**, Cd 22.30–22.36 pF, RMS 0.0051–0.0055 |

Measured:

- The R18 load fully explains the 5.4 µS node conductance seen with the electrodes floating.
- With the blade connected it explains the pure-resistive 134 kΩ reading and halves the
  residual. Cd net of Cp then comes out at 22.31 − 8.87 = 13.4 pF, matching boards 1 and 2 in
  air (13.5–13.7 pF).
- What remains is a separate, small electrode-side branch: 42 kΩ in series with 1.65 pF. At
  1 MHz that is about 3.8 µS real and 8.7 µS imaginary, with a relaxation frequency of 2.3 MHz.
  That shape is a lossy dielectric or a surface film on the blade. Which one is not determined.

The load also biases soil readings on every board. Synthetic sweeps with the load, fitted by the
firmware model (Cm 50 pF, Cd 13.5 pF, Cp 8.9 pF):

| True Rsoil | Rs 1k | Rs 3.3k | Rs 5.6k |
| --- | --- | --- | --- |
| 3 kΩ | −1.5 % | +0.2 % | +1.4 % |
| 10 kΩ | −6.6 % | −5.0 % | −4.7 % |
| 15 kΩ | −10.0 % | −8.0 % | −7.9 % |

That is a monotone compression of the dry end, which the stored-number calibration absorbs. It
is not a fault.

Not shown: why the 1 kΩ boards read open in air. The load moves H by at most 0.009 at 1 kΩ,
below those boards' roughly 0.02 open-fit residual, so they cannot resolve it. That is consistent
with the readings, but no 1 kΩ raw sweep was refitted.
