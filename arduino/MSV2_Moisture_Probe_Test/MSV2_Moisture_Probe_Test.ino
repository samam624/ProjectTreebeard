/*********************************************************************
  MSV2 moisture probe bench test (Seeed XIAO nRF52840).

  Free-running RC relaxation oscillator, modeled directly on the 555
  astable architecture validated in the MSV1 proof-of-concept paper
  (Amezcua, "Design and Experimental Characterization of a Capacitive
  Soil Moisture Sensor Using a 555 Timer Architecture"). Charge until
  HIGH, immediately discharge until LOW, immediately recharge, repeat,
  continuously -- count how many discharge-complete transitions happen
  in a fixed window and derive a frequency, exactly analogous to
  counting the 555 astable's output edges. Lower frequency = wetter
  (higher effective capacitance).

  Resistor: 100k pulldown on the sensing node (IO0 / A0).

  LOW-POWER PERIODIC MODE: runs as a wake/measure/sleep cycle instead of
  a continuous loop, mirroring MSV2_Moisture_BLE_Sleep.ino's pattern --
  the oscillator (and the CPU driving it) is only active during the
  brief measurement burst each wake. The red LED on A1 is lit for the
  exact duration of each burst, so you can see (or scope) precisely how
  long the active-power window is.

  MEDIAN-OF-5 + SHORT BURSTS: an earlier window sweep (50/20/10/5/2/1ms)
  showed that shrinking the burst below ~10ms starts producing
  occasional large outlier spikes (e.g. one single 2ms reading came
  back 67% high) -- almost certainly rare RTOS/scheduler tick jitter
  delaying the burst's own millis()-based cutoff check, not a real
  moisture change. A ~1ms delay is invisible on a 50ms burst but a huge
  fraction of a 2-5ms one. Rather than lengthen the burst back out to
  dodge that, each report now takes 5 independent SAMPLE_WINDOW_MS
  bursts and reports the MEDIAN cycle count -- a median-of-5 silently
  drops up to 2 outliers per report, so a rare single-sample glitch
  never reaches the output. This lets the per-sample burst itself stay
  very short (SAMPLE_WINDOW_MS) while staying immune to that jitter.
  Total active time per report is still only
  MEDIAN_SAMPLES * SAMPLE_WINDOW_MS -- far below the original 50ms.

  Open Serial Monitor or Serial Plotter at 115200 baud.

  Battery + charge monitoring (XIAO nRF52840 Sense only):
  The Sense variant has an onboard BQ25100 charger and a VBAT divider
  behind a protection switch. Pin names below come straight from this
  core's variant.h/variant.cpp for Seeed_XIAO_nRF52840_Sense:
    - VBAT_ENABLE (D14/P0.14): drive LOW to connect the VBAT divider to
      PIN_VBAT, HIGH to disconnect it. The core leaves this HIGH
      (disconnected) at boot specifically because leaving the divider
      connected wastes sleep current and, per Seeed's own battery
      charging notes, driving this pin HIGH while it is also configured
      as an analog input can push PIN_VBAT toward its 3.6V limit. So it
      is only pulled LOW for the brief moment of each reading.
    - PIN_VBAT (D32/P0.31): ADC input for the divided battery voltage.
      The onboard divider is a 1M/510k pair (ratio (1000k+510k)/510k),
      so real battery voltage = ADC pin voltage * VBAT_DIVIDER_RATIO.
    - D23/P0.17 (BQ25100 ~CHG, no core macro): open-drain, active LOW
      while the BQ25100 is actively charging the battery.
    - PIN_CHARGING_CURRENT (D22/P0.13, BQ25100 HICHG) selects the BQ25100
      charge current (LOW = 100mA, HIGH = 50mA) and is left floating by
      this core's default init, so charge current follows the chip's
      own default. Not overridden here since the right value depends on
      your battery's mAh rating.
*********************************************************************/

#include <Adafruit_TinyUSB.h> // required on this core for Serial over USB

const int MOISTURE_PIN = A0; // IO0 on the XIAO silkscreen.
const int LED_PIN = A1;      // external red LED, HIGH = on (flip if wired active-low)

// Sleep duration between moisture test bursts.
const uint32_t WAKE_INTERVAL_MS = 3000;

// Per-sample burst duration and how many samples feed the median. Total
// active time per report is MEDIAN_SAMPLES * SAMPLE_WINDOW_MS = 5ms.
const uint32_t SAMPLE_WINDOW_MS = 1;
const uint8_t MEDIAN_SAMPLES = 5;

// Safety cap per half-cycle so a disconnected/open probe (pin never
// reads LOW) can't hang a burst forever.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL; // 5 ms

float smoothedHz = -1.0f;
float sessionMinHz = 1.0e9f;
float sessionMaxHz = 0.0f;

const int CHG_STATUS_PIN = 23; // D23/P0.17, BQ25100 ~CHG, active LOW.
const uint8_t VBAT_ADC_BITS = 12;
const float VBAT_ADC_REF_MV = 3600.0f; // AR_INTERNAL = 0.6V * 6
const float VBAT_DIVIDER_RATIO = (1000.0f + 510.0f) / 510.0f; // onboard 1M/510k divider

void setup()
{
  Serial.begin(115200);

  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000)
  {
    delay(10);
  }

  analogReference(AR_INTERNAL);
  analogReadResolution(VBAT_ADC_BITS);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH); // divider disconnected except while sampling
  pinMode(CHG_STATUS_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("MSV2 moisture probe bench test (periodic, median-of-5)");
  Serial.println("Pin: A0 (IO0), 100k pulldown. LED: A1, on during each burst.");
  Serial.println("raw_cycles(5),median_cycles,freq_hz,smoothed_hz,percent_of_session_range");
}

void loop()
{
  digitalWrite(LED_PIN, HIGH); // on for the entire measurement burst below

  uint32_t samples[MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    samples[i] = countOscCycles(SAMPLE_WINDOW_MS);
  }

  Serial.print("raw:");
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    Serial.print(samples[i]);
    Serial.print(i + 1 < MEDIAN_SAMPLES ? " " : ",");
  }

  uint32_t medianCycles = medianOf(samples, MEDIAN_SAMPLES);
  float freqHz = (float)medianCycles * 1000.0f / (float)SAMPLE_WINDOW_MS;

  if (smoothedHz < 0)
  {
    smoothedHz = freqHz;
  }
  else
  {
    smoothedHz += 0.2f * (freqHz - smoothedHz);
  }

  if (freqHz < sessionMinHz) sessionMinHz = freqHz;
  if (freqHz > sessionMaxHz) sessionMaxHz = freqHz;

  // Auto-scaled percent relative to what this session has seen so far,
  // inverted since lower frequency = wetter: 100% = closest to the
  // wettest thing seen this session, 0% = closest to the driest.
  float percent = 0.0f;
  if (sessionMaxHz > sessionMinHz)
  {
    percent = 100.0f * (sessionMaxHz - smoothedHz) / (sessionMaxHz - sessionMinHz);
  }

  Serial.print(medianCycles);
  Serial.print(",");
  Serial.print(freqHz, 1);
  Serial.print(",");
  Serial.print(smoothedHz, 1);
  Serial.print(",");
  Serial.println(percent, 1);

  digitalWrite(LED_PIN, LOW); // off while sleeping

  reportBattery();

  delay(WAKE_INTERVAL_MS); // sleep until the next wake
}

void reportBattery()
{
  digitalWrite(VBAT_ENABLE, LOW); // connect the divider
  delayMicroseconds(200);         // let it settle
  uint16_t raw = analogRead(PIN_VBAT);
  digitalWrite(VBAT_ENABLE, HIGH); // disconnect again

  float pinMv = raw * (VBAT_ADC_REF_MV / (float)(1UL << VBAT_ADC_BITS));
  float batteryMv = pinMv * VBAT_DIVIDER_RATIO;
  bool charging = digitalRead(CHG_STATUS_PIN) == LOW;

  Serial.print("battery_mV=");
  Serial.print(batteryMv, 0);
  Serial.print(" charging=");
  Serial.println(charging ? 1 : 0);
}

// Free-running relaxation oscillator: charge until HIGH, immediately
// discharge until LOW, immediately recharge, repeat, for windowMs.
// Returns the number of complete HIGH->LOW discharge cycles observed.
uint32_t countOscCycles(uint32_t windowMs)
{
  uint32_t windowStartMs = millis();
  uint32_t cycles = 0;

  while (millis() - windowStartMs < windowMs)
  {
    pinMode(MOISTURE_PIN, OUTPUT);
    digitalWrite(MOISTURE_PIN, HIGH);

    pinMode(MOISTURE_PIN, INPUT);
    uint32_t waitStartUs = micros();
    while (digitalRead(MOISTURE_PIN) == HIGH)
    {
      if (micros() - waitStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        break; // disconnected/open probe -- bail this half-cycle
      }
    }

    cycles++;
  }

  return cycles;
}

// In-place insertion sort + middle element. Fine for the small, fixed
// sample counts used here (no need for anything fancier than O(n^2)).
uint32_t medianOf(uint32_t vals[], uint8_t n)
{
  for (uint8_t i = 1; i < n; i++)
  {
    uint32_t key = vals[i];
    int8_t j = i - 1;
    while (j >= 0 && vals[j] > key)
    {
      vals[j + 1] = vals[j];
      j--;
    }
    vals[j + 1] = key;
  }
  return vals[n / 2];
}
