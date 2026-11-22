# DisplayCenter controller PCB

A two-layer receiver board. It hears every probe over Bluetooth LE, drives a 480 × 320 touch
display and uploads the history over Wi-Fi.

| Part | Role |
| --- | --- |
| ESP32-S3-WROOM-1U-N16R8 | MCU, Bluetooth LE and Wi-Fi, with an external antenna |
| AP2112K-3.3 | 3.3 V LDO, supplied from USB-C |
| 2N7002 | Low-side switches, including the display backlight brightness |
| Buzzer (TDK) | Alerts |

<p align="center">
  <img src="images/DisplayCenter_receiver.jpg" height="360" alt="DisplayCenter receiver showing plant cards">
  <img src="images/DisplayCenter_pcb_layout.png" height="360" alt="DisplayCenter controller PCB layout">
  <img src="images/DisplayCenter_pcb_3d.png" height="360" alt="DisplayCenter controller PCB 3D view">
</p>

## Files

| File | What it is |
| --- | --- |
| [`DisplayCenter_controller_fabrication_2026-07-14.zip`](DisplayCenter_controller_fabrication_2026-07-14.zip) | Gerbers, drill files and reports, exported from Altium on 2026-07-14 |
| [`images/DisplayCenter_schematic_overview.png`](images/DisplayCenter_schematic_overview.png) | The whole schematic sheet (Altium's preview image, readable at block level) |

## Related

[Tabletop enclosure](../V2/) ·
[Receiver firmware, wiring and setup](../../../README.md) ·
[Upload stall incident report](../../../docs/RECEIVER_TLS_STALL_2026-10-14.md)
