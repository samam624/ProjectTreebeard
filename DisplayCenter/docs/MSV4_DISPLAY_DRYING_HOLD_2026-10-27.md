# MSV4 display drying hold (2026-10-27)

**Live build:** `Treebeard runtime build 2026-10-28 msv4-display-hold-v1`. It was flashed on `COM16`
at about 23:25 PDT on 2026-10-27; the tag's date is the next day's.

## What changed

For MSV4 probes only (packets with the MSV4 flag), the presented moisture no longer rises between
waterings. Three things still raise it:
- a confirmed watering snap, which is unchanged;
- the filter's tracked value staying above the held value for 36 h (`MOISTURE_HOLD_RELEASE_MS`);
- the filter resetting, at boot or on a packet-format change.

The measured reading, stored in the `moisture` history column, is untouched. So are every other
generation's display and the learned dry/wet bounds, which are built from the measured reading.
The card's percentage and WATER NOW use the presented value, so they now follow the held line.

The same hold is in:
- `smoothMoistureForDisplay()` in the receiver;
- `smoothMoistureSeries()` on the History page (`MobileAppPage.h`), applied per point via
  `isMsv4Row()`.

`DisplayCenter/scripts/verify_receiver_moisture_filter.mjs` tests it with:
- a daily midday-overshoot-and-rebound series;
- stationary noise with the hold on;
- a watering step;
- a 48 h insertion-settling rise.

## Why

On probe 10, from 10-23, the 2 %/C tip correction overshoots at midday and rebounds 3.5-19 g of
water each evening. On those days the soil's raw resistance did not follow the tip temperature,
while on 10-17 it did and the same correction was exact. No coefficient or delayed temperature
fixed both cycles. The full record is `MSV4/docs/MSV4_SENSOR10_TEMPCO_CHECK_2026-10-27.md`.

## Why this is not the 2026-08-22 drying ratchet

That ratchet (`MOISTURE_CENTERED_FILTER_FIX_2026-08-24.md`) failed in two ways:
- it accepted raw low packets and ignored high ones, so stationary noise walked it down;
- it then read the gap as a watering and snapped up.

Here:
- **Where the hold sits:** on the output of the symmetric tracker, which is the 5-packet median
  followed by the 1/8 EMA. It can sink no lower than that tracker's own lower edge.
- **Where the watering test looks:** at the tracker, never at the held value.

With the hold on, the verifier's Alocasia-GP noise pattern stays at or above 890 and never snaps.
The old ratchet reached 879 on that pattern.

## Replay on probe 10's history (measured)

The History page's copy of the filter was replayed over probe 10's 29,186 packets from 10-08 to
10-27. The span was 5,330 counts, from the receiver's learned bounds 15,127/20,457.

| | without the hold | with the hold |
|---|---|---|
| matches the receiver's stored display (from 10-12) | median 0, 99th percentile 1 count | |
| evening rebound per day | 1.2-19.5 g | 0.0 g every day |
| miss against the scale, cycle 1 / cycle 2 | 4.9 / 7.8 g | 4.4 / 6.5 g |
| watering 10-10 19:14 | 14,137 -> 21,584 | 14,112 -> 21,584 |
| watering 10-19 00:36 | 15,860 -> 21,373 | 15,858 -> 21,373 |

**How far below the unheld line the held one reads:**

| | median | 90th percentile | max |
|---|---|---|---|
| cycle 1 | 0.2 g | 1.7 g | 4.4 g |
| cycle 2 | 0.5 g | 6.5 g | 19.5 g |

The 19.5 g is on the 10-23 evening, until the next midday drop catches up.

## Limits

- **A rise below the watering threshold is hidden for up to 36 h.** Examples are a small top-up
  or rain on an outdoor MSV4. For probe 10 the threshold is 12 % of its span, 640 counts, about
  43 g.
- **The hold restarts from the current reading** after a receiver reboot.
