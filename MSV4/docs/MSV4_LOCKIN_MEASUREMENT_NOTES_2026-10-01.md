# MSV4 lock-in measurement — computed notes, 2026-10-01

Numbers worked out while walking through how `iq`, `spectrum` and `moisture` measure the
electrode. Everything here is **computed** (simulation or algebra) unless marked measured; none of it
has been checked on hardware beyond the board 1 comparisons quoted. This is a dated record.

- Animated walkthrough: [MSV4_LOCKIN_WALKTHROUGH_2026-10-01.html](MSV4_LOCKIN_WALKTHROUGH_2026-10-01.html)
  (self-contained; open in a browser).
- Simulation it runs on: `MSV4/scripts/msv4_lockin_sim.js` — RK4 model of the SENSE node plus the
  firmware's `fitPhases`, `specMeasure`, `specModel` and `specFit` ported line for line.
  Self-test: `node MSV4/scripts/test_msv4_lockin_sim.js`.

## The simulation against board 1 (`iq 16`, electrode A)

| Medium | Simulated A, φ | Board 1 measured A, φ |
|---|---|---|
| Open | 796 mV, 182.1° | 784 mV, 178.3° |
| Air | 745 mV, 186.6° | 760 mV, 182.3° |
| Damp towel | 647 mV, 193.1° | 642 mV, 190.5° |
| Hand grip | 403 mV, 223.1° | 372 mV, 221.5° |

Board 1's phases sit 2–4° below the simulation in every medium. **Inferred, not measured:** a
timing offset in the switch/edges. If DRV_OUT shares it, the `spectrum` division cancels it.
The simulated towel fit reproduces board 1's interface corner (~1.0 MHz).

## What the chopper readings are

- READ_CNTL **HIGH parks** both U4 filters on GND; the filter listens during the LOW half,
  [k+N/2, k+N). That is why DRV_OUT peaks at k = N/2 and raw `iq` phases sit near 180°.
- Each reading is ½ × the average of the signal over that half-period window. Stepping k slides the
  window, so readings vs phase are a **half-period moving average of the SENSE waveform**.
- That average scales the n-th harmonic by sin(nπ/2)/(nπ/2); with the ½, the fundamental is
  scaled by **1/π**. DRV's 2.101 V fundamental (2·3.3/π) ÷ π = **669 mV**, the "pure fundamental"
  amplitude. The factor is common to both channels and cancels in H = X1(SENSE)/X1(DRV_OUT).
- X1 = Σ V·e^(−jθ) = (M/2)·(I − jQ); X1·2/M = A∠−φ. The offset P cancels because evenly spaced
  unit vectors sum to zero. Harmonic n survives the sum when n ≡ 1 (mod M) (folding).
- A 0/1 window needs ≥ 3 phases (P, I, Q unknown); 4 gives I = (V₀−V₁₈₀)/2, Q = (V₉₀−V₂₇₀)/2.

## Folding: DRV_OUT amplitude by phase count (exact sums)

| M | 4 | 6 | 8 | 12 | 16 | ∞ |
|---|---|---|---|---|---|---|
| DRV_OUT A | 825 mV | 733 mV | 704 mV | 684 mV | 677 mV | 669 mV |

M = 4 folds every odd harmonic (Σ 1/n² over odd n = π²/8, so A = 825 mV exactly).

## `specModel` harmonic truncation

`specModel` sums harmonics with k ∈ [−8, 8] and |h| ≤ 63. Against a simulation with **ideal square
edges**, that truncation differs from the full harmonic series by 0.2–0.5 % of drive (worst at
N = 6, M = 6, where |h| stops at 49); the full series matches the simulation to ~1e-4. Fitting the
ideal-edge simulation directly turns air into a **false ~390 kΩ**. Board 1 reads open in air
(10¹⁴–10¹⁷ Ω), so real edge rates evidently keep this from mattering. Not checked on hardware.
The walkthrough's sweep points therefore use `specModel` plus noise propagated through the phase sums.

## Settle time (filter τ = 100 kΩ × 1 nF = 100 µs)

Leftover of a full 1650 mV DRV_OUT step after waiting t: e^(−t/τ) × 1650 mV.

| Wait | 0.1 ms | 0.5 ms | 1 ms | 5 ms (firmware) |
|---|---|---|---|---|
| Leftover | 607 mV | 11 mV | **0.075 mV** | ~0 |

One ADC code is 0.88 mV. `spectrum ab` measurement time (204 phases, ~1.54 ms ADC per phase):

| Settle | Current M | M = 4 where possible (68 phases) |
|---|---|---|
| 5 ms | 1.33 s | 0.44 s |
| 1 ms | 0.52 s | 0.17 s |

Board 1's whole `spectrum ab` took ~5 s; the split between fit and BLE output is unmeasured.
**Hardware check, no reflash needed:** `settle 5` / `spectrum ab` / `settle 1` / `spectrum ab` /
`settle 5` / `spectrum ab` in the towel; agreement means 1 ms is safe on real hardware.

## Chopper duty cycle

Fundamental kept by a window of duty D: sin(πD)/π, maximal at 50 %.

| D | 50 % | 25 % | 12.5 % | 6.25 % |
|---|---|---|---|---|
| Fundamental vs 50 % | 100 % | 71 % | 38 % | 20 % |
| 3rd harmonic / fundamental (window) | ⅓ | ⅓ | 0.80 | 0.95 |

A narrower window samples instead of averaging (closer to the true RC waveform) at the cost of
signal, more folding, loss of even-harmonic immunity and of the complementary k / k+N/2 pairs.
Only an inverting (±1) chopper beats 1/π (2/π); V₀ − V₁₈₀ already recovers that. If noise on SENSE
itself dominated, D ≈ 37 % would be ~7 % better (modelled); for ADC noise 50 % wins.

## Dropping Cs does not change the model at any frequency

The four-element lumped model Cd ∥ (Cm — (Gs ∥ Cs)) has only three observable combinations, so any
four-element set has an exact three-element equivalent. Field-solve values {Cm 671 pF, Rsoil 355 Ω,
Cs 14.9 pF, Cd 5 pF} ≡ {Cm 656.42 pF, Rsoil 370.9 Ω, Cd 19.58 pF}: identical admittance to 1e-16 at
1, 2.67, **30 and 100 MHz**. Consequences:

- Extrapolating the fitted three-element model to higher frequency is self-consistent in lumped
  terms; soil permittivity is folded into Cd, Cm and Rsoil.
- Measuring at ~30 MHz would **not** reveal Cs with this model unless Cd is pinned independently
  (e.g. an air calibration). The 09-23 doc's "Cs becomes visible ~30 MHz" assumes that.
- What limits extrapolation is physics outside the lumped model: distributed fields (0.2–1 % of
  drive in band, unknown above), soil dispersion near relaxation (≈11 / 45 / 108 MHz dry / moist /
  watered potting mix, 09-13 model), and board parasitics (Cp measured flat only to 2.67 MHz).
  Validated band: 250 kHz–2.67 MHz (field solve checked to 4 MHz). MSV4 cannot drive above 4 MHz.

## Interface contact (field solve, modelled)

Cm rises with good contact (wet soil is the mask capacitor's second plate). 5 µm air gap: raw 1 MHz
reading +29 %, fitted Cm −49 %, fitted Rsoil +15.5 %; 10 µm: Cm −67 %. Depth (MSV4 ~5× MSV3) halves
gap sensitivity in non-conducting soil (−8.5 % vs −16.8 %) but helps less in moist soil at 1 MHz
(−42.5 % vs −58.5 %), because every current path crosses the mask and gap in series.

## Naming

Capacitively coupled contactless conductivity measurement (the analytical-chemistry term is C⁴D:
plate–wall C–solution R–wall C–plate with a stray C across it), read by synchronous (I/Q)
demodulation at eight frequencies and fitted as impedance spectroscopy. Output: Rsoil (moisture,
salt-sensitive, per-soil calibration), Cm (contact), Cd (stray).
