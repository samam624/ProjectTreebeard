# Project Treebeard

Project Treebeard is a wireless plant-monitoring system built to answer a simple question: **does
this plant need water?** Small battery-powered probes measure moisture in the soil and send their
readings over Bluetooth to a nearby display. The display shows every plant at a glance and, when
connected to Wi-Fi, saves the readings so they can also be viewed from a phone.

This repository follows the project from its first analog experiment through the working system in
use today and the next-generation solar probe now being developed.

## How it works

```text
soil sensor  ->  wireless probe  ->  tabletop display  ->  cloud history  ->  phone app
                 Bluetooth           Wi-Fi receiver        Supabase           PWA
```

- **The probe** sits with the plant, measures the soil without exposed metal contacts, and sends a
  short Bluetooth update.
- **The DisplayCenter** listens for all nearby probes and gives the household one place to see plant
  moisture, battery level, and alerts.
- **The web app** keeps longer-term history, supports plant-specific calibration, and makes the same
  dashboard available on a phone.

The live web app is at **<https://msv2-plants.netlify.app>**. Its older `MSV2` address has been kept
so existing installations and saved links continue to work.

## Three generations of the idea

| Generation | What it was built to learn | Where it stands |
| --- | --- | --- |
| **MSV1** | Could a low-cost capacitive sensor detect the difference between dry and wet soil? The original 555-timer circuit proved that it could, while also exposing the limits of its analog output stage. | Completed proof of concept |
| **MSV2** | Could the measurement become a practical wireless product? An nRF52840 probe, ESP32-S3 display, web app, history, alerts, and automatic calibration turned the experiment into a connected system. | Deployed and in active use |
| **MSV3** | Could a purpose-built probe improve precision and battery life? The new custom board uses an FDC2214 measurement chip, electrode switching, and solar-assisted power. | Working hardware; long-term soil and energy testing remains |

MSV2 and MSV3 both report to the same DisplayCenter and web app. MSV3 is an evolution of the probe,
not a separate product.

## Explore the project

| Folder | What you will find |
| --- | --- |
| [`MSV1/`](MSV1/) | The original analog prototype and its final engineering report |
| [`MSV2/`](MSV2/) | Deployed wireless-probe firmware, test sketches, and calibration notes |
| [`MSV3/`](MSV3/) | Custom probe firmware, PCB and enclosure files, procurement records, and bring-up results |
| [`DisplayCenter/`](DisplayCenter/) | ESP32-S3 receiver/display firmware, interface assets, hardware, and test tools |
| [`cloud/`](cloud/) | Supabase database migrations and the installable plant-dashboard web app |
| [`docs/`](docs/) | Project status, known issues, roadmap, and the three-generation engineering paper |
| [`scripts/`](scripts/) | Shared Bluetooth logging, plotting, and other cross-project utilities |
| [`data/`](data/) | Recorded sensor logs used for investigation and validation |

For the narrative engineering retrospective, start with the
[`MSV Evolution` paper draft](docs/paper/MSV_EVOLUTION_IEEE_PAPER_DRAFT.md). For current work and
open questions, see the [`status handoff`](docs/STATUS_HANDOFF.md) and
[`product roadmap`](docs/PRODUCT_ROADMAP.md).

## Building the firmware

The checked-in PowerShell helpers select the correct board settings and keep build files out of the
source folders. Run them from the repository root or from their own `scripts` folder.

| Target | Build command |
| --- | --- |
| MSV2 wireless probe | `powershell -ExecutionPolicy Bypass -File MSV2/scripts/arduino_probe.ps1 -Action compile` |
| DisplayCenter receiver | `powershell -ExecutionPolicy Bypass -File DisplayCenter/scripts/arduino_display.ps1 -Action compile` |
| MSV3 custom probe | `powershell -ExecutionPolicy Bypass -File MSV3/scripts/arduino_msv3_probe.ps1 -Action compile` |

> **Important for MSV3:** never compile or upload an MSV3 sketch directly with Arduino IDE or a raw
> `arduino-cli` command. Its board has no 32.768 kHz crystal, so the guarded helper must select and
> verify the internal clock before the Bluetooth and scheduling code starts. Read
> [`Known issues`](docs/KNOWN_ISSUES.md) before changing its clock, sleep, watchdog, or BLE behavior.

The root [`netlify.toml`](netlify.toml) publishes the static app in
[`cloud/web/plant-dashboard/`](cloud/web/plant-dashboard/). Backend setup and migration instructions
are in [`cloud/docs/SUPABASE_SETUP.md`](cloud/docs/SUPABASE_SETUP.md).

## Maintainer notes

The reusable LaTeX tooling for [`docs/paper/`](docs/paper/) lives outside this repository at
`C:\Users\samca\Desktop\latex-tools\`. The Claude-Codex collaboration harness also lives outside the
repository at `C:\Users\samca\Desktop\agent-supervisor\`; neither external tool is required to browse
the project or run its firmware and web tests.
