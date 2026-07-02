// TFT_eSPI setup for the MSV2 ESP32-S3 display wiring.
//
// Important: TFT_eSPI compiles its own .cpp files, so including this only from
// the sketch is not enough in Arduino IDE. Copy this file over the TFT_eSPI
// library's User_Setup.h, or include it from the library's User_Setup_Select.h.

#pragma once

#define USER_SETUP_LOADED

#define USER_SETUP_INFO "MSV2 ESP32-S3 SPI display"

// Your working Arduino_GFX test uses Arduino_ST7796 at 320x480, so TFT_eSPI
// should use the matching ST7796 driver here.
#define ST7796_DRIVER
// #define ST7789_DRIVER
// #define ILI9341_DRIVER
// #define GC9A01_DRIVER

// Portrait panel resolution.
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// ESP32-S3 wiring.
#define USE_HSPI_PORT

#define TFT_MISO -1
#define TFT_MOSI 13
#define TFT_SCLK 12
#define TFT_CS   14
#define TFT_DC   15
#define TFT_RST  16

// Backlight is tied to 3.3V/5V in the current wiring, so TFT_eSPI does not
// control it. If you later move LED/BL to a GPIO, set that pin here.
#define TFT_BL   -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       10000000
#define SPI_READ_FREQUENCY  10000000
