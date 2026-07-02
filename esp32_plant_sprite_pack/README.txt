ESP32 Plant Sprite Pack
=======================

Source: download.gif
Detected animation frames: 6
Unique stored animation poses: 3
Sequence used for playback: [0, 0, 1, 1, 2, 2]
GIF frame durations in ms: [100, 100, 100, 100, 100, 100]

Folders:
- isolated_pngs_original: transparent PNGs for every crop and every original GIF frame.
- isolated_gifs_original: each plant isolated as its own transparent-ish animated GIF.
- esp32_arduino_50pct:
  - plant_sprites.h: 50% nearest-neighbor RGB565 PROGMEM sprite arrays.
  - PlantSpriteExample.ino: Arduino_GFX example for your ESP32-S3/ST7796 pinout.
  - *_50pct.png: preview images matching the header size.

How to use:
1. Copy plant_sprites.h into your Arduino sketch folder.
2. Copy the drawPlantSprite() function from PlantSpriteExample.ino into your sketch.
3. Replace your ASCII art draw call with something like:
      drawPlantSprite(SPRITE_CUP_PLANT, 40, 120, frame);
4. Increment frame every ~100-200 ms for animation.
5. Do not clear the whole screen every frame. Clear only the sprite rectangle, then redraw.

If colors look red/blue swapped:
- Regenerate the header with the rgb565(..., bgr=True) option in the generator script,
  or tell me and I can make a BGR565 version.
