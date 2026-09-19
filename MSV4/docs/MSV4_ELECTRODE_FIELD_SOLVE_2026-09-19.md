# MSV4 electrode: 2D field solve of the as-drawn geometry (2026-09-19)

**Verdict: keep the copper. No respin is warranted by the electrode.** The pattern does what it was
designed to do (about 5x deeper than MSV3, half the interface-gap sensitivity in lossless soil). What
limits the measurement is the 1 MHz operating frequency against soil conductivity, which is a firmware
and calibration question, not a layout one.

Solver: `MSV4/scripts/msv4_electrode_fieldsolve.py` — finite-volume solve of div(eps* grad V) = 0 on the
blade cross-section with complex permittivity (eps0*er - j*sigma/w), so conductivity and the solder
mask's series capacitance (Maxwell-Wagner) are included. Admittance per length via Green's identity,
times the 81.1 mm plate length.

## Geometry as drawn (IPC-2581 export 2026-09-18 23:03)

| | planned (electrode doc) | drawn |
|---|---|---|
| plates A / gap / B | 4.9 / 4.2 / 4.9 mm | **4.725 / 4.725 / 4.725 mm** (lambda 18.9 mm) |
| plate length | 110 mm | **81.1 mm** |
| guard strip | 1.0 mm | **0.62-0.65 mm** (0.21 mm top-left beside the TEMP trace) |
| plate-to-guard clearance | 1.0 mm | 1.04 mm |
| mask over plates | — | full coverage, both faces (no openings below the shoulder) |
| board thickness | — | stackup shows Altium's default 0.32 mm core; order 1.6 mm (see below) |

## Results (1.6 mm FR4, 20 um mask)

**Lossless capacitance, A hot, B + guard grounded:** 6 pF air, 12 pF er 4, 23 pF er 10, 40 pF er 20,
56 pF er 30. Board thickness barely matters electrically (0.8 mm: 4.6 -> 55 pF); choose 1.6 mm for the
stiffness of an 81 mm blade pushed into soil.

**With real potting-mix conductivity at 1 MHz** the apparent capacitance is 24 pF dry -> 667 pF
saturated, |Z| 2.3 kohm -> 230 ohm. The ceiling is the mask's series capacitance (~640 pF). R15 = 1 kohm
is well matched to that range; the divider node runs about 0.70 dry -> 0.21 saturated.

**Penetration** (share of full er-20 response from soil within d of the surface):

| d | 0.25 mm | 0.5 | 1 | 2 | 3 | 5 |
|---|---:|---:|---:|---:|---:|---:|
| MSV4 as drawn | 19% | 32% | 50% | 70% | 82% | 90% |
| MSV3-style IDE (0.5/1.0) | 55% | 81% | 95% | 99% | — | — |

Half the MSV4 signal comes from the first 1.0 mm versus ~0.2 mm for MSV3: about 5x deeper. The
lambda/2pi = 3.0 mm figure overstates it because the plate-to-guard field is shallow. Pulling the plate
edges in to 1.6-2.1 mm from the guard only moves the 1 mm share from 50% to 45-46%, costs 10-16% of the
capacitance, and makes the conductive-regime gap sensitivity slightly worse. **Not worth it.**

**Interface (air-gap) sensitivity, 10 um gap between mask and soil:**

| | lossless er 15 | 1 MHz, moist (er 15, 0.02 S/m) |
|---|---:|---:|
| MSV4 as drawn | -8.5% | **-42.5%** |
| MSV3-style IDE | -16.8% | -58.5% |

**Frequency is the lever** (moist soil):

| f | sigma/er dominance | 10 um gap, mask only | 10 um gap, +75 um coat |
|---|---:|---:|---:|
| 1 MHz | 24x | -42% | -21% |
| 2 MHz | 12x | -32% | -18% |
| **4 MHz** | **6x** | **-25%** | **-12.5%** |
| 10 MHz | 2.4x | -14% | -8% |
| 30 MHz | 0.8x | -9% | -6% |
| 70 MHz | 0.34x | -9% | -5% |

In moist soil at 1 MHz, conductive soil is close to a short circuit, so the voltage drops across the
mask plus any gap: the reading is set by soil conductivity and by the thin interface layer, and
|Y| barely moves with permittivity (+5 % er -> -0.05 % |Y|). Permittivity survives only as a phase
component orthogonal to conductivity, 10-50x smaller. **About 1 degree of phase error leaks about 1 % of
er per degree C from conductivity's +2 %/C tempco**, so the DRV_OUT phase-monitor channel is doing
essential work, not optional work.

This agrees with the literature: stable, EC-independent permittivity needs >= 50-70 MHz (commercial
TEROS 12 / CS616 at 70 MHz; Kizito et al., J. Hydrology 352, 2008), and fringing-field penetration
scales with electrode spacing, independent of frequency (Mamishev et al., Proc. IEEE 92, 2004).

## What to do with this board

1. **Fab it as drawn**, 1.6 mm FR4.
2. **Characterise 1, 2 and 4 MHz** on the first board (firmware now allows N = 4). Expect 4 MHz to be
   the better operating point if the FSUSB42 phase stays stable; the DRV_OUT channel is the check.
3. **Report two quantities, not one:** conductance G (temperature-corrected at ~2 %/C with the tip NTC,
   the standard EC correction) and the inverted permittivity term from the quadrature phase. G is a
   strong moisture signal in its own right; the NTC at the electrodes is what makes its tempco
   correctable, which MSV3 never had.
4. **Coating test:** coat one board with a controlled ~75 um conformal layer and leave one mask-only.
   The coat halves gap sensitivity at every frequency but costs signal and risks water uptake.
5. Optional, only if respinning for other reasons: stop the plates ~10 mm below the shoulder so the
   evaporation / cracking band at the soil surface is not in the sensed region (-12 % capacitance).

## Multi-frequency `spectrum` fit — validated against the field solve (2026-09-19)

Firmware command `spectrum [a|b|ab]` (after `spectrum open` measures the SENSE-node parasitic Cp):
nine frequencies 250 kHz–4 MHz, both electrode orientations averaged, chopper-phase bin-1 of SENSE
divided by DRV_OUT, square-wave harmonic aliasing modelled exactly, Levenberg–Marquardt fit of
`Ye = jw*Cd + 1/(1/(jw*Cm) + 1/(Gs + jw*Cs))` — 18 real equations, 4 unknowns.

Validation: synthetic measurements generated from the 2D field solve (distributed physics, not the
lumped model), passed through the same aliasing, then fitted.

| moist soil (er 15, 0.02 S/m) | fitted Rsoil | fitted Cm | fitted Cs |
|---|---:|---:|---:|
| baseline | 355 ohm | 671 pF | 14.9 pF |
| er +10 % | +0.4 % | −0.2 % | **+3 %** |
| sigma +10 % | **−6 %** | −0.4 % | **−34 %** |
| 5 um air gap | +18 % (raw 1 MHz reading +29 %) | **−50 %** | — |
| 10 um air gap | +33 % (raw +56 %) | **−67 %** | — |

- **Rsoil (conductance) is recovered robustly** and separates from permittivity. Use it as the
  moisture proxy, corrected to 25 C with the tip NTC, calibrated per pot.
- **Cm is a contact-quality channel.** It absorbs roughly half of an interface gap and reports it
  separately. For the thermal investigation this is the discriminator: a daily swing in Cm means the
  interface is moving; a daily swing in Rsoil with steady Cm means conductivity/temperature.
- **Cs (soil permittivity) is not identifiable below 4 MHz in moist soil.** Neither a Cd fixed from an
  in-air calibration nor a constant-phase-element interface model recovers it: lumped-model error
  (~0.2–1 % of drive) is ~30x the permittivity signal. It carries some information when dry. Treat it
  as diagnostic only; firmware labels it so.
- Reported +/- figures are statistical; model error dominates in moist soil.
