# Project Treebeard Display Receiver — Cloud Upload Reliability & Telemetry (2026-08-23)

## Starting point

User-reported symptoms on the display receiver (`arduino/Treebeard_ESP32_Display_Runtime`):
periodic unexplained reboots, the local History tab lagging the physical display/Status
view, and moisture values appearing "stuck" for long stretches. An earlier same-day
Claude+Codex collaboration session (via `tools/agent-supervisor`) produced a synthesized
7-step remediation plan (`.agent-runs/2026-08-23T06-12-38-244Z-f739b2e4/plan.md`), but
**both implementation attempts died before writing a full fix**: `claudeMaxBudgetUsdPerCall:
2` in the new `tools/agent-supervisor/src/billing-safety.ts` guard is too low for a
repo/task this size — one run's implementation call and the next run's proposal call both
hit "Reached maximum budget ($2)" and aborted. This is a real limitation of that tool worth
raising before trying that route again on a task this size.

## What was already done before this pass (verified in code, not just the log)

Of the plan's 7 steps, 3 had real, correct partial implementation already in the working
tree from the aborted collaboration run:

- **Step 4 (cross-core `sensors[]` race)** — `sensorsMux` critical section wraps both the
  BLE-task write (`onResult()`) and the read side (`sensorDataJson()`). Correct and complete.
- **Step 5 (upload latency)** — `CLOUD_SYNC_DEFAULT_SECONDS` dropped 300 -> 60, plus a
  backlog-triggered nudge with a hard `CLOUD_MIN_UPLOAD_SPACING_MS = 60000` floor so it
  cannot recreate the 2026-08-11 packet-triggered contention regression.
- **Step 7 (filter correction), partial** — `MOISTURE_MAX_STALLED_WAKES = 20` bounded
  hold-time escape hatch added to `smoothMoistureForDisplay()`. Logic only; no regression
  test existed (plan required one).

Not started: step 2 (telemetry), step 3 (root-cause/hardening), step 6 (incremental
History polling + backend endpoint), step 7's test, step 8 (soak).

## Bug found in the "done" work: config migration gap

`loadCloudConfig()` always preferred a value already persisted in `/cloud.json` over the
compiled default — so a receiver that had ever saved `syncIntervalSeconds: 300` would keep
loading 300 forever, silently defeating the step-5 fix on reflash. Confirmed live: the real
receiver (`10.0.0.35`) reported `syncIntervalSeconds: 300` before this fix, despite the new
firmware's compiled default already being 60.

**Fix**: one-time migration in `loadCloudConfig()` — if the stored config predates a new
`syncIntervalMigratedV2` marker and its value is still exactly the old default (300), bump
it to 60; any value the user set deliberately (including 300, on purpose) is left alone.
Every future save stamps the marker so this never re-runs. Verified live post-flash:
`/device` now reports `syncIntervalSeconds: 60`.

## Step 2: telemetry added

- Separate RTC checkpoints for BLE-scan and cloud-sync tasks (`lastCheckpointBle`,
  `lastCheckpointCloud`) — the original `lastCheckpoint` only ever recorded boot/setup and
  the main-loop render path (confirmed: every prior call site was in `setup()` or `loop()`'s
  render section).
- Intentional-restart marker (`intentionalRestartPending`, RTC-backed) set immediately
  before both `ESP.restart()` call sites, consumed/cleared at the top of the next `setup()`.
- Live heap: `ESP.getFreeHeap()` sampled once per `loop()` iteration and immediately
  before/after the actual snapshot-upload POST call, plus a running `minFreeHeapEverBytes`
  and `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`.
- BLE-task and cloud-task stack high-water marks (`uxTaskGetStackHighWaterMark`).
- History-queue high-water mark alongside the existing depth/drop counters.
- One-time-per-boot compact record to LittleFS (`/boot-record.json`) capturing the
  *previous* boot's reset reason/checkpoints before this boot overwrites them in RTC memory
  — survives a full power loss, which RTC memory does not.
- All exposed under a new `health` object in the existing `/device` endpoint. No new
  endpoint added, per the plan's explicit instruction.
- **Fixed en route**: `resetReasonName()`'s switch was missing ESP32-S3-specific enum
  values (`ESP_RST_USB`, `ESP_RST_JTAG`, `ESP_RST_EFUSE`, `ESP_RST_PWR_GLITCH`,
  `ESP_RST_CPU_LOCKUP`) — the very first real reset on this board (an S3) came back
  `"unknown"` until this was added. Confirmed fixed: a subsequent flash-triggered reset
  correctly reported `"USB peripheral reset"`.

## Root cause of the live "connection refused" failures

With telemetry live, caught real, unrecovered heap loss on failed snapshot-upload attempts
(11-44KB per attempt, bracketing the actual POST call) rather than a clean failure. Traced
to `runCloudSnapshotUploader()`'s `!began` branch (when `http.begin()` itself fails) having
**no cleanup at all** — no `http.end()`, no `secureClient.stop()`.

### A mistake made and corrected during this pass

First attempt: added a low-heap guard skipping upload attempts below a floor, set at 40960
bytes. A live 12-minute background monitor showed this insufficient — "connection refused"
recurred for 13 consecutive attempts, queue climbing 28 -> 111, with success and failure
observed at *nearly identical* free-heap levels (e.g. success at 90384 bytes, failure at
91436 bytes with the same `largestFreeBlockBytes`). That kills a simple "heap level predicts
failure" theory.

Reacted by raising the floor to 122880 bytes, reasoning "more margin = safer" **without
checking it against the receiver's actual normal operating range**. This was wrong: normal
steady-state free heap on this receiver mostly sits at 80-95KB, so the new floor blocked
**100% of upload attempts** (`"skipped: low heap"` on every poll) — worse than the bug it
was meant to fix. Caught within ~3 poll cycles via the live monitor and reverted to 40960
rather than keep re-guessing a number the evidence didn't support.

### The actual fix

The real, more consistent culprit: `runCloudSnapshotUploader()` was the *only* one of five
cloud-network functions (`runCloudBootstrap`, `runCloudDisplaySettings`,
`runCloudConfigPull`, `runCloudAutoCalPull`, `runCloudSnapshotUploader`) with any defensive
cleanup on its failed-connection path. All five share the same limited TCP/TLS resource
pool. Applied the same `http.end()` / `secureClient.stop()` / `plainClient.stop()` cleanup
to all five, on every exit path (including every `!began` early return, several of which
had no cleanup at all before this).

Also added exponential backoff on **consecutive** upload failures
(`cloudConsecutiveUploadFailures`, doubling from the existing 17s base, capped at
`CLOUD_UPLOAD_BACKOFF_MAX_MS = 180000`, resets to 0 on the next success) — the fixed 17s
retry alone let 7+ failures in a row hammer a connection that was already failing.

`CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES` was left at the original conservative 40960 — kept as a
backstop for genuinely catastrophic heap states, not as the primary fix.

### Verification

Live 12-minute background monitor across the whole sequence: 3 clean successes -> 13
consecutive "connection refused" failures (pre-fix) -> 14 polls 100%-blocked (the
122880-threshold mistake) -> 9 polls post-real-fix, 7 clean `200`s and 2 ambiguous
`status=0` (very likely a poll caught mid-attempt, not real failures), zero "connection
refused" recurrence. Confirmed separately, ~10+ minutes after the monitor ended, that
uploads were still succeeding (`200`, `cloudConsecutiveUploadFailures: 0`).

**This is NOT a multi-day soak.** The plan's own success criteria call for 24-48+ hours;
what exists here is roughly 15 minutes of clean post-fix data. Treat as "meaningfully
improved and root-caused," not "proven."

## Netlify / History-tab investigation

Initially suspected the 2026-08-17 stale-service-worker-cache bug (documented, fixed in the
working tree, previously undeployed per repo history) might still be live. Checked directly:
`msv2-plants.netlify.app` is **already serving the fixed `v15`** service worker and asset
bundle — not a deploy gap. The actual page the user's screenshots came from is the
receiver's own local UI (`MobileAppPage.h`, served from `10.0.0.35`), which has its own,
separate History-tab chart code — `web/plant-dashboard/assets/app.v15.js` (the Netlify app)
does not contain this chart code at all. Two genuinely separate frontends; don't conflate
them again.

## MSV3 "stuck for 10 minutes" — diagnosed, not yet fixed

Live `/data` showed `measuredMoisture` (raw) actively differing from the displayed value
(e.g. 43501 vs 43489) at the moment this was checked — the raw signal is moving; the filter
is suppressing it, which is the design intent. But re-reading `smoothMoistureForDisplay()`'s
escape-hatch logic found a real weakness: `moistureStalledWakes` resets to 0 on **any**
upward tick, even one far smaller than the watering threshold, not just a real rise. For a
sensor with roughly symmetric noise (ticks up about as often as down), getting 20
*consecutive* non-upward readings is comparatively rare — so in practice the escape hatch
can take far longer than "20 wakes" to fire, sometimes effectively never. This sensor's
observed wake cadence (~30s) makes "~10 minutes flat" land right around 20 wakes, i.e.
exactly where this weakness bites hardest. **Not fixed yet** — flagged to the user, offered
as a small contained change (make the counter tolerant of small noise instead of
zeroing on any uptick), not yet actioned.

## Added: raw-value diagnostic overlay on the History chart

`MobileAppPage.h`'s `renderHistoryChart()` now optionally draws a second series when points
carry a `.raw` field: a dashed amber line (the unsmoothed `moisture` column) alongside the
existing solid blue displayed line (`display_moisture` falling back to `moisture`). Both
series come from the same row/timestamp in one pass, so they can't drift out of alignment.
Hover tooltip and the "latest" summary both show `displayed (raw N)` when raw data is
present. This only affects the receiver's own local page, not the Netlify dashboard.

## Build tags, in order (all flashed to COM16 / `10.0.0.35` tonight)

1. `2026-08-23 reboot-telemetry-v1` — step 2 telemetry.
2. (same build, reflashed) — `resetReasonName()` ESP32-S3 enum fix.
3. `2026-08-23 low-heap-upload-guard-v1` — first heap-guard attempt (40960 floor).
4. `2026-08-23 low-heap-upload-guard-v2` — **the mistake**: floor raised to 122880, blocked
   100% of uploads. Corrected in v3, not left deployed.
5. `2026-08-23 cloud-cleanup-v3` — floor reverted to 40960; cleanup added to all 5 cloud
   functions; exponential backoff added. **This is the real fix.**
6. `2026-08-23 history-raw-overlay-v1` — raw-value diagnostic overlay on the History chart.
   **Currently live.**

## Explicitly not done tonight

- **`displaySync` HTTP 400** — has failed every attempt since ~13s after boot, across every
  reflash tonight (one early success at boot, then continuous 400s). Never investigated —
  root cause (payload/auth mismatch against the display-settings endpoint, most likely) is
  still unknown.
- **Step 6** (incremental History polling on the Netlify dashboard + a new Supabase
  Edge Function query surface) — untouched. Per the plan's own risk list, this needs
  explicit user sign-off before any deploy, independent of this repo's general
  Netlify/Supabase hold history.
- **Step 7's regression test** — the escape-hatch weakness above is a good concrete target
  for it, but no test exists yet, and no decision was made on the test-harness approach
  (host-buildable C++ extraction vs. a TS fixture mirroring the existing Deno test pattern —
  both discussed, neither chosen).
- **The escape-hatch symmetric-noise fix itself** — diagnosed and explained, not implemented.
- **A real multi-day soak** — the plan's own bar for calling any of this resolved.
- **`tools/agent-supervisor`'s `$2`/call budget cap** — still there; will kill any future
  collab run attempted on a task this size the same way it did tonight.

## Full-directory review remediation (later 2026-08-23)

- The dry-side escape-hatch bug above is fixed in source: ordinary sub-watering upward jitter no
  longer resets accumulated evidence, and the twentieth qualifying dry-side packet releases the
  deadband. `scripts/verify_receiver_moisture_filter.mjs` guards the former failure mode.
- The displaySync POST failure was traced to `msv2_upsert_receiver_display_settings()` using
  `on conflict (receiver_id)` inside a `RETURNS TABLE` PL/pgSQL function that also defines a
  `receiver_id` output variable. Migration 033 switches to the named primary-key constraint.
  Firmware now retains the backend's JSON error message beside the HTTP status.
- **Later live recovery:** migration 033 was applied and the receiver was flashed with
  `live-history-recovery-v2`. The live 512-row queue had already dropped 2,186 readings; after 033
  stopped the HTTP 400 loop it partially drained, but the old 32-row sender then reproduced
  `send payload failed`. The recovery build uses 16-row batches, backs off failed display writes,
  and prioritizes history TLS work above a 75% queue watermark. Post-flash snapshot/display sync
  both returned HTTP 200 and a direct backend read reached 17:00:31 PDT. A real 24-48-hour soak is
  still required.
