# BLE / cloud-sync contention fix (2026-08-11)

## Hardware and identity involved

- Real ESP32-S3 display receiver during this session: **COM16**
- Unused ESP32-S3 DevKit during this session: **COM13 — do not flash**
- Transplanted Supabase receiver ID on the real board: `MSV2088A3104A7AC`

COM numbers can change after reconnecting USB. Re-check the port inventory before any future
upload rather than treating COM16 as permanent. Normal sketch upload uses `EraseFlash=none` and
must preserve LittleFS (`/device.json`, `/cloud.json`, display settings, and touch calibration).

## Confirmed failure mechanism

`Msv2AdvertisedDeviceCallbacks::onResult()` used to call `scheduleCloudSyncSoon()` after every new
BLE packet. That helper moved `nextCloudSyncMs` to five seconds in the future, throttled to once per
15 seconds. It did not respect `cloudSyncIntervalSeconds`.

Consequently, setting the receiver's interval from 60 to 300 seconds did not govern snapshot
uploads while probes were active. Observed database rows remained roughly 18–35 seconds apart.
The cloud task performs blocking HTTPS/TLS operations and shares ESP32 core 0 with the scan task,
so this packet-driven upload cadence repeatedly created avoidable BLE/WiFi coexistence and task
contention.

Sensor 2 also went at least seven minutes without a new packet while sensors 1 and 3 continued.
Repeated cloud rows for sensor 2 (`wake=304`, `moisture=160`, `rssi=-59`) were copies of the cached
receiver reading in later snapshots; they do not prove that the receiver heard new sensor-2
advertisements. The silence may have been probe-side or a missed advertising window and still
needs a physical multi-probe soak test.

## Applied firmware behavior

- BLE packet reception no longer changes `nextCloudSyncMs`.
- Routine snapshot uploads now follow `cloudSyncIntervalSeconds`.
- Explicit configuration changes can still request one prompt confirmation snapshot five seconds
  later through `scheduleCloudSnapshotAfterStateChange()`.
- An unchanged remote-display poll no longer requests a confirmation snapshot. A newly applied
  display setting still does.
- `bleScanTask` remains on core 0 but now has FreeRTOS priority 2; `cloudSyncTask` remains on core 0
  at priority 1. Core 1 remains dedicated to the render/WebServer loop.
- `GET /cloud` reports `upload.packetTriggered=false` and the task priorities for field diagnosis.

Keeping both application workers on core 0 is deliberate. An earlier implementation that ran
blocking cloud HTTP calls in the core-1 render/WebServer loop caused obvious periodic UI freezes.

## What this fix does not claim

- It does not prove sensor 2 itself is healthy; check its power, range, advertisement log, and wake
  counter during the soak test.
- The current history API records every uploaded cached plant reading, not only newly received BLE
  packets. Identical wake values in history therefore mean “same cached sample uploaded again,”
  not multiple receptions. Changing history semantics should be a separate database/API change so
  live-snapshot behavior is not altered during this contention fix.
- Configuration and remote-display polling still use HTTPS and therefore WiFi radio time. Higher
  BLE task priority reduces application-task interference, but only a real multi-probe soak test can
  validate RF coexistence on the finished PCB.

## Field verification checklist

1. Confirm `/cloud` still shows `syncIntervalSeconds: 300` after flashing.
2. Confirm serial boot logs show both cloud and BLE tasks started.
3. Run two or three probes, including the formerly silent sensor 2, for longer than one full normal
   advertising interval.
4. Compare `[Treebeard debug] sensor ... wake ...` messages with cloud upload messages. Packets must not
   cause an upload five seconds later.
5. Confirm snapshot upload spacing settles near 300 seconds except immediately after an intentional
   UI/display configuration change or reconnect/bootstrap.
6. Exercise local touch and remote brightness while advertisements are active; verify animation,
   touch response, and packet reception continue.

