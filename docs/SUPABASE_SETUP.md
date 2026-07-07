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

The `receiver-snapshot` function still accepts the shared receiver token for the first hosted test. It can also accept per-receiver upload tokens after a receiver has bootstrapped.

## 1. Create A Receiver Token

In PowerShell:

```powershell
[guid]::NewGuid().ToString("N") + [guid]::NewGuid().ToString("N")
```

Save the output somewhere private. This is the token the ESP32 will send as:

```text
Authorization: Bearer YOUR_TOKEN
```

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

CLI alternative:

```powershell
supabase login
supabase init
supabase link --project-ref lyemomfpzkawqecpfufm
supabase db push
```

## 3. Set Edge Function Secrets

In Supabase Dashboard:

1. Go to `Edge Functions`.
2. Open `Secrets`.
3. Add only the custom receiver token:

```text
MSV2_RECEIVER_TOKEN=YOUR_GENERATED_TOKEN
```

Supabase Edge Functions often provide the reserved `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY` environment variables automatically. The dashboard may prevent you from manually adding secrets with the `SUPABASE_` prefix; that is expected.

If your project requires manual values and rejects the `SUPABASE_` prefix, add these fallback names instead:

```text
URL=https://lyemomfpzkawqecpfufm.supabase.co
SERVICE_ROLE_KEY=YOUR_SERVICE_ROLE_KEY
MSV2_RECEIVER_TOKEN=YOUR_GENERATED_TOKEN
```

The `receiver-snapshot` function checks both naming styles.

The service role key is private. Do not put it in frontend JavaScript, firmware, GitHub, or chat.

CLI alternative:

```powershell
supabase secrets set MSV2_RECEIVER_TOKEN=YOUR_GENERATED_TOKEN
```

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

The claim endpoint URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/claim-receiver
```

The receiver bootstrap endpoint URL should be:

```text
https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap
```

## 5. Test From PowerShell

First pull a local receiver snapshot:

```powershell
$snapshot = Invoke-RestMethod http://BOARD_IP/snapshot
```

Then upload it to Supabase:

```powershell
$token = "YOUR_GENERATED_TOKEN"
$body = $snapshot | ConvertTo-Json -Depth 20 -Compress
Invoke-RestMethod `
  https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot `
  -Method Post `
  -ContentType "application/json" `
  -Headers @{ Authorization = "Bearer $token" } `
  -Body $body
```

Read latest snapshots back:

```powershell
Invoke-RestMethod `
  https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-snapshot `
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

The future QR code should open:

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

Remote editing is the next backend milestone. The receiver currently pushes snapshots up to Supabase; to give the hosted app the same controls as the ESP32 webpage, add a cloud config command table/API and have the receiver periodically pull and apply pending config updates.

## 8. Future Zero-Touch Receiver Bootstrap

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
