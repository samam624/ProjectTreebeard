# Repository instructions

Codex auto-loads the equivalent `AGENTS.md` at the same path instead of this file (its own
auto-loaded convention) -- keep the two in sync when either changes; they must state the same
rules.

## MSV3 nRF52840 builds

The MSV3 E73-2G4M08S1C PCB has no 32.768 kHz crystal. Its `P0.00`/`P0.01` pins are used by the PCB, so every MSV3 build must select the internal LFRC before FreeRTOS and the SoftDevice start.

- Never compile or upload an `arduino/MSV3_*` sketch with a raw `arduino-cli` command or the Arduino IDE Upload button.
- Use `scripts/arduino_msv3_probe.ps1`. It repairs/verifies the installed Adafruit variant, performs a clean build, supplies the required NFC-as-GPIO flags, and refuses unsafe states.
- Run `powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action doctor` before diagnosing an MSV3 timing, BLE-init, watchdog, USB-disconnect, or repeated-reset symptom.
- The MSV3 sketches contain compile-time LFCLK guards. Do not remove or bypass them.
- Read `docs/KNOWN_ISSUES.md` and `docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md` before changing clock, watchdog, BLE, sleep, or Arduino-core behavior.

An edit under `%LOCALAPPDATA%\Arduino15\packages` is not part of this Git repository. Never describe such an edit as durably resolved unless a tracked script reapplies/verifies it and tracked source fails safely when it is absent.
