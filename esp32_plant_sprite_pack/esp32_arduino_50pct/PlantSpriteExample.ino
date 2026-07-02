#include <Arduino_GFX_Library.h>
#include "plant_sprites.h"

// Pins from your current ESP32-S3/ST7796 test sketch.
// Change these only if your display wiring changed.
#define TFT_SCK   12
#define TFT_MOSI  13
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   16

#define WHITE 0xFFFF

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1
);

Arduino_GFX *gfx = new Arduino_ST7796(
  bus, TFT_RST, 1, false, 320, 480
);

void drawPlantSprite(const PlantSprite &sprite, int16_t x, int16_t y, uint8_t frame) {
  uint8_t pose = pgm_read_byte(&sprite.sequence[frame % sprite.frameCount]);
  const uint16_t *bitmap = (const uint16_t *)pgm_read_ptr(&sprite.frames[pose]);

  gfx->startWrite();
  for (uint16_t yy = 0; yy < sprite.h; yy++) {
    for (uint16_t xx = 0; xx < sprite.w; xx++) {
      uint16_t c = pgm_read_word(&bitmap[yy * sprite.w + xx]);
      if (c != PLANT_TRANSPARENT) {
        gfx->writePixel(x + xx, y + yy, c);
      }
    }
  }
  gfx->endWrite();
}

void clearSpriteArea(const PlantSprite &sprite, int16_t x, int16_t y) {
  gfx->fillRect(x, y, sprite.w, sprite.h, WHITE);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!gfx->begin()) {
    Serial.println("Display begin failed");
    while (1) delay(100);
  }

  gfx->fillScreen(WHITE);

  // Draw all four once.
  drawPlantSprite(SPRITE_CACTUS_CLUSTER, 20, 20, 0);
  drawPlantSprite(SPRITE_WIDE_PLANTER, 150, 35, 0);
  drawPlantSprite(SPRITE_CUP_PLANT, 25, 210, 0);
  drawPlantSprite(SPRITE_SPROUT_POT, 180, 225, 0);
}

void loop() {
  static uint8_t frame = 0;
  static uint32_t last = 0;

  if (millis() - last >= 100) {
    last = millis();

    // Clear only the rectangles where the sprites are, not the whole screen.
    clearSpriteArea(SPRITE_CACTUS_CLUSTER, 20, 20);
    clearSpriteArea(SPRITE_WIDE_PLANTER, 150, 35);
    clearSpriteArea(SPRITE_CUP_PLANT, 25, 210);
    clearSpriteArea(SPRITE_SPROUT_POT, 180, 225);

    drawPlantSprite(SPRITE_CACTUS_CLUSTER, 20, 20, frame);
    drawPlantSprite(SPRITE_WIDE_PLANTER, 150, 35, frame);
    drawPlantSprite(SPRITE_CUP_PLANT, 25, 210, frame);
    drawPlantSprite(SPRITE_SPROUT_POT, 180, 225, frame);

    frame++;
  }
}
