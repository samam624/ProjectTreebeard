# Bounded History Downloads for Dense Probes

**Date:** 2026-08-29  
**Receiver build:** `MSV2 runtime build 2026-08-29 bounded-history-v1`

## Problem

Alocasia GP accumulated more than 100,000 eligible rows in a seven-day History window. The
old API exhaustively paged all of them through Supabase's 1,000-row PostgREST response cap,
assembled up to 20,000 rows per edge-function response, and made the browser request more
batches until the range was empty. That meant roughly 100 database RPC calls and multiple
large JSON downloads for a chart only 800 pixels wide. The load could outlive either the edge
function or browser/network timeout and fail without rendering the range.

The History JavaScript is embedded in `MobileAppPage.h` and served by the ESP32, but its history
request goes directly from the viewing browser to the Supabase `plant-dashboard` edge function.
The bulk data is not buffered through the ESP32's HTTP client.

## Fix

Migration `036_msv2_bounded_share_history.sql` adds two service-role-only RPCs using the same
share-token and household authorization as the existing history reader:

1. `msv2_read_share_sensor_history_density` counts eligible rows and captures the newest row ID
   and active moisture scale before any history payload is read.
2. The edge function computes `sample_every = ceil(total_rows / 2400)`.
3. `msv2_read_share_sensor_history_sampled` ranks the frozen range newest-first and returns rows
   whose zero-based rank is divisible by that stride. Ranking happens before cursor filtering so
   the sample phase stays stable across Supabase's internal 1,000-row pages.

Windows containing 2,400 rows or fewer remain lossless. Larger windows retain the newest row and
are evenly thinned across the full selected range. For example, 100,001 rows use a stride of 42
and return 2,381 real readings in one browser response. No stored history is deleted or rewritten.

The History notice reports both numbers, for example:

`2,381 plotted from 100,001 readings (sampled every 42)`

The raw and smoothed chart lines are computed from the retained real readings. Therefore a
packet-count-based smoothing control is an approximation when a dense range is sampled; select a
shorter range if packet-for-packet filter diagnostics are required.

## Deployment and verification

Apply migration 036 before deploying the updated `plant-dashboard` function. Then compile/flash
the DisplayCenter build so its embedded page requests the matching 2,400-point budget and shows
the sampling disclosure.

Run:

```powershell
node cloud\scripts\verify_supabase_migrations.mjs
node cloud\scripts\verify_history_downsampling.mjs
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 compile
```
