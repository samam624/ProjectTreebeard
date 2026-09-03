# Receiver durable outbox and paced cloud sync (2026-08-31)

## Failure confirmed on the live receiver

The failure was receiver-side, not Supabase capacity. Error-level ESP32-core logging showed
that every apparent HTTPS `connection refused` was actually `mbedtls_ssl_setup()` returning
`MBEDTLS_ERR_SSL_ALLOC_FAILED`: the receiver allocated a roughly 10KB batched JSON `String`
before TLS tried to reserve its large contiguous record buffers. Supabase returned HTTP 200
whenever TLS was able to start. The previous in-RAM history queue also disappeared on resets.

The earlier batching implementation also contained a direct regression: a successful upload with
more rows waiting scheduled the next full JSON + TLS upload only five seconds later. Adding
sensors made the queue exceed one batch more often, so it recreated the same WiFi/BLE/TLS
contention that batching was intended to remove. In the same cloud-task iteration, as many as
four other HTTPS calls could then run back-to-back.

The observed reset reason was `power-on`, so power integrity remains a separate hardware item
to verify. Software must nevertheless tolerate that reset without losing measurements.

## Implemented data path

```text
BLE callback
    -> 64-record fixed RAM ring mirrored into checksummed RTC memory
    -> healthy path uploads directly from the RTC-backed ring
    -> outage spillover to checksummed LittleFS journal (8,192 pending records max)
    -> reserve TLS buffers before serializing history
    -> stream up to 96 idempotent rows with one reusable row buffer
       (no faster than once per 60 seconds)
    -> contiguous cloud acknowledgement
    -> double-buffered durable head cursor
    -> periodic journal compaction
```

Important invariants:

- The BLE callback never performs filesystem or network I/O.
- A crash/reset restores unacknowledged ingress rows from RTC memory. Healthy operation does
  not program LittleFS; a prolonged cloud/WiFi outage spills staged rows to flash before the
  ring fills, retaining the large power-safe backlog.
- Each record stores its boot id and sequence, so its observation id is stable across reboot.
- Journal records and cursor files are versioned and checksummed.
- A torn append discards only the invalid tail on recovery.
- Cursor loss can replay accepted records, but cannot skip unaccepted records. The backend's
  observation-id uniqueness makes replay idempotent.
- Compaction writes a replacement file before changing the active journal. Its cursor ordering
  deliberately favors safe replay over possible loss during a power interruption.
- All cloud operations share one scheduler lane with at least 15 seconds between connections.
- A history backlog never activates a faster TLS drain loop. Ninety-six rows per 60 seconds is
  four times the measured roughly 24-row/minute bench arrival rate. A merely nonempty queue is
  normal and cannot starve configuration/display/auto-calibration pulls; auxiliary pulls yield
  only at the 75%-full emergency threshold.
- TLS connects before any history payload is allocated. The compact body retains only a small
  latest-value prefix and reuses one roughly 400-byte row buffer as `HTTPClient` pulls the body.
  Batch growth is therefore no longer batch-sized heap growth.
- Learned auto-calibration bounds are cached in LittleFS and loaded before the first render.
  The cloud pull gets a guaranteed scheduler slot and refreshes that cache, so a reboot cannot
  leave previously learned physical cards stuck on raw readings.

## Timestamp behavior

Every successful authenticated snapshot response supplies `serverTimeMs`. The receiver anchors
future observations to that time and stores `observedAtMs` in the journal. The backend accepts
absolute times only within a bounded 90-day-past / five-minute-future window; invalid clocks use
the older bounded `queuedAgeMs` fallback. This preserves timestamps for queued records after a
reset without trusting arbitrary client time.

## BLE scheduling

Passive scan sessions now run for 30 seconds with a 50 ms inter-session gap and clear results at
the session boundary. This reduces stop/clear/restart churn from about 18 times per minute to two,
while still bounding NimBLE's discovered-device storage. Cloud HTTPS gets an explicit radio slot:
an active scan is stopped and verified quiescent before the request, then restarted afterward.

## Verification

- ESP32-S3 production build passes with no PSRAM:
  - program: 2,217,140 bytes (70%)
  - globals: 63,380 bytes (19%)
  - remaining for local allocations: 264,300 bytes
- `node DisplayCenter/scripts/verify_receiver_cloud_recovery.mjs` passes.
- `node --test cloud/supabase/functions/receiver-snapshot/history-batch.test.ts` passes all 11 tests.
- `git diff --check` passes.

Live receiver verification on COM16 / `10.0.0.35`:

- The TLS-first 32-row control build completed five consecutive HTTP 200 uploads after the prior
  capture had measured 57.9% failures; no mbedTLS allocation errors occurred.
- The streamed diagnostic build completed five consecutive requests; each acknowledged exactly
  96 records (journal head advanced 7,680 bytes each time). The queue fell from 1,808 to 1,518
  in roughly four minutes despite ongoing arrivals, with zero failures, dropped rows, journal
  errors, or resets. All five active sensors reported `missedTotal: 0` during the soak.
- Post-upload free heap improved from roughly 46KB on the retained 32-row String path to roughly
  64KB while delivering three times as many rows. Cloud-task stack high-water margin was 5,648
  bytes.
- The final production (`DebugLevel=none`) flash then booted the same v6 source and completed two
  more 96-row uploads with HTTP 200 and zero failures. The queue reached 1,366 and the second
  acknowledgement safely triggered journal compaction (head returned to zero, storage errors
  remained zero).

Display/calibration regression follow-up (v7, 2026-09-01):

- Root cause of the missing percentages was scheduler starvation: any nonempty history queue
  blocked the auto-calibration pull, while a fast probe kept that queue continuously nonempty.
- Root cause of the every-few-seconds display pause was one LittleFS open/append/flush/close per
  unique wake; measured journal transactions ranged from 29 to 240 ms.
- The final healthy path acknowledged RTC-staged rows directly. Across three live HTTP 200 upload
  cycles, `cloudJournalWriteDiag.atMs` remained `0`, with zero dropped rows, storage errors, or
  consecutive upload failures.
- Cards 1-3 were confirmed both `moisture.auto=true` and cloud `ready=true`, with restored learned
  ranges. Card 4 is intentionally configured `auto=false`; card 5 remains backend `ready=false`.

## Rollout checks

After deploying `receiver-snapshot` and flashing the receiver, `/device` exposes:

- `cloud.historyStagingCount`
- `cloud.historyDurableCount`
- `cloud.historyJournalHeadBytes`
- `cloud.historyJournalBytes`
- `cloud.historyJournalReady`
- `cloud.historyJournalStorageErrors`
- `cloud.networkMinGapMs`

Acceptance criteria for a healthy-path soak are: journal ready, storage errors and dropped rows
remain zero, staged rows clear after acknowledgement, durable count stays/returns at zero,
`cloudJournalWriteDiag.atMs` does not advance, upload attempts never occur five seconds apart, BLE
ages remain bounded, learned automatic cards report `ready=true`, and uptime does not reset. During
an intentional outage, journal timestamp/durable count should advance before RTC staging can fill,
then drain idempotently after recovery. Separately test the 5 V rail/cable and brownout behavior
because software recovery cannot repair unstable power.

The final user-visible incident, capacity envelope, durability boundary, and permanent regression
rules are consolidated in `RECEIVER_PERCENT_RANGES_DISPLAY_FREEZE_AND_UPLOAD_FIX_2026-09-01.md`.
