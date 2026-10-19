# MSV3 probe PCB

A four-layer soil probe built around a capacitance-to-digital converter, with an analog
multiplexer that swaps the two electrode nodes, and solar and USB charging.

| Part | Role |
| --- | --- |
| E73-2G4M08S1C (nRF52840) | MCU and Bluetooth LE radio |
| FDC2214 | Capacitance-to-digital converter: the moisture measurement |
| TMUX1109 | Electrode-swap analog multiplexer |
| BQ25504 | Solar energy-harvesting charger |
| BQ25170 | USB lithium charger |
| TPS7A0233 | 3.3 V low-quiescent LDO |

<p align="center">
  <img src="images/MSV3_assembly_paste_dispensing.jpg" height="360" alt="MSV3 board on a paste-dispensing machine during assembly">
  <img src="images/MSV3_pcb_layout.png" height="360" alt="MSV3 PCB layout">
  <img src="images/MSV3_pcb_3d.png" height="360" alt="MSV3 PCB 3D view">
  <img src="images/MSV3_probe_in_pot.jpg" height="360" alt="MSV3 probe in its enclosure in a pot">
</p>

## Files

| File | What it is |
| --- | --- |
| [`MSV3_Probe_PCB_fabrication_2026-08-10.zip`](MSV3_Probe_PCB_fabrication_2026-08-10.zip) | Gerbers (four copper layers), drill files and reports, exported from Altium on 2026-08-10 and 2026-08-11 |
| [`MSV3_schematic_export_2026-07-26.pdf`](MSV3_schematic_export_2026-07-26.pdf) | The full five-sheet schematic as exported on 2026-07-26. The MCU and solar-charger sheets were edited again up to 2026-08-10, before the board was made; the netlist in `reference/` is the later one |
| [`images/MSV3_fdc2214_front_end.png`](images/MSV3_fdc2214_front_end.png), [`images/MSV3_bq25504_solar_charger.png`](images/MSV3_bq25504_solar_charger.png), [`images/MSV3_electrode_swap_mux.png`](images/MSV3_electrode_swap_mux.png) | The sensing, solar-charging and multiplexer circuits |
| [`images/MSV3_pcb_3d_annotated.png`](images/MSV3_pcb_3d_annotated.png) | 3D view with the parts called out |
| [`reference/`](reference/) | Netlist, pin and net reference, component index and datasheet extracts |

## Related

[Enclosure and solar-panel mount](../MSV3_Probe_PCB_Enclosure/) ·
[Bill of materials](../../docs/MSV3_PROBE_BOM.md) ·
[Firmware hang root cause](../../docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md) ·
[Solar overvoltage root cause](../../docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md)
