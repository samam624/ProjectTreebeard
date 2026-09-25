# MSV4 Probe PCB — Pin / Net Reference

Source: `netlist/MSV4_sch_2026-09-17_rev3.NET` (Altium export, 2026-09-17 18:48). Pinouts are
datasheet-verified — see `COMPONENT_INDEX.md` for each part's pin table and the datasheet it came
from. Component roles: `U1` = E73 module, `U` = BQ25504 harvester, `U2` = TPS7A0233 LDO,
`U3` = FSUSB42 electrode swap, `U4` = FSUSB42 chopper, `Q1` = NTZD3155C, `L1` = 22 µH,
`Y1` = 32.768 kHz crystal, `R17` = NTC, `D1` = LED.

## Nets

| Net | Pins | Function |
|---|---|---|
| `3V3` | C4-1, C5-1, C14-2, C15-1, C17-1, SW_3V3-1, U1-19, U1-23, U2-5, U3-9, U4-9, 3V3-1 | Main 3.3 V rail — LDO output. Feeds MCU VDD/VDDH and both switches. **Only live when `VBAT_OK` is asserted.** |
| `GND` | 46 pins — see netlist | Common ground. Includes U-17 (BQ25504 thermal pad) and U2-4 (LDO NC pin). |
| `VBAT` | C1-1, C8-2, Q1-4, R1-2, U-14, VBAT1-1, VBAT+-1 | Battery rail. Harvester VBAT pin, Q1 P-source, R1 gate pullup. **Test points only — no connector.** |
| `VSTOR` | C11-1, C12-2, C13-2, U-5, U-15, U2-1, VSTOR-1 | Harvester boost output. Also OT_PROG (U-15) → 120 °C threshold, and LDO IN. |
| `SP+` | C7-2, L1-1, R2-2, SP+-1, U-2 | Solar panel positive → BQ25504 VIN_DC, via L1. |
| `LBST` | L1-2, U-16 | Harvester boost switching node. |
| `VOC_SAMP` | R2-1, R6-2, U-3 | MPPT sample tap — 77.9 % of V_OC. |
| `NetR6_1` | R6-1, R8-2 | R6/R8 junction. Series pair only; connects nowhere else. |
| `VREF_SAMP` | C10-2, U-4 | Sampled MPP reference held on C10 (10 nF **C0G** — leakage here is MPPT error). |
| `VRDIV` | R3-2, R11-2, R12-2, U-7 | Switched divider bias. Top of all three threshold ladders. |
| `VBAT_OV` | R11-1, R13-2, U-6 | OV tap → **3.889 V**. |
| `VBAT_UV` | R12-1, R14-2, U-8 | UV tap → **3.040 V**. |
| `NetR3_1` | R3-1, R4-2, **U-9 (OK_HYST)** | Upper VBAT_OK tap → rising threshold **3.728 V**. |
| `NetR4_1` | R4-1, R7-2, **U-10 (OK_PROG)** | Lower VBAT_OK tap → falling threshold **3.369 V**. |
| `VBAT_OK` | U-11, U2-3 | **Harvester gates the LDO enable.** Drive is VSTOR through ~20 kΩ internal; EN needs ≥1.1 V into a 500 kΩ pulldown → 3.27 V. ✓ |
| `VBAT_DIV_EN` | Q1-2, U1-14 | P0.06 → Q1 N-gate. HIGH enables the battery divider. |
| `NetQ1_5` | Q1-5, Q1-6, R1-1 | N-drain ↔ P-gate, pulled to VBAT by R1 (1 M). The level shift. |
| `NetQ1_3` | Q1-3, R5-2 | P-drain → top of the battery divider. |
| `VBAT_DIV` | C9-2 **(DNP)**, R5-1, R9-2, U1-7 | Battery ADC midpoint (100k/100k) → P0.02/AIN0. C9 depopulated — pad kept, see finding 4. |
| `DRV` | DRV-1, R15-1, U1-22, U4-4 | **Excitation node.** P0.07 square wave, tapped *before* Rs for the drift monitor (U4 HSD1−). |
| `SENSE` | R15-2, SNS-1, U3-5, U3-6, U4-5 | **Sense node.** After Rs (R15 = 1 kΩ). Feeds U3 HSD1+/HSD2− (crossed swap) and U4 HSD1+ (chopper). |
| `MS_A` | MS_A-1, U3-1 | Electrode A ← U3 `D+` common. |
| `MS_B` | MS_B-1, U3-2 | Electrode B ← U3 `D−` common. |
| `SWAP_CTRL` | R20-1, U1-33, U3-10 | U3 `Sel` ← P0.13. 100 k pulldown R20. |
| `SWAP_nOE` | R21-2, U1-28, U3-8 | U3 `/OE` ← P0.15. 100 k **pullup to 3V3** (R21) — default state is isolated. |
| `NetR18_1` | R18-1, U4-1 | U4 `D+` common → R18 (100 k) averaging filter. |
| `SENSE_OUT` | C18-2, R18-2, SNSO-1, U1-3 | Measurement ADC ← P0.03/AIN1. R18/C18 = 100 k / 1 nF, τ = 100 µs. |
| `NetR19_1` | R19-1, U4-2 | U4 `D−` common → R19 (100 k) averaging filter. |
| `DRV_OUT` | C19-2, DRVO-1, R19-2, U1-18 | Drift-monitor ADC ← P0.04/AIN2. Read at quadrature (VDD/2). |
| `READ_CNTL` | R22-1, U1-30, U4-10 | U4 `Sel` — **the chopper**, ← P0.17 via TIMER/PPI/GPIOTE. 100 k pulldown R22. |
| `READ_nOE` | R23-2, U1-32, U4-8 | U4 `/OE` ← P0.20. 100 k **pullup to 3V3** (R23) — default state is isolated. |
| `TEMP_READ` | R16-2, U1-20 | NTC divider drive ← P0.12. Pulse only. |
| `TEMP` | C16-1, R16-1, R17-2, U1-15 | NTC divider midpoint → P0.05/AIN3. C16 (1 nF) is the RF bypass to guard. |
| `NetC3_2` | C3-2, U1-11, Y1-1 | LFXO XL1 + 10 pF load cap. |
| `NetC2_1` | C2-1, U1-13, Y1-2 | LFXO XL2 + 10 pF load cap. |
| `RESET` | C6-1, RST-1, U1-26 | P0.18 reset, 100 nF, bare test point. |
| `SWD_IO` / `SWD_CLK` | IO-1/U1-37, CLK-1/U1-39 | SWD, bare test points. **The only wired programming path on this board.** |
| `NetR10_1` | R10-1, U1-2 | P1.10 → 470 Ω → D1. |
| `NetD1_1` | D1-1, R10-2 | LED anode. |

## Switch wiring — verified correct

**U3, the A/B swap.** The architecture doc's crossed wiring is implemented exactly:

| U3 pin | Name | Net |
|---|---|---|
| 1 | D+ (common) | `MS_A` — electrode A |
| 2 | D− (common) | `MS_B` — electrode B |
| 5 | HSD1+ | `SENSE` |
| 4 | HSD1− | `GND` |
| 7 | HSD2+ | `GND` |
| 6 | HSD2− | `SENSE` |

`Sel`=0 → A hot, B grounded. `Sel`=1 → B hot, A grounded. `/OE`=1 → both float (the OPEN
reference). One Rs (R15) is in circuit either way, so any Rs error is common-mode across the
swap — which is what makes a swap-state difference mean something real.

**U4, the chopper.** `D+`(1) and `D−`(2) are the two filter inputs; `HSD1+`(5) = `SENSE`,
`HSD1−`(4) = `DRV`, and both HSD2 throws (6,7) are GND. So `Sel`=0 connects both filters to their
signals and `Sel`=1 parks both on ground. The cap therefore integrates signal-for-half-a-cycle,
zero-for-half — which *is* the lock-in demodulation (half-wave mean), at half the amplitude of an
open-throw chopper. Intentional and correct; just don't be surprised by the factor of 2.

---

## Findings

### 1. RESOLVED in the 17:26 export — the VBAT_OK ladder

The 17:13 export had `R3-1, R4-1, R7-2, U-10` on one node with `R4-2, U-9` dangling. That put
OK_PROG at the R3/R7 tap with R4 stranded in a stub and OK_HYST connected to nothing else —
both thresholds would have collapsed to **1.609 V with zero hysteresis**, below the 1.8 V
`VSTOR_CHGEN`, so the LDO would have enabled permanently and the harvester would never have had
an accumulation window. The 17:26 export fixes it: R3-1 now sits with R4-2 on OK_HYST, and R4-1
with R7-2 on OK_PROG. **Ladder is correct as exported; thresholds are the 3.728 / 3.369 V pair.**

### 2. RESOLVED in the 18:48 export — `/OE` pull polarity

R21 and R23 now pull `SWAP_nOE` and `READ_nOE` **up to `3V3`**, so the undriven/reset state is
`/OE` HIGH = Disconnect: both electrodes floating, both chopper filters isolated. Firmware can
leave those pins undriven in sleep at ~zero current, instead of burning 33 µA per pin holding
them high against a pulldown. This is also what makes the architecture doc's OPEN reference and
sleep isolation free rather than a power trade.

R20 and R22 remain **pulldowns to GND** on the two `Sel` lines, which is correct — `Sel` is a
don't-care once `/OE` is high, and a pulldown costs nothing when firmware parks the pin low.

### 3. D1 is a blue LED on a 3.3 V rail — NOT A PROBLEM, confirmed in the field

**Settled 2026-09-17 by the maintainer: blue at 3.3 V with R10 = 470 Ω has been working fine.
Leave it. Do not re-raise this.**

Recorded because the datasheet numbers look alarming in isolation and will otherwise get
re-flagged by the next reader. `SM0805UBWC` is **V_F = 3.3 V typ, 4.2 V max** — but *at
I_F = 20 mA*, which is ~20× this circuit's operating point. An InGaN LED's V_F falls well below
its 20 mA figure at sub-mA currents, so the knee here is nearer 2.6–2.8 V and the part lights
from the ~0.5–1 mA the divider allows. nRF52840 standard drive guarantees I_OH,SD = 1 mA min
(2 mA typ) at VDD − 0.4 V (`nRF52840_PS_v1.1` §6.20.1), which is in the same range.

The concern was margin and part-to-part spread, not whether it would light at all. Bench
experience covers both, and it outranks the extrapolation.

### 4. RESOLVED in the 18:48 export — C9 depopulated

C9 (100 nF on `VBAT_DIV`) is now marked **DNP**, pad retained. That removes the 5 ms time
constant entirely: `VBAT_DIV_EN` can be pulsed and read almost immediately, with no reservoir
cap to charge.

**What it moves the requirement to:** with no cap, the SAADC sees the raw divider source
impedance, R5 ∥ R9 = **50 kΩ**. `nRF52840_PS_v1.1` Table 95 (§6.23.1.1) requires:

| TACQ | max source resistance |
|---:|---:|
| 3 µs | 10 kΩ |
| 5 µs | 40 kΩ |
| **10 µs** | **100 kΩ** |
| 40 µs | 800 kΩ |

So AIN0 needs **TACQ ≥ 10 µs**. Trivially available (the SAADC goes to 40 µs) and ~2500× faster
than waiting out the old RC. Good trade. If the reading ever looks low, that setting is the
first thing to check — the failure mode of an under-set TACQ looks exactly like a flat battery.

For reference, the other three channels: `TEMP` sees 5 kΩ (3 µs would do, and C16 buffers it
anyway); `SENSE_OUT` / `DRV_OUT` see 100 kΩ but each has its own 1 nF filter cap acting as the
reservoir.

### 5. The excitation pin (P0.07) must be set to high drive

Rs = R15 = **1 kΩ**, against an electrode |Z| at the chosen 1.000 MHz of ~1990 Ω dry → ~795 Ω wet
(architecture doc). That is a divider output of 0.44–0.66 of drive — a good choice of Rs — but it
means the pin sources **~1.1–1.65 mA peak**, against nRF52840 standard drive's **1 mA guaranteed**
(2 mA typ) at VDD − 0.4 V. In standard drive the GPIO's own output impedance becomes a
significant, VDD- and temperature-dependent term in the divider.

Set P0.07 to **high drive (H0H1: 3 mA at VDD ≥ 1.7 V, 5 mA at VDD ≥ 2.7 V)**. Module pin 22 is not
one of the E73's "low frequency I/O, standard drive" pins, so it supports this.

Mitigating factor, and a point in the design's favour: the drift monitor taps `DRV` at the pin
itself, *before* Rs, so GPIO droop is inside the monitored chain rather than outside it. Read at
quadrature it reports the real drive amplitude. High drive is still the right call — better to
not have the droop than to measure it.

### 6. Battery and panel attach by test point, not connector

`VBAT+` / `VBAT−` and `SP+` / `SP−` are all `Test Point-Footprint-1` — bare pads. MSV3 had a real
battery connector. If that is deliberate (hand-soldered leads on a potted probe), fine; flagging
it because it is the kind of thing that reads as an omission at assembly time.

### 7. Minor / informational

- **C16, C18, C19 are specified X7R, but the BOM doc asks for C0G** (`MSV4_PROBE_BOM_2026-09-16.md`
  line 185 lists C_g as "1 nF C0G", LCSC C1791). Electrically it does not matter for C16's RF-bypass
  job — 1 nF X7R is ~26 Ω at 6 MHz even after a 50 % bias/temperature droop, still three orders
  below the ~26 kΩ trace-to-electrode coupling. C18/C19 are averaging filter caps where DC value
  sets only the corner. Worth a deliberate decision rather than a silent divergence; the one
  real argument for C0G is that X7R is piezoelectric and C16 sits on a blade that flexes during
  insertion, on a high-impedance ADC node.
- **The BOM asks for 2× C_g; the electrode doc's final topology needs 1.** The netlist has one
  (C16), which matches the doc, not the BOM. Reconcile the BOM.
- **0805 load caps on a 32.768 kHz crystal** (C2/C3) are large for the job and lengthen the
  oscillator loop. The whole board is 0805, so this is presumably a deliberate hand-assembly
  choice; noting it only because crystal loops are the one place it costs something.
- **No bleed resistors on `MS_A` / `MS_B`.** With `/OE` high both electrodes float. Soil is
  conductive enough to bleed charge and the FSUSB42's V_SW range (−0.5 to 4.5 V) plus its
  power-off tolerance covers the rest, so this is low-risk — but a 10 M to GND on each would be
  free insurance and invisible to a 800–2000 Ω measurement.

### 8. `VBAT_DIV_EN` (P0.06) has no pull resistor — add a pulldown before fab (audit 2026-09-18)

The net is only `Q1-2, U1-14`. Firmware drives it low from `initVariant()` onwards, but the pin is
**floating** whenever firmware is not running: during reset, in the bootloader (which returns every
pin to `nrf_gpio_cfg_default`, input-disconnected), for the whole of a BLE DFU session, and in the
bring-up image's dead-crystal escape, where the bootloader waits in DFU with no timeout. A floating
N-FET gate with V_GS(th) 0.45–1.0 V can drift on, which pulls Q1's P-gate low and connects the
200 kΩ divider plus R1 (1 MΩ) across VBAT: roughly 20 µA against a 10–15 µA sleep budget, for as
long as the board sits there.

**Fix (value is the maintainer's call):** a pulldown from `VBAT_DIV_EN` to GND so the reset state is
defined. Pick it from GPIO and Q1 gate leakage versus V_GS(th); something in the 1-10 MΩ range costs
only µA during the ~2 ms reads. Note the undefined state may be worse than a clean 20 µA: a gate
half-on is indeterminate.
Every other control line is already defined without firmware: `SWAP_nOE`/`READ_nOE` pull up
(isolated), `SWAP_CTRL`/`READ_CNTL` pull down, and DRV/TEMP_READ floating is harmless with both
switches isolated.

### 9. Firmware contract — verified against rev3 (audit 2026-09-18). SUPERSEDED for the ADC pins by finding 10

`MSV4_Probe_Bringup_BLE.ino` pin constants match rev3 exactly: VBAT_DIV_EN P0.06, DRV P0.07,
TEMP_READ P0.12, SWAP_CTRL P0.13, SWAP_nOE P0.15, READ_CNTL P0.17, READ_nOE P0.20, LED P1.10,
AIN0 P0.02 `VBAT_DIV`, AIN1 P0.03 `SENSE_OUT`, AIN2 P0.04 `DRV_OUT`, AIN3 P0.05 `TEMP`. Crystal pins
P0.00/P0.01 are never touched. The two 1 MHz outputs (P0.07 module pin 22, P0.17 module pin 30) are
NOT on the E73's "low frequency I/O, standard drive" list; the only pins on that list that this board
uses are ADC inputs (P0.02, P0.03) and the LED (P1.10), which is correct.

### 10. The LAYOUT has moved on from this rev3 netlist (IPC-2581 audit, 2026-09-18 20:11 export)

Pad-by-pad comparison of `C:\Users\Public\Documents\Altium\MSV4\Project Outputs for MSV4\MSV4.cvg`
against `MSV4_sch_2026-09-17_rev3.NET` found 42 pin differences. The ones that matter:

| | rev3 netlist | layout (what gets built) |
|---|---|---|
| `SENSE_OUT` ADC | P0.03 / AIN1 (U1-3) | **P0.30 / AIN6 (U1-10)**, filter R18 + **C9** |
| `DRV_OUT` ADC | P0.04 / AIN2 (U1-18) | **P0.31 / AIN7 (U1-9)**, filter R19 + **C16** |
| `TEMP` ADC | P0.05 / AIN3 (U1-15) | **P0.03 / AIN1 (U1-3)**, RF bypass **C8** (placed at the blade top) |
| `VBAT_DIV_EN` | Q1 gate only | **+ R24 pulldown to GND** (finding 8, done) |
| VBAT decoupling | C1 + C8 | **C1 only** (C8 re-used as the TEMP bypass) — confirm intended |
| VBAT_DIV DNP cap | `C9 (DNP)` | designator is literally **`(DNP)`** — rename (e.g. C20) and mark DNP by variant/parameter, or the BOM and pick-and-place carry a part called "(DNP)" |
| test points | `3V3`, `GND`, `VBAT1` | `SW_3V3`, `SW_GND`; `VBAT1` removed |

`MSV4_Probe_Bringup_BLE.ino` was re-pinned to the layout the same day (AIN1 TEMP, AIN6 SENSE_OUT,
AIN7 DRV_OUT). P0.30/P0.31 are "analog input, low frequency I/O" on the E73, which suits DC reads.
**Export a fresh netlist** so this reference and the layout agree again; the component tables above
still describe rev3.

### 11. Layout audit findings (same IPC-2581 export)

- **14 floating GND copper pieces on the top layer** (no pad, no via): at approx. (96.7,151.4),
  (75.7,121.9), (78.3,125.2), (74.7,145.0), (77.2,124.1), (74.0,152.3), (79.1,126.4), (88.3,126.4),
  (84.8,143.0), (80.1,139.0), (91.2,149.6), (93.2,116.7), (94.3,125.7), (88.0,122.6) mm, board
  coordinates. Several sit beside DRV/SENSE and U3/U4. Enable *Remove Dead Copper* on the top
  polygon(s) or stitch each with a GND via. Every other top GND piece reaches the bottom plane.
- **Sense/feed clearance is the 0.15 mm polygon default everywhere**: MS_A/MS_B to GND 0.152 mm on
  both layers, 0.158 mm where the feeds enter the blade (y ≈ 111, the soil line), SENSE 0.147 mm.
  Fixed capacitance calibrates out, but a 0.15 mm gap puts much of the feed's field through the mask
  surface, where condensation and soil splash live. A net-class polygon clearance of ~0.5 mm on
  MS_A/MS_B (and SENSE where pin pitch allows) keeps it inside the laminate.
- **Top-layer GND under Y1** (about 1 mm² between the pads). The SC-32S sheet says no circuit
  pattern under the mounting area; add a top polygon cutout under the crystal body. Bottom-layer
  ground under it is fine.
- Only **one** GND via ties the pour between the A/B feeds (near (86.0,112.8)); add 2-3 more.
- Verified OK: guard ring continuous on both layers around the blade, blade clear of copper between
  and under the plates on both layers, NTC trace inside the guard to R17 at the tip, SWD pads
  RST/IO/CLK/SW_GND on a 2.54 mm pitch at x = 97.0, test points present on SNS, DRV, SNSO and DRVO,
  MS_A/MS_B track lengths matched within about 1 %.

### 12. Re-audit of the 2026-09-18 22:26 DRC + 22:29 IPC-2581 export (reviewed with Codex)

Fixed since finding 11: no floating GND (every top piece reaches the bottom plane), no top copper
under Y1, C8/C9/C16 now C0G in the BOM, DRC 0 violations (8 waived: Y1 silk, plate-track antennae).

Still open, in release order:
1. **U1 (E73) has no component record in the export** — no Component, Package or BOM line. All 43
   pads are present with identical coordinates and nets to the earlier export, so the COPPER is
   intact; the part identity is lost (exploded to free primitives, or a component type the exporter
   omits). Restore it as a real component before any BOM/placement/ECO use, then re-export and
   re-diff all 43 pads — restoring can duplicate or shift the existing free pads.
2. **The VBAT_DIV cap is designator `C`, in the X7R BOM group, with no DNP marking** → it would be
   fitted. Firmware assumes the VBAT divider has no cap (it reads 2 ms after enabling Q1; a 100 nF cap
   on the 50 kΩ source is a 5 ms time constant, ~33 % settled). Give it a numbered designator and
   mark it DNP in the BOM/variant, or tell firmware it is fitted.
3. Designators `U` (the BQ25504) and `-VBAT+` (leading minus) — clean up with #2.
4. Sub-rule gaps of 0.142–0.147 mm, all in the U3/U4/R18/R19 cluster (e.g. SENSE_OUT–DRV_OUT at
   (81.57,122.14)), while DRC reports none against a 0.152 mm rule. Above typical fab minimums
   (0.10–0.127 mm); check which rule is active at those points in Altium.
5. Feed/sense clearance still 0.15 mm (advisory, see finding 11); one stitching via between feeds.
6. Not visible in IPC copper data: solder mask over the plates and guard, exposed edge copper at the
   blade, castellation mask/paste, polarity marks. Confirm these are defined in the fab outputs.

E73 antenna end (y ≈ 154–158.25, to the board edge) has no pour on either layer; the E73 manual gives
no keepout dimensions beyond keeping the antenna exposed.

### 13. Final pre-fab check (2026-09-18 23:03 IPC-2581 + 23:02 netlist)

`netlist/MSV4_sch_2026-09-18_final.NET` **supersedes rev3** as the source of truth; the component
tables above still describe rev3 for anything finding 10 lists.
- Netlist vs layout: 176 pins each, **0 differences**.
- U1 restored as a real component (E73 package, in the BOM): 43 pads, 0.000 mm shift from the
  original placement, identical nets, no leftover free pads.
- GND fully connected; DRC 0 violations (8 waived: Y1 silk, plate-track antennae).
- Firmware pin map (`MSV4_Probe_Bringup_BLE.ino`) matches: TEMP U1-3 P0.03/AIN1, DRV_OUT U1-9
  P0.31/AIN7, SENSE_OUT U1-10 P0.30/AIN6, VBAT_DIV U1-7 P0.02/AIN0, VBAT_DIV_EN U1-14 P0.06,
  TEMP_READ P0.12, DRV P0.07, SWAP_nOE P0.15, READ_CNTL P0.17, READ_nOE P0.20, SWAP_CTRL P0.13,
  LED P1.10.
- Remaining, BOM/labels only: the VBAT_DIV cap (100 nF X7R) has designator `DNP` but is still a
  fitted line item (qty 8 on the X7R row). Fine for hand assembly (do not place it); mark it
  not-fitted if a BOM is ever used for purchasing/assembly. The BQ25504 is still designator `U`.

### 14. FUTURE REVISION CANDIDATE — power-gate U3/U4 during sleep (2026-09-20)

The present board is acceptable as built: asserting both FSUSB42 `/OE` inputs HIGH disconnects
their analog channels, and each part specifies at most 1 µA quiescent supply current when its
control inputs are at valid rails. U3 + U4 therefore add at most about **2 µA**, or
**0.048 mAh/day**, while powered but disabled. Do not rework the first boards solely for this.

For a future PCB revision, consider feeding U3 and U4 from one GPIO-controlled **high-side power
gate** (`SW_AFE_3V3`) instead of directly from `3V3`. A P-channel MOSFET or load switch with
guaranteed off leakage well below 1 µA over the required temperature range could recover up to
about 2 µA. This is a modest whole-probe energy improvement, but material against the intended
10–15 µA sleep-current budget. Select from guaranteed maximum leakage, including temperature,
rather than a room-temperature typical value, and include the gate/load-switch current in the
comparison.

This change is safe only if the schematic and firmware implement all of the following:

1. Move R21 and R23 (`SWAP_nOE` and `READ_nOE`) from permanent `3V3` to `SW_AFE_3V3`. Leaving a
   control input at 3.3 V while the FSUSB42 VCC is 0 V violates its specified control-input range
   and can back-power the part. R20/R22 `Sel` pulldowns may remain on GND.
2. Give the power-gate enable a hardware default-OFF state during reset and boot. Keep all four
   switch-control GPIOs high-impedance while `SW_AFE_3V3` is off.
3. Sleep sequence: set both `/OE` inputs HIGH while power is present, park `DRV` LOW, release the
   control GPIOs, then remove `SW_AFE_3V3`.
4. Wake sequence: keep `DRV` LOW and the control GPIOs released, enable `SW_AFE_3V3`, wait for its
   rail/decoupling to settle, then select the paths and drive `/OE` LOW.
5. The FSUSB42 signal ports are power-off tolerant, but still specify up to ±2 µA `IOFF` per port
   when an external voltage is present. Parking `DRV` LOW and discharging the filter nodes avoids
   providing a voltage that can drive this leakage. Power gating removes VCC quiescent current; it
   does not guarantee zero analog-port leakage.

Verify the revision by measuring whole-board sleep current both with the AFE rail forced on and
with it gated off. The measured difference, not the 2 µA datasheet ceiling, is the real benefit.
