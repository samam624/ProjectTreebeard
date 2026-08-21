# MSV3 Probe PCB — Complete Pin/Net Reference

Every net in `netlist/MSV3_sch.NET`, alphabetically, with every pin connected to it and a
function note where established. This is the raw, exhaustive companion to
`COMPONENT_INDEX.md` (which organizes by chip instead of by net) — use this file when you have a
net name and want to know everything touching it; use the component index when you have a chip
and want its full pinout in one place.

Refdes key: U1=E73 module, MS_IC=FDC2214, MUX=TMUX1109, SP_IC=BQ25504, USB_IC=BQ25170,
3V3_LDO=TPS7A0233, Q1=power-mux PMOS, D1=Schottky diode, ESD1=USBLC6-2P6, USB=USB-C receptacle.

| Net | Pins | Function |
|---|---|---|
| `3V3` | 3V3_LDO-5, C2-2, C12-1, C13-1, C18-2, C19-2, C20-2, MS_IC-7, MUX-2/6/10/14, R4-2, R5-2, U1-19, U1-23 | Main 3.3V rail — LDO output feeds MCU VDD/VDDH, FDC2214, mux, I2C pullups |
| `ESP_D+` | D+-1, ESD1-4, R21-2, TP2-1 | USB D+ after ESD protection, before 22Ω series resistor to E73 |
| `ESP_D-` | D--1, ESD1-6, R22-2, TP1-1 | USB D- after ESD protection, before 22Ω series resistor to E73 |
| `GND` | (56 pins — see netlist directly) | Common ground |
| `INTB` | MS_IC-5, U1-43 | FDC2214 interrupt / data-ready → E73 P0.10 (NFC2 pin, needs `CONFIG_NFCT_PINS_AS_GPIOS`) |
| `LBST` | L1-2, SP_IC-16 | Harvester boost inductor ↔ BQ25504 LBST |
| `MS_NODE_A` | MS_NODE_A-1 (test point), MUX-8 | Electrode A, routed through mux |
| `MS_NODE_B` | MS_NODE_B-1 (test point), MUX-9 | Electrode B, routed through mux |
| `MUX0` | MUX-1, U1-10 | TMUX1109 select A0 → E73 P0.30/AIN6 |
| `MUX1` | MUX-16, U1-9 | TMUX1109 select A1 → E73 P0.31/AIN7 |
| `Net3V3_LDO_1` | 3V3_LDO-1, 3V3_LDO-3, C1-2, D1-3, Q1-3 | LDO IN tied to EN (self-enable) — fed from the D1/Q1 power-source-select network |
| `NetD2_1` | D2-1, R16-2 | Status LED anode ↔ 470Ω series resistor |
| `NetR2_2` | R2-2, USB-A5 | USB-C CC1 pin, R2=5.1kΩ pulldown (device-mode detection) |
| `NetR3_1` | R3-1, USB-B5 | USB-C CC2 pin, R3=5.1kΩ pulldown (device-mode detection) |
| `NetR6_2` | R6-2, USB_IC-3 | BQ25170 TS (NTC/temp-qualification) resistor network |
| `NetR7_2` | R7-2, USB_IC-2 | BQ25170 ISET (fast-charge current set) resistor |
| `NetR8_2` | R8-2, USB_IC-7 | BQ25170 VSET (regulation voltage set) resistor |
| `NetR10_1` | R10-1, R13-2 | Mid-point in the BQ25504 VOC_SAMP/divider resistor chain — exact role not yet fully traced, revisit if MPPT behavior needs debugging |
| `NetR16_1` | R16-1, U1-2 | Status LED series resistor ↔ E73 P1.10 (Arduino pin 42) |
| `NetR21_1` | R21-1, U1-31 | 22Ω D+ series resistor ↔ E73 USB D+ |
| `NetR22_1` | R22-1, U1-29 | 22Ω D- series resistor ↔ E73 USB D- |
| `NetU1_15` | U1-15, USB_IC-5 | E73 P0.05/AIN3 ↔ BQ25170 STAT (charge status) |
| `NetU1_17` | U1-17, USB_IC-6 | E73 P1.09 ↔ BQ25170 PG (power good) |
| `RESET` | C15-1, RST-1 (test point), U1-26 | E73 hardware reset (P0.18) — bare test point, no button on this board |
| `SCL` | MS_IC-1, R4-1, U1-6 | I2C clock, E73 P1.13 (Arduino pin 45), 4.7k pullup R4 |
| `SD` | MS_IC-6, U1-41 | FDC2214 shutdown ← E73 P0.09 (NFC1 pin, needs `CONFIG_NFCT_PINS_AS_GPIOS`) |
| `SDA` | MS_IC-2, R5-1, U1-4 | I2C data, E73 P0.28 (Arduino pin 28), 4.7k pullup R5 |
| `SP+` | C4-2, C8-1, L1-1, R9-2, SP+1-1 (test point), SP_IC-2 | Solar panel positive input → BQ25504 VIN_DC, via L1 harvester inductor |
| `SWD_CLK` | SWD_CLK-1 (test point), U1-39 | SWDCLK, bare test point |
| `SWD_IO` | SWD_IO-1 (test point), U1-37 | SWDIO, bare test point |
| `TANK_HOT` | C17-1, L2-2, MS_IC-9, MUX-4, MUX-12 | FDC2214 sensing tank hot side — hardwired, NOT through the mux |
| `TANK_REF` | C17-2, L2-1, MS_IC-10, MUX-5, MUX-13 | FDC2214 sensing tank reference side — hardwired, NOT through the mux |
| `USB_D+` | ESD1-3, USB-A6, USB-B6 | Raw USB-C D+ before ESD chip |
| `USB_D-` | ESD1-1, USB-A7, USB-B7 | Raw USB-C D- before ESD chip |
| `VBAT` | -BAT+-2 (battery connector), C3-2, C6-2, C7-2, R29-2, SP_IC-14, USB_IC-8 | Battery rail — charger output, harvester battery pin, divider input |
| `VBAT_DIV` | C16-2, R29-1, R30-2, U1-7 | Battery-voltage ADC midpoint (100k/100k) → E73 P0.02/AIN0 |
| `VBAT_DIV_EN` | R30-1, U1-8 | Enables the battery divider ← E73 P0.29/AIN5 |
| `VBAT_OV` | R11-1, R14-2, SP_IC-6 | BQ25504 overvoltage threshold divider |
| `VBAT_UV` | R12-1, R15-2, SP_IC-8 | BQ25504 undervoltage threshold divider |
| `VBUS` | C5-2, C14-2, D1-1, ESD1-5, Q1-1, R1-2, U1-27, USB-A4_B9, USB-B4_A9, USB_IC-1 | USB-C VBUS — feeds E73 directly, charger IN, and the power-mux |
| `VDIV` | MUX-7, MUX-11, R17-2, U1-11 | EC-check divider midpoint (the "DIV_NODE" in the design docs) ← E73 P0.00 (no crystal populated, so free for this) |
| `VOC_SAMP` | R9-1, R10-2, SP_IC-3 | BQ25504 MPPT sample divider |
| `VRDIV` | R11-2, R12-2, SP_IC-7 | BQ25504 resistor-divider bias output |
| `VREF_SAMP` | C9-2, SP_IC-4 | BQ25504 MPPT reference storage cap |
| `VSTOR` | C10-2, C11-1, Q1-2, SP_IC-5, SP_IC-15 | BQ25504 boost output — also feeds Q1 (power mux) and OT_PROG (selects 120°C threshold) |

## Reading this alongside the schematic

- Bare test points (`SWD_IO`, `SWD_CLK`, `RESET`, `SP+1`, `MS_NODE_A`, `MS_NODE_B`, `VBAT_DIV`,
  `GND_SP`, `TP1`/`TP2`) all use footprint `Test Point-Footprint-1` in the netlist — none are
  connectors or buttons, all bare pads for probing/hand-contact.
- The netlist alone doesn't carry passive values (resistor/cap sizes) — those live in
  `docs/MSV3_PROBE_BOM.md` (the real LCSC/Mouser order) and the schematic itself. Cross-reference
  by refdes (e.g. "R9" here → look up R9 in the BOM for its actual value).
- This file and `COMPONENT_INDEX.md` were built once, by hand, following the process in the
  `pcb-reference` skill (`.claude/skills/pcb-reference/SKILL.md`). If the schematic changes
  (new revision, added/removed parts), regenerate rather than hand-patch — the skill documents
  the exact steps.
