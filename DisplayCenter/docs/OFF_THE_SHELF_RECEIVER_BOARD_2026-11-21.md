# Off-the-shelf receiver board: chosen candidate (2026-11-21)

**Status: chosen, not bought.** On 2026-11-21 the maintainer picked the **Waveshare
ESP32-S3-Touch-LCD-3.5** as the off-the-shelf replacement for the hand-built DisplayCenter (an
ESP32-S3-WROOM-1U-N16R8 on their own PCB, wired to a ~$20 3.5" ST7796S TFT). The aim is a clean
desk unit that runs the same receiver firmware. The cased version is the likely buy. Nothing
below was checked on hardware; the facts come from vendor pages read on 2026-11-21.

## What the maintainer asked for

- WiFi and BLE must work well. This is the priority: it is a receiver.
- A better screen than a $20 TFT, ideally. Touch is optional. The buzzer is unimportant.
- Changes to the display and audio code are acceptable.
- $30-60, an all-in-one desk package, and a stand that keeps it tilted upright.

## The board

From [Waveshare's docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5),
[wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5) and
[product page](https://www.waveshare.com/esp32-s3-touch-lcd-3.5.htm):

- **MCU:** a bare ESP32-S3R8 chip, not a WROOM module. 16 MB flash (W25Q128JVSIQ) and 8 MB
  PSRAM. Espressif's R8 parts have octal PSRAM, so the build's `PSRAM=opi` should carry over.
  That is inferred from the part naming; Waveshare does not say "octal".
- **Display:** a 3.5" 320×480 IPS panel on an ST7796, over SPI. This is the same driver and
  resolution the firmware already drives through Arduino_GFX.
- **Touch:** capacitive, on an FT6336 over I2C.
- **Audio:** an ES8311 codec, a microphone, and an MX1.25 speaker header. A 6 Ω 1 W speaker comes
  in the standard package.
- **Other parts:** an AXP2101 PMU, a QMI8658 IMU, a PCF85063 RTC, a TF slot, a camera socket and a
  Li battery header. Waveshare's demos depend on a TCA9554 IO expander. What it controls is not
  stated; LCD reset or backlight would be the usual candidates.
- **Antenna:** onboard. There is also an IPEX1 connector, which is "switched to use the external
  antenna via resoldering an onboard resistor". The pages do not give the resistor's designator,
  value or location, so check the schematic linked from
  [Resources](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5/Resources-And-Documents).
- **Board revisions:** V2 has a 4-layer PCB (V1 had 2 layers) and an "adjusted RF circuit".
  Waveshare says the two run the same programs. A label on the back gives the version. Which
  revision ships in the cased kit is not stated.
- **Variants:** SKU 30733 is the board only. SKU 30934 (`-C`) adds a case and an OV5640 camera.
  waveshare.com shows **$25.99-34.99** but does not say which price belongs to which variant.
  Third-party sellers ask about $90 for `-C`, so buy direct.
- **The case:** there is no stand, and none is sold. Its material and dimensions are not
  published, beyond an outline drawing. No antenna-cable opening is mentioned, and no external
  antenna is bundled.

## Porting the receiver firmware

The parts that carry over unchanged: the ST7796 driver in Arduino_GFX, the 480×320 card layout
(`verify_display_card_layout.mjs`), and partial redraws (the `display-surgical-redraw` rule). So
do BLE scanning, WiFi/TLS, LittleFS and the cloud path.

What changes:

- **Pins.** The current map is TFT SCK 12, MOSI 13, DC 14, RST 15, CS 16, backlight 11, touch
  CS 9, IRQ 7, MISO 8, buzzer 38 and LED 2. None of it will match.
- **Touch.** The FT6336 replaces the XPT2046. The 3-point affine calibration, and the
  shared-SPI-bus workarounds made for the resistive touch, can go.
- **Audio.** I2S tones through the ES8311 replace the `ledc` buzzer on GPIO 38.
- **Backlight and LCD reset.** These may run through the TCA9554 or the AXP2101. Not checked.
- **Flash size and partitions.** The current build is pinned to `FlashSize=4M` so the existing
  LittleFS stays where it is. A new board has no data to keep, but changing the setting means new
  receiver setup (`/wifi.json`, `/cloud.json`, `/ui.json`).

## Before porting: check the radio

1. Flash the board as it comes, or with a small beacon sketch. Measure its BLE signal at the PC
   with the receiver firmware's `ble-beacon <1-600>` serial command and a bleak scan. Compare that
   with build two in the same spot. For reference, build two on 2026-11-21 at this PC gave a
   median of −42 dBm (range −45 to −39) and its WiFi AP read −51 dBm. Where it sat was not
   recorded. On 11-19, in its V2 enclosure next to the PC, it gave −35 dBm and −39 dBm.
2. A better test: put it beside the main receiver (`28:84:85:46:70:D8`) and compare the RSSI each
   one logs for the same probe packets.
3. Only if the onboard antenna hears the probes clearly worse, move the IPEX resistor and fit an
   external antenna. With the `-C` case, expect to drill a hole for an IPEX-to-SMA pigtail. The
   other route is the board-only SKU in a printed back shell with an SMA mount built in.

**Stand:** print one on the P1S, adapted from the V2 enclosure's stand with prop pivots
(`DisplayCenter/hardware/MSV2_ESP32_DisplayCenter/V2/`). Measure the case with calipers or work
from Waveshare's outline drawing.

## Rejected, and why

- **Guition JC3248W535 ($11-18) and Waveshare ESP32-S3-Touch-LCD-3.5B.** They use the AXS15231B
  QSPI panel. atomic14: "only works in LVGL full-refresh mode (partial/direct render is broken)",
  and some batches ignore window-address commands. That breaks partial redraws.
- **Elecrow CrowPanel Advance 3.5" ($25.90).** It has the same N16R8 module, a 480×320 ILI9488
  IPS panel and capacitive touch. But there is no case option (it hangs from the back) and no
  IPEX connector.
- **800×480 RGB boards: CrowPanel Advance 4.3"/5" (N16R8, from $32.90) and Waveshare
  ESP32-S3-Touch-LCD-4.3B-BOX ($43.99 with case).** An RGB panel is refreshed continuously from
  PSRAM. Espressif documents that the image drifts during WiFi, BLE and flash writes. The fix,
  XIP on PSRAM plus a bounce buffer, needs sdkconfig options the stock Arduino core does not
  expose. This receiver does all three at once: BLE scanning, TLS uploads and LittleFS outbox
  writes. The UI would also need relaying out for 800×480. (The 4.3B-BOX case has 75 mm hole
  spacing on its back.)
- **Seeed SenseCAP Indicator.** It has a 480×480 RGB panel, with the same drift risk. The D1S is
  £75.90-$98.60. The D1 is about $49-52, but whether a stand is included, and how much PSRAM it
  has, could not be confirmed.
- **Espressif ESP32-S3-BOX-3.** The dock stand is included, but the screen is 2.4" 320×240, a
  downgrade. The one price found was $99.87.
- **Makerfabs MaTouch ESP32-S3 3.5".** It is N16R2, with only 2 MB of PSRAM.
- **ESP32-P4 boards (MIPI screens).** The P4 has no radio of its own. WiFi and BLE go through a
  companion chip, which is a poor fit for a BLE receiver.

Sources: [Waveshare 3.5 docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5),
[wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5),
[product](https://www.waveshare.com/esp32-s3-touch-lcd-3.5.htm),
[3.5B](https://www.waveshare.com/esp32-s3-touch-lcd-3.5b.htm),
[4.3B-BOX](https://www.waveshare.com/product/esp32-s3-touch-lcd-4.3b.htm?sku=28141),
[CrowPanel Advance 3.5](https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-480x320-artificial-intelligent-ips-touch-screen.html),
[CrowPanel Advance 5](https://www.elecrow.com/crowpanel-advance-5-0-hmi-esp32-ai-display-800x480-ips-artificial-intelligent-touch-screen.html),
[atomic14 JC3248W535](https://www.atomic14.com/esp32/boards/guition-jc3248w535/),
[done.land JC3248W535](https://done.land/components/microcontroller/families/esp/esp32/developmentboards/esp32-s3/guitionjc3248w535/),
[Espressif LCD FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html),
[arduino-esp32 #9348](https://github.com/espressif/arduino-esp32/issues/9348),
[ESP32-S3-BOX-3 hardware](https://github.com/espressif/esp-box/blob/master/docs/hardware_overview/esp32_s3_box_3/hardware_overview_for_box_3.md),
[SenseCAP D1S](https://thepihut.com/products/sensecap-indicator-d1s),
[MaTouch 3.5](https://www.cnx-software.com/2022/09/21/makerfabs-3-5-inch-16-bit-parallel-tft-touchscreen-display-esp32-s3-soc/).
