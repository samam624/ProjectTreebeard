# Capacitive Soil Moisture Sensor v1.2 AFE checkout

**Date:** 2026-09-14

**Purpose:** Drive an experimental probe trace with the inexpensive v1.2 analog front end and log
its live output overnight, independently of MSV3's FDC2214 firmware.

The serial-only ESP32-S3 sketch is
`MSV3/arduino/ESP32_Capacitive_v1_2_Serial_Checkout/ESP32_Capacitive_v1_2_Serial_Checkout.ino`.
It initializes no BLE or cloud identity and applies no duty cycle. Wiring is VCC to 3V3, GND to
GND, and AOUT to GPIO4/ADC1_CH3. The sketch prints the five-sample median, exponential smoothing,
session minimum/maximum, and span every 500 ms; lower millivolts correspond to greater capacitance.

The initial decay to zero was not an oversized probe capacitor or firmware filtering problem. It
was a missed physical connection. After repair, hand loading moved the output from roughly 2.6 V
to 1.8 V and the setup was left running for the overnight comparison. Use
`MSV3/scripts/monitor_capacitive_v1_2_serial.ps1` for a no-reset live COM-port monitor.
