# MPD BA4AAPC 4 x AA battery holder, Altium library

`MPD_BA4AAPC.IntLib` is installed in Altium's Components panel. Place `BA4AAPC`
(designator `BT?`). It is one holder with 4 AA cells in series, so about 6 V. The pump
board uses two holders, with holder A's `+` wired to holder B's `-` on the PCB.

| | |
|---|---|
| Source | MPD drawing BA4AAPC rev H (03/06/19): its "PCB LAYOUT", top view and end view |
| Body | 57.48 x 63.09 mm, 15.88 mm tall |
| Origin | Body centre |
| Mounting holes | 2 x NPTH on the centre line, 54.61 mm apart (y = ±27.305) |
| Pins | 12.70 mm apart, 26.82 mm right of the hole line. 6.17 mm (pin 1) and 18.87 mm (pin 2) above the lower hole |
| Buy | Digi-Key 2439225. LCSC C6748055 showed no stock on 2026-11-11 |

**Polarity is inferred, not measured.** Tracing the cell ⊕/⊖ marks through the
four series cells puts pack **+** on the **outer** pin, pin 1 (square pad, `+` on the
silkscreen). The end view's item 1, MPD's "positive contact", is that same outer pin,
which agrees. Check a real holder with a meter before ordering boards. Reversed, this
puts the supply onto the board backwards.

Two holes are deliberately bigger than MPD's drawing, so the parts fit:

- Pin holes are **1.10 mm** (drawing: 1.02). The pin is 0.89 x 0.38 mm, about 0.97 mm
  corner to corner, and JLC's plated holes can come out 0.08 mm small.
- Mounting holes are **2.40 mm** (drawing: 2.03). A 2.03 mm hole does not clear
  the 2-56 screws MPD tells you to use.

Rebuild with `generator\Build-BA4AAPC.ps1`. It generates the library, checks every
pad and hole against the drawing values typed into `Test-BA4AAPC.ps1`, and installs it
through `hardware/altium/tools/Install-AltiumLibrary.ps1`.
