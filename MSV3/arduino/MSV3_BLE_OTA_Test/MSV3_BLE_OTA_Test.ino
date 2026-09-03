// Recoverable MSV3 BLE OTA proof-of-concept.
//
// This intentionally stays continuously connectable and does not run the
// production sensing/sleep loop. Flash it only on a USB/SWD-recoverable bench
// probe. A BLE client such as Bluefruit Connect can use the advertised Nordic
// buttonless DFU service to transfer the .zip emitted by the guarded build.
//
// REQUIRED build/upload path:
//   powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 ^
//     -Action upload -Sketch arduino/MSV3_BLE_OTA_Test -Port COMx


#include <Arduino.h>
#include <bluefruit.h>
#include "ResetSafeBLEDfu.h"

#if !defined(USE_LFRC) || defined(USE_LFXO)
#error "MSV3 requires USE_LFRC and no USE_LFXO. Build with scripts/arduino_msv3_probe.ps1; see docs/KNOWN_ISSUES.md."
#endif

// Increment this before building an OTA candidate. The version is printed on
// USB serial and encoded by the number of short LED flashes in each group.
constexpr uint8_t OTA_TEST_VERSION = 3;
constexpr uint8_t STATUS_LED_PIN = 42;  // P1.10, D2 through R16, active HIGH
constexpr char DEVICE_NAME[] = "MSV3-OTA-TEST";

ResetSafeBLEDfu bledfu;

void connectCallback(uint16_t connHandle)
{
  (void)connHandle;
  digitalWrite(STATUS_LED_PIN, HIGH);
  Serial.println("[OTA] BLE client connected");
  Serial.flush();
}

void disconnectCallback(uint16_t connHandle, uint8_t reason)
{
  (void)connHandle;
  digitalWrite(STATUS_LED_PIN, LOW);
  Serial.print("[OTA] BLE client disconnected, reason=0x");
  Serial.println(reason, HEX);
  Serial.flush();
}

void startAdvertising()
{
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bledfu);
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // 20 ms fast, 152.5 ms slow
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);              // remain available until connected
}

void setup()
{
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(115200);
  const uint32_t waitStartedMs = millis();
  while (!Serial && millis() - waitStartedMs < 1500)
  {
    delay(10);
  }

  Bluefruit.autoConnLed(false);
  Bluefruit.begin();
  Bluefruit.setTxPower(8);
  Bluefruit.setName(DEVICE_NAME);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  // This Nordic-compatible buttonless service enters the resident bootloader
  // through GPREGRET=0xA8 plus a full reset (see ResetSafeBLEDfu.h).
  bledfu.begin();
  startAdvertising();

  Serial.print("[OTA] MSV3 BLE OTA test version ");
  Serial.println(OTA_TEST_VERSION);
  Serial.println("[OTA] advertising as MSV3-OTA-TEST; USB/SWD recovery expected");
  Serial.flush();
}

void loop()
{
  if (Bluefruit.connected())
  {
    delay(50);
    return;
  }

  // A human-visible firmware-version marker independent of USB serial.
  for (uint8_t i = 0; i < OTA_TEST_VERSION; ++i)
  {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(90);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(140);
  }
  delay(1000);
}
