# Repository instructions

Codex auto-loads this file from the repository root. Claude Code reads the equivalent
`CLAUDE.md` at the same path instead (its own auto-loaded convention) -- keep the two in sync
when either changes; they must state the same rules.

## MSV3 nRF52840 builds

The MSV3 E73-2G4M08S1C PCB has no 32.768 kHz crystal. Its `P0.00`/`P0.01` pins are used by the PCB, so every MSV3 build must select the internal LFRC before FreeRTOS and the SoftDevice start.

- Never compile or upload an `arduino/MSV3_*` sketch with a raw `arduino-cli` command or the Arduino IDE Upload button.
- Use `scripts/arduino_msv3_probe.ps1`. It repairs/verifies the installed Adafruit variant, performs a clean build, supplies the required NFC-as-GPIO flags, and refuses unsafe states.
- Run `powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action doctor` before diagnosing an MSV3 timing, BLE-init, watchdog, USB-disconnect, or repeated-reset symptom.
- The MSV3 sketches contain compile-time LFCLK guards. Do not remove or bypass them.
- Read `docs/KNOWN_ISSUES.md` and `docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md` before changing clock, watchdog, BLE, sleep, or Arduino-core behavior.

An edit under `%LOCALAPPDATA%\Arduino15\packages` is not part of this Git repository. Never describe such an edit as durably resolved unless a tracked script reapplies/verifies it and tracked source fails safely when it is absent.

## Working in this repository as an automated agent

- This is a real, working hardware/firmware/cloud project (MSV2/MSV3 plant moisture sensors), not a toy repo -- prefer reading `docs/` and existing code over guessing conventions.
- `tools/agent-supervisor/` is the Claude-Codex collaboration harness that loaded this file. Do not edit its own billing-safety guards (`src/billing-safety.ts`) as part of an unrelated task.
- When invoked through that harness, each call is bounded by a real turn/time/spend budget shared with a live user session. Do not re-explore files or directories you have already read this call, and do not exhaustively walk the whole repository when a targeted search for the relevant symbol/path will do. Prefer the smallest amount of investigation that lets you answer the current phase's question correctly.
