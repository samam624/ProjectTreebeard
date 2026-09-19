# MSV4 Probe PCB — Component Index

Built 2026-09-17 from the real Altium netlist (`netlist/MSV4_sch_2026-09-17.NET`, exported
17:26 that day) and each part's real datasheet (saved in `datasheets/`, extracted to matching
`_extracted.txt` for fast text search — see the `pcb-reference` and `datasheet-lookup` skills).
**Every number below was read out of the primary-source datasheet table, not a distributor
summary or a product page.** Where a web summary disagreed with the datasheet, the datasheet
value is used and the discrepancy is recorded.

Architecture this board implements: `MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md`
(software lock-in, no FDC) and `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md`
(guarded blade, NTC at the tip).

| Refdes | Part | Role | Package | Datasheet |
|---|---|---|---|---|
| U1 | E73-2G4M08S1C (Ebyte) | MCU + BLE radio (nRF52840 inside) | SMD 18×13 mm module | `E73-2G4M08S1C_user_manual.pdf` + `nRF52840_PS_v1.1.pdf` |
| U | BQ25504RGTR (TI) | Solar energy-harvesting boost charger | VQFN-16-EP (RGT) | `BQ25504.pdf` |
| U2 | TPS7A0233PDBVR (TI) | 3.3 V low-IQ LDO, gated by VBAT_OK | SOT-23-5 (DBV) | `TPS7A02_TPS7A0233.pdf` |
| U3 | FSUSB42UMX (onsemi/Fairchild) | **A/B electrode swap** DPDT + OPEN state via /OE | 10-lead UMLP 1.4×1.8 mm | `FSUSB42.pdf` |
| U4 | FSUSB42UMX (onsemi/Fairchild) | **Chopper** (ch1) + drive-node drift monitor (ch2) | 10-lead UMLP 1.4×1.8 mm | `FSUSB42.pdf` |
| Q1 | NTZD3155CT2G (onsemi) | Complementary N+P MOSFET — level-shifted high-side switch for the VBAT divider | SOT-563 (CASE 463A) | `NTZD3155C.pdf` |
| L1 | LPS4018-223MRC (Coilcraft) | 22 µH harvester boost inductor | 4.0×4.0×1.8 mm | `LPS4018_coilcraft.pdf` |
| Y1 | SC-32S 32.768 kHz ±20 ppm CL=7 pF (Seiko Instruments) | **LFXO crystal — populated on V4** | SMD3215 (3.2×1.5×0.85 mm) | `SC-32S_crystal.pdf` |
| R17 | NCP15XH103F03RC (Murata) | Soil-temperature NTC at the blade tip | 0402 | `NCP15XH103F03RC_murata.pdf` (catalogue R44E) |
| D1 | SM0805UBWC (Bivar) | Status LED — **blue** | 0805 | `SM0805UBWC_LED.pdf` |

Everything else is 0805 passives (`TB_C_0805_2012M` / `TB_R_0805_2012M`) — values carried in the
netlist itself, so the netlist is authoritative for them, not a separate BOM.

---

## What changed from MSV3 (read this first)

Four structural differences. Each one invalidates an MSV3 rule that is written down elsewhere in
this repo, so none of them is safe to carry over by habit.

### 1. The 32.768 kHz crystal is populated. MSV3's LFRC rule does NOT apply to V4.

MSV3 has no LFXO and repurposes `P0.00`/`P0.01`, which is why every MSV3 build must select
`USE_LFRC` in the board variant and why `MSV3/scripts/arduino_msv3_probe.ps1` exists to enforce it
(`docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md`).

**V4 populates Y1 across module pins 11/13 = `P0.00/XL1` and `P0.01/XL2`, with C3/C2 = 10 pF
load caps.** So V4 can and should run `USE_LFXO`. It needs its **own board variant** — do not
point an MSV4 build at the MSV3 variant, in either direction. Running the LFRC variant on this
board merely wastes the crystal; running an LFXO variant on an MSV3 board reproduces the
RTC1 hang.

### 2. No USB. At all.

Module pins 27 (`VBUS`), 29 (`D−`) and 31 (`D+`) are **unconnected**, there is no USB-C
receptacle, and there is no BQ25170 charger. MSV3's `BQ25170` + USB-C path is gone.

Consequences: no USB CDC serial, no UF2/USB DFU, and **solar is the only way charge enters the
cell**. Firmware loading is SWD or BLE OTA only. The most recent MSV3 work (`c6f9cce`, "Make USB
a normal MSV3 transport, with BLE OTA running on top of it") does not transfer.

### 3. No NFC pins used → no `CONFIG_NFCT_PINS_AS_GPIOS`.

MSV3 drives the FDC2214 from `P0.09`/`P0.10` (module pins 41/43) and therefore needs
`-DCONFIG_NFCT_PINS_AS_GPIOS` on both `compiler.c.extra_flags` and `compiler.cpp.extra_flags`,
plus the one-time silent UICR self-heal that comes with it. **V4 uses neither pin.** That whole
compile-flag hazard disappears.

### 4. The battery divider is a real high-side switch, not a GPIO-grounded leg.

MSV3 drove the bottom of the divider from `P0.29`. V4 uses Q1 (NTZD3155C) as an N-to-P level
shifter so the divider is fully disconnected from VBAT when idle — see the Q1 section below.

---

## U1 — E73-2G4M08S1C (module pin → nRF52840 → Arduino pin → net)

Module pin numbering verified against `E73-2G4M08S1C_user_manual_extracted.txt` §3 ("Size and pin
definition"). `pca10056`'s `g_ADigitalPinMap[]` is an identity map (Arduino pin N = P0.N for
N < 32), so the Arduino column is exact for any sketch built with that FQBN.

| Module pin | nRF52840 | Arduino | Net | Function on this board |
|---|---|---|---|---|
| 2 | P1.10 | 42 | `NetR10_1` | Status LED via R10 (470 Ω) → D1 |
| 3 | P0.03/**AIN1** | 3 | `SENSE_OUT` | **Measurement ADC** — post-chopper, after R18/C18 |
| 5 | GND | — | `GND` | — |
| 7 | P0.02/**AIN0** | 2 | `VBAT_DIV` | Battery-voltage ADC (R5/R9 100k/100k) |
| 11 | P0.00/**XL1** | 0 | `NetC3_2` | **32.768 kHz crystal** ← populated on V4 |
| 13 | P0.01/**XL2** | 1 | `NetC2_1` | **32.768 kHz crystal** |
| 14 | P0.06 | 6 | `VBAT_DIV_EN` | Q1 N-channel gate — enables the battery divider |
| 15 | P0.05/**AIN3** | 5 | `TEMP` | **Soil NTC ADC** — R16/R17 divider midpoint |
| 18 | P0.04/**AIN2** | 4 | `DRV_OUT` | **Drift-monitor ADC** — post-chopper, after R19/C19 |
| 19 | VDD | — | `3V3` | Main power |
| 20 | P0.12 | 12 | `TEMP_READ` | NTC divider drive (pulse, not continuous) |
| 21 | GND | — | `GND` | — |
| 22 | P0.07 | 7 | `DRV` | **Excitation square wave** → Rs (R15) → sense node |
| 23 | VDDH | — | `3V3` | Tied to the same 3.3 V rail as VDD |
| 24 | GND | — | `GND` | — |
| 26 | P0.18/RESET | 18 | `RESET` | Reset, C6 (100 nF) + bare test point |
| 28 | P0.15 | 15 | `SWAP_nOE` | U3 `/OE` — floats both electrodes (OPEN reference / sleep isolation) |
| 30 | P0.17 | 17 | `READ_CNTL` | U4 `Sel` — **the chopper**, driven by TIMER→PPI→GPIOTE at 1 MHz |
| 32 | P0.20 | 20 | `READ_nOE` | U4 `/OE` |
| 33 | P0.13 | 13 | `SWAP_CTRL` | U3 `Sel` — electrode A/B select |
| 37 | SWDIO | — | `SWD_IO` | SWD, bare test point |
| 39 | SWDCLK | — | `SWD_CLK` | SWD, bare test point |

**Unconnected on purpose:** 27 (VBUS), 29 (D−), 31 (D+) — no USB. 41/43 (NFC1/NFC2) — no NFC
flag needed.

**All four ADC nets land on real `AIN` pins** (AIN0–AIN3). Unlike MSV3's `VDIV` mistake — which
landed on `P0.00`, a pin with no SAADC path at all, and needed a bodge — nothing on this board
needs an analog read from a pin that cannot do one.

### LFXO load capacitance (Y1, C2, C3)

Per `nRF52840_PS_v1.1` §5.4.2.4, the load the crystal sees is the series combination of each
branch, where each branch is the external cap plus PCB stray plus the pin capacitance:

- `Cpin` on XL1/XL2 = **4 pF** (PS §5.4.4.3)
- C2 = C3 = 10 pF, `Cpcb` ≈ 2 pF (estimate)
- → C_L ≈ (10 + 4 + 2) / 2 = **8.0 pF**, against a crystal specified for **7 pF**

That 1 pF over-load pulls roughly **−20 ppm** (tuning-fork sensitivity ≈ −C1/(2(C0+CL)²), with
C0 = 1.0 pF typ from the SC-32S sheet). Stacked worst-case with the crystal's own ±20 ppm, the
−0.030 ppm/°C² parabola over 0–40 °C (≤19 ppm) and ±3 ppm/yr ageing, the total stays under
~65 ppm — comfortably inside the BLE stack's **±500 ppm** requirement (PS §5.4.4.3). **Leave it
at 10 pF.** 8.2 pF would centre it if you ever care; ANT's ±50 ppm would be the only reason to.

Margins that do matter, both satisfied:

| | SC-32S | nRF52840 requires | |
|---|---|---|---|
| ESR (R1) | 70 kΩ max | ≤ 100 kΩ (`RS_LFXO`) | ✓ |
| Drive level | 1.0 µW max | 0.5 µW max (`PD_LFXO`) | ✓ |

**Layout rule from the crystal datasheet:** *"When designing the circuit board, do not place
circuit patterns on the crystal resonator mounting area (bottom surface)."* No copper under Y1.

---

## U3 / U4 — FSUSB42UMX (the entire analog front end)

`MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` specifies **TS3USB221A**. This board uses
**FSUSB42UMX** instead. Verified from the primary datasheet, the substitution is better on every
axis the architecture doc actually cares about:

| | TS3USB221A (as specced in the doc) | **FSUSB42 (measured from `FSUSB42.pdf`)** |
|---|---|---|
| C_ON | 6 pF | **3.7 pF** typ (VCC=3.3 V, /OE=0, f=240 MHz) |
| C_OFF | 3.5 pF | **2.0 pF** typ (VCC and /OE = 3.3 V) |
| R_ON | 3 Ω | **3.9 Ω** typ, 6.5 Ω max (V_SW=0.4 V, I_ON=−8 mA, VCC=3.0 V) |
| ΔR_ON (match) | — | 0.65 Ω typ |
| t_ON | 30 ns | **13 ns** typ, 30 ns max (S,/OE→out, VCC 3.0–3.6 V, −40…+85 °C) |
| t_OFF | 12 ns | **12 ns** typ, 25 ns max |
| **t_ON − t_OFF asymmetry** | **18 ns** | **1 ns typ** |
| I_CC | — | 1 µA max (VCC = 4.3 V) |
| Bandwidth | 900 MHz (signal path) | >720 MHz (signal path) |

The asymmetry is the one that matters. The architecture doc's ceiling analysis — *"the 30 ns
fixed delay is harmless… the 18 ns asymmetry is the limit… usable as a chopper to roughly
1–2 MHz, not 8"* — was written against TS3USB221A. At the chosen **N=16, f = 1.000 MHz**
operating point that 18 ns cost **6.5° of phase error**. FSUSB42's 1 ns typical asymmetry costs
**0.36°**, ~18× better, and the whole 1–2 MHz ceiling argument relaxes accordingly.

Two caveats before leaning on that:
- 1 ns is **typ − typ**. The max column (30 / 25 ns) leaves room for a larger part-to-part
  asymmetry. A fixed per-part offset calibrates out; it is the *drift* of that offset with
  temperature and VDD that corrupts a loss measurement, and neither sheet specifies it.
- C_ON is characterised at **f = 240 MHz**. At 1 MHz it will differ somewhat.

Do not re-derive the ceiling from the doc's table without noticing the part changed.

### Pinout (10-lead UMLP — the `UMX` suffix), verified from `FSUSB42_extracted.txt` "Pin Definitions"

| Pin | Name | | Pin | Name |
|---|---|---|---|---|
| 1 | **D+** (common) | | 6 | HSD2− |
| 2 | **D−** (common) | | 7 | HSD2+ |
| 3 | GND | | 8 | **/OE** |
| 4 | HSD1− | | 9 | VCC |
| 5 | HSD1+ | | 10 | **Sel** |

Truth table: `/OE` HIGH → Disconnect (both channels). `/OE` LOW + `Sel` LOW → D+=HSD1+,
D−=HSD1−. `/OE` LOW + `Sel` HIGH → D+=HSD2+, D−=HSD2−.

**Note 5 in the datasheet:** *"The control input must be held HIGH or LOW and it must not
float."* This is why R20–R23 exist (see PIN_NET_REFERENCE).

V_SW (switch I/O) recommended range is **−0.5 V to 4.5 V**, so a rail-to-rail 0–3.3 V excitation
is inside spec. R_ON is only *characterised* at V_SW = 0.4 V, so its flatness across a full-swing
square wave is a curve (Figure 4), not a table entry — irrelevant here, since R_ON sits in series
with Rs (hundreds of Ω) in the swap and with 100 k in the chopper. The architecture doc's
"deliberately buy a higher-R_on part" reasoning holds.

---

## U — BQ25504RGTR (solar harvester)

Pinout verified from `BQ25504_extracted.txt` Table 6-1:

| Pin | Name | | Pin | Name |
|---|---|---|---|---|
| 1 | VSS | | 9 | **OK_HYST** |
| 2 | VIN_DC | | 10 | **OK_PROG** |
| 3 | VOC_SAMP | | 11 | VBAT_OK |
| 4 | VREF_SAMP | | 12 | AVSS |
| 5 | OT_PROG | | 13 | VSS |
| 6 | VBAT_OV | | 14 | VBAT |
| 7 | VRDIV | | 15 | VSTOR |
| 8 | VBAT_UV | | 16 | LBST |

**Pin 9 is OK_HYST and pin 10 is OK_PROG — not the other way round.** Easy to invert; the ladder
order in the schematic depends on it.

### Threshold equations — verified against the datasheet's own worked example

`VBIAS` = **1.25 V typ** (1.21 min / 1.27 max), `BQ25504_extracted.txt` line ~860. Note this is
*typ*; using the 1.21 V minimum by mistake shifts every threshold down ~3 %.

```
VBAT_OV       = (3/2) · VBIAS · (1 + ROV2/ROV1)      ROV1 = bottom (pin→GND), ROV2 = top (VRDIV→pin)
VBAT_UV       =         VBIAS · (1 + RUV2/RUV1)      RUV1 = bottom,           RUV2 = top
VBAT_OK_PROG  =         VBIAS · (1 + ROK2/ROK1)      falling threshold
VBAT_OK_HYST  =         VBIAS · (1 + (ROK2+ROK3)/ROK1)   rising threshold
```

The `3/2` on the OV equation is real and is the classic trap here — `datasheet-lookup`'s SKILL.md
records a previous session losing time to exactly this. Both the factor and the resistor
orientation are confirmed by reproducing TI's §9.2.1.1 example: ROV1 = 5.90 MΩ, ROV2 = 4.02 MΩ →
1.875 × (1 + 4.02/5.90) = **3.153 V**, matching TI's stated 3.15 V.

The VBAT_OK ladder runs **VRDIV — ROK3 — [pin 9 OK_HYST] — ROK2 — [pin 10 OK_PROG] — ROK1 — GND.**
Both thresholds divide by ROK1, because the chip switches ROK3 in and out internally rather than
moving the tap.

### As populated on this board

| | Top (VRDIV side) | Bottom (GND side) | Computed |
|---|---|---|---|
| OV | R11 = 5.36 M | R13 = 4.99 M | **3.889 V** |
| OK rising (HYST) | R3 = 1.00 M (ROK3), R4 = 5.90 M (ROK2) | R7 = 3.48 M (ROK1) | **3.728 V** |
| OK falling (PROG) | R4 = 5.90 M (ROK2) | R7 = 3.48 M (ROK1) | **3.369 V** |
| UV | R12 = 5.90 M | R14 = 4.12 M | **3.040 V** |

TI's required ordering `VBAT_OV ≥ VBAT_OK_HYST > VBAT_OK_PROG ≥ VBAT_UV` holds:
3.889 ≥ 3.728 > 3.369 ≥ 3.040. ✓

Resistor-sum guidance (≈10 MΩ per ladder) also met: OV 10.35 M, UV 10.02 M, OK 10.38 M.

**MPPT:** VOC_SAMP taps R2 (4.42 M) above / R6+R8 (10 M + 5.6 M = 15.6 M) below →
15.6/20.02 = **77.9 %** of V_OC. TI's solar example uses 78 %. ✓ (`NetR6_1`, the R6/R8 junction,
connects to nothing else — the pair is a series build of one value.)

`OT_PROG` (pin 5) ties to `VSTOR` → **120 °C** overtemperature threshold (GND would select 60 °C).

**Support components vs datasheet minimums:** L1 = 22 µH ✓ (recommended value exactly);
C7 = 4.7 µF on VIN_DC ✓ (min 4.7 µF); C10 = **10 nF C0G** on VREF_SAMP ✓ (TI asks for a
*low-leakage* 10 nF, and C0G is the right call — this cap holds the sampled MPP reference between
sampling cycles, so its leakage directly becomes MPPT error); C11/C12/C13 = 4.7 µF + 100 nF +
1 µF on VSTOR ✓.

**L1 headroom** (`LPS4018_coilcraft_extracted.txt`): 22 µH ±20 %, DCR 0.360 Ω, SRF 26 MHz,
Isat 0.74 A (10 % L drop), Irms 0.65 A (20 °C rise). Far above anything this harvester will draw.

---

## U2 — TPS7A0233PDBVR (3.3 V LDO)

SOT-23-5 (DBV) pinout, `TPS7A02_TPS7A0233_extracted.txt` Table 5-1: **1 = IN, 2 = GND, 3 = EN,
4 = NC, 5 = OUT.** Pin 4 NC is explicitly *"Connect to ground or leave floating"* — this board
grounds it, which is fine.

**The LDO is gated by the harvester.** `VBAT_OK` (U pin 11) drives `EN` (U2 pin 3), so the 3.3 V
rail — and therefore the whole MCU and both switches — only comes up once VSTOR has risen past
3.728 V, and drops out below 3.369 V. That is a genuinely good arrangement: it gives the
harvester a guaranteed accumulation window before the load ever appears, with 359 mV of
hysteresis, entirely in hardware.

**The level interface works.** BQ25504 drives `VBAT_OK` high *through ~20 kΩ internal series
resistance* (datasheet §8.3.4, there to protect an unpowered MCU). TPS7A02's EN has a **500 kΩ**
smart pulldown and needs **V_EN(HI) ≥ 1.1 V**. The divider gives 3.4 V × 500/520 = **3.27 V** —
3× margin, and the pulldown disconnects once the device has started. ✓

---

## Q1 — NTZD3155CT2G (battery-divider high-side switch)

SOT-563 pinout read off the rendered datasheet page 1 diagram (it does not extract as text):

| Pin | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| | **S1** (N src) | **G1** (N gate) | **D2** (P drain) | **S2** (P src) | **G2** (P gate) | **D1** (N drain) |

Wiring: N-drain (6) ties to P-gate (5); R1 (1 MΩ) pulls that node to VBAT; N-source (1) to GND;
N-gate (2) to `VBAT_DIV_EN`; P-source (4) to VBAT; P-drain (3) to R5 → the divider.

- `VBAT_DIV_EN` LOW → NMOS off → R1 holds the P-gate at VBAT → V_GS(P) = 0 → PMOS off →
  **divider fully disconnected, zero standby current.**
- `VBAT_DIV_EN` HIGH (3.3 V) → NMOS on → P-gate to ~0 → V_GS(P) = −VBAT → PMOS on.

Ratings check: V_GS abs max = **±6 V**, and VBAT is capped at VBAT_OV = 3.889 V, so the worst
case is −3.9 V. ✓ **This margin is tied to the OV divider** — if R11/R13 are ever changed for a
higher-voltage cell, re-check it. V_GS(th): N 0.45–1.0 V, P −0.45 to −1.0 V, so both switch
hard at these levels. Enabled current ≈ VBAT/200 k (divider) + VBAT/1 M (R1) ≈ 21 µA.

---

## R17 — NCP15XH103F03RC (soil NTC)

From Murata catalogue R44E (`NCP15XH103F03RC_murata_extracted.txt`), 10 kΩ / B25/50 = 3380 K row:

| Parameter | Value |
|---|---|
| R25 | 10 kΩ ±1 % |
| B25/50 | 3380 K ±1 % |
| B25/80 / B25/85 / B25/100 (reference) | 3428 / 3434 / 3455 K |
| **Permissible operating current (25 °C)** | **0.31 mA** |
| Rated electric power (25 °C) | 100 mW |
| **Typical dissipation constant (25 °C)** | **1 mW/°C** |

**Two corrections that matter:**

1. **A web search reported "maximum operating current 0.1 mA." That is wrong** — the primary
   catalogue says **0.31 mA** for any 10 kΩ part in this family, and it is internally consistent
   (I = √(1 mW / 10 kΩ) = 0.316 mA). It is also not a damage limit: Murata defines *permissible
   operating current* as *"It is possible to keep the thermistor's temperature rising max. 1 °C."*
   The damage limit is the 100 mW rated power, ~300× higher.

2. **`MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md` understates the self-heat by 2×.** It says
   *"~0.27 mW, which is ~0.14 °C of self-heat."* The power is right — 3.3 V / 20 kΩ = 165 µA,
   P = I²R = **0.272 mW** — but at the verified 1 mW/°C dissipation constant that is
   **0.27 °C**, not 0.14 °C. The doc implicitly assumed ~2 mW/°C.

   This strengthens rather than changes the design: the doc's own error budget is ~0.26 °C at
   25 °C, so continuous drive would **double** the total error, not add half of it. The pulsed
   GPIO read is mandatory, not an optimisation. Circuit current is 165 µA against the 0.31 mA
   permissible figure, so even DC operation stays inside the ≤1 °C definition — but it spends
   the whole budget.

   Caveat in the other direction: 1 mW/°C is measured on a bare part on a test board in still
   air. Under an epoxy blob in wet soil the dissipation constant will be considerably better and
   the real self-heat lower. Treat 0.27 °C as a conservative bound, not a prediction.

The catalogue does not publish a per-part thermal time constant, so the doc's *"the 0402's
thermal time constant is seconds"* is **unverified** — it is the assumption that makes a few-ms
pulse read ≈0 self-heat, and it is worth measuring on the bench rather than trusting.

---

## D1 — SM0805UBWC (status LED)

**Blue**, InGaN, 470 nm. `SM0805UBWC_LED_extracted.txt`: **V_F = 3.3 V typ, 4.2 V max** at
I_F = 20 mA, tolerance ±0.05 V, I_V = 115 mcd typ, 130° viewing angle.

Those V_F numbers are quoted at 20 mA and look impossible against a 3.3 V rail. **They are not a
problem at this circuit's ~0.5–1 mA — maintainer-confirmed working 2026-09-17. Leave it alone.**
See PIN_NET_REFERENCE finding 3.

---

## Regenerating this library

Datasheets were fetched with `fetch_datasheet.py` in this directory (PyMuPDF extraction with
`===PAGE N===` markers). `WebFetch` on a vendor PDF is not a substitute — see the
`datasheet-lookup` skill for why, and for the "render the page as an image when the text is an
equation" rule that was needed here for the NTZD3155C pin diagram.

onsemi, Mouser, DigiKey and Murata all 403 a plain `curl`. Working sources this session:
LCSC (`atta.szlcsc.com`), Farnell (`farnell.com/datasheets/`), `componentsearchengine.com`,
`dtsheet.com`, Bivar and SII direct, Coilcraft `coilcraft.com/pdfs/`.
