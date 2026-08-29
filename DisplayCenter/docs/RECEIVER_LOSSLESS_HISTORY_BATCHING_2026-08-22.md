# Receiver lossless history batching (2026-08-22)

## Outcome

Receiver build `MSV2 runtime build 2026-08-22 lossless-history-batch-v1` is live on COM16.
The configured cloud interval remains 300 seconds, but that interval is now only the HTTPS
transport cadence. Every unique BLE wake received between uploads is retained and written as its
own Supabase history row.

Before this change, `sensors[sensorId]` was only a latest-value cache. A new wake overwrote the
previous wake, and `snapshotJson()` serialized that cache once per cloud interval. With a
five-minute interval, four of five minute-cadence readings never reached history.

## Receiver queue

- The BLE duplicate-wake check still rejects repeated advertisement frames.
- Each new wake is copied into a 512-entry `PendingHistoryReading` ring queue after receiver-side
  moisture smoothing is updated.
- Each item retains sensor ID, wake, measured and displayed moisture, moisture scale, raw kHz or
  full FDC counts, battery percentage, RSSI, capture `millis()`, and a boot-scoped sequence.
- The cloud uploader copies at most 32 oldest entries into one request. It removes that exact
  prefix only after HTTP 2xx plus an exact `historyAccepted` count from the server.
- Failed connections, failed sends, HTTP errors, and acknowledgement mismatches keep the queue
  intact. Backlogs drain in five-second-spaced batches; failed uploads retry after 17 seconds.
- History upload runs before lower-value config/display cloud pulls when multiple jobs are due.
- `/cloud` and `/device` expose queue depth, capacity, and the cumulative overflow/drop count.

At four normal one-minute production probes the queue represents about two hours of outage
headroom. The current sensor-3 bench transmitter advances about every three seconds, so it consumes
that headroom much faster. If all 512 entries are occupied, a new entry is rejected and
`historyQueueDropped` increments rather than silently overwriting an older unacknowledged row.

The queue is RAM-resident. It survives Wi-Fi/TLS/server failures but not a receiver reset or
firmware flash. Making the queue power-loss durable would require a separate flash-wear-aware
journal; it is not implied by this fix.

## Cloud ingestion

- Migration 030 adds nullable `observation_id`, a receiver-scoped partial unique index, exact
  `p_received_at`, and retry-idempotent single-row insertion.
- Receiver observation IDs are `<random boot id>-<monotonic sequence>`. A retry returns before
  scale-reset side effects as well as before duplicate insertion.
- Migration 031 adds `msv2_store_sensor_reading_batch`, which processes up to 128 readings in one
  database transaction and returns the acknowledged count.
- `receiver-snapshot` treats the presence of `historyBatch`, including an empty array, as the new
  protocol. It no longer inserts the latest `plants` snapshot into history for a batch-capable
  receiver. Older receivers that omit the field retain legacy behavior.
- `queuedAgeMs` is converted to a server-side observation timestamp, so rows retain their actual
  spacing instead of sharing the batch upload time.
- The transient batch envelope is removed before the latest-snapshot upsert, avoiding database
  bloat. The latest snapshot and insert-only history remain separate concerns.

## Verification

- Cloud unit tests: 14/14 passing, including explicit-empty-batch behavior, legacy fallback,
  server bounds, timestamp reconstruction, and observation-ID validation.
- Firmware: 2,153,572 bytes flash (68%); 74,924 bytes global RAM (22%), leaving 252,756 bytes.
- COM16 flashed and returned on the same port.
- A transient connection refusal and one send failure both retained the complete queue with zero
  drops; automatic retries later succeeded.
- Two consecutive full 32-row batches returned HTTP 200 and reduced the live backlog to only the
  readings that arrived while uploads and other checks were running.
- Post-fix MSV3 wakes 135, 136, 137, and 138 are separate Supabase rows with distinct observation
  IDs, full raw FDC counts, and capture-time gaps of 69.5, 70.1, and 70.4 seconds.

Only readings actually received over BLE can be queued. A probe may measure internally more often
than it advertises, and a radio packet missed by the receiver cannot be reconstructed later.
