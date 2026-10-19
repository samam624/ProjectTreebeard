# MSV2: XIAO nRF52840 probe

The probe the whole system was first built around: a XIAO nRF52840 Sense module that times the
electrode through its own GPIO pins and broadcasts over Bluetooth LE, with solar charging. It
monitored real plants for months. See the [main README](../README.md#status-and-next-steps) for
where the remaining probe is now.

| | |
| --- | --- |
| **Hardware** | [Probe PCB](hardware/) |
| **Firmware** | [`arduino/MSV2_Moisture_BLE_Sleep`](arduino/MSV2_Moisture_BLE_Sleep/) (production) and the test sketches beside it |
| **Tools** | [`scripts/`](scripts/): build scripts |
| **Write-ups** | [`docs/`](docs/) |

Start with:
[oscillator report](docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md) ·
[adaptive moisture calibration](docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md)
