# MSV2 probe PCB

A two-layer soil probe carrying a XIAO nRF52840 Sense module, which times how fast the electrode
charges through its own GPIO pins and broadcasts the result over Bluetooth LE. A solar panel
charges the battery through a USB charger IC.

| Part | Role |
| --- | --- |
| Seeed XIAO nRF52840 Sense | MCU, Bluetooth LE radio and the moisture timing |
| BQ25100 | Battery charger, fed from the solar panel |

<p align="center">
  <img src="images/MSV2_probe_in_pot.jpg" height="360" alt="MSV2 probe in its solar enclosure in an outdoor pot">
  <img src="images/MSV2_pcb_layout.png" height="360" alt="MSV2 PCB layout">
  <img src="images/MSV2_pcb_3d.png" height="360" alt="MSV2 PCB 3D view">
</p>

## Files

| File | What it is |
| --- | --- |
| [`MSV2_Probe_PCB_fabrication_2026-06-19.zip`](MSV2_Probe_PCB_fabrication_2026-06-19.zip) | Gerbers and reports, exported from Altium on 2026-06-19 |
| [`images/MSV2_schematic.png`](images/MSV2_schematic.png) | Schematic |

## Related

[Firmware](../arduino/) · [Oscillator report, calibration and field notes](../docs/)
