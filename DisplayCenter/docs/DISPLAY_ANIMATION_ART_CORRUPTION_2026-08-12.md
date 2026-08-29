# Display animation corruption investigation

Status: **RESOLVED, user-confirmed 2026-08-12.** All four symptoms below (sprite-area
corruption/"two animations at once", the 2 red pixels above "Moist", and probe 3 intermittently
showing probe 1's sprite) are gone after Part 3 (the `T_IRQ` idle-gate) was flashed. Root cause
was the combination of Part 1's command-heavy per-pixel renderer (fixed by batching into
row bursts) and idle XPT2046 SPI polling leaking bytes onto the shared bus into whatever address
window the display renderer last left open (fixed by not polling touch at all while T_IRQ reads
idle-high). Kept below as a reference for the next time something in this area misbehaves.

Part 1: Codex's "Recommended implementation direction" (below) was implemented and flashed.
`drawPlantSpriteScaled()`/`drawPlantSpriteDiffScaled()` no longer exist - both are replaced by
a single `drawPlantSpriteComposited()` that resolves an entire row (sprite pixels +
transparent-to-background substitution) into a buffer and sends it as one
`writeAddrWindow()`+`writePixels()` burst, called for every animation-frame update as well as
initial card layout. The per-pixel diff comparison and the separate `tft.fillRect()` pre-clear
are both gone; the only diff-like logic left is a cheap pose-equality early exit in
`drawPlantArtDiffInCard()` (skip entirely if the sequence held the same pose - no drawing
either way, so nothing to desync). **User-confirmed this fixed the sprite-area corruption and
the "two animations at once" look.**

Part 2: after part 1, a *new* symptom appeared in a consistent spot on every card: 2 red pixels
directly above the "M" in the "Moist" label. A redraw-coverage gap was closed so residue between
the art and metric rows can self-heal. That is useful hardening, but the new symptom's exact
coordinate supplies stronger evidence for the still-open shared-SPI hypothesis: the composited
renderer leaves the ST7796 address window on the bottom art row starting at `x + 7`, exactly
above the `M`. The first two XPT2046 command words leaking into that window therefore produce
the same two pixels on every animated card. Moving from sparse 1x1 writes to row bursts did not
create new random corruption; it made the leaked bytes land at a deterministic window origin.

Part 3: `handleTouchInput()` now checks the raw active-low `T_IRQ` level before calling
`touchScreen.touched()`. The GPIO check creates no SPI traffic. Idle XPT2046 polling is skipped,
while polling continues after `touchWasDown` becomes true so release detection still works. This
removes the roughly 31 idle touch transactions per second that could recreate the two pixels
after each animation repaired its bottom row. **Flashed 2026-08-12; hardware confirmation
pending.** An actual touch still performs SPI transactions, so corruption during a press would
confirm that the display module's controller-side TFT CS needs the documented D1/pull-up
hardware repair.

Part 4 (new symptom, reported same session, before Part 3 was flashed): probe 3's card
intermittently displayed probe 1's sprite for roughly half a second before reverting - i.e. a
plausible, coherent *different* image, not scattered noise, and the duration lines up closely
with `ART_FRAME_INTERVAL_MS` (500ms). No logic bug was found in the draw call chain
(`renderAnimatedPlantArt` -> `drawPlantArtDiffInCard` -> `drawPlantArt` ->
`drawPlantSpriteComposited`) that could explain one slot's `art`/`x`/`y` being substituted with
another's - each iteration reads only its own `PlantArtSlot`, single-threaded, no aliasing found.
Leading theory: switching from many small self-resyncing per-pixel writes (part 1's old design)
to row-sized bursts *reduced total corruption opportunities but increased blast radius* - a
glitch that previously could only corrupt one isolated pixel (each `tft.writePixel()` re-sends
its own address window) can now, mid-`writePixels()` burst, desync the rest of that row (or spill
into the next row/command) with no re-sync point until the next row's fresh
`writeAddrWindow()`. A consistent-enough byte/bit shift across a whole row (or several) could
render as a coherently-wrong-but-recognizable image rather than random static. Not confirmed;
flagged as a real risk of the part-1 approach that Part 3 (killing idle touch traffic, the
most likely source of a mid-burst glitch) may or may not resolve. If Part 3 doesn't fix this,
the next mitigation to try is chunking `drawPlantSpriteComposited`'s per-row burst into smaller
pieces with a fresh `writeAddrWindow()` between chunks, trading back some of Part 1's efficiency
gain for a smaller failure radius per glitch.

Originally written 2026-08-12 as a handoff for a fresh set of eyes (Codex) after two rounds of
firmware changes failed to fix it; Codex then reviewed and added its own findings (see "Codex
static-review findings" below), which is what the implemented fix is based on. All code
references are to `arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino` (line
numbers below are from before this fix's edits and will have shifted - search by function name)
and to the `GFX Library for Arduino` version installed at
`C:\Users\samca\Documents\Arduino\libraries\GFX_Library_for_Arduino`.

## Leading implementation finding (added after a photo of the live corruption)

A photo of the physical unit showed three plant cards side by side and, together with the report
that the middle animation visibly glitches, sent this investigation back into the `GFX Library
for Arduino` source. One caution: a still photo alone cannot prove that all of the middle plant's
dither-like detail is corruption because the embedded `monstera` source art is itself highly
multicolored/dithered. The small isolated pink pixel below/right of its pot is a more useful static
artifact. The library review nevertheless turned up a significant renderer weakness:

**`Arduino_TFT::writePixelPreclipped()` (`Arduino_TFT.cpp` line 53) issues a full
`writeAddrWindow(x, y, 1, 1)` (column-address-set + row-address-set + memory-write, i.e.
`ST7796_CASET`/`ST7796_RASET`/`ST7796_RAMWR`, each with its own command byte and DC pin
toggle - see `Arduino_ST7796::writeAddrWindow`, `Arduino_ST7796.cpp` line 82) for *every single
pixel* written via `tft.writePixel()`.** `drawPlantSpriteDiffScaled()` (this repo, line 2328)
calls `tft.writePixel()` once per *changed* pixel inside its diff loop, and the reverted
"self-heal" experiment called the equivalent full-redraw path
(`drawPlantArtInCard`/`drawPlantArt`/`drawPlantSpriteScaled`, line 2260) which calls it once per
opaque pixel in the sprite bounding box.

`writeAddrWindow` does cache the last-set window (`_currentX`/`_currentY`/`_currentW`/
`_currentH` in `Arduino_TFT.cpp`) and skips re-sending CASET/RASET if the new window is
identical to the last one - but for a horizontal scan where `x` changes on every inner-loop
iteration (`drawPlantSpriteDiffScaled`'s `for (xx = 0; xx < drawW; xx++)`), the column cache
misses on every selected pixel. The row cache still hits within one row. In practice this means:
**a modest diff touching a few hundred pixels issues a few
hundred separate, discrete SPI command sequences (typically about 8 transferred bytes for the
next pixel on the same row and about 13 when the row changes, with repeated DC toggling), all
within one continuously-held CS-low transaction**, instead of the far
more typical and efficient pattern of "set the window once, stream N pixels' worth of color
data in one burst."

Why this is the best implementation target even though it is not yet a confirmed root cause:

- **It directly explains visible frame superposition during a transition.** A frame is rebuilt as
  hundreds or thousands of separately addressed pixels. Even on a perfect bus, the panel is
  observably half old/half new until that loop finishes. The pure diff also never repairs a pixel
  once physical GRAM differs from the assumed old frame.
- **The old "self-heal" full redraw was not a clean diagnostic** (see "Changes made" below): it
  clears the art rectangle first, then reconstructs the sprite through the same sparse per-pixel
  primitive. Looking worse can simply be the user seeing an empty/partly rebuilt intermediate
  frame; it does not prove that more commands caused more corruption.
- **Why two clock-speed reductions (40->20->10MHz) didn't fix it.** If the corruption is tied to
  a per-command timing edge case (e.g. DC pin transition setup/hold relative to the *next* SPI
  clock edge, or some other command-boundary-specific hazard) rather than being purely a function
  of steady-state signal integrity during a long data burst, then slowing the clock reduces
  exposure only for the parts of this hypothesis that overlap with generic signal integrity, not
  for a boundary/edge-triggered issue.

**Suggested concrete fix: IMPLEMENTED 2026-08-12.** `drawPlantSpriteComposited()` now does
exactly this - every transparent source pixel is resolved to the card's background color in a
row buffer, and each row is sent as one bulk `writeAddrWindow()`+`writePixels()` call, with no
separate `fillRect()` and no diff against a stored old frame. `gfxPtr`'s declared type is the
base `Arduino_GFX*`, which doesn't expose `writeAddrWindow()`/`writePixels()` (those are
`Arduino_TFT` members) - resolved with a `tftBus` macro
(`#define tftBus (*static_cast<Arduino_TFT*>(gfxPtr))`) next to the existing `tft` macro,
relying on `gfxPtr` always actually being constructed as an `Arduino_ST7796` (a subclass of
`Arduino_TFT`). Not yet visually confirmed on hardware - see the Status line at the top.

The hypotheses and history below were written before this finding and are kept for completeness
- they're not wrong, just superseded in priority by the above.

## Symptom

On the physical ESP32-S3 display receiver (see `DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md` for
the module/pinout), the animated plant-card art is visibly corrupted during normal idle
dashboard viewing (not specifically while touching the screen):

- A small, **fixed, reproducible number of red-tinted pixels** appear on specific cards and stay
  there - reported as 2 stray pixels on probe 2's card, 2 on probe 3's, 1 on probe 1's. This is
  not randomly-varying noise; the same cards show the same count repeatedly.
- Occasionally a card's art looks like **two animation frames superimposed** - the user's words:
  "it looks like the display gets caught between two animations and shows both of them at once."
- Separately (already fixed, see "Resolved in this pass" below): a full-board flash/flicker
  during slider drags and during the low-moisture blink cadence, and the slider knob not visibly
  tracking touch. These were real, understood bugs and are confirmed fixed.

The user is confident this started **after the touch/brightness feature was added**
(`docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`, uncommitted in this working tree) - it was
not present before. The animation rendering code itself (see "Code map" below) is **byte-for-byte
unchanged** by that feature (`git diff HEAD` touches none of it), so the new touch traffic may be
a trigger for an existing fragile rendering pattern rather than the animation arithmetic itself
having newly regressed.

## Hardware context

- ESP32-S3, ST7796S TFT (480x320, `Arduino_GFX`/`Arduino_ST7796`, write-only at runtime) and an
  XPT2046 resistive touch controller sharing **one physical SPI bus**: `SPIClass displaySpi(FSPI)`
  at file scope, with separate CS pins (`TFT_CS`=16, `TOUCH_CS`=9) but shared `SCK`=12, `MOSI`=13.
  The display driver's MISO argument is deliberately wired to `TOUCH_MISO`=8 (`TFT_MISO`=10 is
  present on the connector but unused), because the display is write-only and the touch
  controller is the only thing that ever needs to read - see the comment at the
  `Arduino_HWSPI` construction, ~line 4750.
- Full J1/GPIO pin map is in `DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`.
- Touch is polled, not interrupt-driven: `touchScreen.begin(displaySpi)` is called **without** a
  `tirqPin` argument (constructor is `XPT2046_Touchscreen touchScreen(TOUCH_CS)`, no IRQ pin), so
  `XPT2046_Touchscreen`'s internal `isrWake` flag is never touched by an ISR. Reading the library
  source (`C:\Users\samca\Documents\Arduino\libraries\XPT2046_Touchscreen\XPT2046_Touchscreen.cpp`,
  `update()`, ~line 120): `if (!isrWake) return;` is the only gate before doing a real SPI
  transaction, and the code that resets `isrWake` to false is itself gated on
  `255 != tirqPin` (line 181) - which is false here. Net effect: **each normal poll's first
  `touchScreen.touched()` call performs a real, bracketed SPI transaction
  (`beginTransaction`/CS low/~8 transfers/CS high/`endTransaction`) at 2MHz, unconditionally,
  whether or not a finger is on the glass.** A second call within the library's 3ms cache window
  reuses the sampled values rather than starting another transfer. This is genuinely new bus traffic that did not
  exist before the touch feature - previously nothing but the display touched this SPI bus.
- `handleTouchInput(now)` (line 2005) is called on **every** `loop()` iteration (line 4923),
  gated only by `TOUCH_POLL_INTERVAL_MS` (currently 32ms, was 16ms - see "Changes made" below).

## Code map

Animation rendering (all unchanged by the touch feature, confirmed via `git diff HEAD`):

- `renderAnimatedPlantArt()` (line 3036) - called from `loop()` roughly every
  `ART_RENDER_POLL_MS` (25ms), advances `PlantArtSlot.frame` for whichever slots are due
  (`slot.nextFrameMs`, randomized ~500ms +/-120ms per `randomizedArtInterval()`), then calls
  `drawPlantArtDiffInCard(..., oldFrame, newFrame)`.
- `drawPlantArtDiffInCard()` (line 2423) resolves the sprite and calls
  `drawPlantSpriteDiffScaled()`.
- `drawPlantSpriteDiffScaled()` (line 2328) is the actual diff renderer: looks up the sprite pose
  for `oldFrame` and `newFrame` (`sprite.sequence[frame % sprite.frameCount]`), and if the poses
  differ, walks every destination pixel in the scaled sprite's bounding box
  (`spriteDrawGeometry()`, line 2302) and only calls `tft.writePixel()` where the old and new
  pose's `spriteScaledPixel()` (line 2321) values disagree - the entire draw is bracketed in one
  `tft.startWrite()`/`tft.endWrite()`.
- This is a **pure diff renderer**: it has no way to verify what's actually on the physical
  screen. It trusts that the last thing it (or a full redraw) drew was exactly `oldFrame`'s
  bitmap. If that assumption is ever wrong for even one pixel - for any reason, including a
  corrupted SPI write - there is no self-correction; the wrong pixel just sits there.
- The only code path that repaints a card's art from a clean slate (not a diff) is
  `drawPlantCard()` -> `drawPlantArtInCard()` inside a **full** `renderUi()` rebuild
  (`cachePlantArtSlot()` also resets `slot.frame = 0` there). During ordinary steady-state
  operation (cards already on screen), a full `renderUi()` is rare - see the
  `displayDirty = true` call sites; only closing the brightness overlay, waking the screen from
  off, or a `ui.json` push trigger one.

Display driver transaction bracketing (library code, not this repo, included here because it's
the other half of the shared-bus contract):

- `Arduino_HWSPI::beginWrite()`/`endWrite()`
  (`C:\Users\samca\Documents\Arduino\libraries\GFX_Library_for_Arduino\src\databus\Arduino_HWSPI.cpp`,
  ~line 216) call `SPI.beginTransaction(mySPISettings)`/`CS_LOW()` and `CS_HIGH()`/
  `SPI.endTransaction()` respectively, gated on `_is_shared_interface` (constructed `true` here -
  see the `Arduino_HWSPI(...)` call, ~line 4750). `mySPISettings` is a **file-scope `static`**
  inside `Arduino_HWSPI.cpp`, set once in `begin(speed, dataMode)` to whatever speed is passed to
  `tft.begin()`. On paper this reconfigures the peripheral to the display's own clock/mode on
  every write, regardless of what the touch driver did in between.
- `XPT2046_Touchscreen::update()` likewise brackets its own transaction with its own
  `SPISettings(2000000, MSBFIRST, SPI_MODE0)`.
- Both sides claim to be well-behaved. Static reading did not turn up an obvious bracketing bug
  in either library, but this was not verified on a scope/logic analyzer - see "Not yet tried."

## Changes made this session, in order, with results

1. **SPI clock 40MHz -> 20MHz** (`DISPLAY_SPI_HZ`, then unnamed default). Rationale: Arduino_GFX
   defaults an ESP32 SPI bus to 40MHz (`SPI_DEFAULT_FREQ`,
   `GFX_Library_for_Arduino/src/Arduino_DataBus.h`); halving it trades fill-rate for
   signal-integrity margin now that a second chip's input capacitance is tapped onto the same
   SCK/MOSI traces. **Result: no change reported.**
2. **SPI clock 20MHz -> 10MHz**, bundled with:
3. **Touch poll interval 16ms -> 32ms** (`TOUCH_POLL_INTERVAL_MS`, line 333). Rationale: halve
   how often the bus switches between the two devices per second, independent of clock speed.
   **Result (both bundled together): user reports still seeing the fixed-count red pixels and
   the "two animations at once" look. Not isolated from each other - see "Not yet tried."**
4. **Periodic full (non-diff) art redraw every 8th animation tick**, added to
   `renderAnimatedPlantArt()` as a "self-heal" - the idea being that even without knowing the
   root cause, forcing an occasional clean repaint would bound how long a stray pixel could
   persist. **Result: made it actively worse** ("animations looks much worse now") **and did not
   fix either symptom** (red pixels still present, still getting stuck between two animations).
   **This was reverted** - the code is back to always using the diff path
   (`drawPlantArtDiffInCard`, unconditionally, as it was originally). **Codex correction:** this
   was not an atomic clean-frame experiment. `drawPlantArtInCard()` first clears the complete art
   rectangle, then `drawPlantSpriteScaled()` reconstructs the sprite with thousands of sparse
   `writePixel()` calls. A camera or eye can therefore see the empty/partly rebuilt intermediate
   state, and the test greatly increases address-window command overhead as well as pixel data.
   Its looking worse is expected even on a perfect bus. It does show that this particular
   clear-then-sparse self-heal is unsuitable, but does **not** by itself distinguish an electrical
   fault from renderer/desynchronization.

**Current live state of the relevant constants** (after the revert):
- `DISPLAY_SPI_HZ = 10000000` (line 344)
- `TOUCH_POLL_INTERVAL_MS = 32` (line 333)
- `renderAnimatedPlantArt()` always uses the diff path, no periodic full redraw (reverted to
  original behavior modulo the two constants above).

Also fixed this session, confirmed working, unrelated to the above (kept):
- Brightness modal's `+`/`-` buttons snapping to 0%/100% instead of stepping by 10 - was a hit-box
  overlap between the live slider-drag region and the button regions.
- Full-board flicker during slider drag - `drawBrightnessOverlay()` was repainting the entire
  modal panel on every touch sample; split into `drawBrightnessSliderRow()` for live updates.
- Full-board redraw on the 500ms low-moisture blink cadence - `renderDashboardDataOnly()` now
  takes a `blinkOnly` flag (line 2721) and only touches the card(s) actually blinking
  (`slotNeedsBlink()`, line 2694), not the whole board.
- Stale auto-cal bounds/overdue flag only refreshing on a full `renderUi()` - added
  `refreshSlotMoistureBounds()` (line 2199) so the lightweight per-tick redraw path picks up
  background auto-cal updates too.

None of these four are implicated in the red-pixel/double-animation bug - they're independent,
confirmed-working fixes, mentioned here only so the diff between "known-good" and "still broken"
is unambiguous.

## Hypotheses and their current standing

1. **Pure electrical/signal-integrity issue from the shared bus at high clock speed.**
   Weakened by evidence: two successive clock reductions (40->20->10MHz) did not fix it. Not
   fully eliminated - 10MHz might still be too fast for whatever the actual trace/wiring quality
   is, and clock speed is only one variable in signal integrity (rise time, stub length,
   termination, power/ground integrity, and CS/DC timing are others). But "just lower the clock
   more" is looking less promising as a standalone fix.
2. **Diff-render desync**: something causes `PlantArtSlot.frame`/`oldFrame` to stop accurately
   reflecting what's actually on screen, so `drawPlantSpriteDiffScaled()`'s diff either skips
   pixels it should touch (leaving a stray old-frame pixel = the fixed red-pixel count) or the
   *sequencing itself* is what produces the "two frames at once" look (some pixels updated to the
   new pose, others still showing the old one, because the diff only ever touches pixels it
   detects as changed relative to its internal assumption). Static reading of
   `spriteDrawGeometry()`/`spriteScaledPixel()`/`drawPlantSpriteDiffScaled()` did not turn up an
   arithmetic bug (scale/rounding math checks out for both upscale and downscale cases,
   `oldFrame`/`slot.frame` bookkeeping in `renderAnimatedPlantArt()` looks internally consistent,
   and `cachePlantArtSlot()` correctly resets `slot.frame = 0` in lockstep with the one code path
   that draws a real frame-0 image). Nothing conclusive found, but not ruled out - a static read
   can't see actual on-device pixel state.
3. **Bus arbitration/CS timing issue not primarily driven by clock speed** - e.g. a deselected
   chip not fully releasing the bus before the other's CS goes low, or the `SPI.beginTransaction`
   mutex/reconfiguration on ESP32 not being as clean a boundary as assumed when the two
   `SPISettings` are very different (2MHz vs 10-40MHz) and switching happens at a high rate
   (every ~32ms from touch alone, plus whatever cadence the display is writing at). This would
   explain why lowering the display's own clock didn't help - the problem might not be about the
   display's clock being "too fast" so much as about the *transition* between two very different
   clock/CS states happening very frequently. Not verified; would need a scope/logic analyzer on
   SCK/MOSI/both CS lines to actually see what's happening at a transition, which wasn't
   available this session.
4. The periodic art redraw making the animation look *worse* (see change #4 above) is not useful
   evidence against hypothesis 2. That path exposes an empty/partly rebuilt sprite on screen and
   uses the same sparse per-pixel primitive as the diff path. A useful clean-frame experiment must
   first composite transparency to the card surface in RAM and then send an opaque rectangle (or
   opaque rows) in long bursts, without a separately visible clear.

## Codex static-review findings (2026-08-12)

These findings are based on the current sketch, generated sprite header, the installed
Arduino_GFX/XPT2046 library sources, and ESP32 Arduino core 3.3.11. They do not replace a physical
test, but they change the order in which the existing hypotheses should be tested.

1. **The current renderer is intrinsically non-atomic and extremely command-heavy.** Both
   `drawPlantSpriteScaled()` and `drawPlantSpriteDiffScaled()` call `tft.writePixel()` for each
   selected destination pixel. In the installed Arduino_GFX implementation, every one of those
   calls issues a 1x1 address window plus `RAMWR` and a 16-bit color. `startWrite()` keeps CS and
   the SPI transaction open around the loop, but it does not turn the sparse calls into one pixel
   burst. A transition can therefore remain visibly half old/half new for long enough to be seen
   or photographed. This is a direct software explanation for the "two frames superimposed"
   appearance, even if every byte reaches the panel correctly. It does not alone explain a pixel
   that remains wrong indefinitely; the pure diff renderer can make any one missed/wrong update
   persistent.
2. **Static reading found no firmware-level overlap between a touch transfer and an art write.**
   Normal touch polling and all `tft` calls run sequentially in the main loop; the BLE/cloud tasks
   do not draw. Both drivers bracket the shared `SPIClass` with `beginTransaction()` /
   `endTransaction()`. `XPT2046_Touchscreen::begin(displaySpi)` does call `displaySpi.begin()` a
   second time, but ESP32 core 3.3.11 immediately returns when that `SPIClass` is already started,
   so it does not silently remap the custom GPIO12/GPIO8/GPIO13 pins. This does not rule out an
   electrical or mode-switch settling issue, but "software transactions overlap" should no longer
   be the leading explanation without a scope trace or a newly found cross-task SPI caller.
3. **MISO is not a shared/tapped net on this PCB.** TFT MISO is GPIO10 and touch MISO is GPIO8;
   runtime simply configures the ESP32 input to GPIO8 because the display is write-only. GPIO8
   routing can affect touch reads, but it is not an extra load on the shared SCK or MOSI lines and
   is unlikely to explain corrupt display writes. Physical inspection should focus on SCK, MOSI,
   both CS signals, ground/decoupling, and mode-switch timing rather than a nonexistent shared-MISO
   tap.
4. **The photographed dot is worth treating as a color signature.** Sprite transparency is stored
   as RGB565 `0xF81F` (bright magenta). The generator produced no opaque magenta-like pixels in the
   embedded `monstera` poses, and both intended draw paths either skip that sentinel or replace it
   with `surfaceColor`. The small pink/magenta-looking pixel just below and right of the middle
   plant pot therefore should be compared against an on-screen `0xF81F` reference swatch. If it
   matches, temporarily regenerating with a different sentinel is a high-value diagnostic: a dot
   that changes with the sentinel would prove transparent bitmap data is somehow reaching GRAM;
   a dot that does not change is probably a theme/sprite color or transfer artifact. Do not adopt a
   darker sentinel as the final "fix" because it would only hide the fault and could collide with
   legitimate art colors.
5. **The touch IRQ can eliminate idle SPI traffic without relying on the library ISR.** T_IRQ is an
   active-low level signal. In normal `handleTouchInput()`, skip `touchScreen.touched()` while
   `digitalRead(TOUCH_IRQ) == HIGH` and `touchWasDown == false`; continue polling while a gesture is
   active so release is detected. This avoids the previously reported missed-edge/isrWake problem
   entirely and removes essentially all touch mode switches while the screen is idle. Keep the
   no-IRQ constructor. If this fixes the art, it gives a usable mitigation and specifically
   implicates frequent MODE0/MODE3 switching rather than touch coordinate logic.

### Recommended implementation direction - IMPLEMENTED 2026-08-12

Replace sparse `writePixel()` animation rendering with an **opaque composited burst**. For each
output row (or one reusable full sprite buffer), resolve every transparent source pixel to
`surfaceColor`, then send the complete row/rectangle with a bulk RGB565 operation. Do not issue a
separate `fillRect()` first. This has four useful properties:

- the new frame overwrites every pixel and therefore self-heals any earlier desynchronization;
- the panel never sits at a deliberately cleared intermediate frame;
- address-window commands fall from one per changed/opaque pixel to one per row or frame; and
- the experiment separates renderer artifacts from shared-bus faults much better than the reverted
  every-eighth-frame clear-and-redraw.

A row buffer is safer than a large stack allocation: the sprites are currently 84x84 but
`spriteDrawGeometry()` permits 2x/3x scaling in larger layouts. Use a fixed reusable/global buffer
large enough for the maximum rendered row, or allocate it once with an explicit failure path.
Keep all display calls on the main loop.

**What was actually built:** `drawPlantSpriteComposited(sprite, centerX, topY, maxW, maxH,
frame, backgroundColor)` is now the single entry point for all sprite art - it composites the
*entire* `maxW x maxH` card art area (sprite pixels plus background-substituted transparent
pixels), not just the sprite's own scaled bounding box, so no separate clear is needed even
when the sprite is smaller than its card slot. Row buffer is a 480-`uint16_t` (960 byte)
**global** array (`plantSpriteRowBuffer`, matching the panel width so no sprite-scale-dependent
sizing math is needed), not a stack allocation - addressing the concern above directly, and safe
because this function is only ever called from the single-threaded main loop, never
reentrantly. `drawPlantSpriteScaled()` and `drawPlantSpriteDiffScaled()` (the old per-pixel
functions) were deleted outright rather than kept alongside the new path. The only surviving
diff-like logic is a cheap pose-equality check in `drawPlantArtDiffInCard()` that skips the
call entirely when the animation sequence held the same pose (no drawing happens either way, so
there's nothing for that check to desync against - unlike the old diff, it never decides what
to draw, only whether to bother).

## Card row-gap fix (2026-08-12, after part 1 was confirmed working)

`drawPlantCardDataOnly()` (the per-tick fast redraw path) draws three independent regions per
card that are never all repainted together outside a full `renderUi()`: the art (via
`drawPlantArtDiffInCard`, on the animation cadence), the "Moist" row, and the "Batt" row. The
latter two used hardcoded pixel offsets that didn't quite reach the region above them:

```cpp
// Before: gap between art bottom and here, and between here and the batt row
tft.fillRect(x + 7, y + h - 63, w - 14, 26, slot.surfaceColor);
...
tft.fillRect(x + 7, y + h - 30, w - 14, 24, slot.surfaceColor);
```

`drawPlantArtInCard`/`drawPlantArtDiffInCard` compute the art region's bottom edge as
`y + 24 + max(24, h - 91)` (non-compact case). For a typical card that's 4px above where the
Moist row's clear started, and the Moist row's own bottom (`y + h - 37`) is 7px above where the
Batt row's clear started. Neither gap is touched by anything else in the fast redraw path, so a
stray pixel there (from any cause) never gets overwritten. Fixed by computing each row's clear
top edge as the minimum of its old hardcoded value and the bottom edge of the region above it -
`min()` only ever moves a top edge *up* (closing a gap), never down, so it can't regress any
case where the old offsets already had no gap:

```cpp
const int artBottomY = y + 24 + max(24, h - 91);
const int moistRowBottom = y + h - 37;
const int moistRowTop = min(y + h - 63, artBottomY);
tft.fillRect(x + 7, moistRowTop, w - 14, max(1, moistRowBottom - moistRowTop), slot.surfaceColor);
...
const int battRowBottom = y + h - 6;
const int battRowTop = min(y + h - 30, moistRowBottom);
tft.fillRect(x + 7, battRowTop, w - 14, max(1, battRowBottom - battRowTop), slot.surfaceColor);
```

**Not fixed, lower priority since not reported:** the same class of gap exists between the
card's plant-name text (drawn only by the full render, ends around `y + 14`) and the art
region's top (`y + 24`, a ~10px band) - the art composite doesn't extend upward to cover it and
nothing else repaints it on the fast path either. Untouched because it hasn't actually shown a
visible artifact yet; if one ever appears there, the fix is the same shape (extend
`drawPlantSpriteComposited`'s covered area, or give the name text its own small clearing
`fillRect` on a redraw path that actually runs periodically).

## Not yet tried / suggested next steps

- **Isolate touch polling entirely**: temporarily stub `handleTouchInput()` to a no-op (or set
  `touchReady = false` after init) and watch the dashboard animate for an extended period with
  zero touch-controller SPI traffic. If the red pixels/ghosting stop completely, that's a
  near-definitive confirmation that touch SPI traffic or SPI mode switching is the trigger (not
  necessarily electrical "contention"). Then test the raw, active-low T_IRQ level gate described
  above before attempting an ISR-driven library configuration.
- **Run the complementary static-art test**: leave touch polling active but disable only frame
  advancement/diff drawing. If new dots appear in already-drawn static art, sprite sequencing and
  diff arithmetic are exonerated and the shared bus/power path moves sharply upward in likelihood.
  If no dots ever appear, the volume/shape of art writes or the transition logic is implicated.
- **Isolate the diff logic with a real opaque-frame test**: with touch polling still active, draw
  each new animation pose as one precomposited opaque rectangle/row sequence. Do not use
  `drawPlantArtInCard()` for this experiment because its clear followed by sparse pixels is the
  already-failed non-atomic test. Persistent corruption after repeated identical opaque frames is
  strong evidence of a transfer/electrical problem; clean output implicates the sparse diff path.
- **Use T_IRQ as a level gate**: first log it for several minutes without changing behavior and
  verify idle HIGH, held-touch LOW, release HIGH. Then skip SPI touch reads while idle HIGH. This
  experiment is lower risk and more diagnostic than merely increasing `TOUCH_POLL_INTERVAL_MS`.
- **Test a short bus-settling guard only if touch-off succeeds**: explicitly ensure both CS pins
  are HIGH and add a small microsecond-scale quiet interval after a touch transaction before the
  next display transaction. If that alone fixes it, mode/CS settling is more likely than raw clock
  frequency. Do not add arbitrary millisecond delays as a permanent workaround.
- **Serial-log the actual draw calls** for one specific affected card (e.g. probe 2) - print
  `oldFrame`, `newFrame`, and whether the poses were considered equal on every
  `renderAnimatedPlantArt()` tick for that slot, correlated with when the red pixel is visually
  first noticed, to narrow down whether it appears immediately after a specific diff call.
- If a scope or logic analyzer becomes available, capture SCK/MOSI/TFT_CS/TOUCH_CS during a
  window where the corruption is visually reproducible, to see directly whether there's
  contention (both CS lines briefly low), an unexpected MODE0/MODE3 transition edge, or a
  runt/glitched clock rather than continuing to infer it from firmware-only experiments.

## Reproduction

Not currently automatable - requires the physical receiver with cards actively animating (needs
`slot.h >= 76`, i.e. not the compact card layout, and `slot.artEnabled`). The user observes it
during normal idle dashboard viewing over a period of roughly a minute or more; no specific
trigger action (like opening the brightness overlay) has been identified as necessary or
sufficient - it appears to be a background/ambient corruption during ordinary animated operation.
