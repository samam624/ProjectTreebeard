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
