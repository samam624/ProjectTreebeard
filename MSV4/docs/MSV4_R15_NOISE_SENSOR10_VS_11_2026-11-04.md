# Why the Begonia line looks cleaner than the Alocasia line (2026-11-04)

**Question (maintainer, 2026-11-04):** sensor 11's moisture reading looks much cleaner than
sensor 10's. Is that board 2's 3.3 kohm R15 doing its job?

**Answer: no, it comes from the pot size.** Per reading, both boards are equally noisy. The
Begonia pot is small, so each gram of water moves its reading about twice as far as it moves the
Alocasia's. The same sensor noise therefore shows up as about half as many grams.

Script: `MSV4/scripts/msv4_r15_noise_compare.py` (reads the two caches). Window: 10-28 00:00 PDT to
11-04 12:00 PST, with the 24 h after each watering left out (Alocasia watered 10-30 15:37). Both
pots were on the same shelf over this window (maintainer, 11-01).

## Measured

| | sensor 10 (board 1, R15 1k, Alocasia) | sensor 11 (board 2, R15 3.3k, Begonia) |
|---|---|---|
| readings in window | 9371 (1-min test cadence) | 990 (every 10th minute) |
| median Rsoil / Cm / Cd | 10,991 ohm / 53.1 pF / 22.13 pF | 8,828 ohm / 90.4 pF / 23.66 pF |
| tip daily range | 0.5 to 7.0 C | 1.2 to 6.5 C |
| **per-reading noise, Rsoil** | **0.68 %** (0.65 % on every 10th wake) | **0.62 %** |
| per-reading noise, Cm | 0.52 pF | 0.42 pF |
| per-reading noise, Cd | 0.063 pF | 0.105 pF |
| per-reading noise, stored number | 30 units | 27 units |
| **sensitivity to water, stored number** | **16.9 units/g** (16 weighings) | **28.1 units/g** (9 weighings) |
| sensitivity to water, Rsoil | -0.30 %/g | -0.65 %/g |
| per-reading noise in grams (stored) | 1.80 g | 0.97 g |
| white noise left in an hourly median, as plotted | ~0.29 g (60 readings/h) | ~0.49 g (6 readings/h) |
| within-day swing of hourly medians about a 24 h trend (stored) | 3.41 g rms (58 units) | 1.61 g rms (45 units) |

Per-reading noise is the scatter between successive readings (robust sigma divided by sqrt 2),
so slow drying and daily swings cancel out of it. Board 1 gives the same result on every tenth wake,
so its 1-minute cadence does not change the per-reading figure.

## What R15 should have done, and did not

Model of the front-end divider: how far it moves for a 1 % change in Rsoil, rms over the 8 sweep
frequencies, at each pot's own median soil values:

| | R15 1k | R15 3.3k |
|---|---|---|
| sensor 10's soil | 6.12e-4 of drive | 11.41e-4 |
| sensor 11's soil | 7.80e-4 | 13.86e-4 |

With 3.3k the divider moves about 1.8x further per percent of Rsoil. If the noise were a fixed
floor in the readout, board 2's Rsoil noise would be about 1.8x lower. It is 1.09x lower.
Expressed as a fraction of the drive, board 2's noise is about twice board 1's: 0.62 x 13.86e-4 =
8.6e-4, against 0.68 x 6.12e-4 = 4.1e-4.

**Inferred, not measured:** at these soil values the per-reading noise is not set by the
readout's resolution. It grows along with the signal, which points to gain or phase jitter in the
front end, or to real fluctuation at the soil contact, rather than to ADC noise. This is one
board of each type. The boards also differ in more than R15: Cm is 53 pF against 90 pF, and board
2 went through the salt and hot-water cup tests. So this is not a controlled R15 test. A 3.3k could
still help in drier soil (higher Rsoil), where a 1k divider barely moves. That range was not
covered here.

## Why the plots still look different

- **Pot size.** Each gram of water moves the Begonia's stored number 1.7x as far as the
  Alocasia's, and its Rsoil 2.2x as far. Every noise source shrinks by that factor once it is
  converted to grams.
- **The within-day swing,** in units, is 1.3x larger on the Alocasia (58 against 45 units). This
  swing includes real daytime-concentrated drying as well as any temperature leftover, so it is
  not all error.
- **White noise is not what is visible.** The Alocasia's 60 readings per hour average its white
  noise down below the Begonia's (0.29 g against 0.49 g).
- **The Alocasia figure draws seven candidate lines** and three post-watering overshoots. The
  Begonia figure draws one line, and its only watering sits before the first weighing.

## Update 2026-11-15: same processing for both, over every cycle

**Question (maintainer, 2026-11-15):** is the Begonia's number processed differently from the
Alocasia's? The Begonia looks clean and the Alocasia much worse.

**Processing is identical.** Both numbers are made by the receiver's `calibrateMsv4Moisture()`
(log10 of Rsoil, 2 %/C on the tip thermistor), the same code for both probes. The Begonia figure
plots that number as hourly medians through one straight line. The Alocasia figure
(`msv4_moisture_plot.py`) plots seven re-fitted candidates instead, so it was the more processed of
the two. The firmware does differ: board 1 runs 262166 (1-minute test cadence), board 2 runs 262172
(10-minute cadence, raw sweeps, R15 3.3k). Neither changes how the number is calculated.

Figure: `data/sensor_logs/latest/stored_vs_mass_s10_s11.png` from
`MSV4/scripts/msv4_stored_vs_mass_both.py`. Both panels use the same processing and the same gram
span. Each pot is fitted on its own weighings, and weighings within 24 h after a watering are left
out.

| | sensor 10, Alocasia | sensor 11, Begonia |
|---|---|---|
| weighings fitted | 46 (10-08..11-15) | 14 (10-27..11-15) |
| sensitivity | 12.0 units/g | 31.2 units/g |
| weighings off the line | 59 g rms, worst 246 g | 13.5 g rms, worst 22 g |
| per-reading noise | 24 units = 2.0 g | 30 units = 1.0 g |
| within-day wobble of hourly medians | 3.6 g rms (~43 units) | 0.44 g rms (~14 units) |

**Measured:**
- Per reading, the Alocasia is not noisier in units (24 against 30).
- Its wobble within a day is about 3x larger in units.
- Its 59 g miss comes mostly from **cycle-to-cycle offsets and overshoot**, not noise. After the
  11-10 watering it read ~+320 g at the peak and still ~+100 g days later, at the same scale
  weight where earlier cycles sat on the line. The Begonia's three cycles line up within 22 g, and
  its overshoot after watering is ~+50 g.
- The single 12.0 units/g line is a compromise across those offsets. The 11-04 figure of 16.9
  units/g came from one window.

**Inferred, not measured:** the size and decay of the overshoot fit water that stays near the blade
in a large pot (the 192-337 g waterings wet a much smaller fraction of the Alocasia's soil than of
the Begonia's) and spreads out over days. The per-cycle offset is the same kind seen on 10-21, when cycle 2 read
wetter than cycle 1 at the same weight. Neither has been tested.

### Was the Alocasia's first calibration worse? (2026-11-15, measured)

No. Each pot was fitted on its first full drying cycle (weighings more than 24 h after a watering),
and that line was carried forward unchanged to the later cycles:

| first-cycle line | Alocasia, 12.8 units/g (19 weighings) | Begonia, 29.6 units/g (10 weighings) |
|---|---|---|
| cycle 1 itself | -18..+9 g | -23..+12 g |
| next cycle | +14 g mean (-11..+44) | +15 g mean (-3..+27) |
| cycle after | +30 g mean (+18..+46) | +37 g (one weighing, 29 h after watering) |
| fourth (from 11-10) | +101 g mean (+65..+138) | n/a |

Each Alocasia cycle fitted on its own sits within 4-11 g rms. So the first calibration was as good
as the Begonia's, and so is the fit inside each cycle. What goes wrong is that each watering shifts
the line toward "wetter". The shift was the same size as the Begonia's for two cycles and then
jumped in the 11-10 cycle. Inside that cycle the number also fell at 24 units/g, against 12.8 in
cycle 1. The single 12.0 units/g line in the table above is a compromise across these shifts.

### Is the Alocasia's daily wobble thermal? (2026-11-15, measured)

Hourly medians and tip temperatures were each taken about a 24 h running median. The 24 h after
every watering was left out.

| | Alocasia | Begonia |
|---|---|---|
| hours | 738 | 370 |
| daily tip swing | 1.69 C rms | 1.66 C rms |
| wobble of the number | 35 units rms | 18 units rms |
| left over against tip temperature after the receiver's 2 %/C | -8.9 units/C, r -0.43 | +0.4 units/C, r +0.04 |
| wobble with that leftover removed | 32 units (2.7 g) | 18 units (0.6 g) |

Both pots see the same temperature swing, and on the Begonia the 2 %/C correction leaves nothing.
On the Alocasia, a perfect temperature correction would remove only about 10 % of the wobble. Most of it
is not temperature. The 11-15 table above lists 43 units for this wobble; that used the whole record
and included hours without a tip reading. Either way it is under 4 g, against the +101 g shift of the
11-10 cycle.

### Is the moisture number calculated the same way on both probes? (2026-11-16, verified)

**Which source is running.** Each installed image was rebuilt from its source commit with its
recorded build flags. Both came out byte-identical to the hex that was flashed:

| | sensor 10, board 1 | sensor 11, board 2 |
|---|---|---|
| installed (DIS-read) | 262166 `fast1min-ledafter-2026-10-11` | 262172 `prod-rawsweep-rs3300-2026-10-15` |
| source commit | 19a0b4d | 8521275 |
| flags | `-SensorId 10 -MoistureEveryNWakes 1 -CpCentiPf 869` | `-SensorId 11 -RsOhms 3300 -CpCentiPf 874` |
| rebuilt hex sha256 = deployed | c0fda79fd7c7e79e... | c93c92749136ff3e... |

**What differs between the two sources** (`git diff 19a0b4d 8521275`, sketch only):
- `SPEC_RS`: 1000 ohm on board 1 and 3300 ohm on board 2. Each matches that board's real R15.
- `Cp`: 8.69 pF on board 1 and 8.74 pF on board 2, each calibrated on its own board.
- The SAADC zero-acceptance window: -10..0 LSB on 262166 and -15..-1 LSB on 262172. A
  recalibration is oversampled on 262172.
- Measurement cadence: every minute on board 1 (test), every 10th minute on board 2.
- 262172 also sends the raw sweep in the scan response. That code only reads the sweep and does not
  change the fit.

The sweep, the electrode model, the fit and the way Rsoil is produced are the same code on both.
Every difference is a constant per board. A constant error in Rs or Cp scales Rsoil by a fixed
factor, so it shifts the log number by a fixed amount and cannot grow over time.

**Receiver.** `calibrateMsv4Moisture()` takes no sensor ID: 10000 x log10(400 kohm / Rsoil
corrected to 25 C at 2 %/C on the tip NTC). Recomputing it from each stored row's Rsoil and tip
temperature reproduces the stored number in **every row**: sensor 10, 57,405 rows (57,375 exact,
30 off by 1); sensor 11, 46,613 rows (46,590 exact, 23 off by 1). The 1-unit misses are float
rounding in the Python recomputation.

**Grams.** Neither probe nor receiver converts to grams. That conversion exists only in the plotting
scripts, and they apply it the same way to both pots.

So the Alocasia's upward trend is in the Rsoil its probe measures, or in the weight it is compared
against. It is not in the processing.

### Firmware sources of non-symmetric drive (2026-11-16, read from the verified sources)

The maintainer reports that board 1 (sensor 10) still shows large soil polarization even with the
A/B swap. Everything below is read from 19a0b4d and 8521275, the sources proven above, and from
`MSV4/hardware/reference/PIN_NET_REFERENCE.md`.

**How the drive works.** DRV is a unipolar 0/3.3 V, 50 % square wave through R15, with no blocking
capacitor. U3 makes one plate hot (on SENSE) and grounds the other, then swaps. The guard ring is
GND on both layers and is never switched. `runSpectrumFit()` sweeps A-hot, then B-hot: 8 frequencies,
102 phase steps per orientation. Each step is `FE_SETTLE_MS` 2 plus two 64x ADC reads, so an
orientation takes roughly 0.3 s. That figure is estimated from the code, not timed.

Asymmetries found:
1. **Plates against the guard are not cancelled by the swap.** While hot, a plate sits at a mean
   +1.65 V; while cold it is at 0 V. The swap cancels A against B. The guard, though, is at 0 V the
   whole time, so averaged over a measurement both plates sit about +0.83 V above it. Both boards
   run the same code here.
2. **Board 1 measures 10x as often.** 262166 is built with `-MoistureEveryNWakes 1`, so it sweeps
   every minute: 1440 measurements a day, against 144 on board 2 (production default, 10). Its daily
   drive time, and so its DC dose against the guard, is 10x board 2's. This is the only firmware
   difference between the two boards that bears on polarization.
3. **The order is fixed: A, then B, every time.** And if the B sweep fails after A succeeded
   (`if (!specSweep(s)) return false;`), A was driven with no B to balance it.
4. **Each orientation is a DC block of about 0.3 s.** The hot plate holds its +1.65 V mean for the
   whole orientation. A and B balance only across the full measurement, never within one cycle.
5. **The mask check** holds DRV at DC 3.3 V for 5 ms plus a read on A, then on B, once a day. That
   is small next to 1 to 3.

**Measured 2026-11-16 (DIS over BLE, board 1):** build 262166 `fast1min-ledafter-2026-10-11`. Mask
check 36 of this boot: A -0.9 mV, B -1.8 mV, both within the switch's own 2 mV. No DC path to
ground shows on either plate. One check cannot see a path above about 500 kohm.

**Inferred:** with the mask whole, item 1 cannot drive Faradaic current through a measurable path
to copper, so any polarization from it would build up as charge at the mask and soil interface.
Item 2 is the clean test: production cadence on board 1 gives it the same dose as board 2. Items 1
and 4 can only be removed in hardware: a series blocking capacitor, or a mid-rail cold plate and
guard. Item 3 can be fixed in firmware by alternating the starting orientation and by not counting
an unbalanced sweep. None of this has been acted on.
