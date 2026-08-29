---
name: flash-moisture-probe
description: Compile, flash, and reliably capture boot-time serial output from the MSV2 moisture probe firmware (XIAO nRF52840 Sense, Alocasia/Scindapsus sketches) via arduino-cli. Use when asked to flash a probe, check probe compile status, or capture/confirm serial diagnostics from probe boot - including across a fresh reflash, where a plain serial monitor session does not survive the DFU reset.
---

# Flash a moisture probe and capture its boot output

Two probe sketches, both on Seeed XIAO nRF52840 Sense boards, both flashed the same way via `MSV2/scripts/arduino_probe.ps1`:

| Probe | Sketch | Port (last known, can shift) |
|---|---|---|
| Scindapsus | `MSV2/arduino/MSV2_Moisture_BLE_Sleep` | COM15 |
| Alocasia | `MSV2/arduino/MSV2_Moisture_BLE_Sleep_Alocasia` | COM11 |

FQBN: `Seeeduino:nrf52:xiaonRF52840Sense`. `arduino_probe.ps1` hardcodes this and the `arduino-cli.exe` path already — don't hand-roll a different invocation.

```powershell
powershell -ExecutionPolicy Bypass -File MSV2/scripts/arduino_probe.ps1 -Action board-list
powershell -ExecutionPolicy Bypass -File MSV2/scripts/arduino_probe.ps1 -Action compile -Sketch "MSV2/arduino/MSV2_Moisture_BLE_Sleep_Alocasia"
powershell -ExecutionPolicy Bypass -File MSV2/scripts/arduino_probe.ps1 -Action upload -Port COM11 -Sketch "MSV2/arduino/MSV2_Moisture_BLE_Sleep_Alocasia"
```

Compile before upload when making changes — cheap, no hardware needed, catches errors before touching the physical device. Flashing overwrites whatever's running and briefly interrupts a probe that may be actively deployed — treat it as a real action, not a no-op, same as the display receiver (see `flash-esp32-display`).

## Capturing boot-time serial output (the hard part)

`arduino-cli monitor -p COMx` (or any single-open serial session, including the Arduino IDE's monitor) **cannot** catch output across a reflash. The DFU upload process on this board briefly renumbers the port (e.g. `COM11` shows as `COM10` mid-flash in the tool's own log) and any plain monitor session dies the moment the port disappears — it does not reconnect. If you want to see what a sketch prints in `setup()` right after flashing it, you need a **reconnecting** capture, not a plain monitor.

**Use `scripts/serial_capture.py`** — retry-connects in a loop for a fixed window instead of exiting on the first drop:

```powershell
python scripts/serial_capture.py --port COM11 --duration 18 --out capture.log
```

### The recipe that actually works

1. **Run the upload to completion first.** Do not start the capture script concurrently with the upload — the DFU tool needs *exclusive* access to the port for its own bootloader-touch handshake, and a capture script already holding the port open causes the upload to fail with `PermissionError: Access is denied` (confirmed 2026-08-09). Sequential only: upload, wait for it to finish, then capture.
2. **Start the capture script immediately after the upload command returns.** No sleep needed before starting it — it retry-connects on its own.
3. **In the sketch, don't rely on `if (Serial)` / `while (!Serial)` to gate boot diagnostics you want to guarantee capturing.** On this board, `Serial` reads `true` almost immediately after a DFU reset — before a real terminal has actually attached and started reading — so a `while (!Serial) { delay(10); }` loop exits far too early to matter (confirmed empirically: it did not give the capture script enough time to reconnect). Use a **flat, unconditional delay instead** — `delay(1500);` right after `Serial.begin()`, before anything you want to see, was sufficient in testing. It's a one-time cost at boot only (`setup()` runs once per power-on, not per wake cycle), so it's free to leave in permanently — it does not affect steady-state battery life on a sleep/advertise loop.
4. **Expect some flakiness even with this recipe.** In practice this worked cleanly and repeatably on one probe (Scindapsus/COM15) and did not catch output on another (Alocasia/COM11) across three attempts in the same session, despite identical firmware logic and successful "Device programmed" confirmations each time from the DFU tool itself — likely host-side USB re-enumeration timing that varies by port/hub, not a firmware bug. A failed capture is not evidence the flash failed; the DFU tool's own success message is the authority on whether the upload worked. If capture keeps missing on a given port, the more reliable fallback is: open `serial_capture.py` (or a plain monitor) first, *then* physically power-cycle the board (unplug/replug USB) rather than racing a fresh reflash — a real unplug/replug doesn't have the DFU tool's port-probing in the way.

### Example end-to-end

```powershell
powershell -ExecutionPolicy Bypass -File MSV2/scripts/arduino_probe.ps1 -Action upload -Port COM15 -Sketch "MSV2/arduino/MSV2_Moisture_BLE_Sleep"
python scripts/serial_capture.py --port COM15 --duration 15 --out scindapsus_boot.log
```

Then read `scindapsus_boot.log` for whatever `setup()` printed.
