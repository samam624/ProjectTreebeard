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
