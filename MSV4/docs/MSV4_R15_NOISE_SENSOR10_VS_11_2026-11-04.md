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
