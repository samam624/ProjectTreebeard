# Display Center Backlight Bring-Up — 2026-08-11 Handoff

This is a handoff note for whichever Claude Code session picks up the display
receiver work next (e.g. the desktop checkout). It documents a debugging
session done on a different local machine against the ESP32-S3 display
receiver on `COM10`. **Only this doc has been pushed to `origin/main`** — the
actual firmware/header edits described below are still uncommitted, local
working-tree changes on the machine that did this session (alongside several
other unrelated in-flight edits on that checkout), so `git pull` will *not*
bring them in. Apply the changes in this doc by hand to get the same fix on
another checkout, until a real commit lands.

## TL;DR

The display panel was rendering correctly over SPI but showing nothing —
**not even backlight** — because the board's backlight wiring changed from
"tied to 3.3V/5V, no GPIO needed" to "switched through a GPIO-gated FET,"
and the firmware was never updated to drive that GPIO. Fix: drive GPIO11
high during display init. Also corrected a stale SPI pin mapping (CS/DC/RST)
that predated the current wiring revision.

## Root cause

1. **Backlight is no longer hardwired.** Current board wiring switches the
   backlight through Q1 (`DMP3098L-7`, high-side P-FET) gated by Q2
   (`2N7002`, low-side), driven by `ESP_BKLED` on **GPIO11** (module pin 19).
   Mapping is non-inverted: GPIO11 HIGH/duty maps directly to backlight
   brightness. This was confirmed by direct bench probe today
   (2026-08-11) — an earlier pinout read had `ESP_BKLED` on GPIO10 instead,
   which is actually `TFT_MISO`, a display data line, not the backlight.
   The `TFT_eSPI_User_Setup_Treebeard_ESP32S3.h` comment previously said
   "Backlight is tied to 3.3V/5V in the current wiring, so TFT_eSPI does not
   control it" — that's now false for this board revision.
2. Neither the display-only smoke-test path (`TREEBEARD_BRINGUP_STAGE == 1`) nor
   the full runtime's display-init path in
   `Treebeard_ESP32_Display_Runtime.ino` ever drove GPIO11. `Arduino_GFX` (the
   library actually used at runtime — the `TFT_eSPI` header exists mainly as
   a pin-mapping reference doc) doesn't touch a backlight pin on its own, so
   SPI init/draw completed successfully every time and the panel just sat
   dark.
3. Separately: the ST7796 CS/DC/RST pin assignment hardcoded in the `.ino`
   (and mirrored in the `TFT_eSPI` header) was stale relative to the current
   wiring — it had `CS=14, DC=15, RST=16`, should be `DC=14, RST=15, CS=16`.
   This didn't block rendering (SPI still worked, just probably luck/tolerant
   wiring or previously-untested path) but should be corrected together with
   the backlight fix.

## Exact changes needed on another checkout

### 1. `arduino/Treebeard_ESP32_Display_Runtime/TFT_eSPI_User_Setup_Treebeard_ESP32S3.h`

```diff
-#define TFT_MISO -1
+#define TFT_MISO 10
 #define TFT_MOSI 13
 #define TFT_SCLK 12
-#define TFT_CS   14
-#define TFT_DC   15
-#define TFT_RST  16
-
-// Backlight is tied to 3.3V/5V in the current wiring, so TFT_eSPI does not
-// control it. If you later move LED/BL to a GPIO, set that pin here.
-#define TFT_BL   -1
+#define TFT_DC   14
+#define TFT_RST  15
+#define TFT_CS   16
+
+// Backlight is switched via Q1 (DMP3098L-7 high-side P-FET), gated by Q2
+// (2N7002) off GPIO11 -- see Treebeard_ESP32_Backlight_Test for the bench-verified
+// PWM sweep. Non-inverted: duty cycle maps directly to backlight brightness.
+#define TFT_BL   11
```

### 2. `arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino`

Two separate spots need the same backlight-enable added: the stage-1
display-only smoke test, and the full runtime's real display init. Also
correct the SPI pin constants/args to match the header above.

**a) Stage-1 smoke test (`#if TREEBEARD_DISPLAY_TEST` block in `setup()`):**

```diff
 #if TREEBEARD_DISPLAY_TEST
+  // Backlight is gated by Q2 (2N7002) on GPIO11, non-inverted -- see
+  // Treebeard_ESP32_Backlight_Test. Arduino_GFX doesn't drive this pin itself,
+  // so it has to be switched on explicitly or the panel stays dark even
+  // though SPI init/draw succeeds.
+  Serial.println("[Treebeard display test] enabling backlight (GPIO11)");
+  pinMode(11, OUTPUT);
+  digitalWrite(11, HIGH);
+
   Serial.println("[Treebeard display test] initializing Arduino_GFX");
-  Arduino_DataBus* testBus = new Arduino_ESP32SPI(15, 14, 12, 13, -1);
-  Arduino_GFX* testGfx = new Arduino_ST7796(testBus, 16, 1, false, 320, 480);
+  Arduino_DataBus* testBus = new Arduino_ESP32SPI(14, 16, 12, 13, -1);
+  Arduino_GFX* testGfx = new Arduino_ST7796(testBus, 15, 1, false, 320, 480);
```

**b) Pin constants (global scope, `#if TREEBEARD_ENABLE_DISPLAY`):**

```diff
 const int TFT_SCK = 12;
 const int TFT_MOSI = 13;
-const int TFT_CS = 14;
-const int TFT_DC = 15;
-const int TFT_RST = 16;
+const int TFT_DC = 14;
+const int TFT_RST = 15;
+const int TFT_CS = 16;
```

**c) Full runtime display init (`#if TREEBEARD_ENABLE_DISPLAY` block in the real
`setup()`, right before `busPtr = new Arduino_ESP32SPI(...)`):**

```diff
 #if TREEBEARD_ENABLE_DISPLAY
+  // Backlight is gated by Q2 (2N7002) on GPIO11, non-inverted -- see
+  // Treebeard_ESP32_Backlight_Test. Arduino_GFX doesn't drive this pin itself,
+  // so it has to be switched on explicitly or the panel stays dark even
+  // though SPI init/draw succeeds.
+  bootLog("enabling backlight (GPIO11)");
+  pinMode(11, OUTPUT);
+  digitalWrite(11, HIGH);
+
   bootLog("initializing display");
   busPtr = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1);
```

`TREEBEARD_BRINGUP_STAGE` should be left at (or restored to) `3` — full runtime
with BLE scanning — which is the shipped default. It only needs to be
dropped to `1` temporarily if you're isolating a display-only problem again.

### 3. Toolchain: `ArduinoJson` library may be missing

Compiling anything at `TREEBEARD_BRINGUP_STAGE >= 2` requires `ArduinoJson.h`. If
`arduino-cli compile` fails with `ArduinoJson.h: No such file or directory`,
install it:

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" lib install "ArduinoJson"
```

This machine ended up on `ArduinoJson@7.4.3`. `FS`, `GFX Library for
Arduino`, `SPI`, and `Wire` were already present.

### 4. New bench-test sketches (not yet pushed)

Two standalone hardware bring-up sketches were added this session, untracked
so far. They're independent of the `TFT_eSPI`/`Arduino_GFX` display driver
and only exercise raw GPIOs, so they're useful for verifying backlight/LED/
buzzer hardware in isolation before touching the display driver at all.
Recreate them locally if useful — full contents below.

**`arduino/Treebeard_ESP32_Backlight_Test/Treebeard_ESP32_Backlight_Test.ino`** — sweeps
the backlight FET through its full PWM range via GPIO11 so brightness (and
BKLED net voltage) can be probed with a multimeter, using LED0/1/2 as a
visual proxy:

```cpp
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
```

**`arduino/Treebeard_ESP32_LED_Test/Treebeard_ESP32_LED_Test.ino`** — chases
LED0→LED1→LED2 and exercises the piezo buzzer (LS1) via `ledcWrite`, never
`digitalWrite`, per the datasheet's DC-bias warning:

```cpp
// Bring-up test for ESP32-S3-WROOM-1U-N16R8 board: chase LED0 -> LED1 -> LED2, then beep LS1.
//
// LS1 is a TDK PS1440P02BT piezo buzzer: rated 3V, 4kHz resonant frequency,
// 75dB min SPL @ 3V/4kHz. Its datasheet warns against applying DC bias (can
// degrade insulation resistance over time), so it must only ever be driven
// with tone()/noTone() (AC square wave through Q3), never digitalWrite HIGH.
const int LED0 = 40;
const int LED1 = 41;
const int LED2 = 42;
const int BUZZER = 38;
const int BUZZER_RESONANT_HZ = 4000;

// Volume control: LEDC duty cycle instead of tone()'s fixed 50%. Lower duty ->
// less average voltage swing across LS1 -> quieter. Tune to taste.
const int BUZZER_DUTY_PERCENT = 15;
const int BUZZER_PWM_RES_BITS = 8;
const int BUZZER_PWM_MAX = (1 << BUZZER_PWM_RES_BITS) - 1;

const int leds[] = {LED0, LED1, LED2};
const int numLeds = sizeof(leds) / sizeof(leds[0]);

void beep(int durationMs, int freqHz = BUZZER_RESONANT_HZ, int dutyPercent = BUZZER_DUTY_PERCENT) {
  int duty = (BUZZER_PWM_MAX * dutyPercent) / 100;
  ledcWrite(BUZZER, duty);
  delay(durationMs);
  ledcWrite(BUZZER, 0);
}

void setup() {
  for (int i = 0; i < numLeds; i++) {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], LOW);
  }
  ledcAttach(BUZZER, BUZZER_RESONANT_HZ, BUZZER_PWM_RES_BITS);
}

void loop() {
  for (int i = 0; i < numLeds; i++) {
    digitalWrite(leds[i], HIGH);
    delay(300);
    digitalWrite(leds[i], LOW);
  }

  // Buzzer disabled for now.
  // beep(150);
  // delay(100);
  // beep(150);

  delay(500);
}
```

## Session narrative (what actually happened, in order)

1. Board on `COM10` wasn't being picked up; a reboot of the host machine
   fixed device detection (no firmware cause identified — treated as a host
   USB/driver hiccup, not a board defect).
2. Reflashed `Treebeard_ESP32_Display_Runtime` at `TREEBEARD_BRINGUP_STAGE 1`
   (display-only smoke test, already staged as the checkout's working state
   before this session started). Serial capture confirmed
   `begin()`/drawing/`init/draw complete` all completed successfully and the
   safe-boot heartbeat loop ran cleanly — but the physical panel showed
   nothing, not even backlight.
3. Diffed the working tree and found `TFT_eSPI_User_Setup_Treebeard_ESP32S3.h`
   already had an uncommitted note that the backlight wiring changed to a
   GPIO-gated FET (GPIO11, confirmed by bench probe earlier the same day),
   but the `.ino`'s actual `setup()` paths had never been updated to
   actually drive that pin — see [Root cause](#root-cause) above.
4. Added the GPIO11 backlight-enable to the stage-1 smoke test, reflashed,
   and the user visually confirmed text was now visible on the panel.
5. Restored `TREEBEARD_BRINGUP_STAGE` to `3` (the committed default — full
   runtime with WiFi + BLE) and added the identical backlight-enable fix to
   the full runtime's real display-init path, since it had the same gap.
6. First compile attempt at stage 3 failed: `ArduinoJson.h: No such file or
   directory` — this machine's Arduino library set was missing it. Installed
   via `arduino-cli lib install "ArduinoJson"` (landed on 7.4.3).
7. Recompiled and flashed successfully: 2,088,960 bytes written, 66% of
   program storage, 16% of dynamic memory. Board re-enumerated cleanly on
   `COM10`. Serial log during `setup()` was as expected; `loop()` prints
   nothing by design once WiFi/BLE are idle, which is normal (not a hang) —
   don't mistake a quiet serial capture on the full runtime for a boot
   failure the way the stage-1 build's constant heartbeat print might
   condition you to expect.
8. User visually confirmed the full runtime is rendering correctly on the
   physical panel.

## Status of these changes in git

Everything above except this doc is still **local, uncommitted working-tree
state** on the machine that did this session — intentionally not pushed
alongside several other unrelated in-flight changes on that checkout. If
you're reading this from a different checkout, `git pull` will not give you
any of it; apply the diffs in this doc by hand, or wait for a future commit
that actually lands them.
