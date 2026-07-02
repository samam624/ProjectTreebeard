# MSV2 Status Handoff

Date: 2026-07-02

## Current State

- ESP32 display runtime renders the plant grid, animated pixel art, BLE readings, diagnostics, and phone setup app.
- Phone app supports:
  - Status dashboard.
  - Plant setup.
  - Receiver WiFi setup.
  - Receiver pairing ID/claim code.
  - Config version display.
  - Cloud Sync settings panel.
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
- Netlify production deploy is live at `https://msv2-plant-dashboard-samam624-924.netlify.app`.
- Receiver bootstrap scaffold exists so future firmware can register itself after WiFi setup and receive a per-device upload token.
- ESP32 firmware now has first-pass cloud auto-bootstrap:
  - calls `receiver-bootstrap` after station WiFi is connected
  - sends `receiverId`, private `receiverSecret`, `claimCode`, and build tag
  - stores returned `uploadUrl`, per-device `apiToken`, and interval in `/cloud.json`
  - leaves existing manual cloud config alone if an upload URL and token already exist
- Auto-bootstrap was tested successfully against hosted Supabase:
  - `bootstrap.lastHttpStatus: 200`
  - `bootstrap.lastError: ""`
  - `hasApiToken: true`
  - `syncIntervalSeconds: 300`
  - cloud sync enabled automatically
- Per-device-token snapshot upload was tested successfully against hosted Supabase:
  - `upload.lastHttpStatus: 200`
  - `upload.lastError: ""`
  - `upload.lastSuccessMs` populated
- Bootstrap SQL fix `006` removes ambiguity in `msv2_register_receiver` by using the primary key constraint in the upsert and `return query` for the returned token row.
- Hash SQL fix `007` updates `msv2_hash_secret` to call `digest(convert_to(..., 'UTF8'), 'sha256')` because the hosted database reported `function digest(text, unknown) does not exist`.
- Pgcrypto schema fix `008` sets `search_path = public, extensions` for the hash/register functions because Supabase reported `function digest(bytea, unknown) does not exist` when `pgcrypto` was not visible from the function search path.
- Firmware now includes bootstrap HTTP response text in `/cloud.bootstrap.lastError` when the cloud returns a non-2xx status.
- Roadmap next milestone: test `web/plant-dashboard/index.html` in a browser, deploy the static app to a page host, then add a cloud config command path so the hosted app can edit the same plant settings as the ESP32 local app.

## Last Known User Feedback

- Display and app are generally working.
- New plant GIF additions are working in the dropdown after regeneration/flash.
- Version numbers are visible and incrementing.
- Mock cloud test concept makes sense and looks good.
- User is out of usage for today and wants to continue tomorrow.

## Important Preferences

- Do not compile/flash unless explicitly asked; it is slow and not always helpful.
- Prefer static checks and focused edits.
- Keep the ESP32 local-first.
- Cloud features should stay disabled unless explicitly enabled.
- User wants a product path suitable for family/friends: simple setup, phone app, remote monitoring eventually.
- User specifically wants future setup where each ESP32 already has its own ID/secret and, after WiFi setup, connects to cloud automatically without manually entering device IDs into Supabase.

## Suggested Next Work

1. Apply `supabase/migrations/001_msv2_cloud.sql` in the Supabase SQL editor.
2. Set Edge Function secrets:
   - `SUPABASE_URL`
   - `SUPABASE_SERVICE_ROLE_KEY`
   - `MSV2_RECEIVER_TOKEN`
3. Deploy `receiver-snapshot`, `plant-dashboard`, `claim-receiver`, and `receiver-bootstrap`.
4. Test with PowerShell using `docs/SUPABASE_SETUP.md`.
5. Point ESP32 Cloud Sync at `https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot`.
6. Open hosted dashboard: `https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/plant-dashboard`.
7. Test `web/plant-dashboard/index.html` with `http://BOARD_IP/claim`, deploy the static app, then design remote config write/pull support.

## Quick Test Commands

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

Direct receiver uploader test:

```text
Cloud Sync upload URL:
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
