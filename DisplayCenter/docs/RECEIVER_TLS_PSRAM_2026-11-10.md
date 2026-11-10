# Receiver TLS buffers moved to PSRAM (2026-11-10)

**Build:** `Treebeard runtime build 2026-11-10 tls-psram-v1`, flashed to `COM16` ~00:32 PST.
**Device:** live receiver `MSV2088A3104A7AC`, 10.0.0.35.
**Commit:** 8493af7.

## Why

This is the same failure as [the 08-31 "connection refused"](RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md)
and [the 10-14 stall](RECEIVER_TLS_STALL_2026-10-14.md): `mbedtls_ssl_setup()` cannot get its
contiguous record buffers (`MBEDTLS_ERR_SSL_ALLOC_FAILED`, -32512). It had never been fixed at
the source, only made rarer:

| date | change | what it left |
|---|---|---|
| 08-31 | connect TLS before building the payload; stream history rows | TLS still in internal RAM |
| 09-02 | PSRAM enabled (`PSRAM=opi`) | large JSON/String allocations moved out; mbedTLS still internal |
| 10-14 | real error code in `/cloud`, self-restart after 30 min without an upload | detection and recovery only |

The core's sdkconfig (esp32s3-libs 3.3.11) sets `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`, so every
mbedTLS allocation is `heap_caps_calloc(MALLOC_CAP_INTERNAL)` while 8 MB of PSRAM sit unused.

## Measured before the flash (`msv4-display-hold-v1`, 11.6 h uptime)

- `/cloud` `upload.lastTlsFailure`: code -32512, **104 failures this boot**, last one with the
  largest internal block at 31,732 and 98.8 KB free.
- `/device`: `resetReason` `panic/exception`, `previousBoot.resetReason` `panic/exception`
  (two unintentional panics running), `minFreeHeapEverBytes` **7,680**.
- 18 samples at 15 s (08:27-08:32 UTC): largest internal block 31,732 on most, down to 22,516,
  14,324 and **7,668**; free heap 16,360-96,232.

## Change

`installTlsPsramAllocator()`, first thing in `setup()`, calls
`mbedtls_platform_set_calloc_free()` with a calloc that prefers PSRAM and falls back to internal
RAM (`heap_caps_calloc_prefer`), and `heap_caps_free`. This is the runtime equivalent of ESP-IDF's
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`. It is available because the core's `esp_config.h` defines
`MBEDTLS_PLATFORM_MEMORY` with `MBEDTLS_PLATFORM_STD_CALLOC` (not the `_MACRO` form that would
remove the setter); `mbedtls_platform_set_calloc_free` is a `T` symbol in `libmbedcrypto.a`. The
core's own free is `heap_caps_free`, so anything allocated before the swap frees correctly.

`cloudHeapHasContiguousRoomForTls()` now checks the PSRAM largest block when the swap succeeded.
`/device` `health.tlsAllocator` reads `psram` or `internal`.

Not tried before in this repo (no `set_calloc_free` in any commit before 8493af7).

## Measured after the flash

- `/device`: build `... tls-psram-v1`, `tlsAllocator` `psram`.
- First upload HTTP 200 at 28 s uptime.
- First 5 minutes (one sample a minute): every upload 200, **0 TLS failures**, free heap low-water
  **51,140** (was 7,680), largest internal block 31,732-51,188.

## Still open

- Five minutes is not a verdict. The test is days: TLS failures stay at 0 in `/cloud`
  `upload.lastTlsFailure.countThisBoot`, and no `panic/exception` reset.
- The panics are not proven to be heap exhaustion. That is the inference: they come with a
  free-heap low-water near zero, and the 09-02 investigation ruled out stack overflow
  ([RECEIVER_PANIC_STACK_RULED_OUT_2026-09-02.md](RECEIVER_PANIC_STACK_RULED_OUT_2026-09-02.md)).
- The largest internal block returns to 31,732 within a minute of boot, as it did before. That
  ceiling is structural (whatever owns internal RAM), not TLS; it no longer limits a connect.
- If TLS in PSRAM causes a problem, revert 8493af7; nothing stored on the receiver changed.
