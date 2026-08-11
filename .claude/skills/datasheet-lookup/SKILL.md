---
name: datasheet-lookup
description: Pull verified electrical specs (RON, capacitance, leakage, charge injection, price, pinout, package, etc.) out of a component datasheet PDF or distributor page for part selection / BOM decisions. Use this any time a spec number is about to be quoted or compared for a real part - not just when a fetch has already failed. Triggers on part search, datasheet lookup, "find a part that...", comparing ICs, or verifying a spec before it goes in a schematic/BOM.
---

# Datasheet lookup: get real numbers, not summarized guesses

## Why this exists

`WebFetch` on a vendor datasheet PDF (confirmed repeatedly on TI PDFs: TMUX1574, TMUX1109,
TMUX1308/1309-Q1) frequently returns "this looks like corrupted/compressed binary data, I can't
read it" even though the PDF is completely normal. The failure is in WebFetch's own PDF-to-text
step, not the file - `PyMuPDF` opens and extracts the same files perfectly.

**The more dangerous failure mode isn't the outright refusal - it's when `WebFetch` on an HTML
product page (ti.com, distributor comparison tables) *does* return a confident-sounding number
that's simply wrong.** Confirmed 2026-08-10: a product-page fetch reported TMUX1309's RON as
"59Ω typical." The real datasheet table (VDD=3.3V row) says **330Ω typical, 350-370Ω max** -
6x worse, and actually worse than the legacy part it was being recommended to replace. The
summarizer likely pulled a number from the wrong row of a multi-part comparison table. It read
as authoritative and was wrong, which is worse than an obvious failure.

**Rule: any numeric spec that will actually be used in a recommendation, a BOM, or a schematic
must be verified against the primary-source datasheet PDF table, with page/line context, before
it's trusted - not taken from a WebFetch summary of an HTML product page.** Product-page
summaries are fine for orientation (does this part exist, roughly what family) but not as the
cited source for a number someone will act on.

## The reliable procedure

1. Find the datasheet PDF URL (search, or the user gives it directly - TI's pattern is
   `https://www.ti.com/lit/ds/symlink/<partnumber-lowercase>.pdf`; other vendors vary).
2. Extract it with the helper script instead of `WebFetch`:
   ```bash
   python scripts/fetch_datasheet.py "https://www.ti.com/lit/ds/symlink/tmux1109.pdf"
   ```
   This downloads the PDF (if given a URL) and writes a `..._full.txt` next to it, with
   `===PAGE N===` markers. If `WebFetch` already ran and saved a copy under
   `tool-results/webfetch-*.pdf`, point the script at that local path directly instead of
   re-downloading:
   ```bash
   python scripts/fetch_datasheet.py "C:\path\to\tool-results\webfetch-....pdf"
   ```
3. **Grep the extracted text for the parameter**, not just the part name - datasheets repeat
   the same parameter (RON, COFF, IS(OFF), bandwidth...) once per supply-voltage section, so
   there are usually several hits and you need the row for *this design's* actual VDD, not the
   first one found or the 5V default section:
   ```
   Grep pattern: "RON|COFF|Charge injection|IS\(OFF\)|Bandwidth"
   ```
4. **Read the surrounding ~30-150 lines around the hit**, not just the matched line. TI's table
   layout extracts as one value per line with the parameter name, test conditions, and each
   VDD/temperature corner as separate lines in sequence - the number means nothing without
   walking a few lines up (parameter/units) and confirming which VDD section you're in (search
   for `Electrical Characteristics (VDD = `, these mark section boundaries with page numbers in
   the table of contents near the top of the file).
5. Cite the actual value **and which VDD/temperature corner it came from** when reporting it -
   "RON 8.75Ω typ / 9.75Ω max at VDD=3.3V, -40 to 125°C" is a real citation; "RON is low" or an
   unqualified single number from a summarized page is not.

## When the text itself is the problem: equations, not just corrupted PDFs

`PyMuPDF` text extraction is reliable for tables (parameter name, condition, value, unit as
separate lines) but **not for actual mathematical formulas** - fractions, square roots, and
multi-term equations with subscripts extract as a scrambled flat list of numbers and symbols in
roughly the right order but with no structure ("3 3 10 M 1.25 V RSUM VBIAS R 5.95 M 5.90 M...").
Confirmed 2026-08-10 on a BQ25504 threshold-setting equation: hand-parsing that scramble produced
a plausible-looking formula that was subtly wrong (missing a 1.5x term), which then failed to
reproduce the datasheet's own worked examples not once but twice, and nearly led to "correcting"
a resistor network that was already right. Two failed cross-checks against the datasheet's own
stated results is the signal to stop parsing scrambled text and go look at the real page.

**Fix: render the actual PDF page as an image and read it directly, rather than keep guessing
from mangled text.** `poppler` (specifically `pdftoppm`) is what the `Read` tool's PDF `pages`
parameter needs under the hood - confirmed installed via
`winget install --id oschwartz10612.Poppler` and added to the persistent User PATH (registry-level,
not just the invoking shell), so a fresh session's tools should pick it up automatically. If
`Read` on a PDF with a `pages` range still errors "pdftoppm is not installed," the shell that
tool call runs in hasn't picked up the updated PATH yet (happens if it's the very shell winget
ran in, or something reset the environment) - reinstall or manually locate and add
`.../WinGet/Packages/oschwartz10612.Poppler_.../poppler-<version>/Library/bin` to PATH for that
session as a one-off, rather than assuming the capability is gone.

Use this whenever a formula, an equation-heavy figure, or a schematic diagram is the actual
source of truth and the extracted text isn't resolving cleanly after one or two attempts - don't
burn multiple rounds re-deriving a garbled equation by hand first.

## Distributor pages (pricing, package/footprint availability)

Structured distributor pages (DigiKey, LCSC, Mouser product-detail pages) are a different, more
reliable failure mode than PDFs - `WebFetch` usually parses their price/stock fields correctly
since they're plain HTML tables, not encoded PDF text. These are fine to trust for **price and
package/pin-count**, but still confirm electrical specs against the datasheet PDF, not a
distributor's paraphrased feature list.

## When a part is ruled out or reinstated mid-session

Verifying numbers can reverse an earlier recommendation (it did, twice, in the same session that
produced this skill). That's the process working, not a failure - say so plainly, show the old
vs. new number and their sources, and update anywhere the earlier (wrong) number was written
down (docs, memory, BOM notes) so it doesn't get re-cited later.
