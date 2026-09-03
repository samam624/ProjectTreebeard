/*********************************************************************
  MSV2 BLE advertisement receiver for Adafruit/Seeed nRF52 Bluefruit.

  This scans continuously for the sensor's manufacturer data packet:
    company id: 0xFFFF
    payload:    "MSV2" + sensor_id_u8 + wake_count_low24 + moisture_u16_le
*********************************************************************/
#include <bluefruit.h>

const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint8_t MSV2_MAGIC[] = { 'M', 'S', 'V', '2' };
const uint8_t MAX_SENSOR_ID = 16;

uint32_t lastWakeBySensor[MAX_SENSOR_ID + 1] = { 0 };
bool haveLastWakeBySensor[MAX_SENSOR_ID + 1] = { false };

void setup()
{
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000)
  {
    // Give the host up to 3s to open the port without hanging forever if nothing
    // connects - this sketch is meant to run unattended off USB power, and the
    // scan/log loop below works fine with Serial never opened.
    delay(10);
  }

  Bluefruit.autoConnLed(false);
  Bluefruit.begin(0, 1);
  Bluefruit.setTxPower(4);
  Bluefruit.setName("MSV2-RECEIVER");

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.filterRssi(-90);
  Bluefruit.Scanner.setInterval(16, 16); // 10 ms interval, 10 ms window.
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);

  Serial.println("MSV2 BLE advertisement receiver scanning...");
}

void loop()
{
  waitForEvent();
}

void scan_callback(ble_gap_evt_adv_report_t* report)
{
  uint8_t buffer[31];
  uint8_t len = Bluefruit.Scanner.parseReportByType(
    report,
    BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA,
    buffer,
    sizeof(buffer)
  );

  if (len < 12)
  {
    Bluefruit.Scanner.resume();
    return;
  }

  uint16_t companyId = buffer[0] | (buffer[1] << 8);
  if (companyId != MSV2_COMPANY_ID)
  {
    Bluefruit.Scanner.resume();
    return;
  }

  if (memcmp(&buffer[2], MSV2_MAGIC, sizeof(MSV2_MAGIC)) != 0)
  {
    Bluefruit.Scanner.resume();
    return;
  }

  uint8_t sensorId = buffer[6];
  if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
  {
    Bluefruit.Scanner.resume();
    return;
  }

  uint32_t wake = ((uint32_t)buffer[7]) |
                  ((uint32_t)buffer[8] << 8) |
                  ((uint32_t)buffer[9] << 16);
  uint16_t moisture = buffer[10] | (buffer[11] << 8);

  if (haveLastWakeBySensor[sensorId] && wake == lastWakeBySensor[sensorId])
  {
    Bluefruit.Scanner.resume();
    return;
  }

  // Guard the reboot case explicitly (wake counter resets to a small value on
  // probe power-cycle/reflash) instead of letting `wake - lastWake - 1` underflow
  // into a huge, nonsensical 24-bit-masked count - same fix already applied to
  // Treebeard_ESP32_Display_Runtime.ino's onResult().
  uint32_t missed = 0;
  if (haveLastWakeBySensor[sensorId] && wake > lastWakeBySensor[sensorId])
  {
    missed = (wake - lastWakeBySensor[sensorId] - 1) & 0xFFFFFF;
  }

  lastWakeBySensor[sensorId] = wake;
  haveLastWakeBySensor[sensorId] = true;

  Serial.print("sensor=");
  Serial.print(sensorId);
  Serial.print(",wake=");
  Serial.print(wake);
  Serial.print(",moisture=");
  Serial.print(moisture);
  Serial.print(",rssi=");
  Serial.print(report->rssi);
  Serial.print(",missed=");
  Serial.print(missed);

  Serial.println();

  Bluefruit.Scanner.resume();
}
