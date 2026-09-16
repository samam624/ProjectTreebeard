# MSV4 Probe BOM — Working Draft

**Status:** architecture/BOM draft, not ready to order
**Date:** 2026-09-02

> **Superseded on sensing (2026-09-16).** The TLC555 capacitance-to-frequency block below is no
> longer the V4 sensing choice. V4 is a software lock-in: fixed-frequency excitation plus a phase-
> swept chopper built from two `TS3USB221A` DPDT switches, driven by TIMER/PPI/GPIOTE. See
> `MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` and
> `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md`. The MCU (E73/nRF52840), power chain,
> BQ25504 network and cost model below still stand; the `TLVH431B` clamp footprint is now a
> live requirement, not a DNP option — see `MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`.

## Baseline assumptions

- One capacitive moisture channel.
- Current-probe capacitance is provisionally estimated at roughly 50 pF dry to 500 pF wet;
  bench-measure this range before freezing the timing resistors. The older MSV1 probe's
  270 pF-to-1.28 nF range is not transferable because its sensing area was larger.
- TLC555 astable converts probe capacitance to frequency; its `OUT` pin goes directly to an
  nRF52840 digital GPIO using GPIOTE/PPI/TIMER capture. There is no frequency-to-voltage AFE and
  no ADC in the moisture signal path.
- Solar-only charging through BQ25504; USB charging and USB-C hardware are omitted. Firmware is
  loaded through SWD pads only. **Confirmed by user 2026-09-02.**
- Existing E73-2G4M08S1C nRF52840 BLE module is retained for the first build.
- Protected 1-cell LiPo, nominal 3.7 V and 4.2 V maximum.
- Existing panel is provisionally characterized as 6 V raw open-circuit and 30 mA short-circuit
  in direct sun. Exact panel MPN and cold-weather maximum VOC remain unknown.
- Main load runs from a TPS7A0233 3.3 V LDO.
- BQ25504 `VBAT_OK` controls an external high-side PFET through an NMOS inverter so the entire
  load remains disconnected during cold start and low-battery recovery.
- The TLC555 is separately power-gated. A high-side PFET is used instead of switching its ground
  with an NMOS, avoiding ground shift and back-powering through the timer output.

## Intended power and signal path

```text
Solar panel -> BQ25504 -> VBAT: protected 1S LiPo
                         VSTOR -> load-isolation PFET -> TPS7A0233 -> 3V3_LOAD
                         VBAT_OK -> NMOS inverter --------^ PFET gate

3V3_LOAD -> TLC power PFET -> TLC555 astable -> nRF digital GPIO
                              ^ probe is timing capacitance
```

## Active BOM — major parts

| Ref. | Qty | Proposed part | Function | Basis / status | Approx. unit cost |
|---|---:|---|---|---|---:|
| U1 | 1 | E73-2G4M08S1C | nRF52840 MCU + BLE module | Reuse verified MSV3 part | $7.5888 |
| U2 | 1 | BQ25504RGTT | Solar energy harvester / 1S-cell charger | Reuse verified MSV3 part | $5.8501 |
| U3 | 1 | TPS7A0233PDBVR | 3.3 V, low-IQ LDO | Reuse verified MSV3 part | $0.7263 |
| U4 | 1 | TLC555IDR | Capacitance-to-frequency astable | SOIC-8; first V4 sensing choice | ~$0.32 |
| L1 | 1 | LPS4018-223MRC | 22 uH BQ25504 boost inductor | Reuse verified MSV3 part/footprint | $0.5656 |
| Q1 | 1 | NTR1P02T1G | VSTOR-to-load high-side isolation PFET | Reuse stocked MSV3 part | $0.1711 |
| Q2 | 1 | 2N7002-7-F | Inverts `VBAT_OK` to drive Q1 gate | New; logic-level NMOS, SOT-23 | ~$0.04 |
| Q3 | 1 | NTR1P02T1G | GPIO-controlled high-side TLC555 power gate | Preferred over NMOS low-side gating | $0.1711 |
| D1 | 1 | Red or green 0805 LED, exact MPN TBD | Diagnostic/warning indication | Prefer lower VF than current blue LED | ~$0.02 |
| BT1 | 1 | Protected 1S LiPo, 70–150 mAh TBD | Energy storage | Exact cell/connector/polarity required | TBD |
| PV1 | 1 | Existing miniature solar panel | Energy source | 6 V raw VOC / 30 mA ISC measured | TBD |
| J1 | 1 | S2B-PH-K-S(LF)(SN), or smaller keyed connector | Battery connection | Existing stock part if size allows | $0.0393 |
| J2 | 1 | S2B-PH-K-S(LF)(SN), or solder pads | Solar-panel connection | Decide removable versus soldered panel | $0.0393 |
| TP1–TP6 | 6 | Bare test pads | SWDIO, SWDCLK, RESET, VSTOR, 3V3, TLC_OUT | No placed component required | — |

## BQ25504 network and decoupling

These values initially copy the electrically working MSV3 circuit. The OV and load-good thresholds
must be rechecked before ordering rather than copied blindly.

| Ref. group | Qty | Value / part | Purpose | Status |
|---|---:|---|---|---|
| C1 | 1 | 10 uF, 16 V, X5R 0805 | Solar-input bulk capacitance | Reuse |
| C2 | 1 | 4.7 uF, 25 V, X5R 0805 | Additional solar-input capacitance | Reuse; may become optional after testing |
| C3 | 1 | 10 nF C0G 0805 | `VREF_SAMP` storage capacitor | Reuse |
| C4 | 1 | 4.7 uF X5R 0805 | VSTOR decoupling | Reuse |
| C5 | 1 | 100 nF X7R 0805 | VSTOR high-frequency bypass | Reuse |
| C6 | 1 | 10 uF X5R 0805 | VBAT local bulk capacitor | Provisional |
| RMPPT1 | 1 | 4.42 MOhm, 1% | MPPT divider top | Reuse |
| RMPPT2 | 1 | 10 MOhm, 1% | MPPT divider bottom segment | Reuse |
| RMPPT3 | 1 | 5.6 MOhm, 1% | MPPT divider bottom segment; total ratio about 78% | Reuse |
| ROV1 | 1 | 5.62 MOhm, 1% | Battery overvoltage divider | Provisional; present circuit gives about 4.26 V |
| ROV2 | 1 | 4.42 MOhm, 1% | Battery overvoltage divider | Provisional; target should not exceed cell rating |
| RUV1 | 1 | 5.90 MOhm, 1% | BQ internal fail-safe undervoltage divider | Present circuit gives about 3.04 V |
| RUV2 | 1 | 4.12 MOhm, 1% | BQ internal fail-safe undervoltage divider | Reuse provisionally |
| ROK1–ROK3 | 3 | 3.48 MOhm / 5.36 MOhm / 1.13 MOhm, 1% | `VBAT_OK` load-off/load-on thresholds | Candidate gives about 3.18 V falling / 3.58 V rising; verify orientation in schematic |
| RQ1 | 1 | 10 MOhm, 1% | Q1 gate-to-source pullup; load defaults off | Provisional |

`VBAT_OK` is high when the stored voltage is usable. Q2 converts that high into a low gate voltage
for Q1. When `VBAT_OK` is low, RQ1 pulls Q1 gate to VSTOR and disconnects the load.

The present 3.3 V LDO cannot remain regulated all the way down to a 3.2 V load-disconnect point.
Two coherent options are:

- keep 3.3 V and use roughly 3.4–3.45 V load-off / 3.65–3.7 V load-on thresholds; or
- change to TPS7A0230 (3.0 V) and use roughly 3.2 V load-off / 3.6 V load-on thresholds.

The 3.0 V rail is electrically adequate for the E73/nRF52840 and TLC555 and gives more regulated
battery range. The 3.3 V part remains the baseline only because it is already stocked and verified.

## 3.3 V rail, MCU, diagnostics, and battery measurement

| Ref. group | Qty | Value | Purpose | Status |
|---|---:|---|---|---|
| C7 | 1 | 1 uF X7R 0805 | TPS7A0233 input capacitor | Required |
| C8 | 1 | 1 uF X7R 0805 | TPS7A0233 output capacitor | Required |
| C9 | 1 | 10 uF X5R 0805 | Local MCU/radio bulk capacitor | Recommended |
| C10–C11 | 2 | 100 nF X7R 0805 | Local E73 VDD/VDDH bypass | Recommended |
| RLED | 1 | 2.2 kOhm, 1% | LED current limiting; brief GPIO-driven flashes | Exact value may be 1–4.7 kOhm |
| RBAT1–RBAT2 | 2 | 100 kOhm, 1% | GPIO-gated 1:2 battery ADC divider | Reuse verified topology |
| CBAT | 1 | 100 nF X7R 0805 | Battery-divider ADC settling/filter capacitor | Reuse; firmware must allow settling |

## TLC555 sensing network

| Ref. | Qty | Value / part | Purpose | Status |
|---|---:|---|---|---|
| RA | 1 | 10.0 kOhm, 1% | Astable charge timing | Provisional for the current probe's estimated 50-500 pF range |
| RB | 1 | 10.0 kOhm, 1% | Astable charge/discharge timing | Provisional for the current probe's estimated 50-500 pF range |
| C12 | 1 | 100 nF X7R 0805 | TLC555 local VDD bypass | Required, adjacent to U4 |
| C13 | 1 | 10 nF C0G 0805 | TLC555 CONTROL-pin bypass | Recommended |
| RQ3 | 1 | 1 MOhm, 1% | Q3 gate-to-source pullup; timer defaults off | Provisional |
| ROUT | 1 | 1 kOhm, 1% | TLC555 OUT-to-nRF GPIO series protection | Recommended |
| CSENSE | 1 | Physical probe capacitance | Timing capacitance from TRIG/THRES node to reference electrode | Integral electrode, not purchased |

With `RA = RB = 10.0 kOhm`, the first-order relationship is approximately:

```text
f = 1.44 / ((RA + 2*RB) * CSENSE)
```

The current probe's provisional 50 pF dry to 500 pF wet estimate predicts about 960 kHz to
96 kHz. TLC555 propagation delay and discharge resistance increasingly affect the simple formula
above 100 kHz, so the dry-end result will likely be lower and must be bench-tuned. Firmware should
transmit raw frequency and apply empirical calibration. Do not reuse the larger MSV1 probe's
270 pF-to-1.28 nF measurements for V4 resistor sizing.

## Deliberately omitted from the baseline

- FDC2214, LC tank, I2C pullups, and TMUX1109.
- Frequency-to-voltage converter, monostable, low-pass filter, comparator, and moisture ADC input.
- USB-C receptacle, USB ESD array, USB charger, USB source-selection diode, and USB data resistors.
- Dedicated external temperature sensor; nRF die temperature can be logged initially. Leave optional
  I2C pads if board area permits.
- 32.768 kHz crystal. The existing LFRC build flow is known and guarded. An optional crystal footprint
  can be added if P0.00/P0.01 are kept free.

## DNP safety/options footprints worth reserving

| Parts | Purpose | Preliminary implementation |
|---|---|---|
| TLVH431B + 2 resistors | Solar-input precision shunt clamp | Pads only until the panel maximum VOC and clamp stability are bench-verified; about 330 kOhm/187 kOhm gives roughly 3.43 V |
| I2C temperature sensor + bypass | Board-temperature compensation | Pads only; no assembly on the first cost-down prototype |
| 32.768 kHz crystal + 2 load capacitors | Optional nRF LF crystal | Pads only if the selected E73 pins and layout allow it |

## Cost checkpoint

Using actual MSV3 low-quantity purchase prices where available and current low-quantity estimates
for TLC555/NMOS, the populated electronics subtotal is approximately **$16.5–$17 per board** before:

- solar panel;
- battery;
- PCB fabrication and assembly;
- enclosure and coated probe electrodes;
- any solar-input clamp; and
- optional temperature sensing or USB charging.

The dominant costs remain the E73 module (about $7.59) and BQ25504 (about $5.85). The complete
TLC555 sensing block is well under $1 before assembly.

## Decisions required before an orderable BOM

1. Record exact solar-panel MPN, dimensions, connector, specified maximum VOC, and purchased cost.
   The measured 6 V raw VOC exceeds the BQ25504 5.5 V absolute maximum, so the final design must
   either qualify a lower-VOC panel or include a low-leakage clamp. Add clamp footprints even if DNP.
2. Record exact battery MPN, capacity, protection-board presence, connector, and allowed charge voltage.
3. Choose a true 3.3 V rail with earlier cutoff, or a 3.0 V rail with more usable battery range; then
   calculate the three `VBAT_OK` resistors.
4. Confirm whether the electrodes are fully insulated. A simple 555 astable biases the timing electrode
   between about one-third and two-thirds of 3.3 V; exposed electrodes need a corrosion/polarization plan.
5. Decide whether E73/nRF52840 firmware compatibility is worth $7.59, or whether V4 should evaluate a
   cheaper nRF52810/nRF52832 BLE module.
6. Bench-measure TLC555 startup/settling time and frequency over dry/wet soil, temperature, and 3.3 V
   rail tolerance before removing optional footprints.

## Price and design references

- Actual carried-over MSV3 prices: `MSV3/procurement/LCSC__WM2607270222_20260731161456.csv`
- TLC555 datasheet: <https://www.ti.com/lit/ds/symlink/tlc555.pdf>
- Current TLC555IDR listing: <https://www.lcsc.com/product-detail/C6987.html>
- BQ25504 datasheet/reference copy: `MSV3/hardware/MSV3_Probe_PCB/reference/datasheets/BQ25504.pdf`
