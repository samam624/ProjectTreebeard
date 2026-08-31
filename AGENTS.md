# Repository instructions

Codex auto-loads this file from the repository root. Claude Code reads the equivalent
`CLAUDE.md` at the same path instead (its own auto-loaded convention) -- keep the two in sync
when either changes; they must state the same rules.

## Repository layout (Project Treebeard, reorganized 2026-08-29)

This repo was renamed from `MSV2` to `ProjectTreebeard` and split by subsystem so each hardware
generation and the shared receiver/backend are clearly separate:

- `MSV1/` -- the original 555-timer analog prototype (reference only; the paper predates this repo)
- `MSV2/` -- the XIAO nRF52840 moisture probe (arduino/, docs/, scripts/)
- `MSV3/` -- the E73-2G4M08S1C custom-PCB probe with FDC2214 sensing + solar harvesting
  (arduino/, hardware/, docs/, scripts/, procurement/)
- `DisplayCenter/` -- the shared ESP32-S3 receiver/display product that BOTH MSV2 and MSV3 probes
  report to (arduino/, hardware/, docs/, scripts/, ui/, assets/, generated/)
- `cloud/` -- the shared Supabase backend + Netlify-hosted PWA that serves all generations' data
  (web/, supabase/, netlify.toml, docs/)
- `docs/` (top level) and `scripts/` (top level) -- only genuinely cross-cutting material; anything
  about one specific subsystem lives inside that subsystem's own `docs/`/`scripts/`, not here

A file's location tells you what it's about -- check the owning subsystem's `docs/` before
top-level `docs/` when researching something generation- or product-specific.

## MSV3 nRF52840 builds

The MSV3 E73-2G4M08S1C PCB has no 32.768 kHz crystal. Its `P0.00`/`P0.01` pins are used by the PCB, so every MSV3 build must select the internal LFRC before FreeRTOS and the SoftDevice start.

- Never compile or upload an `MSV3/arduino/MSV3_*` sketch with a raw `arduino-cli` command or the Arduino IDE Upload button.
- Use `MSV3/scripts/arduino_msv3_probe.ps1`. It repairs/verifies the installed Adafruit variant, performs a clean build, supplies the required NFC-as-GPIO flags, and refuses unsafe states.
- Run `powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 -Action doctor` before diagnosing an MSV3 timing, BLE-init, watchdog, USB-disconnect, or repeated-reset symptom.
- The MSV3 sketches contain compile-time LFCLK guards. Do not remove or bypass them.
- Read `docs/KNOWN_ISSUES.md` and `MSV3/docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md` before changing clock, watchdog, BLE, sleep, or Arduino-core behavior.

An edit under `%LOCALAPPDATA%\Arduino15\packages` is not part of this Git repository. Never describe such an edit as durably resolved unless a tracked script reapplies/verifies it and tracked source fails safely when it is absent.

## Working in this repository as an automated agent

- This is a real, working hardware/firmware/cloud project (MSV1/MSV2/MSV3 plant moisture sensors, a shared DisplayCenter receiver, and a shared cloud backend), not a toy repo -- prefer reading the owning subsystem's `docs/` and existing code over guessing conventions.
- If this file was loaded by the `agent-supervisor` Claude-Codex collaboration harness, that tool is NOT part of this repository -- it lives externally (as of 2026-08-29, `Desktop\agent-supervisor` on the maintainer's machine) and was pointed at this repo via `--root`/`--config`. Do not edit its own billing-safety guards (`src/billing-safety.ts`) as part of an unrelated task -- that file lives in the harness's own repo, not here.
- When an automated or sandboxed host starts that harness's `src/live-server.ts`, launch the server process with outbound network access. A workspace-sandboxed parent can still serve the local UI but its Codex child fails every WebSocket and HTTPS attempt with Windows socket error 10013. Keep the agents' own read-only sandbox settings unchanged; they are separate from the provider transport's network access.
- When invoked through that harness, each call is bounded by a real turn/time/spend budget shared with a live user session. Do not re-explore files or directories you have already read this call, and do not exhaustively walk the whole repository when a targeted search for the relevant symbol/path will do. Prefer the smallest amount of investigation that lets you answer the current phase's question correctly.
- When the user explicitly asks to use `phone-a-friend` for a task, treat it as an ongoing tandem consultation. Start one stable `consultation_id`, continue it only after meaningful discovery/planning/implementation/verification checkpoints, and finish it with the completed result and test evidence before giving the user the final answer. The primary agent still owns all edits and decisions; send the friend compact new evidence instead of repeating the full transcript.
