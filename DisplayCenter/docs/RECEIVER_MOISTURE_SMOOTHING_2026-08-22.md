# Receiver Moisture Presentation Smoothing

Date: 2026-08-22

> **Superseded on 2026-08-24:** the asymmetric drying ratchet documented below was proven to
> bias stationary Alocasia GP noise toward its lower envelope. It has been replaced by a
> five-packet robust center plus symmetric EMA. See
> `docs/MOISTURE_CENTERED_FILTER_FIX_2026-08-24.md`. The raw/presentation data separation and
> storage contract described here remain current.

## Outcome

Moisture smoothing now happens once, on the ESP32 display receiver, after every supported
probe packet has been converted to the receiver's normal increasing-with-moisture scale.
No probe firmware changes are required. Legacy packets, MSV2 raw-kHz packets, and MSV3
wide FDC-count packets therefore receive identical presentation behavior.

The design intentionally separates a user-friendly display from measurement truth:

| Value | Meaning | Consumers |
| --- | --- | --- |
| `moisture` | Receiver-smoothed presentation value | Physical display, local Status page, cloud latest dashboard |
| `measuredMoisture` | Latest unsmoothed receiver-derived value | Local diagnostics and cloud ingestion |
| history `moisture` | Unsmoothed receiver-derived history | Auto-calibration and later analysis |
| history `display_moisture` | The value actually presented at that time | User-facing History moisture chart |
| `raw.moistureKHz` / `raw.fdcCounts` | Probe wire-format value before receiver conversion | Local diagnostics, cloud latest snapshot, durable Supabase raw history |

`measuredMoisture` is deliberately called measured rather than raw: for MSV2 it has
already passed through the per-sensor inversion ceiling, and for MSV3 the FDC count has
also been mapped. The truly raw wire value remains in the local/cloud `raw` object and,
from migration 029 onward, in history columns `raw_moisture_khz` or `raw_fdc_counts`.

Rows recorded before migration 028 have no `display_moisture`; clients fall back to their
existing `moisture` value. Receivers running older firmware do not send
`measuredMoisture`; cloud ingestion likewise falls back to their `moisture` value. This
makes database, edge-function, receiver, and web deployments safe to roll independently.

## MSV3 high-resolution mapping

The initial MSV3 receiver mapping divided the absolute FDC count by 1,000 before
inversion. A measured 20,000,000 dry to 19,000,000 wet swing therefore retained exactly
1,000 derived steps. That was usable, but unnecessarily spent most of the existing
`uint16_t` presentation range on headroom far outside the observed sensor window.

Scale version 2 uses an affine/windowed mapping instead:

```text
derived moisture = clamp_u16((24,000,000 - raw FDC counts) / 100)
```

- 20,000,000 dry -> 40,000.
- 19,000,000 wet -> 50,000.
- The measured 1,000,000-count signal span -> 10,000 derived steps.
- A roughly 3,000-count bench noise movement -> roughly 30 derived steps, which remains
  visible to diagnostics but is removed from presentation by the adaptive filter.
- The unclamped input window is 24,000,000 down to 17,446,500, comfortably covering the
  observed roughly 18-22M FDC range.

Every history row carries `moisture_scale`. Existing rows are scale 1; high-resolution
MSV3 rows are scale 2. On a scale transition, the history insert RPC advances that
sensor's calibration reset epoch and clears its old anchors. History readers return only
the latest compatible scale, so the numeric jump cannot be mistaken for watering or
flatten a user's chart. Older rows remain stored rather than being deleted.

## Filter behavior

The filter is a display-only drying ratchet, not a scientific estimator.

1. The first packet is displayed immediately.
2. The second packet trains the initial packet-to-packet noise estimate and leaves the
   displayed value unchanged.
3. Ordinary upward movement is treated as noise and ignored, so a drying curve does not
   visibly bounce upward.
4. A lower measurement must clear an adaptive deadband before the displayed value moves.
   The receiver follows half the excess distance, capped to 5% of the estimated useful
   span per packet. This gives a clean, monotonic, non-abrupt dry-down.
5. A sufficiently large upward rise is treated as watering, but only once it persists
   for two consecutive unique packets. The first packet that clears the threshold only
   arms a pending flag and leaves the displayed value unchanged; if the very next packet
   also clears the threshold, the display snaps immediately to it (not averaged into a
   misleading slow climb). If the next packet instead falls back below the threshold, the
   candidate is discarded as a one-off noisy outlier and that packet is evaluated normally
   instead. (Added 2026-08-23 after a spiky sensor's single-packet noise excursions were
   observed snapping the display on their own - see
   docs/HISTORY_SMOOTHING_WATERING_DEBOUNCE_2026-08-23.md.)

The signal span is the learned auto-calibration range when ready. Before learning is
ready, it is `max(20, measured / 5)`, which keeps the same logic usable across the small
legacy values and MSV3's larger FDC-derived values.

- Drying deadband: `max(2, 1% of span, 3 x learned packet noise)`.
- Maximum downward step: `max(1, 5% of span)` per unique probe packet.
- Watering rise: `max(4, 12% of span, 2% of displayed value, 4 x learned packet noise)`.
- Noise estimate: 1/8 EWMA update from ordinary packet deltas; large transitions do not
  inflate it.

The state lives in RAM. A receiver restart starts a new two-packet training period. If a
watering happens immediately after boot, it will be accepted on the third packet after a
stable repeat; this is the deliberate tradeoff against declaring startup noise a watering.
A packet-format change under an existing sensor ID resets the filter so incompatible
numeric scales are never blended.

## Production deployment

Completed on 2026-08-22:

- Applied `028_msv2_display_moisture_history.sql` to the linked Supabase project.
- Applied `029_msv3_high_resolution_raw_history.sql`; full raw FDC/kHz history and scale
  transition isolation are active.
- Deployed the updated `receiver-snapshot` Edge Function.
- Deployed Netlify dashboard `app.v15.js` with service-worker cache
  `msv2-cloud-app-v15`.
- Compiled and flashed the ESP32 display receiver on COM16. Flash verification passed.
- Confirmed `/device` reports
  `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2`.
- Confirmed `/data` exposes both fields and observed the filter active live (one packet
  reported display `943` while its unsmoothed measurement was `946`).
- Confirmed the receiver's post-flash cloud upload and display sync both returned HTTP 200.
- High-resolution follow-up live proof: sensor 4 raw FDC `20,243,185` produced measured
  `37,568` while presentation held at `37,561`; the next cloud upload returned HTTP 200,
  and a refreshed auto-cal response moved its bounds from the old ~4,760 scale to
  `37,574`, proving the scale-2 row/reset path completed in production.

## Verification commands

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 -Action compile
node --test supabase/functions/receiver-snapshot/autocal.test.ts
node --check supabase/functions/receiver-snapshot/index.ts
node --check web/plant-dashboard/assets/app.v15.js
supabase.cmd migration list --linked
```

The firmware compiled at 2,133,296 bytes (67% flash) and 58,508 bytes (17% RAM). All nine
auto-calibration regression tests pass. A deterministic presentation-filter simulation
also verified a flat output under stationary noise, a strictly non-increasing dry-down,
and an immediate watering snap.
