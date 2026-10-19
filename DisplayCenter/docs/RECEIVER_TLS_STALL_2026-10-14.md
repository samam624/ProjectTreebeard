# Receiver cloud uploads stalled for 6.7 h (2026-10-14)

**Status:** cleared by a USB reset at 10:00:09 PDT; backlog uploaded with nothing dropped. The
cause was internal to the receiver. That it was a TLS buffer-allocation failure is **inferred, not
measured**: the production build throws the error code away (see "What would measure it").

Receiver `MSV2088A3104A7AC` at `10.0.0.35`, build `Treebeard runtime build 2026-10-11
water-now-region-v2-msv4-262166`, booted 2026-10-11 ~22:29 PDT (`USB peripheral reset`).

## Timeline (PDT, 2026-10-14)

| Time | What | Source |
|---|---|---|
| ~03:05 | Last rows in the cloud for sensors 1, 7, 10 and 11, all at 10:05 UTC | `fetch_supabase_history.py` |
| 03:05-10:00 | Every snapshot upload failed `TLS connect failed (pre-connect)`; config pull and display sync failed `connection refused` (HTTPClient's string for -1) | `/cloud`, `/device` |
| 09:56 | 135 consecutive upload failures, 81 display-sync failures; last success at uptime 189,385,544 ms | `/device` |
| 09:57 | 1,812 rows queued, 0 dropped, 0 journal storage errors | `/device` |
| 10:00:09 | Hard reset over USB (esptool `read-mac --after hard-reset`, COM16) | |
| 10:00:35 on | Uploads HTTP 200, config pull and display sync clean | `/cloud` |
| 10:04-10:05 | Two consecutive pre-connect failures on the fresh boot (uptime 274 s and 334 s), then success | `/cloud` |
| 10:11 | Queue 1,000 and falling ~95 rows per upload, 0 dropped | `/cloud` |

## Ruled out, each by measurement

| Suspect | Measurement | Result |
|---|---|---|
| Wi-Fi | `/wifi` | Connected since 9 s after boot, `disconnectCount` 0, RSSI -42 dBm |
| Router DNS | `nslookup` against 10.0.0.1 | Resolves the Supabase host (172.64.149.246, 104.18.38.10) |
| Server certificate rotation | `openssl s_client -showcerts` | `supabase.co` <- WE1 <- GTS Root R4 <- GlobalSign Root CA: both pinned roots still complete the chain; verify OK |
| Server refusing the ESP32's TLS | `openssl -tls1_2 -curves prime256v1` with ECDHE-ECDSA AES128-GCM, AES256-GCM, CHACHA20 | All three handshake and verify. The core has `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3` off, so TLS 1.2 is what the receiver offers |
| IP block at Cloudflare | The PC shares the receiver's public IP | The PC connected and fetched throughout |
| Clock | Sketch never sets time; `CONFIG_MBEDTLS_HAVE_TIME_DATE` not set | Certificate dates are not checked |
| Socket exhaustion | Receiver's web server polled 64 times | Answered every one |

A reset alone cured it, which on its own puts the fault inside the receiver.

## Heap, failing vs healthy

| | Failing (40 samples, 09:53-09:57) | Healthy after reset (24 samples, 10:00-10:03) |
|---|---|---|
| `largestFreeBlockInternalBytes` | **31,732 on every sample** | 8,180-42,996; 36,852-42,996 between scans |
| `freeHeapNowBytes` | 60,040-90,236 | 17,520-95,644 |
| `minFreeHeapEverBytes` | 7,216 | 15,500 within 4 minutes of boot |
| Free heap before / after a successful upload | 99,216 / 56,452 (the last one, ~03:05) | 77,344-176,004 / ~54,400 |

The largest internal block was pinned at one value in the failing state, but it moves on a healthy
boot, where 31,732 also shows up between peaks. A single reading of 31,732 means nothing; the
difference is that it never rose.

Free heap ran a 31 s sawtooth in both states (about 5 KB lost per 6 s, then recovered).

## Core facts that bound it (esp32 core 3.3.11, `esp32s3-libs/3.3.11/sdkconfig`)

- `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`: TLS buffers come from **internal RAM only**; the 8 MB
  PSRAM enabled on 2026-09-02 cannot hold them.
- `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, symmetric: two record buffers of ~16.7 KB each.
- `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y`; `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`.
- `libraries/BLE/src/BLEScan.cpp` (NimBLE path) keeps a `new BLEAdvertisedDevice` for every
  advertiser heard until `clearResults()`, because `m_maxResults` defaults to `0xFF` and has no
  setter. The sketch scans 30 s, then clears, and never reads the stored results. The 31 s
  sawtooth matches that period (attribution by period only).

## Measured on the new build, 10:49:47 PDT

At 150 s of uptime the new build caught a failed connect:

```
TLS connect failed (pre-connect): code -32512, largest block 31732   (free heap 98,704)
```

-32512 is `MBEDTLS_ERR_SSL_ALLOC_FAILED`. With almost 99 KB of internal heap free, the largest
block was 31,732 bytes, and the TLS setup could not allocate. That is the same block size that was
pinned through the whole stall. A transient failure is therefore **measured** to be the
allocation failure at exactly the stall's signature. The stall itself was never caught with a
code, so it remains inferred, though now on much firmer ground.

## Inferred, not measured

The stall was `MBEDTLS_ERR_SSL_ALLOC_FAILED`, the failure measured on 2026-08-31
(`RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md`): a long-lived allocation landed in the
largest internal free region around 03:05 and capped it at 31,732 bytes. From then on the two
record buffers plus the handshake no longer fit, so every connect failed until the reset. The
two failures right after the reset show that TLS connect is marginal on this receiver even when
the heap is healthy.

## What would measure it

After a failed `secureClient.connect()`, `NetworkClientSecure::lastError()` returns:

- **0**: DNS failed (`Network.hostByName()` returns before `start_ssl_client()`)
- **-1**: socket or TCP connect failed
- **the mbedTLS code** for anything else: -32512 (`-0x7F00`) is the allocation failure;
  certificate and handshake failures have their own codes

`last_error` survives `stop()`. The pre-connect failure path in `runCloudSnapshotUploader()`
currently records only the fixed string. (The 2026-08-31 doc says `lastError()` does not
distinguish failure modes. That is true of DNS vs TCP, both collapsed to -1 or 0 here, but not of
the allocation failure.)

## Why now, after weeks of working

**Measured:**
- The cloud holds 237 receiver boot sessions since 2026-08-23. `observation_id` starts with a
  per-boot session ID.
- Several earlier sessions ran far longer than this one without a visible stall: 125.3 h
  (09-22 to 09-27), 94.4 h, 86.7 h (10-07 to 10-10), 71.2 h, 65.8 h and 60.5 h. This session
  stalled 52.6 h after boot.
- Whether any of them stalled cannot be measured. `received_at` is the observation time, not the
  insert time, and the outbox backfills in order. A stall that ended on its own, or at the next
  reflash, leaves no trace.
- TLS headroom has been thin all along: the largest internal block peaks at 36-43 KB against two
  ~16.7 KB record buffers plus the handshake. Even this morning's fresh boot failed 2 of its first
  5 connects.

**Inferred:** the stall needs a long-lived allocation to land in the one large free region. That is
a chance event whose odds grow with uptime and heap churn, so a first visible stall after weeks
fits without anything having changed. Two things did change recently, with no evidence either
caused it: the receiver build of 10-11 (water-now region, MSV4 decoding) and sensor 11 joining at
~22:59 on 10-13. Heap telemetry was never stored, so builds cannot be compared after the fact.

## Fix flashed 2026-10-14 10:47 PDT

Build `Treebeard runtime build 2026-10-14 tls-stall-restart`. Chosen by the maintainer:

- **Error capture:** each failed upload connect records `lastError()`, the largest internal block,
  free heap and a per-boot count. They show in `/cloud` under `upload.lastTlsFailure`, and in
  `upload.lastError` as `TLS connect failed (pre-connect): code N, largest block B`.
- **Stall restart:** after 30 min with no successful upload while WiFi has stayed connected, the
  receiver restarts itself (`serviceCloudStallRestart`, run between cloud-task iterations).
  - A boot caused by a stall restart cannot do it again until it has been up 6 h, and it skips the
    startup chime.
  - It keys on time since the last success, not on the failure count, because a low-heap skip
    does not count as a failure.
  - A WiFi outage never triggers it, because `wifiLastConnectedMs` moves only on reconnect.
  - `/cloud` shows `upload.stallRestart.bootWasStallRestart`, and the LittleFS boot record carries
    `wasCloudStallRestart`.
- `verify_receiver_cloud_recovery.mjs` asserts all of the above.

## Identity note

The receiver is the board on **COM16** (USB-Serial/JTAG `303A:1001`, chip MAC
`28:84:85:46:70:D8`). Its receiver ID `MSV2088A3104A7AC` is the value stored in its identity
file, not one derived from this chip's MAC, so the MAC cannot be used to confirm which port is
the receiver.
