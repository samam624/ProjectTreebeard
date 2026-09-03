# Display, Touch, and Brightness Implementation

Last updated: 2026-08-12

This is the durable hardware and firmware reference for the MSV2 display center. It supersedes
older assumptions that the backlight is permanently wired on or that the touch controller is
unknown.

A real display-corruption issue (stray pixels, torn animation frames, one card's sprite briefly
showing another's) traced to the shared-SPI behavior described below was found and fixed on
2026-08-12 - see `docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md` (status: RESOLVED) for the
full investigation. The "Shared SPI topology" and "Touch calibration and controls" sections below
reflect the fixed, current state, not the original implementation.

## Confirmed module

- Product: Hosyond 4.0-inch TFT Touch Screen LCD module, ASIN `B0CKRJ81B5`
- Physical resolution: 480 x 320 pixels
- LCD controller: ST7796S
- LCD color/interface: 65K RGB over 4-wire SPI
- Touch type: 4-wire resistive
- Touch controller: XPT2046, 12-bit ADC
- Runtime graphics library: Arduino_GFX `Arduino_ST7796`
- Runtime touch library: Paul Stoffregen `XPT2046_Touchscreen` 1.4.0

## Confirmed ESP32-S3 and J1 pin map

| J1 | PCB net | ESP32-S3 GPIO | Function |
|---:|---|---:|---|
| 1 | `T_IRQ` | 7 | XPT2046 interrupt, active low |
| 2 | `T_MISO` | 8 | XPT2046 data out / ESP32 MISO |
| 3 | `TFT_MOSI` | 13 | Shared SPI MOSI; XPT2046 `T_DIN` |
| 4 | `T_CS` | 9 | XPT2046 chip select, active low; 10K pull-up R13 |
| 5 | `TFT_SCK` | 12 | Shared SPI clock; XPT2046 `T_CLK` |
| 6 | `TFT_MISO` | 10 | Optional ST7796S readback; not used by runtime |
| 7 | `BKLED` | 11 via Q2/Q1 | Non-inverted backlight PWM |
| 8 | `TFT_SCK` | 12 | ST7796S SPI clock |
| 9 | `TFT_MOSI` | 13 | ST7796S data in |
| 10 | `TFT_DC` | 14 | ST7796S data/command |
| 11 | `TFT_RST` | 15 | ST7796S reset, active low |
| 12 | `TFT_CS` | 16 | ST7796S chip select, active low; 10K pull-up R14 |
| 13 | `GND` | GND | Module ground |
| 14 | `VBUS` | 5V | Module/backlight supply; C3 10uF + C4 100nF decoupling |

Other confirmed board outputs are buzzer GPIO38 and LEDs GPIO40/GPIO41/GPIO42. Native USB uses
GPIO19 (`D-`) and GPIO20 (`D+`).

## Shared SPI topology

The ST7796S and XPT2046 are separate SPI devices. They share GPIO12 clock and GPIO13 MOSI, but
have separate chip-select and MISO nets. Firmware uses one `SPIClass(FSPI)` and transaction-aware
drivers for both devices. Both CS pins are driven high before bus initialization in addition to
their PCB pull-ups.

The runtime writes but does not read the ST7796S. Therefore the shared hardware SPI peripheral's
MISO input is routed to the touch controller on GPIO8. The separate GPIO10 `TFT_MISO` wire remains
unused unless display-register readback is added later.

`XPT2046_Touchscreen` runs in polling mode with no IRQ pin passed to its `begin()` call
(deliberate - the library's own interrupt-based gating was found unreliable on this board). That
means every `touchScreen.touched()` call used to perform a real SPI transaction unconditionally,
even with no finger on the glass, which is genuinely new bus traffic that didn't exist before
touch was added. Those idle transactions could leak bytes into whatever ST7796S address window
the display's own renderer had last left open, producing visible display corruption. Fixed by
gating `handleTouchInput()` on a passive `digitalRead(TOUCH_IRQ) == HIGH` check (T_IRQ is
active-low) before touching the SPI bus at all while idle - polling still runs normally once a
touch is in progress, so release detection is unaffected. `DISPLAY_SPI_HZ` is also set to 10MHz
rather than Arduino_GFX's ESP32 default of 40MHz, as a signal-integrity margin trade; this alone
did not resolve the corruption but was kept since it isn't shown to cost anything either. See
`docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md` for the full investigation, including the
separate command-volume fix to the sprite renderer itself.

## Backlight behavior

`ESP_BKLED` GPIO11 drives Q2 (2N7002), which pulls down the Q1 DMP3098L-7 P-FET gate. The behavior
is non-inverted: zero duty is off and full duty is full brightness. This was bench-confirmed with
`arduino/Treebeard_ESP32_Backlight_Test`.

Runtime settings:

- LEDC frequency: 1 kHz
- Resolution: 10-bit, duty 0-1023
- User setting: 1-100 percent plus a separate `screenOn` boolean
- Default: 75 percent, screen on
- Mapping: squared perceptual curve from percent to electrical PWM duty
- Persistence: LittleFS `/display.json`

Separating `screenOn` from brightness preserves the last useful nonzero setting. The first touch
while off wakes the display and is consumed so it cannot activate a hidden control.

## Touch calibration and controls

XPT2046 raw readings vary between individual resistive panels. Firmware does not rely on generic
`300..3800` endpoint guesses for normal operation. It collects four corner presses, builds a
three-point affine transform, validates the fourth corner, and stores raw calibration points in
LittleFS `/touch-calibration.json`.

- On first boot without calibration, the receiver waits up to 10 seconds for the first target.
- Hold the screen during boot for about one second to force recalibration.
- The transform supports swapped, inverted, and mildly skewed panel axes.
- Calibration is collected in display rotation 1 and transformed for later rotations.

The dashboard opens its brightness modal on a double tap anywhere on the screen. The old 40x30
sun button and its narrow 48x40 hit target were removed after physical testing showed that the
corner target commonly required repeated attempts. XPT2046 is a single-point resistive controller,
so it cannot implement a real two-finger gesture. Double-tap detection uses completed releases, a
120ms minimum gap to reject contact bounce, and a forgiving 650ms maximum gap. A wake-only first
touch is explicitly excluded from the gesture.

A single tap on a plant card expands it in place into a diagnostics view
(`drawPlantCardDetail()`/`drawPlantCardDetailDataOnly()` in the `.ino`) - signal quality, RSSI,
age, wake/received/missed counters, packet loss, moisture range, and (when the probe sends it)
raw oscillator frequency/charging state, laid out to scale with the tapped card's actual size.
Tapping again - the expanded card or anywhere else on the dashboard - collapses it back. Only
one card can be expanded at a time. Because double-tap is a two-release gesture, a tap-up can't
be known to be a genuine single tap until `TOUCH_DOUBLE_TAP_WINDOW_MS` passes with no second tap;
`resolvePendingDashboardTap()`, called every `loop()` iteration rather than only when the touch
controller is polled, resolves this on a plain elapsed-time check so it still fires even if the
panel goes idle with no finger on it for the whole window. Confirmed working on the physical
receiver 2026-08-12.

The modal contains a large slider, +/- 10-percent buttons, screen on/off, and close. PWM previews
immediately while dragging; persistence and cloud write occur only once on finger release. Dragging repaints only the slider row
(`drawBrightnessSliderRow()` - value text, track, fill, knob), not the whole modal, so a drag
doesn't flash the rest of the panel. The live drag-preview hit-region and the `+`/`-` buttons'
own hit-regions are mutually exclusive by construction (the drag check explicitly excludes the
exact button boxes used at release time) - they used to overlap by a few pixels on each button's
inner edge, which made a tap land on 0%/100% instead of stepping by 10.

## Local and remote settings protocol

Display settings intentionally do not live inside `ui.json`. Plant layout/config and display
brightness have different writers and update frequencies; separating them prevents a brightness
write from replacing cards, theme, or sensor calibration.

Local receiver API:

- `GET /display`
- `POST /display` with `{"brightnessPercent":75,"screenOn":true}` during the normal post-boot
  local-admin window

Cloud components:

- Table: `public.msv2_receiver_display_settings`
- Migration: `019_msv2_receiver_display_settings.sql`
- Receiver endpoint: `receiver-snapshot?display=1`
- Household endpoint: `plant-dashboard/api/share-display`
- Receiver polling: every 30 seconds
- Snapshot confirmation: reports desired/effective brightness, on/off, applied settings version,
  touch readiness, and calibration status

All accepted writes receive an atomic server-assigned monotonically increasing version. A physical
touch applies locally and survives loss of WiFi; when connectivity returns it is submitted as a
new server version. Phone writes use the household share token and can affect only receivers linked
to that household. The PWA compares desired server version to the receiver snapshot's applied
version and displays `queued` until confirmed.

## Rendering discipline

Every piece of dashboard UI - the plant grid, the brightness modal, the tap-to-expand card
detail view - follows a full-draw/data-only function split: a full paint happens once per
genuine "this appeared or the layout changed" event, and the routine/periodic refresh path
only repaints the pixels whose content actually changed (`drawPlantCard`/`drawPlantCardDataOnly`,
`drawBrightnessOverlay`/`drawBrightnessSliderRow`, `drawPlantCardDetail`/
`drawPlantCardDetailDataOnly`). This is a correctness rule on this hardware, not just an
efficiency preference - see "Shared SPI topology" above for the real corruption incident that
excess redraw traffic contributed to. See `.claude/skills/display-surgical-redraw/SKILL.md` for
the full rule and worked examples before adding or modifying any display feature.

## Local verification and deployment boundary

Compile the receiver without flashing:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/arduino_display.ps1 -Action compile
```

Do not deploy the Supabase migration/functions or the Netlify PWA until local verification is
complete and the user explicitly approves deployment. Physical verification still requires the
real receiver: PWM levels, four calibration targets, coordinate orientation, touch wake behavior,
slider drag, and shared-SPI stability during display animation.

BLE/cloud task interaction, receiver identity, and the packet-triggered upload fix are documented
separately in `BLE_CLOUD_CONTENTION_FIX_2026-08-11.md`.
