# MSV2 Product Roadmap

The goal is a plant monitor that a friend or family member can set up without Arduino tools:

1. Plug in the ESP32 display receiver.
2. Open the MSV2 phone app.
3. Put the receiver on home WiFi.
4. Add probes to plants.
5. Monitor plants locally and remotely.

## Current State

- ESP32 display receives BLE advertisements from XIAO/nRF probes.
- ESP32 hosts a local phone web app at `/app`.
- The app can edit plant names, probe IDs, art, moisture presets, and calibration.
- The app now has diagnostics for RSSI, packet loss, age, wake count, and battery.
- The receiver exposes `/device` for app/device discovery.
- The receiver exposes `/wifi`, stores credentials in LittleFS, and can be configured from the phone app.
- The receiver creates a persistent local identity in `/device.json` and exposes `/claim` with a receiver ID and claim code.
- Supabase has a receiver bootstrap endpoint, and the ESP32 firmware has first-pass auto-bootstrap so the receiver can register itself with its built-in ID/secret and receive a per-device upload token automatically.
- The UI config carries `_meta.schema`, `_meta.version`, `_meta.updatedMs`, and `_meta.updatedBy`; `/config-meta` exposes that sync state.
- Cloud relay settings are stored locally in `/cloud.json`, exposed through `/cloud`, and normally populated by automatic Supabase bootstrap after WiFi setup.
- The ESP32 phone app shows cloud bootstrap/upload/config-pull status but no longer exposes manual cloud URL/token editing in the normal setup UI.
- `/snapshot` exposes the cloud upload payload locally, and `scripts/mock_cloud_server.py` provides a tiny local API test harness.
- The receiver serves PWA metadata, icon, and a service worker route. Some browsers still treat local `http://` receiver pages as home-screen shortcuts rather than full PWAs because full PWA installability generally expects HTTPS.

Remote monitoring is implemented through the Supabase relay and hosted Netlify dashboard. Local
receiver pages still require the same LAN (or the receiver setup AP); cloud sharing/history uses
the receiver claim/share-token flow documented in `SUPABASE_SETUP.md`.

## Product Architecture

### Probe

The probe should stay simple and power-first:

- Wake on a low-power schedule.
- Read moisture and battery.
- Advertise a signed or versioned BLE packet.
- Sleep again.

Recommended long-term behavior:

```text
Every 3-5 minutes:
  wake
  read moisture
  if moisture changed meaningfully OR 30 minutes since last BLE:
    advertise for about 500 ms
  sleep
```

This gives quick watering updates without paying the BLE radio cost every check.

### Receiver

The ESP32 receiver is wall-powered and should do the heavier work:

- Scan BLE continuously.
- Render the display.
- Host local setup.
- Store plant config.
- Relay snapshots to the cloud when configured.
- Keep working locally if internet/cloud is down.

### Phone App

Current: a local PWA-style app served by the receiver plus a cloud-backed, installable Netlify
dashboard. The cloud dashboard intentionally keeps configuration read-only while setup remains in
the local receiver app.

Possible later expansion of those same screens:

- Household dashboard.
- Plant setup.
- Probe diagnostics.
- Moisture presets and calibration.
- Art/theme choices.
- Alerts for dry/stale/low battery.

## Setup Flow For Friends

1. Receiver boots.
2. If it has no valid WiFi settings, the TFT shows first-time setup instructions and the receiver starts the open `MSV2-Display` setup WiFi.
3. Phone joins the open `MSV2-Display` setup network; captive-portal DNS/redirects try to open the receiver setup app automatically.
4. If the phone does not open the page, the TFT tells the user to open `192.168.4.1`.
5. User either enters home WiFi in the `Setup > Receiver WiFi` panel or checks `Use without home WiFi` for local-only mode.
6. Receiver saves that choice and reboots to either the home network or the local `MSV2-Display` dashboard.
7. If home WiFi is enabled, the receiver bootstraps to the hosted backend using its built-in receiver ID and private receiver secret.
8. If home WiFi is enabled, the receiver saves the returned cloud upload URL/token and starts uploading snapshots.
9. The receiver shows a QR code containing its receiver ID and one-time claim code.
10. The hosted app creates a household share token in the browser and claims the receiver; no user
    account is required for the normal flow.
11. Probes appear as unassigned sensors when they are first heard.
12. User assigns each probe to a plant, art, and moisture preset.

## Remote Monitoring Flow

The receiver pushes small snapshots and lossless history batches to Supabase. The first API sketch
is retained in [MSV2 Cloud API Draft](CLOUD_API_DRAFT.md) as historical design context; the deployed
Edge Functions and migrations are authoritative.

The hosted app reads the latest snapshot and history from the backend. The receiver remains the
source of truth for local BLE readings and local setup.

## Backend Requirements

Implemented backend foundation:

- Share-token household/device ownership.
- Account-free claim code and QR flow.
- Receiver bootstrap endpoint.
- Latest device snapshot storage.
- Lossless timestamped moisture/battery history.
- Plant config storage.
- Versioned receiver display-setting storage/pull.
- Per-receiver API tokens and browser household share tokens.

Optional later additions:

- Push notifications.
- User-account sign-in in addition to the account-free flow.
- OTA firmware metadata.
- Sensor/probe manufacturing records.

## Security Notes

- Do not expose the ESP32 directly to the internet.
- Receiver should make outbound HTTPS requests to the cloud.
- Each receiver needs a unique device ID and secret.
- Claim codes should expire.
- WiFi credentials should only stay on the receiver.
- Probe BLE packets can start unsigned during prototyping, but production should include a packet version and some spoofing protection.

## Next Implementation Steps

1. Apply migration 033 and deploy the remediated receiver/display-settings function before
   treating remote display-setting writes as healthy.
2. Flash the review-remediated receiver and probe builds, then complete the wet-soil and 24-48-hour
   soak gates recorded in `STATUS_HANDOFF.md`.
3. Keep the hosted Netlify app read-only for now to conserve hosting/backend usage: plant status,
   history, summary, and pairing only.
4. Keep names, art, probe IDs, and calibration in the ESP32 local app until hosted editing is an
   explicit product decision.
5. Add OTA/update-channel metadata if remote fleet maintenance becomes necessary.

The next best code milestone is polishing the read-only hosted plant dashboard while keeping setup local-first on the ESP32.
