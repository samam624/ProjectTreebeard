# MSV3 Probe PCB — Component Index

Built 2026-08-21 from the real schematic netlist (`netlist/MSV3_sch.NET`) and each part's real
datasheet (saved locally in `datasheets/`, extracted to matching `_extracted.txt` files for fast
text search — see the `pcb-reference` skill for how to regenerate/extend this). Every pinout below
is datasheet-verified and cross-checked against the actual netlist connections, not assumed.

| Refdes | Part | Role | Package | Datasheet |
|---|---|---|---|---|
| U1 | E73-2G4M08S1C (Ebyte) | MCU + BLE radio (nRF52840 inside) | SMD 18x13mm module | `E73-2G4M08S1C_user_manual.pdf` + `nRF52840_PS_v1.1.pdf` |
| MS_IC | FDC2214RGHR (TI) | Capacitance-to-digital, moisture sensing | WQFN-16-EP | `FDC2214.pdf` |
| MUX | TMUX1109PWR (TI) | Electrode-swap / EC-check analog mux | TSSOP/UQFN-16 | `TMUX1109.pdf` |
| SP_IC | BQ25504RGTR (TI) | Solar/energy-harvesting boost converter | VQFN-16-EP | `BQ25504.pdf` |
| USB_IC | BQ25170DSGR (TI) | LiPo linear charger | WSON-8 | `BQ25170.pdf` |
| 3V3_LDO | TPS7A0233PDBVR (TI) | Low-IQ LDO, powers MCU/mux/FDC2214 | SOT-23-5 (DBV) | `TPS7A02_TPS7A0233.pdf` |
| ESD1 | USBLC6-2P6 (ST) | USB D+/D- ESD protection | SOT-666-6 | not fetched — small, low-risk part |
| Q1 | NTR1P02T1G (onsemi) | P-channel MOSFET, battery/solar power-source mux | SOT-23 | not fetched — see BOM doc |
| D1 | Schottky (SOT-23) | Power-path diode | SOT-23 | not fetched |
| USB | TYPE-C-31-M-12 | USB-C receptacle | SMD | n/a (connector) |

Full BOM with real LCSC/Mouser pricing: `docs/MSV3_PROBE_BOM.md`. This index is about **pinout
and function**, not cost.

> **Added 2026-08-21**: the nRF52840 Product Specification (`nRF52840_PS_v1.1.pdf`, Nordic doc
> 4413_417 v1.1) was missing from this library even though the nRF52840 is the board's main IC —
> the Ebyte module manual covers the module's pinout but none of the silicon's electrical specs.
> Added for power-budget and peripheral questions. Key figures already pulled from it (§5.2.1.1
> Sleep, §5.4.4.3–5.4.4.4 oscillators) are summarized in the LFCLK/power section below.

## nRF52840 power & LF-clock figures (from `nRF52840_PS_v1.1.pdf`, verified 2026-08-21)

Relevant because this board has **no 32.768 kHz crystal populated** and runs LFRC instead — see
`docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md` for why that choice is baked into the board variant.

| Symbol | Description | Typ. | §  |
|---|---|---|---|
| `ION_RAMOFF_RTC` | System ON, no RAM retention, wake on RTC (**from LFRC**) | 1.50 µA | 5.2.1.1 |
| `ION_RAMON_RTC` | System ON, full 256 kB RAM retention, wake on RTC (**from LFRC**) | 3.16 µA | 5.2.1.1 |
| `ION_RAMON_EVENT` | System ON, full RAM retention, wake on any event (clock not running) | 2.35 µA | 5.2.1.1 |
| `IOFF_RAMOFF_RESET` | System OFF, no RAM retention, wake on reset | 0.40 µA | 5.2.1.1 |
| `ILFXO` | Run current, 32.768 kHz **crystal** | 0.23 µA | 5.4.4.3 |
| `ILFRC` | Run current, 32.768 kHz **RC** (normal mode) | 0.70 µA | 5.4.4.4 |
| `ILFULP` | Run current, RC in ultra-low-power mode | 0.30 µA | 5.4.4.5 |
| `fTOL_LFRC` | RC frequency tolerance, **uncalibrated** | ±5 % | 5.4.4.4 |
| `fTOL_CAL_LFRC` | RC frequency tolerance, **after calibration** (≥ every 8 s) | ±500 ppm | 5.4.4.4 |
| `fTOL_LFXO_BLE` | Frequency tolerance **required by the BLE stack** | ±500 ppm | 5.4.4.3 |

**The load-bearing conclusion**: calibrated LFRC (±500 ppm) exactly meets the BLE stack's own
±500 ppm requirement, which is why running crystal-less is a supported configuration and not a
hack. Note the sleep figures Nordic publishes for RTC wake are *already measured with LFRC* — so
the ~1.5–3.2 µA floor is the crystal-less number, not something to add a penalty on top of. The
oscillator delta itself is only 0.70 − 0.23 = **0.47 µA**.

## U1 — E73-2G4M08S1C (module pin → nRF52840 GPIO → Arduino pin under `adafruit:nrf52:pca10056`)

The module wraps a bare nRF52840; `pca10056`'s `g_ADigitalPinMap[]` is a straight identity map
(Arduino pin N = P0.N for N<32, P1.(N-32) for N>=32), so "Arduino pin" below is exact, not
approximate, for any sketch built with that FQBN.

| Module pin | nRF52840 | Arduino pin | Net (this board) | Function on this board |
|---|---|---|---|---|
| 2 | P1.10 | 42 | NetD2_1 → R16 → D2 | Status LED (active HIGH, 470Ω series) |
| 4 | P0.28 | 28 | SDA | I2C data to FDC2214 (4.7k pullup, R5) |
| 5 | GND | — | GND | — |
| 6 | P1.13 | 45 | SCL | I2C clock to FDC2214 (4.7k pullup, R4) |
| 7 | P0.02/AIN0 | 2 | VBAT_DIV | Battery-voltage ADC read (100k/100k divider, R29/R30) |
| 8 | P0.29/AIN5 | 29 | VBAT_DIV_EN | Enables the battery divider read |
| 9 | P0.31/AIN7 | 31 | MUX1 | TMUX1109 select bit A1 |
| 10 | P0.30/AIN6 | 30 | MUX0 | TMUX1109 select bit A0 |
| 11 | P0.00/XL1 | 0 | VDIV | EC-check divider midpoint (no 32kHz crystal populated, so this pin is free — see the LFCLK doc below) |
| 15 | P0.05/AIN3 | 5 | NetU1_15 → USB_IC-5 | BQ25170 **STAT** (charge status, active-low open-drain) |
| 17 | P1.09 | 41 | NetU1_17 → USB_IC-6 | BQ25170 **PG** (power good, active-low open-drain) |
| 19 | VDD | — | 3V3 | Main power |
| 21 | GND | — | GND | — |
| 23 | VDDH | — | 3V3 | Tied to same 3.3V rail as VDD (no separate high-voltage rail used) |
| 24 | GND | — | GND | — |
| 26 | P0.18/RESET | 18 | RESET | Reset (bare test point, no button on this board) |
| 27 | VBUS | — | VBUS | USB-C VBUS in |
| 29 | D- | — | ESP_D- (via R22, 22Ω) | USB D- |
| 31 | D+ | — | ESP_D+ (via R21, 22Ω) | USB D+ |
| 37 | SWDIO | — | SWD_IO | SWD (bare test point) |
| 39 | SWDCLK | — | SWD_CLK | SWD (bare test point) |
| 41 | P0.09/NFC1 | 9 | SD | FDC2214 shutdown (active HIGH; **needs `-DCONFIG_NFCT_PINS_AS_GPIOS`** to use as GPIO — see gotcha below) |
| 43 | P0.10/NFC2 | 10 | INTB | FDC2214 interrupt/data-ready (**same NFC gotcha**) |

**Gotcha (compile-time)**: P0.09/P0.10 default to NFC-antenna function on this chip, not GPIO,
until compiled with `-DCONFIG_NFCT_PINS_AS_GPIOS` (both `compiler.c.extra_flags` and
`compiler.cpp.extra_flags`). The core's `system_nrf52840.c` self-heals the UICR and silently
resets once, permanently, the first time this flag is used on a given chip.

**Gotcha (LFCLK)**: no 32.768kHz crystal is populated (P0.00/P0.01 repurposed as above). The
`pca10056` variant must select `USE_LFRC`, not the default `USE_LFXO` — and that selection must
happen in the **board variant**, never reconfigured from inside a sketch's `setup()`, because
FreeRTOS's scheduler (this core always ticks off RTC1, an LFCLK-driven peripheral, regardless of
tickless-idle settings) is already running by the time `setup()` executes. Getting this wrong
causes an extremely hard-to-diagnose hang after ~100-150 I2C+delay() cycles — see
`docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md` for the full incident writeup.

**For V4**: omitting the crystal is a legitimate, commonly-used choice (not a mistake), but
Nordic's own guidance recommends the external 32.768kHz crystal specifically for BLE designs —
the internal RC oscillator's timing accuracy is looser than what reliable BLE radio timing
typically wants. This board's XL1/XL2 pins are deliberately repurposed (not just left
unpopulated), so it reads as an intentional call for this revision, not an oversight. Worth an
explicit decision on the next board rev: keep the internal-RC simplicity if BLE range/reliability
stays fine in practice, or add the crystal back if it doesn't. (For comparison: MSV2's probes run
on Seeed's XIAO nRF52840 Sense, a commercial dev board, which almost certainly does populate the
crystal as the safe default — not independently confirmed against that board's own schematic.)

## MS_IC — FDC2214RGHR (capacitance-to-digital / moisture sensing)

I2C address: **0x2A** (ADDR pin strapped to GND per netlist). Only CH0 is wired to real hardware
(CH1-3 pins tied to GND on this board — confirmed unused, no separate termination needed).

Key registers (full field-level derivation in `arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino`):

| Register | Addr | Purpose |
|---|---|---|
| DATA_CH0 (MSB) | 0x00 | Read first — latches the LSB |
| DATA_LSB_CH0 | 0x01 | Read second |
| RCOUNT_CH0 | 0x08 | Conversion time (must be ≥0x0100, default 0x0080 is invalid/reserved) |
| SETTLECOUNT_CH0 | 0x10 | Settle time before conversion (default 0 too short, causes amplitude warnings) |
| CLOCK_DIVIDERS_CH0 | 0x14 | FIN_SEL / FREF_DIVIDER |
| STATUS | 0x18 | DRDY (bit6), CH0_UNREADCONV (bit3), ERR_WD (bit11), ERR_AHW (bit10), ERR_ALW (bit9) |
| CONFIG | 0x1A | ACTIVE_CHAN, SLEEP_MODE_EN, REF_CLK_SRC, INTB_DIS |
| MUX_CONFIG | 0x1B | AUTOSCAN_EN, DEGLITCH (left at power-on default 0x020F on this board — single-channel, correct) |
| DRIVE_CURRENT_CH0 | 0x1E | IDRIVE (sensor drive current, NOT bench-tuned as of 8/21 — starting guess 0x8000 works but hasn't been optimized) |
| MANUFACTURER_ID | 0x7E | Always 0x5449 ("TI") |
| DEVICE_ID | 0x7F | **0x3055 for FDC2214** (0x3054 is FDC2112/FDC2114 — easy mix-up, same family) |

**Sensing network on this board**: TANK_HOT/TANK_REF (MS_IC pins 9/10) are hardwired directly to
the FDC2214 and a fixed L2 (6.8µH)/C17 (270pF) tank — **never routed through the mux**. What the
mux (below) actually switches is whether the *external electrodes* are connected in parallel
across that always-oscillating tank (electrode-swap mode) or disconnected and redirected to the
EC-check divider (EC-check mode). Confirmed empirically 8/20: electrode-swap codes read
~20.34M, EC-check codes read ~22.62M (bare-tank-only, no electrode capacitance) — a clean,
repeatable ~11% step, not an oscillation failure in either mode.

## MUX — TMUX1109PWR (dual 4:1 precision mux)

Select pins: A0 (pin1, = MUX0 net) and A1 (pin16, = MUX1 net) — no separate EN net found, likely
hardwired always-enabled. Two independent 4:1 banks:

| Bank | Common (electrode) | 4 throw destinations |
|---|---|---|
| Node A | MS_NODE_A (pin 8) | TANK_HOT (4), 3V3 (2), TANK_REF (5), VDIV (7) |
| Node B | MS_NODE_B (pin 9) | 3V3 (10), VDIV (11), TANK_HOT (12), TANK_REF (13) — rotated order vs Node A |

Datasheet specs (VDD=3.3V, verified from real PDF table): RON 8.75Ω typ / 9.75Ω max,
leakage 0.5/2nA max, charge injection 1pC, CON 38pF.

## SP_IC — BQ25504RGTR (energy harvesting boost)

| Pin | Name | Net (this board) |
|---|---|---|
| 1 | VSS | GND |
| 2 | VIN_DC | SP+ (solar input, via L1 22µH harvester inductor) |
| 3 | VOC_SAMP | VOC_SAMP (R9/R10 divider) |
| 4 | VREF_SAMP | VREF_SAMP (C9 storage cap) |
| 5 | OT_PROG | VSTOR (selects 120°C overtemp threshold, per datasheet's own documented option) |
| 6 | VBAT_OV | VBAT_OV (R11/R14 divider) |
| 7 | VRDIV | VRDIV (R11/R12 divider bias) |
| 8 | VBAT_UV | VBAT_UV (R12/R15 divider) |
| 9 | OK_HYST | GND (VBAT_OK hysteresis feature unused — tied off per datasheet's "if not used" guidance) |
| 10 | OK_PROG | GND (VBAT_OK threshold feature unused, same reason) |
| 11 | VBAT_OK | not connected on this board (floating, matches "leave floating if not used") |
| 12 | AVSS | GND |
| 13 | VSS | GND |
| 14 | VBAT | VBAT (battery net) |
| 15 | VSTOR | VSTOR (boost output, decoupled via C10/C11) |
| 16 | LBST | LBST (harvester inductor L1, other end) |

**Not yet tested** (as of 8/21): no solar panel/daylight available during bring-up. Charger,
harvester, and the Q1 power-source mux are the three subsystems still unverified on real hardware
— see `docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md`'s parent bring-up plan for status.

## USB_IC — BQ25170DSGR (LiPo linear charger)

| Pin | Name | Net (this board) |
|---|---|---|
| 1 | IN | VBUS |
| 2 | ISET | R7 (fast-charge current set resistor) |
| 3 | TS | R6 (NTC thermistor or fixed resistor for temp qualification) |
| 4 | GND | GND |
| 5 | STAT | → U1 module pin 15 (P0.05, Arduino pin 5) |
| 6 | PG | → U1 module pin 17 (P1.09, Arduino pin 41) |
| 7 | VSET | R8 (regulation voltage set resistor) |
| 8 | OUT | VBAT |
| — | Thermal pad | GND |

**Not yet verified**: whether ISET/VSET resistor values (R7/R8) are actually sized for a safe
charge rate on the real battery cell — flagged as an open item since 2026-08-01, still open as of
8/21 (needs the real resistor values pulled from the schematic and checked against the datasheet's
`ICHG = KISET / RISET` formula).

## 3V3_LDO — TPS7A0233PDBVR (SOT-23-5, DBV package)

| Pin | Name | Net (this board) |
|---|---|---|
| 1 | IN | Net3V3_LDO_1 (tied together with EN — self-enabling whenever powered) |
| 2 | GND | GND |
| 3 | EN | Net3V3_LDO_1 (same net as IN) |
| 4 | NC | not connected |
| 5 | OUT | 3V3 (powers U1 VDD/VDDH, MUX, MS_IC) |

Fed from the power-mux (Q1/D1) output, not directly from VBAT or VBUS — confirms the LDO input is
downstream of whatever source-select logic Q1 implements.
