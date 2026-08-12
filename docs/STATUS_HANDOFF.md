# MSV2 Status Handoff

Date: 2026-08-12 (see "2026-08-12 Surgical-Redraw Skill and Detail-View Refresh Fix" below for the most recent display-receiver work)

## Current State

- ESP32 display runtime renders the plant grid, animated pixel art, BLE readings, diagnostics, and phone setup app.
- ESP32 display runtime was last flashed successfully to the display receiver on `COM16` (port numbering shifts between sessions - always run `arduino_display.ps1 -Action board-list` first) with:
  - `.\scripts\arduino_display.ps1 -Action upload -Port COM16`
  - Firmware build tag constant is still `MSV2_BUILD_TAG "MSV2 runtime build 2026-08-11 display-touch-brightness-v1"` (not bumped despite the 2026-08-12 fixes below - the string wasn't touched, so don't rely on it to distinguish pre/post-fix builds).
  - Build size after flash: 2,090,044 bytes, 66% of program storage; 57,948 bytes (17%) of dynamic memory.
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
- Not done in this pass (by scope, not oversight): the Medium/Low findings from the review (docs drift elsewhere, dead code in `plant-dashboard/index.ts`'s unused `renderApp()`, script robustness issues in `esp32_packet_loss_test.py`/`mock_cloud_server.py`, stale plant-art whitelist in the dashboard vs. the new `color_*` sprite keys, etc.).

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

## Important Preferences

- Prefer static checks and focused edits.
- Keep the ESP32 local-first.
- Cloud features should stay disabled unless explicitly enabled.
- User wants a product path suitable for family/friends: simple setup, phone app, remote monitoring eventually.
- User specifically wants future setup where each ESP32 already has its own ID/secret and, after WiFi setup, connects to cloud automatically without manually entering device IDs into Supabase.

## Suggested Next Work

Top of the list as of 2026-08-09 (the 2026-07-25 list below is superseded - its overnight-oscillator and pulldown-confirmation items are long since resolved):

1. Check the overnight battery test: has Alocasia's battery held within ~1-2% (fix working) or dropped ~8-12% (QSPI leak somehow still active)?
2. Recharge/swap Scindapsus's battery - it's been at/near 0% since ~Aug 7 and is blocking clean data from that probe on every front (moisture *and* the battery-fix test).
3. Bench-check Scindapsus's BQ25100 `~CHG` pin for noise (leading theory for its erratic battery% readings even before the QSPI fix - see the 2026-08-09 section above).
4. Continue the FDC2212/MSV3 PCB migration open items (`docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md`) - now with concrete field evidence (not just theory) that VDD-coupling is a real, currently-happening problem, both from low battery and from active charging.
5. Consider a firmware-only mitigation in the meantime: discard/flag moisture samples taken while charging or below a battery floor, independent of the FDC2212 hardware timeline.
6. Revisit the receiver's `DEFAULT_STALE_SECONDS` - a prior session's uncommitted change sized it for the old 30-minute probe cadence; probes are back on ~60s now.

Superseded, top of the list as of 2026-07-25:

1. Check tonight's overnight results (via the now-fixed History tab, or `scripts/fetch_supabase_history.py`): are oscillator readings solid/stable in real soil (vs. last night's RC-timing sawtooth-to-saturation), and has auto mode's "ready" gate cleared for any sensor yet.
2. Confirm the 100kΩ pulldown is actually populated on the deployed field probe(s), not just the bench rig — the firmware can't compensate if it isn't there.
3. Tomorrow's solar-panel charging test in direct sunlight — panel only appeared to charge in direct sun, not even slight shade; the battery EMA smoothing landed specifically so this test's charging curve reads cleanly.
4. Once real soil data exists, revisit the provisional dry/wet defaults (currently 120/175, bench-touch-derived) - or just lean on auto mode once enough per-sensor history accumulates, rather than hand-tuning again.
5. Update `web/plant-dashboard`'s hardcoded art catalog (`known` array in `assets/app.v11.js` + `assets/plant_gifs/`) for the newer sprite keys and deploy - blocked on Netlify minutes until end of month.
6. Fix the receiver's missed-packet-counter wraparound-on-reboot bug (cosmetic, see 2026-07-24 section) if it's actually annoying in practice.

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
