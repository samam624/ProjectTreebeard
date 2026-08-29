# Project Treebeard

Three generations of a capacitive plant soil-moisture sensor, a shared receiver/display product,
and the cloud backend that ties them together.

```
MSV1/           Original 555-timer analog prototype (reference only)
MSV2/           XIAO nRF52840 moisture probe -- deployed, digitally-timed sensing
MSV3/           Custom-PCB probe: FDC2214 capacitance-to-digital front end + solar harvesting (WIP)
DisplayCenter/  Shared ESP32-S3 receiver/display -- BOTH MSV2 and MSV3 probes report to this
cloud/          Shared Supabase backend + installable Netlify PWA -- serves all generations' data
docs/           Cross-cutting docs only (status, known issues, roadmap, the IEEE evolution paper)
scripts/        Cross-cutting tooling only (BLE bench tools, datasheet fetcher, sensor plotting)
data/           Historical sensor logs, spanning probes/generations
```

Renamed from `MSV2` (2026-08-29) once the project outgrew a single generation's name -- see
`docs/paper/MSV_Evolution_IEEE_Paper.tex` for the full engineering retrospective across all three
hardware generations.

Each subsystem's own `docs/`/`scripts/`/`README.md` covers what's specific to it. Check there
first before top-level `docs/`.

## Live system

- App: <https://msv2-plants.netlify.app> (installable PWA; URL predates this rename, left as-is —
  renaming a live Netlify site is a separate, deliberate action, not part of this reorg)
- Backend: Supabase project `lyemomfpzkawqecpfufm`, config at `cloud/supabase/`
- Deploy config: `netlify.toml` (repo root) sets `base = "cloud"`, which is where the real build
  config (`cloud/netlify.toml`) and the app itself (`cloud/web/plant-dashboard/`) live

## Quick build reference

| Target | Guarded helper | Notes |
|---|---|---|
| MSV2 probe | `MSV2/scripts/arduino_probe.ps1` | XIAO nRF52840 Sense, Scindapsus/Alocasia sketches |
| MSV3 probe | `MSV3/scripts/arduino_msv3_probe.ps1` | **Never** flash with raw arduino-cli -- see `docs/KNOWN_ISSUES.md` |
| ESP32 display/receiver | `DisplayCenter/scripts/arduino_display.ps1` | Full guide: `DisplayCenter/README.md` |

For the MSV3 probe specifically: the E73-2G4M08S1C PCB has no 32.768 kHz crystal, so every build
must select the internal LFRC before FreeRTOS/SoftDevice start. The guarded helper enforces this;
read `docs/KNOWN_ISSUES.md` before touching clock/watchdog/BLE/sleep behavior on that board.

## Local LaTeX / paper tooling

The generic LaTeX compile/figure-import scripts used for `docs/paper/` live outside this repo at
`C:\Users\samca\Desktop\latex-tools\` (reusable across projects, not moisture-sensor-specific).
See `docs/paper/README.md`.

## Cross-project agent tooling

The Claude-Codex collaboration harness used for larger changes in this repo lives outside it at
`C:\Users\samca\Desktop\agent-supervisor\`, pointed at this repo via `--root`. See that folder's
own `README.md`.
