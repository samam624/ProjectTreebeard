# MSV4 probe PCB

Two-layer soil probe. The blade carries two matching electrodes inside a guard ring on both faces,
and a soil-temperature NTC at the tip. The electronics measure the electrodes' impedance with a
lock-in (I/Q) front end built from two analog switches, at eight frequencies from 250 kHz to
2.67 MHz.

| Part | Role |
| --- | --- |
| E73-2G4M08S1C (nRF52840) | MCU and Bluetooth LE radio |
| 2 × FSUSB42 | A/B electrode swap, and the chopper of the lock-in front end |
| BQ25504 | Solar energy-harvesting charger |
| TPS7A0233 | 3.3 V low-quiescent LDO |
| NCP15XH103 (0402 NTC) | Soil temperature at the blade tip |

<p align="center">
  <img src="images/MSV4_four_assembled_boards.jpg" height="420" alt="Four assembled MSV4 boards">
  &nbsp;
  <img src="images/MSV4_pcb_layout.png" height="420" alt="MSV4 PCB layout">
  &nbsp;
  <img src="images/MSV4_pcb_3d.png" height="420" alt="MSV4 PCB 3D view">
  &nbsp;
  <img src="images/MSV4_lockin_front_end.png" height="420" alt="MSV4 lock-in front end schematic">
</p>

| File | What it is |
| --- | --- |
| [`MSV4_Probe_PCB_fabrication_2026-09-18.zip`](MSV4_Probe_PCB_fabrication_2026-09-18.zip) | Gerbers, drill files, pick-and-place and BOM as exported from Altium on 2026-09-18, the set ordered on 2026-09-19 |
| [`images/MSV4_schematic_overview.png`](images/MSV4_schematic_overview.png) | The whole schematic sheet (Altium's preview image, readable at block level) |
| [`images/MSV4_lockin_front_end.png`](images/MSV4_lockin_front_end.png) | The lock-in front end at full detail |
| [`images/MSV4_pcb_3d_detail.png`](images/MSV4_pcb_3d_detail.png), [`images/MSV4_tip_ntc_3d.png`](images/MSV4_tip_ntc_3d.png) | 3D close-ups: the front end, and the NTC at the tip |
| [`../reference/`](../reference/) | Netlist, pin and net reference, component index and datasheet extracts |
| [`../MSV4_Probe_Enclosures/`](../MSV4_Probe_Enclosures/) | Enclosures |

How the measurement works: [lock-in notes](../../docs/MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md).
Bring-up: [board 1](../../docs/MSV4_BOARD1_BRINGUP_2026-10-01.md),
[board 2](../../docs/MSV4_BOARD2_BRINGUP_2026-10-13.md).
