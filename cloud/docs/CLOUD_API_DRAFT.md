# Treebeard Cloud API Draft

This is the first narrow contract for remote monitoring. It is intentionally small so the receiver can stay local-first while a cloud relay is prototyped.

## Receiver Snapshot Upload

Future endpoint:

```text
POST /api/v1/receivers/{receiverId}/snapshot
Authorization: Bearer <receiver api token>
Content-Type: application/json
```

Request body:

```json
{
  "receiverId": "MSV2AABBCCDDEEFF",
  "claimCode": "ABCD-2345",
  "build": "Treebeard runtime build ...",
  "sentAtMs": 123456,
  "config": {
    "schema": 1,
    "version": 7,
    "updatedMs": 120000,
    "updatedBy": "receiver-local",
    "title": "Plant Status Center",
    "cards": [
      {
        "sensor": 1,
        "plant": "Basil",
        "art": "sprout_pot",
        "moisture": {
          "dry": 350,
          "wet": 800
        }
      }
    ]
  },
  "network": {
    "mode": "station",
    "ip": "10.0.0.236",
    "rssi": -48
  },
  "plants": [
    {
      "sensor": 1,
      "wake": 612,
      "moisture": 512,
      "batteryPercent": 94,
      "rssi": -68,
      "ageMs": 1000,
      "received": 120,
      "missedTotal": 1
    }
  ]
}
```

Expected response:

```json
{
  "ok": true,
  "serverTimeMs": 1234567890,
  "acceptedConfigVersion": 7
}
```

## Receiver Claim

Future endpoint:

```text
POST /api/v1/claim
Authorization: Bearer <user session token>
Content-Type: application/json
```

Request body:

```json
{
  "receiverId": "MSV2AABBCCDDEEFF",
  "claimCode": "ABCD-2345",
  "householdName": "Kitchen Plants"
}
```

Expected response:

```json
{
  "ok": true,
  "householdId": "household_123",
  "receiverApiToken": "generated_receiver_token"
}
```

The receiver API token should be saved to `/cloud.json` and never shown again. The app may show `hasApiToken: true`.

## Local Receiver Endpoints Used By The App

- `GET /device`: receiver identity, build, network mode, endpoint list, config/cloud summary.
- `GET /claim`: receiver ID and claim code for setup.
- `GET /config-meta`: current UI config version.
- `GET /data`: live probe readings.
- `GET /snapshot`: the cloud-shaped snapshot payload generated locally without uploading.
- `GET/POST/DELETE /cloud`: local cloud relay settings.

## Prototype Rules

- Receiver remains usable with cloud disabled.
- Receiver makes outbound HTTPS requests only.
- Cloud should store only the latest snapshot first; history can come later.
- Cloud should reject older config versions unless the request is explicitly marked as conflict resolution.
- Claim codes should become rotating/expiring before sharing outside trusted testers.
