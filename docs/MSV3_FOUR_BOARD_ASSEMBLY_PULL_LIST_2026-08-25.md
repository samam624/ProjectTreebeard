# MSV3 Four-Board Assembly Pull List

Prepared 2026-08-25 for four copies of the current MSV3 probe PCB.

This is the exact minimum population quantity, with no allowance for dropped or
damaged parts. It contains 55 fitted components per board: 220 component
placements total, plus four bare PCBs. Quantities are sorted from highest to
lowest. All passives are 0805 unless the package is stated otherwise.

| Pull qty | Per board | Component | Designators |
|---:|---:|---|---|
| **24** | 6 | 10 uF X5R capacitor | C1-C4, C13, C14 |
| **24** | 6 | 100 nF X7R capacitor | C11, C12, C15, C16, C18, C20 |
| **16** | 4 | 100 kohm 1% resistor | R1, R17, R29, R30 |
| **12** | 3 | 1 uF X7R capacitor | C5, C6, C19 |
| **12** | 3 | 4.7 uF X5R capacitor | C7, C8, C10 |
| **8** | 2 | 22 ohm resistor | R21, R22 |
| **8** | 2 | 4.42 Mohm 1% resistor | R9, R14 |
| **8** | 2 | 4.7 kohm 1% resistor | R4, R5 |
| **8** | 2 | 5.1 kohm 1% resistor, **0603** | R2, R3 |
| **8** | 2 | 10 kohm 1% resistor | R6, R7 |
| **4** | 1 | 10 nF C0G/NP0 capacitor | C9 |
| **4** | 1 | 270 pF C0G/NP0 capacitor | C17 |
| **4** | 1 | 27 kohm 1% resistor | R8 |
| **4** | 1 | 10 Mohm 1% resistor | R10 |
| **4** | 1 | 5.62 Mohm 1% resistor | R11 |
| **4** | 1 | 5.9 Mohm 1% resistor | R12 |
| **4** | 1 | 5.6 Mohm 1% resistor | R13 |
| **4** | 1 | 4.12 Mohm 1% resistor | R15 |
| **4** | 1 | 470 ohm 1% resistor | R16 |
| **4** | 1 | E73-2G4M08S1C nRF52840 module | U1 |
| **4** | 1 | FDC2214RGHR, WQFN-16-EP | MS_IC |
| **4** | 1 | TMUX1109PWR, TSSOP-16 | MUX |
| **4** | 1 | BQ25504RGTT, VQFN-16-EP | SP_IC |
| **4** | 1 | BQ25170DSGR, WSON-8 | USB_IC |
| **4** | 1 | TPS7A0233PDBVR, SOT-23-5 | 3V3_LDO |
| **4** | 1 | LPS4018-223MRC, 22 uH | L1 |
| **4** | 1 | LPS4018-682MRC, 6.8 uH | L2 |
| **4** | 1 | NTR1P02T1G P-channel MOSFET, SOT-23 | Q1 |
| **4** | 1 | PMEG2010AET,215 Schottky diode, SOT-23 | D1 |
| **4** | 1 | HL-PSC-2012H203BC blue LED | D2 |
| **4** | 1 | USBLC6-2P6 ESD protector, SOT-666 | ESD1 |
| **4** | 1 | TYPE-C-31-M-12 USB-C receptacle | USB |
| **4** | 1 | S2B-PH-K-S(LF)(SN) JST connector | BATT_JST |
| **4** | 1 | Bare MSV3 PCB | PCB |

## Stock and substitution warnings

- The recorded 2026-07-31 order contains only 20 of the 100 nF capacitors,
  while these four boards require 24. If the existing prototype consumed six
  from that bag, only 14 remain and ten more are needed.
- The recorded order does not contain the 4.7 kohm resistors, the 270 pF tank
  capacitors, or correct 22 ohm USB resistors. Pull those from verified lab
  stock.
- Do not use `BLM21PG220SN1D` for R21/R22. It is a ferrite bead, not a 22 ohm
  resistor.
- Do not use the ordered `74HC4052D`. The fitted mux is `TMUX1109PWR`.
- Do not use `LVS404018-6R8M-N` for L2. The fitted tank inductor is the
  separately sourced `LPS4018-682MRC`.
- Use the low-leakage 10 nF C0G/NP0 part for C9, not the X7R 10 nF stock.
- The ordered solar-harvester part is marked `BQ25504RGTT`. The schematic
  netlist says `BQ25504RGTR`; the final letter is an ordering/packaging suffix,
  and the ordered `RGTT` part fits the RGT footprint.
- Test-point designators are bare PCB pads and require no components.

## Sources

- `hardware/MSV3_Probe_PCB/reference/netlist/MSV3_sch.NET`
- `hardware/MSV3_Probe_PCB/reference/COMPONENT_INDEX.md`
- `docs/MSV3_PROBE_BOM.md`
- `LCSC_reorder_2026-07-31.csv`
