# MSV4 board 5 full bring-up — analog front end failure (2026-10-29)

Dated measurement record. Current physical state lives in `MSV3/fleet.json` under
`msv4_boards.boards[board 5]`.

Board 5 (`MSV4-BU-9FA3`, `C8:48:67:8B:9F:A3`) ran the same unattended BLE-console suite as
boards 3 and 4 after its BQ25504-pad short was repaired and it was flashed over SWD.

## Passed

- Exact build `MSV4-BUILD:bringup-live-2026-10-11`, application 262152.
- LFCLK XTAL running, 393 ms start.
- Initial and final safe pin states.
- VDD 3308 mV; VBAT 4057 mV (not meter-checked).
- Temperature electrical path: 23.90 C, 10429 ohm; die 24.75 C.
- SAADC calibration and command/control path.
- Final `safe` returned OK.

## Failed: analog front end

At 1 MHz with the chopper enabled and electrodes open, passing boards 3 and 4 measured about
780 mV of fundamental on both channels. Board 5 measured:

| State | SENSE fundamental | DRV fundamental |
|---|---:|---:|
| open | 26.38 mV | 15.55 mV |
| A | 27.12 mV | 7.15 mV |
| B | 27.91 mV | 5.91 mV |

The 16-phase open sweep likewise had only 27.76 mV SENSE and 38.49 mV DRV fundamental. DRV_OUT
also changed strongly with mux state instead of remaining stable.

`spectrum open` was physically impossible: magnitude 1.6 to 45, Cp 0.00 pF, rms 15.58.
`spectrum ab` completed A and then aborted with `ERR DRV_OUT has no fundamental at N=6`.
The subsequent moisture fit is invalid.

## Interpretation and next check

Measured conclusion at the time: board 5's analog readout did not work. U4, the FSUSB42 chopper
shared by both SENSE_OUT and DRV_OUT, was the leading inferred fault location because both channels
lost their fundamental. The maintainer later confirmed U4 was resoldered and the complete retest
passed; U3 was also resoldered, as on every board.

Inspect/reflow U4 first, then inspect R18/R19/C18/C19 and the nearby DRV/SENSE paths for bridges
or opens. Meter R15 from DRV to SENSE against its intended fitted value. Rerun the complete suite
after repair. Do not assign a sensor ID or install production firmware yet.
