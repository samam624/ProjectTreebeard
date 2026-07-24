# MSV2 Status Handoff

Date: 2026-07-24 (see "2026-07-24 Moisture Probe RC Sensing + Overnight Battery Test" below for the most recent work)

## Current State

- ESP32 display runtime renders the plant grid, animated pixel art, BLE readings, diagnostics, and phone setup app.
- ESP32 display runtime was last flashed successfully to the display receiver on `COM13` with:
  - `powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13 -ForceWifiSetup`
  - Current firmware build tag: `MSV2 runtime build 2026-07-05 reboot-screen-fix`
  - Build size after flash: about 2,098,561 bytes, 66% of program storage.
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

## Last Known User Feedback

- Display and app are working well after the WiFi/setup fixes.
- User confirmed the first-time setup/local network flow looks good for now.
- User noticed a brief visual artifact where pixel art stayed visible during the reboot message; this was fixed and flashed in `2026-07-05 reboot-screen-fix`.
- New plant GIF additions are working in the dropdown after regeneration/flash.
- Latest sprite feedback: normalized old and new sprites now look much closer in size after the sheet-derived GIFs were upscaled before sprite generation.
- Version numbers are visible and incrementing.
- Mock cloud test concept makes sense and looks good.

## Important Preferences

- Prefer static checks and focused edits.
- Keep the ESP32 local-first.
- Cloud features should stay disabled unless explicitly enabled.
- User wants a product path suitable for family/friends: simple setup, phone app, remote monitoring eventually.
- User specifically wants future setup where each ESP32 already has its own ID/secret and, after WiFi setup, connects to cloud automatically without manually entering device IDs into Supabase.

## Suggested Next Work

Top of the list as of 2026-07-24:

1. Populate the moisture probe's DNC pulldown footprint (10K leaning favorite - see that section for the reasoning) to fix the wild instability (151-60,000+ swings) seen once the probe was actually inserted in soil. Re-flash/re-verify `MOISTURE_FALL_TIMEOUT_US` and `MOISTURE_PRECHARGE_US` are still well-matched once the resistor changes the real discharge time constant - both are currently tuned for the old undefined-parasitic-path behavior.
2. Once the resistor is in and readings are stable, re-run a real overnight moisture calibration test (the 2026-07-24 run was battery-life-only, not valid moisture data) and use it to set real `dry`/`wet` presets on the display - current presets (350/800) are stale defaults from a totally different sensing scale.
3. Check tonight's actual battery runtime/voltage curve (via `scripts/fetch_supabase_history.py` in the morning) to see the external LED's real overnight power impact.
4. Fix the receiver's missed-packet-counter wraparound-on-reboot bug (cosmetic, see 2026-07-24 section) if it's actually annoying in practice.

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
