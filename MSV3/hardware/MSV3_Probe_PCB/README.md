# MSV3 probe PCB

Four-layer soil probe built around a capacitance-to-digital converter, with solar charging and an
analog multiplexer that can swap the two electrode nodes.

| Part | Role |
| --- | --- |
| E73-2G4M08S1C (nRF52840) | MCU and Bluetooth LE radio |
| FDC2214 | Capacitance-to-digital converter (the moisture measurement) |
| TMUX1109 | Electrode-swap analog multiplexer |
| BQ25504 | Solar energy-harvesting charger |
| TPS7A0233 | 3.3 V low-quiescent LDO |

<p align="center">
  <img src="images/MSV3_assembly_paste_dispensing.jpg" height="420" alt="MSV3 board on a paste-dispensing machine during assembly">
  &nbsp;
  <img src="images/MSV3_pcb_layout.png" height="420" alt="MSV3 PCB layout">
  &nbsp;
  <img src="images/MSV3_pcb_3d.png" height="420" alt="MSV3 PCB 3D view">
  &nbsp;
  <img src="images/MSV3_probe_in_pot.jpg" height="420" alt="MSV3 probe in its enclosure in a pot">
</p>

| File | What it is |
| --- | --- |
| [`MSV3_Probe_PCB_fabrication_2026-08-10.zip`](MSV3_Probe_PCB_fabrication_2026-08-10.zip) | Gerbers (four copper layers), drill files and reports as exported from Altium on 2026-08-10/11 |
| [`MSV3_schematic_export_2026-07-26.pdf`](MSV3_schematic_export_2026-07-26.pdf) | The full five-sheet schematic as exported on 2026-07-26. The MCU and solar-charger sheets were edited again up to 2026-08-10, before the board was made; the netlist below is the later one |
| [`images/MSV3_fdc2214_front_end.png`](images/MSV3_fdc2214_front_end.png), [`images/MSV3_bq25504_solar_charger.png`](images/MSV3_bq25504_solar_charger.png), [`images/MSV3_electrode_swap_mux.png`](images/MSV3_electrode_swap_mux.png) | The sensing, solar-charging and multiplexer circuits |
| [`images/MSV3_pcb_3d_annotated.png`](images/MSV3_pcb_3d_annotated.png) | 3D view with the parts called out |
| [`reference/`](reference/) | Netlist, pin and net reference, component index and datasheet extracts |
| [`../MSV3_Probe_PCB_Enclosure/`](../MSV3_Probe_PCB_Enclosure/) | Enclosure and solar-panel mount |
