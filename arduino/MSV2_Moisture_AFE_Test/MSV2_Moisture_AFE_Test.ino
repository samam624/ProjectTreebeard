/*********************************************************************
  MSV2 analog-front-end capacitive sensor bench test (off-the-shelf
  "Capacitive Soil Moisture Sensor v1.2" module).

  Why this exists: this commercial module senses moisture the same
  fundamental way our custom probe does (a capacitive sense pad whose
  capacitance shifts with soil moisture) but processes it very
  differently. Ours drives the pad directly and has firmware digitally
  count oscillator cycles / time an RC decay (see
  MSV2_Moisture_Probe_Test.ino, MSV2_Moisture_RC_Test.ino). This module
  puts a 555-style oscillator on the pad, then rectifies and low-pass
  filters that oscillation in hardware (diode + smoothing cap) into a
  slow, clean DC voltage on AOUT - i.e. it has an analog front end
  ahead of the ADC, we don't. This sketch logs that DC voltage with the
  same session-relative-percent framing MSV2_Moisture_Probe_Test.ino
  uses for its frequency reading, so the two sketches' serial output
  can be read side by side (same wet-water bench, same time window) to
  judge whether an AFE like this is worth adding to our own design -
  compare noise floor (raw vs median vs smoothed spread) and step
  response, not just the calibrated percent number.

  Sensing behavior of this module (for interpreting the numbers):
  LOWER voltage = WETTER (higher pad capacitance loads the oscillator
  down further). Dry-air baseline is typically ~2.2-3.0V, wet/submerged
  down near ~1.2-1.5V, at 3.3V supply. Percent below is inverted to
  match that, same convention as the probe test: 100% = wettest thing
  seen this session, 0% = driest.

  Wiring:
    Module VCC -> ESP32 3V3 (NOT 5V/VIN - this board's ADC pins are not
      5V tolerant, and this module runs fine at 3.3V; a genuinely 5V-only
      clone would just read at a proportionally higher, still-safe range
      since AOUT tracks VCC, but there's no reason to risk it here).
    Module GND -> ESP32 GND
    Module AOUT -> ESP32 IO4 (GPIO4)

  Board: ESP32-S3 devkit (native USB). Flash/monitor with:
    powershell -ExecutionPolicy Bypass -File scripts\arduino_moisture_afe_test.ps1 upload -Port COM12
    powershell -ExecutionPolicy Bypass -File scripts\arduino_moisture_afe_test.ps1 monitor -Port COM12

  BLE feed into the display center: also advertises as sensor 3, in the same MSV2
  manufacturer-data format MSV2_Moisture_BLE_Sleep.ino uses, so MSV2_ESP32_Display_
  Runtime.ino picks it up exactly like a real probe (/data, cloud snapshot history) -
  see that receiver's MOISTURE_INVERT_CEILING_BY_SENSOR[3] for how the raw mV below is
  turned into a moisture percent. The wire field is still named "rawMoistureKHz" on the
  receiver side (it's generic - "raw units, higher = drier" - not literally kHz), we're
  just putting millivolts in it here. Byte layout (15 bytes total):
    [0-1]  company ID 0xFFFF, LE
    [2-5]  'M','S','V','2' magic
    [6]    sensor ID (3)
    [7-9]  24-bit wake counter, LE - must change every packet or the receiver dedups it
    [10-11] raw reading (this sketch's medianMv), LE
    [12-13] "battery" mV, LE - fixed placeholder (this board is USB-powered, no cell)
    [14]   charging flag - always 0 here
*********************************************************************/

#include <BLEDevice.h>

const int SENSE_PIN = 4; // IO4/GPIO4 silkscreen label - ADC1_CH3 on ESP32-S3

const uint8_t SENSOR_ID = 3; // sensor 1 = Scindapsus probe, 2 = Alocasia probe, 3 = this bench rig
const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint16_t PLACEHOLDER_BATTERY_MV = 4200; // "full, not charging" - no real battery on this board
BLEAdvertising* pAdvertising = nullptr;
uint32_t wakeCount = 0;

// Matches MSV2_Moisture_Probe_Test.ino's WAKE_INTERVAL_MS so a side-by-side
// serial capture of both sketches lines up on roughly the same cadence.
const uint32_t READING_PERIOD_MS = 3000;

const uint8_t MEDIAN_SAMPLES = 5;
const uint32_t SAMPLE_SPACING_MS = 2; // decorrelates any mains/switching noise between samples

float smoothedMv = -1.0f;
float sessionMinMv = 1.0e9f;
float sessionMaxMv = 0.0f;

void setup()
{
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000)
  {
    delay(10);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(SENSE_PIN, ADC_11db); // full 0-3.3V-ish range

  BLEDevice::init("MSV2-AFE-TEST");
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start(); // Bluedroid start() advertises indefinitely; data is pushed via setAdvertisementData() per loop

  Serial.println();
  Serial.println("MSV2 analog-front-end sensor bench test (Capacitive Soil Moisture Sensor v1.2)");
  Serial.printf("Chip: %s rev%d, %d cores @ %dMHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.println("Sense pin: IO4 (ADC), lower mV = wetter");
  Serial.println("raw_mv(5),median_mv,smoothed_mv,percent_of_session_range");
}

void loop()
{
  uint16_t samples[MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    samples[i] = analogReadMilliVolts(SENSE_PIN);
    if (i + 1 < MEDIAN_SAMPLES)
    {
      delay(SAMPLE_SPACING_MS);
    }
  }

  Serial.print("raw:");
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    Serial.print(samples[i]);
    Serial.print(i + 1 < MEDIAN_SAMPLES ? " " : ",");
  }

  uint16_t medianMv = medianOf(samples, MEDIAN_SAMPLES);

  if (smoothedMv < 0)
  {
    smoothedMv = medianMv;
  }
  else
  {
    smoothedMv += 0.2f * ((float)medianMv - smoothedMv);
  }

  if (medianMv < sessionMinMv) sessionMinMv = medianMv;
  if (medianMv > sessionMaxMv) sessionMaxMv = medianMv;

  // Inverted like the probe test's Hz->percent mapping (there: lower Hz =
  // wetter; here: lower mV = wetter) - 100% = closest to wettest seen this
  // session, 0% = closest to driest.
  float percent = 0.0f;
  if (sessionMaxMv > sessionMinMv)
  {
    percent = 100.0f * (sessionMaxMv - smoothedMv) / (sessionMaxMv - sessionMinMv);
  }

  Serial.print(medianMv);
  Serial.print(",");
  Serial.print(smoothedMv, 1);
  Serial.print(",");
  Serial.println(percent, 1);

  advertiseReading(medianMv);

  delay(READING_PERIOD_MS);
}

// Builds and pushes one MSV2 manufacturer-data packet (see file header for the byte
// layout) - same format MSV2_Moisture_BLE_Sleep.ino's startAdv() sends, so the receiver
// treats this board as just another probe.
void advertiseReading(uint16_t rawMv)
{
  wakeCount++;

  uint8_t manufacturerData[15];
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
  manufacturerData[10] = (uint8_t)(rawMv & 0xFF);
  manufacturerData[11] = (uint8_t)((rawMv >> 8) & 0xFF);
  manufacturerData[12] = (uint8_t)(PLACEHOLDER_BATTERY_MV & 0xFF);
  manufacturerData[13] = (uint8_t)((PLACEHOLDER_BATTERY_MV >> 8) & 0xFF);
  manufacturerData[14] = 0x00; // not charging

  BLEAdvertisementData advData;
  advData.setManufacturerData(String((char*)manufacturerData, sizeof(manufacturerData)));
  pAdvertising->setAdvertisementData(advData);
}

// In-place insertion sort + middle element - same approach as
// MSV2_Moisture_Probe_Test.ino's medianOf(), fine for this small n.
uint16_t medianOf(uint16_t vals[], uint8_t n)
{
  for (uint8_t i = 1; i < n; i++)
  {
    uint16_t key = vals[i];
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
