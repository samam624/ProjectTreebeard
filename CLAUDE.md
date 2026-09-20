# Repository instructions

Codex auto-loads the equivalent `AGENTS.md` at the same path instead of this file (its own
auto-loaded convention) -- keep the two in sync when either changes; they must state the same
rules.

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

## MSV3 BLE OTA sessions

- Use the repository-root `scripts/ble_ota_update.py` with the portable Python under
  `MSV3/.arduino-build/python-portable/runtime/python.exe`. Target a fleet probe with `--probe N`;
  never replace the registry-derived address with a guessed name or generic `AdaDFU` scan.
- Run `--preflight-only` first. It is safe inside a workspace sandbox and must pass package
  integrity, signature, identity, device-type, rollback, and boot-guard pairing checks before any
  live attempt.
- On Codex or another sandboxed Windows host, run the LIVE updater outside the workspace sandbox
  (Codex `exec_command` with `sandbox_permissions=require_escalated`). Measured on 2026-09-11:
  passive WinRT advertisement scans worked inside the sandbox, but every active GATT device open
  failed with `E_ABORT`/`E_FAIL`; the same checked command connected and completed DFU immediately
  outside it. This permission changes only host BLE access and does not bypass the updater's
  firmware or fleet safety checks.
- After `DFU Complete`, prove the exact DIS build ID and observe at least two advancing wake
  counters with `scripts/msv3_fleet_scan.py --probe N --require-progress`. Only then update that
  probe's `installed_application_version` in `MSV3/fleet.json`.
- Do not use `--allow-identity-mismatch`, `--allow-downgrade`, `--skip-jump`, or
  `--reflash-same-build` merely to make an ordinary field update proceed. Each is a deliberate
  exceptional path whose precondition must already be established.

## SWD sessions: never issue a bare serial command to the flasher

The maintainer holds pogo pins against castellated pads BY HAND for the whole of every SWD
operation. Two failures on 2026-09-07 came from ignoring that:

- A hand contact that passes one instantaneous check can drop mid-write. Every operation that
  touches a target -- erase, program, AND read -- must first wait for **3 continuous seconds of
  stable, consistent-DPIDR contact**. `wait_for_contact()` in `scripts/swd_flash_hex.py` is the one
  implementation; import it, never re-roll it.
- The gate used to be opt-in (`--bootloader` implied it, plain `--hex --mass-erase` did not), so it
  was missing exactly when nobody thought to ask. It is now the DEFAULT in every target-touching
  script, with an explicit `--no-wait-contact` for a fixed jig.

**Do not hand the maintainer a `python -c "...serial.Serial(...)..."` one-liner that talks to the
flasher.** It has no contact gate, so it is the one shape guaranteed to reintroduce this. If a new
capability is needed, add it to a script that goes through the gate:

- `scripts/swd_flash_hex.py`  -- erase + program (`--msv3 <version>` verifies the hex against its
  release manifest before writing, and resets the target afterwards)
- `scripts/swd_dump.py`       -- bulk read / forensic capture
- `scripts/swd_nvmc_probe.py` -- NVMC and protection register inspection

Chain steps INSIDE one script invocation rather than across several, so one hold covers the whole
sequence (`swd_dump.py --eraseall-diag` is the pattern).

**Mass erase goes through CTRL-AP, not NVMC.** Writing `NVMC_ERASEALL` over the AHB-AP is ACKed at
the SWD layer but faults on the AHB bus -- measured on nRF52840, DP CTRL/STAT `0xF0000060` with
STICKYERR set, no busy state, flash untouched. `erase: OK` was printed anyway and a board came out
still running its previous application. Any erase must PROVE its postcondition by blank-checking
before reporting success; a status string from the adapter is not evidence.

## Working in this repository as an automated agent

- This is a real, working hardware/firmware/cloud project (MSV1/MSV2/MSV3 plant moisture sensors, a shared DisplayCenter receiver, and a shared cloud backend), not a toy repo -- prefer reading the owning subsystem's `docs/` and existing code over guessing conventions.
- If this file was loaded by the `agent-supervisor` Claude-Codex collaboration harness, that tool is NOT part of this repository -- it lives externally (as of 2026-08-29, `Desktop\agent-supervisor` on the maintainer's machine) and was pointed at this repo via `--root`/`--config`. Do not edit its own billing-safety guards (`src/billing-safety.ts`) as part of an unrelated task -- that file lives in the harness's own repo, not here.
- When an automated or sandboxed host starts that harness's `src/live-server.ts`, launch the server process with outbound network access. A workspace-sandboxed parent can still serve the local UI but its Codex child fails every WebSocket and HTTPS attempt with Windows socket error 10013. Keep the agents' own read-only sandbox settings unchanged; they are separate from the provider transport's network access.
- When invoked through that harness, each call is bounded by a real turn/time/spend budget shared with a live user session. Do not re-explore files or directories you have already read this call, and do not exhaustively walk the whole repository when a targeted search for the relevant symbol/path will do. Prefer the smallest amount of investigation that lets you answer the current phase's question correctly.
- When the user explicitly asks to use `phone-a-friend` for a task, treat it as an ongoing tandem consultation. Start one stable `consultation_id`, continue it only after meaningful discovery/planning/implementation/verification checkpoints, and finish it with the completed result and test evidence before giving the user the final answer. The primary agent still owns all edits and decisions; send the friend compact new evidence instead of repeating the full transcript.
