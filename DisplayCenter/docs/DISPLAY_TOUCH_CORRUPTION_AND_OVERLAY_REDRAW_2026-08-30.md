# Touch/display SPI corruption and brightness overlay redraw bugs (2026-08-30)

Status: **Both threads resolved and field-confirmed by the user, 2026-08-30.** This closes out
`DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`, whose own status line ("RESOLVED,
user-confirmed 2026-08-12") was wrong - see that doc's own now-added correction note. Kept here
as the detailed record; that doc still has the original hardware/library-level investigation
notes and should be read first for background on the shared-SPI-bus context.

All code changes are in
`DisplayCenter/arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino`. Two
independent phone-a-friend (Codex) consultations under consultation_id
`display-touch-corruption-2026-08-30` are cited inline; both directly changed the outcome and
are worth reading in full if this area breaks again.

## Thread 1: sprite-art corruption during active touch gestures

### Symptom

Reported via two photos: a card's diagnostic overlay stuck showing a stale "Age: Ns" stat, and
separately, cards *other than* the one being touched showing garbled/checkerboard corruption in
their sprite-art region, self-healing gradually ("fills in slowly over time... fully swaps over
around 15 seconds later").

### Why the 2026-08-12 fix didn't hold

That session's fix (`drawPlantSpriteComposited()` - full per-row composited redraw instead of
sparse per-pixel writes, plus a T_IRQ idle-gate to stop idle touch polling) closed the *idle*
polling corruption source. It said nothing about touch SPI traffic during an *active* gesture,
which the 08-12 doc explicitly flagged as untested: "An actual touch still performs SPI
transactions, so corruption during a press would confirm [a] hardware repair is needed" - never
followed up on. This session's corruption was exactly that untested case.

### Diagnosis: isolation test

Per the 08-12 doc's own "Not yet tried" list (never executed until now), added a diagnostic
build (`TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL`) that made `handleTouchInput()` return before any
`touchScreen.touched()`/`getPoint()` call - zero XPT2046 SPI transactions, verified via a
`touchSpiTransactionCount` counter exposed on `/device`. User ran it for an extended period,
physically tapping/holding as normal (though inert): **zero card-art corruption**, confirming
touch SPI traffic during active gestures - not idle polling, not redraw scheduling - as the
trigger.

### Fix 1: IRQ-primary touch state machine

`handleTouchInput()` was rewritten to treat the T_IRQ GPIO pin (zero SPI cost) as the primary
down/up signal, spending a real SPI transaction (`touchScreen.getPoint()`) only when coordinates
are actually needed:

- **Idle**: watch T_IRQ only.
- **Press confirm** (`touchPendingConfirm`): one poll interval (32ms) after T_IRQ first reads
  LOW, spend the gesture's one SPI read on a settled sample.
- **Held**: pure timestamp check for long-press (550ms); no further SPI unless actively
  dragging the brightness slider.
- **Release**: pure GPIO+timestamp debounce (`TOUCH_RELEASE_STABLE_MS` = 160ms of continuous
  T_IRQ-high), no SPI at all.

Net effect: a plain tap/long-press now costs **1** SPI transaction total, down from ~5-20+ under
the prior release-stability-polling design. Field result: corruption dropped from "most
gestures" to roughly 1/10 taps.

### Fix 2: bidirectional bus-turnaround guard

Codex's read on the 1/10 residual: the one remaining transaction could still land adjacent to
an *unrelated* display write (a card's own ~500ms animation-frame redraw, running independently
of touch). Added:

- `touchAcquisitionPending` - set the instant T_IRQ first reads LOW, checked by `loop()`'s three
  periodic display-writing call sites (`renderDashboardDataOnly`/`renderAnimatedPlantArt`/
  `renderUi`) so they defer while a gesture is acquiring its coordinate read.
- `lastDisplaySpiActivityMs` - stamped by those same three call sites (and, after a second
  field report - see Thread 2 below - by every touch-triggered draw too) so the pending
  coordinate read waits for quiet on *both* the touch and display sides, not just its own.
- Both `TFT_CS`/`TOUCH_CS` explicitly driven HIGH immediately before the read, so neither
  controller can be mid-address-window from whatever it was last doing.

Field-confirmed working: "the rest looks fixed now" after this pair of changes. Full elimination
would still need a hardware check (verifying `TFT_CS` actually holds high at the ST7796
controller pin during an XPT2046 transfer, per Codex's diagnostic suggestion) - not done, since
the deployed unit can't be reworked physically. Treat this as "corruption risk greatly reduced,
not architecturally eliminated" if it resurfaces.

## Thread 2: brightness overlay redraw bugs

Found and fixed while the touch redesign above was in progress, since the overlay is the
main consumer of touch gestures during testing. Four separate, unrelated bugs:

### 2a. Overlay violated the surgical-redraw rule

Every button in the overlay (`-`/`+`, drag-end, sound toggle, one screen-toggle branch) called
the full `drawBrightnessOverlay()` (entire modal repaint) instead of a targeted redraw -
`drawBrightnessSliderRow()` already existed for exactly this (built during the original
touch/brightness feature specifically to stop the full-panel-per-sample flash during drags,
per `DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`) but had regressed on the button paths. Added
two more targeted functions on the same pattern - `drawBrightnessScreenButtonRow()`,
`drawBrightnessSoundButtonRow()` - and pointed every steady-state call site at the narrowest
applicable one. `drawBrightnessOverlay()` itself now calls all three for its one-time full paint,
so there's one source of truth for each region's layout.

### 2b. Slider value text showed stale leading digits ("over 100%")

`drawTextRight()`/`tft.print()` only clears the glyph cells it actually draws; right-aligned
text shifts its start position rightward as the string shrinks (e.g. "100%" -> "9%"), so the
old leading digit(s) were never overwritten - a stray "1" next to "9%" reads like "19%" or
worse. Not a math/clamp bug (the brightness value itself was correctly clamped to 1-100 the
whole time). Same class of bug the battery header already had a fix for. Fixed by clearing a
fixed-width rect (sized for "100%") before drawing, matching that precedent.

### 2c. Overlay could close itself under rapid button-mashing

Reported twice as "hit the brightness menu behind the rest of the display." Root cause (Codex
consultation, second call in the thread): `TOUCH_RELEASE_STABLE_MS`'s 160ms debounce - needed to
absorb this exact resistive panel's documented ~120ms release bounce - merges any re-press
inside that window into the *same* ongoing gesture rather than firing an action. While merged,
the old code re-sampled touch coordinates every poll and unconditionally overwrote
`lastTouchX`/`lastTouchY`, even while sitting on the `-`/`+` button (not dragging). Normal touch
jitter across many rapid re-samples could drift that cached position outside the minus button's
hitbox (only 4px inset from the modal's own left edge) by the time the merged gesture finally
released, misfiring the "outside tap closes the overlay" catch-all.

Fix: the touched control is now classified exactly **once**, from the gesture's single confirmed
press coordinate, into a latched `OverlayTouchTarget` enum (`Minus`/`Plus`/`Slider`/`Screen`/
`Sound`/`Close`/`None`). Neither the drag-sampling loop nor the release handler re-derives the
action from a fresh (possibly-drifted) coordinate anymore - release just dispatches on the
latched target. A gesture that started on the minus button can never be reclassified as a drag
or an outside-tap no matter how the finger position jitters while held.

Known accepted trade-off (per Codex, not fixed): very fast mashing still produces at most one
step per *merged* hold, not one step per physical tap - inherent to keeping the bounce-debounce.
It will not misfire as closing the overlay anymore, but a user mashing faster than ~160ms/tap
should expect fewer steps than taps, not more per-tap responsiveness. Not reported as a problem
by the user after this fix (field report was specifically "wayyy more responsive").

### 2d. Overlay-close redraw left the whole display flashing, then left gutter residue

Two sequential bugs at the same call site (`brightnessOverlayOpen = false` in the newly-added
`OverlayTouchTarget::Close` case):

1. Originally set `displayDirty = true`, forcing a full `renderUi()` purely to erase the modal -
   re-reads/parses `ui.json` from LittleFS, `fillScreen()`s the entire panel, redraws the header,
   and rebuilds every card from scratch (resetting each card's animation frame in the process).
   Visible as "the whole display refreshes." Fixed by adding `restoreCardsFromCache()`, which
   restores every card's actual content - background, plant name, art, border, moisture/
   battery/temp - from each card's own `PlantArtSlot` cache, with no `ui.json` re-read at all.
2. `restoreCardsFromCache()` only repainted each card's own `x/y/w/h` rect. The modal's panel
   rect doesn't line up with the card grid - it clips each card's outer edge (the grid has a
   margin the modal doesn't reach) but paints straight across the *gutters between* cards, in
   the modal's `TFT_SURFACE` tone rather than the dashboard's actual `backgroundColor` tone.
   Restoring only card rects left that gutter residue sitting there indefinitely (not part of
   any card, so `drawPlantCardDataOnly()` never touches it either) until some later, unrelated
   full `renderUi()` happened to `fillScreen()` over it - which read as "the old menu stays up
   until the default display eventually gets written over it," reported twice. Fixed by filling
   the overlay's own known panel rect with the real `backgroundColor` first, before any card
   redraws itself on top.

`restoreCardsFromCache()`'s writes are also now tracked by `lastDisplaySpiActivityMs` (Thread
1's guard) - it's a large multi-card write burst, and a rapid next tap's coordinate read must
wait for it to settle the same as it waits for the periodic writers.

## Final state (2026-08-30, all field-confirmed)

- Card-art corruption during touch: reduced from "most gestures" to not reproduced since the
  bidirectional guard, over normal use. Not architecturally eliminated - see Thread 1's closing
  note.
- Brightness overlay: fully surgical-redraw compliant, no stale text artifacts, does not
  self-close under rapid mashing, does not leave any visible residue on close.
- No known open display/touch bugs as of this write-up. If something in this area misbehaves
  again, do not assume it's automatically the same mechanism as either thread above - this
  session had two visually-similar-looking but mechanically unrelated bug classes (SPI
  corruption vs. a plain redraw-coverage gap). Get an independent read (phone-a-friend) before
  and after any fix attempt rather than iterating solo.
