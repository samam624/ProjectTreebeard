# PTB_Pump_Driver x2: stock pull and order checklist (2026-11-18)

**Upload only `PTB_Pump_Driver_DigiKey_order_2boards_2026-11-18.csv` to DigiKey.** This file lists what
comes from stock and what comes from elsewhere. Do not upload it.

BOM source: the Altium project `PTB_Pump_Driver`, 28 components. That comes from the 11-11 PCB ECO
log and netlist; the schematic has not changed since 11-11 15:37, and the later ECOs are only M1
footprint updates. Built: 2 boards (JLCPCB PCBs x5).

## Order on DigiKey (verified live in the browser 2026-11-18)

| Designator | Part | DigiKey # | Status / stock | Qty | Price |
|---|---|---|---|---|---|
| U2 | TI TPS709A33DBVR | 296-50427-1-ND | Active, 6,130 | 3 (1 spare) | $1.51 |
| Q1 | AOS AO3400A | 785-1000-1-ND | Active, 104,034 | 10 (price break) | $0.322 @10 |
| D1, D3 | Vishay SS34-E3/57T | SS34-E3/57TGICT-ND | Active, 19,666 | 5 (4 + 1 spare) | $1.02 |
| C5 | Panasonic ECA-1CHG221B | 10-ECA-1CHG221BCT-ND | Active, 3,501 | 10 (price break) | $0.29 @10 |
| J1 | Same Sky (CUI) PJ-102AH | CP-102AH-ND | Active, 101,181 | 3 (1 spare) | $0.83 |
| (off-board) | Triad WSU120-1000 12 V 1 A adapter | 237-1455-ND | Active, 7,303 | 2 | $8.00 |

The list total is about **$34.24** before shipping and tariff. This is the wall-powered revision, changed
2026-11-18. The BA4AAPC holders (BA4AAPC-ND, 4 x $4.39) are **removed**: J1 plus a series SS34 (D3)
takes their place. The crystal line was dropped earlier because the maintainer has the crystals. C5 can
also come off if the maintainer finds a suitable electrolytic in the lab.

**U2 must be the A-version.** TI TPS709 datasheet SBVS186H, table 5-1, DBV package:

| Part | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 |
|---|---|---|---|---|---|
| TPS709A (this board) | OUT | GND | IN | NC | EN |
| TPS709 | IN | GND | EN | NC | OUT |
| TPS709B | GND | IN | OUT | NC | EN |

The board has pin 1 on 3V3, pin 2 on GND, pin 3 on 12V, and EN floating. The datasheet says EN "can
be left floating to enable the device". **Do not accept DigiKey's suggested substitutes**
TPS70933DBVR, TPS70933QDBVRQ1 or TPS709B33DBVR. Its "Parametric Equivalent" list includes them.
With the plain TPS709, 12V goes to EN (abs max 7 V) and the output is unconnected. The A-version in a
DBVT (small reel) package, TPS709A33DBVT, is the same part.

The pump library notes in `hardware/altium/Pump_Controller/` and the `TPS70933DBVR` / C89347 row in
`hardware/procurement/lcsc_verified_parts.csv` describe the **non-A** part. They do not apply to this
board.

Y1: DigiKey sells the same part as `SC-32S32.768kHz20PPM7pF` (schematic) and `SC32S-7PF20PPM` (Seiko
part number). It is 32.768 kHz, 7 pF, ±20 ppm, 70 kohm ESR, 3.2 x 1.5 mm.

## Pull from stock (MSV3 + MSV4 leftovers)

The stock figures are **computed, not counted**. They are the MSV3 leftovers and the MSV4 LCSC order
(2026-09-19), minus 5 MSV4 boards. The maintainer chose 2026-11-18 to use these now and reorder for
the second MSV4 batch later.

| ✓ | Designators | Per board | Pull x2 | Stock part | Stock left (computed) |
|---|---|---|---|---|---|
| ☐ | C1, C2: 10pF C0G 0805 | 2 | 4 | C107107 Yageo CC0805JRNPO9BN100 | 10 -> 6 |
| ☐ | C3, C8: 100nF X7R 0805 | 2 | 4 | C49678 Yageo CC0805KRX7R9BB104 | 15 -> 11 |
| ☐ | C4, C6, C7: 10uF 16V 0805 | 3 | 6 | C1713 Samsung CL21A106KOQNNNE x5 **+ 1 from the DigiKey cart line below** | 5 -> 0 |
| ☐ | R1, R3: 470 ohm 1% 0805 | 2 | 4 | C17710 0805W8F4700T5E | 90 -> 86 |
| ☐ | R2, R5: 100k ohm 1% 0805 | 2 | 4 | C149504 0805W8F1003T5E | 40 -> 36 |
| ☐ | R4: 1M ohm 1% 0805 | 1 | 2 | C2933280 FRC0805F1004TS | 85 -> 83 |
| ☐ | D2: blue 0805 LED | 1 | 2 | C497961 HL-PSC-2012H203BC (in place of Bivar SM0805UBWC) | 10 -> 8 |
| ☐ | Y1: SC-32S crystal | 1 | 2 | on hand (the maintainer said 2026-11-18 there are enough) | — |
| ☐ | U1: E73-2G4M08S1C | 1 | 2 | on hand (the maintainer said 2026-11-18 there are enough) | — |
| ☐ | M1: Kamoer NKP pump | 1 | 2 | on hand, Amazon | — |

Notes on the stock lines:

- **Count before relying on these:** the 10uF 16V (5) is about one board's worth. The maintainer confirmed
  the crystals and the 10pF caps are on hand (2026-11-18).
- **10uF line:** the DigiKey cart already holds 100x `1276-2872-1-ND`, which redirects to Samsung
  **CL21B106KOQNNNE**: 10uF 16V **X7R** 0805, a better dielectric than the X5R on the schematic. That
  covers the sixth capacitor. If that cart line is removed, add one 10uF 16V X5R/X7R 0805 to the order.
- **D2:** the HL-PSC blue LED was proven on MSV3 and MSV4 at 3.3 V with 470 ohm, which is the same
  circuit as here (R3 470 ohm from U1-2). Check its polarity mark against the SM0805UBWC footprint
  before soldering.

## Not ordered

Test points 3V3, CLK, GND, IO, SW_GND and VBAT are bare pads, not parts.

## Designator check

Wall-powered revision: BT1 and BT2 are removed and J1 and D3 are added:

- Order: U2, Q1, D1, D3, C5, J1, plus the off-board adapter.
- Stock: C1-C4, C6-C8, R1-R5, D2, Y1, U1, M1.
- Pads: the 6 test points.
