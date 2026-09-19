# MSV4 probe BOM — for Altium (2026-09-16)

**Status: part selection settled enough to draw. Prices verified where marked; LCSC stock must be
confirmed at order time.** Supersedes the sensing half of `MSV4_PROBE_BOM_DRAFT.md` (2026-09-02).

> **2026-09-17 decisions (maintainer):** **3.3 V rail (TPS7A0233)** — unchanged from MSV3, because the
> whole architecture was validated at 3.3 V and there is no reason to revalidate it. The maintainer's stock of 60 mAh
> cells, `VBAT_OV` = 3.9 V from a ~10 MΩ divider, and a single 2 V / 60 mA / 0.12 W / 36 x 30 mm
> panel (ordered on Amazon; not yet bench-qualified). Every orderable part below was checked
> against LCSC's live product API on 2026-09-17 — see [LCSC stock check](#lcsc-stock-check-2026-09-17).
> Where this banner and older text below disagree, the tables are current.

Scope, per the maintainer: nRF BLE module with attached antenna, the same BQ25504 solar charger, the
lock-in sensing front end, and a soil temperature sensor. Nothing else. No USB, no FDC, no 555.

Architecture references — read these before drawing, they contain the constraints:

- `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md` — blade geometry, guard, NTC routing
- `MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` — lock-in topology, TIMER/PPI/GPIOTE, switches
- `MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md` — the panel spec, and why it matters
- `MSV4/docs/MSV4_SOLAR_PANEL_SELECTION_2026-09-16.md` — measured recharge budget, panel-array cost
  comparison and the small-batch selection

## First: where the $50 actually is

Before optimising the BOM, know what it can move. Taking the 09-02 draft's own figure of ~$16.5 of
electronics against a ~$50 finished board:

```
  ~$16.5   parts on this BOM        <- the only thing a BOM change can touch
  ~$33.5   PCB fab, assembly, shipping, solar panel, battery, enclosure
```

**Two thirds of the cost is not on this BOM.** A perfect BOM optimisation caps out at about a third of
the board price. That does not make the BOM work pointless — the numbers below cut it hard — but the
biggest levers are almost certainly elsewhere:

- **Quantity.** Assembly setup, stencil and per-unique-part loading fees are fixed per order, so they
  amortise steeply. At five boards they dominate; at thirty they are noise.
- **Unique part count, and stocked vs non-stocked parts.** Most assemblers charge a per-BOM-line fee
  for parts they do not keep loaded. Consolidating passive values and preferring stocked parts can
  save more than the MCU choice does.
- **Board area.** The blade is 18 x 110 mm — long. Check where the fab's price tiers fall before
  fixing the outline.

**Open question for the maintainer: what is the actual breakdown of the ~$50?** Fab, assembly,
shipping, panel, battery, enclosure — separately. Everything above is guesswork about priorities until
that exists.

## The savings are already banked, and not where you would expect

Verified unit prices from this project's own LCSC orders (`MSV3/procurement/`, 2026-07-31), so these
are what was really paid, not a quote:

| removed from MSV3 | LCSC | unit price | note |
|---|---|---:|---|
| **FDC2214RGHR** | C913845 | **$6.70** | the single most expensive cut |
| BQ25170DSGR (USB charger) | C5208201 | $0.8243 | USB dropped |
| USBLC6-2P6 (USB ESD) | C15999 | $0.2779 | USB dropped |
| PMEG2010AET (source select) | C552819 | $0.2387 | USB dropped |
| TYPE-C-31-M-12 | C165948 | $0.1857 | USB dropped |
| 74HC4052D | C11350 | $0.0801 | replaced by the switches below |
| LVS404018-6R8M-N (tank L) | C142128 | $0.0574 | no LC tank |
| | | **−$8.37** | |

Against roughly **+$1.2 to +$1.7** of new parts (two signal switches, an NTC, a few passives).

**Net: about $7 out of a ~$16.5 electronics BOM — roughly 42 %, before any MCU decision.** The FDC
removal alone is worth more than twice what downgrading the MCU could save.

## MCU: recommend staying on the nRF52840 for the first V4 build

The maintainer's constraint is "an nRF, cheap, antenna attached, in stock at LCSC" — the E73 is not
required. Verified price paid: **E73-2G4M08S1C, C356849, $7.5888, SMD 18 x 13 mm.**

The obvious downgrade is the nRF52832 in the same family — **E73-2G4M04S1B, C411306**, 512 KB flash /
64 KB RAM, integrated PCB antenna, same vendor and footprint family.

> **Price not verified.** LCSC blocks automated fetches and returned 404 on every product URL this
> session. A search summary quoted ~$4.59, which is exactly the kind of number `datasheet-lookup`'s
> rule says must not go into a BOM unverified. **Confirm on the LCSC page before ordering.** If it
> holds, the saving is about **$3.00**.

**Recommendation: keep the nRF52840 / E73-2G4M08S1C for V4.0, and revisit on V4.1.** Three reasons:

1. **$3 is 6 % of a $50 board**, against $6.70 already saved by removing the FDC. It is not where the
   money is.
2. **It costs the OTA chain.** This project's BLE DFU infrastructure — `fleet.json` registry gating,
   signed packages, identity and rollback checks, the Guard-2 counter paired to bootloader 1.0.3 — is
   built and validated against the nRF52840 Adafruit bootloader. A different silicon variant means a
   different SoftDevice (S132), a different bootloader build, and revalidating all of it. That is the
   most safety-critical tooling in the repo and it was expensive to get right.
3. **V4 already changes three big things at once** — electrode geometry, front-end topology and the
   solar panel. Adding a fourth makes a failure hard to attribute. This is the same discipline the
   project already applies elsewhere: sensor 3 held as an untouched control, receiver-first rollout
   order.

**If the MCU is changed anyway**, the requirement is: integrated antenna, at least ~14 free GPIO (see
the map below), SWD accessible, and an Adafruit-lineage bootloader the existing DFU scripts can be
pointed at. Budget the firmware port as real work, not a footprint swap.

### Add the 32.768 kHz crystal this time

MSV3's PCB has no LFXO and uses `P0.00`/`P0.01` for other things. That is the direct cause of
`MSV3_LFCLK_RTC1_HANG_2026-08-21.md`, the compile-time LFCLK guards, and the whole
`arduino_msv3_probe.ps1` variant-repair dance. **A 32.768 kHz crystal plus two load capacitors is
about $0.15 and retires that entire class of problem.**

It also *saves* power, which matters more here than the cost: the internal LFRC has to be periodically
recalibrated against the HFCLK, and those wakeups are pure overhead against a 10–15 µA sleep budget.
LFXO does not need them.

**Keep `P0.00`/`P0.01` free for it.** This is a layout constraint, so decide it before routing.

## BOM

Prices marked **(v)** are verified from this project's own LCSC orders. Everything else needs a price
and a stock check at order time.

### Power

| Ref | Qty | Part | LCSC | Package | Unit | Note |
|---|--:|---|---|---|--:|---|
| U2 | 1 | BQ25504RGTT | C157471 | VQFN-16-EP 3x3 | $5.7464 **(v)** | keep — 330 nA I_Q, operates from 130 mV |
| U3 | 1 | **TPS7A0233PDBVR** (3.3 V) | C2887324 | SOT-23-5 | $0.5665 **(v)** | same part as MSV3. 25-50 nA I_Q, 200 mA, 270 mV dropout at 200 mA (~20 mV at the nRF's ~15 mA) |
| L1 | 1 | LPS4018-223MRC | C5278935 | 3.9x3.9 | $1.2026 **(v)** | 22 µH boost inductor; price has doubled since the 07-31 order ($0.5656) |
| Q1 | 1 | NTR1P02T1G | C129192 | SOT-23 | $0.1756 **(v)** | VSTOR-to-load isolation PFET; MOQ 5 |
| Q2 | 1 | 2N7002 | C916396 | SOT-23 | $0.0112 **(v)** | inverts `VBAT_OK` to drive Q1; MOQ 50 |
| J1 | 1 | S2B-PH-K-S | C173752 | TH right-angle | $0.0399 **(v)** | battery — **must match the connector and polarity already on the 60 mAh cells** |
| J2 | 1 | solder pads | — | — | — | panel |
| PV1 | 1 | **2 V / 60 mA / 0.12 W, 36 x 30 mm** poly panel | — | — | ~$1-2 | **ordered 09-17 (Amazon)**; qualify before fitting — see panel section |
| BT1 | 1 | **EHAO 301020**, 3.7 V 1S LiPo, **60 mAh** | eBay | — | **$1.444** (10 for $14.44) | maintainer's stock; confirm protection board, connector/polarity, and ≥30 mA charge rating |

### BQ25504 threshold network — decided 2026-09-17

Equations from the local datasheet copy
(`MSV3/hardware/MSV3_Probe_PCB/reference/datasheets/BQ25504_extracted.txt`, rev G), with
`VBIAS` = 1.25 V typical (1.21–1.27 V). Each string must total **9–11 MΩ** (Recommended Operating
Conditions). "Top" connects to VRDIV/VSTOR side, "bottom" to GND, matching TI's naming in which
`R_x1` is the bottom resistor.

| string | bottom | top | third | nominal | VBIAS 1.21–1.27 V | sum |
|---|---|---|---|---:|---:|---:|
| `VBAT_OV = 1.5·VBIAS·(1 + ROV2/ROV1)` | ROV1 **4.99 MΩ** C2930334 | ROV2 **5.36 MΩ** C422723 | — | **3.889 V** | 3.765–3.951 V | 10.35 MΩ |
| `VBAT_UV = VBIAS·(1 + RUV2/RUV1)` | RUV1 **4.12 MΩ** C367866 | RUV2 **5.90 MΩ** C313940 | — | 3.040 V | 2.94–3.09 V | 10.02 MΩ |
| `VBAT_OK = VBIAS·(1 + ROK2/ROK1)` (falling, load off) | ROK1 **3.48 MΩ** C332846 | ROK2 **5.90 MΩ** C313940 | ROK3 **1.00 MΩ** C17514 | 3.369 V | 3.26–3.42 V | 10.38 MΩ |
| `VBAT_OK_HYST = VBIAS·(1 + (ROK2+ROK3)/ROK1)` (rising, load on) | | | | 3.728 V | 3.61–3.79 V | |

- The OV pair was chosen over the closer-sum 4.87 MΩ + 5.23 MΩ because LCSC had only 90 and 17 of
  those on 09-17, and 4.99/5.36 gives the same 3.889 V from stocked parts.
- The `VBAT_OK` set is sized for the **3.3 V** rail: load-off 3.369 V keeps the TPS7A0233 out of
  dropout (~20 mV at the nRF's peak current), and load-on 3.728 V stays below `VBAT_OV`. 3.32 MΩ
  would have given a rounder 3.37/3.74 but is **0 stock** at LCSC; 3.48/5.90/1.00 are all stocked,
  and 5.90 MΩ is already the `RUV2` value.
- `VBAT_UV` < load-off < load-on < `VBAT_OV` holds at nominal. `VBIAS` shifts all four together, so
  their ordering survives its tolerance.
- MSV3's `RUV` naming was swapped in the 09-02 draft (it listed 5.90 MΩ as RUV1). The
  3.04 V result requires 5.90 MΩ on **top**.
- **Capacity consequence of the 3.3 V rail plus 3.9 V OV:** the usable window is 3.37–3.89 V, about
  30 mAh of the 60 mAh cell, roughly 10 days at the 3.03 mAh/day load with no sun. A 3.0 V rail
  would have widened this; it was rejected deliberately to keep the validated 3.3 V architecture.
- `RQ1` (Q1 gate pull-up) 10 MΩ C844596; MPPT network unchanged from MSV3 (4.42 MΩ C332857 /
  10 MΩ C844596 / 5.6 MΩ C2933468, 77.9 %).

### Decoupling (LCSC numbers from the MSV3 order, stock re-checked 09-17)

| value | LCSC | use |
|---|---|---|
| 10 µF 16 V X5R 0805 | C1713 | solar input bulk, VBAT bulk, MCU bulk |
| 4.7 µF 25 V X5R 0805 | C1779 | second solar-input cap, VSTOR |
| 10 nF C0G 0805 | C97905 | `VREF_SAMP` |
| 100 nF X7R 0805 | C49678 | VSTOR HF, E73 bypass, battery-ADC settle |
| 1 µF X7R 0805 | C28323 | TPS7A0230 in/out |

### Sensing front end

| Ref | Qty | Part | Package | Unit | Role |
|---|--:|---|---|--:|---|
| U4 | 1 | TS3USB221ARSER | UQFN-10 1.5x2 | $0.2638 **(v)** C128396 | A/B electrode swap, one select pin; `OE` = OPEN reference |
| U5 | 1 | TS3USB221ARSER | UQFN-10 1.5x2 | $0.2638 **(v)** C128396 | chopper (ch1) + drive-node drift monitor (ch2) |
| Rs | 1 | value set at commissioning | 0805 | <$0.01 | divider series R — from measured &#124;Z&#124; at 1 MHz |
| R, C | 2+2 | 100 kΩ C149504 + 1 nF C0G C1791 | 0805 | <$0.05 | post-chopper averaging filters |
| RT1 | 1 | NCP15XH103F03RC, 10 kΩ, **B25/50 3380 K** | 0402 | $0.0266 **(v)** C77131 | soil temperature at the blade tip; MOQ 50 |
| R_ref | 1 | 10 kΩ 1 % | 0805 | C17414 (basic) | NTC divider reference |
| C_g | 2 | 1 nF C0G | 0805 | C1791 | **RF-bypass NTC traces to guard** — see electrode doc |

> **TS3USB221A stock verified 09-17: 58,970 at LCSC.** Only the TI `RSER` UQFN-10 is listed; its
> supply range is 2.3–3.6 V, so the 3.0 V rail is inside it. The `-HXY` clone (C49108275) is a
> different part with different specs — do not substitute it without re-running the front-end
> doc's low-`C_off` check. Do not substitute a "precision analog mux" — that is how the TMUX1109's
> 38 pF got into MSV3.

### MCU and housekeeping

| Ref | Qty | Part | LCSC | Unit | Note |
|---|--:|---|---|--:|---|
| U1 | 1 | E73-2G4M08S1C | C356849 | $7.5888 paid 07-31; **see note** | nRF52840 + PCB antenna, 18 x 13 mm |
| Y1 | 1 | Q13FC13500004, 32.768 kHz, 12.5 pF, ±20 ppm | C32346 (basic) | $0.1734 **(v)** | **new — see above**; 12.5 pF is the nRF52840 LFXO maximum |
| C_y | 2 | 22 pF C0G | C1804 (basic) | $0.0123 **(v)** | 2 x (12.5 pF − ~1.5 pF stray); trim after measuring LFXO frequency |
| R_bat | 2 | 100 kΩ 1 % | C149504 (basic) | $0.006 **(v)** | 1:2 battery ADC divider, fed from VBAT through Q3 — see note below |
| C_bat | 1 | 100 nF | C49678 | $0.0196 **(v)** | divider settling; keeps MSV3's 25 ms read delay |
| Q3 | 1 | NTZD3155CT1G (N+P dual) | C2940634 | SOT-563 | $0.1096 **(v)** | high-side divider switch + its gate driver in one part |
| R_q3 | 1 | 1 MΩ 1 % | C17514 | 0805 | $0.0044 **(v)** | P-gate pull-up; also the ROK3 value |
| R_q3g | 1 | 100 kΩ 1 % | C149504 | 0805 | $0.006 **(v)** | optional N-gate pull-down |
| D_led | 1 | NCD0805R1 red + 2.2 kΩ | C84256 + C17520 (both basic) | $0.0135 + $0.005 **(v)** | diagnostics; low-V_f red, runs from 3.0 V |
| TP | 6 | bare pads | — | — | SWDIO, SWDCLK, RESET, VSTOR, 3V3, one front-end node |
| U_clamp | 1 | ATL431BIDBZR + 2 resistors | C701718 | $0.4418 **(v)** | **DNP** — insurance only. Replaces the earlier "3.3 V zener" line: the 09-16 Probe 7 reassessment showed an ordinary zener is the wrong part here |

> **E73 sourcing (09-17).** Bought from LCSC on 2026-07-31 (order WM2607270222, 5 @ $7.5888), but
> LCSC's own storefront and product API no longer return C356849. **JLCPCB's assembly parts library
> still stocks it: 1,980 on 09-17, $7.64 (1-9), $7.48 (10-15), $7.06 (18+)**, as an extended part
> sourced from LCSC. **But JLCPCB parts are only for JLCPCB assembly; they are not shipped loose**,
> and V4 is hand-assembled. **Buy loose modules from Ebyte's own store instead:**
> <https://ebyteiot.com/products/2-4ghz-ble-mesh-small-smd-e73-2g4m08s1c-nordic-nrf52840-module-small-size-ble-5-0>,
> **$6.54** on 09-17 (free China Post shipping over $89.99, 7-15 working days). Pick the `S1C`
> (nRF52840, ceramic antenna) variant, not `S1E` (nRF52833) or either `X` (IPEX) variant.
> **Do not substitute E73-2G4M08S1CX (C2764963):** its listing describes an **IPEX antenna
> connector**, not the integrated PCB antenna this design and enclosure assume.

> **Battery divider (decided 2026-09-17).** High-side switched from **VBAT** with one
> **NTZD3155C** complementary N+P dual (SOT-563), replacing MSV3's low-side GPIO enable:
> P-channel source to VBAT, drain to the 100k/100k top; 1 MΩ from the P gate to VBAT holds it off;
> the N-channel pulls that gate to GND when `VBAT_DIV_EN` is **HIGH**. Optional 100 kΩ from the N
> gate to GND keeps it off while the MCU is unpowered.
>
> This is **active-high**, the opposite of MSV3's `readVbatMv()`, which drives the enable LOW.
> Firmware must be updated. It also removes MSV3's out-of-spec idle condition: with the low-side
> enable the ADC pin sat at VBAT (above the nRF's VDD+0.3 V limit) between reads, and with V4's new
> Q1 load switch it would also have back-powered the dead rail through the pin's protection diode
> (~27 µA) in the cold-start and below-`VBAT_OK` states. With the high-side switch off, the bottom
> 100 kΩ pulls the ADC node to 0 V, so VBAT (rather than a switched rail) is the correct source.
> The P-channel body diode points from the divider back to VBAT and blocks when off.
>
> Pinout verified on the **T1G** datasheet (C2940634, SOT-563: 1=S1, 2=G1, 6=D1 N-channel;
> 3=D2, 4=S2, 5=G2 P-channel). The schematic currently calls out **T2G** (C895156, 2,028 stock,
> $0.149) — either confirm that variant's pinout or move to T1G (5,220 stock, $0.110).

## GPIO map — check against the module's broken-out pins before routing

| function | type | note |
|---|---|---|
| excitation out | GPIO via GPIOTE | from TIMER CC, PPI-driven |
| chopper select | GPIO via GPIOTE | same TIMER, phase offset |
| A/B select (U4 `S`) | GPIO | toggles once per 10 s |
| U4 `OE` | GPIO | floats both electrodes — OPEN reference, and sleep isolation |
| U5 `OE` | GPIO | |
| measurement | AIN | post-chopper RC |
| drift monitor | AIN | **lay out, do not populate initially** |
| NTC drive | GPIO | pulse-read only — self-heat is 0.14 °C if left on |
| NTC sense | AIN | ratiometric |
| `VBAT_DIV_EN` | GPIO | **active HIGH** (drives Q3's N gate); MSV3's LOW-to-read firmware must be flipped |
| VBAT sense | AIN | divider fed from VBAT through Q3 |
| LED | GPIO | |
| SWDIO / SWDCLK / RESET | pads | |
| `P0.00` / `P0.01` | **reserved** | 32.768 kHz crystal — do not use for anything else |

Roughly 12 GPIO plus 4 analog plus SWD. Confirm against the chosen module's pinout — this is the
number that disqualifies an over-small module.

## The panel is a hard electrical spec now, not a shopping preference

**Production `V_OC` ≤ 2.7 V across the specified cold-temperature range.** Full safety reasoning is
in `MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`; the measured recharge and sourcing decision is
in `MSV4_SOLAR_PANEL_SELECTION_2026-09-16.md`.
Short version: the BQ25504's `V_IN(DC)` recommended maximum is 3000 mV, panel `V_OC` rises about
0.3 %/°C as temperature falls, and MSV3's 6 V panel is the root cause of every overcharged cell in the
fleet. A panel that violates this makes `VBAT_OV` inoperative no matter what the resistors say.

Fewer cells in series at the same die area gives the same power at a legal voltage, so **this costs
nothing** — and MPPT then tracks the real maximum power point instead of a clamp, which is worth
roughly 40 % more harvested power than a clamped V3.

**2026-09-17: the V4 panel is one 2 V / 60 mA / 0.12 W, 36 x 30 mm polycrystalline panel**
(ordered on Amazon; the uxcell/MECCANIXITY listing family), replacing the two-panel 1.5 V parallel
array. It fits the existing 30 x 55 mm envelope with 19 mm to spare, so the carrier no longer needs
to grow to 60.4 mm. It has ~68 % of V3's panel area, so expect ~68–80 % of V3's *unclamped*
harvest — roughly 5.5–7 mAh/day gross at Probe 4's window, 2.5–4 mAh/day net — and about
1.5–1.7x what a zener-clamped V3 gets. A 4-cell 2 V poly panel should read about 2.4 V open-circuit
in sun (estimate; the listing gives no `V_OC`). Peak charge current is about 25–30 mA.

**Acceptance test for each panel before fitting**, in direct sun, disconnected from any board:

1. `V_OC` ≤ 2.8 V, else reject.
2. `I_SC` near 60 mA, and at the deployment window at least ~1.7x the V3 panel's `I_SC` measured
   side-by-side.

**Record the final panel's listing/ASIN, measured `V_OC`/`I_SC`, dimensions and price.** It remains
the single most safety-relevant sourcing item.

## LCSC stock check (2026-09-17)

Live stock from LCSC's product API (`wmsc.lcsc.com/ftps/wm/product/detail`). Quantities move daily;
re-check at order time. "MOQ" is LCSC's minimum buy.

| part | LCSC | stock | unit $ (qty) | MOQ | flag |
|---|---|---:|---|---:|---|
| E73-2G4M08S1C | C356849 | **JLCPCB only: 1,980** | 7.6421 (1) / 7.4842 (10) / 7.0628 (18) | 1 | gone from LCSC storefront; JLCPCB is assembly-only — buy loose from Ebyte's store, $6.54 |
| BQ25504RGTT | C157471 | 319 | 5.7464 (1) / 4.9212 (10) | 1 | stock is thin — buy the batch now |
| **TPS7A0233PDBVR** | C2887324 | 9,094 | 0.5665 / 0.4795 | 1 | 3.3 V, decided 09-17 |
| NTZD3155CT1G (N+P dual) | C2940634 | 5,220 | 0.1356 / 0.1096 | 5 | divider high-side switch |
| LPS4018-223MRC | C5278935 | 4,258 | 1.2026 / 1.0202 | 1 | price up 2x since July |
| NTR1P02T1G | C129192 | 2,190 | 0.1756 | 5 | |
| 2N7002 | C916396 | 910,150 | 0.0112 | 50 | |
| S2B-PH-K-S | C173752 | 170,960 | 0.0399 | 20 | only if the cells use JST-PH |
| TS3USB221ARSER | C128396 | 58,970 | 0.2638 / 0.2087 | 5 | |
| NCP15XH103F03RC | C77131 | 296,350 | 0.0266 | 50 | |
| Q13FC13500004 | C32346 | 507,190 | 0.1734 | 5 | basic |
| ATL431BIDBZR (DNP) | C701718 | 9,940 | 0.4418 | 1 | |
| 22 pF C0G | C1804 | 639,700 | 0.0123 | 50 | basic |
| 1 nF C0G | C1791 | 110,410 | 0.0121 | 10 | |
| 10 nF C0G | C97905 | 6,150 | 0.0684 | 10 | |
| 100 nF X7R | C49678 | 9,077,840 | 0.0196 | 20 | |
| 1 µF X7R | C28323 | 869,070 | 0.0403 | 10 | |
| 4.7 µF X5R | C1779 | 1,977,680 | 0.0348 | 20 | |
| 10 µF X5R | C1713 | 487,720 | 0.0637 | 20 | |
| 4.99 MΩ | C2930334 | 6,800 | 0.0041 | 100 | ROV1 |
| 5.36 MΩ | C422723 | 1,100 | 0.0022 | 100 | ROV2 |
| 3.48 MΩ | C332846 | 3,800 | 0.0019 | 100 | ROK1 (C422746 was **0 stock**; 3.32 MΩ also 0 stock) |
| 1.00 MΩ | C17514 | 1,850,700 | 0.0044 | 100 | ROK3 + Q3 gate pull-up |
| 5.90 MΩ | C313940 | 4,720 | 0.0213 | 20 | RUV2 + ROK2 (1.13 MΩ no longer used) |
| 4.12 MΩ | C367866 | 600 | 0.0029 | 100 | RUV1 |
| 4.42 MΩ | C332857 | 4,100 | 0.0019 | 100 | MPPT top |
| 10 MΩ | C844596 | 12,800 | 0.0135 | 50 | MPPT, RQ1 |
| 5.6 MΩ | C2933468 | 17,600 | 0.0038 | 100 | MPPT |
| 100 kΩ | C149504 | 3,560,800 | 0.006 | 100 | basic |
| 10 kΩ | C17414 | 46,323,500 | 0.0039 | 100 | basic |
| 2.2 kΩ | C17520 | 3,111,600 | 0.005 | 100 | basic |
| red LED 0805 | C84256 | 4,796,700 | 0.0135 | 50 | basic |

## Before this is orderable

1. **The ~$50 breakdown** — fab, assembly, shipping, panel, battery, enclosure, separately.
2. **2 V panel bench-qualified** (acceptance test above) when it arrives.
3. ~~TS3USB221A price and LCSC stock~~ — done 09-17.
4. **E73-2G4M08S1C** — buy loose from Ebyte's own store ($6.54); LCSC no longer lists it and JLCPCB is assembly-only.
5. **Battery connector and polarity** from the 60 mAh cells in hand, and the cell datasheet's
   maximum charge current (≥ 30 mA). Capacity decided: 60 mAh.
6. ~~3.0 V vs 3.3 V rail~~ — **3.3 V (TPS7A0233)**, and the full threshold network, decided 09-17.
7. **Rs value** — cannot be picked until &#124;Z&#124; is measured on a real blade at the chosen
   frequency. Leave it a placeholder and set it at commissioning.
