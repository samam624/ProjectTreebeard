# Two receivers on one cloud and one network (2026-11-21)

The second DisplayCenter (build two, chip MAC `28:84:85:27:9a:d8`) is going to the maintainer's
parents. Before it left, it was run next to the main receiver (chip MAC `28:84:85:46:70:D8`) on
the same home WiFi and the same Supabase project, to check that the two keep their data apart.
The requirement was that the main receiver's history (July 2026 onward) must survive the test.

Times are UTC as stored in Supabase; PST is UTC-8.

## Safety steps taken first

- **Full cloud export before any device was touched.** Every `public` table was read with
  `SELECT` only, paged by primary key, to gzip JSONL with a manifest: 1,027,211
  `msv2_sensor_history` rows (2026-07-25 06:11 to 2026-11-22 03:42), every id present once, the
  exported count equal to `count(*)`. It is kept **outside the repository** (the repo is public and
  the export holds token hashes): `Desktop\Treebeard_cloud_backup_2026-11-21\`.
- **The main receiver's LittleFS** (0x310000, 0xE0000 bytes) was read back with `esptool
  read-flash` (no writes) into the same folder. It holds the receiver's identity file, so the
  cloud link (`MSV2088A3104A7AC`, which was transplanted and is not derived from this chip's MAC)
  can always be restored. A first attempt to read the whole 4 MB failed mid-transfer
  ("Packet content transfer stopped"); the board was reset afterwards and nothing was written.

## Identities

| | main receiver | build two |
|---|---|---|
| receiver ID | `MSV2088A3104A7AC` (stored, transplanted) | `MSV2D89A27858428` (from its own MAC) |
| household | `0316511a-…` | `0dd69767-…` (created at its first share-claim, 03:5x) |
| COM port on this PC | COM16 | COM4 |
| LAN IP during the test | 10.0.0.35 | 10.0.0.14 |
| firmware | `rename-pass2-v3` (unchanged) | `2026-11-21 boot-repaint-rename-v1` |

Build two's LittleFS was read before anything else ran: its `/device.json` held
`MSV2D89A27858428`, it had no WiFi or cloud configuration, and the cloud had no row for it. So
the two boards never shared an identity. (Had they, the second board's registration would have
rotated the first one's upload token and both would have written under one ID.)

## How build two got on the network

Build two was flashed on COM4 with the current source (the lab workstation's boot repaint and
screen flip merged with the local rename and sound work). It joined the home WiFi with no setup
step, because a build compiled on the maintainer's PC includes the gitignored
`WiFiCredentials.h` as a compile-time default (`/wifi` reports `hasStoredCredentials:false`,
serial reports `credential source: firmware`). It then registered itself through
`receiver-bootstrap` at 03:46:59 as a new row.

**Consequence for the parents' unit:** any build made on this PC carries the home WiFi password
in flash. The parents' unit was therefore reflashed with a build compiled while that header was
moved aside (see "Handed-over state" below).

## Results

All measured against the live project.

| check | result |
|---|---|
| build two registers as its own receiver row | yes, `MSV2D89A27858428`; the main row's `cloud_registered_at` stayed 2026-08-12 05:34 |
| main receiver keeps uploading after build two registers | yes, HTTP 200 throughout |
| a share token for build two's household sees only build two | yes, `share-snapshots` returned only `MSV2D89A27858428` |
| that token reads the main receiver's history | refused, HTTP 401 "receiver is not linked to this household" |
| that token reads build two's own history | yes |
| that token's `share-config` mentions the main receiver | no |
| `share-history-clear` with that token for a receiver outside its household | refused, HTTP 401 (sent only with a non-existent ID, never the main one) |
| deployed `msv2_clear_share_sensor_history` | has the household guard; execute granted to `service_role` only |
| brightness change on build two | its own `msv2_receiver_display_settings` row (v1); the main row stayed v27, 100 %, last updated 2026-09-01 |
| every unique key on a data table includes `receiver_id` | yes (history, hourly moisture, calibration reset, raw sweeps, snapshots, configs, display settings) |
| same probe packet heard by both | stored once under each receiver with identical moisture, FDC counts and battery mV; no duplicates |
| simultaneous reset of both (esptool `read-mac`, 03:51:10) | both uploading again with HTTP 200 by 03:51:32; neither re-ran bootstrap; no token rotation |

| main receiver's upload rate with build two running (03:47-04:12, against the 25 min before) | unchanged: sensor 4 21 vs 21, sensor 7 21 vs 21 (22 and 22 in the same window a day earlier), sensor 1 16 vs 12; queue drained, 0 dropped, 0 TLS failures |

Build two, next to the PC, stored sensors 1, 4 and 7 under its own ID until it was reflashed at
03:58.

## Handed-over state of build two

Reflashed at ~04:00 UTC on COM4 with the same source compiled while `WiFiCredentials.h` was moved
aside (restored straight after; the image was checked not to contain the home SSID). Read back
over its own setup AP (`Display-Center`, 192.168.4.1): receiver ID `MSV2D89A27858428`, cloud
configured (token kept in `/cloud.json`), `hasStoredCredentials:false`, setup portal active. On
the parents' WiFi it will upload under its existing ID and household without re-registering. The
share token minted for the test was revoked through `/api/share-revoke` and then rejected.

## What is shared, and what can still go wrong

- **The hosted dashboard keeps one share token per browser.** Opening the parents' pairing QR on
  the maintainer's phone switches that phone's dashboard to the parents' household. Nothing is
  deleted; re-opening the main receiver's QR (or its local `/app`) switches it back.
- **Both setup portals are called `Display-Center`.** Only matters if both are in first-time
  setup at once.
- **Every receiver records every probe it can hear.** There is no probe allowlist. While build
  two sat here it stored the maintainer's sensors 4 and 7 under its own ID. That does not touch
  the main receiver's rows, but those readings stay in build two's history (and would appear in
  the parents' History tab if one of their probes uses ID 4 or 7).
- **The local app's History tab had a "Clear All History" button** that deleted every cloud
  history row for that receiver after one browser `confirm()`. The guard above stopped it
  reaching another household's receiver, but on the main receiver's own app it would have deleted
  the main history. Removed the same evening at the maintainer's request ("that would be bad"):
  - the button and `clearAllHistory()` are gone from `MobileAppPage.h`; build
    `2026-11-21 no-clear-history-v1` was flashed to both receivers (build two without the WiFi
    header; the main receiver normally, chip MAC checked, back on the same ID, saved WiFi,
    config v20 and HTTP 200 uploads; its `/app` no longer contains the button);
  - the `/api/share-history-clear` route is removed from `plant-dashboard` in the repo;
  - migration `044` drops `msv2_clear_share_sensor_history`.

  Deployed at the maintainer's word the same evening. The live `plant-dashboard` was downloaded
  first and equalled the repo apart from the removed route; after `supabase functions deploy
  plant-dashboard --use-api` its endpoint list no longer names `shareHistoryClear` and
  `/api/share-history-clear` falls through to the index. Remote migration history matched the
  repo through `043`, and `supabase db push --linked` applied only `044`.
  `verify_multi_receiver_isolation.py` then reported "function dropped", main history 1,027,311
  rows (above the 1,027,211 backup floor), and the main receiver kept uploading with HTTP 200.

## BLE signal, main vs build two (new antenna and U.FL connector)

Same probe packets heard by both, 03:47-03:58, paired on (sensor, wake):

| sensor | packets | main | build two | build two − main |
|---|---|---|---|---|
| 4 | 7 | −58.3 dBm | −61.7 dBm | −3.4 dB (sd 2.1) |
| 7 | 10 | −71.8 dBm | −75.8 dBm | −4.0 dB (sd 3.9) |
| 1 (outdoors) | 4 | −92.0 dBm | −92.0 dBm | 0 |

WiFi to the router: main −47 dBm, build two −48/−49 dBm. The two boards were in different places
(build two beside the PC), so the 3-4 dB is not attributable to the antenna; a side-by-side or
swapped-position run is needed for that.

## Verification tool

`cloud/scripts/verify_multi_receiver_isolation.py` repeats the cloud-side checks read-only
(households, a history-row floor per receiver, shared-packet agreement, duplicates, the clear
guard):

    python cloud/scripts/verify_multi_receiver_isolation.py --hours 6 --floor MSV2088A3104A7AC=1027211
