# MSV4 board 5 full bring-up retest — analog path restored (2026-10-29)

Dated measurement record. Current physical state lives in `MSV3/fleet.json` under board 5.

After board 5's first full suite found both U4 readout channels nearly dead, the maintainer did
intervening board work and stated that board 5 was back online. The maintainer later confirmed
that U4 was resoldered, and that U3 was also resoldered as it had to be on every board. Blade
condition was not stated. The identical full BLE-console suite was rerun.

## Result: pass

| Check | Failed run | Retest |
|---|---:|---:|
| Open SENSE fundamental | 26.38 mV | 783.44 mV |
| Open DRV fundamental | 15.55 mV | 772.26 mV |
| A SENSE | 27.12 mV | 756.54 mV |
| B SENSE | 27.91 mV | 754.92 mV |
| Open Cp | 0.00 pF, invalid | 8.78 pF |
| Spectrum rms | 15.58 | 0.02290 |
| `spectrum ab` | aborted | completed |
| Cd | invalid | 15.07 pF |

A and B now differ by only 0.21%. DRV remains stable between them at 771.26 and 770.79 mV.
The spectrum fit is dry/open-like: Rsoil 9.59e15 ohm, Cm at the zero boundary, Cd 15.07 pF.
These values closely match passing boards 3 and 4.

The rest of the suite also passed: exact v262152 build, LFCLK XTAL (386 ms), VDD 3288 mV,
VBAT 4055 mV, temperature electrical path 23.62 C / 10541 ohm, SAADC calibration, 16-phase
sweep, and initial/final safe pins. The final `safe` command succeeded.

Measured conclusion: the U4 analog-path failure is resolved. Remaining physical check: MSV4 Lab
hand-on/hand-off response. Then assign a sensor ID and install signed production firmware.
