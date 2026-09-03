# Receiver percentage-range, display-freeze, and upload reliability fix (2026-09-01)

## Status

Resolved, compiled, flashed to the ESP32-S3 receiver on COM16, and verified live at
`10.0.0.35` with production build:

```text
MSV2 runtime build 2026-08-31 durable-cloud-outbox-v7-smooth-display
```

This was one receiver-side incident with three visible symptoms. Supabase was accepting valid
requests and was not over capacity.

## User-visible symptoms

1. Cards that previously showed learned moisture percentages reverted to raw sensor values.
2. The physical display paused every few seconds, increasingly often as sensors were added.
3. Cloud history arrived inconsistently and the receiver sometimes reported a misleading
   connection-refused error.

## Root causes

### 1. Learned percentage ranges were starved

`autoCal[]` is refreshed from the receiver-snapshot `autocal=1` endpoint. A scheduler change
treated *any* queued history row as emergency pressure and blocked display, configuration, and
auto-calibration pulls until the queue became empty.

A continuously reporting probe made that assumption false: the queue was normally nonempty at
the scheduler's one-second check even though the uploader had ample drain capacity. After a
receiver restart cleared the RAM-only `autoCal[]` state, the auto-calibration pull could be
postponed indefinitely. The renderer correctly fell back to raw readings because it had no
trusted learned range.

### 2. Every unique wake forced a filesystem synchronization

The first durable-outbox implementation opened the LittleFS journal, appended the staged row,
called `flush()`, and closed the file on almost every cloud-task iteration. The fast bench probe
created a unique wake roughly every three seconds.

Measured LittleFS transactions took 29-240 ms. ESP32 internal-flash program/erase activity can
briefly suspend cache activity system-wide, so placing one transaction on every sensor wake made
the display/touch loop visibly pause at the same cadence. More sensors meant more flash work and
therefore a worse symptom.

### 3. TLS buffers were allocated after the history payload

The uploader originally built a large batched JSON `String` before opening the HTTPS connection.
That fragmented the receiver's internal heap immediately before mbedTLS needed a large contiguous
record-buffer allocation. `mbedtls_ssl_setup()` returned `MBEDTLS_ERR_SSL_ALLOC_FAILED`, which
surfaced through the higher-level client as the misleading `connection refused` message.

Larger/more frequent batches made this receiver-memory failure more likely. Supabase never saw
requests whose TLS setup failed locally.

## Final architecture

```text
BLE callback
    -> latest-value display cache
    -> 64-row RAM ingress ring
    -> checksum-protected RTC mirror (survives panic/watchdog/software reset)
    -> healthy path: copy up to 96 rows directly into the streamed upload
    -> establish TLS before serializing rows
    -> stream one bounded JSON row buffer at a time
    -> remove only the exact acknowledged prefix after HTTP 2xx

If WiFi/backend remains unavailable:
    RTC staging age/capacity threshold
    -> append a coalesced batch to checksummed LittleFS
    -> 8,192-row durable outage backlog
    -> replay idempotently after recovery
```

The display and cloud paths now have separate responsibilities:

- BLE callbacks perform no filesystem or network work.
- Healthy cloud operation performs no periodic history-journal writes.
- RTC staging protects unacknowledged rows from receiver crashes/resets.
- LittleFS is outage spillover, not a per-reading write-through queue.
- The upload is not acknowledged locally until the backend accepts the exact prefix.
- Observation IDs remain stable, so replay after a reset or torn cursor update is idempotent.
- The network scheduler starts at most one blocking cloud operation per lane slot and spaces
  slots by at least 15 seconds.
- Snapshot uploads have a hard minimum 60-second attempt spacing; a backlog cannot reactivate
  the former five-second TLS burst loop.

## Percentage-range recovery

Two protections prevent the percentage regression:

1. A merely nonempty queue is considered normal. Auxiliary state pulls are blocked only when
   the 8,192-row outbox reaches the 75%-full emergency threshold.
2. Successfully learned core ranges (`ready`, `dry`, `wet`, trend flag) are cached in
   `/autocal-cache-v1.json` and loaded before the first normal render. The scheduled cloud pull
   then refreshes the cache and marks sensor data dirty so cached cards redraw immediately.

The physical display shows a percentage only when both conditions are true:

- the card has `moisture.auto=true`; and
- the backend reports that sensor's learned calibration as `ready=true`.

At final verification, sensors 1-3 met both conditions. Sensor 4 had a learned range but its card
was intentionally configured `auto=false`. Sensor 5 was configured automatic but remained
backend `ready=false`, so raw display for those two was correct rather than a recurrence.

## Capacity for 10-16 sensors

The intended operating envelope is comfortably below the upload capacity:

| Scenario | Approximate ingress | Capacity comparison |
|---|---:|---:|
| 10 sensors at one unique reading/minute | 10 rows/min | 9.6x below a 96-row batch |
| 16 sensors at one unique reading/minute | 16 rows/min | 6x below a 96-row batch |
| 1 fast sensor at one row/3 s + 15 at one/min | 35 rows/min | 2.7x below a 96-row batch |
| all 16 sensors at one row/3 s indefinitely | 320 rows/min | unsupported; exceeds drain rate |

The 8,192-row LittleFS backlog holds approximately 8.5 hours at 16 rows/minute or 3.9 hours at
35 rows/minute. These are planning estimates, not permission to increase probe advertising
arbitrarily: actual admission must keep sustained unique-row ingress below sustained successful
cloud drain.

The backend accepts at most 128 rows per request; firmware deliberately uses 96 to preserve
memory/stack headroom. Do not increase the number by returning to a single batch-sized `String`.

## Durability boundary

RTC slow memory survives receiver crashes and software resets but not complete removal of power.
Rows already spilled to LittleFS survive power loss. In normal online operation, a total power
cut can erase the small current RTC batch that has not yet received its next cloud acknowledgement.
Eliminating that final boundary without reintroducing internal-flash pauses requires dedicated
nonvolatile hardware suited to frequent low-latency writes (for example external FRAM), not more
per-wake LittleFS synchronizations.

## Permanent regression rules

The following are release invariants, enforced by
`DisplayCenter/scripts/verify_receiver_cloud_recovery.mjs`:

1. Keep bounded 64-row RTC-backed ingress and 8,192-row LittleFS outage capacity.
2. Never call LittleFS or HTTP from the BLE callback.
3. Healthy uploads must copy/acknowledge RTC staging directly; do not force a journal commit
   immediately before each upload.
4. Never gate auto-cal/config/display pulls on `queuedHistoryCount() > 0`. Only the explicit
   75%-full emergency threshold may reserve all network slots for history recovery.
5. Restore the auto-calibration cache during setup and refresh it through a guaranteed scheduler
   slot.
6. Connect TLS before constructing/streaming history rows.
7. Keep compact history streaming; never rebuild one batch-sized JSON body in RAM.
8. Keep the hard upload-spacing floor and one-operation-per-network-slot scheduler.
9. Remove rows only after a successful backend acknowledgement of the exact copied prefix.

The standard compile/upload wrapper runs this verifier before invoking Arduino CLI. A source
change that violates these rules must fail before firmware can be flashed.

## Verification evidence

Final production build:

- program storage: 2,217,140 bytes (70%);
- global RAM: 63,380 bytes (19%);
- remaining RAM for local allocations: 264,300 bytes;
- cloud task stack high-water margin observed live: at least 5,656 bytes.

Live receiver results after the final flash:

- three observed upload cycles completed HTTP 200;
- `cloudConsecutiveUploadFailures = 0`;
- `historyQueueDropped = 0`;
- `historyJournalStorageErrors = 0`;
- durable backlog returned to `0` after uploads;
- latest queue sample was four newly staged rows awaiting the next normal batch;
- `cloudJournalWriteDiag.atMs = 0` across healthy cycles, proving normal operation issued no
  periodic history-journal writes;
- no uptime reset occurred during the verification window;
- sensors 1-3 restored learned percentage ranges.

Run the enforced checks with:

```powershell
node DisplayCenter\scripts\verify_receiver_cloud_recovery.mjs
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action compile
```

After a field flash, acceptance requires at least two complete upload intervals while checking
`/device`, `/data`, and `/cloud`: HTTP 200, failures/drops/storage errors zero, learned cards ready,
durable backlog draining, and journal-write timestamp not advancing during healthy operation.

## Related records

- `RECEIVER_DURABLE_OUTBOX_2026-08-31.md` - journal/cursor/TLS pacing design and earlier backlog soak.
- `RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md` - mbedTLS allocation diagnosis and
  TLS-first streamed uploader evidence.
- `RECEIVER_LOSSLESS_HISTORY_BATCHING_2026-08-22.md` - original history batching behavior, now
  superseded where its fixed-size RAM queue differs from this document.
