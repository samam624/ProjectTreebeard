# Receiver cloud-upload intermittent "connection refused" (2026-08-31)

> **RESOLVED 2026-08-31 (late).** Root cause found by direct measurement: the failures are
> **`mbedtls_ssl_setup()` returning `MBEDTLS_ERR_SSL_ALLOC_FAILED` (-0x7F00 = -32512)** - an
> out-of-contiguous-internal-RAM condition, not a network fault at all. See
> "**ROOT CAUSE**" at the bottom of this doc. Everything above that section is the elimination
> trail that led there; keep it, but read the root-cause section first.

**Status (historical):** Unresolved, handed off. Eight concrete hypotheses eliminated with direct
measurement; no root cause found yet. Not a data-loss or crash issue currently - the durable
outbox (see `RECEIVER_DURABLE_OUTBOX_2026-08-31.md`) is correctly buffering everything, so this
is a delivery-latency/reliability problem, not a safety one, while it stays open.

## Symptom

After the durable-outbox rewrite landed (fresh, function-local TLS clients per cloud call,
32-row compact batches, single paced sync lane), snapshot uploads to Supabase intermittently
fail with a generic `connection refused`. Roughly 30-40% of attempts fail; the rest succeed
in 2-3 seconds. `queuedHistory` grows steadily as a result (1522 and climbing as of this
writing) - no rows are lost (`historyQueueDropped: 0`, `historyJournalStorageErrors: 0`
throughout), they just aren't draining as fast as they arrive.

`"connection refused"` is this stack's catch-all string for essentially any failed
`connect()`/handshake - confirmed by reading `NetworkClientSecure.cpp` in the installed esp32
Arduino core (3.3.11): socket(), connect(), select(), and getsockopt() failures inside
`start_ssl_client()` all collapse to a bare `return -1;` before mbedTLS is ever involved, so
`NetworkClientSecure::lastError()`/`mbedtls_strerror()` would not actually distinguish
DNS-vs-TCP-vs-TLS failure modes here - not worth adding as a diagnostic, already checked.

## Ruled out tonight, each with direct measurement (not assumption)

| # | Hypothesis | How it was tested | Result |
|---|---|---|---|
| 1 | General heap exhaustion/fragmentation | Watched `freeHeapNowBytes`/`largestFreeBlockInternalBytes` across dozens of attempts | Stayed 68-104KB free / 31-40KB largest block the whole time, no downward trend |
| 2 | Global socket/PCB pool exhaustion | Polled the receiver's own local WebServer (`/device`, `/cloud`) continuously throughout every failure window | Never once failed to answer - if sockets/PCBs were globally exhausted, inbound accepts would starve too |
| 3 | A shared, reused `WiFiClientSecure` accumulating bad state | Already refactored (this session, before this investigation started) to a fresh, function-local `WiFiClientSecure`+`WiFiClient`+`HTTPClient` trio per call, all 5 cloud functions | Identical failure pattern persisted after the change - not this |
| 4 | Slow response / edge-function cold start past the 8s client timeout | Timed every successful upload | All completed in 2-3s; failures are outright connect() failures, not mid-request timeouts |
| 5 | "Works once per boot, then permanently broken" | Live serial capture, 8 consecutive real attempts with timestamps | `succeed(0s) -> fail(+66s) -> fail(+64s) -> succeed(+123s) -> succeed(+66s) -> fail(+66s) -> succeed(+63s) -> succeed(+67s)` - recovers repeatedly, not permanent |
| 6 | "Just needs enough backoff/idle time to recover" | Same capture as #5 | No clean correlation - both short and long gaps produced both successes and failures |
| 7 | BLE active-scan radio contention (cloudSyncTask is priority 1, bleScanTask is priority 2, both pinned to core 0 - see "Structural facts" below) | Added `cloudRadioSlotDiag` instrumentation (`wasScanning`, `stopCallMs`, `stillScanningAfterSettle`) logged on every real attempt, correlated against outcome | `stillScanningAfterSettle` was `0` on **every single attempt**, success or failure - the existing 75ms settle delay in `beginCloudRadioSlot()` always fully quiesces the scan. Two failures happened with `wasScanning=0` (BLE wasn't even scanning at the time). **Directly disproven, not just weakened.** |
| 8 | WiFi station reconnection churn | Grepped the full serial capture for `[Treebeard wifi]` disconnect/reconnect log lines during a window with both failures and successes | Zero disconnect/reconnect events - station connection was continuously healthy throughout |
| 9 | Network path (router/ISP/DNS) or Supabase server-side | Hit the exact same Supabase edge function URL 20x from a laptop on the same LAN/router/WAN path over a comparable window, while the receiver was also failing intermittently | 20/20 clean TCP+TLS connections, 228-493ms each, zero failures - the network path and backend are both fine |

Also checked and ruled out as *contributing* factors:
- DNS resolution timing: confirmed via `HTTPClient.cpp` that `connect()` (and therefore DNS
  resolution) happens inside `POST()`, which `beginCloudRadioSlot()`/`endCloudRadioSlot()`
  already wraps - not an unprotected gap.
- `NetworkClientSecure::lastError()`: exists but not useful here (see catch-all note above).

## Structural facts discovered (useful context, not necessarily the cause)

Confirmed from the actual generated `sdkconfig` of this build (not guessed):

- Arduino's own `loopTask` (runs the WebServer, display rendering, touch handling, WiFi
  connection maintenance) is on **core 1**, priority 1 (`CONFIG_ARDUINO_RUNNING_CORE=1`).
- `cloudSyncTask` (does the actual HTTPS POST) is pinned to **core 0**, priority 1
  (`CLOUD_SYNC_TASK_PRIORITY`).
- `bleScanTask` (drives NimBLE scanning) is also pinned to **core 0**, priority 2
  (`BLE_SCAN_TASK_PRIORITY`).
- This priority ordering is **deliberate**, not an oversight - see the comment at
  `CLOUD_SYNC_TASK_PRIORITY`'s declaration: *"BLE gets the higher application-task priority so
  a due cloud request cannot delay the next scan window. The ESP32 BLE host/controller tasks
  retain their own higher priorities."* The original author already knew the underlying
  BLE host/controller tasks run higher than either app-level task and chose to protect BLE scan
  timing over cloud upload timing. **Do not casually invert this ordering** - I nearly did before
  re-reading this comment; it would trade upload reliability for probe-reception reliability,
  which may be the wrong trade (5 active probes now, durable journal buffers upload delays fine,
  but BLE packet loss is not similarly recoverable).
- Given finding #7 above, this priority layout does not appear to be the actual cause of the
  connection failures - it was the leading hypothesis for a while and is now well-tested away.

## Diagnostic instrumentation currently in the code

- `cloudRadioSlotDiag` (in `/device`'s `health` object, and logged to Serial as
  `[Treebeard cloud] radio slot: wasScanning=... stopCallMs=... stillScanningAfterSettle=...`
  immediately after every real upload attempt's own result line) - see
  `lastRadioSlotWasScanning`'s declaration comment near `cloudRadioExclusive`. Safe to leave in
  place; it's a passive observer, doesn't touch scheduling/timing/backoff.
- A separate `testPlainLoopbackConnect()` diagnostic (plain WiFiClient connect to the
  receiver's own station IP) was added, tested, and **removed** - it failed 100% of the time
  regardless of whether the real HTTPS attempt succeeded that same cycle, including during
  windows when HTTPS was working fine. Concluded this is a known WiFi-station-can't-reliably-
  hairpin-to-its-own-IP limitation, not a valid control for TLS-vs-general-TCP. **Don't
  re-add this specific approach** - if a plain-vs-TLS comparison is wanted again, connect to a
  *different* host on the LAN (e.g. a small server on a laptop), not the receiver's own IP.

## What's genuinely left untested

Everything at the application/firmware-log level has been checked. What remains needs either
expert ESP-IDF/WiFi-driver-internals knowledge or lower-level tooling not available in this
session:

1. **ESP-IDF's WiFi/BT coexistence arbiter below what app-level `isScanning()` can see.**
   Finding #7 ruled out the *application-visible* BLE scan state as correlating with failures,
   but the coexistence arbiter operates at the PHY/controller level - it's possible some BLE
   controller-level activity (not "scanning" in the app sense) still occasionally wins a radio
   arbitration slot WiFi needed. Would need ESP-IDF's own coexistence statistics/logging
   (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE` debug output, or a much higher `CORE_DEBUG_LEVEL`
   rebuild to surface the WiFi/BT driver's own `log_e`/`log_w` lines, which the current build
   doesn't emit) to actually see.
2. **RF/physical-layer margin specific to this board or its current location.** Tonight's testing
   happened on a desk with a lot of concurrent 2.4GHz activity (a laptop's own WiFi, this same
   laptop's USB traffic, multiple BLE probes at close range). Worth retesting once the desk is
   quieter, or with the receiver in its normal deployed location, to see if the failure rate
   changes - if it does, that's a strong pointer toward RF margin rather than a firmware bug.
3. **A genuine packet capture** (Wireshark on a machine bridging/monitoring the WiFi channel, or
   the router's own connection logs if it exposes any) during a live failure would settle this
   definitively - something a future session with more time/tooling could set up.

If picking this up: don't restart from hypothesis #1. Start from "what's genuinely left" above,
and bring this doc's evidence table to whatever consultation/tool you have - it should save
significant time re-deriving what's already been eliminated.

## New hypothesis added 2026-08-31 (post-handoff): flash/journal contention

An outside consultation proposed one genuinely new angle not covered by hypotheses #1-9 or the
"genuinely left untested" list: LittleFS journal writes briefly suspend flash cache/interrupts,
and `copyHistoryUploadBatch()` calls `serviceHistoryJournal()` (a real flash append) just a few
lines before `beginCloudRadioSlot()`/`http.POST()` starts the actual connect - see
`runCloudSnapshotUploader()` in `Treebeard_ESP32_Display_Runtime.ino`. Worth measuring before
dismissing, unlike the same consultation's other three suggestions, which this doc's existing
evidence already rules out or which this codebase has already tried and reverted:

- **`WiFi.setSleep(false)` in STA mode** - already tried on 2026-08-13, hardware-confirmed to
  starve the BLE scan task ("good RSSI, huge miss%"), reverted 2026-08-14. See the comment at
  `maintainWifiConnection()`'s radio-reset branch. Do not re-add without re-testing BLE loss% on
  real probes.
- **`NetworkClientSecure::lastError()` after a failed `POST()`** - already checked in this doc
  (see the "connection refused" catch-all note above) by reading the actual installed
  Arduino-ESP32 3.3.11 `NetworkClientSecure.cpp`: `socket()`/`connect()`/`select()`/
  `getsockopt()` failures inside `start_ssl_client()` collapse to a bare `return -1;` before
  mbedTLS ever runs, so `lastError()` would not distinguish the failure modes actually occurring
  here.
- **Adaptive/larger upload batch size when the queue is deep** - contradicts a deliberate
  invariant from `RECEIVER_DURABLE_OUTBOX_2026-08-31.md`: faster draining was the direct cause
  of the earlier "receiver refusing local HTTP connections and rebooting" regression that
  durable-outbox-v5 was written to fix. Do not increase batch size or drain rate without
  re-reading that doc's "Implemented data path" section first.

Added passive, timing-only instrumentation (no behavior change) to test the flash-contention
idea: `lastJournalWriteDurationMs`/`lastJournalWriteRecords`/`lastJournalCompactedThisWrite`/
`lastJournalCompactDurationMs`, declared next to `historyJournalStorageErrors`. Logged on every
real upload attempt right after the existing `cloudRadioSlotDiag` line
(`[Treebeard cloud] journal write: durationMs=... records=... compacted=... compactDurationMs=...`)
and exposed at `/device` under `health.cloudJournalWriteDiag`. `durationMs` times the
`serviceHistoryJournal()` append that ran immediately before *this* attempt's connect;
`compacted`/`compactDurationMs` reflect whether *this* attempt's own successful acknowledgement
triggered a compaction afterward (relevant to the *next* attempt, not this one). Not yet
flashed/soaked - next session should flash this, run the same 20-30 attempt capture used for
hypothesis #7, and check whether `durationMs` (or a rare `compacted=1` with a large
`compactDurationMs`) is unusually large on failed attempts vs. successful ones.

Also logged on the same line: `staRssi` (station `WiFi.RSSI()`, a read-only register read - not
a network operation, so it adds no competing DNS/TCP traffic). This tests "genuinely left
untested" item #2 (RF margin) from the same capture, at no extra cost.

Deliberately NOT added, despite being suggested: a pre-flight `WiFi.hostByName()` to log DNS
separately. DNS already resolves inside `POST()` (see the "Also checked" note above), so a
separate lookup would be a real extra network operation on the same contended channel - a
confound in the exact window under investigation, not instrumentation.

### Agreed test order (two independent consultations converged on this)

A second consultation reviewed the same code and independently reached the same conclusion:
flash/journal contention is the best remaining candidate, and BLE task priority should stay
untouched because hypothesis #7 already disproved it. Its recommended order, which this doc
adopts:

1. **Measure first (done, not yet flashed):** journal write/compaction timing correlated with
   each attempt's outcome, per the instrumentation above.
2. **Only if step 1 shows correlation:** a cloud-transport lock that defers journal writes for
   the duration of DNS/TCP/TLS/POST, with a small RAM queue covering the ~2-3s window. This is a
   real behavior change to the durable-outbox data path, so it needs step 1's evidence first -
   the outbox's whole design premise is that the BLE callback and the network lane stay
   decoupled.
3. **Separately, as a controlled A/B:** `WiFi.setSleep(false)` on the STA path. The 2026-08-13
   BLE-starvation result that got this reverted predates the durable-outbox rewrite, which
   changed BLE scan sessions from ~18 stop/clear/restart cycles per minute to 2 (see
   `RECEIVER_DURABLE_OUTBOX_2026-08-31.md`), so the coexistence picture is genuinely different
   now and the old result may no longer hold. Still must be measured *both* ways per the
   existing code comment: upload failure rate AND BLE loss% on real probes. Not a ship-it
   change; a temporary test build.
4. **Last:** BLE priority / deeper ESP-IDF coexistence. Do not reopen before 1-3.

### RESULT of step 1 (measured 2026-08-31 19:36-20:09, 19 attempts) - hypothesis DISPROVEN

Flashed the instrumented build and captured 19 real attempts (8 ok / 11 failed, **57.9% failure
rate** - notably worse than the 30-40% recorded earlier in this doc).

| Outcome | n | journal write mean | median | min | max | staRssi mean |
|---|---|---|---|---|---|---|
| ok | 8 | 58.9 ms | 39.5 ms | 29 | 195 | -43.6 |
| failed | 11 | 48.6 ms | 35.0 ms | 29 | 186 | -42.6 |

**Successes had a *longer* journal write than failures**, not shorter, and the two distributions
overlap almost exactly (both min 29 ms, both carrying a single ~190 ms outlier - the 195 ms one
was a success, the 186 ms one a failure). There is no correlation. Hypothesis #10 (flash/journal
contention, app-visible timing) is disproven the same way #7 was.

Also settled by the same capture:
- **Compaction excluded outright:** `compacted=0` on all 19 attempts - no compaction ran in the
  window at all, so it cannot explain any of these failures.
- **RF margin (untested item #2) does not correlate:** `staRssi` was -42 to -46 throughout, and
  essentially identical for pass and fail. Strong signal; failures happen anyway.
- **Re-confirms #7:** `wasScanning` was 1 on 6/11 failures and 4/8 successes - no correlation.
- **Failures are instantaneous** (sub-second, same log timestamp as the attempt start) while
  successes take ~2 s. These are outright connect failures, not timeouts.

### The queue math is now empirically broken (separate from the connect failures)

Over the 30.5-minute capture `queued` went 1810 -> 2312 while 7 compact batches (224 rows) were
delivered. Arrivals were therefore ~726 rows / 30.5 min = **~23.8 rows/min**, against a
theoretical maximum drain of ~31 rows/min at the observed ~62 s cadence and 100% success - and
an *actual* drain of ~7.3 rows/min at the observed 57.9% failure rate.

This falsifies the premise stated in `RECEIVER_DURABLE_OUTBOX_2026-08-31.md` that "thirty-two
rows per 60 seconds is above the measured current arrival rate." With 5 probes it is above it by
only ~30% even in the perfect case, and far below it in practice. **The backlog can never
drain.** Revisiting batch size / cadence is now justified *by measurement* - but it is a
throughput fix, and must not be mistaken for a fix to the connect failures. It also must respect
the reason that invariant exists (the earlier reboot regression), so it needs its own soak.

### Next diagnostic (written, compiles, NOT yet flashed): `postErrno`

The one thing that can actually classify these instant failures is the lwIP `errno` from the
socket call that failed. This is **not** the `lastError()` idea already ruled out above:
`lastError()` reports mbedTLS state, which is never reached, whereas lwIP sets `errno` on the
`socket()`/`connect()`/`select()` that actually fails. Captured in
`runCloudSnapshotUploader()` immediately after `http.POST()` returns (cleared to 0 first, read
before `endCloudRadioSlot()` can clobber it) and logged as
`[Treebeard cloud] transport: postErrno=... payloadBytes=... fullSnapshot=...`.

What the value will tell us:
- `ECONNREFUSED (111)` - a genuine RST from the peer; would point back at the network path.
- `ENOMEM (12)` / `ENOBUFS (105)` - lwIP resource exhaustion.
- `EADDRNOTAVAIL (99)` - no free local port (TIME_WAIT / ephemeral port pressure).
- `EHOSTUNREACH (113)` / `ETIMEDOUT (116)` - routing/path.

`payloadBytes`/`fullSnapshot` ride along because in this capture both full snapshots succeeded
(2/2) while the larger 32-row compact batches carried every failure - far too small a sample to
mean anything yet, but cheap to settle.

**Note a real gap in hypothesis #2's reasoning, now relevant again:** that test polled the
receiver's own *inbound* WebServer, which answers on an already-listening socket. That does not
demonstrate that *outbound* connection resources were available. Outbound-specific exhaustion
remains live, and `postErrno` is the cheapest way to confirm or kill it.

## Current live state (as of this doc)

- Build on device: `MSV2 runtime build 2026-08-31 durable-cloud-outbox-v5`
- `queuedHistory: 1522`, `historyQueueDropped: 0`, `historyJournalStorageErrors: 0`,
  `historyJournalReady: true` - durable outbox is healthy and doing its job; this is a
  throughput problem, not a data-loss problem, while it stays open.
- `freeHeapNowBytes: ~73KB` - healthy.
- Uncommitted working-tree changes touch `Treebeard_ESP32_Display_Runtime.ino` (+1059/-139 vs HEAD,
  includes tonight's durable-outbox work, the fresh-client revert, and this investigation's
  `cloudRadioSlotDiag` instrumentation), plus unrelated same-session changes to
  `verify_receiver_cloud_recovery.mjs`, the MSV3 probe sketch/doc (new sensor 5 onboarding),
  and the cloud edge function files (`observedAtMs` support). None of this has been committed
  yet.

## Related: more probes coming

The user's original report was that this failure "started happening when I started storing
data on ESP32 instead of sending literally every single packet to Supabase instantly" and "is
definitely getting worse as I add more sensors." There are 5 active probes right now (sensors
1-5); the user intends to add a 6th soon. **Recommend resolving this connection-refused issue
before adding probe 6** - more probes means more BLE traffic and a larger/faster-growing history
backlog, which will make whatever this is more visible, not less, and makes any future
diagnostic capture noisier (more concurrent BLE activity to rule in/out).

---

## ROOT CAUSE (confirmed 2026-08-31, live hardware)

**The failures are not network failures.** `HTTPClient` reports `connection refused` because that
is its generic string for a `-1` return; the actual error, surfaced by building with
`-CoreDebugLevel error` (new opt-in parameter on `arduino_display.ps1`; production default is
still `none`), is:

```
[E][ssl_client.cpp:41] _handle_error(): [start_ssl_client():318]: (-32512) SSL - Memory allocation failed
[E][NetworkClientSecure.cpp:159] connect(): start_ssl_client: connect failed: -32512
```

`-32512` is `-0x7F00` = `MBEDTLS_ERR_SSL_ALLOC_FAILED`. Line 318 of `ssl_client.cpp` is
`mbedtls_ssl_setup()`, which allocates mbedTLS's large **contiguous** SSL record buffers. It
appeared on **every** failure and on **no** success.

### The measurements that pin it down

Live `/device` telemetry (receiver at `10.0.0.35`, found via its MAC in the host ARP table):

| Field | Value |
|---|---|
| `freeHeapNowBytes` | 80,124 |
| `largestFreeBlockInternalBytes` | **32,756** |
| `minFreeHeapEverBytes` | 25,248 |
| `cloudUploadFreeHeapBeforeBytes` | 105,532 |
| `cloudUploadFreeHeapAfterBytes` | 46,020 |
| `psramSizeBytes` / `psramFreeBytes` | **0 / 0** |

One upload consumes **~59.5 KB** of heap, against a hard ceiling of only **~32.7 KB contiguous
internal**. The order of operations in `runCloudSnapshotUploader()` is what tips it over:

1. the 32-row history batch is serialized into one `String` of **~9.5-10 KB** (measured
   `payloadBytes` 8,963-9,998 across attempts),
2. `cloudHeapHasContiguousRoomForTls()` checks `largestFreeBlockInternal >= 24576` and **passes**,
3. `POST()` then calls `mbedtls_ssl_setup()`, which needs its own large contiguous buffers - and
   fails, because step 1 already carved up the largest block.

That guard's own comment predicted exactly this and said what to do: *"if snapshot uploads still
intermittently fail at this threshold, check the added largestFreeBlockInternalBytes/
psramFreeBytes health fields before raising it again."* Those fields now read 32,756 and 0.

### Why this explains every observation in this doc

- **Sub-second failures** (measured: 0 s to fail, 2-3 s to succeed) - a local `malloc` failure
  needs no network round trip. This is why it never looked like a timeout.
- **`postErrno=119` (`EINPROGRESS`) on every failure, `11` (`EAGAIN`) on every success** - the
  TCP connect had already succeeded into its async path; the failure is *after* it, in TLS setup,
  and neither error path sets `errno`. (Verified against this toolchain's own `errno.h`:
  `ECONNREFUSED` is 111 and `EADDRNOTAVAIL` is 125, so 119 was never either of those.)
- **Uncorrelated with BLE, flash/journal, RSSI, WiFi churn, and the network path** - correctly
  ruled out; none of them were ever involved.
- **The laptop's 20/20 clean connections** - the network and backend really are fine.
- **"Started when I stored data on the ESP32 instead of sending every packet instantly"** - that
  is precisely when a ~10 KB batched JSON `String` started being held across TLS setup.
- **"Getting worse as I add more sensors"** - more probes means fuller batches and more heap
  pressure, so the contiguous block is more often too fragmented at exactly the wrong moment.

### CRITICAL: this inverts the "bigger batches" suggestion

The consultation math about the queue never draining was right, but its proposed fix - raising
the batch to 96-128 rows - would have made the payload **~3-4x larger (~30-38 KB)** and would
have made this root cause dramatically worse, likely failing nearly every upload. **Do not
increase `HISTORY_UPLOAD_BATCH_SIZE`.** The throughput problem is real (see the queue-math
section above) but must be solved without enlarging the pre-TLS allocation.

Raising `CLOUD_SNAPSHOT_MIN_LARGEST_INTERNAL_BLOCK_BYTES` is also **not** a viable fix on its
own: the largest block observed while healthy is 32,756, so any threshold high enough to prevent
the failure would block most uploads outright - the same failure mode as the 48 KB attempt
documented in that constant's comment.

### Candidate fixes, in the order worth trying

1. **Allocate TLS before the payload.** Connect the `WiFiClientSecure` *before* serializing the
   batch, so `mbedtls_ssl_setup()` takes its contiguous buffers from an unfragmented heap and the
   ~10 KB `String` is allocated afterwards. `HTTPClient::connect()` reuses an already-connected
   client, so this is a reordering inside `runCloudSnapshotUploader()`, not a rewrite. Lowest
   risk, directly targets the measured mechanism.
2. **Stream the body instead of building one `String`.** Removes the large contiguous allocation
   entirely. Bigger refactor; also the only option that would later allow larger batches.
3. **Enable PSRAM.** esptool reports this chip has **8 MB embedded PSRAM** ("Embedded PSRAM 8MB
   (AP_3v3)"), but the build pins `PSRAM=disabled`, which is why `psramSizeBytes` is 0. Enabling
   it would let large mbedTLS/GFX allocations leave internal RAM and would remove this ceiling
   structurally. **Do not flip this casually** - it must be verified against this board's pin
   usage (S3 PSRAM uses dedicated pins, and quad-vs-octal must match the part) and a wrong
   setting can prevent boot. Treat as a deliberate, separately-tested change.
4. Only then revisit throughput/cadence for the backlog.

### Diagnostic tooling added by this investigation

- `arduino_display.ps1 -CoreDebugLevel <none|error|...>` - opt-in; production default unchanged
  and it builds to its own build path. `error` is enough to surface `log_e` (this root cause);
  the `select returned due to timeout` line is `log_i` and would need `info`.
- `[Treebeard cloud] transport: postErrno=... payloadBytes=... fullSnapshot=...` per attempt.
- `[Treebeard cloud] journal write: ...` per attempt, plus `health.cloudJournalWriteDiag` at `/device`.

All of the above are passive observers and safe to leave in place. The `CoreDebugLevel` build is
**not** what should ship - reflash without that flag once fixing is done.

### Resolution implemented and verified

The final uploader implements both structural fixes rather than relying on a larger heap guard:

1. A fresh, request-scoped `WiFiClientSecure` completes its TLS connection before history JSON
   serialization starts. This reserves mbedTLS's contiguous buffers while the heap is least
   fragmented.
2. Compact history uses `HTTPClient::sendRequest(Stream*, Content-Length)`. The stream retains
   only the small receiver/latest-values prefix and reuses one bounded row `String`; it never
   constructs a batch-sized body in RAM.
3. Because payload memory is now essentially constant with row count, the paced batch is 96 rows
   per minute (the backend bound is 128). This gives 4x headroom over the measured ~24 rows/minute
   without increasing TLS frequency.

Live results on COM16 / `10.0.0.35`:

- TLS-first control: five consecutive HTTP 200 uploads after the previous 19-attempt capture had
  failed 11 times (57.9%); no `MBEDTLS_ERR_SSL_ALLOC_FAILED` lines.
- Streamed 96-row diagnostic build: five consecutive compact requests advanced the 80-byte-record
  journal head by 7,680 bytes each, proving the backend acknowledged all 96 rows each time.
- Those five cycles reduced the queue from 1,808 to 1,518 despite ongoing arrivals, with
  `cloudConsecutiveUploadFailures=0`, dropped rows `0`, journal errors `0`, no reset, post-upload
  free heap around 64KB, and 5,648 bytes of cloud-task stack high-water margin. All five active
  sensors had `missedTotal=0`.
- The final production (`DebugLevel=none`) flash completed two more 96-row requests with HTTP 200
  and zero failures. The queue reached 1,366; the second acknowledgement also exercised journal
  compaction successfully (head reset to zero with storage errors still zero).

The earlier warning not to increase the batch applied to the old one-big-`String` encoder. It is
still correct for that design, but no longer applies to the streamed encoder now in production.
PSRAM remains disabled; the fix does not depend on unverified board-memory configuration.

---

## VERIFIED FIX AND OUTCOME (2026-08-31, late)

Two changes landed, from two editors working on this file concurrently. **Both were needed**;
neither alone is sufficient, and the note above listing them as alternative "candidate fixes 1
and 2" is superseded by this section.

1. **TLS connects before the payload is built** (`runCloudSnapshotUploader()`): mbedTLS takes its
   contiguous record buffers from an unfragmented heap. `HTTPClient::connect()` returns early
   when the client is already connected, so `POST()`/`sendRequest()` reuses that connection, and
   `begin()` does not reset it. Helper `parseCloudUrlHostPort()` supplies host/port; the new
   pre-connect failure path routes through `noteCloudUploadFailureAndGetBackoffMs()` (extracted
   from the existing post-POST path) so both escalate backoff identically.
2. **`HistoryOutboxStream` streams the history rows** instead of serializing them into one large
   `String`, so the ~10-37 KB contiguous payload allocation disappears entirely.

Change 2 is what makes `HISTORY_UPLOAD_BATCH_SIZE = 96` viable. Without it, 96 rows x
`HISTORY_JSON_BYTES_PER_ROW` (384) = a **36,864-byte** single `reserve()`, which exceeded the
measured ~32.7 KB contiguous ceiling and produced `could not reserve history batch payload` on
every attempt while a backlog existed - observed live at 21:49 and 21:50 before streaming landed.

### Measured outcome

| | before | after |
|---|---|---|
| upload failure rate | 57.9% (19 attempts), then 25% (20 attempts) | **0% (20 attempts)** |
| `MBEDTLS_ERR_SSL_ALLOC_FAILED` | every failure | **none** |
| `queuedHistory` | growing without bound (high-water 2344) | **drained to ~18 and holding** |
| `largestFreeBlockInternalBytes` | 31,732-32,756 | **36,852** |
| `historyQueueDropped` / journal errors | 0 | **0** (no loss at any point) |

Steady state is now ~22-25 rows/min arriving and fully drained each minute, so the throughput
deficit documented earlier in this doc is resolved as a side effect: the drain was never really
32 rows/min-limited, it was limited by the ~40-58% of attempts that never connected.

### Caveats and follow-ups

- The receiver is currently running a **diagnostic build** (`-CoreDebugLevel error`). Reflash
  without that flag before considering this done.
- **None of this is committed.** The working tree also still carries the unrelated MSV3 sensor-5
  and cloud `observedAtMs` changes described earlier.
- The 20/20 verification ran with the queue already drained, so batches were 22-48 rows
  (~7-14 KB streamed). The **96-row backlog case has not been exercised** since streaming
  landed. Worth one deliberate test (e.g. block the uploader briefly, let a backlog build, then
  release) before trusting it under a real outage.
- Serial captures were observed to drop/garble bytes under load - one line read as
  `could not reserve compact history payload`, a string that exists in neither the source nor the
  flashed binary. Aggregate counts across many attempts are sound; do not draw conclusions from
  any single captured line without checking it against the binary.
- PSRAM remains disabled (`psramSizeBytes: 0`) despite 8 MB on the chip. No longer urgent now
  that the contiguous-allocation pressure is gone, but it is still the structural headroom fix.

### Final production supersession (2026-09-01)

The diagnostic-checkpoint caveats immediately above are historical and no longer describe the
deployed receiver. Production `DebugLevel=none` build
`MSV2 runtime build 2026-08-31 durable-cloud-outbox-v7-smooth-display` is flashed on COM16. The
streamed 96-row backlog case and subsequent steady-state batches completed with HTTP 200, zero
consecutive failures, zero dropped rows, and zero journal storage errors.

The later display/calibration follow-up also removed healthy-path journal writes and queue-based
auto-calibration starvation. Its final architecture, supported 10-16 sensor envelope, and enforced
release rules are documented in
`RECEIVER_PERCENT_RANGES_DISPLAY_FREEZE_AND_UPLOAD_FIX_2026-09-01.md`. Treat that document and
`RECEIVER_DURABLE_OUTBOX_2026-08-31.md` as the current production state; retain this file for the
low-level mbedTLS diagnosis and TLS-first evidence.
