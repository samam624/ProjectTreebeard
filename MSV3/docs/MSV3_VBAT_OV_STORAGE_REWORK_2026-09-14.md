# MSV3 VBAT_OV storage-ceiling rework — 2026-09-14

Lowers the BQ25504 harvester charge ceiling from **≈4.26V to ≈3.90V** by swapping two resistors,
so a solar-floated LiPo is not held above its own 4.20V spec for its whole service life.

## Why

Field probes were reading **4.27V** on the battery node. That is above the 4.20V maximum for a
standard LiCoO2 cell, and it matches the as-built VBAT_OV of ≈4.26V within the part's ±5%
threshold accuracy — so this was the divider doing exactly what it was programmed to do, not a
fault.

The duty cycle here makes it worse than the raw number suggests: a harvester holds the cell near
the ceiling more or less continuously, so this is a **float/calendar-aging** duty, not a cycling
duty. Calendar aging is driven by time-at-high-voltage × temperature, and these probes sit in
pots, some in sun.

## The resistors

`R11` is the VRDIV-side leg, `R14` the GND-side leg, of the BQ25504 VBAT_OV divider. Confirmed
from the netlist via `MSV3/hardware/MSV3_Probe_PCB/reference/PIN_NET_REFERENCE.md`: net `VBAT_OV`
= `R11-1`, `R14-2`, `SP_IC-6`; net `VRDIV` = `R11-2`, `R12-2`, `SP_IC-7`.

In TI's naming (datasheet Equation 3, `BQ25504_extracted.txt:1265-1283`) `R11` is `R_OV2` and
`R14` is `R_OV1`:

```
VBAT_OV = 1.5 · VBIAS · (1 + R11/R14)
```

VBIAS is **1.25V typ** (range 1.21–1.27V). Do not use the 1.21V figure sitting near it in the
datasheet — that is the unrelated MPPT-disable threshold.

| | R11 (VRDIV side) | R14 (GND side) | Sum | VBAT_OV |
|---|---|---|---|---|
| As-built (2026-08-21) | 5.62 MΩ | 4.42 MΩ | 10.04 MΩ | 4.259 V |
| **Reworked (2026-09-14)** | **4.42 MΩ** | **4.12 MΩ** | 8.54 MΩ | **3.887 V** |

Using the VBIAS backed out of this board's own measurement (4.27V observed ÷ 4.259V computed →
VBIAS ≈ **1.2532V**) the reworked ceiling predicts **≈3.897V**, and charging restarts at
VBAT_OV − VBAT_OV_HYST (35 mV typ, 18/35/89 range) ≈ **3.862V**. With ±1% resistors the corner
spread is **3.857–3.937V**.

Both constraints hold: 3.90V is inside the 2.5–5.25V programmable range, and it clears
VBAT_UV ≈3.04V by a wide margin.

### Parts — no new part number required

| Value | LCSC | In-stock qty | Already used on this board as |
|---|---|---|---|
| 4.42 MΩ 1% 0805 | C332857 (RTT054424FTP) | 100 | `R9`, and the old `R14` |
| 4.12 MΩ 1% 0805 | C367866 (WR08W4124FTL) | 100 | `R15` |

## Alternatives considered and rejected

Enumerated across all 36 pairs of the megohm values actually in stock
(4.12 / 4.42 / 5.6 / 5.62 / 5.9 / 10 MΩ). Only three land in the 3.8–4.1V window:

| R11 | R14 | Sum | VBAT_OV | Verdict |
|---|---|---|---|---|
| 4.42 M | 4.12 M | 8.54 MΩ | 3.887 V | **chosen** |
| 5.9 M | 5.6 M | 11.50 MΩ | 3.850 V | rejected — sum > 10 MΩ |
| 5.9 M | 5.62 M | 11.52 MΩ | 3.843 V | rejected — sum > 10 MΩ |

The >10 MΩ rejections are not a nitpick. VRDIV only biases the divider string for a brief window
every 64 ms (`BQ25504_extracted.txt:1386-1391`, Nano-Power management) — an 11.5 MΩ source has to
settle against the pin and stray capacitance *inside that pulse*. That settling budget is why TI
caps the sum at 10 MΩ. 5.9 MΩ is also the scarcest stocked value (qty 20).

**4.00V was preferred on paper but is not reachable from stock with single parts.** The only route
is two 10 MΩ in parallel (= 5.0 MΩ) piggybacked on the `R11` pad, giving 4.006V. Rejected: stacked
0805s on an 8–9 MΩ node, on a board destined for soil and conformal coat, is exactly where flux
residue and moisture leakage shift the threshold — and leakage across `R14` *raises* VBAT_OV,
which is the failure being fixed. Not worth 100 mV.

**Clean the flux under `R11`/`R14` and get coating over them.** At these impedances that is a
functional step, not cosmetic.

### Rework note

`R11` and `R14` used to be visibly different parts (5.62 M vs 4.42 M). They are now 4.42 M and
4.12 M — markings `4424` vs `4124`, one character apart at 0805 size. **The larger value (4.42 MΩ)
goes in `R11`**, the leg tied to BQ25504 pin 7 (VRDIV). Reversed, the ceiling lands at 3.623V.
Meter both before placing. Use fresh parts rather than relocating the old `R14` — two extra
reflows on a high-value thick film risks drift that lands straight in the threshold.

## What this does NOT change

- **`R12`/`R15` (VBAT_UV ≈3.04V)** and **`R9`/`R10`/`R13` (MPP/VOC ≈78%)** are untouched.
- **The BQ25170 USB charge path is untouched**: `R8` = 27 kΩ → 4.20V. USB charging still tops the
  cell to 4.20V, which is in-spec; it was the harvester at 4.27V that was over-spec. Lowering it
  further needs a part not in stock — 47 kΩ → 4.05V is the correct 1-cell **Li-Ion** table entry
  (`BQ25170_extracted.txt`, Table 7-1). Avoid 62 kΩ → 3.70V: it is a **LiFePO4** entry and drags
  the protection thresholds with it (VBAT_SHORT 2.2→1.2V, precharge→fastcharge 2.8→2.0V).
  VSET requires an exact table value at ±1% (`RVSET_TOL`), so no series/parallel substitute works.

## Consequences to follow up

1. **`BATTERY_CURVE` needs rescaling.** It maps 4200 mV → 100%
   (`DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino`,
   and a deliberately byte-identical copy in `MobileAppPage.h`). At a 3.897V ceiling a **full**
   probe reports **63%** and never reads higher; normal float sits ~58–63%. Above the dashboard's
   <20% low-battery flag (`cloud/web/plant-dashboard/assets/app.v15.js`), so nothing breaks — but
   both copies should be rescaled so a full pack reads 100%. **Not yet done.**
   - Related pre-existing quirk, unchanged by this rework: the curve bottoms out at 3270 mV → 0%
     while VBAT_UV does not cut the load until ~3.04V, so "0%" still has runway under it.
2. **Smaller dark-spell buffer.** Usable window goes from 3.04–4.26V to 3.04–3.90V — roughly a
   third less stored energy. Calendar life improves substantially; ride-through on consecutive
   overcast days gets shorter.
3. **MPPT margin improved.** MPP is 78% of V_OC; at the 4.62V V_OC measured 2026-08-21 that is
   ≈3.6V, comfortably below a 3.90V ceiling. (A 3.70V ceiling, considered and rejected, would have
   put those nearly on top of each other and also parked every probe under the 20% low-battery
   flag at 17%.)

## Per-board status

| Board | Swapped? | Notes |
|---|---|---|
| All field probes except `MSV3-MOISTURE-007` | **Yes**, 2026-09-14 (maintainer) | — |
| `MSV3-MOISTURE-007` | **No** — still `R11`=5.62 MΩ / `R14`=4.42 MΩ, ceiling ≈4.26V | Measured **4513–4529 mV** across 13 BLE advertisements over ~10 min on 2026-09-14 — steady, not a charge transient. Needs a direct multimeter read on the cell and then the swap. This is the board with the dead TMUX A1 line (`MSV3_FOUR_BOARD_MUX_TRIAGE_2026-08-31.md` methodology) and a bootloader that rejects every DFU package tried — see `docs/STATUS_HANDOFF.md`. |

## Verifying a reworked board

Power it from the panel with a cell attached and watch VSTOR: it should plateau at **≈3.90V** and
ripple down to **≈3.86V** before charging resumes. A probe still stopping near 4.26V has the
resistors reversed or unswapped.
