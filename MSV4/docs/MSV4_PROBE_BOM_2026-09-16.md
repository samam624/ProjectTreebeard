# MSV4 probe BOM — for Altium (2026-09-16)

**Status: part selection settled enough to draw. Prices verified where marked; LCSC stock must be
confirmed at order time.** Supersedes the sensing half of `MSV4_PROBE_BOM_DRAFT.md` (2026-09-02).

Scope, per the maintainer: nRF BLE module with attached antenna, the same BQ25504 solar charger, the
lock-in sensing front end, and a soil temperature sensor. Nothing else. No USB, no FDC, no 555.

Architecture references — read these before drawing, they contain the constraints:

- `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md` — blade geometry, guard, NTC routing
- `MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md` — lock-in topology, TIMER/PPI/GPIOTE, switches
- `MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md` — the panel spec, and why it matters

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
| U2 | 1 | BQ25504RGTT | C157471 | VQFN-16-EP 3x3 | $5.8501 **(v)** | keep — 330 nA I_Q, operates from 130 mV |
| U3 | 1 | TPS7A0233PDBVR | C2887324 | SOT-23-5 | $0.7263 **(v)** | or TPS7A0230 (3.0 V) — see draft |
| L1 | 1 | LPS4018-223MRC | C5278935 | 4.0x4.0x1.7 | $0.5656 **(v)** | 22 µH boost inductor |
| Q1 | 1 | NTR1P02T1G | C129192 | SOT-23 | $0.1711 **(v)** | VSTOR-to-load isolation PFET |
| Q2 | 1 | 2N7002 | — | SOT-23 | ~$0.02 | inverts `VBAT_OK` to drive Q1 |
| J1 | 1 | S2B-PH-K-S | C173752 | TH right-angle | $0.0393 **(v)** | battery |
| J2 | 1 | solder pads | — | — | — | panel; decide removable vs soldered |
| PV1 | 1 | **panel, `V_OC` ≤ 2.7 V @ 25 °C** | — | — | TBD | **hard spec — see below** |
| BT1 | 1 | protected 1S LiPo | — | — | TBD | 150 mAh gives ~50 days with no sun |

BQ25504 resistor network, `VBAT_OK` thresholds and decoupling: carry over from
`MSV4_PROBE_BOM_DRAFT.md`, with **one correction** — the OV divider sum must be **9–11 MΩ** (page 6
Recommended Operating Conditions, a spec with a *minimum*). MSV3's reworked 8.54 MΩ is out of spec.
Use a compliant pair on V4.

### Sensing front end

| Ref | Qty | Part | Package | Unit | Role |
|---|--:|---|---|--:|---|
| U4 | 1 | TS3USB221A | US8/QFN | ~$0.60 | A/B electrode swap, one select pin; `OE` = OPEN reference |
| U5 | 1 | TS3USB221A | US8/QFN | ~$0.60 | chopper (ch1) + drive-node drift monitor (ch2) |
| Rs | 1 | value set at commissioning | 0805 | <$0.01 | divider series R — from measured &#124;Z&#124; at 1 MHz |
| R, C | 2+2 | 100 kΩ + 1 nF | 0805 | <$0.05 | post-chopper averaging filters |
| RT1 | 1 | NTC 0402, R25 10 kΩ, **B25/50 3380 K** | 0402 | ~$0.05 | soil temperature at the blade tip |
| R_ref | 1 | 10 kΩ 1 % | 0805 | <$0.01 | NTC divider reference |
| C_g | 2 | 1 nF | 0805 | <$0.02 | **RF-bypass NTC traces to guard** — see electrode doc |

> **TS3USB221A price and LCSC stock are unverified.** The front-end doc verified its *electricals*
> only and flagged this as open. Confirm both before committing the footprint. If it is not stocked,
> the selection rule is in that doc: **shop USB/HDMI/DisplayPort signal switches for low `C_off`, and
> deliberately accept higher `R_on`.** Do not substitute a "precision analog mux" — that is how the
> TMUX1109's 38 pF got into MSV3.

### MCU and housekeeping

| Ref | Qty | Part | LCSC | Unit | Note |
|---|--:|---|---|--:|---|
| U1 | 1 | E73-2G4M08S1C | C356849 | $7.5888 **(v)** | nRF52840 + PCB antenna, 18 x 13 mm |
| Y1 | 1 | 32.768 kHz crystal | — | ~$0.10 | **new — see above** |
| C_y | 2 | load caps per crystal spec | 0805 | <$0.02 | |
| R_bat | 2 | 100 kΩ 1 % | 0805 | <$0.01 | GPIO-gated 1:2 battery ADC divider |
| C_bat | 1 | 100 nF | 0805 | <$0.04 | divider settling |
| D_led | 1 | LED + 2.2 kΩ | 0805 | ~$0.04 | diagnostics; prefer low-V_f red over blue |
| TP | 6 | bare pads | — | — | SWDIO, SWDCLK, RESET, VSTOR, 3V3, one front-end node |
| D_z | 1 | 3.3 V zener, SOD-123 | — | ~$0.03 | **DNP** — insurance only, if the panel spec holds |

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
| `VBAT_DIV_EN` | GPIO | existing MSV3 pattern |
| VBAT sense | AIN | |
| LED | GPIO | |
| SWDIO / SWDCLK / RESET | pads | |
| `P0.00` / `P0.01` | **reserved** | 32.768 kHz crystal — do not use for anything else |

Roughly 12 GPIO plus 4 analog plus SWD. Confirm against the chosen module's pinout — this is the
number that disqualifies an over-small module.

## The panel is a hard electrical spec now, not a shopping preference

**`V_OC` ≤ 2.7 V at 25 °C.** Full reasoning in `MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`.
Short version: the BQ25504's `V_IN(DC)` recommended maximum is 3000 mV, panel `V_OC` rises about
0.3 %/°C as temperature falls, and MSV3's 6 V panel is the root cause of every overcharged cell in the
fleet. A panel that violates this makes `VBAT_OV` inoperative no matter what the resistors say.

Fewer cells in series at the same die area gives the same power at a legal voltage, so **this costs
nothing** — and MPPT then tracks the real maximum power point instead of a clamp, which is worth
roughly 40 % more harvested power than a clamped V3.

**Record the panel's MPN, dimensions, connector, specified maximum `V_OC` and price.** The 09-02 draft
listed this as unknown; it is now the single most safety-relevant unknown on the board.

## Before this is orderable

1. **The ~$50 breakdown** — fab, assembly, shipping, panel, battery, enclosure, separately.
2. **Panel sourced against the ≤ 2.7 V spec**, with a datasheet.
3. **TS3USB221A price and LCSC stock**, or a substitute chosen by the low-`C_off` rule.
4. **E73-2G4M04S1B price confirmed on LCSC** if the MCU downgrade is taken — and the firmware port
   budgeted honestly if so.
5. **Battery MPN**, capacity, protection board, connector and polarity.
6. **3.0 V vs 3.3 V rail decision**, which sets the three `VBAT_OK` resistors (draft section stands).
7. **Rs value** — cannot be picked until &#124;Z&#124; is measured on a real blade at the chosen
   frequency. Leave it a placeholder and set it at commissioning.
