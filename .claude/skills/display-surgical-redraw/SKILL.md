---
name: display-surgical-redraw
description: Core rendering rule for the MSV2 ESP32 display firmware (DisplayCenter/arduino/MSV2_ESP32_Display_Runtime) - any new or changed display feature's steady-state/periodic redraw must only repaint the pixels that actually changed, never a whole card or the whole screen. Load this before writing or reviewing any drawPlantCard*/render*/draw* function, before adding a new touchscreen UI element (modal, card, overlay, expanded view), or when a redraw is causing visible flicker.
---

# Display: only touch the pixels that changed

## The rule

Once a display feature is on screen and just updating over time (new sensor data, a blink
tick, a periodic refresh, a value ticking up), its redraw path must only repaint the specific
regions whose content actually changed since the last draw - never re-paint a whole card's
background, never re-paint the whole screen, as the routine/steady-state behavior. A full
repaint is only acceptable as a one-time event: the feature's first paint (e.g. a card just
got expanded, a modal just opened, layout just changed).

This is a hard rule for this project, not a nice-to-have.

## Why: this isn't just about flicker

On other displays, over-repainting is merely wasteful. On this hardware it's a **correctness
risk with a documented incident**. See `DisplayCenter/docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`
(status: RESOLVED, but read as a cautionary tale, not ancient history):

- The ST7796 display and the XPT2046 touch controller share one SPI bus (`DisplayCenter/docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`, "Shared SPI topology"). Touch polling happening at the same time as display writes was the root cause of real, user-visible corruption (stray red pixels, torn frames, one card briefly showing another card's sprite).
- More writes = more SPI transactions = more opportunities for that kind of glitch, plus more idle-touch-poll interleaving windows. Minimizing writes isn't just about looking clean; it's shrinking the actual attack surface for that bug class recurring.
- Separately, a full-panel repaint on every touch sample is *exactly* the bug that made the brightness slider look frozen/flashing before it was fixed (see `drawBrightnessSliderRow()`'s history) - the display/touch bus is slow enough that unnecessary large writes are visible to the user as flicker, not just theoretically wasteful.

## The established pattern: full draw vs. data-only draw

Every existing piece of dashboard UI in this firmware already follows a two-function split.
Match it for anything new:

1. **A full-draw function** - paints everything: background fill, border, all rows/labels,
   unconditionally. Called exactly once per "this thing just appeared or its layout changed"
   event (a fresh `renderUi()` full rebuild, a modal just opening, a card just got expanded).
2. **A data-only function** - paints nothing unconditionally except genuinely cheap,
   non-flickering strokes (a 1px rounded-rect border re-stroke is fine even if the color didn't
   change - it's invisible when the color is identical). Everything else - any `fillRect` clear
   followed by text/bar redraw - is gated behind an explicit "did this specific value change
   since last time" check, using state cached on the relevant struct (`PlantArtSlot`, or similar
   for a new UI element).

Existing pairs to copy the shape of:

- `drawPlantCard()` (full) / `drawPlantCardDataOnly()` (data-only) - the reference
  implementation. Read `drawPlantCardDataOnly()`'s moist/battery-row logic
  (`moistureValueChanged`/`moistureColorChanged`/`batteryValueChanged`) for the exact idiom:
  cache the last-drawn value on the struct (`slot.lastMoistureRaw`, `slot.lastBatteryPct`, ...),
  compare against the freshly computed value, only `fillRect` + redraw if it actually differs.
  The code comments there explain *why* even the clear is conditional (color-only blink ticks
  skip it entirely to avoid a visible flash).
- `drawBrightnessOverlay()` (full modal paint, called once on open) /
  `drawBrightnessSliderRow()` (redraws only the value text + track + fill + knob, called on
  every drag sample) - this split exists *because* the naive full-panel-per-sample version
  visibly flashed during a drag. See the comment above `drawBrightnessSliderRow()`.
- `drawPlantCardDetail()` (full) / `drawPlantCardDetailDataOnly()` (data-only) - the
  tap-to-expand diagnostics view (2026-08-12). Worked example of applying this rule to a new
  feature: the first cut of this feature only had the full-draw function and called it on
  every periodic refresh (every ~2s while expanded, every blink tick) - a full
  `fillRoundRect` + full info-band `fillRect` + every row redrawn, every single tick, even
  when nothing had changed. That's the anti-pattern this skill exists to catch. The fix added
  `drawPlantCardDetailDataOnly()`, which strokes only the border unconditionally (cheap,
  matches `drawPlantCardDataOnly()`'s own precedent) and diffs every other row (signal, RSSI,
  age, wake, loss, received, missed, moisture range, raw oscillator) against cached
  last-drawn text on the slot, via a small `drawDetailStatRowIfChanged()` helper that only
  clears+redraws a row's own rect when its formatted text actually differs.

## Checklist for a new display feature

1. Identify the region this feature owns on screen (a card's `x/y/w/h`, a modal's panel rect,
   etc.) - never assume "the whole screen" unless the feature genuinely is full-screen (a setup
   wizard screen, a WiFi-connecting screen - those already redraw fully once per screen and
   don't have a periodic-refresh path, so this rule doesn't apply to them).
2. Write the full-draw function first, unconditional, to get the visuals right. Wire it to run
   exactly once per genuine "this appeared/changed layout" event.
3. Before wiring up *any* periodic/steady-state refresh path (a `loop()` render-gate tick, a
   blink cadence, a data-change callback), write the data-only counterpart:
   - Cache whatever values the redraw depends on somewhere that survives between calls (a
     struct field, mirroring `PlantArtSlot`'s `lastMoisture*`/`metricsInitialized` fields, or a
     new field if this is a new kind of UI element).
   - For each independently-updatable piece of content (a row, a label, a bar), compute the
     new value, compare to cache, and only `fillRect`-clear + redraw *that piece's own
     rectangle* when it differs. Use the smallest rect that covers the content, not the row's
     full available height/width if the text is short.
   - Update the cache after drawing.
   - A border/outline re-stroke without a preceding fill can stay unconditional if it's cheap
     and paints the same pixels either way (no visible cost) - don't over-engineer diffing for
     genuinely free operations, only for anything that would otherwise flicker or fill a region.
4. Point every periodic caller (render-gate in `loop()`, blink-only redraw passes, etc.) at the
   data-only function, never the full-draw function, once the feature is already on screen.
5. Before flashing, sanity-check by reading the new data-only function next to its full-draw
   counterpart: could any branch in the data-only path end up calling `fillRoundRect`/a
   whole-region `fillRect` unconditionally on a normal refresh tick? If yes, that's the bug this
   skill exists to catch - fix it before it reaches hardware.
