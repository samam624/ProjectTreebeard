# Same Sky (CUI) PJ-102AH DC power jack, Altium library

`CUI_PJ102AH.IntLib` is installed in Altium's Components panel. Place `PJ-102AH` (designator `J?`).
It is the pump board's 12 V input. It replaces the two BA4AAPC battery holders; the board is fed
from a Triad WSU120-1000 12 V 1 A wall adapter (5.5 x 2.1 mm plug, centre positive).

| | |
|---|---|
| Source | CUI datasheet PJ-102AH, 10/30/2019, page 2: "PCB LAYOUT TOP VIEW", front and side views |
| Body | 9.0 wide x 14.4 long x 11.0 tall; plug axis 6.50 above the board |
| Origin | Pin 1. The mating face is toward +Y, at y = +13.7 |
| Pins | 1 at (0, 0), 2 at (0, 6.0), 3 at (-4.7, 3.0); round plated holes 1.6 mm, pads 2.6 mm |
| Buy | Digi-Key CP-102AH-ND; LCSC C3096093 |

## Pins

From the datasheet schematic:

| Pin | Contact | Connect to |
|---|---|---|
| 1 (square pad, `+`) | Centre pin, + with a centre-positive adapter | The anode of the series reverse-polarity diode; its cathode feeds 12V |
| 2 | Sleeve | GND |
| 3 | Switch | Unconnected, or GND. It touches pin 2 when no plug is in and opens when a plug is in. Solder it either way, for strength |

## Placement

Put the mating face (the double silk line) at the board edge, so a plug can reach the jack. Seen
from above with the mating face at the top, pin 3 is on the left. That is CUI's own top view.

## Deliberate deviation from the drawing

CUI recommends **1.00 x 1.60 mm** holes for the blade pins. JLCPCB makes plated slots only with
length at least 2 x width, so each pin gets a **round 1.6 mm** plated hole. That clears the
1.00 mm-wide blade. KiCad's `BarrelJack_CUI_PJ-102AH_Horizontal` uses the same hole size and the
same pin positions.

## Verification

`generator/Build-PJ102AH.ps1` generates the library, then reads back every hole. It compares
them with numbers typed into `Test-PJ102AH.ps1` from the CUI drawing, not from the generator.
Last, it installs the library through `hardware/altium/tools/Install-AltiumLibrary.ps1`.

The check passed on 2026-11-18. The compiled IntLib carries the symbol's footprint link and the
footprint's 3 pads. Altium's own model count read 0 in that session, which is the known caching
quirk, not a fault.
