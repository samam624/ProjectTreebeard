# Moisture display centered-filter fix

**Date:** 2026-08-24  
**Live build:** `MSV2 runtime build 2026-08-24 centered-moisture-filter-v1`

## Symptom

On the Alocasia GP History chart, the blue displayed line repeatedly fell below the center
of the orange raw cloud, stayed there, and later jumped upward. Adding two-packet watering
confirmation on 2026-08-23 reduced single-outlier snaps but did not eliminate this cycle.

## Live evidence

Alocasia GP is receiver sensor 3. A read-only query of its stored 2026-08-24 history showed:

- During 09:00-13:59 UTC, hourly raw medians remained about 898-903.
- The stored presentation value fell from 893 to 882 and then 879.
- A five-minute slice around 13:14-13:19 UTC contained ordinary raw readings centered near
  896-900, with both high and low excursions, while the display stayed at 879.
- The exact downward transitions included `902 -> 893`, `892 -> 882`, and `882 -> 879`.
- The large event near 19:37 UTC was also present in the raw signal: it briefly reached 1,481 and
  then established a new center above roughly 1,080. That event is not created by chart smoothing
  and the new filter intentionally does not hide a sustained physical/raw transition.

This was a filter error, not a chart drawing error. The browser's retroactive filter and the
receiver's stored presentation filter both implemented the same asymmetric algorithm.

## Root cause

The old filter was a drying ratchet:

- Low readings could move the presentation downward after clearing a deadband or after the
  dry-side stalled-wake escape fired.
- Ordinary high readings were ignored completely unless they crossed the watering threshold.
- The 2026-08-23 fix preserved low-side evidence across high-side jitter, making the directional
  bias even more deterministic.

With stationary two-sided noise, accepting one direction and rejecting the other must drift
toward the accepted envelope. Once the blue line was sufficiently low, ordinary readings near
the real center eventually looked like a sustained watering-sized rise and snapped it upward.
Two-packet watering confirmation could reject one spike, but it could not correct that baseline
bias.

## Replacement algorithm

Both `smoothMoistureForDisplay()` in the receiver and `smoothMoistureSeries()` in the embedded
History page now use the same structure:

1. Keep the latest five unique measurements.
2. Use their median as the robust signal center. A five-value median rejects as many as two
   isolated spikes.
3. Calculate median absolute deviation for the noise contribution to the watering threshold.
4. Track the median in both directions with a symmetric 1/8 EMA.
5. Keep the accumulator in Q8 fixed point so small positive and negative corrections have equal
   representation instead of being lost to integer rounding.
6. Cap normal movement to 5% of the learned/presentation span per packet.
7. Treat a sufficiently large upward median shift as watering only after two consecutive robust
   centers clear the threshold, then snap to the median rather than the newest raw peak.

With the default five-packet window, a stable watering step is recognized on the fourth high
packet: three high packets move the median, and the fourth supplies the second robust-center
confirmation. Until the five-packet window fills after boot or a packet-format change, the first
reading is held.

## History UI changes

The on-device History page still recomputes its blue line from stored unsmoothed `moisture` rows,
so this fix applies retroactively to already-recorded history after refreshing the page.

The old deadband and dry/wet stall controls were removed. The moisture controls are now:

- robust-center window (odd packets), default 5;
- symmetric moisture EMA alpha, default 0.125;
- watering threshold as noise multiplier and percentage of span;
- maximum symmetric tracking step;
- watering confirmation count.

The browser settings key advanced from `msv2.historySmoothingParams.v1` to `.v2`, preventing old
ratchet-specific settings from leaking into the centered filter.

## Regression coverage

`scripts/verify_receiver_moisture_filter.mjs` now checks:

- the embedded page remains valid JavaScript;
- firmware contains the five-packet centered filter and signed symmetric update;
- obsolete dry-ratchet state is absent;
- an Alocasia-derived stationary-noise fixture remains centered instead of falling toward its
  low envelope;
- alternating symmetric noise is unbiased;
- two isolated high spikes cannot trigger watering;
- a sustained watering step snaps on the fourth high packet;
- a sustained dry-down converges smoothly.

## Verification and deployment

- Filter verifier: pass.
- Browser/firmware battery-curve parity verifier: pass.
- ESP32-S3 compile: pass, 2,202,076 bytes flash (70%) and 77,252 bytes RAM (23%).
- Uploaded to COM16 with `EraseFlash=none`; application hash verified.
- Live `http://10.0.0.35/device` confirmed the new build tag, normal BLE scanning, cloud enabled,
  zero queued-history drops, and zero consecutive upload failures.

The live blue presentation will start with fresh receiver state after the flash. The History chart
will recompute the whole selected range with the new algorithm immediately after a normal refresh.
