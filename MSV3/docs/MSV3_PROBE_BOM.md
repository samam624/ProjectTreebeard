# MSV3 Probe BOM — Ground Truth from the Actual LCSC Order

**Source of truth:** `LCSC_reorder_2026-07-31.csv` (repo root) — the real order placed 2026-07-31, not a
researched/estimated BOM. Where this contradicts an earlier candidate part discussed in
`MOISTURE_SENSOR_FDC2212_FRONTEND.md` or a memory note, **this order wins** — see the
"Reconciliation" section below for what changed and why it matters.

Quantities in this order are stock-up quantities (enough spare inventory for a few boards,
per this project's own "never buy more than 5 of an IC" rule — see `msv3-low-volume-procurement`),
not a per-board bill of materials. Unit price is real; for the ICs (ordered qty 5-6, one per
board) unit price ≈ per-board cost for that part. For passives (resistors/caps, ordered
qty 20-100) the real per-board count isn't known from this file alone — those are all
sub-cent-to-a-few-cents parts regardless, so they don't move the total meaningfully either way.

## Key ICs and modules (confirmed real 1-piece cost paid)

| Part | Mfr | Role | Package | Qty ordered | Unit price | Notes |
|---|---|---|---|---|---|---|
| E73-2G4M08S1C | Ebyte | MCU + BLE radio module | SMD 18x13mm | 5 | **$7.5888** | nRF52840-based, castellated hand-solderable pads. Replaced an earlier Raytac MDBT50Q-1MV2 choice. |
| FDC2214RGHR | TI | Capacitance-to-digital (sensing front end) | WQFN-16-EP(4x4) | 5 | **$6.70** | Substitute for FDC2212DNTT (out of stock). 4-channel; design math elsewhere assumes 2-channel FDC2212 — not yet reconciled, see FDC2212 doc open item 10. |
| BQ25504RGTT | TI | Solar/energy-harvesting boost converter | VQFN-16-EP(3x3) | 6 | **$5.8501** | Stock was tight at order time (12 units left at LCSC). |
| BQ25170DSGR | TI | LiPo battery charger | WSON-8(2x2) | 5 | **$0.8243** | |
| TPS7A0233PDBVR | TI | Low-IQ linear regulator | SOT-23-5 | 5 | **$0.7263** | **Powers the MCU (E73), TMUX1109, and FDC2214** (user-confirmed 2026-08-16) — the digital sensing chain already runs off this regulated rail, not raw battery. Relevant precedent if a future 555-based sensing experiment is explored, see FDC2212 doc's 2026-08-16 section. |
| **TMUX1109** | **TI** | **Electrode-swap / EC-check analog mux — confirmed real, populated part** | TSSOP or QFN | — | **$3.52-4.60** (verified 2026-08-10, DigiKey/LCSC; not in this CSV, ordered separately) | Picked for low RON (8.75Ω typ) and sub-nA leakage on the floating high-Z sensing trace — full selection process in `MOISTURE_SENSOR_FDC2212_FRONTEND.md`'s 2026-08-10/11 sections. |
| ~~74HC4052D,653~~ | Nexperia | Not used — bench/spare stock only | SOIC-16 | 5 | $0.0801 | |

## Power path and sensing passives

| Part | Mfr | Role | Package | Qty ordered | Unit price | Notes |
|---|---|---|---|---|---|---|
| LPS4018-223MRC | Coilcraft | Harvester boost inductor (22µH) — confirmed real part (photo/invoice) | SMD 4.0x4.0x1.7mm | 10 | **$0.5656** | Replaces C92914/LBC2012T220M (out of stock at LCSC) — footprint is NOT 0805, PCB layout must account for this. |
| **LPS4018-682MRC** | **Coilcraft** | **FDC2214 sensing-tank inductor (6.8µH) — confirmed real part (photo/invoice)** | SMD 4.0x4.0mm | 5 | Purchased via Mouser (Mouser P/N 994-LPS4018-682MRC) — not in the LCSC order | Same Coilcraft LPS4018 shielded-inductor family as the harvester inductor above (just the "682"/6.8µH code instead of "223"/22µH), sourced from Mouser because LCSC didn't carry this exact part. Real Coilcraft datasheet with published specs (unlike the Chilisin fallback below) — meaningfully de-risks the bench ring-down validation still called for in the FDC2212 doc's open item 3. |
| ~~LVS404018-6R8M-N~~ | Chilisin | Not used — LCSC fallback candidate, superseded by the confirmed part above | SMD 4.0x4.0mm | 10 | $0.0574 | Kept in the LCSC order as spare/backup stock only. |
| NTR1P02T1G | onsemi | P-channel MOSFET | SOT-23 | 5 | **$0.1711** | **Power muxing** (user-confirmed 2026-08-16) — switches between power sources, e.g. battery vs. solar/harvester input. |
| PMEG2010AET,215 | Nexperia | Schottky diode | SOT-23 | 5 | **$0.2387** | |
| USBLC6-2P6 | ST | ESD protection diode | SOT-666-6 | 5 | **$0.2779** | |
| TYPE-C-31-M-12 | Korean Hroparts | USB-C receptacle | SMD | 5 | **$0.1857** | |
| S2B-PH-K-S(LF)(SN) | JST | 2-position battery/probe connector | THT right-angle | 20 | **$0.0393** | |
| HL-PSC-2012H203BC | Honglitronic | Status LED (blue) | 0805 | 20 | **$0.0245** | |

**Padding capacitor (220-270pF C0G/NP0, needed to bring the FDC2214 tank to TI's recommended
300pF-2nF window) is not in this order** — flagged in the CSV itself as unresolved. Matches
`MOISTURE_SENSOR_FDC2212_FRONTEND.md`'s open item 1 (real capacitance measurement still needed
to size it correctly). Still open.

## Bulk passives (resistors/caps for biasing, filtering, dividers)

Not itemized here — see the CSV directly for the full list (mostly 0805 thick-film resistors
in the kΩ-MΩ range and 0805 ceramic caps, all sub-$0.02-0.13/unit even at 1pc). These don't
meaningfully move total probe cost regardless of exact per-board count.

**Exception: `R17` (EC-check divider resistor) — resolved 2026-08-31, not a sub-cent bulk part.**
Originally 100k 0805 (per the real netlist), confirmed via bench characterization this session
(`docs/STATUS_HANDOFF.md`, 2026-08-31 entries) to be roughly three orders of magnitude undersized
for the real electrode/soil resistance range (measured ~200M-1000M depending on medium, after
ruling out body-leakage, mux ESD-diode injection, and floating-VDD charging artifacts as
confounds). Replacement:

| Part | Mfr | Role | Package | Unit price | Notes |
|---|---|---|---|---|---|
| HVC0805Z5006JET | Ohmite | EC-check divider resistor (replaces 100k `R17`) | 0805 | **$1.13** | 500MΩ, ±5%, 1/8W. Nearest standard value to the ~424M geometric-mean target (200M-900M real bench range); same 0805 footprint as the part it replaces, no PCB rework. Real DigiKey pricing verified 2026-08-31 — notably more expensive than every other passive in this BOM, worth calling out explicitly rather than lumping in with the sub-cent bulk parts above. |

**Known BOM error, confirmed 2026-08-16: `BLM21PG220SN1D` (Murata, 0805, $0.03, qty 20) is a
ferrite bead, not a resistor.** It was meant to be a **22Ω USB D+/D- series resistor** (standard
practice — damps reflections/ringing on the USB data lines, standard in USB reference designs).
Root cause: this part's "22Ω @ 100MHz" impedance rating reads exactly like a resistor's value
in a parametric search (e.g. LCSC/EasyEDA search for "22Ω 0805" without filtering by component
type) - an easy, common mix-up. Functionally this matters: a ferrite bead's DC resistance is
~9mΩ (effectively a short), so it does none of the job a real series resistor would on these
lines. No resistor already in this order is a substitute - the closest value anywhere in the
BOM is 470Ω, over 20x off. **Fix: user is sourcing a standard 0805 22Ω resistor from lab stock**
(any 1% or 5% part is fine for this application, no special power rating needed) rather than a
matched LCSC reorder.

## Reconciliation with prior design-doc candidates and decisions

This order (2026-07-31) predates several design decisions recorded elsewhere in this repo/memory
that assumed different parts. Flagging each so the newer decision isn't silently treated as
"what's actually built":

- **Mux: this LCSC order has 74HC4052D ($0.08), but it's not what's populated.** User confirmed
  2026-08-16: **TMUX1109 was ordered separately** (not through LCSC) and is the real mux on the
  board, matching memory's "FINAL DECISION 2026-08-11: TMUX1109" — that decision *was* acted on,
  just not captured in this CSV. The 74HC4052D units bought here are bench/spare stock, not
  populated parts.
- **Sensing-tank inductor: order has Chilisin LVS404018-6R8M-N ($0.057) as an LCSC in-stock
  substitute** (true-0805 alternatives were out of stock there). User also confirmed 2026-08-16
  that inductor(s) were sourced outside LCSC for the same out-of-stock reason — **not yet
  confirmed whether that means this specific part was replaced too**, or refers only to the
  harvester inductor (`LPS4018-223MRC`) above. Don't treat either inductor line as final without
  checking against whatever non-LCSC order covers this.
- **FDC2214 channel count**: order confirms FDC2214 (4-channel), matching the FDC2212 doc's own
  already-flagged discrepancy (open item 10) against its FDC2212-based (2-channel) design math —
  still not reconciled as of this order.

## What this changes about MSV3 cost-reduction options (2026-08-16 research)

With real numbers instead of researched estimates, the "MCU is the biggest cost" framing from
the 2026-08-16 cost-reduction discussion needs revising: the Ebyte module is **$7.59**, not the
$15.99 XIAO Sense module that discussion was benchmarked against — a bare nRF52810 die ($3.26,
verified) would save roughly **$4.33/unit**, not ~$12.70, and adds real antenna/crystal PCB
design work the Ebyte module already handles. Still worth doing if that design effort is
welcome, but a much smaller lever than first estimated.

**Correction (2026-08-16, same day): TMUX1109 is confirmed real, ordered separately from this
LCSC batch — the original cost-reduction discussion's "mux is a real ~$3.50-4.60 cost" framing
stands as originally estimated**, it was only this doc's first-draft assumption (that 74HC4052D
was populated) that was briefly wrong, not the earlier research. The 555+RC-vs-FDC2214+mux
architecture tradeoff from that discussion is unaffected either way and remains the single
biggest unrealized lever (~$7-10/unit potential, see `msv3-probe-bom` memory) — still an open,
unvalidated idea, not something this order confirms or rules out.

The sensing-tank inductor's real identity is still open (see Reconciliation above) — once
confirmed, update this doc and the FDC2212 doc's "Design parameters" section rather than
leaving both the LCSC substitute and the possible external part listed as live candidates.
