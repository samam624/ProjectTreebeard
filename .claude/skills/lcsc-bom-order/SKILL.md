---
name: lcsc-bom-order
description: Turn an Altium BOM export into an LCSC/JLCPCB upload file in which every line resolves to a verified, in-stock part, with leftover stock subtracted and nothing left to fuzzy matching. Use whenever preparing, fixing, or reviewing an LCSC or JLCPCB parts order or BOM upload, when LCSC's BOM tool shows "No Match Found", "Not Available", backorders or odd substitutions, or when working out what to reorder for another build.
---

# LCSC BOM order

The goal is an upload in which **LCSC never has to guess**. Every line carries its own verified
`LCSC Part #`, the quantities already account for stock on hand, and the parts on backorder go
somewhere else.

Every rule below comes from a real failure on the 2026-09-19 MSV4 order.

## Why fuzzy matching is never acceptable

LCSC's BOM tool matches on text whenever the `LCSC Part #` column is empty. On that order it:

- picked Samsung C0G parts that were already **Not Available** (`10p`, `1n`);
- read **"10M ohm" as a 10 milliohm** alloy shunt (C51934136), because `M` and `m` are ambiguous to its matcher;
- returned **No Match** for `470` (no unit) and for every line whose C-number was merged into
  another column;
- picked a **backordered** TDK part for 10 nF C0G while in-stock equivalents existed.

The Treebeard generic passive library leaves `LCSC Part #` blank on purpose, because the value
changes per placement. That is fine in the design. It is **not** fine in an upload. The
**BOM freeze** below is the step that fills it in.

## Procedure

1. **Get the real BOM.** Use the Altium export from `Project Outputs for <project>/<project>.csv`
   or the `.cvg` BOM, not a hand-written list. Note per-board quantities and designators.
2. **Pre-flight the designators.** Stop and report if you see any of these:
   - a designator without a number (`U`, `C`);
   - a punctuation-mangled one (`-VBAT+`);
   - a part literally named `DNP` or `(DNP)`.

   DNP parts must be excluded by an Altium variant or "exclude from BOM", never by renaming them.
3. **Freeze every line to a C-number.**
   - Look it up first in `hardware/procurement/lcsc_verified_parts.csv` (value/class → LCSC#, MPN,
     date verified, known-dead alternatives).
   - For anything new, find the part with WebSearch (`LCSC <MPN>` or `LCSC 0805 1nF C0G 50V`).
   - **Before trusting any C-number, confirm its status** with WebFetch on
     `https://www.lcsc.com/product-detail/<Cnumber>.html`. Ask for stock, price and discontinued
     status. A 404 means delisted. **Parts go Not Available within hours:** C1785 was in stock in
     the morning and dead by the afternoon.
   - Preferences:
     - high-stock or JLC-basic parts over single-source ones;
     - Yageo `CC0805JRNPO9BNxxx` for 0805 C0G, since Samsung `CL21C...` C0G parts keep going unavailable.
   - **Keep the dielectric and rating class.** Never "fix" a C0G line with X7R. For example, the
     BQ25504's VREF_SAMP capacitor must be low-leakage C0G.
   - **Exact parts:**
     - Use the same MPN and package code. Tape suffixes may differ, e.g. BQ25504 RGTR vs RGTT are
       the same die.
     - Check the manufacturer name against the datasheet (LPS4018 is Coilcraft, not Bourns).
4. **Subtract stock on hand.**
   - Leftovers = previous orders (e.g. `MSV3/procurement/*.csv`, DigiKey order CSVs) minus
     per-board usage × boards built. Per-board usage comes from the pull list or BOM, plus any
     rework noted in the docs.
   - List the scarce leftovers (≤ about 1 board's worth) and ask for a physical count before
     removing them from the order.
   - A part you already have must match the **same spec class**, not just the same value.
5. **Set quantities.** Order per-board × boards, plus spares:
   - about 1–2 per 5 boards for fine-pitch or hand-soldered ICs, modules and QFNs (UMLP, SOT-563, QFN);
   - passives rounded up to LCSC's Min/Mult (usually 10, 20, 50 or 100).

   Say which lines include spares.
6. **Move backordered lines off the order.** A backordered line can hold the whole shipment.
   Source it elsewhere (e.g. E73 modules from Ebyte's store) and record where.
7. **Write the files.**
   - The upload CSV goes in `<subsystem>/procurement/<NAME>_LCSC_order_<N>boards_<date>.csv`, with
     the columns `Comment, Designator, Quantity, LCSC Part #, Manufacturer, Manufacturer Part Number, Notes`.
   - **One value per column.** Never put a C-number and an MPN in the same field.
   - **ASCII units:** `470 ohm`, `10M ohm`, `10pF`, `1nF`, `1uF`. `Ω` and `µ` come out of Altium's
     CSV export as `O` and `�`.
   - UTF-8 with BOM is fine.
   - Anything **not** to upload (pull-from-stock lists, source-elsewhere notes) goes in a `.md`
     checklist, **never a `.csv`**. A stock pull list uploaded by mistake is what "bricked" the
     order once.
8. **Validate before handing over.** Every row has a C-number; there are no duplicate C-numbers
   (merge them); designators across rows add up to the BOM.
9. **After upload, read LCSC's own match output** if the user pastes it. Replace any "Not
   Available", "No Match" or "Backordered" line with a WebFetch-verified alternative, and add dead
   numbers to the table's Notes column.
10. **Update `hardware/procurement/lcsc_verified_parts.csv`** with every newly verified or newly
    dead part, and today's date.

## Hand-over

Tell the user:
- which file to upload (and which files not to);
- what's coming from stock, with the counts;
- what's sourced elsewhere;
- which lines include spares;
- any line you couldn't verify, stated plainly rather than guessed.

## Related

- Library side: the Codex skills `altium-generic-passives` and `altium-component-generator`
  (`~/.codex/skills/`) define the parameters this freeze fills.
- Part lookups for specs rather than stock: the `datasheet-lookup` skill.
