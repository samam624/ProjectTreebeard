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
const uint8_t SENSOR_ID = 1; // Change to 2 before uploading to the second sender XIAO.

const uint32_t WAKE_PERIOD_MS = 3000;
const uint32_t ADVERTISE_MS = 500;
const uint32_t LED_PULSE_MS = 80;
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = false; // onboard RGB disabled for tonight's battery-impact test - see EXTERNAL_LED_PIN below
const bool ENABLE_SERIAL_LOGS = false; // disabled for tonight's unattended overnight run - flip back to true to debug

// External LED on D1, pulsed once per wake to mark a BLE advertise burst going out.
// Only carries the RGB's old per-wake heartbeat semantics, not one blink per raw
// 802.15 packet - Bluefruit doesn't expose a per-packet callback, and a literal
// ~25-blink strobe every 500ms would defeat the point of an overnight power test.
const int EXTERNAL_LED_PIN = D1;
const bool EXTERNAL_LED_ACTIVE_HIGH = true;
const bool ENABLE_EXTERNAL_LED = true;

// Moisture sensing node: precharge HIGH, release to INPUT, time the fall to LOW.
// See MSV2_Moisture_RC_Test for the standalone version this was validated against.
const int SENSE_PIN = D0;
// A verify-via-analogRead precharge loop was tried and reverted: checking the charge
// level means releasing the pin to INPUT to read it, which starts the very discharge
// it's trying to avoid - if the ADC call's own overhead is comparable to the charge
// burst, the loop nets out *worse* than a plain fixed delay (confirmed: it read
// moisture=0 every cycle). A long, uninterrupted precharge is the sound approach here.
// 10ms moved the baseline from ~12,000 to ~17,700us with much more consistent readings
// and correct touch response, confirmed on hardware. Pinned here, NOT bumped further -
// 20ms was tried for extra margin but broke touch response (reading collapsed far below
// baseline on contact instead of rising), so 10ms is the validated value, not a guess.
const uint32_t MOISTURE_PRECHARGE_US = 10000;
// Bumped from 50ms: unplugging USB (removing ground-loop/mains-noise coupling into this
// high-impedance node) revealed real fall times exceeding 50ms - every sample was hitting
// the old timeout and reporting MOISTURE_INVALID (0xFFFF), which looked like a plausible
// high reading but was actually "no valid measurement." Bumped generously to observe the
// true value before dialing this back down.
const uint32_t MOISTURE_FALL_TIMEOUT_US = 500000;
const uint8_t MOISTURE_SAMPLES = 8;
const uint16_t MOISTURE_INVALID = 0xFFFF; // sent when no valid fall was detected this cycle

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

uint32_t wakeCount = 0;

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
  wakeCount++;
  int moisture = readAveragedMoisture();
  uint8_t batteryPercent = batteryMvToPercent(readBatteryMv());

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.print("Sensor ");
    Serial.print(SENSOR_ID);
    Serial.print(" wake ");
    Serial.print(wakeCount);
    Serial.print(": advertising for ");
    Serial.print(ADVERTISE_MS);
    Serial.print(" ms moisture=");
    Serial.print(moisture);
    Serial.print(" battery=");
    Serial.print(batteryPercent);
    Serial.println("%");
    Serial.flush();
  }

  startAdv(moisture, batteryPercent);
  pulseExternalLed(LED_PULSE_MS); // marks the advertise burst going out
  lowPowerDelay(ADVERTISE_MS);
  Bluefruit.Advertising.stop();

  // System OFF entry point commented out along with USE_SYSTEM_OFF_AFTER_SEND above.
  // enterSystemOff();

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

// Precharges SENSE_PIN high, releases it to input, and times the fall to LOW.
// Returns the fall time in microseconds, or -1 if it never fell within the timeout.
long readMoistureFallUs()
{
  pinMode(SENSE_PIN, OUTPUT);
  digitalWrite(SENSE_PIN, HIGH);
  delayMicroseconds(MOISTURE_PRECHARGE_US);

  pinMode(SENSE_PIN, INPUT);
  uint32_t start = micros();

  while (digitalRead(SENSE_PIN) == HIGH)
  {
    if (micros() - start > MOISTURE_FALL_TIMEOUT_US)
    {
      return -1;
    }
  }

  return (long)(micros() - start);
}

uint16_t readAveragedMoisture()
{
  uint32_t sum = 0;
  uint8_t validSamples = 0;

  for (uint8_t i = 0; i < MOISTURE_SAMPLES; i++)
  {
    long fallUs = readMoistureFallUs();
    if (fallUs >= 0)
    {
      sum += (uint32_t)fallUs;
      validSamples++;
    }
    delay(5); // let the node fully settle before recharging
  }

  if (validSamples == 0)
  {
    return MOISTURE_INVALID;
  }

  return (uint16_t)(sum / validSamples);
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

void pulseExternalLed(uint32_t durationMs)
{
  if (!ENABLE_EXTERNAL_LED || durationMs == 0)
  {
    return;
  }

  externalLedWrite(true);
  delay(durationMs);
  externalLedWrite(false);
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
