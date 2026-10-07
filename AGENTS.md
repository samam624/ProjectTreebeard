# Repository instructions

Codex auto-loads this file from the repository root. Claude Code reads the equivalent
`CLAUDE.md` at the same path instead (its own auto-loaded convention) -- keep the two in sync
when either changes; they must state the same rules.

## READ THIS FIRST: `MSV3/fleet.json` is the only source of current physical state

**Every dated document in this repository is HISTORY. None of them describes the present.** They
record what was true on the date in their filename and are never retro-edited when hardware moves.
Reading them to infer where a probe is, what electrode is in its soil, what it is powered by or what
has been reworked is the single most repeated failure mode in this project.

**Before any analysis, any recommendation, or any claim about a probe, read
`MSV3/fleet.json`** and use these sections, which carry a `verified` date and a `source`:

- `probes[].physical_state` -- location, pot, room, power, the electrode ACTUALLY IN THE SOIL,
  readout, and `mods[]` (divider swap, zener, coating, C17)
- `non_msv3_nodes` -- sensors 1, 2, 3 and 9 report to the same receiver but are NOT MSV3 probes and
  have no bootloader/OTA entry. Their electrode and location are what analysis most often gets wrong.
- `pots` -- which sensors share soil. Two sensors in one pot see the same water; two in different
  pots do not, and must never be compared as if they did.
- `power_topology` -- which rigs share a mains outlet, and which comparisons that does and does not
  confound.
- `measurement_model` -- **read before analysing any thermal data.** The maintainer's physical
  reading is TWO-FACTOR: the readout and the electrode each contribute their own noise term and
  their own temperature term. FDC readout = low noise, temperature-susceptible. v1.2 driving
  circuit = noisy, low temperature susceptibility. MSV3 blade = VERY temperature-susceptible.
  v1.2 electrode = low. **Do not try to force a single-factor answer** ("is it the electrode OR the
  readout"); sessions have repeatedly got stuck doing that. It is both, which is why the apparent
  electrode effect changes size depending on which readout it is measured through.
- `msv4_boards` -- MSV4 boards: where each one is, what powers it, and any `active_test` running on
  it. They are NOT in `probes[]` and no OTA tool reads this section.

Rules:

1. **fleet.json wins over every document, always.** If a dated doc disagrees, the doc is stale.
   Do not "reconcile" them; correct or annotate the doc.
2. **If `verified` is older than the work being done, ASK the maintainer rather than assuming.**
   Hardware moves between sessions and nothing detects that automatically.
3. **When the maintainer states a physical fact, update `fleet.json` FIRST**, in the same turn,
   before writing analysis or docs that depend on it.
4. **Never infer physical state from telemetry alone.** A silent probe may be unplugged; a probe
   reading high may be on USB; a probe in sun may be a different probe than the notes say.
5. Field notes carry the same rule: `swd_accessible` and `physical_state` are MUTABLE PHYSICAL
   STATE, not history.

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
- **MSV4 boards are not in `probes[]` and `ble_ota_update.py` must never target one.** Use
  `MSV4/scripts/msv4_ble_ota.py <zip> --board N` (`--preflight-only` first), which reads the board
  from `msv4_boards` in `MSV3/fleet.json`. Prove the result with
  `MSV4/scripts/msv4_adv_scan.py --sensor <id> --require-progress` and `--read-build <address>`,
  then update that board's `installed_application_version` and `installed_build`. The bootloader's
  application DFU is single-bank: a failed transfer leaves the board in BLE DFU with no
  application, and the fix is to re-run with `--skip-jump`. Do not fall back to
  `ble_dfu_recover.py` on a weak link: at -78 dBm on 2026-10-06 it streamed a whole image past
  its 0.8 s receipt deadline and the bootloader refused it (status 6), twice.

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

## Before you patch vendored upstream code, find out what upstream actually does

Two days and twenty bootloader releases were spent on 2026-09-20/21 fixing races in the Adafruit
nRF52 serial DFU path. **None of those races exist upstream.** Stock 0.11.0 brings up exactly one
transport:

```c
if (_ota_dfu) { mbr_init_sd(); ble_stack_init(); }   // SoftDevice ENABLED
else          { usb_init(serial_only_dfu); }          // wired: SoftDevice NEVER enabled
```

so a stock wired session has no SoftDevice, synchronous flash, and no asynchronous erase to race.
MSV3 1.0.4 attached USB inside the SoftDevice-enabled branch, and every symptom chased afterwards
descended from that one local change. One look at that branch on day one would have replaced the
entire search.

So, when something works for everyone else and not here:

1. **Read the upstream source for the exact version pinned** under `MSV3/.arduino-build/`, and find
   the branch that makes it work for them.
2. **Ask what this repo changed.** `MSV3/bootloader/patches/` is the list of local deviations; the
   answer is usually in it.
3. Only then design a fix. A fix that teaches local code to survive a condition upstream never
   creates is almost always the wrong shape — prefer restoring the upstream invariant.

The same applies to host tooling. The vendored `adafruit-nrfutil` under
`MSV3/.arduino-build/dfu-pydeps/` is the reference for what the Arduino IDE actually sends; read it
before theorising about the wire protocol, and capture the bytes if in doubt. Doing that took ten
minutes and eliminated the host as a suspect outright.

## Trust measurement over inference, and verify the instrument first

Every wrong turn in that same investigation came from believing a reading instead of checking what
produced it:

- A retained trace with **no nonce** was read four times across four bootloaders and reported
  byte-identical. The conclusion drawn — "the fix changed nothing" — was wrong; the record had
  simply never been re-armed. `msv3_dfu_trace_arm()` now increments a nonce that is deliberately
  never cleared. **If the nonce has not moved since the last read, nothing else in the record is
  evidence.**
- An **inverted constant** in the reader (`MSV3_DFU_TRANSPORT_SERIAL` is `0x01`, BLE is `0x02`)
  reported a serial-transport record as a BLE one.
- Un-latched sentinel values were rendered as a confident "the ring has drifted" warning.
- Scan-response bytes were **reused across builds** without updating the decoder, four times. One
  produced a definite-sounding sentence about a field that did not exist in the build being read.

Rules that follow, and they are cheap:

- The scan-response layout is an **ABI** between `scan_response_init()` in `dfu_transport_ble.c` and
  `MSV3/scripts/msv3_read_dfu_trace.py`. Change one side, change the other, and key any reused byte
  on `build_id`.
- **Self-test a decoder against synthetic input** before trusting it against hardware.
- When a measurement contradicts the model, suspect the instrument before rebuilding firmware — a
  bootloader cycle here is ~20 minutes and a BLE-only recovery.
- State plainly which findings are **measured** and which are **inferred**. Three releases were
  built on an inference presented as a conclusion.

See [the wired DFU resolution](MSV3/docs/MSV3_WIRED_DFU_RESOLVED_2026-09-21.md) for the full
account.

## Working in this repository as an automated agent

- **Always push to `main`.** Never push a feature, topic, or working branch to GitHub. If work was
  created on another branch, integrate only the intended task commits onto the latest
  `origin/main`, verify them there, and push `main` while preserving unrelated local changes.
- This is a real, working hardware/firmware/cloud project (MSV1/MSV2/MSV3 plant moisture sensors, a shared DisplayCenter receiver, and a shared cloud backend), not a toy repo -- prefer reading the owning subsystem's `docs/` and existing code over guessing conventions.
- If this file was loaded by the `agent-supervisor` Claude-Codex collaboration harness, that tool is NOT part of this repository -- it lives externally (as of 2026-08-29, `Desktop\agent-supervisor` on the maintainer's machine) and was pointed at this repo via `--root`/`--config`. Do not edit its own billing-safety guards (`src/billing-safety.ts`) as part of an unrelated task -- that file lives in the harness's own repo, not here.
- When an automated or sandboxed host starts that harness's `src/live-server.ts`, launch the server process with outbound network access. A workspace-sandboxed parent can still serve the local UI but its Codex child fails every WebSocket and HTTPS attempt with Windows socket error 10013. Keep the agents' own read-only sandbox settings unchanged; they are separate from the provider transport's network access.
- When invoked through that harness, each call is bounded by a real turn/time/spend budget shared with a live user session. Do not re-explore files or directories you have already read this call, and do not exhaustively walk the whole repository when a targeted search for the relevant symbol/path will do. Prefer the smallest amount of investigation that lets you answer the current phase's question correctly.
- When the user explicitly asks to use `phone-a-friend` for a task, treat it as an ongoing tandem consultation. Start one stable `consultation_id`, continue it only after meaningful discovery/planning/implementation/verification checkpoints, and finish it with the completed result and test evidence before giving the user the final answer. The primary agent still owns all edits and decisions; send the friend compact new evidence instead of repeating the full transcript.
