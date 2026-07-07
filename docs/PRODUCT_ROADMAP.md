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

Current limitation: remote monitoring only works while the phone is on the same network as the receiver. Real remote access needs a cloud relay or user-managed VPN/tunnel.

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

Near-term: installable PWA served by the receiver.

Long-term: cloud-backed PWA or native app using the same screens:

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
9. App shows a claim code or QR code.
10. User signs into app and claims the receiver.
11. Probes appear as unassigned sensors when they are first heard.
12. User assigns each probe to a plant, art, and moisture preset.

## Remote Monitoring Flow

The receiver should push small snapshots to a backend:

The first API sketch is in [MSV2 Cloud API Draft](CLOUD_API_DRAFT.md).

```json
{
  "deviceId": "display-abc123",
  "sentAt": 1234567890,
  "plants": [
    {
      "sensor": 1,
      "moisture": 512,
      "batteryPercent": 94,
      "rssi": -68,
      "ageMs": 1000,
      "received": 120,
      "missedTotal": 1
    }
  ],
  "configVersion": 7
}
```

The phone app reads the latest snapshot from the backend. The receiver remains the source of truth for local BLE readings.

## Backend Requirements

Minimum useful backend:

- User accounts.
- Household/device ownership.
- Claim code flow.
- Receiver bootstrap endpoint.
- Latest device snapshot storage.
- Plant config storage.
- Receiver API token.
- App API token/session.

Later:

- Push notifications.
- Historical moisture charts.
- Shared household access.
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

1. Test the static no-login household Connect flow with the real receiver claim code.
2. Deploy the static cloud app to a webpage host.
3. Keep the hosted Netlify app read-only for now to conserve hosting/backend usage: clean plant status, summary, and pairing only.
4. Use the ESP32 local app as the plant setup surface for names, art, probe IDs, and calibration.
5. Re-enable hosted config editing later only after the UX and backend write budget are ready.
6. Make the phone app able to switch between local receiver mode and cloud account mode.
7. Add OTA/update-channel metadata.

The next best code milestone is polishing the read-only hosted plant dashboard while keeping setup local-first on the ESP32.
