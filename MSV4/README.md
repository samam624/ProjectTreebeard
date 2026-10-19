# MSV4: impedance-measuring solar probe

The current probe generation. It measures the soil's impedance with a lock-in front end at eight
frequencies, carries a soil-temperature sensor at the tip, and charges from a 2 V solar panel. See
the [main README](../README.md#status-and-next-steps) for where each board is now.

| | |
| --- | --- |
| **Hardware** | [Probe PCB](hardware/MSV4_Probe_PCB/) · [Enclosures](hardware/MSV4_Probe_Enclosures/) · [Pin, net and part reference](hardware/reference/) |
| **Firmware** | [`arduino/MSV4_Moisture_BLE_Sleep`](arduino/MSV4_Moisture_BLE_Sleep/) (production) · [`arduino/MSV4_Probe_Bringup_BLE`](arduino/MSV4_Probe_Bringup_BLE/) (bring-up console) |
| **Tools** | [`scripts/`](scripts/): build, Bluetooth update, advertisement scanner, bench lab, analysis · [`ui/`](ui/): the bench lab page |
| **Parts** | [`procurement/`](procurement/): BOM and the LCSC order for five boards |
| **Write-ups** | [`docs/`](docs/) |

Start with:
[board 1 bring-up](docs/MSV4_BOARD1_BRINGUP_2026-10-01.md) ·
[lock-in measurement notes](docs/MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md) ·
[solar charge test](docs/MSV4_OUTDOOR_SOLAR_CHARGE_TEST_2026-10-06.md) ·
[moisture against a kitchen scale](docs/MSV4_EXPERIMENTAL_MOISTURE_2026-10-10.md) ·
[board 2 in water and salt](docs/MSV4_BOARD2_CUP_WATER_SALT_TEST_2026-10-14.md) ·
[electrode models on raw sweeps](docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md)
