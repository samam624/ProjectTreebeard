// MSV2 Antenna Test
//
// Standalone RF bring-up test for a new ESP32-S3-WROOM-1U-N16R8 PCB (not the
// devkit breadboard build). The "U" module has no onboard PCB antenna - it
// depends on the board's U.FL connector, cable, and external antenna. WiFi
// and BLE share that single RF path, so exercising both here is a reasonable
// smoke test that the antenna/connector/matching network are all intact.
//
// What it does, forever:
//   1. Connects to WIFI_SSID from WiFiCredentials.h and tracks RSSI (rolling
//      avg + session min/max) - the main real-world signal-quality test.
//   2. Runs a full WiFi network scan every ~30s so you get a signal reading
//      even without a known AP in range, and something to compare against a
//      known-good board (e.g. the devkit).
//   3. Runs a BLE scan continuously and tracks every advertiser seen with its
//      RSSI, to confirm the second radio works over the same antenna.
//
// Every second it prints one machine-parseable line prefixed "RPT," (see
// scripts/antenna_rssi_logger.py) plus human-readable scan/connect logs.

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "WiFiCredentials.h"

const uint32_t WIFI_SCAN_INTERVAL_MS = 30000;
const uint32_t BLE_SCAN_SECONDS = 2;
const uint32_t REPORT_INTERVAL_MS = 1000;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
const uint32_t BLE_DEVICE_STALE_MS = 60000;
const int RSSI_WINDOW = 10;
const int MAX_BLE_DEVICES = 24;
const int MAX_WIFI_SCAN_RESULTS = 64;

struct BleDevice
{
  String addr;
  String name;
  int rssi;
  uint32_t lastSeenMs;
};

BLEScan* bleScan = nullptr;
BleDevice bleDevices[MAX_BLE_DEVICES];
int bleDeviceCount = 0;
int bleSeenThisCycle = 0;

int rssiWindow[RSSI_WINDOW];
int rssiWindowCount = 0;
int rssiWindowIndex = 0;
int rssiMin = 0;
int rssiMax = -200;
bool haveConnectedOnce = false;

uint32_t lastReportMs = 0;
uint32_t lastWifiScanMs = 0;
uint32_t wifiConnectStartMs = 0;
bool wifiConnecting = false;

class AntennaTestBleCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    bleSeenThisCycle++;
    String addr = advertisedDevice.getAddress().toString().c_str();
    int rssi = advertisedDevice.getRSSI();
    String name = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "";

    for (int i = 0; i < bleDeviceCount; i++)
    {
      if (bleDevices[i].addr == addr)
      {
        bleDevices[i].rssi = rssi;
        bleDevices[i].lastSeenMs = millis();
        if (name.length())
        {
          bleDevices[i].name = name;
        }
        return;
      }
    }

    if (bleDeviceCount < MAX_BLE_DEVICES)
    {
      bleDevices[bleDeviceCount].addr = addr;
      bleDevices[bleDeviceCount].name = name;
      bleDevices[bleDeviceCount].rssi = rssi;
      bleDevices[bleDeviceCount].lastSeenMs = millis();
      bleDeviceCount++;
    }
  }
};

void printHardwareInfo()
{
  Serial.println();
  Serial.println("=== MSV2 Antenna Test ===");
  Serial.printf("Chip: %s rev%d, %d cores @ %dMHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %uMB\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  Serial.println();
}

void runWifiScan()
{
  Serial.println("--- WiFi scan ---");
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0)
  {
    Serial.println("  no networks found (check antenna / RF path)");
  }
  else
  {
    int count = n > MAX_WIFI_SCAN_RESULTS ? MAX_WIFI_SCAN_RESULTS : n;
    int idx[MAX_WIFI_SCAN_RESULTS];
    for (int i = 0; i < count; i++)
    {
      idx[i] = i;
    }
    for (int i = 1; i < count; i++)
    {
      int key = idx[i];
      int j = i - 1;
      while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(key))
      {
        idx[j + 1] = idx[j];
        j--;
      }
      idx[j + 1] = key;
    }
    for (int i = 0; i < count; i++)
    {
      int k = idx[i];
      Serial.printf("  %4d dBm  ch%-2d  %s\n", WiFi.RSSI(k), WiFi.channel(k), WiFi.SSID(k).c_str());
    }
  }
  WiFi.scanDelete();
  Serial.println("-----------------");
}

void printBleTable()
{
  uint32_t now = millis();
  Serial.println("--- BLE devices seen (last 60s) ---");
  if (bleDeviceCount == 0)
  {
    Serial.println("  none yet");
  }
  int idx[MAX_BLE_DEVICES];
  int count = 0;
  for (int i = 0; i < bleDeviceCount; i++)
  {
    if (now - bleDevices[i].lastSeenMs <= BLE_DEVICE_STALE_MS)
    {
      idx[count++] = i;
    }
  }
  for (int i = 1; i < count; i++)
  {
    int key = idx[i];
    int j = i - 1;
    while (j >= 0 && bleDevices[idx[j]].rssi < bleDevices[key].rssi)
    {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = key;
  }
  for (int i = 0; i < count; i++)
  {
    BleDevice& d = bleDevices[idx[i]];
    Serial.printf("  %4d dBm  %s  %s\n", d.rssi, d.addr.c_str(), d.name.c_str());
  }
  Serial.println("------------------------------------");
}

void beginWifiConnect()
{
  Serial.printf("Connecting to '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiConnecting = true;
  wifiConnectStartMs = millis();
}

void recordRssiSample(int rssi)
{
  rssiWindow[rssiWindowIndex] = rssi;
  rssiWindowIndex = (rssiWindowIndex + 1) % RSSI_WINDOW;
  if (rssiWindowCount < RSSI_WINDOW)
  {
    rssiWindowCount++;
  }
  if (!haveConnectedOnce)
  {
    rssiMin = rssi;
    rssiMax = rssi;
    haveConnectedOnce = true;
  }
  else
  {
    if (rssi < rssiMin) rssiMin = rssi;
    if (rssi > rssiMax) rssiMax = rssi;
  }
}

int rollingAvgRssi()
{
  if (rssiWindowCount == 0)
  {
    return 0;
  }
  long sum = 0;
  for (int i = 0; i < rssiWindowCount; i++)
  {
    sum += rssiWindow[i];
  }
  return (int)(sum / rssiWindowCount);
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  printHardwareInfo();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  runWifiScan();
  beginWifiConnect();

  BLEDevice::init("MSV2-ANTTEST");
  bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new AntennaTestBleCallbacks(), true);
  bleScan->setActiveScan(true);
  bleScan->setInterval(100);
  bleScan->setWindow(90);

  Serial.println("RPT,elapsed_s,wifi_status,wifi_rssi,wifi_avg,wifi_min,wifi_max,ble_seen,ble_devices,ble_best_rssi,ble_best_addr");
  lastReportMs = millis();
  lastWifiScanMs = millis();
}

void loop()
{
  uint32_t now = millis();

  if (wifiConnecting)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      wifiConnecting = false;
      Serial.printf("WiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());
    }
    else if (now - wifiConnectStartMs > WIFI_CONNECT_TIMEOUT_MS)
    {
      Serial.println("WiFi connect timed out, retrying...");
      beginWifiConnect();
    }
  }
  else if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi dropped, reconnecting...");
    beginWifiConnect();
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    recordRssiSample(WiFi.RSSI());
  }

  bleSeenThisCycle = 0;
  bleScan->start(BLE_SCAN_SECONDS, false);
  bleScan->clearResults();

  int bleBestRssi = -200;
  String bleBestAddr = "-";
  for (int i = 0; i < bleDeviceCount; i++)
  {
    if (now - bleDevices[i].lastSeenMs <= BLE_DEVICE_STALE_MS && bleDevices[i].rssi > bleBestRssi)
    {
      bleBestRssi = bleDevices[i].rssi;
      bleBestAddr = bleDevices[i].addr;
    }
  }
  if (bleBestAddr == "-")
  {
    bleBestRssi = 0;
  }

  now = millis();
  if (now - lastReportMs >= REPORT_INTERVAL_MS)
  {
    lastReportMs = now;
    const char* status = WiFi.status() == WL_CONNECTED ? "CONNECTED" : (wifiConnecting ? "CONNECTING" : "DISCONNECTED");
    int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    Serial.printf(
      "RPT,%.1f,%s,%d,%d,%d,%d,%d,%d,%d,%s\n",
      now / 1000.0, status, rssi, rollingAvgRssi(),
      haveConnectedOnce ? rssiMin : 0, haveConnectedOnce ? rssiMax : 0,
      bleSeenThisCycle, bleDeviceCount, bleBestRssi, bleBestAddr.c_str()
    );
  }

  if (now - lastWifiScanMs >= WIFI_SCAN_INTERVAL_MS)
  {
    lastWifiScanMs = now;
    runWifiScan();
    printBleTable();
  }
}
