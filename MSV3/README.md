# MSV3: FDC2214 solar probe

The first custom-board probe: a capacitance-to-digital converter, an electrode-swap multiplexer,
solar charging and signed firmware updates over Bluetooth. See the
[main README](../README.md#status-and-next-steps) for where each board is now.

| | |
| --- | --- |
| **Hardware** | [Probe PCB](hardware/MSV3_Probe_PCB/) · [Enclosure and solar-panel mount](hardware/MSV3_Probe_PCB_Enclosure/) |
| **Firmware** | [`arduino/MSV3_Moisture_BLE_Sleep`](arduino/MSV3_Moisture_BLE_Sleep/) (production) · [`arduino/MSV3_Probe_Bringup_Test`](arduino/MSV3_Probe_Bringup_Test/) (bring-up) |
| **Bootloader** | [`bootloader/`](bootloader/): the signed Bluetooth and USB bootloader, its local patches and its releases |
| **Tools** | [`scripts/`](scripts/): the build script, which enforces the board's clock rules, and test tooling |
| **Parts** | [`procurement/`](procurement/): LCSC orders |
| **Fleet registry** | [`fleet.json`](fleet.json): where every probe is, what powers it and what is installed on it |
| **Write-ups** | [`docs/`](docs/) |

Start with:
[firmware hang root cause](docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md) ·
[wired firmware updates resolved](docs/MSV3_WIRED_DFU_RESOLVED_2026-09-21.md) ·
[solar overvoltage root cause](docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md) ·
[electrode or electronics](docs/MSV3_ALOCASIA_ELECTRODE_VS_CIRCUIT_2026-09-29.md) ·
[the MSV4 electrode design](docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md)
