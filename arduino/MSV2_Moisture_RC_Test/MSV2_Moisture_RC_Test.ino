/*********************************************************************
  MSV2 capacitive moisture sensor RC discharge test.

  Sensing method: precharge the sensing node to HIGH, then switch the pin
  to INPUT (high-Z) and time how long it takes to fall back to LOW on its
  own. The sensing node's effective RC time constant shifts with the
  dielectric constant of whatever is against the probe (soil moisture),
  which is what this measures. No moisture math yet - this sketch just
  proves the read loop works: prints raw fall time over serial each
  reading, and flashes the external LED once a valid reading completes.

  Also reads LiPo battery voltage through the XIAO nRF52840's on-board
  divider (PIN_VBAT / VBAT_ENABLE, defined in the board variant), so the
  reading loop below can eventually feed the same battery percent field
  the BLE payload in MSV2_Moisture_BLE_Sleep sends.

  Wiring:
    D0 (IO0) -> sensing node (probe pad / RC network)
    D1 (IO1) -> external LED (through a current-limiting resistor to GND)
    Battery -> XIAO's onboard JST connector (no extra wiring needed)

  Board: Seeeduino:nrf52:xiaonRF52840, e.g.
    powershell -ExecutionPolicy Bypass -File scripts\arduino_probe.ps1 upload -Port COM5 -Sketch arduino/MSV2_Moisture_RC_Test
    powershell -ExecutionPolicy Bypass -File scripts\arduino_probe.ps1 monitor -Port COM5
*********************************************************************/

#include <Adafruit_TinyUSB.h> // needed to link native USB CDC Serial on the nRF52840

const int SENSE_PIN = D0;
const int LED_PIN = D1;
const bool LED_ACTIVE_HIGH = true; // external LED: HIGH turns it on. Flip if wired inverted.

// A verify-via-analogRead precharge loop was tried and reverted: checking the charge
// level means releasing the pin to INPUT to read it, which starts the very discharge
// it's trying to avoid - if the ADC call's own overhead is comparable to the charge
// burst, the loop nets out *worse* than a plain fixed delay (confirmed: it read
// moisture=0 every cycle on MSV2_Moisture_BLE_Sleep). A long, uninterrupted precharge
// is the sound approach here. 10ms is the validated value - 20ms was tried for extra
// margin but broke touch response (reading collapsed far below baseline on contact
// instead of rising). Kept in sync with MSV2_Moisture_BLE_Sleep.
const uint32_t PRECHARGE_US = 10000;    // time to hold SENSE_PIN HIGH before releasing it to input
const uint32_t FALL_TIMEOUT_US = 50000; // give up waiting for the fall after this long
const uint8_t SAMPLES_PER_READING = 8;  // averaged per printed reading
const uint32_t READING_PERIOD_MS = 1000;

// 3.0V ADC reference, 12-bit resolution: 3000mV / 4096 counts.
#define VBAT_MV_PER_LSB (0.73242188F)
// The Bluefruit52Lib adc_vbat example assumes a 150K+150K divider (comp=2.0), which is
// wrong for this board. Per the actual XIAO nRF52840 Sense schematic: BAT -> R1(1M, 1%)
// -> P0.31/AIN7_BAT (the ADC read node) -> R2(510K, 1%) -> P0.14/READ_BAT, and P0.14 is
// driven LOW (sink) to complete the divider to ground while sampling. Nominal ratio =
// 510K/(1M+510K) = 0.3377, i.e. theoretical comp = 2.961. Empirically calibrated against
// a multimeter reading of 4200mV on B+/B- (this board read battery_raw_counts~1947,
// battery_mv~1426 at comp=1.0, so comp = 4200/1426 = 2.946) - within 0.5% of the
// schematic's nominal value, i.e. just normal 1%-resistor tolerance. Per-unit divider
// tolerance means this may need rechecking on other physical boards - recalibrate with:
// VBAT_DIVIDER_COMP = knownMultimeterMv / (battery_raw_counts * VBAT_MV_PER_LSB)
#define VBAT_DIVIDER_COMP (2.946F)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

void ledWrite(bool on)
{
  digitalWrite(LED_PIN, on == LED_ACTIVE_HIGH ? HIGH : LOW);
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  ledWrite(false);

  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000)
  {
    // Give the host up to 3s to open the port without hanging forever if nothing connects.
  }

  Serial.println("MSV2 moisture RC sensor test");
  Serial.print("Sense pin: D0, LED pin: D1, precharge_us=");
  Serial.print(PRECHARGE_US);
  Serial.print(", timeout_us=");
  Serial.println(FALL_TIMEOUT_US);

  // VBAT_ENABLE/PIN_VBAT come from the board variant header; the variant's
  // init already sets VBAT_ENABLE to OUTPUT HIGH (divider off) at boot.
  readBatteryMv(nullptr); // throw away the first sample while the ADC reference settles
}

// Precharges SENSE_PIN high, releases it to input, and times the fall to LOW.
// Returns the fall time in microseconds, or -1 if it never fell within the timeout.
long readFallTimeUs()
{
  pinMode(SENSE_PIN, OUTPUT);
  digitalWrite(SENSE_PIN, HIGH);
  delayMicroseconds(PRECHARGE_US);

  pinMode(SENSE_PIN, INPUT);
  uint32_t start = micros();

  while (digitalRead(SENSE_PIN) == HIGH)
  {
    if (micros() - start > FALL_TIMEOUT_US)
    {
      return -1;
    }
  }

  return (long)(micros() - start);
}

// Reads LiPo voltage in millivolts via the XIAO's onboard VBAT divider.
// VBAT_ENABLE gates the divider so it only draws current while sampling.
// rawCountsOut (optional) returns the raw 12-bit ADC counts, useful for calibration.
float readBatteryMv(float *rawCountsOut)
{
  digitalWrite(VBAT_ENABLE, LOW);
  delay(1); // let the divider settle

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(1); // let the ADC settle after switching reference

  float raw = analogRead(PIN_VBAT);

  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  digitalWrite(VBAT_ENABLE, HIGH); // disable the divider again to save power

  if (rawCountsOut != nullptr)
  {
    *rawCountsOut = raw;
  }

  return raw * REAL_VBAT_MV_PER_LSB;
}

// Rough LiPo mV -> percent curve (same shape as the Bluefruit52Lib adc_vbat example).
uint8_t batteryMvToPercent(float mvolts)
{
  if (mvolts < 3300)
  {
    return 0;
  }

  if (mvolts < 3600)
  {
    return (uint8_t)((mvolts - 3300) / 30);
  }

  float percent = 10 + (mvolts - 3600) * 0.15F;
  return (uint8_t)(percent > 100 ? 100 : percent);
}

void loop()
{
  uint32_t sum = 0;
  uint8_t validSamples = 0;

  for (uint8_t i = 0; i < SAMPLES_PER_READING; i++)
  {
    long fallUs = readFallTimeUs();
    if (fallUs >= 0)
    {
      sum += (uint32_t)fallUs;
      validSamples++;
    }
    delay(5); // let the node fully settle before recharging
  }

  Serial.print("samples=");
  Serial.print(validSamples);
  Serial.print("/");
  Serial.print(SAMPLES_PER_READING);

  if (validSamples > 0)
  {
    uint32_t avgUs = sum / validSamples;
    Serial.print(" avg_fall_us=");
    Serial.print(avgUs);
    Serial.println(" sensor OK");

    // Confirm a working sensor with a quick blink.
    ledWrite(true);
    delay(80);
    ledWrite(false);
  }
  else
  {
    Serial.println(" no fall detected within timeout - check wiring on D0");

    // Distinct fast triple-blink so a failure is visible without opening the serial monitor.
    for (uint8_t i = 0; i < 3; i++)
    {
      ledWrite(true);
      delay(60);
      ledWrite(false);
      delay(60);
    }
  }

  float batteryRawCounts = 0;
  float batteryMv = readBatteryMv(&batteryRawCounts);
  uint8_t batteryPercent = batteryMvToPercent(batteryMv);
  Serial.print("battery_raw_counts=");
  Serial.print(batteryRawCounts, 1);
  Serial.print(" battery_mv=");
  Serial.print(batteryMv, 2);
  Serial.print(" battery_pct=");
  Serial.println(batteryPercent);

  delay(READING_PERIOD_MS);
}
