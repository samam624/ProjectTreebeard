# MSV2 Status Handoff

Date: 2026-08-23 (the section immediately below is current; older deployment-status notes are historical)

## 2026-08-23 Full-Directory Review Remediation and Live History Recovery

- **Live recovery at 17:00 PDT:** the History chart stall was ingestion, not rendering. The live
  receiver had a full `512/512` RAM queue, 2,186 dropped readings, repeated snapshot
  `connection refused`/`send payload failed` errors, and a pending display write retrying through
  the migration-019 HTTP 400 bug.
- Applied migration 033 to the linked Supabase project. Display sync immediately changed to HTTP
  200 and the old queue began draining, proving the one-second failing display/TLS loop was a
  direct contributor.
- Reduced receiver history batches from 32 to 16 rows, added exponential display-sync failure
  backoff, and reserve cloud TLS work for history whenever the queue is at least 75% full. Flashed
  `MSV2 runtime build 2026-08-23 live-history-recovery-v2` to COM16 with LittleFS preserved.
- Post-flash `/cloud` reported snapshot HTTP 200, display HTTP 200, zero drops, the 16-row batch
  size, and roughly 82-85KB free heap. A direct share-history read returned sensor-3 rows through
  `2026-08-24T00:00:31.187Z` (17:00:31 PDT); its temporary verification token was revoked.
- The 2,186 readings dropped before recovery and the remaining stale RAM queue discarded by the
  approved flash cannot be reconstructed. Current ingestion is live again.

- Closed the receiver's partial `sensors[]`/`autoCal[]` synchronization fix: BLE now publishes
  filter state and packet fields as one update, while render, `/data`, cloud, and auto-calibration
  callers take short value snapshots under the matching mutex. Duplicate advertisement frames are
  rejected before either the moisture or battery filter mutates.
- Fixed the moisture dry-side escape hatch so ordinary sub-watering upward jitter preserves
  accumulated dry-side evidence. The capped convergence step now occurs on the twentieth
  qualifying packet, with a host regression verifier covering symmetric alternating noise.
- Bumped the receiver identity to `2026-08-23 live-history-recovery-v2`; this source can no longer
  masquerade as the already-flashed `retroactive-history-smoothing-v1` binary.
- Root-caused the continuous display-setting POST HTTP 400 to the PL/pgSQL upsert's ambiguous
  `on conflict (receiver_id)` target. Migration 033 uses the named primary-key constraint;
  receiver telemetry now also includes the Edge Function error text on failures.
- MSV3 production defaults now disable serial logging/USB-host wait and the temporary overnight
  LED load. Real wet-soil threshold/IDRIVE tuning remains a physical validation gate.
- Automation now fails closed: Arduino wrappers throw on CLI failures and compile before upload,
  the orphaned SD helper no longer names a nonexistent default sketch, the agent supervisor cannot
  approve failed/timed-out required tests, dashboard verifiers discover the active bundle, and fresh
  Supabase setup runs through the highest migration (currently 033).
- **Deployment state:** migration 033 and the receiver recovery build are live. The separate MSV3
  production firmware has not been reflashed; wet-soil plus 24-48-hour soak checks remain external
  work.

## Historical: 2026-08-23 Receiver Cloud Reliability, Telemetry, and History Raw Overlay

- User reported periodic reboots, History-tab lag, and stuck moisture values. A same-day
  Claude+Codex collaboration session (`tools/agent-supervisor`) synthesized a 7-step plan
  but both implementation attempts died from `billing-safety.ts`'s `$2`/call budget cap —
  too low for a task this size. Worth raising before trying that route again.
- Of the plan, 3 steps were already correctly implemented in the working tree from the
  aborted run (cross-core `sensors[]` race fix, 60s upload-latency default with a backlog
  nudge, and the moisture filter's bounded stalled-wake escape hatch). Found and fixed one
  real bug in that work: a persisted `syncIntervalSeconds: 300` would never pick up the new
  60s default on reflash — added a one-time migration.
- Added step-2 telemetry: per-subsystem RTC checkpoints, an intentional-restart marker,
  live/min free-heap and largest-free-block tracking, BLE/cloud task stack high-water
  marks, a history-queue high-water mark, and a one-time-per-boot LittleFS boot record —
  all exposed under a new `health` object in `/device`. Fixed `resetReasonName()` missing
  ESP32-S3-specific reset reasons (first real reset came back "unknown" without this).
- Root-caused the live "connection refused" upload failures using that telemetry: all five
  cloud-network functions (bootstrap, display-settings, config-pull, auto-cal-pull,
  snapshot-upload) share a limited TCP/TLS resource pool, and only one of the five had any
  cleanup (`http.end()`/`stop()`) on its failed-connection path. Fixed all five; added
  exponential backoff on consecutive failures. A first attempt at a low-heap guard was
  raised too high (122880 bytes) without checking it against the receiver's real ~80-95KB
  normal operating range, and blocked 100% of uploads for a few minutes before being caught
  and reverted — documented in full in the detailed doc below, including the mistake.
- Confirmed Netlify (`msv2-plants.netlify.app`) is already serving the fixed `v15` service
  worker from the 2026-08-17 stale-cache bug — not a deploy gap. The screenshots in this
  investigation were actually the receiver's own local page (`MobileAppPage.h` at
  `10.0.0.35`), a separate frontend from the Netlify dashboard.
- Diagnosed (not yet fixed) a real weakness in the moisture filter's escape hatch: it resets
  its stalled-wake counter on any upward tick, even sub-deadband noise, so it can take far
  longer than "20 wakes" to fire against symmetric noise.
- Added a raw-value diagnostic overlay to the receiver's local History chart
  (`MobileAppPage.h`) — dashed line showing the unsmoothed `moisture` value alongside the
  existing smoothed `display_moisture` line, same timestamps, shared tooltip.
- Current live build: `MSV2 runtime build 2026-08-23 history-raw-overlay-v1`, flashed to
  `COM16`/`10.0.0.35`, compiled at 2,160,164 bytes flash / 74,940 bytes RAM.
- **Not done**: `displaySync`'s HTTP 400 (failing continuously, never investigated), the
  History tab's incremental-polling backend work (step 6, needs its own deploy sign-off),
  the escape-hatch fix itself, a real regression test for the filter, and — most
  importantly — a real multi-day soak. Only ~15 minutes of clean post-fix data exists as of
  this handoff.
- Full write-up, including the mistake made and corrected mid-session:
  `docs/RECEIVER_CLOUD_RELIABILITY_2026-08-23.md`.

## 2026-08-22 MSV3 High-Resolution Moisture Follow-Up

- Replaced the initial `(25M - raw) / 1000`-equivalent receiver mapping with
  `clamp_u16((24,000,000 - rawFdcCounts) / 100)`. The nail-polish-covered probe's measured
  20M dry -> 19M wet span now produces 40,000 -> 50,000: 10,000 useful derived steps rather
  than 1,000. The roughly 3,000-count bench noise becomes roughly 30 derived steps for the
  receiver presentation filter to suppress.
- Migration 029 adds `raw_fdc_counts`, `raw_moisture_khz`, and `moisture_scale` to durable
  history. The original 28-bit FDC value is now retained in Supabase, not just BLE/serial/
  local `/data`.
- Scale transitions are isolated automatically: the history insert RPC starts a fresh
  per-sensor calibration epoch when `moisture_scale` changes, and history readers chart only
  the latest scale. Old rows are preserved but cannot masquerade as a watering jump.
- Migration 029 and `receiver-snapshot` are deployed. Receiver build
  `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2` compiled at 2,133,296 bytes flash /
  58,508 bytes RAM and was flashed successfully to COM16 with hash verification.
- Live receiver verification: MSV3 raw `20,243,803` maps to measured/display `37,561` at
  scale 2, exactly matching the new formula.
- Live end-to-end verification after the scheduled cloud upload: HTTP 200; raw FDC
  `20,243,185` produced measured `37,568` while the smoothed display held at `37,561`.
  A forced normal auto-cal refresh then changed sensor 4's cloud-derived bounds from the
  old ~4,760 scale to `37,574`, proving the scale-2 history insert and automatic reset both
  executed successfully in production.

## 2026-08-22 Global Receiver Moisture Smoothing

- All probe generations now share one receiver-side presentation filter after protocol
  conversion: legacy calibrated packets, MSV2 raw-kHz packets, and MSV3 wide FDC packets.
  Normal upward jitter is ignored, drying follows downward through a learned deadband and
  capped step, and a significant rise snaps immediately as watering.
- Measurement truth is preserved. Receiver/API `moisture` is the smoothed display value and
  `measuredMoisture` is the unsmoothed derived value. Supabase history keeps the latter in
  its existing `moisture` column (so auto-calibration is unchanged) and migration 028 adds
  `display_moisture` for the user-facing curve. Old rows/firmware fall back compatibly.
- Migration 028 is applied, `receiver-snapshot` is deployed, and Netlify production is
  deployed as `app.v15.js` / `msv2-cloud-app-v15`.
- Receiver firmware `MSV2 runtime build 2026-08-22 receiver-moisture-smoothing-v1` compiled
  at 2,132,916 bytes flash / 58,508 bytes RAM and was flashed successfully to COM16 with
  hash verification. Live `/device`, `/data`, and `/cloud` checks passed; both cloud upload
  and display sync returned HTTP 200.
- Full design, threshold, data-contract, and rollout details:
  `docs/RECEIVER_MOISTURE_SMOOTHING_2026-08-22.md`.

## 2026-08-22 Adaptive Multi-Cycle Moisture Calibration

- Migrations 024-027 and `receiver-snapshot` are deployed. History is resampled into one
  median per five-minute bucket, so changing upload cadence cannot bias the learned range.
- A watering is now a supported time-series event, not one upward sample: two hours of
  pre-water context, three hours of post-water response, and 6-12 hours of retained
  elevation are checked. The probe-3 August 9 `1202 -> 851 -> 1282` transient and probe
  1's short square-wave plateaus are rejected; the retained August 17 probe-2/probe-3
  watering remains.
- `autocal.ts` combines all valid 21-day watering cycles. Five-minute distribution bounds
  are a weak live prior, events are weighted by evidence and seven-day recency, and after
  three cycles a median/MAD guard prevents one outlier from taking over. New events enter
  provisionally at 25% weight and are promoted or removed when their retention window
  fills, so every upload can improve the estimate without immediately trusting a spike.
- Current replayed model bounds are approximately probe 2 `164-182` and probe 3
  `888-1303`; raw readings `170` and `984` resolve to about `33%` and `23%`. Probe 3 has
  one confirmed cycle versus probe 2's two, so their reported model confidence is still
  intentionally moderate and will improve with additional real waterings.
- Moisture alert semantics now match on every surface: `11-100%` normal, `1-10%` steady
  yellow, and exactly `0%` red/flashing/buzzer. Battery thresholds are intentionally
  separate and unchanged. The periodic battery-percentage swings remain a separate open
  investigation; no battery correction was slipped into this moisture change.
- Netlify production is deployed as `app.v14.js` with service-worker cache
  `msv2-cloud-app-v14`. The receiver was compiled and flashed successfully on COM16; flash
  hash verification passed and `/device` confirmed build `MSV2 runtime build 2026-08-21
  watering-cycle-alerts-v1`. Live `/data` then resolved probe 1/2/3 to `15%`, `33%`, and
  `23%`, all normal rather than the stale firmware's red/yellow states.
- Nine auto-calibration regression tests pass, covering multi-cycle contribution,
  continuously changing priors, provisional evidence, multi-pour clustering, outliers,
  readiness, and shared-pot scaling. The auto-calibration response contract did not change;
  receivers still consume `ready`, `dryBound`, and `wetBound`. The alert-threshold firmware
  change does require a receiver flash, which the production receiver has now received.
- Full design and operations reference:
  `docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md`.

## 2026-08-17 Netlify Service Worker Stale-Data Bug

User reported the Netlify dashboard (`msv2-plants.netlify.app`) kept showing old data on a
normal page reload, requiring a hard reload (Ctrl+Shift+R) every single time to see fresh
readings. This turned out to be a real bug in `web/plant-dashboard/sw.js`, unrelated to the
auto-calibration work also happening this session - found and fixed, but **left
undeployed** at the user's explicit request (Netlify pushes are on hold this session
alongside the pending `app.v13.js` cache-bust from the auto-cal redesign work - see that
section below).

- **Root cause**: the service worker's `fetch` handler had exactly two branches - the app
  shell (`/`, `/index.html`, navigations), handled correctly with a network-first
  `cache:"no-store"` fetch, and a catch-all fallback for literally everything else that was
  **cache-first**: `caches.match(request).then(cached => cached || fetch(...))`. That
  fallback branch caught the page's own `fetch()` calls to the Supabase `plant-dashboard`
  edge function (`/api/share-snapshots`, etc.) - the actual live moisture/battery/snapshot
  data - not just the intended static assets (plant GIFs, `app.vXX.js`, jsQR). Once that
  data URL was cached on first load, every normal reload kept serving that same stale
  response from Cache Storage indefinitely, since nothing ever invalidated or re-fetched it.
- **Why hard reload "fixed" it**: a forced/hard reload is a documented special case where
  the browser bypasses the active service worker entirely for that one navigation - the
  page temporarily has no SW controller, so *every* fetch it makes, including the data
  call, goes straight to network. A normal reload stays under the SW's control and kept
  hitting the same stale cache entry forever.
- **Fix** (`web/plant-dashboard/sw.js`): the fallback branch now only cache-firsts
  same-origin requests under `/assets/` (the actual versioned, immutable static assets -
  matches the `/assets/*` immutable-cache scoping already in `_headers`/`netlify.toml`).
  Everything else, including the cross-origin Supabase API calls, now goes straight to
  `fetch(event.request)` with no Cache Storage involvement at all - no caching, no
  fallback-to-stale, always live.
- Side effect (harmless, not acted on): `/manifest.webmanifest` and `/app-icon.svg` are
  still listed in the `install` handler's precache array but are no longer read from cache
  by the `fetch` handler either (they're not under `/assets/`) - matches those two paths'
  own `Cache-Control: no-store` in `_headers` anyway, so this is more consistent than
  before, just slightly redundant precache work. Not worth trimming for two tiny files.
- **Status: fixed in the working tree, not deployed.** Will ship in the same Netlify push
  as the `app.v13.js` cache-bust from the auto-cal redesign work whenever that's approved -
  see that section's own "not deployed" note.

## Current State

- ESP32 display runtime renders the plant grid, animated pixel art, BLE readings, diagnostics, and phone setup app.
- ESP32 display runtime was last flashed successfully to the display receiver on `COM16` (port numbering shifts between sessions - always run `arduino_display.ps1 -Action board-list` first) with:
  - `.\scripts\arduino_display.ps1 -Action upload -Port COM16`
  - Firmware build tag: `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2`.
  - Build size after flash: 2,133,296 bytes, 67% of program storage; 58,508 bytes (17%) of dynamic memory.
  - The header sun-button single-tap has been replaced (Codex, same day) with a double-tap-anywhere-on-dashboard gesture to open the brightness overlay - `TOUCH_DOUBLE_TAP_MIN_GAP_MS`/`TOUCH_DOUBLE_TAP_WINDOW_MS` in the `.ino`. `drawBrightnessButton()`/the header sun icon are gone. This freed up plain single-tap for the new tap-to-expand card detail view below. `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md` still describes the old single-tap-header-button scheme and needs a pass to catch up.
  - Local uncommitted working tree - none of the 2026-08-12 firmware/doc changes are committed yet (see `git status`/`git diff`).
- Phone app supports:
  - Status dashboard.
  - Plant setup.
  - Receiver WiFi setup.
  - Receiver pairing ID/claim code.
  - Config version display.
  - Read-only Cloud Status panel for automatic bootstrap, upload, and config-pull status.
  - Add/Delete plant setup controls that resize the local `cards[]` array before saving to the display.
  - Auto layout by plant count: 6 plants -> `3x2`, 7 plants -> `4x2` with the bottom row centered, 8 plants -> `4x2`.
- Receiver stores local config in LittleFS:
  - `/ui.json`
  - `/wifi.json`
  - `/device.json`
  - `/cloud.json`
- Receiver exposes:
  - `/app`
  - `/ui`
  - `/data`
  - `/snapshot`
  - `/device`
  - `/claim`
  - `/config-meta`
  - `/cloud`
- `/snapshot` returns the cloud-upload payload locally.
- `/snapshot` includes plant setup summary under `config.cards[]` for cloud/app display.
- Receiver-side snapshot upload is implemented behind `/cloud.enabled`.
- `/cloud` reports upload status:
  - last attempt time
  - last success time
  - last HTTP status
  - last error string
- Mock cloud tooling exists:
  - `scripts/mock_cloud_server.py`
  - `scripts/relay_snapshot_once.py`
- Mock cloud server has an auto-refreshing browser dashboard at `/` with plant names and moisture health bars when the latest receiver firmware has uploaded config card metadata.
- Mock cloud server also serves PWA shell routes: `/manifest.webmanifest`, `/sw.js`, and `/app-icon.svg`, plus iOS home-screen metadata.
- Cloud API draft exists at `docs/CLOUD_API_DRAFT.md`.
- Supabase hosted backend scaffolding exists:
  - `supabase/migrations/001_msv2_cloud.sql`
  - `supabase/functions/receiver-snapshot/index.ts`
  - `supabase/functions/plant-dashboard/index.ts`
  - `supabase/functions/claim-receiver/index.ts`
  - `supabase/migrations/002_msv2_store_snapshot_rpc.sql`
  - `supabase/migrations/003_msv2_read_latest_snapshots_rpc.sql`
  - `supabase/migrations/005_msv2_receiver_bootstrap.sql`
  - `supabase/migrations/006_msv2_receiver_bootstrap_fix.sql`
  - `supabase/migrations/007_msv2_hash_secret_bytea_fix.sql`
  - `supabase/migrations/008_msv2_pgcrypto_schema_fix.sql`
  - `supabase/migrations/009_msv2_household_share_tokens.sql`
  - `supabase/functions/receiver-bootstrap/index.ts`
- Claim/user access scaffold exists:
  - `supabase/migrations/004_msv2_households_and_claims.sql`
  - `msv2_households`
  - `msv2_household_members`
  - `msv2_receiver_claims`
  - `msv2_claim_receiver`
  - `msv2_read_user_latest_snapshots`
  - `supabase/config.toml`
  - `docs/SUPABASE_SETUP.md`
- Hosted `plant-dashboard` Edge Function is deployed and returned HTTP 200.
- `claim-receiver` and updated `plant-dashboard` Edge Functions are deployed.
- `plant-dashboard` Edge Function now provides the no-login household app APIs:
  - `/functions/v1/plant-dashboard/api/share-claim`
  - `/functions/v1/plant-dashboard/api/share-snapshots`
  - tested successfully with receiver `MSV2088A3104A7AC`
  - share snapshot read returned `plant_count: 2` with Basil and Mint readings
- Static no-login household app shell exists at `web/plant-dashboard/index.html`:
  - receiver connection with `receiverId` and `claimCode`
  - long random household share tokens
  - share token persistence in browser `localStorage`
  - household-scoped dashboard via the Supabase API endpoints
  - dark Plant Status Center visual style
  - animated plant GIF cards copied to `web/plant-dashboard/assets/plant_gifs/`
  - PWA files exist: `manifest.webmanifest`, `app-icon.svg`, `sw.js`
  - Netlify config exists: `netlify.toml`, `_headers`, `_redirects`
  - unsigned share snapshot API requests return `401 share token required`
- Important: Supabase Edge Functions returned HTML as `text/plain` with `Content-Security-Policy: default-src 'none'; sandbox`, so the browser app must be hosted as a static page elsewhere. Edge Functions should stay API-only.
- `plant-dashboard` root now returns an API-only JSON note instead of raw HTML.
- Static app package exists at `web/msv2-plant-dashboard-static.zip`.
- Netlify production deploy is live at `https://msv2-plants.netlify.app`.
- Receiver bootstrap scaffold exists so future firmware can register itself after WiFi setup and receive a per-device upload token.
- ESP32 firmware now has first-pass cloud auto-bootstrap:
  - calls `receiver-bootstrap` after station WiFi is connected
  - sends `receiverId`, private `receiverSecret`, `claimCode`, and build tag
  - stores returned `uploadUrl`, per-device `apiToken`, and interval in `/cloud.json`
  - leaves existing manual cloud config alone if an upload URL and token already exist
- ESP32 local app no longer exposes manual Cloud Sync URL/token editing in normal setup; use `GET /cloud` for status and `/cloud` API calls only for debugging/reset testing.
- Auto-bootstrap was tested successfully against hosted Supabase:
  - `bootstrap.lastHttpStatus: 200`
  - `bootstrap.lastError: ""`
  - `hasApiToken: true`
  - `syncIntervalSeconds: 60`
  - cloud sync enabled automatically
- Per-device-token snapshot upload was tested successfully against hosted Supabase:
  - `upload.lastHttpStatus: 200`
  - `upload.lastError: ""`
  - `upload.lastSuccessMs` populated
- Bootstrap SQL fix `006` removes ambiguity in `msv2_register_receiver` by using the primary key constraint in the upsert and `return query` for the returned token row.
- Hash SQL fix `007` updates `msv2_hash_secret` to call `digest(convert_to(..., 'UTF8'), 'sha256')` because the hosted database reported `function digest(text, unknown) does not exist`.
- Pgcrypto schema fix `008` sets `search_path = public, extensions` for the hash/register functions because Supabase reported `function digest(bytea, unknown) does not exist` when `pgcrypto` was not visible from the function search path.
- Firmware now includes bootstrap HTTP response text in `/cloud.bootstrap.lastError` when the cloud returns a non-2xx status.
- Current cloud config path: Supabase can store receiver configs and the ESP32 display can poll/apply newer versions, but the Netlify app hides remote editing for now to keep the hosted app read-only and lower traffic.
- WiFi/setup behavior is currently working well in bench testing:
  - First-time setup can still be tested with `-ForceWifiSetup`.
  - The forced setup build ignores saved WiFi until the setup app saves a choice.
  - Saving home WiFi or local-only mode writes `/wifi.json` and `/wifi-setup-unlock-v2.flag`.
  - After that save, resets should return to the saved mode rather than first-time setup.
  - Local-only mode keeps broadcasting the open `MSV2-Display` AP and returns to the dashboard.
  - If a phone sees `MSV2-Display` but cannot join, forget the saved phone network first because older firmware used password `moisture` for the same SSID.
  - BLE scanning was relaxed to reduce WiFi AP contention: 3-second scans with a 250 ms idle gap, and scan interval/window `120/40`.
  - The save/reboot screen now clears cached pixel-art animation state and suppresses display repainting while restart is pending, so old plant art should not bleed through the reboot message.

## 2026-07-06 Sprite/Art Pipeline Update

- Plant sprite generation was normalized so every generated ESP32 sprite frame is emitted on an `84x84` transparent canvas.
  - `scripts/generate_plant_sprites.py` now supports `--target-width` and `--target-height`, defaulting to `84`.
  - Frames are trimmed, aspect-preserving downsampled if needed, and bottom-aligned on the shared canvas.
  - The generated firmware/header/mobile previews now all use the same canvas dimensions, which keeps art scale consistent across plant choices.
- ESP32 sprite rendering was updated for better sparse-layout sizing.
  - `drawPlantSpriteScaled` and animation diff rendering now use shared sprite geometry.
  - Sprites can draw at crisp integer upscales when a sparse card has room, then fall back to `1x` or downsample for dense layouts.
  - This fixed tiny art in `1x1`, `2x1`, and `3x1` layouts while preserving dense grid fit.
- The ESP32 phone dashboard now caps plant setup at 16 plants.
  - Clicking `Add Plant` at the limit shows `Limit reached: the display supports up to 16 plants.`
  - Saving a config with more than 16 cards is blocked.
  - Embedded service-worker cache was bumped to `msv2-app-v2` so phones pick up the fixed `/app`.
- Two new source sprite sheets were added under `assets/`:
  - `GREEN-BLUE-PURP-PLANTER.png`
  - `YELL-ORG-RED-PLANTER.png`
- New helper script:
  - `scripts/generate_planter_sheet_gifs.py`
  - Detects exact plant components from those sheets using the published sprite sizes.
  - Converts black backgrounds to transparent.
  - Chooses 3 deterministic random color frames per plant by default to keep firmware size reasonable.
  - Upscales sheet-derived GIFs `4x` before the main sprite generator so they visually match the older normalized art after the generator's normal `0.5` scale.
- New generated source GIFs in `assets/plant_gifs/`:
  - `color_vine_pot.gif`
  - `color_grass_planter.gif`
  - `color_round_pot.gif`
  - `color_bonsai_bowl.gif`
  - `color_zigzag_pot.gif`
  - `color_hanging_vine.gif`
  - `color_hanging_clump.gif`
- The planter sheet GIF workflow is documented in `assets/plant_gifs/README.md`.
- Important size note:
  - Embedding all 18 color frames for all 7 new sheet-derived plants made firmware too large at about `112%` of program storage.
  - The final flashed build uses 3 frames per sheet-derived plant and compiles at about `2,059,353 bytes`, `65%` of program storage.
- Final regeneration/flash state:
  - Ran `python scripts\generate_planter_sheet_gifs.py`.
  - Ran `python scripts\generate_plant_sprites.py`.
  - Compiled successfully with `powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 compile`.
  - Flashed successfully to the ESP32 display receiver on `COM13` with `powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13`.

## 2026-07-07 Security Hardening Pass

A full-repo review (firmware, Supabase backend, scripts, web dashboard) found 46 issues, 6 critical. The critical/high findings were fixed and **deployed live** the same day — not just committed. Scoped specifically to preserve the no-login QR/claim-code onboarding flow unchanged; nothing here adds a step for the end user.

- **Supabase RPC lockdown** (`supabase/migrations/011_msv2_lock_rpcs_to_service_role.sql`, `012_msv2_revoke_public_execute.sql`): every RPC (`msv2_read_latest_snapshots`, `msv2_claim_receiver`, `msv2_register_receiver`, etc.) was reachable directly via `/rest/v1/rpc/<fn>` with just the public API key, completely bypassing every auth check written into the Edge Functions. Now locked to `service_role` only.
  - **011 alone did not work** — Postgres grants `EXECUTE` to the `PUBLIC` pseudo-role by default on function creation, and revoking from `anon, authenticated` specifically doesn't touch that. Verified live (`msv2_check_rate_limit`, granted only to `service_role`, still returned `204` to an anonymous request). 012 explicitly revokes from `PUBLIC` too and is the one that actually closed it — confirmed via direct `curl` against the live project, now returns `401`.
  - If you add a new RPC later: `revoke ... from public` on it, not just from named roles, or it's wide open regardless of what you did/didn't grant.
- Rate limiting added on registration/claim attempts (keyed by `receiver_id`), claim codes are now hashed at rest (were the one plaintext credential), and a real `msv2_revoke_household_share` RPC exists now (the dashboard's "Forget" button previously only cleared `localStorage`, never actually invalidated the token server-side).
- `receiver-snapshot`: removed the shared `MSV2_RECEIVER_TOKEN` master-token bypass and the unscoped fleet-wide snapshot listing (`GET` with no `receiverId` used to return the latest 20 snapshots across every household). See `docs/SUPABASE_SETUP.md` for the updated auth flow.
- Firmware:
  - `secureClient.setInsecure()` replaced with real TLS validation against the actual root CAs Supabase presents (Google Trust Services / GlobalSign), pinned as PEM constants in the `.ino`. Confirmed live against the real bootstrap/config-pull/snapshot-upload endpoints.
  - Local admin API (`/ui`, `/cloud`, `/wifi` POST/DELETE) used to have no auth and stayed open indefinitely, including forever in local-only AP mode. Now gated behind a 15-minute post-boot grace window (`adminUnlockUntilMs`) — covers every mode with one mechanism, zero added friction for actual setup (which happens in minutes), but reconfiguring later needs a power-cycle to reopen the window.
  - Fixed a stored-XSS gap in `MobileAppPage.h` (`card.sensor` and the art-key label fallback weren't escaped, unlike everything else in that file).
- Web dashboard: extracted the inline `<script>` in `index.html` to `assets/app.js` so a real CSP (no `unsafe-inline` on scripts) is possible; added CSP + baseline security headers across all three places Netlify config lives (`netlify.toml` x2, `_headers`); SRI-pinned the `jsQR` CDN script.
- **Migration history was out of sync with reality** on the live database — `supabase migration list --linked` showed 004-010 as unapplied even though their tables/functions were clearly live (device was actively using them). This came from applying SQL by hand in the dashboard editor in the past instead of `supabase db push`. Fixed via `supabase migration repair 001..010 --status applied --linked` (bookkeeping only, no schema change) before pushing 011/012. **If you hand-edit SQL again, remember to repair the tracking table afterward** or the next `db push` will try to re-run already-applied migrations and fail on non-idempotent statements like `create policy`.
- Incident during verification: a test `POST /ui` with an empty `{}` body to the live device wiped its plant card config (full-replace semantics in `writeUiJson`, not a merge). Recovered by pushing `ui/display_default.json` (generic herb-themed placeholder cards) via `push_display_ui.py`. The original custom names/art are still recoverable from the last cloud snapshot: `select snapshot->'config'->'cards' from msv2_latest_snapshots where receiver_id = 'MSV2088A3104A7AC' order by received_at desc limit 1;` — not yet done as of this handoff.
- Everything verified live end-to-end, not just compiled: firmware flashed to the physical device on `COM13`, migrations applied to the real Supabase project, both edge functions redeployed, web dashboard redeployed to `https://msv2-plants.netlify.app`. Commits: `825c1fc`, `9d959ec` (plus `451574c` checkpoint before the pass started).
- Not done in this pass (by scope, not oversight): the Medium/Low findings from the review (docs drift elsewhere, dead code in `plant-dashboard/index.ts`'s unused `renderApp()`, script robustness issues in `esp32_packet_loss_test.py`/`mock_cloud_server.py`, stale plant-art whitelist in the dashboard vs. the new `color_*` sprite keys, etc.). **All of these were picked up and closed in the 2026-08-13/14 full-repo review pass below** - `renderApp()` deleted, both scripts hardened, the art whitelist updated and deployed.

## 2026-07-24 Moisture Probe RC Sensing + Overnight Battery Test

First real end-to-end work on the capacitive moisture probe (XIAO nRF52840 Sense on `COM15`/`COM5`, board FQBN corrected to `Seeeduino:nrf52:xiaonRF52840Sense`) alongside the display receiver (`COM13`). Probe sensing: precharge `SENSE_PIN` (`D0`) HIGH, release to `INPUT`, time the RC fall to LOW as a proxy for dielectric constant/moisture.

- New standalone test sketch `arduino/MSV2_Moisture_RC_Test/` validates the RC read loop and battery read independent of the BLE sketch; kept in sync with `MSV2_Moisture_BLE_Sleep` on every sensing-parameter change.
- Battery reading (`PIN_VBAT`/`VBAT_ENABLE`, both on the Sense board's battery-charge divider) calibrated against a multimeter: schematic-nominal comp is 2.961 (`R1=1M, R2=510K`), empirically calibrated to `2.946`, within normal 1%-resistor tolerance of nominal.
- BLE packet-loss measured at the probe's 3s test cadence: **1.00% over 10 minutes** (198/200 wake cycles received) via `scripts/esp32_packet_loss_test.py`.
- New logging/charting tooling: `scripts/log_sensor_readings.py` (polls the receiver's `/data` and appends to CSV), `scripts/plot_sensor_readings.py` (renders two single-series line charts - moisture, battery - never dual-axis), `scripts/fetch_supabase_history.py` (pulls history via `supabase db query --linked`, bypassing the need for the receiver's own per-device token).
- **Cloud history pipeline added** so overnight data survives the laptop being off: `supabase/migrations/013_msv2_sensor_history.sql` adds an insert-only `msv2_sensor_history` table (the existing `msv2_latest_snapshots` is upsert-only by design, keyed on `receiver_id`, and only ever holds the single latest snapshot). `receiver-snapshot/index.ts` now inserts one history row per plant on every snapshot upload, in addition to the existing latest-snapshot upsert. The receiver's existing `scheduleCloudSyncSoon()` (15s-throttled fast-path on new BLE data) means history already lands every ~15-18s at the probe's current fast test cadence, with zero firmware cadence changes.
- **Display app gained a History tab** (`MobileAppPage.h`): per-probe, per-range (6h/24h/3d/7d) moisture + battery charts, auto-refreshing every 30s while visible, fetched through a new local `/history` proxy endpoint on the receiver (`handleHistoryProxy()` in the `.ino`) that forwards to `receiver-snapshot`'s `history=1` GET route using the receiver's own stored token.
  - Hit and fixed a real bug along the way: `http.getString()` on the ESP32 has a **hard ~31.6KB response-size ceiling** (confirmed consistent across requests, not a timeout) that silently truncates mid-JSON past ~218 rows. Real fix was pagination end-to-end (new `id`-cursor param on `msv2_read_sensor_history`, migration `014_msv2_sensor_history_pagination.sql`), 150-row pages assembled client-side in a loop - not a bigger buffer or longer timeout, since either risks the ESP32 blocking long enough to freeze the display again (see next bullet).
  - Also fixed: `SENSOR_RENDER_INTERVAL_MS` was throttling sensor-card redraws to once per 10s regardless of how fast new data arrived; dropped to 2000ms.
  - **Caused one real incident**: an early streaming-proxy attempt for `/history` hung indefinitely (chunked-transfer-encoding from the Supabase edge function wasn't being read correctly), which froze the whole display (WebServer runs in the same loop as rendering) until reflashed with a fix. Recovered by reflashing normally - a clean upload's own hard-reset was enough, no manual intervention needed. Root cause: don't hand-roll a chunked-stream proxy on this HTTPClient; use the same `getString()` pattern already proven elsewhere in this firmware (just watch the size ceiling above).
  - Separately, **opening a raw serial connection to the ESP32-S3 with DTR+RTS asserted can drop it into the ROM bootloader** (native USB-Serial/JTAG, not a real UART) and it won't boot the app until reflashed/hard-reset. Don't poll this board's serial for diagnostics; use its HTTP API instead.
- **LED scheme changed for tonight's battery test**: onboard RGB disabled (`ENABLE_STATUS_LEDS = false`); external LED on `D1` now pulses once per wake/advertise cycle (not per raw BLE packet - Bluefruit has no per-packet callback, and a literal ~25-blink strobe every 500ms would defeat the point of measuring the LED's own power draw).
- **Probe firmware stripped down for the overnight run**: GATT/peripheral-connection stack (`BLEDfu`/`BLEDis`/`BLEBas`, connect callbacks) commented out - this design only ever advertises and is never connected to, so none of it was reachable; the unused System-Off deep-sleep path likewise commented out (real future work, not deleted); `ENABLE_SERIAL_LOGS` set to `false` for the unattended run.
- **Precharge tuning saga** (all on `MOISTURE_PRECHARGE_US`, the fixed HIGH-drive duration before releasing `SENSE_PIN` to `INPUT`):
  - Original 500us was insufficient - baseline moisture jumped from ~12,000 to ~17,700us (much more consistent) after bumping to 10ms.
  - A "verified" precharge (drive HIGH in bursts, check via `analogRead` until near-VDD, cap at 60 steps) was tried and reverted: checking the charge means releasing the pin to read it, which starts the very discharge it's trying to avoid - confirmed live, it read `moisture=0` every cycle. Also surfaced (and left fixed, harmless either way) that `AR_DEFAULT` is a fixed 3.6V ADC range on this chip, not ratiometric to actual ~3.3V VDD, making a naive 98%-of-1023 threshold unreachable.
  - 20ms (further margin beyond the working 10ms) was tried and **caused touch response to invert** - reading collapsed far below baseline on contact instead of rising. Reverted to the validated 10ms. Notable complication discovered afterward: essentially all of this touch testing happened with the probe USB-tethered to the laptop (needed for flashing/serial), and tethering turned out to be a real confound (see below) - so the 20ms-specific causation is less certain in hindsight than it seemed at the time.
- **Major finding: USB tethering itself was injecting noise into the measurement.** The floating high-impedance `SENSE_PIN` during the discharge phase acts as an antenna; a USB-tethered laptop provides a ground-loop/mains-hum coupling path, and a human hand is a good antenna for that same ambient noise - explaining touch-dependent instability that had nothing to do with sensor hardware. Confirmed by the user unplugging USB entirely: readings immediately became clean and consistent. Likely explains some/most of the touch weirdness chased over the session; the resistor-value question below is now a robustness/uptime improvement rather than an active bug fix, since real deployment (battery power, no laptop nearby) is exactly the condition that already tested clean.
- **`MOISTURE_FALL_TIMEOUT_US` bug found via the above**: fixed at 50ms, which turned out to be too short once USB noise (which had been causing false-early threshold trips, i.e. artificially short readings) was removed - every sample was hitting the timeout and reporting `MOISTURE_INVALID` (`0xFFFF` = 65535), which reads as a plausible high value but means "no measurement." Bumped to 500ms (generous, not yet tightened - see next work).
- **Hardware finding once actually inserted in soil**: readings became wildly unstable (151 to 60,000+ observed across a handful of cycles - not physically plausible for real soil moisture). Diagnosed as very likely the DNC (unpopulated) pulldown resistor footprint: with no dominant, well-defined discharge resistor, the baseline discharge relies on undefined parasitic leakage, and wet soil's variable ionic conductivity can provide an unstable competing resistive path - the same category of effect as the finger-bridging artifact seen on the bench, but continuous and uncontrolled rather than a discrete touch event. A dominant pulldown (discussed: 10K over 1M, since a lower value both cuts discharge/uptime ~100x *and* is far less perturbable by marginal parasitic resistances in parallel, without costing sensitivity to the real capacitive signal, which is R-independent in relative terms) should fix this, but needs solder access, not firmware. **Not fixed yet** - deferred to tomorrow.
- **Tonight's run is a battery-life test only, not a valid moisture calibration run.** Moisture data is expected to be unreliable until the pulldown resistor is populated. The point of tonight is measuring the external LED's actual impact on overnight battery life; moisture/BLE/cloud pipeline all being exercised for real is a bonus, not the goal.
- Pre-existing, unrelated bug noticed in passing (not fixed, cosmetic, doesn't affect sensor data): the receiver's missed-packet counter wraps to a huge number (`~2^24`) on the first packet after any probe reboot, since the wake-counter-gap math doesn't account for the counter resetting to a small value. Isolated to the `missedTotal` diagnostic stat.

## 2026-07-25 Oscillator Sensing Port, Auto Mode, and the RC-Timing-Was-Never-Going-To-Work Postmortem

The 2026-07-24 run confirmed the one-shot RC precharge/discharge approach is fundamentally unfixable: no resistor value survives real, variable soil conductivity, since a single timed discharge has no way to reject noise or self-correct drift (see that section). Today replaced the sensing architecture entirely, rebuilt calibration around it (twice, as the scale kept moving), added a self-calibrating "auto mode" to stop hand-tuning thresholds altogether, reworked probe power behavior for a real overnight test, and fixed a real silent-failure bug in the config-save path. Everything below is flashed and live as of this handoff.

- **Oscillator sensing ported into `MSV2_Moisture_BLE_Sleep.ino`**, replacing `readMoistureFallUs()`/`readAveragedMoisture()` entirely. Architecture (bench-validated separately in `arduino/MSV2_Moisture_Probe_Test/` + `docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md`, same free-running-astable-plus-frequency-counting principle as the MSV1 555-timer proof-of-concept): free-run the sensing node (charge to HIGH, discharge to LOW, immediately recharge, repeat) and count cycles in a fixed window, rather than timing a single discharge. Median-of-5 × 1ms bursts (`OSC_SAMPLE_WINDOW_MS`/`OSC_MEDIAN_SAMPLES`) rejects scheduler-jitter outliers; total active sensing time per reading is 5ms (down from up to several hundred ms worst-case). **Requires a 100kΩ pulldown resistor populated on `SENSE_PIN`** (the earlier unpopulated/DNC footprint from 2026-07-24) — this is a hardware precondition the firmware can't compensate for.
- **Moisture value inverted at the source**: raw oscillator frequency runs backwards for a field called "moisture" (drier soil oscillates faster), so `readAveragedMoisture()` now transmits `MOISTURE_INVERT_CEILING (200) - frequencyKHz` instead of raw frequency, restoring the original "higher = wetter" convention for every consumer downstream.
- **Calibration recalibrated twice** as the transmitted scale moved (RC-µs hundreds/thousands → raw kHz 10-90 → inverted ~110-190) across every hardcoded dry/wet default: `MSV2_ESP32_Display_Runtime.ino`'s 8-card default config and 4 fallback sites, `MobileAppPage.h`'s preset table, `ui/display_default.json`. Current defaults: dry≈120, wet≈175 (herb preset). **These remain provisional** — no real-soil calibration data exists yet (all bench validation used touch/hand proximity as a moisture stimulus).
- **Auto mode added** (`supabase/migrations/017_msv2_sensor_auto_calibration.sql`, `receiver-snapshot/index.ts`'s new `autocal=1` branch, `runCloudAutoCalPull()` in the `.ino`) to stop this recalibration treadmill: per-sensor moisture range self-learned from history using the 5th/95th percentile over a trailing 21-day window (not raw min/max — a single outlier or a probe-removal transient barely moves a percentile, whereas it would permanently redefine a raw max). Gated by a "ready" check (≥50 samples, ≥3 days of history) before trusting/showing a percentage — shows "Learning..." (web) / muted (physical display) until then, **falling back to the manual/preset range as a working estimate** while learning (not blocking to nothing), since that range is at least on the current sensing scale. Also detects watering events (≥15% of range rise within ≤3h, clustered within 12h into one event) and predicts a watering interval from the median gap of the last ≤6 events, driving an "overdue" flag that forces the existing danger-color styling. Setup tab gained a per-card "Auto-calibrate" checkbox that disables the now-irrelevant manual dry/wet inputs.
- **Battery reading smoothed** with an exponential moving average (`smoothedBatteryPercent`, 0.2 factor, same constant already used for the oscillator's own frequency smoothing) so the History tab's charging curve isn't dominated by per-sample ADC noise — still one real ADC read per wake, only the transmitted/charted value is smoothed.
- **Probe power rework for tonight's test**: measure/check cadence dropped to every 10s (cheap, ~5ms active), but actual BLE advertising (the expensive radio-on part) now only happens every 60s normally, or immediately if moisture swings ≥15 units between checks (a real watering/removal event, not sensor noise) — matches the "measure often, radio rarely" pattern already sketched in `docs/PRODUCT_ROADMAP.md`'s probe section. `wakeCount` now only increments on actual advertises so the receiver's missed-packet diagnostic still means "real over-the-air loss," not artificially inflated by the new power-saving skips. External LED no longer pulses per-advertise (pure battery cost, no signal value) — now blinks 3× per wake only while moisture is at/below a **fixed** threshold (134, matching the 25%-of-default-range "danger" cutoff used elsewhere) - fixed because the probe is advertise-only with no BLE receive path, so it has no way to learn the live auto-calibrated or per-plant manual range.
- **Physical display now blinks red** (500ms phase) instead of a static border for low/overdue cards, mirroring the probe's own alert. Had to add a forced ~500ms redraw path specifically while a card is blinking (`anyLowMoistureCardVisible()`), since the normal redraw gate only fires on new BLE data (`sensorDataDirty`) — which, under the probe's new 60s-normal-advertise cadence, would have made "blinking" look like a color change once a minute instead of an actual blink.
- **Real bug found and fixed**: manual dry/wet saves were silently failing and nobody noticed. Root cause was two compounding issues - (1) `isAdminActionAllowed()`'s 15-minute post-boot admin window (a deliberate security control from the 2026-07-07 pass - see that section) never refreshed, so any receiver that had been up for a while (exactly the overnight-test scenario) silently rejected every save with 401; (2) `MobileAppPage.h`'s `saveConfig()` mutated the shared `config` object (used by the Status tab's rendering) *before* the save's success was known, so a rejected save still looked like it took effect in the app while the receiver's file never changed - explaining "the app updates but the physical display doesn't." Fixed both: `isAdminActionAllowed()` now slides the window forward on each successful action (doesn't weaken the "must reboot to gain admin access at all" property - an already-expired window still can't extend itself), and `saveConfig()` now collects into a local candidate and only commits it to shared state after a confirmed 200, surfacing a parsed, readable error otherwise. Added a persistent hint in the Setup tab explaining the reboot-to-reconfigure behavior.
- **Known, deferred**: the Netlify-hosted `web/plant-dashboard` has its own hardcoded art catalog (`known` array in `assets/app.v11.js` + static GIFs in `assets/plant_gifs/`) completely separate from the firmware's embedded one - newer sprite keys (used by the current live cards, e.g. Scindapsus/Alocasia) aren't in either, so they silently fall back to `sprout_pot` there. Fix is known (update the array + add the GIFs) but **blocked on Netlify deploy minutes until end of month** - not done.
- **Not yet done**: real-soil calibration data (everything above used touch/hand-proximity bench testing); confirming the 100kΩ pulldown is actually populated on the deployed field probe(s), not just the bench rig; a solar-panel charging test planned for tomorrow in direct sunlight (motivated the battery smoothing work - panel only appears to charge in direct sun, not even slight shade).

## 2026-08-09 QSPI Power-Down Bug (Root Cause of the Battery Drain), Cadence Restored, and VDD-Coupling Confirmed in Moisture Sensing

Triggered by reviewing a week of Supabase history from a vacation trip: both field probes (Alocasia, Scindapsus) ran their batteries to 0% in ~4-6 days despite a 30-minute sleep/500ms-advertise duty cycle that should have supported many months on a 150mAh cell. Full data review published as an artifact; key findings and the fix below.

- **Root cause found: `powerDownExternalFlash()` was silently never running.** `Adafruit_SPIFlashBase::begin()` (bundled library, `Seeeduino:nrf52` core v1.1.13) enables the QSPI peripheral (`nrfx_qspi_init()` → `nrf_qspi_enable()`) *before* checking its hardcoded device table (GD25Q\*/S25FL\*/W25Q\*/AT25\*/MB85RS\* only). The XIAO nRF52840 Sense's actual onboard flash is a **Puya P25Q16H**, not in that table, so `flash.begin()` always returns `false` on this board - and since our code's old `if (!flash.begin()) { return; }` guard bailed out early with no matching `_trans->end()`, the QSPI peripheral was left enabled for the rest of the probe's life, every single boot, and the flash chip never received its `0xB9` deep-power-down command. This tracks almost exactly with real-world reports on this board family: `~1.3-1.5mA of idle current from QSPI left active` (Seeed/Nordic forum threads), matching our own inferred ~1.5-1.6mA average draw.
- **Fix**: `powerDownExternalFlash()` in both `MSV2_Moisture_BLE_Sleep.ino` and `MSV2_Moisture_BLE_Sleep_Alocasia.ino` now talks to `flashTransport` directly (`begin()` → `runCommand(0xB9)` → `delay(10)` → `end()`), bypassing `flash.begin()`'s device-table dependency entirely - `runCommand()` is a raw opcode transfer that doesn't need the chip to be "recognized."
- **Live-confirmed on real hardware**, not just reasoned from source: added a wake/sleep "ping" diagnostic (`logFlashDiagnostic()`) that sends `0xAB` (release from deep power-down), reads the JEDEC ID, re-sends `0xB9`, reads again - all in one boot, deliberately with no status-poll loop (avoids a known, unrelated QSPI-specific DPD-exit bug on this exact chip, `zephyrproject-rtos/zephyr#55543`). Captured on Scindapsus: `awake ID: 0x856015  asleep ID: 0x000000` - `0x85` is Puya's real manufacturer ID, `6015` matches the P25Q16H's type/16Mbit capacity, and it goes fully silent immediately after our own `0xB9` in the same boot. Proof, not inference.
- **Ruled out**: the onboard RGB LED being stuck on (`ENABLE_STATUS_LEDS=false` skips the off-write, and GPIO `OUT` defaults LOW = on for this active-low LED) was a live suspect earlier in this investigation but the user physically checked both probes - LED confirmed off. Not the cause.
- **New tooling**: `scripts/serial_capture.py` (a reconnecting serial logger - survives the COM-port renumbering during a DFU reflash, which a plain `arduino-cli monitor` session cannot) and `.claude/skills/flash-moisture-probe/SKILL.md`, documenting the whole capture recipe including two hard-won gotchas: (1) the capture script and the upload command cannot run concurrently - the DFU tool needs exclusive port access for its bootloader-touch handshake; (2) `while (!Serial) {}` is not a reliable "wait for a real terminal" gate on this board's TinyUSB stack (it resolves true too early post-reset) - use a flat `delay(1500)` instead, confirmed empirically. Added `BOOT_HOST_WAIT_MS`/the flat delay to both probe sketches for future bench debugging; one-time cost at boot only (`setup()` runs once per power cycle), harmless to leave in permanently.
- **Cadence restored**: `WAKE_PERIOD_MS` set back to `10000` (from `1,800,000`/30min) on both probes now that the QSPI leak is fixed, reactivating the dormant `bigSwing`/`MIN_SWING_ADVERTISE_INTERVAL_MS` immediate-advertise-on-watering logic (`ADVERTISE_INTERVAL_MS` was already `60000`, so this was the only constant that needed to change). Both flashed and live-confirmed via Supabase showing the wake counter advancing every ~60-70s as expected. This also doubles as a real-world overnight battery test: computed active-time budget is ~38x higher than the 30-min config, expected to cost ~40-60 days of battery life at this cadence (vs. the theoretical 100s-of-days at 30min) if the fix is genuinely working, vs. an obvious ~8-12% overnight drop if the QSPI leak somehow persists. **Alocasia (~96-100% battery at test start) is the meaningful test; Scindapsus is still critically low (0-3%, unaddressed since ~Aug 7) and isn't a fair test right now** - needs an actual recharge first.
- **Scindapsus quantization rechecked over a fresh 5-day window - artifact persists, but this is expected and unrelated to the QSPI fix.** 67.6% of all 3,945 readings over the 5 days are the single value 222 (pinned near the sensor's ceiling). Root cause is the same one already known: Scindapsus's battery has been at/near 0% continuously since ~Aug 7, and this design has no reference cancellation against VDD drift, so a critically low/unstable supply corrupts the oscillator reading. Battery needs an actual recharge before this probe's moisture data means anything.
- **New finding this session, beyond quantization**: VDD/battery-state coupling into the moisture reading isn't limited to "battery critically low" - **active charging also visibly corrupts the reading** (large jumps observed correlating with charge events), via the same underlying mechanism (charge-current IR drop + possible charger switching noise disturbing the shared VDD rail that the GPIO-timed oscillator has zero rejection of). Root-caused precisely: the oscillator's timing is ratiometric to VDD (GPIO HIGH = VDD, input threshold = a fraction of VDD) with no independent reference, unlike the planned FDC2212 replacement, which measures LC-tank resonant frequency (a property of L and C only, not voltage) against a stable reference *clock* - a different measurement domain entirely, not just "a reference exists." Full mechanism writeup and field evidence added to `docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md`'s 2026-08-09 section - this is concrete confirmation of that doc's existing "battery-voltage drift" rationale, not a new problem.
- **Not yet done**: check tomorrow morning whether Alocasia's overnight battery drop matches the "fix is working" prediction (<~1-2%) or the "still broken" one (~8-12%); recharge/swap Scindapsus's battery; bench-check Scindapsus's BQ25100 `~CHG` pin for the noise hypothesis that explained its erratic battery% readings (still open, see the vacation-review memory); consider a firmware-side mitigation of discarding/flagging moisture samples taken while charging or below a battery floor, independent of the FDC2212 hardware timeline; the receiver's `DEFAULT_STALE_SECONDS` (currently uncommitted locally at `3900`, sized for the old 30-min probe cadence) needs revisiting now that probes are back on ~60s - not done as part of this session, flagged for whoever picks that file back up.

## 2026-08-11 Home-Screen QR Scanner Repair and Production Deploy

- Root-caused the missing iPhone camera prompt: the live scanner waited for the jsQR CDN fallback before calling `getUserMedia()`, and its SRI value did not match the actual pinned jsQR 1.4.0 response. Browsers rejected the decoder, the camera line was never reached, and a catch-all message misreported the decoder failure as camera unavailability.
- The reviewed decoder is now vendored as `web/plant-dashboard/assets/jsQR-1.4.0.min.js` with its Apache 2.0 license and correct SHA-384 pin. The production CSP no longer permits jsDelivr, and all three Netlify header sources explicitly allow `camera=(self)` while disabling microphone access.
- Camera permission is now requested directly from the Scan QR click before decoder initialization. Permission, missing-camera, busy-camera, preview, and decoder failures have distinct user-facing guidance. Live scanning is throttled and downsampled to reduce iPhone CPU load, and camera tracks are released on Stop, page hide, or navigation.
- Added a `Scan Photo` fallback using the phone's native image/camera picker. The selected image is decoded locally; nothing is uploaded.
- Cache-safe upgrade: application code moved to `assets/app.v11.js`, the service-worker cache moved to `msv2-cloud-app-v11`, and both the versioned app and decoder are precached. This avoids the previous one-year immutable `assets/app.js` URL trapping existing Home-Screen installs on stale code.
- Local verification command: `node scripts/verify_plant_dashboard_qr.mjs`. It launches a hidden headless Edge instance against localhost with a fake camera and verifies production CSP/permissions policy, v11 service-worker precaching, camera-before-decoder ordering, SRI-checked same-origin decoder load, MediaStream release, denied-permission guidance, and end-to-end decoding of `scripts/fixtures/msv2-pairing-qr.png`. No real claim request is sent; the verifier intercepts it locally.
- Deployed to the existing `msv2-plants` Netlify production site on 2026-08-11 as deploy `6a7c09d1a8e26d5ff34c2c9b` after the user approved publishing. Verified both the immutable deploy URL and `https://msv2-plants.netlify.app`: root HTML references `app.v11.js`, CSP and camera policy are correct, service worker reports v11 with `no-store`, and the live app/decoder hashes exactly match the locally tested bytes.
- Remaining validation: confirm on the real iPhone that the existing Home-Screen v10 installation upgrades, the first-use iOS prompt appears, the rear-camera preview and QR recognition work, the photo fallback works, and Stop/reopen releases and reacquires the camera normally.

## 2026-08-12 Touch/Brightness UI Consistency and Display Reliability Fixes

Started as a UI-consistency pass on the touch/brightness feature Codex had added (see
`docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`), then expanded into a real reliability
investigation once physical testing turned up genuine display corruption. Everything below is
flashed and user-confirmed working as of this handoff. Session ran collaboratively with Codex
(the user ran both in parallel, feeding each round's results to the other).

**UI consistency and touch-control bugs (first pass):**

- The new touch UI (brightness modal, calibration screens, WiFi setup screens) was drawn with
  hardcoded Adafruit_GFX-style primary colors (neon green `0x07E0`, pure red `0xF800`, etc.)
  instead of the brand palette every other screen uses. Rebased the shared `TFT_*` palette
  constants onto the exact web-dashboard tokens (`web/plant-dashboard/index.html` `:root`) and
  renamed them to match (`TFT_ACCENT`/`TFT_WARNING`/`TFT_DANGER`/`TFT_SURFACE`/`TFT_MUTED`) so a
  future edit can't silently drift back to neon colors.
- `+`/`-` brightness buttons were snapping straight to 100%/0% instead of stepping by 10. Root
  cause: the live slider-drag hit-region and the release-time button hit-regions used different,
  independently-hardcoded coordinates that overlapped by ~10px on the button's inner edge - any
  touch sample landing there mid-tap latched a "user is dragging" flag that made the release
  handler skip button detection entirely. Fixed by excluding the exact button hit-boxes from the
  drag-preview check.
- Dragging the slider flashed the whole modal panel - `drawBrightnessOverlay()` repainted the
  entire panel on every touch sample. Split into `drawBrightnessSliderRow()` for live updates
  (just the value text, track, fill, and knob) so a drag redraws a small strip instead of the
  whole modal.
- The low-moisture blink cadence (every 500ms) was redrawing every visible card, not just the
  blinking one(s) - `renderDashboardDataOnly()` now takes a `blinkOnly` flag and
  `slotNeedsBlink()` scopes it down.
- Auto-cal-learned moisture bounds/overdue flags were only ever applied on a full `renderUi()`
  rebuild, which almost never happens again once cards are on screen (see the `displayDirty`
  call sites) - a background auto-cal update could sit applied-but-invisible for a long time,
  then suddenly appear the moment something incidental (like closing the brightness overlay)
  forced a full re-render, looking like closing the menu itself caused a plant to flip to
  "overdue". Fixed with `refreshSlotMoistureBounds()`, called from the lightweight per-tick path
  too.

**Display corruption investigation (second pass, triggered by physical testing after the above):**
full write-up with code-level detail in `docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`
(status: RESOLVED). Summary:

- Symptoms: random red pixels in the animated plant art, frames occasionally looking like two
  poses superimposed, and (briefly, mid-investigation) one card's sprite flashing to a
  *different* card's sprite for about half a second. All started specifically after the
  touch/brightness feature was added - confirmed via `git diff HEAD` that the animation
  rendering code itself was untouched, pointing at an interaction with the new touch traffic on
  the shared SPI bus rather than a bug in the (unchanged) animation code.
- Root cause 1: `tft.writePixel()` issues a full column/row-address-set + memory-write command
  sequence (~13 bytes, several DC pin toggles) for *every single pixel*. The old animation
  renderer called this once per changed (diff) or opaque pixel, so a single frame update could
  issue hundreds of separate command sequences within one held SPI transaction - both slow and,
  per Codex's review, a large surface for a leaked/misinterpreted byte to land somewhere visible.
  Fixed by replacing the whole renderer (`drawPlantSpriteScaled`/`drawPlantSpriteDiffScaled`,
  both deleted) with a single `drawPlantSpriteComposited()` that resolves an entire row into a
  buffer (transparent pixels substituted with the background color) and sends it as one
  `writeAddrWindow()`+`writePixels()` burst - one command sequence per row instead of one per
  pixel, and no separate `fillRect()` pre-clear (eliminates the visible clear-then-rebuild flash
  a full redraw used to cause).
- Root cause 2 (found by Codex): `XPT2046_Touchscreen` runs in polling mode without an IRQ pin
  (deliberately, per an existing code comment - the library's own interrupt-based gating was
  found unreliable on this board), which means *every* `touchScreen.touched()` call performs a
  real SPI transaction, forever, whether or not a finger is on the glass - continuous idle bus
  traffic that didn't exist before touch was added. Those idle transactions could leak bytes into
  whatever ST7796 address window the display renderer had last left open, landing at a
  deterministic position (explaining the "2 pixels always in the same spot on every card"
  symptom precisely). Fixed with a passive `digitalRead(TOUCH_IRQ) == HIGH` check in
  `handleTouchInput()` before ever touching the SPI bus - skips the transaction entirely while
  idle (T_IRQ is active-low), but keeps polling once a touch is in progress so release detection
  still works. This is a level check re-evaluated every poll, not an attached interrupt, so it
  doesn't have the missed-edge failure mode the original no-IRQ design was written to avoid.
- Also closed two related but distinct redraw-coverage gaps found along the way, both in
  `drawPlantCardDataOnly()` (the per-tick fast redraw path, as opposed to a full `renderUi()`
  which repaints everything at once): (a) the art region's bottom edge and the "Moist" row's own
  clear rect didn't quite meet (a persistent-artifact risk, any stray pixel landing in the gap
  could never self-heal), and (b) the moist and battery rows were *always* fully cleared and
  repainted on every low-moisture blink tick regardless of whether either value had actually
  changed - including the battery row, which has nothing to do with a moisture alert. Both
  fixed: gaps closed with `min()`-based clamps that only ever extend coverage, and each row now
  caches its last-drawn value/color per `PlantArtSlot` and only repaints when something about it
  actually changed (the moisture row's *color* still updates every blink tick as the alert
  signal, but without the clear step, since same-width text/bar redraws cleanly in place).
- SPI clock (`DISPLAY_SPI_HZ`) was also dropped from the Arduino_GFX ESP32 default of 40MHz to
  10MHz as a signal-integrity margin trade, and the touch poll interval widened from 16ms to
  32ms - both cheap, low-risk changes tried before the two root causes above were found; kept in
  place since neither was shown to hurt anything, even though the T_IRQ fix turned out to be the
  one that actually mattered.

## 2026-08-12 Tap-to-Expand Plant Card Detail View

Built on top of Codex's same-day double-tap rework above (brightness overlay moved off the
header button onto a double-tap gesture specifically to free up plain single-tap for this).
Compiled clean and flashed to the display receiver on `COM16`; not yet physically tapped/verified
on the real touchscreen as of this handoff.

- Tapping a plant card now expands it in place into a diagnostics view - same card footprint
  (`x`/`y`/`w`/`h`), same rounded-rect/border/name-row/Moist/Batt-bar chrome as the normal card,
  with extra rows filling the region the plant sprite would otherwise occupy. Tapping again
  (the expanded card or anywhere else on the dashboard) collapses it back to normal. Only one
  card can be expanded at a time; only one plain tap is needed (not a long-press or a
  gesture) since double-tap is now reserved for the brightness overlay.
- New `drawPlantCardDetail(PlantArtSlot& slot)` in the `.ino` - self-contained (no split
  full/data-only variant like `drawPlantCard`/`drawPlantCardDataOnly`, since only one card is
  ever expanded, a full repaint of just that card on every refresh is cheap). Content density
  scales with the card's actual size using the *same* geometry `drawPlantArtInCard()` already
  uses to size the sprite (`y + 24`, height `max(24, h - 91)`) - the diagnostic rows are quite
  literally drawn into the space the art would have used, so bigger cards (fewer plants
  configured) automatically show more rows, and row spacing (`clampInt(infoH / N, min, max)`)
  stretches to fill taller cards instead of leaving them sparse. Below the `h < 76` compact
  threshold (already used elsewhere for the smallest grid size) it falls back to the existing
  compact Moist/Batt layout plus a small RSSI-quality dot in the corner, since there's no room
  for anything else. Wider cards (`w >= 200`, roughly 1-2 cards per row) get a two-column
  stat grid close to the phone app's Diagnostics tab (Probe, RSSI, Age, Wake, Received, Missed,
  Loss%, dry/wet range, plus raw oscillator kHz/charging state when the probe firmware sends
  raw fields); narrower cards get a single stacked column with fewer rows.
- Touch-state plumbing (`handleTouchInput()` in the `.ino`): a tap-up that *doesn't* get
  completed into a double-tap within `TOUCH_DOUBLE_TAP_WINDOW_MS` is a genuine single tap, but
  that can only be known once the window has passed with no second tap. New
  `resolvePendingDashboardTap(now)`, called every `loop()` iteration (not gated on the touch
  controller being polled, since the panel can sit idle with no finger on it for the whole
  window), checks elapsed time against `lastDashboardTapMs` and then calls
  `handleDashboardSingleTap(x, y)`. The tap's coordinates are stashed into dedicated
  `pendingSingleTapX/Y` at tap-up time rather than re-read from `lastTouchX/lastTouchY` when
  resolved later, because a second, unrelated gesture starting before the window elapses would
  otherwise overwrite those first.
- `handleDashboardSingleTap(x, y)`: if a card is already expanded, any tap collapses it;
  otherwise it hit-tests `artSlots[]` (already has each visible card's live `x/y/w/h`) and
  expands whichever card the tap landed on. Respects the same `wifiSetupPortalActive`/
  `pendingRestartMs` guard the double-tap path already used.
- `PlantArtSlot` gained a cached `plant[24]` name field (the detail view's periodic refresh
  path only has the `PlantArtSlot`, not the original JSON `card` object, to read from).
  Factored the plant/label/name/title-with-"Probe N"-fallback resolution logic that used to be
  inline in `drawPlantCard()` into a shared `resolvePlantName()` used by both
  `drawPlantCard()` and `cachePlantArtSlot()`, so the name shown in the detail view can never
  drift from the one shown on the normal card.
- `expandedCardSlot` (an `artSlots[]` index, -1 = none) is cleared automatically in `renderUi()`
  if a config reload leaves it pointing past the new card count - otherwise a reconfigure to
  fewer cards, then back up, could make an unrelated card appear "expanded" the next time the
  count grew back to include that index again.
- `renderAnimatedPlantArt()` skips the expanded slot (no sprite is drawn into the detail view,
  so there's nothing to animate there); the periodic sensor-render gate in `loop()` now also
  fires while a card is expanded (reuses the existing 2s `SENSOR_RENDER_INTERVAL_MS`, no new
  constant) so the Age/RSSI rows don't go stale for a full minute between real BLE updates.
- Not yet done: physical confirmation that a single tap on the real touchscreen expands the
  right card, that the double-tap-for-brightness gesture still works cleanly alongside it (no
  accidental double-tap-detected-as-single-tap-then-second-tap-expands-a-card ambiguity in
  practice), and that the smallest (4x4, 16-card) grid size's compact+dot fallback is legible.
  `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md` also still needs a pass to document the
  double-tap gesture and this feature - not done as part of this session.

## 2026-08-12 Surgical-Redraw Skill and Detail-View Refresh Fix

Follow-up to the tap-to-expand detail view above, after the user physically tested it: the
diagnostic content itself looked right, but the periodic refresh was visibly repainting more
than it should ("refresh issues"), and the header IP address had drifted left. Both fixed,
plus a new standing rule for all future display work. Compiled clean and flashed to `COM16`;
not yet re-verified on the physical screen as of this handoff.

- **New project skill: `.claude/skills/display-surgical-redraw/SKILL.md`.** Codifies a core
  tenet for this firmware going forward: once a display feature is on screen and just
  updating over time, its redraw path must only repaint the pixels that actually changed -
  never a whole card or the whole screen as routine/steady-state behavior, full repaints
  reserved for genuine one-time "this just appeared/changed layout" events. Grounded in real
  history, not just style preference: the shared ST7796/XPT2046 SPI bus (see
  `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`) turned "too many writes" into an actual
  correctness bug once already (`docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`), and a
  full-panel repaint on every touch sample is exactly what made the brightness slider look
  frozen/flashing before `drawBrightnessSliderRow()` split it out from
  `drawBrightnessOverlay()`. Documents the established full-draw/data-only function-pair
  pattern (`drawPlantCard`/`drawPlantCardDataOnly`, `drawBrightnessOverlay`/
  `drawBrightnessSliderRow`, and now `drawPlantCardDetail`/`drawPlantCardDetailDataOnly`
  below) with a checklist for applying it to the next new feature. Load this skill before
  writing or reviewing any `drawPlantCard*`/`render*`/`draw*` function in this firmware.
- **Root cause of the detail-view refresh issue**: the first cut of the feature had only one
  drawing function, `drawPlantCardDetail()`, and the periodic refresh path (`loop()`'s
  `detailOpen` gate, every ~2s while a card is expanded, plus every low-moisture blink tick)
  called it directly - meaning a full `fillRoundRect` background repaint, a full info-band
  `fillRect`, and every diagnostic row got redrawn unconditionally on every single tick, even
  when nothing had changed. This is the exact anti-pattern the new skill exists to catch.
- **Fix**: added `drawPlantCardDetailDataOnly()`, the data-only counterpart. Never fills the
  card background or the info band; only the border re-strokes unconditionally (cheap, matches
  `drawPlantCardDataOnly()`'s own existing precedent). Every diagnostic row (signal
  label, RSSI, age, wake, loss, received, missed, moisture range, raw oscillator/charging) is
  diffed against per-slot cached *formatted text* (new `PlantArtSlot` fields,
  `lastDetailRssiText` etc. - caching the formatted string rather than the raw value avoids a
  separate "how is this bucketed for display" logic path that could drift out of sync with
  what's actually on screen) and only clears+redraws via a new
  `drawDetailStatRowIfChanged()` helper when the text actually differs. Moist/Batt rows reuse
  the exact same diff-and-redraw logic (and the same cache fields) as
  `drawPlantCardDataOnly()`'s own moist/batt rows. `renderDashboardDataOnly()` now calls
  `drawPlantCardDetailDataOnly()` for the expanded slot on every periodic tick;
  `drawPlantCardDetail()` (full paint) is only reached from `renderUi()`'s full rebuild, i.e.
  the moment a card is first expanded or the layout otherwise changes.
- To keep the two draw functions from silently drifting apart on what a row's text/color
  should be (a real risk once the logic exists in two places), extracted all the
  formatting/computation into a shared `computeDetailStats()` filling a new `PlantDetailStats`
  struct, called by both functions; `seedDetailCache()` (called at the end of the full paint)
  seeds every field the data-only path diffs against. `PlantDetailStats` had to be defined up
  near `PlantArtSlot`/`DashboardGrid` (not next to where it's actually used) - same reason
  those two already have manual prototypes at that spot: Arduino's auto-generated-prototype
  insertion point is before any struct definitions further down the file, so a function
  signature referencing a not-yet-declared struct type fails to compile otherwise. Hit this
  exact error on the first compile attempt, moved the struct, fixed on the second.
- **IP address position fix**: the header's `App <ip>` text was right-anchored at
  `tft.width() - 50` - a margin sized to clear the old header sun button. Now that Codex's
  double-tap rework removed that button entirely (see the section above), the reserved 50px
  was just empty space, making the address look like it had "slid left." Changed to
  `tft.width() - 8`, matching the title's own 8px left margin for a symmetric header.
- **User-confirmed working on the physical receiver 2026-08-12**: the expanded card's periodic
  refresh no longer flashes, and the header IP address is back in the top-right corner.

## 2026-08-13/14 Full-Repo Hostile Review Pass

User asked for a from-scratch, "reviewer on a bad day" pass over the entire repo -
every Arduino sketch, every Python script, the Supabase SQL/edge functions, and the
web dashboard - not a single feature. Done as 8 parallel read-only review agents (one
per area), triaged by hand, fixed, compile/syntax-checked, then committed and
deployed end-to-end the same session. Everything below is live as of this handoff,
not just committed - see the deploy status note at the end of this section.

**Probe firmware:**

- `MSV2_Moisture_BLE_Sleep_Alocasia.ino` was missing the charge-settle poll-loop fix
  its own header comment claimed was already ported from Scindapsus -
  `countOscCycles()` drove the plate HIGH and released it to `INPUT` with zero wait,
  unlike Scindapsus's validated poll loop (same "locks onto a value, then jumps"
  quantization risk this exact fix already closed once on the other probe). Fixed
  in the file, compiled clean. **Not yet flashed to the physical Alocasia board** -
  user said to do that later, not part of this session's hardware work.
- `MSV2_Moisture_Probe_Test.ino` (the bench reference this sensing approach was
  ported from originally) had drifted stale, still reproducing two already-fixed
  real-firmware bugs (`SAMPLE_WINDOW_MS=1`, a fixed 40-NOP charge-settle delay).
  Updated to match the deployed, validated approach so future bench A/B comparisons
  aren't measuring already-fixed bugs.

**Display receiver (`MSV2_ESP32_Display_Runtime.ino`, `MobileAppPage.h`) - flashed
and user-confirmed working on the physical receiver (`COM16`):**

- `isAdminActionAllowed()` used a raw `millis() >=` comparison instead of the
  rollover-safe idiom used elsewhere in the file - would have silently reopened the
  unauthenticated local admin API after ~49.7 days of uptime. Fixed.
- `DEFAULT_STALE_SECONDS` was still `3900` (sized for a stale 30-min probe-cadence
  assumption, flagged as open in the "Suggested Next Work" list below as of
  2026-08-09) - dropped to `180` to match the current ~60s cadence. **This closes
  that open item.**
- `displayDirty`/`staleSeconds` globals made `volatile` (cross-core race between
  the cloud-sync task and the render loop); dry/wet moisture bounds now clamped to
  prevent a signed-overflow path in `moisturePercent()`.
- `MobileAppPage.h`: `saveWifi()`/`forgetWifi()` were dumping raw JSON error bodies
  into the UI instead of the parsed `error` field `saveConfig()` already used
  correctly (new shared `parseErrorText()` helper now used by all three);
  `loadReadings()`/`loadWifi()`/`loadClaim()`/`loadCloud()` had no error handling,
  so a dropped connection could leave the status text frozen on a stale
  "Updated HH:MM:SS" forever; added a duplicate-Probe-ID check before save; fixed
  an inconsistent HTML-escaping gap on RSSI/moisture/battery values.
- Replaced the pairing QR code's dependency on a third-party rasterizing service
  (`api.qrserver.com`) with a vendored, fully client-side QR encoder
  (`kazuhikoarase/qrcode-generator`, MIT, verified via direct download plus a
  minified-vs-unminified byte-comparison test before embedding, +21KB flash / 67%
  total). The third party was free and worked over the internet, but was
  completely unreachable when viewing the pairing screen over the receiver's own
  local-only AP (a normal supported mode) - the QR silently never rendered there.
  **User confirmed on the physical receiver post-flash: dashboard renders fine and
  the new on-device QR code looks good.**

**Other Arduino sketches:**

- `MSV2_BLE_Adv_Receiver.ino`: bounded the `while(!Serial)` boot wait to 3s (was
  the only sketch in the repo with an unbounded one); fixed the missed-packet
  counter's wraparound-on-reboot math (same fix already live in the main
  receiver - this is the "cosmetic, not fixed" item first noted in the
  2026-07-24 section, closed here for this sketch; the main receiver's own
  version turned out to already be fixed on inspection, just undocumented).
- `MSV2_ESP32_DeviceIdentity_Dump.ino`: `LittleFS.begin(true)` was formatting the
  filesystem on the exact mount-failure case this "never writes/formats" rescue
  tool exists to recover data from. Changed to `begin(false)`.
- `README.md`: TFT pin table (DC/RST/CS) was cyclically swapped vs the real
  firmware constants - would have misdirected anyone wiring new hardware.

**Supabase SQL - migrations 019, 020, 021 all pushed to the live project and
functionally verified (not just deployed):**

- 019 (`msv2_receiver_display_settings`) had been written and committed in an
  earlier session (`516838a`) but, it turns out, never actually pushed -
  `supabase migration list --linked` showed it missing remotely; confirmed this
  wasn't the known migration-tracking-drift class of issue (see 2026-07-07
  section) by querying `pg_proc` directly and confirming its functions genuinely
  didn't exist live yet, so this was a clean push, not a repair situation.
- 020: `msv2_read_share_sensor_history` and `msv2_clear_share_sensor_history` were
  the only two RPCs in the schema's history still granted execute to
  `anon, authenticated` - every sibling RPC has been `service_role`-only since the
  2026-07-07 hardening pass, so these two were reachable directly via the public
  anon key, bypassing the edge function's auth/CORS layer entirely. Locked to
  `service_role` to match. Also adds a missing index for the id-cursor
  sensor-history pagination introduced in 014/018 (was doing a full scan).
- 021: mitigates (does **not** fully close - see the migration's own SQL comments
  for the full reasoning) the `receiver-bootstrap` registration race, where
  `receiver_id` is derived directly from the device's own MAC address and is
  passively observable by anyone in WiFi range of its setup AP before the real
  device's own first bootstrap call. A cryptographically complete fix needs a
  manufacturing-time pre-shared secret, which would break this project's
  deliberate zero-touch onboarding design (see "Important Preferences" below) -
  that tradeoff wasn't made unilaterally. Instead: requires a non-empty claim code
  on first-time registration (costs the real device nothing - it always has one
  ready by the time it calls bootstrap) and adds a global rate limit
  (`register-new-global`, 20/hr) on brand-new-receiver creation, which the
  existing per-receiver_id limit does nothing to stop for a bulk/speculative
  squatting campaign across the whole MAC-derived id space. Verified live by
  directly calling `msv2_register_receiver` on the real DB with a disposable test
  id: confirmed it now genuinely rejects a claim-code-less registration and
  succeeds with one; test row deleted after.

**Supabase edge functions - `plant-dashboard` and `receiver-snapshot` redeployed
and verified live:**

- `plant-dashboard/index.ts`: malformed JSON bodies on 4 POST routes were escaping
  to the top-level catch, which returns an HTML error page - breaking
  JSON-consuming clients and violating the function's own "API-only JSON"
  contract (see the 2026-07-07 section's note about this exact HTML-vs-JSON
  boundary). New `parseJsonBody()` helper surfaces a clean 400 instead - confirmed
  live post-deploy by POSTing deliberately malformed JSON to `/api/share-claim`
  and getting back `{"ok":false,"error":"invalid JSON body"}` with
  `content-type: application/json`, not HTML. Also deleted the confirmed-dead
  320-line `renderApp()` (zero references, the "dead code" item flagged as
  out-of-scope in the 2026-07-07 section - closed here). Clamped
  `sinceHours`/`limit`/`beforeId` on the history route.
- `receiver-snapshot/index.ts`: same `sinceHours`/`limit` clamp on the GET history
  route; bounded the `snapshot.plants` array before looping one RPC call per
  entry (was unbounded).

**Web dashboard - deployed to Netlify production (`https://msv2-plants.netlify.app`)
and verified against the live URL, not just the draft:**

- The app's own copy of `assets/plant_gifs/` and its art-key catalog were missing
  all 7 `color_*` sprites added in the 2026-07-06 pipeline update (this is the
  "known, deferred" item from that section's own writeup, finally closed) -
  anyone using those plants silently saw the wrong icon. Copied the GIFs in,
  updated the catalog array and the service worker's precache list.
- Fixed a real camera-privacy bug in the QR scanner: an out-of-order
  `getUserMedia()` resolution from a fast double-tap on "Scan QR" could overwrite
  the shared `state.scanStream` field, so the stream actually feeding the visible
  video element lost its only reference and was never `.stop()`'d - the camera
  hardware indicator could stay on indefinitely after the user thought they'd
  stopped scanning.
- Confirmed via research (Netlify's own docs: header values are concatenated
  across every matching rule, not overridden by the more specific one) that all
  three header configs (`netlify.toml` x2, `_headers`) had a broad `/*` and a
  specific `/assets/*` rule both setting `Cache-Control` - per RFC 7234,
  `no-store` anywhere in that concatenated list wins, silently defeating the
  long-cache/immutable strategy for every versioned asset since those files
  existed. Restructured into non-overlapping path-scoped rules in all three
  files; confirmed live via `curl -I` against the production URL post-deploy that
  `app.v12.js`/the GIFs get clean `public,max-age=31536000,immutable` and
  `index.html`/`sw.js` get clean `no-store`, no concatenation.
- Added missing `.catch()` handling on the Connect and Refresh button click
  handlers; disabled the Connect button for the duration of its request.

**Python tooling** (`scripts/`): `mock_cloud_server.py` had drifted from the real
Supabase functions' actual response shapes; `esp32_packet_loss_test.py`'s loss-rate
math silently misreported instead of flagging a mid-test reboot;
`generate_planter_sheet_gifs.py` hardcoded a transparency palette index without
verifying the placeholder color actually quantized there (could silently corrupt
shipped sprite art); `generate_plant_sprites.py` had a `ZeroDivisionError` on the
documented valid `--max-poses 1` and a `slugify()` bug on digit-leading filenames;
`antenna_rssi_logger.py` had no serial-reconnect handling; `fetch_datasheet.py`
could leave a truncated file trusted by later runs. All fixed.

**Deploy status - everything from this pass is fully live, not just committed:**
git commit `2d7c7e4`; Supabase migrations 019/020/021 pushed and functionally
tested against the real DB; both edited edge functions redeployed and verified;
web dashboard on Netlify production, verified; display receiver firmware flashed
and user-confirmed working on physical hardware. **Not yet flashed to physical
hardware**: the Alocasia probe fix and the other Arduino sketch fixes
(`MSV2_BLE_Adv_Receiver`, `MSV2_ESP32_DeviceIdentity_Dump`,
`MSV2_Moisture_Probe_Test`) - committed to git, compiled clean, deliberately
deferred per the user ("no lets do that later").

Full findings/triage detail (including everything checked and found clean, e.g. no
leaked credentials anywhere in git history, the 2026-07-07 RPC-lockdown holding for
every function through migration 021) lives in this session's Claude memory
(`msv2_full_repo_review_2026-08.md`) for whoever picks this back up.

## 2026-08-17 Auto-Calibration Redesign Implementation

Implements all six items from the "2026-08-16 Auto-Calibration ('Learning Mode')
Redesign" section directly below. Code changes only in this pass - **not yet deployed**:
the new migration hasn't been pushed to Supabase, the two edge functions haven't been
redeployed, the Netlify site hasn't been redeployed, and no firmware has been flashed to
physical hardware. Both firmware sketches (`MSV2_ESP32_Display_Runtime`,
`MSV2_Moisture_AFE_Test`) compile clean via `arduino_display.ps1`/`arduino_moisture_afe_test.ps1
-Action compile`.

Two open design questions from the punch list were resolved with the user before
implementing:
- **Item 1 (daysSinceLastWatering fate)**: keep it, informational only. The event-clustering
  SQL (`rises` CTE) and the clustering loop in `deriveAutoCal()` both stay; only the
  `overdue`/`predictedIntervalDays` forcing/alerting behavior built on top was removed.
- **Item 6 (drift guard design)**: "track drift as its own signal" (of the three candidates
  listed) - a flag, not a clamp, so a genuinely lower-water plant's learned range can still
  adapt; a household with chronic late watering instead sees a visible notice.

**Item 1 - overdue/predicted-interval removed:**
- `supabase/functions/receiver-snapshot/index.ts`'s `deriveAutoCal()`: dropped
  `predictedIntervalDays`/`overdue` computation and `AUTOCAL_MAX_EVENTS_FOR_INTERVAL`;
  `daysSinceLastWatering` and the `rises`-clustering loop stay.
- **Follow-up fix, live-tested and deployed same day**: `MobileAppPage.h`'s "watered X ago"
  note (see item 5 below) originally used `daysSinceLastWatering`, a rolling
  hours/24 float with no concept of calendar-day boundaries - watering at 11pm and
  checking at 9am the next morning is ~10 hours ago (rounds to "watered today") even
  though it's clearly yesterday. User caught this live on the physical device. Fixed by
  adding `lastWateredAtMs` (the actual event timestamp) alongside the existing float
  everywhere in the pipeline (`deriveAutoCal`'s return, `AutoCalState`, both
  `sensorDataJson()`/`snapshotJson()` emit sites, `runCloudAutoCalPull()`'s parse) and
  rewriting `MobileAppPage.h`'s label function (`daysAgoText` → `wateredAgoText`) to
  compare local calendar dates (`new Date(ms).setHours(0,0,0,0)`) instead of a rolling
  24h window - this can only be done client-side, since the server has no idea what
  timezone the viewer is in. Compiled, deployed (`receiver-snapshot` redeployed), and
  reflashed to the physical receiver same session - confirmed live.
- Firmware (`MSV2_ESP32_Display_Runtime.ino`): `resolveMoistureColor()` deleted entirely
  (call sites now call `healthColor()` directly, per the punch list's own note that it
  "collapses to plain healthColor()"). `resolveMoistureBounds()`/`refreshSlotMoistureBounds()`
  no longer output/track `overdue`; `slotNeedsBlink()` (shared by the blink alert and the
  buzzer's `serviceWaterAlert()`) dropped its overdue OR-clause, so both alerts are now
  purely "moisture < 25%".
- `MobileAppPage.h`: `renderStatus()`'s `overdue` computation removed; the Sound Alerts
  copy reworded from "stays overdue for water" to "stays critically low on water".
- `web/plant-dashboard/assets/app.v12.js` → renamed to **`app.v13.js`** (cache-bust, same
  reasoning as the v10→v11 and v11→v12 bumps earlier this project - an in-place edit
  wouldn't reach already-installed Home Screen PWAs). `resolveMoisture()`'s overdue-forces-red
  logic removed. `index.html` and `sw.js` (cache name + precache list) updated to match.

**Item 3 - manual recalibration/reset control:**
- New migration `supabase/migrations/023_msv2_calibration_reset_and_drift.sql`: table
  `msv2_sensor_calibration_reset` (receiver_id, sensor_id, reset_at) + RPC
  `msv2_reset_sensor_calibration(share_token, receiver_id, sensor_id)`, same share-token
  household-auth pattern as `msv2_clear_share_sensor_history` (016).
  `msv2_read_sensor_auto_calibration` rebuilt to clamp its percentile window's lower bound
  to `greatest(now() - lookback, reset_at)` - History-tab data is untouched, only the
  calibration math ignores anything before the reset.
- `supabase/functions/plant-dashboard/index.ts`: new route
  `/api/share-autocal-reset` (`shareAutocalResetApi`). Deliberately **not** added to the
  Netlify dashboard - matches the existing read-only-Netlify decision (see 022's retired
  share-display RPCs).
- `MobileAppPage.h` Setup tab: new "Reset calibration" button next to each auto-enabled
  card's checkbox (`resetSensorCalibration()`), using the same self-minted share-token
  pattern as `loadHistory()`/`clearAllHistory()` - no new receiver-local endpoint needed.

**Item 4 - AFE bench sensor smoothing:** `MSV2_Moisture_AFE_Test.ino` now transmits
`smoothedMv` (rounded) instead of raw `medianMv` - one line, the EMA was already computed
and logged to serial, just never wired into `advertiseReading()`.

**Item 5 - raw-to-percent moisture text once calibrated:** `resolveMoistureBounds()` gained
an `outCalibrated` flag (true only when auto-cal bounds are actually in use, i.e. the old
early-return branch). `drawPlantCard()`/`drawPlantCardDataOnly()`/`computeDetailStats()` all
switch their moisture text from `"%u"` to `"%u%%"` when calibrated; the data-only diff
tracking (`slot.lastMoistureCalibrated`) was extended so a calibration-readiness flip alone
(without a raw-value change) still triggers a redraw. `MobileAppPage.h`'s `renderStatus()`
does the same for the local app.

**Item 6 - dry-bound floor clamp (revised twice after further discussion the same day):**
the first cut (bucketed 3-window monotonic-decrease flag, informational only) was
superseded before deployment once the user clarified the actual goal - stop chronic
over/under-watering from destroying the recommendation interval, not just get notified
about it, with no absolute ground-truth moisture value available to clamp against (differs
per plant/pot/soil). The second cut (flat `AUTOCAL_DRY_FLOOR_MAX_DROP = 15` raw units) was
itself revised once the user pointed out a flat unit count is meaningless across this
deployment's three probes, which sit on very different raw scales (confirmed live via
Supabase query mid-session: sensor 1 at `dryBound=183/wetBound=358` vs sensor 3 at
`dryBound=1078/wetBound=1265`). **Landed design: anchor each sensor to its own first
full-lookback-window calibration (not the earlier 3-day "ready" gate - the first 21 days
run completely unclamped so several real watering cycles get observed first), then
hard-clamp the dry bound to never drift more than `AUTOCAL_DRY_FLOOR_MARGIN_PCT` (10%,
provisional) of that sensor's own anchored (wet - dry) range below its anchor.**
- Table `msv2_sensor_calibration_reset` (migration 023, rewritten in place twice since
  never deployed) gained `anchor_dry_bound` and `anchor_wet_bound`, both nullable, separate
  from `reset_at` (also nullable) so a row can exist for anchor-only or reset-only reasons
  independently. The two anchor columns are always written together (the margin needs both
  to mean anything).
- New RPC `msv2_set_sensor_calibration_anchor_if_unset(receiver_id, sensor_id, dry, wet)`
  (service_role only, no share-token - internal system write, not a user action):
  idempotent, only ever fills in a null anchor pair, never overwrites an existing one.
- `msv2_reset_sensor_calibration` (item 3's manual reset button) now also clears both
  anchor columns to null - a reset means "different plant/pot," so the old anchor is as
  stale as the old learned range; a fresh one gets captured automatically once the
  newly-reset window is full again.
- `msv2_read_sensor_auto_calibration` returns each sensor's `anchorDryBound`/`anchorWetBound`
  alongside the raw computed `dryBound`/`wetBound`; the earlier 3-bucket CTEs from the first
  revision are gone entirely.
- `receiver-snapshot/index.ts`'s `deriveAutoCal()` does the actual clamping, gated on a new
  `AUTOCAL_LOOKBACK_DAYS = 21` (must match the SQL's `p_lookback_days` default) full-window
  check, separate from and later than the existing 3-day `ready` gate: if the window is
  full and no anchor exists yet, it flags `needsAnchor` (the GET handler then calls
  `msv2_set_sensor_calibration_anchor_if_unset` with the current raw dry+wet bounds,
  best-effort/fire-and-await, not blocking on failure); if an anchor exists, `dryBound` in
  the response is clamped to `max(rawDryBound, anchorDry - 0.10*(anchorWet - anchorDry))` -
  this is the value firmware/app actually use for percent/color, so the interval genuinely
  cannot collapse past the floor, not just get a warning about collapsing.
- `dryBoundTrendingLower` **keeps its exact name and JSON field** but now means "the clamp
  is actively engaged" rather than "3 buckets happened to decrease" - zero firmware/app
  changes needed as a result. Physical-display Range-row recoloring and the local app's
  `.notice.warn` line both still work unmodified, now backed by a real corrective action
  instead of a passive observation.
- Researched via web search before landing on the clamp-vs-flag decision - see the
  "shifting baseline"/"boiling frog" drift literature and the CGM/irrigation-controller
  domain comparisons discussed in chat; the chosen approach mirrors the "sanity min/max on
  an adaptive baseline" pattern from monitoring tooling, anchored per-sensor since no
  universal ground truth exists.
- **Live-queried mid-session, not deployed from**: pulled real numbers for the actual
  deployed receiver (`MSV2088A3104A7AC`) via `supabase db query --linked` to answer a live
  user question ("why does 297 read as ~65%?"). Confirmed the currently-*live* (pre-this-session,
  unmodified) auto-cal already shows sensor 1 (Thaumatophyllum) at `dryBound=183`,
  `wetBound=358` - and a day-by-day min/max breakdown showed a suspiciously flat 8/2-8/8
  plateau (~180-183 every day, near-zero variance) sitting inside the 21-day window, lining
  up exactly with the pre-8/9 QSPI battery bug (see that section above) rather than genuine
  soil dryness. Real range since the 8/9 fix looks like ~280-430. Not a bug in anything
  built today - this is live evidence of exactly the failure mode item 6 targets, plus a
  reminder that whichever sensor gets its anchor captured first after deployment should
  probably get a manual "Reset calibration" first if its window still contains pre-fix
  data, so the anchor isn't captured from a still-corrupted floor.

**Not done in this pass:**
- Nothing pushed/deployed/flashed - see the note at the top of this section.
- `web/msv2-plant-dashboard-static.zip` (mentioned elsewhere in this doc) was not
  regenerated; it will be stale against `app.v13.js` until a fresh export happens.
- No physical-hardware testing of the reset-calibration button, the percent-text switch,
  or the drift notice - compiles clean but unverified on real screens/probes.

## 2026-08-16 Auto-Calibration ("Learning Mode") Redesign — Handoff Notes

User reviewed how the moisture auto-calibration/"learning mode" system actually works (see
`supabase/migrations/017_msv2_sensor_auto_calibration.sql` and `deriveAutoCal()` in
`supabase/functions/receiver-snapshot/index.ts` for the current implementation) and wants
several changes. Nothing below is implemented yet - this is the punch list for next session.

**1. Remove the predicted-watering-interval / "overdue" feature entirely.** User's reasoning:
"the whole purpose of this system is to go off of moisture not timing, that's its whole upside" -
a calendar-style watering-schedule prediction cuts against the product's actual value
proposition. Touches every layer that currently reads `autoCal.overdue`:
- `receiver-snapshot/index.ts`'s `deriveAutoCal()` (`AUTOCAL_CLUSTER_GAP_MS`,
  `AUTOCAL_MAX_EVENTS_FOR_INTERVAL`, the event-clustering loop, `predictedIntervalDays`,
  `overdue`) - remove the computation. **Check before deleting the "rises" pipeline entirely**:
  the SQL function's `rises` CTE (candidate-jump flagging: gap ≤3h, delta ≥15% of range) exists
  only to feed this clustering - if nothing else in the system reads `rises`, that whole
  candidate-detection stage in `msv2_read_sensor_auto_calibration` becomes dead weight too and
  should come out alongside it, not just the TypeScript consumer.
- Firmware: `resolveMoistureColor()`'s `overdue` parameter (forces dangerColor) and
  `refreshSlotMoistureBounds()`/`resolveMoistureBounds()`'s `slot.overdue`/`outOverdue`
  assignments - `resolveMoistureColor()` collapses to plain `healthColor()` once this is gone,
  could be inlined/removed.
- `MobileAppPage.h`'s `overdue` computation in `renderStatus()`.
- `web/plant-dashboard/assets/app.v12.js`'s `resolveMoisture()` - **note this was added THIS
  SAME SESSION** (2026-08-16, to fix the Netlify/display color mismatch) specifically to mirror
  the firmware's overdue-forces-red behavior. Once the firmware behavior changes, this needs a
  matching follow-up edit, not just a one-time fix-and-forget.
- Open question for tomorrow, not decided here: does `daysSinceLastWatering` (informational,
  not a forcing/alerting signal by itself) stay as a nice-to-have display stat, or does the
  whole watering-event-detection apparatus go? User's stated reasoning suggests the latter, but
  wasn't explicitly asked.

**2. No changes to the 21-day calibration window or the 3-day minimum calibration time** -
explicitly confirmed good as-is (`msv2_read_sensor_auto_calibration`'s `p_lookback_days := 21`
default, and `AUTOCAL_MIN_DAYS_READY = 3` / `AUTOCAL_MIN_SAMPLES_READY = 50` in
`receiver-snapshot/index.ts`).

**3. Add a manual recalibration/reset control** - a way to force a sensor back into "learning"
mode and rebuild its range from fresh data, for cases like moving a probe to a new plant/pot.
Not a trivial flag flip: readiness and the dry/wet bounds are computed live from a rolling
window over real `msv2_sensor_history` rows (`min(received_at)` as the readiness clock, 21-day
percentile window for the bounds) - there's no separate "calibration state" to simply clear.
Two directions, leaning toward the second:
- Destructive: delete/exclude that sensor's history before the reset point. Loses real
  historical chart data - probably wrong.
- **Recommended: a per-sensor "calibration epoch" timestamp** (new small table, e.g.
  `receiver_id` + `sensor_id` + `reset_at`, or a column somewhere queryable by
  `msv2_read_sensor_auto_calibration`) that the auto-cal SQL function clamps its window's lower
  bound against (`greatest(now() - interval, reset_at)`) instead of using
  `now() - lookback_days` unconditionally. Keeps full history intact for the History tab/charts;
  only the calibration math ignores anything before the reset. Needs its own migration and a
  new RPC (e.g. `msv2_reset_sensor_calibration`) plus a UI control - most natural home is the
  local app's Setup tab next to the existing auto-calibrate checkbox, not something exposed on
  Netlify (stays consistent with the read-only-Netlify decision from earlier this session).

**4. More smoothing on the AFE bench sensor's ADC readings** - "this data jumps around a lot
even with our current implementation." **This one has a concrete, already-diagnosed fix**, found
earlier this same session: `MSV2_Moisture_AFE_Test.ino` already computes an EMA-smoothed value
(`smoothedMv`, alpha 0.2) every reading cycle - but only prints it to serial for diagnostics.
The value actually broadcast over BLE and stored in history is `medianMv` (line 149,
`advertiseReading(medianMv)`), the raw median-of-5, not the smoothed one. The smoothing work is
already written and never wired up. Fix: transmit `smoothedMv` (rounded) instead. If that's not
smooth enough once it's actually live, tune the alpha constant down (e.g. 0.2 → 0.1) for a
slower-moving average - straightforward once the EMA is actually in the transmit path.

**5. Once the 3-day calibration period finishes, switch the displayed value from raw to
percentage as a visual "done calibrating" indicator.** Confirmed the physical display currently
always shows the raw value as text regardless of calibration state - `drawPlantCard()`
(`MSV2_ESP32_Display_Runtime.ino` ~line 3086-3094): `snprintf(moistureText, ..., "%u",
reading->moisture)`, with the percent value only driving the color bar underneath, never the
text itself. Same pattern repeats in the diff-redraw siblings (`drawPlantCardDataOnly()`
~3126-3154, `computeDetailStats()` ~3258-3280) - all three need the same change, not just one.
Proposed behavior: once a sensor's bounds are auto-cal-derived (not manual/preset), swap the
text to `"%u%%"` with `moisturePct` instead of the raw value. Also applies to `MobileAppPage.h`'s
`moistureMetric` (currently always shows the raw value too) for consistency between the display
and the local app.

**6. Guard against the learned range silently drifting over time from chronic late watering.**
Real concern, not yet addressed anywhere: since `dryBound` is the 5th percentile of a rolling
21-day window, a household that's consistently late watering will see each cycle's unusually-low
minimum become part of the new normal, pulling `dryBound` progressively lower - the alert
threshold chases the neglectful behavior instead of holding a meaningful floor, exactly
backwards from what an alert system should do. Not designed yet; candidate directions to weigh
next session, none chosen:
- Clamp the learned `dryBound` so it can never drift more than some fixed margin below the
  original manual/preset value - anchors drift to a sane starting reference but caps how far
  genuine long-term adaptation (e.g. a plant that's just naturally drought-tolerant) can go too.
- Track the learned bound across successive windows and flag a sustained one-directional drift
  as its own signal ("this plant's watering has been trending later") rather than only guarding
  against it - turns the failure mode into a feature.
- Keep the adaptive bound for day-to-day color/percent display, but maintain a separate,
  slower-moving or fixed "hard floor" that still fires red/danger regardless of what the rolling
  window currently considers normal.

## Last Known User Feedback

- Display and app are working well after the WiFi/setup fixes.
- User confirmed the first-time setup/local network flow looks good for now.
- User noticed a brief visual artifact where pixel art stayed visible during the reboot message; this was fixed and flashed in `2026-07-05 reboot-screen-fix`.
- New plant GIF additions are working in the dropdown after regeneration/flash.
- Latest sprite feedback: normalized old and new sprites now look much closer in size after the sheet-derived GIFs were upscaled before sprite generation.
- Version numbers are visible and incrementing.
- Mock cloud test concept makes sense and looks good.
- 2026-08-12: user confirmed the touch/brightness UI now matches the rest of the app, the
  `+`/`-` brightness buttons step correctly, the slider tracks smoothly with no flash, and all
  animation corruption (red pixels, superimposed frames, the probe-sprite swap, and the
  moist/batt row flashing on every blink tick) is resolved - "everything looks good now."
- Later 2026-08-12 hardware feedback found the small header brightness activator still required
  many attempts. The sun button was removed and replaced with a release-based double tap anywhere
  on the dashboard (120-650ms between releases); wake-only touches and resistive contact bounce do
  not count. XPT2046 cannot provide a true two-finger gesture because it reports only one point.
- Final 2026-08-12 round: user confirmed the tap-to-expand plant card detail view works well on
  the physical receiver, including after the redraw-discipline fix - the periodic refresh no
  longer flashes the whole card, and the header IP address is back in the top-right corner -
  "this is all working great."
- 2026-08-14: after the full-repo review pass and flashing the display receiver, user confirmed
  on the physical hardware: "looks great, QR good too" - the dashboard renders normally
  post-flash and the new fully-offline client-side QR code (replacing the third-party
  api.qrserver.com dependency) renders correctly on the real screen.

## Important Preferences

- Prefer static checks and focused edits.
- Keep the ESP32 local-first.
- Cloud features should stay disabled unless explicitly enabled.
- User wants a product path suitable for family/friends: simple setup, phone app, remote monitoring eventually.
- User specifically wants future setup where each ESP32 already has its own ID/secret and, after WiFi setup, connects to cloud automatically without manually entering device IDs into Supabase.

## Suggested Next Work

Top of the list as of 2026-08-17 (the 2026-08-14 list below is superseded by ranking,
not by completion):

1. Deploy and verify the 2026-08-17 auto-calibration redesign implementation (see that
   section above): push migration `023_msv2_calibration_reset_and_drift.sql`, redeploy
   `receiver-snapshot` and `plant-dashboard` edge functions, redeploy the Netlify site
   (now on `app.v13.js`), flash `MSV2_ESP32_Display_Runtime` to the physical receiver, and
   flash/bench-verify `MSV2_Moisture_AFE_Test`'s smoothed-value fix. Then physically test:
   the reset-calibration button in the local app's Setup tab, the raw-to-percent moisture
   text switch once a sensor calibrates, and (once enough history exists to actually drift)
   the dry-bound trend notice. None of this has been deployed or hardware-tested yet -
   code-complete and compiling only.

Superseded by ranking, top of the list as of 2026-08-14 (the 2026-08-09 list below is
superseded by ranking, not by completion - its battery/hardware items are physical-world tasks
that session's software review pass didn't touch and are presumably still open):

1. Flash the probe/bench-sketch fixes from the 2026-08-13/14 review to their
   physical boards - explicitly deferred this session ("no lets do that later").
   Most important: `MSV2_Moisture_BLE_Sleep_Alocasia.ino`'s charge-settle poll-loop
   fix, a real quantization-risk bug on live hardware. Lower priority:
   `MSV2_BLE_Adv_Receiver.ino`, `MSV2_ESP32_DeviceIdentity_Dump.ino`,
   `MSV2_Moisture_Probe_Test.ino` (bench tools, not continuously deployed).
2. Decide whether the registration-race mitigation in migration 021 (claim-code-
   required first registration + a global new-receiver rate limit) is sufficient,
   or whether the residual risk (a determined, physically-proximate attacker can
   still win the race - see that migration's own SQL comments for why a complete
   fix needs a manufacturing-time secret) is worth a bigger design change. Not
   urgent given the actual threat model for a small family/friends deployment.
3. `claim-receiver` Edge Function still exists but is superseded by the no-login
   `plant-dashboard` share-claim flow - decide whether to keep it or retire it
   (open since the 2026-07-07 pass, still undecided).

Superseded by ranking, top of the list as of 2026-08-09 (item 6 below is now DONE -
see the 2026-08-14 section above):

1. Check the overnight battery test: has Alocasia's battery held within ~1-2% (fix working) or dropped ~8-12% (QSPI leak somehow still active)?
2. Recharge/swap Scindapsus's battery - it's been at/near 0% since ~Aug 7 and is blocking clean data from that probe on every front (moisture *and* the battery-fix test).
3. Bench-check Scindapsus's BQ25100 `~CHG` pin for noise (leading theory for its erratic battery% readings even before the QSPI fix - see the 2026-08-09 section above).
4. Continue the FDC2212/MSV3 PCB migration open items (`docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md`) - now with concrete field evidence (not just theory) that VDD-coupling is a real, currently-happening problem, both from low battery and from active charging.
5. Consider a firmware-only mitigation in the meantime: discard/flag moisture samples taken while charging or below a battery floor, independent of the FDC2212 hardware timeline.
6. ~~Revisit the receiver's `DEFAULT_STALE_SECONDS`~~ - **done 2026-08-14**, dropped 3900 -> 180.

Superseded, top of the list as of 2026-07-25:

1. Check tonight's overnight results (via the now-fixed History tab, or `scripts/fetch_supabase_history.py`): are oscillator readings solid/stable in real soil (vs. last night's RC-timing sawtooth-to-saturation), and has auto mode's "ready" gate cleared for any sensor yet.
2. Confirm the 100kΩ pulldown is actually populated on the deployed field probe(s), not just the bench rig — the firmware can't compensate if it isn't there.
3. Tomorrow's solar-panel charging test in direct sunlight — panel only appeared to charge in direct sun, not even slight shade; the battery EMA smoothing landed specifically so this test's charging curve reads cleanly.
4. Once real soil data exists, revisit the provisional dry/wet defaults (currently 120/175, bench-touch-derived) - or just lean on auto mode once enough per-sensor history accumulates, rather than hand-tuning again.
5. ~~Update `web/plant-dashboard`'s hardcoded art catalog~~ - **done 2026-08-14**, all 7 `color_*` GIFs and catalog keys added, deployed to production.
6. ~~Fix the receiver's missed-packet-counter wraparound-on-reboot bug~~ - **done 2026-08-14** for `MSV2_BLE_Adv_Receiver.ino`; the main receiver's own `onResult()` was found already correctly guarding this (undocumented until now).

Older, still open from the 2026-07-07 pass:

1. Recover the original 8 plant card names/art from the last cloud snapshot (query in the section above) and push them back with `push_display_ui.py` — the device is currently running the generic `ui/display_default.json` placeholder cards.
2. Work through the Medium/Low findings from the 2026-07-07 review that were explicitly out of scope for the security pass (see that section for the list).
3. `claim-receiver` Edge Function still exists but is superseded by the no-login `plant-dashboard` share-claim flow — decide whether to keep it or retire it.

## Quick Test Commands

Fetch the probe's Supabase sensor history into a CSV (survives the laptop being off overnight):

```powershell
python scripts\fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 1 --since-hours 24 --csv data\sensor_logs\alocasia_overnight.csv
python scripts\plot_sensor_readings.py --csv data\sensor_logs\alocasia_overnight.csv --title "Alocasia overnight"
```

Log probe readings directly from this laptop instead (requires the laptop to stay on):

```powershell
python scripts\log_sensor_readings.py --url http://10.0.0.236 --sensor 1 --interval 5 --csv data\sensor_logs\local_log.csv
```

BLE packet-loss test against the display receiver's `/data`:

```powershell
python scripts\esp32_packet_loss_test.py --url http://10.0.0.236 --sensors 1 --duration 600 --csv packet_test.csv
```

Mock cloud:

```powershell
python scripts\mock_cloud_server.py --host 0.0.0.0 --port 8088
```

Relay one snapshot from ESP32 to mock cloud:

```powershell
python scripts\relay_snapshot_once.py --receiver http://BOARD_IP --cloud http://127.0.0.1:8088
```

Inspect mock cloud snapshots:

```powershell
curl http://127.0.0.1:8088/api/v1/snapshots
```

Open the mock cloud dashboard:

```text
http://localhost:8088/
```

Direct receiver uploader test through the `/cloud` API:

```text
http://PC_LAN_IP:8088/api/v1/receivers/RECEIVER_ID_FROM_APP/snapshot
```

Receiver local checks:

```text
http://BOARD_IP/app
http://BOARD_IP/cloud
http://BOARD_IP/snapshot
http://BOARD_IP/device
http://BOARD_IP/config-meta
```
# 2026-08-22: receiver lossless history batching deployed

- Fixed the lossy five-minute history path. The receiver now queues every unique BLE wake and
  uploads acknowledged 32-row batches while retaining the existing 300-second normal HTTPS
  cadence. New readings no longer overwrite the previous four minute-cadence readings before a
  snapshot.
- Migration 030 adds exact observation timestamps and receiver-scoped idempotency IDs; migration
  031 adds atomic batch ingestion. The batch-aware `receiver-snapshot` Edge Function is deployed.
- Receiver build `MSV2 runtime build 2026-08-22 lossless-history-batch-v1` is live on COM16.
  Live full-batch retries/draining succeeded with zero queue drops. MSV3 wakes 135-138 are separate
  Supabase rows spaced 69.5-70.4 seconds apart with full raw FDC counts.
- Queue capacity is 512 receiver-observed wakes and is RAM-resident. See
  `docs/RECEIVER_LOSSLESS_HISTORY_BATCHING_2026-08-22.md` for protocol, retry behavior, outage
  headroom, and the reset/power-loss limitation.

## 2026-08-23: display card and animation pipeline repair

- Replaced each card's battery bar with a compact color-coded `Batt: N%` value in the top-right
  header and renamed the visible `Moist` label to `Moisture` in full, compact, expanded, and
  data-only redraw paths.
- Reclaimed the old battery row for artwork: a 137px-tall four-card layout now provides an 80px
  art band instead of 46px. Runtime scaling now aspect-fits directly; the former integer-divisor
  path turned an 84px sprite into only 42px whenever the available height was 43-83px.
- Repaired `generate_plant_sprites.py` so every GIF pose uses one shared crop and retains logical
  canvas coordinates. The old per-frame trim/re-center path made pose-dependent bounds look like
  random motion. `cactus_cluster.gif` also contained a detached 231px source artifact 33px below
  its main plant; crop selection now prevents that speck from shrinking/lifting only pose 3.
- Source GIF durations are emitted into `plant_sprites.h` and honored by the receiver. The old
  random 380-620ms interval is gone; only the initial phase is staggered. All unique poses are
  preserved by default instead of silently capping them at three.
- Verification: five sprite geometry/timing tests pass, the 1-16-card layout contract passes, and
  the final normal display firmware compiles at 2,197,400 bytes flash / 77,044 bytes RAM. Upload to
  COM16 is intentionally pending explicit approval because flashing reboots the receiver and can
  discard any rows still in its RAM-only history queue.
