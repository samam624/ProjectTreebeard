# MSV4 board 4 bring-up — 2026-10-26

Dated record. Current physical state lives in `MSV3/fleet.json` → `msv4_boards.boards[board 4]`.
Same session and checks as [board 3](MSV4_BOARD3_BRINGUP_2026-10-26.md).

**Board:** advertising as `MSV4-BU-D7D2` at `C1:63:B3:84:D7:D2`. The maintainer called it board 4.
**Flash:** SWD on the COM4 flasher, same command as board 3 (bootloader 1.0.3 + bring-up v262152).
**Image running (console `info`):** `MSV4-BUILD:bringup-live-2026-10-11`, app version 262152,
uptime 62 s, reset reason 0x1, boot count 1.
**Not stated:** R15, R17 (NTC), rework, U2, power, and whether the blade was clean, dry and in air.

All rows are console readings over BLE, 13:25–13:29 PDT.

## Passed

| Check | Board 4 | Board 3 / board 1 |
|---|---|---|
| `info` LFCLK | XTAL running, RC at setup entry, start wait **400 ms** | 356 ms / 250–380 ms |
| `vdd` | 3293 mV | 3321 / 3250–3350 |
| `vbat` | 4029 mV | 3999 mV |
| `drive 16` + `pins` | 1 MHz, DRV hi-drive under GPIOTE | same |
| `pins` after `safe` | all low / released | same |
| `spectrum open` | Cp = 7.11 pF ±11.2 %, rms 0.0181 | 7.07 pF / 8.8 pF |

The 400 ms crystal start is just past boards 1–2's range. The crystal runs, so this isn't a failure.

## Electrode swap (U3)

`iq 16` at 1 MHz, `chop on`:

| State | SENSE | DRV | Board 1 in air |
|---|---|---|---|
| `swap open` | 780.1 mV ∠177.9° | 769.2 mV | 787.7–788.1 mV ∠178.8° |
| `swap a` ×2 | **779.2–779.7 mV ∠177.8°** | 768.7–769.7 mV | 761 mV ∠−177.5° |
| `swap b` ×2 | 757.2 mV ∠−177.9° | 768.6 mV | A and B agree to 0.4 % |

Measured:
- **B is normal:** 2.9 % below open with the phase moved across 180°, like board 1's A in air (3.4 %).
- **A reads the same as open** to 0.1 %. Electrode A adds nothing.
- `temp` failed (`ERR temp read failed`), as on board 3 with no NTC.

Inferred, not checked on the board: MS_A isn't connected to SENSE, at U3 or on the MS_A trace. That's
the same A symptom as board 3. Board 3 also had B tied to ground; board 4's B is fine.

## Open

- [ ] Reflow U3 and rerun the swap test. Pass: A within about 1 % of B.
- [ ] Is R17 fitted? If it is, the `temp` failure is a separate fault.
- [ ] Then: `spectrum ab`, MSV4 Lab hand test, sensor ID, production image.
