# MSV2 Adaptive Moisture Calibration

Status: deployed and hardware-verified on 2026-08-22.

**Superseded on the receiver 2026-10-11.** Receiver build `water-now-region-v1` asks the cloud
for the "water now" region instead (`receiver-snapshot?autocal=1&region=1`, migration 041,
`region.ts`), and its danger threshold is −21 %, not the −10 % in the table below. What this
document describes is still what the function answers without `region=1`. Why it was replaced,
and the replacement: `cloud/docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md`.

## Purpose

Each probe has its own raw scale, and snapshot upload cadence is not perfectly uniform.
Consequently, raw minimum/maximum values or percentiles over upload rows do not produce a
stable moisture percentage. A temporary fast-upload period can dominate row-weighted
statistics, and a reconnect transient can resemble a watering if only one upward step is
examined.

The adaptive model learns a dry point and wetting span from supported watering cycles while
continuing to use all uploaded history as a weak live prior. It does not predict watering
from a calendar schedule; alerts remain moisture-based.

## Data pipeline

1. `msv2_sensor_history` is limited to the trailing 21 days and the sensor's most recent
   manual calibration-reset epoch.
2. Uploads are reduced to one median moisture reading per five-minute bucket. A period with
   thousands of uploads therefore has the same time weight as any other five-minute period.
3. A positive step is only a watering candidate. It must have:
   - at least six supported buckets in the preceding two-hour baseline;
   - at least six supported buckets in the following three-hour response;
   - a sustained post-water elevation of at least 50% of the time-weighted range;
   - a just-before value consistent with the preceding baseline; and
   - for a mature event, at least twelve readings from 6-12 hours afterward and a retained
     elevation of at least 20% of the time-weighted range.
4. Candidate steps within twelve hours are one multi-pour watering. The model keeps the
   first supported dry point and highest five-minute-median wet peak.
5. Every valid cycle contributes to the learned dry point and wetting span. Evidence is
   weighted by context sample support and a seven-day recency half-life.
6. The five-minute 5th/95th percentiles are retained as a weak continuously changing prior.
   They are also the fallback before any supported watering exists.
7. Once at least three cycles exist, a median/MAD guard constrains an isolated endpoint or
   span outlier before the weighted average is calculated.

### New waterings

A new watering does not need to wait twelve hours before affecting the display. Once its
initial response has enough data, it enters as provisional evidence at 25% weight. Later
uploads either validate the 6-12-hour retention window and promote it to full evidence, or
remove it from the learned event set. The receiver pulls recalculated bounds approximately
every ten minutes.

## Readiness, confidence, and resets

The existing readiness gate remains at 50 five-minute buckets and three elapsed days. Until
then, a card uses its manual/default bounds and reports that auto-calibration is still
learning.

The edge response also reports:

- `calibrationSource`: `adaptive-watering-history` or `time-weighted-percentiles`;
- `wateringCycleCount` and `provisionalWateringCycleCount`;
- `effectiveCycleWeight`; and
- `calibrationConfidence`, an informational 0-100 score derived from effective evidence.

Existing firmware only requires `ready`, `dryBound`, and `wetBound`; the additional fields
are backward-compatible diagnostics. Moving a probe to a different plant, pot, or soil
still requires **Reset calibration** in the local Setup app. That advances the learning
epoch without deleting History-tab data.

## Alert semantics

The physical display, receiver-hosted phone app, production cloud dashboard, and mock cloud
server use the same moisture rules:

| Learned moisture | Appearance | Blink/buzzer meaning |
|---:|---|---|
| `11-100%` | normal/accent | none |
| `1-10%` | steady yellow | dry soon, but not overdue |
| `0%` | red | flashing and buzzer: at or below the learned pre-watering dry point |

Battery colors keep their separate historical thresholds. This change does not diagnose or
alter the periodic battery-percentage swings.

## 2026-08-22 production evidence

The recorded history exposed two false positives that the context checks now reject:

- Probe 3 on August 9 briefly moved approximately `1202 -> 851 -> 1282`. Its two-hour
  baseline never became dry and its post-event plateau did not represent a sustained
  watering response.
- Probe 1 produced short `278/289` square-wave plateaus that returned to baseline within
  the later retention window.

The simultaneous August 17 probe-2/probe-3 watering passes every window. At final
verification, the model resolved:

| Probe | Adaptive bounds | Raw reading | Calculated moisture |
|---:|---:|---:|---:|
| 1 | `276-289` fallback (no confirmed cycle after reset) | `278` | `15%` |
| 2 | `164-182` from two confirmed cycles | `170` | `33%` |
| 3 | `888-1303` from one confirmed cycle plus live prior | `984` | `23%` |

Probe 2 and probe 3 confidence remains moderate because only one confirmed watering is
shared. Additional supported waterings now refine the estimates instead of replacing them.

## Deployment and verification

Production contains migrations 024-027 and the matching `receiver-snapshot` edge function.
The cloud dashboard is the versioned `app.v15.js`/`msv2-cloud-app-v15` bundle. The physical
receiver `MSV2088A3104A7AC` was compiled, flashed on COM16, and verified at
`10.0.0.35/device` with build:

```text
MSV2 runtime build 2026-08-21 watering-cycle-alerts-v1
```

Useful checks:

```powershell
node --test supabase/functions/receiver-snapshot/autocal.test.ts
node scripts/verify_moisture_alert_thresholds.mjs
powershell -ExecutionPolicy Bypass -File scripts/arduino_display.ps1 -Action compile
supabase migration list --linked
```

The auto-calibration RPC remains service-role-only. Read it through the authenticated
`receiver-snapshot?receiverId=...&autocal=1` route rather than exposing the database RPC or
service-role key to firmware or browser code.

## Implementation map

- `supabase/migrations/024_msv2_time_weighted_watering_cycle_calibration.sql`: five-minute
  resampling and initial watering landmarks.
- `supabase/migrations/025_msv2_adaptive_multi_cycle_calibration.sql`: supported contextual
  watering detection.
- `supabase/migrations/026_msv2_watering_persistence_validation.sql`: later dry-down
  validation.
- `supabase/migrations/027_msv2_provisional_watering_evidence.sql`: low-weight provisional
  events while their retention window fills.
- `supabase/functions/receiver-snapshot/autocal.ts`: clustering, evidence weighting,
  multi-cycle robust estimation, readiness, and diagnostics.
- `supabase/functions/receiver-snapshot/autocal.test.ts`: model regressions.
- `scripts/verify_moisture_alert_thresholds.mjs`: cross-surface alert-contract check.
