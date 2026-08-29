---
name: flash-esp32-display
description: Compile and flash the MSV2 ESP32-S3 display receiver firmware to a connected device via arduino-cli. Use when asked to flash, upload, or verify firmware changes to the physical display receiver, or to check board/port status.
---

# Flash the MSV2 ESP32 display receiver

Full reference: [DisplayCenter/docs/ESP32_DISPLAY_FLASHING.md](../../../DisplayCenter/docs/ESP32_DISPLAY_FLASHING.md) — read it if anything below is ambiguous or the known-good status/build tag needs checking.

This flashes `DisplayCenter/arduino/MSV2_ESP32_Display_Runtime` using the Arduino IDE's bundled `arduino-cli.exe` via `DisplayCenter/scripts/arduino_display.ps1`. No `-ExecutionPolicy Bypass` needed — call the script directly.

## Before flashing

1. **List ports** to confirm the device and find its COM port:
   ```powershell
   .\DisplayCenter\scripts\arduino_display.ps1 -Action board-list
   ```
   The display receiver normally shows as an `ESP32 Family Device` (commonly `COM13` on this machine; ports can shift).

2. **Check nothing else holds the port.** The Arduino IDE (if open) frequently keeps a Serial Monitor session attached to the port, which makes `esptool` fail with `Could not open COMx, the port is busy` / `Access is denied`. If upload fails with that error, ask the user to close the Serial Monitor tab (or the IDE) for that port before retrying — do not kill the user's IDE process unilaterally, it may hold unsaved work.

## Compile only (no hardware needed, safe to run any time)

```powershell
.\DisplayCenter\scripts\arduino_display.ps1 -Action compile
```

Use this to verify a firmware change builds before touching real hardware.

## Flash normal firmware

```powershell
.\DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM13
```

Compiles then flashes. Swap `COM13` for whatever `board-list` reports.

## Flash forced first-time setup (for testing onboarding without wiping saved WiFi/cloud config until confirmed)

```powershell
.\DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM13 -ForceWifiSetup
```

## Serial monitor

```powershell
.\DisplayCenter\scripts\arduino_display.ps1 -Action monitor -Port COM13
```

This blocks/streams continuously — run with a bounded timeout or in the background when using it to capture boot logs, and stop it before attempting another upload to the same port (see the port-busy note above).

## Notes

- Flashing overwrites whatever firmware is currently running on the physical device — treat it as a real, user-visible action, not a no-op verification step. Confirm with the user before flashing a device that might be in active use, unless they've already asked for the flash directly.
- `arduino-cli.exe` lives at `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` (hardcoded in the script's `$CliPath` default).
- Board FQBN and build flags are already correct in `DisplayCenter/scripts/arduino_display.ps1` — don't hand-roll a different `arduino-cli compile`/`upload` invocation.
