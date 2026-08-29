# Moisture Presentation: Watering-Line Model (2026-08-23)

## Outcome

Replaced the old "0% is the only alert state" moisture bar/color model with a three-tier
watering-line model, shared byte-for-byte across all three moisture-presentation surfaces:
the physical display, the receiver's local web page, and the Netlify cloud dashboard.

| Surface | File(s) |
| --- | --- |
| Physical display (color, bar, buzzer/blink) | `arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino` |
| Receiver's local page (Status cards) | `arduino/MSV2_ESP32_Display_Runtime/MobileAppPage.h` |
| Netlify cloud dashboard | `web/plant-dashboard/assets/app.v15.js`, `web/plant-dashboard/index.html` |

Cross-surface agreement is enforced by `scripts/verify_moisture_alert_thresholds.mjs`, which
now checks both threshold constants and the bar-layout constant match across all three files,
rather than pinning any of them to a hardcoded number in the test itself.

## The model

**0-100 is unchanged from how it has always worked**: 0 is the driest this sensor has ever
recorded (the learned/manual `dry` bound), 100 is the wettest (`wet` bound). The only real
change is that the percent calculation is **no longer clamped at the bottom** - a reading
drier than anything on record now counts down as a negative number instead of silently
pinning at 0 forever. It stays clamped at 100 on top; "more than fully watered" isn't
meaningful.

- `MOISTURE_WATER_LINE_PERCENT` / `_PCT` = **0** - the inflection point. Literally the dry
  bound itself, not a padded-above-dry early-warning cue. 0-100 is normal; crossing below 0
  means "drier than ever observed."
- `MOISTURE_DANGER_PERCENT` / `_PCT` = **-10** - red, and the physical display actually
  blinks and buzzes (`slotNeedsBlink()`/`serviceWaterAlert()`) at or below this.
- Between 0 (exclusive) and -10: a quiet yellow warning, static (no flashing).
- `MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT` / `_PCT` = **30** - a **display-layout** constant,
  not an alert threshold. A bar can't render negative width, so the unchanged 0-100 range is
  squeezed into the right 70% of the bar's width; the left 30% is a fixed, finite strip that
  renders the open-ended negative range, compressing it until the bar goes fully empty ("goes
  off the end of the bar and can't be displayed anymore"). The dotted marker sits at this
  30%-from-left position, which is exactly where percent=0 lands. **The displayed number is
  never clamped this way** - it keeps counting down in text even once the bar itself has
  bottomed out visually.
- Both web surfaces flash the danger state with a CSS animation (`moisture-flash`, 1s
  step-end) matching the physical display's `applyLowMoistureBlink()` cadence exactly
  (500ms/500ms, full color to muted) - "overdue" needed to read as visually distinct from
  "just yellow" on every surface, not only the hardware.

## A real bug found and corrected mid-session

The first implementation anchored the displayed percent's zero point at the watering *line*
(what was, in an earlier iteration of this feature, a marker at the old 0-100 scale's 30%
position) rather than at the dry bound. That silently rescaled the whole positive range, so a
plant sitting comfortably above its historical driest point - but below that old 30% marker -
showed a negative percent it shouldn't have. Caught live: two Alocasia probes, both watered
within the past week and both above their recorded dry minimum, displayed -11% and -37%.

The fix re-anchored 0 to the literal dry bound (unchanged from the original, pre-this-session
scale) and introduced `MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT` as a **separate** display-layout
concern from the alert-threshold constants - the bar's finite width and the number's
open-ended range are no longer the same formula.

## Implementation notes

- `moisturePercent()`'s return type changed from `uint8_t` to `int16_t` since it can now be
  negative. This is the single most consequential type change in the diff: every consumer
  had to be traced by hand, since the compiler catches a type mismatch but not a silent
  unsigned-wraparound bug if any `uint8_t moisturePct` local had been left in place (a
  negative value assigned into one would have wrapped to a large positive number instead of
  failing loudly). Consumers fixed: `drawBar()`'s `percent` parameter, `moistureHealthColor()`'s
  `percent` parameter, `slotNeedsBlink()`'s local, and two cached struct fields used for
  redraw-diffing (`PlantDetailStats.moisturePct`, `PlantArtSlot.lastMoisturePct`).
- `drawBar()` now computes the bar's fill width via
  `clamp(0, 100, MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT + percent * (100 - MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT) / 100)`,
  mirrored exactly in both web clients' `barPercent()` (`MobileAppPage.h`) / `barPercent()`
  (`app.v15.js`) functions.
- The dotted marker (drawn as alternating pixels on the physical display - TFT_eSPI has no
  dashed-line primitive; a `border-left: dotted` div on the web) sits at a fixed
  `MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT` position regardless of the current reading, since it
  marks the boundary itself, not a data-dependent value.
- Every current caller of the physical display's `drawBar()` draws a moisture bar - there is
  no battery bar on this display (battery is a text/color indicator only, see
  `drawCardHeader()`) - so the marker and bar-layout math couple directly to the moisture
  constants rather than taking them as parameters that would only ever be called with one
  value.
- Loaded the `display-surgical-redraw` skill before touching `drawBar()`: confirmed it's
  already called only from the existing `moistureValueChanged || moistureColorChanged` gate
  in `drawPlantCardDataOnly()`, so adding the marker/new width math introduces no new
  unconditional repaint.

## Verification

- `scripts/verify_moisture_alert_thresholds.mjs`: rewritten to extract
  `MOISTURE_WATER_LINE_PERCENT`/`_PCT`, `MOISTURE_DANGER_PERCENT`/`_PCT`, and
  `MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT`/`_PCT` from all three surfaces and assert numeric
  equality (rather than hardcoding the numbers in the test), asserts the danger threshold is
  strictly below the water line, asserts `moisturePercent()`/`pct()`/`percent()` are not
  bottom-clamped (regex-checks for the absence of a `clampInt(x, 0, 100)` / `Math.max(0, ...)`
  lower bound), and asserts both web clients define the flash animation. Passing.
- `scripts/verify_receiver_moisture_filter.mjs`, `scripts/verify_battery_curve_sync.mjs`,
  `scripts/verify_supabase_migrations.mjs`, `scripts/verify_receiver_cloud_recovery.mjs`,
  `scripts/verify_plant_dashboard_qr.mjs`: all still passing, unaffected.
- Firmware compiled clean at each step; final build `2026-08-23 watering-line-model-v3`
  flashed to the physical receiver (COM16 / `10.0.0.35`) and confirmed live via `/device`.

## Not yet done

- **Netlify has not been deployed.** The `web/plant-dashboard` changes (`app.v15.js`,
  `index.html`) are correct in the working tree but not pushed live, consistent with this
  project's standing Netlify-deploy hold from the 2026-08-17 stale-service-worker-cache
  incident (see `docs/STATUS_HANDOFF.md`).
- No live confirmation yet that a probe genuinely below its historical dry bound renders
  correctly on the physical display end-to-end (color, blink, buzzer, bar) - the receiver had
  just rebooted at verification time and none of the four probes had reported a
  currently-negative reading. The logic is verified by construction (compiled, cross-checked
  by the verify script, and the positive-range regression was caught and fixed against real
  data) but not yet observed live in the negative range specifically.
