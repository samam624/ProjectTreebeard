# MSV2 Supabase Setup

Project URL:

```text
https://lyemomfpzkawqecpfufm.supabase.co
```

Project ref:

```text
lyemomfpzkawqecpfufm
```

Public publishable key:

```text
sb_publishable_Ycl5Be9F1qOxcU0pjQd1Cw_EzW4husY
```

Do not commit or paste the database password or service role key into repo files.

## What This Adds

- `msv2_receivers`: one row per physical receiver.
- `msv2_latest_snapshots`: latest uploaded snapshot per receiver.
- `receiver-snapshot` Edge Function:
  - `POST` stores an ESP32 snapshot.
  - `GET` reads stored latest snapshots for testing.
- `receiver-bootstrap` Edge Function:
  - `POST` registers a receiver using its built-in ID/secret.
  - Returns the hosted snapshot URL and a per-receiver upload token.

As of the 2026-07-07 security pass, `receiver-snapshot` **only** accepts a receiver's own per-device upload token (issued by `receiver-bootstrap` and verified against its hash in `msv2_receivers`). The old shared `MSV2_RECEIVER_TOKEN` bypass that let any caller with one fleet-wide secret read or write any receiver's data has been removed from the function and is no longer read from the environment. You do not need to generate or set one; skip straight to migrations.

## 1. (No longer needed) Shared Receiver Token

This step used to have you generate a `MSV2_RECEIVER_TOKEN` shared secret for testing. That auth path was removed for security reasons (it let anyone holding the token impersonate any receiver, and there was no way to scope or revoke it). Every receiver authenticates with its own bootstrapped token now — see [Section 6](#6-confirm-esp32-automatic-bootstrap).

## 2. Apply The Database Migration

In Supabase Dashboard:

1. Open your project.
2. Go to `SQL Editor`.
3. Open [001_msv2_cloud.sql](../supabase/migrations/001_msv2_cloud.sql).
4. Paste it into the SQL editor.
5. Run it.
6. Open [002_msv2_store_snapshot_rpc.sql](../supabase/migrations/002_msv2_store_snapshot_rpc.sql).
7. Paste it into the SQL editor.
8. Run it.
9. Open [003_msv2_read_latest_snapshots_rpc.sql](../supabase/migrations/003_msv2_read_latest_snapshots_rpc.sql).
10. Paste it into the SQL editor.
11. Run it.
12. Open [004_msv2_households_and_claims.sql](../supabase/migrations/004_msv2_households_and_claims.sql).
13. Paste it into the SQL editor.
14. Run it.
15. Open [005_msv2_receiver_bootstrap.sql](../supabase/migrations/005_msv2_receiver_bootstrap.sql).
16. Paste it into the SQL editor.
17. Run it.
18. Open [006_msv2_receiver_bootstrap_fix.sql](../supabase/migrations/006_msv2_receiver_bootstrap_fix.sql).
19. Paste it into the SQL editor.
20. Run it.
21. Open [007_msv2_hash_secret_bytea_fix.sql](../supabase/migrations/007_msv2_hash_secret_bytea_fix.sql).
22. Paste it into the SQL editor.
23. Run it.
24. Open [008_msv2_pgcrypto_schema_fix.sql](../supabase/migrations/008_msv2_pgcrypto_schema_fix.sql).
25. Paste it into the SQL editor.
26. Run it.
27. Open [009_msv2_household_share_tokens.sql](../supabase/migrations/009_msv2_household_share_tokens.sql).
28. Paste it into the SQL editor.
29. Run it.
30. Open [010_msv2_receiver_cloud_config.sql](../supabase/migrations/010_msv2_receiver_cloud_config.sql).
31. Paste it into the SQL editor.
32. Run it.
33. Open [011_msv2_lock_rpcs_to_service_role.sql](../supabase/migrations/011_msv2_lock_rpcs_to_service_role.sql).
34. Paste it into the SQL editor.
35. Run it.
36. Open [012_msv2_revoke_public_execute.sql](../supabase/migrations/012_msv2_revoke_public_execute.sql).
37. Paste it into the SQL editor.
38. Run it.
39. Apply every remaining file in `supabase/migrations/` from `013` through the
    highest-numbered migration present (currently `038`) in numerical order. In particular,
    024-027 are one staged moisture-calibration rollout;
    030-032 add queued history/raw-battery support; 033 fixes receiver display-setting
    upserts; 034 locks down `msv2_reset_sensor_calibration` to `service_role` and
    extracts the scale-transition-reset logic into a shared helper; 035 adds nullable
    MSV3 board-temperature/bare-tank-reference history columns (no compensation formula
    enabled yet - see docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md); 036
    adds the density probe plus stable sampled reader used to bound large History downloads;
    037 adds `raw_fdc_counts`/`raw_moisture_khz` (present on the table since 029, but never
    returned by any history-reader RPC until now) to `msv2_read_sensor_history`,
    `msv2_read_share_sensor_history`, and `msv2_read_share_sensor_history_sampled`; and 038 adds
    an optional `p_until` upper bound to the density/sampled reader pair so the History tab can
    query an arbitrary closed date range, not just "since X through whatever's newest now."
    Do not skip directly to the latest file because the migration history and rollback trail are
    part of the deployment record.

CLI alternative:

```powershell
supabase login
supabase init
supabase link --project-ref lyemomfpzkawqecpfufm
supabase db push
```

**If you ever apply SQL by hand in the dashboard editor instead of `db push`** (this project's history has some of that), the CLI's local migration tracking won't know about it and `db push` will try to re-run migrations that already landed — which fails on any non-idempotent statement (e.g. `create policy`). Check first with:

```powershell
supabase migration list --linked
```

If `local` versions show as applied on your database already but `remote` is blank, mark them as applied without re-running the SQL:

```powershell
supabase migration repair 001 002 003 004 005 006 007 008 009 010 --status applied --linked
```

(adjust the version list to whatever the `remote` column shows as missing) — this only fixes the CLI's bookkeeping table, it does not touch your schema. Then `supabase db push --linked --dry-run` to confirm only the genuinely-new migrations remain before pushing for real.

**Migrations 011 and 012 (2026-07-07) close the biggest issue in this backend:** every RPC below was originally granted `execute` to `anon, authenticated, service_role`, which meant anyone with the public `apikey` could call `/rest/v1/rpc/<function>` directly and skip every auth check written into the Edge Functions. 011 revokes from `anon, authenticated`; 012 is the important one — it additionally revokes from `PUBLIC`, because Postgres grants `EXECUTE` to `PUBLIC` by default on function creation and that isn't affected by revoking from a specific named role. If you ever add a new RPC, remember to `revoke ... from public` on it too, not just `anon, authenticated`, or it'll be callable by anyone regardless of what you did or didn't grant explicitly.

## 3. Set Edge Function Secrets

Supabase Edge Functions provide the reserved `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY` environment variables automatically — there is nothing to set by hand for a normal deploy. `MSV2_RECEIVER_TOKEN` is no longer read by any function as of 2026-07-07; you can leave it unset, and it's safe to delete if you had set it previously.

If your project requires manual values and rejects the `SUPABASE_` prefix, the functions also accept these fallback names:

```text
URL=https://lyemomfpzkawqecpfufm.supabase.co
SERVICE_ROLE_KEY=YOUR_SERVICE_ROLE_KEY
```

The service role key is private. Do not put it in frontend JavaScript, firmware, GitHub, or chat.

## 4. Deploy The Edge Function

CLI:

```powershell
supabase functions deploy receiver-snapshot
supabase functions deploy plant-dashboard
supabase functions deploy claim-receiver
supabase functions deploy receiver-bootstrap
```

The deployed upload URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot
```

The hosted dashboard URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/plant-dashboard
```

### Adaptive moisture calibration

`receiver-snapshot?receiverId=...&autocal=1` returns per-sensor learned bounds to an
authenticated receiver. Migrations 024-027 make that result time-weighted and adaptive:

- one median reading per five-minute bucket prevents upload-cadence bias;
- watering candidates are checked against two-hour pre-water, three-hour post-water, and
  6-12-hour retention windows;
- all supported cycles in the 21-day window contribute with evidence and recency weights;
- a new event contributes provisionally until later uploads confirm soil retention; and
- the response keeps the existing `ready`, `dryBound`, and `wetBound` contract while adding
  confidence/evidence diagnostics that older firmware can ignore.

The RPC itself remains `service_role` only. Do not call it directly from a browser or place
the service-role key in firmware. Operational behavior, alert thresholds, production
evidence, and verification commands are documented in
[MSV2 Adaptive Moisture Calibration](../../MSV2/docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md).

The claim endpoint URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/claim-receiver
```

The receiver bootstrap endpoint URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap
```

## 5. Test From PowerShell

Every request now needs a real per-device token from `receiver-bootstrap` (Section 6) — there's no shared token to paste in anymore. Grab a device's token by reading its local `/cloud` status, or by re-running bootstrap by hand:

```powershell
$boot = Invoke-RestMethod `
  https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap `
  -Method Post -ContentType "application/json" `
  -Body (@{ receiverId = "MSV2..."; receiverSecret = "THE_DEVICE_RECEIVER_SECRET" } | ConvertTo-Json)
$token = $boot.apiToken
```

Then read that receiver's latest snapshot back (requires `receiverId` and that device's own token — there is no fleet-wide listing anymore):

```powershell
Invoke-RestMethod `
  "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot?receiverId=MSV2..." `
  -Headers @{ Authorization = "Bearer $token" } `
  | ConvertTo-Json -Depth 10
```

## 6. Confirm ESP32 Automatic Bootstrap

After the receiver has home WiFi, current firmware calls the hosted `receiver-bootstrap` endpoint automatically. It stores the returned per-device upload URL/token in `/cloud.json`, enables snapshot upload, and starts polling for newer cloud config versions.

Check status from PowerShell:

```powershell
Invoke-RestMethod http://BOARD_IP/cloud | ConvertTo-Json -Depth 5
```

Success looks like:

```json
{
  "enabled": true,
  "hasApiToken": true,
  "bootstrap": {
    "lastHttpStatus": 200,
    "lastError": ""
  },
  "upload": {
    "lastHttpStatus": 200,
    "lastError": ""
  }
}
```

## 7. Claim Flow Scaffold

The first claim endpoint is:

```text
POST https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/claim-receiver
Authorization: Bearer USER_JWT_FROM_SUPABASE_AUTH
Content-Type: application/json
```

Body:

```json
{
  "receiverId": "MSV2...",
  "claimCode": "ABCD-1234",
  "householdName": "Kitchen Plants"
}
```

This creates:

- `msv2_households`
- `msv2_household_members`
- `msv2_receiver_claims`

The hosted `plant-dashboard` can already read user-scoped snapshots when it receives a valid Supabase user JWT. The next app milestone is adding a real sign-in/claim UI so users do not need to call this endpoint manually.

The no-login household app now lives as a static page in:

```text
web/plant-dashboard/index.html
```

Supabase Edge Functions are now used as the API backend only. The Supabase function URL returns HTML as `text/plain` with a restrictive sandbox header, so it should not be used as the webpage host.

It supports:

- receiver connection with `receiverId` and `claimCode`
- long random household share token generation
- share token storage in browser `localStorage`
- household-scoped dashboard loading through `/functions/v1/plant-dashboard/api/share-snapshots`

The older user/password claim endpoint still exists for future upgrades, but the normal happy path no longer requires an account.

To test:

1. For a quick computer test, open [index.html](../web/plant-dashboard/index.html) in a browser.
2. Get receiver pairing details:

```powershell
Invoke-RestMethod http://BOARD_IP/claim | ConvertTo-Json -Depth 5
```

3. Enter `receiverId` and `claimCode` in the hosted app.
4. Click `Connect`.
5. The latest uploaded snapshot should appear.
6. Refresh or reopen the app; it should stay connected because the browser saved the household share token.

The receiver QR code opens:

```text
https://msv2-plants.netlify.app/?receiverId=MSV2...&claimCode=ABCD-1234&autoConnect=1
```

The receiver `/claim` endpoint now returns this as `claimUrl`. The hosted app reads `receiverId`, `claimCode`, and `autoConnect=1`; on first open it creates the household share token and stores it in the phone browser. After that, reopening the app goes straight to the dashboard.

Chosen static app host:

- Netlify

Supabase remains the backend. Netlify only hosts the installable phone web app files from:

```text
web/plant-dashboard/
```

Current live app:

```text
https://msv2-plants.netlify.app
```

Netlify project admin:

```text
https://app.netlify.com/projects/msv2-plants
```

The static app can be hosted anywhere because the Supabase APIs send permissive CORS headers.

Fastest first deployment with Netlify Drop:

1. Open:

```text
https://app.netlify.com/drop
```

2. Drop this zip file:

```text
web/msv2-plant-dashboard-static.zip
```

3. Netlify will return a public HTTPS URL.
4. Open that URL on iPhone Safari.
5. Use Share > Add to Home Screen.

If the static app changes, recreate the zip:

```powershell
Compress-Archive -Path web\plant-dashboard\* -DestinationPath web\msv2-plant-dashboard-static.zip -Force
```

For Git-based deploys later, the repo root has a `netlify.toml` that publishes:

```text
web/plant-dashboard
```

API verification completed:

- `share-claim` created a household share token for receiver `MSV2088A3104A7AC`.
- `share-snapshots` returned the latest uploaded snapshot with `plant_count: 2`.
- The current visible plants were Basil and Mint.

The static app now uses the dark Plant Status Center palette and animated plant GIFs from:

```text
web/plant-dashboard/assets/plant_gifs/
```

The backend and receiver already support versioned cloud display settings: Supabase stores the
settings and the ESP32 periodically pulls and applies a newer version. The hosted Netlify app keeps
those write controls hidden for now, so normal plant setup remains on the receiver's local app. Do
not add a second command-table API for this; extend the existing display-settings path when hosted
editing is intentionally enabled.

## 8. Zero-Touch Receiver Bootstrap

**2026-08-14 update**: as of migration `021_msv2_register_receiver_race_mitigation.sql`,
`claimCode` in the body below is no longer just conventionally included - it's
**required** on a receiver's very first-ever registration (`msv2_register_receiver`
raises `claim code required for first-time registration` if omitted). This mitigates
(does not fully close - see that migration's own SQL comments) a registration race
where `receiverId`, being derived directly from the device's MAC address, is
passively observable over WiFi before the real device's own first bootstrap call.
Real firmware is unaffected - `ensureDeviceIdentity()` always generates a claim code
alongside the secret before either is ever sent anywhere. Only matters if you're
calling this endpoint by hand for testing.

This is the path for giving a receiver to someone with almost no setup:

1. During build/prep, the receiver already has a persistent `receiverId`, `receiverSecret`, and `claimCode` in `/device.json`.
2. The user connects the receiver to home WiFi.
3. The receiver calls:

```text
POST https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap
Content-Type: application/json
```

Body:

```json
{
  "receiverId": "MSV2...",
  "receiverSecret": "DEVICE_SECRET_FROM_DEVICE_JSON",
  "claimCode": "ABCD-1234",
  "build": "MSV2 Display Runtime ..."
}
```

Response:

```json
{
  "ok": true,
  "receiverId": "MSV2...",
  "uploadUrl": "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot",
  "apiToken": "PER_DEVICE_UPLOAD_TOKEN",
  "apiTokenHint": "a1b2c3",
  "claimCode": "ABCD-1234",
  "syncIntervalSeconds": 60
}
```

The receiver should save `uploadUrl`, `apiToken`, and `syncIntervalSeconds` into `/cloud.json`, enable cloud sync, and then upload snapshots normally. The visible claim code is only for pairing the receiver to a user's account; the private `receiverSecret` and returned `apiToken` should never be shown in the app.

This backend endpoint exists now, and the ESP32 firmware has the first auto-bootstrap implementation. After flashing updated firmware, a receiver with home WiFi should call this endpoint automatically if `/cloud.json` does not already contain an upload URL and API token.

For older test boards that already have a manually entered Supabase URL/token, auto-bootstrap intentionally will not overwrite that. To test the automatic path after flashing the new firmware, clear the receiver cloud config first:

```powershell
Invoke-RestMethod http://BOARD_IP/cloud -Method Delete
```

Then watch:

```powershell
Invoke-RestMethod http://BOARD_IP/cloud | ConvertTo-Json -Depth 8
```

Expected signs:

- `bootstrap.lastHttpStatus` becomes `200`.
- `bootstrap.lastError` is empty.
- `hasApiToken` becomes `true`.
- `uploadUrl` becomes the hosted `receiver-snapshot` URL.
- `upload.lastHttpStatus` becomes `200` after the first snapshot upload.

Confirmed on hardware: auto-bootstrap returned HTTP `200`, stored a per-device token, set `syncIntervalSeconds` to `60`, and enabled cloud sync.

Confirmed on hardware: the first post-bootstrap snapshot upload returned HTTP `200` using the per-device token.

If `bootstrap.lastHttpStatus` is `500`, apply [006_msv2_receiver_bootstrap_fix.sql](../supabase/migrations/006_msv2_receiver_bootstrap_fix.sql) in the Supabase SQL editor, wait until `bootstrap.nextAttemptMs`, or reboot the receiver to retry sooner. Newer firmware also includes the server error body in `bootstrap.lastError`.

If the error says `function digest(text, unknown) does not exist`, apply [007_msv2_hash_secret_bytea_fix.sql](../supabase/migrations/007_msv2_hash_secret_bytea_fix.sql). That changes the hash helper to convert text to bytes before calling `digest()`.

If the error says `function digest(bytea, unknown) does not exist`, apply [008_msv2_pgcrypto_schema_fix.sql](../supabase/migrations/008_msv2_pgcrypto_schema_fix.sql). That exposes Supabase's `extensions` schema to the hash/register functions.

If the no-login hosted app says the share RPC is missing, apply [009_msv2_household_share_tokens.sql](../supabase/migrations/009_msv2_household_share_tokens.sql).
