# MSV4 board 2 bring-up — 2026-10-13

Dated record. Current physical state lives in `MSV3/fleet.json` → `msv4_boards.boards[board 2]`.

**Board:** advertising as `MSV4-BU-6528` at `CB:23:3B:C2:65:28`. It's the first of the batch-build
boards (2–5) reported working, so it's numbered 2 here. The maintainer hasn't given it a number.
**Rework (maintainer):** U3 (FSUSB42, A/B swap) and U4 (FSUSB42, chopper) were each reflowed twice
before it worked. The maintainer reports moisture sensing works; how it was tested wasn't stated.
**R15 is 3.3 kΩ (maintainer, 2026-10-13 ~22:20).** The maintainer swapped it in for the netlist's 1 kΩ. Every MSV4
sketch fits with `SPEC_RS = 1000.0`, so the console's fitted admittances on this board read 3.3× too large.
**Image running:** `MSV4-BUILD:bringup-live-2026-10-11`, app version 262152. Uptime 14760 s at
22:08 PDT, reset reason 0x1 (reset pin), boot count 1.

All rows below are console readings over BLE (`msv4_ble_console.py`), 22:07–22:10 PDT.
**Not stated:** whether the blade was clean, dry and in air. Board 1's reference values are all in
air, so treat the analog comparison as provisional until that's confirmed.

## Passed

| Check | Board 2 | Board 1 / guide |
|---|---|---|
| `info` LFCLK | XTAL running, RC at setup entry, start wait 377 ms | 250–380 ms |
| `vdd` | 3282 mV | 3250–3350 |
| `temp` | NTC 24.58 °C (10161 Ω), die 24.25 °C | room |
| `drive 16` + `pins` | 1 MHz, DRV hi-drive under GPIOTE | same |
| `chop on`, `swap a/b/open`, `read` (k = 0) | SENSE 198.63 / 198.63 / 58.89 mV; DRV 48.34 / 48.34 / 46.58 mV | SENSE moves, DRV doesn't |
| `pins` after `safe` | DRV, VBAT_DIV_EN, TEMP_READ low; both /OE and both Sel released | same |

## Analog readings, as the console printed them (Rs assumed 1 kΩ)

| Check | Board 2 | Board 1 |
|---|---|---|
| `spectrum open` | **Cp = 28.83 pF** ±2.3 %, fit rms 0.0133 | 8.8 pF |
| `iq 16`, `swap open` | 730.3 mV at −176.1°, drv 770.8 mV | 787.7–788.1 mV at 178.8°, drv 778.5–779.8 mV |
| `iq 16`, `swap a` ×3 | 615.1–615.4 mV at −163.1°, drv 769.0–770.3 mV | air 761 mV at −177.5°; damp paper towel 642 mV at −169.5° |
| `iq 16`, `swap b` | 615.8 mV at −163.0° | A and B agree to 0.4 % in air |
| `sweep 16`, `swap a` | sense A 578.5 mV, resid 29.0 mV; drv A 664.0, resid 55.3 mV | open: A 666 mV, resid 57 mV |
| `spectrum ab` (Cp 28.83) | Rsoil 27.0 kΩ ±28.9 %, Cm 8.01 pF ±21 %, Cd 44.79 pF ±2 %, rms 0.0064 | air: Rsoil 10¹⁴–10¹⁷ Ω, Cd 14–20 pF |
| `moisture` | Rsoil25 26885 Ω, T 24.31 °C, Cm 8.0 pF, Cd 44.84 pF | — |

`spectrum open` (electrodes floating) per frequency: |H| 0.9804 ∠−2.77° at 250 kHz, 0.9607 ∠−10.83° at
1 MHz, 0.8690 ∠−24.81° at 2.667 MHz.

A repeat at 22:22 (same boot, uptime 15135 s, board untouched) read Cp 28.86 pF, and from `spectrum ab`
Rsoil 26.6 kΩ, Cm 8.04 pF, Cd 44.68 pF.

### Rescaled for the 3.3 kΩ R15

The fit model is H = 1 / (1 + Rs·(Ye + jωCp)), so Rs appears only in the product Rs·Y. Dividing every
fitted admittance by 3.3 is exact for a 3.30 kΩ part. This is a calculation, not a new measurement, and
the resistor's exact value wasn't measured.

| | Console (Rs = 1 kΩ) | Rescaled (Rs = 3.3 kΩ) | Board 1 |
|---|---|---|---|
| Cp | 28.83–28.86 pF | **8.74–8.75 pF** | 8.8 pF |
| Rsoil | 26.6–27.0 kΩ ±29 % | 88–89 kΩ | air: 10¹⁴–10¹⁷ Ω |
| Cm | 8.0 pF ±21 % | 2.4 pF | — |
| Cd | 44.7–44.8 pF ±2 % | **13.5–13.6 pF** | air: 14–20 pF |

The lower `iq` amplitude (615 against 761 mV) is what the larger Rs gives. A and B agree to 0.1 %.
With Cp and Cd matching board 1, the front end measures like board 1 once R15 is accounted for.

One thing differs: the fit finds a weak conducting branch, 88 kΩ in series with 2.4 pF. At 1 MHz that
is about 9 µS, against about 85 µS through Cd. Board 1's air fit found none. Whether the blade was
clean, dry and in air wasn't stated, so this is either the blade's surroundings or a small leak on
the board.

## Battery

`vbat` read 3957–3959 mV, four times over 22:08–22:10. That's above the 3.89 V nominal VBAT_OV, and
board 1's ADC reads its own stop at 3.82–3.83 V. It hasn't been checked against a meter yet. A cell
fresh at about 4.05 V sits above VBAT_OV without being charged. So the reading alone doesn't show the
OV threshold failing; a VBAT that keeps rising with a supply or panel on SP would.

**Resolved:** the maintainer stated the cell read 4.0 V when first plugged in and the battery reading is
correct, so this is a fresh cell above VBAT_OV. The maintainer also confirmed the temperature reading.
The charger hasn't been exercised on this board, because a cell above VBAT_OV draws no charge current.

## Open

- [ ] Blade wiped clean and dry, held in air: does the 88 kΩ / 2.4 pF branch go away?
- [ ] Hand test in MSV4 Lab. G and C read 3.3× high on this board, so the "> 100 µS" pass line is easy to clear.
- [ ] Charging: SP supply once the cell is below about 3.85 V; VBAT should stop at VBAT_OV.
- [x] Production image: done 2026-10-13, see below.

## Range, 2026-10-13 evening

The maintainer took board 2 outside at about 22:37 PDT to cure the tip NTC epoxy overnight. Where
outside, and how far from the computer, wasn't stated. Measured from the maintainer's computer:

- The MSV4 Lab's BLE connection, idle since 22:25:55, held until **22:38:11**. The console then
  logged "Disconnected by the board." Nothing had been sent to it since 22:25:55.
- Passive scans from 22:38:30 to 22:40:39 heard **no advertisements** from CB:23:3B:C2:65:28, and
  none at its address + 1. The same scans heard 44 devices, including board 1 (sensor 10, Alocasia
  pot) 14 times at −82 to −62 dBm, median −65 dBm. So the scanner was working.

Not established from here: whether board 2 was still advertising (its LED blips every 2 s while it
is), and the distance.

## Production image, 2026-10-13

The maintainer asked for production so the board can stay outside overnight as a range test, with
the receiver doing the listening.

- **R15 option added:** `arduino_msv4_probe.ps1 -RsOhms <ohms>` defines `MSV4_RS_OHMS`, which sets
  `SPEC_RS` in `MSV4_Moisture_BLE_Sleep` (default 1000). The compiled image carries 3300.0 four times
  and the previous build (262166) none.
- **Build:** `-Sketch arduino/MSV4_Moisture_BLE_Sleep -SensorId 11 -RsOhms 3300 -CpCentiPf 874
  -BuildTag prod-rs3300-2026-10-13 -ApplicationVersion 262167 -Package -AcceptSharedMsv3DeviceType`.
  Hex sha256 `6ccf5c93eb629ea5956e403764f08b1aeb1e76e9153e5e83921cd49b3b603b2a`. Production
  cadence: moisture every 10th one-minute wake. Cp 8.74 pF is this board's own `spectrum open`
  reading (28.83 pF through a 1 kΩ fit) divided by 3.3.
- **Sensor ID 11:** the build guide's proposal for board 2. The maintainer didn't name a number.
- **Install:** `msv4_ble_ota.py --board 2` at 22:46–22:47 PDT, −71 dBm at the jump and −68 dBm in
  the bootloader, 43 s. The final packet receipt was late, as on board 1; the bootloader's image
  check accepted the image.
- **Proof:** `msv4_adv_scan.py --sensor 11 --require-progress` heard counters 2 (22:48:15) and
  3 (22:49:15) at −66/−68 dBm, then "Progress confirmed". The cloud has wakes 2–7 by 22:53, one a
  minute, at −65 to −70 dBm as the receiver hears them.

First production readings (counters 2 and 3, blade in air with the NTC epoxy curing): Rsoil
86.8 kΩ (84.6 kΩ at 25 °C), Cm 2.3 pF, Cd 13.57 pF, rms 0.0064, VBAT 3948–3950 mV, tip 23.75 °C.
That matches the bring-up console rescaled for 3.3 kΩ (88–89 kΩ, 2.4 pF, 13.5–13.6 pF). So the
image fits with the right R15.

Cp is NOT calibrated on this boot, as expected. The image calibrates only after a reading in air
where nothing conducts (Rsoil at the 400 kΩ ceiling), and this board's air reading carries the
87 kΩ / 2.3 pF branch. Until that branch is explained it runs on the build's 8.74 pF.

DIS read about 23:00 PDT: firmware `MSV4-BUILD:prod-rs3300-2026-10-13`, software 262167, hardware
`Cp 8.74 pF default`. Mask check A −3.5 mV, B −6.2 mV: both read the wrong way for a DC path, so
there's no leak. With a 3.3 kΩ R15, a given leak reads 3.3× more mV than on board 1. **ADC
calibration zero −12 LSB**, outside the window the build centres on board 1's −5 LSB. So 10 of the
first 11 wakes repeated the calibration (up to 3 a wake) and 10 ended out of the window. That window
is per board, and board 2's centre is not yet chosen.
