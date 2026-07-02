/*********************************************************************
  Periodic BLE advertising sensor test for Adafruit nRF52 Bluefruit.

  This uses a timed low-power System ON sleep pattern. It is suitable for
  periodic wakeups. The deepest nRF52 System OFF sleep generally cannot wake
  itself from a normal internal timer; it wakes from reset, GPIO, NFC, or
  similar external events.
*********************************************************************/
#include "Adafruit_SPIFlash.h"
#include "nrf_gpio.h"
#include <bluefruit.h>

const char DEVICE_NAME[] = "MSV2-MOISTURE-001";
const uint16_t MSV2_COMPANY_ID = 0xFFFF; // Test/development company id.
const uint8_t SENSOR_ID = 1; // Change to 2 before uploading to the second sender XIAO.

const uint32_t WAKE_PERIOD_MS = 3000;
const uint32_t ADVERTISE_MS = 500;
const uint32_t LED_PULSE_MS = 80;
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = true;
const bool ENABLE_SERIAL_LOGS = true;

// 0 = timed wake/send/sleep test.
// 1 = send once, then enter true nRF System OFF deep sleep.
#define USE_SYSTEM_OFF_AFTER_SEND 0

// In System OFF mode, connect WAKE_PIN to GND to wake/reset the nRF52840.
// Change this to a pin driven by an external RTC/timer/sensor interrupt for
// periodic true-deep-sleep wakeups.
const int WAKE_PIN = A0;

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

BLEDfu bledfu;
BLEDis bledis;
BLEBas blebas;

uint32_t wakeCount = 0;

void setup()
{
  Serial.begin(115200);

  setupStatusLeds();
  ledsOff();
  powerDownExternalFlash();

  Bluefruit.autoConnLed(false);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bledfu.begin();

  bledis.setManufacturer("Adafruit Industries");
  bledis.setModel("MSV2 Moisture Test");
  bledis.begin();

  blebas.begin();
  blebas.write(100);

  Serial.println("MSV2 BLE advertising sensor ready");
  Serial.print("Sensor ID: ");
  Serial.println(SENSOR_ID);
}

void loop()
{
  uint32_t cycleStartMs = millis();
  wakeCount++;
  int fakeMoisture = 500 + (wakeCount % 100);
  uint8_t batteryPercent = 100;

  pulseLed(LED_GREEN, LED_PULSE_MS);

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.print("Sensor ");
    Serial.print(SENSOR_ID);
    Serial.print(" wake ");
    Serial.print(wakeCount);
    Serial.print(": advertising for ");
    Serial.print(ADVERTISE_MS);
    Serial.print(" ms moisture=");
    Serial.print(fakeMoisture);
    Serial.print(" battery=");
    Serial.print(batteryPercent);
    Serial.println("%");
    Serial.flush();
  }

  startAdv(fakeMoisture, batteryPercent);
  lowPowerDelay(ADVERTISE_MS);
  Bluefruit.Advertising.stop();

  pulseLed(LED_BLUE, LED_PULSE_MS);
  pulseLed(LED_RED, LED_PULSE_MS);

#if USE_SYSTEM_OFF_AFTER_SEND
  enterSystemOff();
#endif

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

void enterSystemOff(void)
{
  Serial.println("Entering System OFF. Pull WAKE_PIN low or reset to wake.");
  Serial.flush();

  Bluefruit.Advertising.stop();
  Bluefruit.disconnect(0);
  powerDownExternalFlash();

  ledsOff();
  pinMode(LED_RED, INPUT);
  pinMode(LED_GREEN, INPUT);
  pinMode(LED_BLUE, INPUT);
  pinMode(WAKE_PIN, INPUT_PULLUP);
  nrf_gpio_cfg_sense_input(
    g_ADigitalPinMap[WAKE_PIN],
    NRF_GPIO_PIN_PULLUP,
    NRF_GPIO_PIN_SENSE_LOW
  );

  delay(20);
  sd_power_system_off();
}

void connect_callback(uint16_t conn_handle)
{
  (void)conn_handle;
  Serial.println("Connected");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void)conn_handle;
  (void)reason;
  Serial.println("Disconnected");
}
