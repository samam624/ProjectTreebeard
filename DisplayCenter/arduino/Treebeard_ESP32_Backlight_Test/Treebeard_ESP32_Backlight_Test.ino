// Bring-up test for ESP32-S3-WROOM-1U-N16R8 board: sweep the display backlight through its
// full PWM duty range so brightness -- and the resulting average voltage on the BKLED net --
// can be probed with a multimeter. Independent of the TFT_eSPI display driver: this only
// exercises the Q2 (2N7002 low-side)/Q1 (DMP3098L-7 high-side P-FET) backlight load switch,
// not the SPI data lines, so it can run standalone before the display itself is brought up.
//
// 2026-08-11: ESP_BKLED confirmed on GPIO11 (module pin 19) by direct probe with a plain
// digitalWrite hold test -- an earlier off-by-one misread of the pinout sheet had this on
// GPIO10 instead, which is actually TFT_MISO (a display data line, not the backlight).
//
// ESP_BKLED (GPIO11) drives Q2's gate through R12. Q2 ON pulls Q1's gate low (R8 is the
// passive pull-up fighting it), which turns the high-side P-FET on and connects VBUS to
// BKLED -- non-inverted: GPIO11 duty cycle maps directly to backlight duty cycle (0% = off,
// 100% = full brightness).
const int BKLED_PIN = 11;

// LED0/1/2 (GPIO40/41/42, confirmed working in Treebeard_ESP32_LED_Test.ino) driven at the same
// duty as BKLED every step -- a visual proxy for backlight brightness so it's readable at a
// glance while both hands are on multimeter probes. Perceived LED brightness vs. linear PWM
// duty is a steep, non-linear curve to the human eye (looks "stuck" near 0%/100% even though
// the duty -- and the BKLED net's actual average voltage -- is moving the whole time), so
// trust the multimeter over your eyes here.
const int LED0 = 40;
const int LED1 = 41;
const int LED2 = 42;
const int leds[] = {LED0, LED1, LED2};
const int numLeds = sizeof(leds) / sizeof(leds[0]);

const int PWM_FREQ_HZ = 1000;
const int PWM_RES_BITS = 10;
const int PWM_MAX = (1 << PWM_RES_BITS) - 1; // 1023

// Slow enough to track by hand with a multimeter probed on the BKLED net.
const uint32_t SWEEP_MS = 5000;
const uint32_t STEP_DELAY_US = (SWEEP_MS * 1000UL) / PWM_MAX;

void setup() {
  Serial.begin(115200);
  if (!ledcAttach(BKLED_PIN, PWM_FREQ_HZ, PWM_RES_BITS)) {
    Serial.println("ledcAttach failed: BKLED");
  }
  for (int i = 0; i < numLeds; i++) {
    if (!ledcAttach(leds[i], PWM_FREQ_HZ, PWM_RES_BITS)) {
      Serial.printf("ledcAttach failed: LED%d\n", i);
    }
  }
}

void sweepTo(int from, int to) {
  int step = (to > from) ? 1 : -1;
  int lastPrintedDecile = -1;
  for (int duty = from; duty != to + step; duty += step) {
    ledcWrite(BKLED_PIN, duty);
    for (int i = 0; i < numLeds; i++) {
      ledcWrite(leds[i], duty);
    }

    int decile = (duty * 10) / PWM_MAX;
    if (decile != lastPrintedDecile) {
      Serial.printf("duty %d%% (raw %d/%d)\n", (duty * 100) / PWM_MAX, duty, PWM_MAX);
      lastPrintedDecile = decile;
    }

    delayMicroseconds(STEP_DELAY_US);
  }
}

void loop() {
  sweepTo(0, PWM_MAX);     // 5s ramp up: dark -> full brightness
  sweepTo(PWM_MAX, 0);     // 5s ramp down: full brightness -> dark
}
