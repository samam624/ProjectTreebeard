# History Chart: Retroactive, On-the-Fly Presentation Smoothing (2026-08-23)

## What changed

Previously, both History-chart series were baked in at ingest time by the receiver and
stored as fixed values: `display_moisture` (moisture) and `battery_percent` (battery, with
no raw counterpart stored at all). Changing the smoothing algorithm meant reflashing
firmware and waiting for new data - old rows kept whatever the filter produced at the time,
forever.

The receiver's local History tab (`MobileAppPage.h`, served from the receiver itself, e.g.
`10.0.0.35`) now recomputes both series **in the browser, from raw data, every time it
renders**. A new "Smoothing" panel exposes the filter's parameters directly; changing one
re-derives the entire currently-loaded history instantly, with no refetch and no reflash.
Defaults mirror the firmware constants and use the currently cached learned calibration span when
available. The UI now labels this honestly as a window-local simulation: history does not retain
the receiver's pre-window filter state or reboot boundaries, so it cannot reconstruct the
originally displayed line exactly.

This only affects the receiver's own local page. The Netlify dashboard
(`web/plant-dashboard`) is a separate frontend with no History-chart code of its own (see
`docs/RECEIVER_CLOUD_RELIABILITY_2026-08-23.md`) and is unaffected.

## Why battery needed a schema change and moisture didn't

`moisture` has always been the raw, unsmoothed receiver-derived measurement (that's exactly
what the diagnostic raw overlay added earlier tonight already read from). Nothing new had to
be stored for moisture.

Battery was different: only the receiver's own already-smoothed `battery_percent` was ever
stored per history row. There was no raw signal left in the database to recompute from.
Migration `032_msv2_raw_battery_history.sql` adds `raw_battery_mv`/`raw_charging` columns
and threads them through `msv2_store_sensor_reading`, `msv2_store_sensor_reading_batch`,
`msv2_read_sensor_history`, and `msv2_read_share_sensor_history` (all nullable/optional, same
backward-compatibility pattern as every prior migration in this history).

Firmware (`MSV2_ESP32_Display_Runtime.ino`) now carries `rawBatteryMv`/`rawCharging` through
the history upload queue (`PendingHistoryReading`, `enqueueHistoryReading()`) and into the
per-row JSON built by `snapshotJsonWithHistory()` - previously only the live `/data` and
"latest snapshot" paths included these fields; the actual per-history-row upload string
builder did not. `receiver-snapshot/index.ts` was extended to read and forward them on both
the batch and legacy single-row insert paths.

Rows that predate this migration, or that come from a probe still on the legacy no-raw
packet format (currently the Alocasia sketch, `arduino/MSV2_Moisture_BLE_Sleep_Alocasia`,
which sends a pre-smoothed on-probe battery percent with no raw mV/charging at all - see
`docs/HISTORY_SMOOTHING_WATERING_DEBOUNCE_2026-08-23.md`), simply have no raw battery
columns. The client falls back to that row's stored `battery_percent` as-is for those rows
and resets the filter state at the boundary, rather than fabricating continuity across two
different data sources.

## The client-side filter (`MobileAppPage.h`)

`smoothMoistureSeries()` and `smoothBatterySeries()` mirror
`smoothMoistureForDisplay()` and `calibrateRawBattery()` from the firmware, generalized so
the watering/charge-state confirmation count is a parameter instead of firmware's fixed 2
(see `docs/HISTORY_SMOOTHING_WATERING_DEBOUNCE_2026-08-23.md` for that debounce fix).
`confirmPackets: 1` on either reproduces the original un-debounced behavior for comparison.

An extra parameter with no firmware equivalent, `wetStalledMaxWakes` (default 0/disabled),
adds an optional symmetric escape hatch for ordinary upward moisture jitter - firmware
ignores that direction forever, which a live poll during this session caught in the act
(MSV3 sensor 4's `measuredMoisture` climbed steadily for 10+ minutes with `moisture`
[displayed] frozen - the same "stuck" behavior already diagnosed but not fixed in
`docs/RECEIVER_CLOUD_RELIABILITY_2026-08-23.md`). This parameter is the tool to experiment
with a fix for that retroactively, without touching firmware, before deciding whether it's
worth porting back.

Parameters exposed in the panel: moisture deadband/watering noise multipliers, watering
threshold (% of span), max dry-down step (% of span), watering confirm packets, dry-down
stall-escape packets, wet-side stall-escape packets; battery EMA alpha, charge-state confirm
packets. Values persist in `localStorage` (this is the receiver's own plain web page, not a
Claude Artifact - ordinary browser storage, no special capability involved) and a "Reset to
firmware defaults" button is provided. Stored/manual values are clamped to the same ranges shown
by the controls before use, so edited `localStorage` cannot inject negative or unbounded settings.

## Verification

- `node --check` on the extracted embedded JS: clean.
- Firmware compiled clean (`scripts\arduino_display.ps1 -Action compile`): 2,177,956 bytes
  flash (69%), 77,020 bytes RAM (23%).
- `node --test supabase/functions/receiver-snapshot/*.test.ts`: 14/14 passing, unchanged.
- Migration 032 applied: `supabase migration list --linked` showed only 032 pending before
  `supabase db push --linked`; confirmed applied after.
- `receiver-snapshot` edge function deployed via `supabase functions deploy receiver-snapshot`.
- Flashed to the physical receiver (COM16 / `10.0.0.35`); `/device` confirmed build
  `MSV2 runtime build 2026-08-23 retroactive-history-smoothing-v1` live.
- End-to-end proof: minted a share token via `/api/share-claim` and queried
  `/api/share-history` directly - newly-inserted sensor-3 rows carry
  `"raw_battery_mv":4200,"raw_charging":false` alongside `battery_percent`, confirming the
  full path (probe format already includes raw fields → firmware queue → cloud upload →
  Supabase storage → share-history read) works end-to-end.
- 2026-08-23 (full-repo review follow-up): `scripts/verify_battery_curve_sync.mjs` added -
  parses `BATTERY_CURVE`/IR-compensation constants out of both the firmware and the embedded
  JS port and fails the build if they ever drift, closing the "keep in sync" comment-only gap
  the review flagged. Currently passing (21 points, all 3 constants match).

## Not yet done

- The Alocasia probe itself (physical hardware) has not been reflashed with today's
  charging-debounce fix - it wasn't connected to this machine. Even once it is, its legacy
  packet format still won't produce `raw_battery_mv`/`raw_charging` (that would need the
  probe rewritten to send raw values, the same "raw-values rework" the regular MSV2 probe
  already got on 2026-07-28 - out of scope for tonight).
- No real multi-day soak of the new upload path; only a short live poll and the one E2E
  check above.
