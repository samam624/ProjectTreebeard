# Dashboard showed 100 % on every card (2026-11-10)

The Netlify dashboard (`msv2-plants.netlify.app`) disagreed with the receiver's display: every
region-calibrated card read 100 %. The site itself was current (bundle `app.v18.js`, identical to
the repo). The data under it was not.

## Cause (measured)

The dashboard reads the receiver's stored latest snapshot (`msv2_latest_snapshots`). It scales a
card with `plants[].autoCal` (the range the display pulls from the cloud) and falls back to the
card's preset `dry 120 / wet 175` when that is missing. Every reading (13,000-44,000) is far above
175, so a card without `autoCal` clamps to 100 %.

The receiver sends a full snapshot (with `autoCal` and the packet counters) every 10 minutes and a
compact history-only upload in between. `mergeLatestPlants()` replaced each stored plant entry
WHOLE with the compact one, which has no `autoCal`. So for most of every 10 minutes the stored
snapshot had none. Read at 08:21:40 UTC: no `autoCal`, no `received` on any plant.

## Fixes (receiver-snapshot, both deployed 11-10)

1. **2bcb8c8**: `mergeLatestPlants()` merges a compact entry over the stored one, so fields only a
   full snapshot sends survive between full snapshots.
2. **2778835**: the autocal pull the display makes every 10 minutes also writes the rows it
   serves onto the stored snapshot's plants (`attachAutoCal()`), so the dashboard gets the
   display's range even if the receiver's 18 KB full snapshot does not get through. Best effort;
   the display's answer never waits on it.

Verified 08:28:15 UTC: stored snapshot carries `autoCal` for every sensor (sensor 10: 15,127 to
20,457, region) and the packet counters, from a full snapshot.

Correction recorded: the claim made during the session that full snapshots were not arriving
(from ~9 minutes of data, shorter than the 10-minute cycle) was wrong; one arrived 16 minutes
after the first deploy. Fix 2 still stands as insurance against the receiver's TLS failures
([RECEIVER_TLS_PSRAM_2026-11-10.md](../../DisplayCenter/docs/RECEIVER_TLS_PSRAM_2026-11-10.md)).

Tests: `history-batch.test.ts` 13/13, all receiver-snapshot suites 62/62.
