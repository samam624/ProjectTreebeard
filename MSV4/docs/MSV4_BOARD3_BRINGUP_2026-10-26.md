# MSV4 board 3 bring-up — 2026-10-26

Dated record. Current physical state lives in `MSV3/fleet.json` → `msv4_boards.boards[board 3]`.

**Board:** advertising as `MSV4-BU-5667` at `E7:5F:AD:54:56:67`. Numbered 3 as the next board
reported working; the maintainer hasn't given it a number.
**Flash:** SWD on the COM4 ESP32-S3 flasher (reflashed from `main` the same session) with
`--msv3 1.0.3 --app MSV4_Probe_Bringup_BLE-v262152-live-2026-10-11.hex`. The maintainer ran it and
reported the LED blinking. The script's output wasn't reported.
**Image running (console `info`):** `MSV4-BUILD:bringup-live-2026-10-11`, app version 262152,
uptime 125 s, reset reason 0x1, boot count 1.
**R15 is 1 kΩ, the netlist value (maintainer, ~13:30 PDT).** The console's fits apply as printed.
**Not stated:** rework, U2 part, power source, and whether the blade was clean, dry and in air.
**Tip NTC (R17) not fitted** on this board and one other batch board (maintainer, ~13:20 PDT).

All rows are console readings over BLE (`msv4_ble_console.py`), 13:12–13:20 PDT.

## Passed

| Check | Board 3 | Board 1 / board 2 |
|---|---|---|
| `info` LFCLK | XTAL running, RC at setup entry, start wait 356 ms | 250–380 ms |
| `vdd` | 3321 mV | 3250–3350 |
| `vbat` | 3999 mV | not checked against a meter |
| `drive 16` + `pins` | 1 MHz, DRV hi-drive under GPIOTE, READ_CNTL under GPIOTE | same |
| `pins` after `safe` | DRV, VBAT_DIV_EN, TEMP_READ low; both /OE and both Sel released | same |
| `spectrum open` | Cp = 7.07 pF ±11 %, fit rms 0.0178 | board 1 8.8 pF; board 2 28.8 pF through its 3.3 kΩ R15 |

## Failed

**`temp`:** `ERR temp read failed` three times. Explained by the missing R17: with no NTC the
divider ratio is above the firmware's 0.999 limit.

**Electrode swap (U3).** `iq 16` at 1 MHz, `chop on`:

| State | SENSE | DRV | Board 1 in air |
|---|---|---|---|
| `swap open` | 785.8 mV ∠177.9° | 775.7 mV | 787.7–788.1 mV ∠178.8° |
| `swap a` ×2 | 786.2–786.7 mV ∠178.0° | 776.2–776.6 mV | 761 mV ∠−177.5° |
| `swap b` ×2 | **3.5 mV** | **745.1–745.4 mV** | A and B agree to 0.4 % |

`sweep 16` agrees: on `swap a`, sense amplitude 668.4 mV, residual 57.6 mV, the same as an open
reference; on `swap b`, sense stays between 0 and 7 mV over all 16 phases.

Measured:
- `swap a` reads the same as `swap open` to 0.1 %. Board 1 drops 3.4 % on `swap a` in air from the
  electrode's own capacitance. Here electrode A adds nothing.
- `swap b` pulls SENSE to 0.45 % of its open value and loads DRV by 4 %. Through a 1 kΩ R15 that's
  about 5 Ω to ground: one FSUSB42 on-resistance (3.9 Ω typ, 6.5 Ω max), not a soil or hand path.

The open response (786 mV, Cp 7.1 pF) matches board 1's, as expected with the same 1 kΩ R15.

Inferred, not yet checked on the board:
- In `swap b`, U3 connects SENSE to MS_B (pin 6 → pin 2), so **MS_B is tied to ground**, at U3 or on
  the blade. In `swap a`, SENSE should connect to MS_A (pin 5 → pin 1), so **MS_A isn't connected**.
  Both faults on one leadless 1.4 × 1.8 mm part point to U3's solder joints. Board 2's U3 needed
  reflowing twice too.

## Open

- [ ] Reflow U3, then rerun `swap a/b/open` + `iq 16`. Pass: A and B within about 1 % of each other,
      both a few percent below open.
- [ ] If B still reads near 0 mV after the reflow: look for a short from the B plate to ground on the
      blade (the build guide's mask check).
- [ ] Fit R17 (NTC), then `temp`, and a finger on the tip moves it.
- [ ] Then: `spectrum ab`, the MSV4 Lab hand test, sensor ID, production image.
