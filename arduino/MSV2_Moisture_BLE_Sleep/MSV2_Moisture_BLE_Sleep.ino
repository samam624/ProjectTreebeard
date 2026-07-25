/*********************************************************************
  Periodic BLE advertising sensor test for Adafruit nRF52 Bluefruit.

  This uses a timed low-power System ON sleep pattern. It is suitable for
  periodic wakeups. The deepest nRF52 System OFF sleep generally cannot wake
  itself from a normal internal timer; it wakes from reset, GPIO, NFC, or
  similar external events.
*********************************************************************/
#include "Adafruit_SPIFlash.h"
#include <bluefruit.h>

const char DEVICE_NAME[] = "MSV2-MOISTURE-001";
const uint16_t MSV2_COMPANY_ID = 0xFFFF; // Test/development company id.
const uint8_t SENSOR_ID = 2; // Change to 2 before uploading to the second sender XIAO.

// Measure/check cadence - cheap (a few ms of oscillator sampling), so this can run
// often without much battery cost. Actually advertising over BLE is the expensive part
// (radio on, ~ADVERTISE_MS awake) so it happens far less often - see shouldAdvertise
// in loop() below. Matches the "measure often, radio rarely" pattern sketched in
// docs/PRODUCT_ROADMAP.md's probe section, tightened to this test's own numbers.
const uint32_t WAKE_PERIOD_MS = 10000;
// Normal BLE advertise cadence when nothing eventful is happening.
const uint32_t ADVERTISE_INTERVAL_MS = 60000;
// If moisture moves at least this much between checks (e.g. a watering event, or the
// probe being pulled out), advertise immediately instead of waiting for the next
// scheduled interval - matches PRODUCT_ROADMAP.md's "if moisture changed meaningfully"
// condition. ~27% of the default 120-175 calibration range (see MOISTURE_INVERT_CEILING
// below) - big enough to skip normal sample-to-sample noise, small enough to catch a
// real watering/removal event within one 10s check.
const int MOISTURE_SWING_THRESHOLD = 15;
const uint32_t ADVERTISE_MS = 500;
const uint32_t LED_PULSE_MS = 80;
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = false; // onboard RGB disabled for tonight's battery-impact test - see EXTERNAL_LED_PIN below
const bool ENABLE_SERIAL_LOGS = false; // disabled for the unattended overnight run - flip back to true to debug

// External LED on D1: no longer a per-wake/per-advertise heartbeat (that cost battery
// for no real benefit) - now purely a low-moisture alert, blinking 3x every wake while
// moisture is low. LOW_MOISTURE_THRESHOLD is a FIXED cutoff, not the live auto-learned
// or per-plant manual range configured on the receiver - this probe is advertise-only
// (no BLE receive path), so it has no way to learn either of those without a real
// architecture change. Matches the same 25%-of-default-range "danger" cutoff already
// used everywhere else in this project (MSV2_ESP32_Display_Runtime.ino's healthColor()),
// off the default dry=120/wet=175 calibration scale: 120 + 0.25*(175-120) = 134.
const int EXTERNAL_LED_PIN = D1;
const bool EXTERNAL_LED_ACTIVE_HIGH = true;
const bool ENABLE_EXTERNAL_LED = true;
const int LOW_MOISTURE_THRESHOLD = 134;

// Moisture sensing node: free-running RC relaxation oscillator, not one-shot timing.
// The earlier precharge/discharge-timing approach (see git history) read cleanly on
// the bench but saturated once actually inserted in soil - no resistor value made a
// single-shot timed discharge survive real, variable soil conductivity. Replaced with
// the architecture validated in MSV2_Moisture_Probe_Test.ino and
// docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md: charge until HIGH, immediately discharge
// until LOW, immediately recharge, repeat continuously, and count cycles in a fixed
// window - the same free-running-astable-plus-frequency-counting principle as this
// project's own MSV1 555-timer proof-of-concept. Requires a 100k pulldown resistor
// populated on this node (the earlier unpopulated/DNC footprint was part of why the
// old approach never stabilized).
const int SENSE_PIN = D0;
const uint32_t OSC_SAMPLE_WINDOW_MS = 1;
const uint8_t OSC_MEDIAN_SAMPLES = 5; // 5 x 1ms bursts, median-of-5 rejects scheduler-jitter outliers
// Safety cap per half-cycle so a disconnected/open probe can't hang a burst.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL;
// Generous headroom above the ~10-90kHz range observed so far - see the inversion
// comment in readAveragedMoisture() below.
const uint16_t MOISTURE_INVERT_CEILING = 200;

// 3.0V ADC reference, 12-bit resolution: 3000mV / 4096 counts.
#define VBAT_MV_PER_LSB (0.73242188F)
// XIAO nRF52840 Sense VBAT divider: BAT -> R1(1M) -> P0.31/AIN7_BAT -> R2(510K) -> P0.14 (sunk
// low to enable). Nominal comp = 2.961; calibrated to 2.946 against a multimeter on this board
// (see MSV2_Moisture_RC_Test for the derivation) - within 1%-resistor tolerance of nominal.
#define VBAT_DIVIDER_COMP (2.946F)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

// Unused for tonight's test (timed wake/send/sleep only) - commented out rather than
// deleted since true System OFF deep sleep is real future work for the 30-min production
// cadence. Re-enabling needs WAKE_PIN moved off A0 first (see note below on why).
//
// #define USE_SYSTEM_OFF_AFTER_SEND 1
//
// // In System OFF mode, connect WAKE_PIN to GND to wake/reset the nRF52840.
// // Change this to a pin driven by an external RTC/timer/sensor interrupt for
// // periodic true-deep-sleep wakeups.
// // NOTE: A0 and D0 are the same physical pin (0), which is also SENSE_PIN above -
// // move this off A0 before ever enabling System OFF mode on this board.
// const int WAKE_PIN = A0;

#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
#define HAS_EXTERNAL_FLASH 1
#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);
Adafruit_SPIFlash flash(&flashTransport);
#define HAS_EXTERNAL_FLASH 1
#else
#define HAS_EXTERNAL_FLASH 0
#endif

// GATT/peripheral-connection services (DFU, device info, battery) commented out - this
// design only ever advertises (BLEScan on the receiver side, never connects), so none of
// this connectable-peripheral machinery is reachable or useful. Battery is already sent
// in our own manufacturer data (see startAdv), making BLEBas redundant too.
// BLEDfu bledfu;
// BLEDis bledis;
// BLEBas blebas;

uint32_t wakeCount = 0; // counts actual advertises, not every measure/check wake
uint32_t lastAdvertiseMs = 0;
int lastAdvertisedMoisture = -1;

void setup()
{
  Serial.begin(115200);

  setupStatusLeds();
  ledsOff();
  pinMode(EXTERNAL_LED_PIN, OUTPUT);
  externalLedWrite(false);
  powerDownExternalFlash();

  Bluefruit.autoConnLed(false);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);

  // GATT/peripheral-connection setup commented out - see the bledfu/bledis/blebas note
  // above. Advertise-only doesn't need a peripheral bandwidth config or connect callbacks.
  // Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  // Bluefruit.Periph.setConnectCallback(connect_callback);
  // Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  // bledfu.begin();
  // bledis.setManufacturer("Adafruit Industries");
  // bledis.setModel("MSV2 Moisture Test");
  // bledis.begin();
  // blebas.begin();
  // blebas.write(100);

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.println("MSV2 BLE advertising sensor ready");
    Serial.print("Sensor ID: ");
    Serial.println(SENSOR_ID);
  }

  readBatteryMv(); // throw away the first sample while the ADC reference settles
}

void loop()
{
  uint32_t cycleStartMs = millis();
  int moisture = readAveragedMoisture();
  uint8_t batteryPercent = readSmoothedBatteryPercent();

  bool intervalElapsed = lastAdvertiseMs == 0 || (millis() - lastAdvertiseMs >= ADVERTISE_INTERVAL_MS);
  bool bigSwing = lastAdvertisedMoisture >= 0 && abs(moisture - lastAdvertisedMoisture) >= MOISTURE_SWING_THRESHOLD;
  bool shouldAdvertise = intervalElapsed || bigSwing;

  if (shouldAdvertise)
  {
    wakeCount++;

    if (ENABLE_SERIAL_LOGS)
    {
      Serial.print("Sensor ");
      Serial.print(SENSOR_ID);
      Serial.print(" wake ");
      Serial.print(wakeCount);
      Serial.print(bigSwing ? " (swing) advertising for " : " advertising for ");
      Serial.print(ADVERTISE_MS);
      Serial.print(" ms moisture=");
      Serial.print(moisture);
      Serial.print(" battery=");
      Serial.print(batteryPercent);
      Serial.println("%");
      Serial.flush();
    }

    startAdv(moisture, batteryPercent);
    lowPowerDelay(ADVERTISE_MS);
    Bluefruit.Advertising.stop();

    // System OFF entry point commented out along with USE_SYSTEM_OFF_AFTER_SEND above.
    // enterSystemOff();

    lastAdvertiseMs = millis();
    lastAdvertisedMoisture = moisture;
  }
  else if (ENABLE_SERIAL_LOGS)
  {
    Serial.print("Sensor ");
    Serial.print(SENSOR_ID);
    Serial.print(" checked, moisture=");
    Serial.print(moisture);
    Serial.println(" - no advertise this wake");
    Serial.flush();
  }

  if (moisture <= LOW_MOISTURE_THRESHOLD)
  {
    blinkExternalLed(3, LED_PULSE_MS, LED_PULSE_MS); // alert: moisture is low
  }

  uint32_t awakeMs = millis() - cycleStartMs;
  uint32_t sleepMs = awakeMs < WAKE_PERIOD_MS ? (WAKE_PERIOD_MS - awakeMs) : 1;

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.print("Sleeping ");
    Serial.print(sleepMs);
    Serial.println(" ms");
    Serial.flush();
  }

  ledsOff();
  lowPowerDelay(sleepMs);
}

// Free-running relaxation oscillator: charge until HIGH, immediately discharge until
// LOW, immediately recharge, repeat, for windowMs. Returns the number of complete
// HIGH->LOW discharge cycles observed - ported near-verbatim from
// MSV2_Moisture_Probe_Test.ino's countOscCycles(), which has no BLE dependency.
uint32_t countOscCycles(uint32_t windowMs)
{
  uint32_t windowStartMs = millis();
  uint32_t cycles = 0;

  while (millis() - windowStartMs < windowMs)
  {
    pinMode(SENSE_PIN, OUTPUT);
    digitalWrite(SENSE_PIN, HIGH);

    pinMode(SENSE_PIN, INPUT);
    uint32_t waitStartUs = micros();
    while (digitalRead(SENSE_PIN) == HIGH)
    {
      if (micros() - waitStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        break; // disconnected/open probe - bail this half-cycle
      }
    }

    cycles++;
  }

  return cycles;
}

// In-place insertion sort + middle element. Fine for the small, fixed sample counts
// used here (no need for anything fancier than O(n^2)).
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

// Takes OSC_MEDIAN_SAMPLES independent OSC_SAMPLE_WINDOW_MS bursts and returns the
// median cycle count, scaled to a per-second rate (kHz, since burst windows are ms).
// At the validated 1ms window this numerically equals frequency in kHz directly, but
// the division is spelled out so it stays correct if the window is ever retuned.
// Lower value = wetter - the oscillator slows down as sensed capacitance rises.
uint16_t readAveragedMoisture()
{
  uint32_t samples[OSC_MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < OSC_MEDIAN_SAMPLES; i++)
  {
    samples[i] = countOscCycles(OSC_SAMPLE_WINDOW_MS);
  }

  uint32_t medianCycles = medianOf(samples, OSC_MEDIAN_SAMPLES);
  uint16_t freqKHz = (uint16_t)(medianCycles * 1000UL / OSC_SAMPLE_WINDOW_MS / 1000UL);

  // The oscillator's raw frequency runs the "wrong" way for a value called moisture -
  // drier soil oscillates faster, so raw frequency goes UP as things dry out. Invert
  // it here (ceiling - frequency) so the transmitted value reads the way the name
  // implies (higher = wetter), matching every consumer downstream (dry/wet calibration,
  // charts) and the original pre-oscillator convention. MOISTURE_INVERT_CEILING just
  // needs to comfortably exceed the highest frequency this sensor will ever report -
  // clamped to 0 rather than underflowing if it doesn't.
  return freqKHz >= MOISTURE_INVERT_CEILING ? 0 : (MOISTURE_INVERT_CEILING - freqKHz);
}

// Reads LiPo voltage in millivolts via the XIAO's onboard VBAT divider.
// VBAT_ENABLE gates the divider so it only draws current while sampling.
float readBatteryMv(void)
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

  return raw * REAL_VBAT_MV_PER_LSB;
}

// Exponential moving average of the battery percent, carried across wakes, to smooth
// out per-sample ADC noise on the History chart's charging curve - same 0.2 smoothing
// factor already validated for the moisture oscillator's frequency in
// MSV2_Moisture_Probe_Test.ino. This only smooths the transmitted/charted value, not
// the sampling rate - still one real ADC read per wake.
float smoothedBatteryPercent = -1.0f;

uint8_t readSmoothedBatteryPercent()
{
  float raw = batteryMvToPercent(readBatteryMv());

  if (smoothedBatteryPercent < 0)
  {
    smoothedBatteryPercent = raw;
  }
  else
  {
    smoothedBatteryPercent += 0.2f * (raw - smoothedBatteryPercent);
  }

  return (uint8_t)(smoothedBatteryPercent + 0.5f);
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

void startAdv(int moisture, uint8_t batteryPercent)
{
  uint8_t manufacturerData[13];

  manufacturerData[0] = lowByte(MSV2_COMPANY_ID);
  manufacturerData[1] = highByte(MSV2_COMPANY_ID);
  manufacturerData[2] = 'M';
  manufacturerData[3] = 'S';
  manufacturerData[4] = 'V';
  manufacturerData[5] = '2';
  manufacturerData[6] = SENSOR_ID;
  manufacturerData[7] = (uint8_t)(wakeCount & 0xFF);
  manufacturerData[8] = (uint8_t)((wakeCount >> 8) & 0xFF);
  manufacturerData[9] = (uint8_t)((wakeCount >> 16) & 0xFF);
  manufacturerData[10] = (uint8_t)(moisture & 0xFF);
  manufacturerData[11] = (uint8_t)((moisture >> 8) & 0xFF);
  manufacturerData[12] = batteryPercent;

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA,
                                manufacturerData,
                                sizeof(manufacturerData));
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(32, 32); // 20 ms to 30 ms while awake.
  Bluefruit.Advertising.start(0);
}

void lowPowerDelay(uint32_t durationMs)
{
  uint32_t start = millis();
  while (millis() - start < durationMs)
  {
    waitForEvent();
  }
}

void setupStatusLeds(void)
{
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
}

void setLed(uint8_t pin, bool on)
{
  if (!ENABLE_STATUS_LEDS)
  {
    return;
  }

  digitalWrite(pin, on ? XIAO_LED_ON : XIAO_LED_OFF);
}

void ledsOff(void)
{
  setLed(LED_RED, false);
  setLed(LED_GREEN, false);
  setLed(LED_BLUE, false);
}

void pulseLed(uint8_t pin, uint32_t durationMs)
{
  if (!ENABLE_STATUS_LEDS || durationMs == 0)
  {
    return;
  }

  ledsOff();
  setLed(pin, true);
  delay(durationMs);
  setLed(pin, false);
}

void externalLedWrite(bool on)
{
  if (!ENABLE_EXTERNAL_LED)
  {
    return;
  }

  digitalWrite(EXTERNAL_LED_PIN, on == EXTERNAL_LED_ACTIVE_HIGH ? HIGH : LOW);
}

void blinkExternalLed(uint8_t times, uint32_t onMs, uint32_t offMs)
{
  if (!ENABLE_EXTERNAL_LED || times == 0 || onMs == 0)
  {
    return;
  }

  for (uint8_t i = 0; i < times; i++)
  {
    externalLedWrite(true);
    delay(onMs);
    externalLedWrite(false);
    if (i + 1 < times)
    {
      delay(offMs);
    }
  }
}

void powerDownExternalFlash(void)
{
#if HAS_EXTERNAL_FLASH
  if (!flash.begin())
  {
    return;
  }

  flashTransport.begin();
  flashTransport.runCommand(0xB9); // SPI/QSPI deep power-down command.
  delay(10);
  flash.end();
#endif
}

// enterSystemOff/connect_callback/disconnect_callback commented out along with their
// call sites above (USE_SYSTEM_OFF_AFTER_SEND, Periph connect callbacks) - unused for
// tonight's timed wake/send/sleep test. Needs "nrf_gpio.h" re-included to bring back.
//
// void enterSystemOff(void)
// {
//   Serial.println("Entering System OFF. Pull WAKE_PIN low or reset to wake.");
//   Serial.flush();
//
//   Bluefruit.Advertising.stop();
//   Bluefruit.disconnect(0);
//   powerDownExternalFlash();
//
//   ledsOff();
//   pinMode(LED_RED, INPUT);
//   pinMode(LED_GREEN, INPUT);
//   pinMode(LED_BLUE, INPUT);
//   pinMode(WAKE_PIN, INPUT_PULLUP);
//   nrf_gpio_cfg_sense_input(
//     g_ADigitalPinMap[WAKE_PIN],
//     NRF_GPIO_PIN_PULLUP,
//     NRF_GPIO_PIN_SENSE_LOW
//   );
//
//   delay(20);
//   sd_power_system_off();
// }
//
// void connect_callback(uint16_t conn_handle)
// {
//   (void)conn_handle;
//   Serial.println("Connected");
// }
//
// void disconnect_callback(uint16_t conn_handle, uint8_t reason)
// {
//   (void)conn_handle;
//   (void)reason;
//   Serial.println("Disconnected");
// }
