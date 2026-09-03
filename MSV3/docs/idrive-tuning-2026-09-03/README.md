# IDRIVE tuning + tank characterization raw data — 2026-09-03

Raw bench data from the session that closed Test 4 (tank/f_INT) and bench-tuned
`DRIVE_CURRENT_CH0` for the first time. **Conclusions, derivations and the decision live in
`../MSV3_BENCH_TEST_PLAN_2026-08-21.md` (Test 4).** This folder is the underlying measurements so
the analysis can be re-run or re-checked.

## Bench setup

| | |
|---|---|
| Board | MSV3 probe on COM12, USB powered |
| Firmware | `MSV3_Probe_Bringup_Test.ino`, freshly flashed this session |
| Scope | HP 54600B, 100 MHz, 20 MSa/s |
| Probes | 2 × 10× (P6150), on TANK_HOT and TANK_REF, both ground clips on board GND |
| Coupling | DC, both channels ground-referenced |
| Electrodes | Bare, in still air unless noted; "2 hands" = both electrodes gripped |

**The scope's per-channel probe factor was initially set to 1× while 10× probes were fitted.**
Every amplitude taken before that was corrected reads 10× low, and the 1× probes' ~40 pF each
loaded the tank ~6× harder than the 10× probes' ~6.5 pF. Check the probe factor before trusting
any number from this rig.

## Files

| File | Contents |
|---|---|
| `fdcstream_raw.csv` | The two 200-sample `fdcstream` streams, still air, codes 16 and 25 |
| `analyze_fdcstream.py` | Reproduces the noise analysis from that CSV |

```
python analyze_fdcstream.py
```

## Frequency / capacitance (scope-measured, no f_INT assumption)

| Electrode state | f_scope | C_total |
|---|---|---|
| Dry, in air | 3.30 MHz | 342 pF |
| Both hands on | 2.67 MHz | 523 pF |

Paired with `DATA_CH0` = 20,012,154 at the same moment (dry, probes attached), this gives
**f_INT = 44.27 MHz** for this specific chip — the number Test 4 existed to find.

## IDRIVE sweep — single-ended peak amplitudes

| Code | IDRIVE | Dry hot | Dry cold | 2-hands hot | 2-hands cold |
|---|---|---|---|---|---|
| 16 run 1 | 0.169 mA | 0.512 V | 0.425 V | 0.300 V | 0.287 V |
| 16 run 2 | 0.169 mA | 0.525 V | 0.437 V | 0.300 V | 0.256 V |
| 25 | 0.644 mA | 1.9 V | 1.6 V | 1.3 V | 1.0 V |
| 26 | 0.747 mA | 2.3 V | 1.894 V | 1.4 V | 1.3 V |
| 31 | 1.571 mA | 2.875 V | 6 V | — | — |

Drive response is **linear** across codes 16→26 (`V ∝ I^1.01`).

**The code-31 row is unreliable** — taken before the probe-attenuation error was found, and its
2.09× hot/cold asymmetry is not physical. It is kept only because it is what first showed max
drive was unusable. Do not fit to it; an earlier sublinear `I^0.77` fit was an artifact of trusting
it.

Amplitudes are **single-ended per node**. The datasheet's 1.2–1.8 Vpk is the *sensor* amplitude
across the tank (A−B differential), which was never measured. Treat these as proxies.

## `DATA_CH0` vs IDRIVE — A-B-A

| Comparison | code 16 | code 25 | Δ |
|---|---|---|---|
| Separated in time | 20,012,154 | 19,955,880 | 56,274 (0.281%) |
| Back-to-back | 20,007,567 | 19,967,378 | 40,189 (0.201%) |
| 200-sample streams | 20,012,868 | 19,972,918 | 39,950 (0.200%) |

Reproducible, and 4–10× larger than same-code drift, so it is a genuine drive-current effect.
**Raising IDRIVE lowers `DATA_CH0`.**

## Known contamination in this data

Both 200-sample streams are **bimodal** — `DATA_CH0` switches between two levels ~5,500 counts
apart (~0.028%), in bursts, ~12% of the time, at both drive currents. It is 5–6× larger than the
real measurement noise and **breaks naive statistics**: raw SD shows no IDRIVE difference and the
successive-difference estimator shows it backwards. `analyze_fdcstream.py` splits levels first for
this reason.

Cause unknown and not chased. Because the gap is roughly constant in absolute counts while drive
current changes 3.8×, a supply-rail step is more likely than an oscillator effect — first suspect
is BQ25170 charge cycling or USB supply negotiation, given the battery-voltage coupling already
documented in `MOISTURE_SENSOR_FDC2212_FRONTEND.md`. Untried test: run `fdcstream 200` alongside
`chgstatus`/`vbat` and check whether STAT/PG transitions align with the jumps.
