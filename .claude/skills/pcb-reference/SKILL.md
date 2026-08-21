---
name: pcb-reference
description: Build (or extend) a local, offline pin/net reference library for a real PCB design from its Altium (or other CAD) netlist export plus each component's real datasheet — so any future pin, net, or IC question about that board can be answered by reading local files instead of re-deriving it from scratch or guessing. Use when the user gives a netlist and wants a durable reference built, asks "what's on this pin/net", asks for a pinout, or wants to set up this same process for a new board.
---

# Build a local pin/net reference library for a PCB

## Why this exists

Answering "what's connected to net X" or "what does pin Y do" by re-reading a raw netlist and
cross-referencing a datasheet from scratch is slow and easy to get subtly wrong (see
`hardware/MSV3_Probe_PCB/reference/` for a worked example — that library resolved several
previously-unidentified nets, like two charger status lines that had been open questions for
weeks). Once built, the reference library turns that into two file reads. Build it once per board
revision, then just read it.

## When to use this

- The user hands you a netlist (Altium `.NET` export, or similar) and wants a reference library
  built for a board that doesn't have one yet.
- An existing library needs extending (schematic changed, a new part needs its pinout added).
- You're about to answer a pin/net question on a board that already has a library under
  `hardware/<BoardName>/reference/` — **check there first**, don't re-derive from the raw netlist.

## Output structure (one per board)

```
hardware/<BoardName>/reference/
  netlist/<original filename>.NET       -- copy of the source netlist, unmodified
  datasheets/<PART>.pdf                 -- real datasheet PDFs, one per unique IC
  datasheets/<PART>_extracted.txt       -- PyMuPDF text extraction (see below), for fast grep
  COMPONENT_INDEX.md                    -- one section per chip: role, pinout table, key specs
  PIN_NET_REFERENCE.md                  -- one row per net: every pin on it, one-line function
```

Follow `hardware/MSV3_Probe_PCB/reference/` as the worked example for both files' exact format
and level of detail — copy its structure rather than inventing a new one each time.

## Process

### 1. Get the netlist into the repo

Copy the source file into `netlist/` verbatim. Altium exports as plain text: components as
`[ RefDes / Footprint / Value-or-partnumber ]` blocks, then nets as
`( NetName pin1 pin2 ... )` blocks. Read the whole file once — for a probe/sensor-scale board
this is small enough to fit in one read; for a larger board, read it in sections but make sure you
see every net before writing anything down as complete.

### 2. Identify the parts worth a datasheet

Skip bulk passives (resistors, ceramic caps) — their *values* matter for calculations but not a
pinout reference. Pull a real datasheet for every IC, module, and any part whose pin *functions*
(not just electrical value) matter: regulators, sensors, muxes/switches, charge/power-management
ICs, MCU/radio modules, connectors with a real pinout (USB-C, JST with more than 2 pins, etc.).

### 3. Fetch each datasheet — never `WebFetch` a PDF directly

Per `datasheet-lookup`: `WebFetch` frequently mangles vendor PDFs into "corrupted binary data" and
worse, can silently return a *summarized* number that's simply wrong. Always:

```bash
python scripts/fetch_datasheet.py "<pdf URL>"
```

This downloads the PDF and writes `<name>_full.txt` alongside it (PyMuPDF extraction, page markers
included). Search the *first* URL result from a web search for `<part number> datasheet pdf` —
manufacturer's own site (ti.com, nordicsemi.com, etc.) is the right target, not a distributor's
product page (fine for price/package, not for pin functions or electrical specs). Move both the
`.pdf` and the extracted `.txt` into `datasheets/`, named after the part number, not the
download's original filename.

**A module wrapping a bare chip needs an extra translation layer.** If the schematic uses a module
(an Ebyte/u-blox/Espressif RF module wrapping a bare MCU die, for instance) rather than the raw
chip, you need the *module's* pinout table (module pin number → chip pin/GPIO) *in addition to*
the bare chip's own datasheet — the netlist's pin numbers are the module's own numbering, not the
die's. Fetch the module vendor's own datasheet/user-manual for this table; don't assume it matches
the die 1:1.

### 4. Find each part's real pin table, don't trust the pinout diagram alone

Datasheet PDFs usually have a pinout **diagram** (a picture, extracts as scrambled/spatially
jumbled text via PyMuPDF — don't try to parse it) immediately followed by a proper **pin function
table** (`PIN / NAME / NO. / I/O / DESCRIPTION` columns, extracts cleanly). Search the extracted
`.txt` for `"Pin Functions"` or the part's known pin names to jump straight to the table; expect
multiple occurrences (a table-of-contents entry, then the real table) — take the one with actual
per-pin descriptions, not the TOC line. **If the part ships in multiple packages** (common for
small analog parts — SOT-23-5 vs WCSP vs QFN), make sure you're reading the table for the package
actually used on this board (check the BOM/netlist footprint field), not just the first one in the
datasheet.

### 5. Cross-reference every net against the real pin table — this is where the value is

For each net in the netlist connecting to a chip you just pulled a datasheet for, look up what
that pin *number* means per the datasheet, and write that down next to the net name. This is what
resolves the ambiguous/mystery nets a raw netlist alone can't answer (a net literally named
`NetU1_17` tells you nothing; cross-referencing pin 17 against the module's pin table and the
target chip's own pin table might reveal it's a charger status line). Sanity-check as you go: does
the resulting picture make physical sense (a "STAT"/status output pin landing on a net that also
touches an MCU input pin makes sense; a "GND" pin landing on a signal net doesn't — go back and
recheck if something looks wrong).

### 6. Write `COMPONENT_INDEX.md`

One table row per component (refdes, part number, role, package, which datasheet file), then one
section per IC with its full pin table **as used on this board** (pin number → name → net → what
it actually does here, not just the generic datasheet description). Note anything not yet fully
resolved honestly (a net whose function you couldn't pin down) rather than guessing — a flagged
gap is more useful than a confident wrong answer.

### 7. Write `PIN_NET_REFERENCE.md`

One row per net, alphabetical, every pin on it, one-line function note. This is the complement to
the component index — same underlying facts, organized the other way round, for when you have a
net name and want to know everything touching it without knowing which chip to look under first.

### 8. Flag gotchas as you find them, don't bury them in a pin table cell

Anything that would trip up someone writing firmware for this board later — a pin that defaults to
the wrong function until a specific compile flag is set, a missing crystal that changes which
clock-source macro a board variant needs, an unused feature deliberately tied off — deserves its
own called-out paragraph in the component index, not just a terse note in a table row. See the
MSV3 reference's "Gotcha (compile-time)" and "Gotcha (LFCLK)" callouts for the pattern.

## Keeping it current

If the schematic changes, don't hand-patch stale entries — re-run this process for the affected
components and regenerate the two markdown files. The netlist copy in `netlist/` is the source of
truth for "what does the real board currently look like"; if a new netlist export arrives, replace
it and diff mentally against the old one before updating the docs.
