/*********************************************************************
  MSV2 ESP32 BLE advertisement display runtime.

  Flash this sketch once. After that, update the screen layout by pushing
  ui.json over WiFi:

    python scripts/push_display_ui.py --file ui/display_default.json

  Arduino library requirements:
    - ESP32 board package
    - GFX Library for Arduino
    - ArduinoJson

  The board starts an access point named MSV2-Display by default:
    SSID:     MSV2-Display
    password: moisture
    updater:  http://192.168.4.1/ui
    status:   http://192.168.4.1/data
*********************************************************************/

// Bring-up stage:
//   0 = serial-only safe boot
//   1 = display-only smoke test
//   2 = display + WiFi updater runtime, BLE off
//   3 = full runtime with BLE scanning
#define MSV2_BRINGUP_STAGE 3

#define MSV2_SAFE_BOOT (MSV2_BRINGUP_STAGE == 0)
#define MSV2_DISPLAY_TEST (MSV2_BRINGUP_STAGE == 1)
#define MSV2_ENABLE_DISPLAY (MSV2_BRINGUP_STAGE >= 1)
#define MSV2_ENABLE_WIFI (MSV2_BRINGUP_STAGE >= 2)
#define MSV2_ENABLE_BLE (MSV2_BRINGUP_STAGE >= 3)
#define MSV2_SAFE_BOOT_LED_PIN 2
#define MSV2_SAFE_BOOT_BLINK_MS 500
#define MSV2_BUILD_TAG "MSV2 runtime build 2026-07-01 packet-loss-stats"

#include <Arduino.h>

#if MSV2_ENABLE_WIFI
#include <FS.h>
using fs::FS;
using fs::File;
#endif

#if MSV2_ENABLE_DISPLAY
#include <Arduino_GFX_Library.h>
#include "PlantSpriteCatalog.h"
#endif

#if MSV2_SAFE_BOOT || MSV2_DISPLAY_TEST

void setup()
{
  pinMode(MSV2_SAFE_BOOT_LED_PIN, OUTPUT);
  digitalWrite(MSV2_SAFE_BOOT_LED_PIN, LOW);

  Serial.begin(115200);
  uint32_t startMs = millis();
  while (millis() - startMs < 5000)
  {
    Serial.println("[MSV2 safe boot] waiting for serial");
    digitalWrite(MSV2_SAFE_BOOT_LED_PIN, !digitalRead(MSV2_SAFE_BOOT_LED_PIN));
    delay(250);
  }

  Serial.println();
  Serial.println("[MSV2 safe boot] setup reached");
#if MSV2_DISPLAY_TEST
  Serial.println("[MSV2 display test] initializing Arduino_GFX");
  Arduino_DataBus* testBus = new Arduino_ESP32SPI(15, 14, 12, 13, -1);
  Arduino_GFX* testGfx = new Arduino_ST7796(testBus, 16, 1, false, 320, 480);
  if (testBus == nullptr || testGfx == nullptr)
  {
    Serial.println("[MSV2 display test] allocation failed");
    return;
  }
  Serial.println("[MSV2 display test] allocation complete");

  Serial.println("[MSV2 display test] calling begin()");
  testGfx->begin();
  Serial.println("[MSV2 display test] begin() complete");

  Serial.println("[MSV2 display test] drawing");
  testGfx->setRotation(1);
  testGfx->fillScreen(0x0000);
  testGfx->setTextColor(0xFFFF, 0x0000);
  testGfx->setCursor(8, 8);
  testGfx->setTextSize(2);
  testGfx->println("MSV2 display test");
  testGfx->setTextColor(0x07E0, 0x0000);
  testGfx->setCursor(8, 32);
  testGfx->setTextSize(1);
  testGfx->println("If you see this, display works.");
  Serial.println("[MSV2 display test] init/draw complete");
#else
  Serial.println("[MSV2 safe boot] no display, WiFi, BLE, LittleFS, or ArduinoJson loaded");
#endif
  Serial.println("[MSV2 safe boot] GPIO 2 heartbeat active if your board has that LED");
  Serial.flush();
}

void loop()
{
  static uint32_t lastPrintMs = 0;
  static uint32_t lastBlinkMs = 0;
  static bool blinkState = false;
  uint32_t now = millis();

  if (now - lastBlinkMs >= MSV2_SAFE_BOOT_BLINK_MS)
  {
    lastBlinkMs = now;
    blinkState = !blinkState;
    digitalWrite(MSV2_SAFE_BOOT_LED_PIN, blinkState ? HIGH : LOW);
  }

  if (now - lastPrintMs >= 1000)
  {
    lastPrintMs = now;
    Serial.print("[MSV2 safe boot] alive ms=");
    Serial.println(now);
  }
}

#else

#include <ArduinoJson.h>
#if MSV2_ENABLE_BLE
#include <BLEDevice.h>
#include <BLEScan.h>
#endif
#include <esp_system.h>
#include <string.h>
#if MSV2_ENABLE_WIFI
#include <LittleFS.h>
#include "MobileAppPage.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#if __has_include("WiFiCredentials.h")
#include "WiFiCredentials.h"
#endif
#endif

const char AP_SSID[] = "MSV2-Display";
const char AP_PASSWORD[] = "moisture";
const char LOCAL_CONFIG_SOURCE[] = "receiver-local";

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef MSV2_CLOUD_BOOTSTRAP_URL
#define MSV2_CLOUD_BOOTSTRAP_URL "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap"
#endif

const char UI_PATH[] = "/ui.json";
const char WIFI_CONFIG_PATH[] = "/wifi.json";
const char DEVICE_CONFIG_PATH[] = "/device.json";
const char CLOUD_CONFIG_PATH[] = "/cloud.json";
const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint8_t MSV2_MAGIC[] = { 'M', 'S', 'V', '2' };
const uint8_t MAX_SENSOR_ID = 16;
const uint32_t BLE_SCAN_SECONDS = 10;
const uint32_t BLE_SCAN_IDLE_MS = 1;
const uint32_t DEFAULT_STALE_SECONDS = 90;
const uint8_t BATTERY_UNKNOWN = 255;
const uint8_t MAX_ART_SLOTS = 16;
const uint32_t SENSOR_RENDER_INTERVAL_MS = 10000;
const uint32_t CLOUD_BOOTSTRAP_FIRST_TRY_MS = 15000;
const uint32_t CLOUD_BOOTSTRAP_RETRY_MS = 300000;

struct SensorReading
{
  bool seen = false;
  uint32_t wake = 0;
  uint16_t moisture = 0;
  uint8_t batteryPercent = BATTERY_UNKNOWN;
  int rssi = 0;
  uint32_t lastSeenMs = 0;
  uint32_t missed = 0;
  uint32_t received = 0;
  uint32_t missedTotal = 0;
};

#if MSV2_ENABLE_DISPLAY
const int TFT_SCK = 12;
const int TFT_MOSI = 13;
const int TFT_CS = 14;
const int TFT_DC = 15;
const int TFT_RST = 16;

const uint16_t TFT_BLACK = 0x0000;
const uint16_t TFT_NAVY = 0x000F;
const uint16_t TFT_DARKGREY = 0x7BEF;
const uint16_t TFT_WHITE = 0xFFFF;
const uint16_t TFT_RED = 0xF800;
const uint16_t TFT_GREEN = 0x07E0;
const uint16_t TFT_ORANGE = 0xFD20;

Arduino_DataBus* busPtr = nullptr;
Arduino_GFX* gfxPtr = nullptr;
#endif
#if MSV2_ENABLE_WIFI
WebServer* serverPtr = nullptr;
#endif
#if MSV2_ENABLE_BLE
BLEScan* bleScan = nullptr;
TaskHandle_t bleScanTaskHandle = nullptr;
#endif
SensorReading sensors[MAX_SENSOR_ID + 1];

struct PlantArtSlot
{
  bool enabled = false;
  uint16_t sensorId = 0;
  char art[24] = "sprout_pot";
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  uint16_t surfaceColor = 0;
  uint16_t backgroundColor = 0;
  uint16_t textColor = 0;
  uint16_t activeColor = 0;
  uint16_t mutedColor = 0;
  uint16_t warningColor = 0;
  uint16_t dangerColor = 0;
  int dry = 350;
  int wet = 800;
  uint8_t frame = 0;
  uint16_t frameIntervalMs = 200;
  uint32_t nextFrameMs = 0;
};

PlantArtSlot artSlots[MAX_ART_SLOTS];
uint8_t artSlotCount = 0;

#if MSV2_ENABLE_DISPLAY
#define tft (*gfxPtr)
#endif
#if MSV2_ENABLE_WIFI
#define server (*serverPtr)
#endif

bool displayDirty = true;
volatile bool sensorDataDirty = false;
uint32_t lastSensorRenderMs = 0;
uint32_t staleSeconds = DEFAULT_STALE_SECONDS;
RTC_DATA_ATTR char lastCheckpoint[48] = "fresh boot";
bool wifiStationConnected = false;
bool storedWifiLoaded = false;
char configuredWifiSsid[64] = "";
char configuredWifiPassword[96] = "";
char receiverId[20] = "";
char receiverSecret[33] = "";
char receiverClaimCode[10] = "";
uint32_t uiConfigVersion = 0;
uint32_t uiUpdatedMs = 0;
char uiUpdatedBy[24] = "unknown";
bool cloudEnabled = false;
char cloudUploadUrl[160] = "";
char cloudApiToken[80] = "";
uint32_t cloudSyncIntervalSeconds = 900;
uint32_t nextCloudSyncMs = 0;
uint32_t cloudLastAttemptMs = 0;
uint32_t cloudLastSuccessMs = 0;
int cloudLastHttpStatus = 0;
char cloudLastError[96] = "";
uint32_t nextCloudBootstrapMs = 0;
uint32_t cloudBootstrapLastAttemptMs = 0;
uint32_t cloudBootstrapLastSuccessMs = 0;
int cloudBootstrapLastHttpStatus = 0;
char cloudBootstrapLastError[96] = "";
uint32_t pendingRestartMs = 0;
uint8_t currentRotation = 1;
uint32_t lastArtFrameMs = 0;
const uint32_t ART_FRAME_INTERVAL_MS = 500;
const uint32_t ART_FRAME_JITTER_MS = 120;
const uint32_t ART_RENDER_POLL_MS = 25;
const uint8_t ART_FRAME_STEP = 1;

#if MSV2_ENABLE_WIFI
IPAddress currentIpAddress();
#endif

const char* resetReasonName(esp_reset_reason_t reason)
{
  switch (reason)
  {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external reset";
    case ESP_RST_SW:
      return "software reset";
    case ESP_RST_PANIC:
      return "panic/exception";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep wake";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "SDIO reset";
    default:
      return "unknown";
  }
}

void setCheckpoint(const char* message)
{
  strlcpy(lastCheckpoint, message, sizeof(lastCheckpoint));
}

void bootLog(const char* message)
{
  setCheckpoint(message);
  Serial.print("[MSV2 boot] ");
  Serial.println(message);
  Serial.flush();
}

uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t parseColor(const char* value, uint16_t fallback)
{
  if (value == nullptr || value[0] != '#')
  {
    return fallback;
  }

  char* end = nullptr;
  uint32_t rgb = strtoul(value + 1, &end, 16);
  if (end == value + 1)
  {
    return fallback;
  }

  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return color565(r, g, b);
}

void cacheUiMetadata(JsonVariantConst meta)
{
  uiConfigVersion = meta["version"] | 0;
  uiUpdatedMs = meta["updatedMs"] | 0;
  const char* updatedBy = meta["updatedBy"] | "unknown";
  strlcpy(uiUpdatedBy, updatedBy, sizeof(uiUpdatedBy));
}

void ensureDefaultUi()
{
  if (LittleFS.exists(UI_PATH))
  {
    return;
  }

  File file = LittleFS.open(UI_PATH, "w");
  if (!file)
  {
    return;
  }

  file.print(
    "{"
    "\"_meta\":{\"schema\":1,\"version\":1,\"updatedMs\":0,\"updatedBy\":\"factory-default\"},"
    "\"title\":\"MSV2 Garden\","
    "\"staleSeconds\":90,"
    "\"rotation\":1,"
    "\"theme\":{\"background\":\"#0E141B\",\"surface\":\"#17212B\",\"text\":\"#F5F7FA\",\"muted\":\"#8FA3AD\",\"accent\":\"#20C997\",\"warning\":\"#FFB020\",\"danger\":\"#FF5A5F\"},"
    "\"layout\":{\"type\":\"grid\",\"orientation\":\"landscape\",\"columns\":4,\"rows\":2,\"gap\":6,\"padding\":6},"
    "\"cards\":["
    "{\"sensor\":1,\"plant\":\"Basil\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":2,\"plant\":\"Mint\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":3,\"plant\":\"Thyme\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":4,\"plant\":\"Sage\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":5,\"plant\":\"Rosemary\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":6,\"plant\":\"Oregano\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":7,\"plant\":\"Parsley\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":350,\"wet\":800}},"
    "{\"sensor\":8,\"plant\":\"Cilantro\",\"art\":\"cactus_cluster\",\"moisture\":{\"dry\":350,\"wet\":800}}"
    "]"
    "}"
  );
  file.close();
}

void ensureUiMetadata()
{
  File file = LittleFS.open(UI_PATH, "r");
  if (!file)
  {
    return;
  }

  DynamicJsonDocument doc(12288);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error)
  {
    return;
  }

  JsonVariant metaVariant = doc["_meta"];
  if (!metaVariant.isNull() && (metaVariant["version"] | 0) > 0)
  {
    cacheUiMetadata(metaVariant);
    return;
  }

  JsonObject meta = doc["_meta"].to<JsonObject>();
  meta["schema"] = 1;
  meta["version"] = 1;
  meta["updatedMs"] = millis();
  meta["updatedBy"] = "metadata-migration";

  file = LittleFS.open(UI_PATH, "w");
  if (!file)
  {
    return;
  }
  serializeJson(doc, file);
  file.close();
  cacheUiMetadata(doc["_meta"]);
}

String readUiJson()
{
  setCheckpoint("readUiJson: open");
  File file = LittleFS.open(UI_PATH, "r");
  if (!file)
  {
    setCheckpoint("readUiJson: open failed");
    return "{}";
  }

  setCheckpoint("readUiJson: read string");
  String json = file.readString();
  setCheckpoint("readUiJson: close");
  file.close();
  DynamicJsonDocument metaDoc(12288);
  if (!deserializeJson(metaDoc, json))
  {
    cacheUiMetadata(metaDoc["_meta"]);
  }
  setCheckpoint("readUiJson: done");
  return json;
}

bool writeUiJson(const String& body)
{
  DynamicJsonDocument validation(12288);
  DeserializationError error = deserializeJson(validation, body);
  if (error)
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return false;
  }

  JsonObject meta = validation["_meta"].to<JsonObject>();
  uint32_t incomingVersion = meta["version"] | uiConfigVersion;
  uint32_t nextVersion = incomingVersion >= uiConfigVersion ? incomingVersion + 1 : uiConfigVersion + 1;
  meta["schema"] = 1;
  meta["version"] = nextVersion;
  meta["updatedMs"] = millis();
  meta["updatedBy"] = LOCAL_CONFIG_SOURCE;

  File file = LittleFS.open(UI_PATH, "w");
  if (!file)
  {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not write ui.json\"}");
    return false;
  }

  serializeJson(validation, file);
  file.close();
  staleSeconds = validation["staleSeconds"] | DEFAULT_STALE_SECONDS;
  cacheUiMetadata(validation["_meta"]);
  displayDirty = true;
  return true;
}

void randomHex(char* output, size_t outputLen)
{
  static const char hex[] = "0123456789ABCDEF";
  if (outputLen == 0)
  {
    return;
  }

  for (size_t i = 0; i + 1 < outputLen; i++)
  {
    if ((i % 8) == 0)
    {
      randomSeed(esp_random());
    }
    output[i] = hex[random(0, 16)];
  }
  output[outputLen - 1] = '\0';
}

void generateClaimCode(char* output, size_t outputLen)
{
  static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  if (outputLen < 10)
  {
    return;
  }

  for (uint8_t i = 0; i < 9; i++)
  {
    output[i] = (i == 4) ? '-' : alphabet[random(0, (int)strlen(alphabet))];
  }
  output[9] = '\0';
}

bool writeDeviceIdentity()
{
  StaticJsonDocument<256> doc;
  doc["receiverId"] = receiverId;
  doc["receiverSecret"] = receiverSecret;
  doc["claimCode"] = receiverClaimCode;

  File file = LittleFS.open(DEVICE_CONFIG_PATH, "w");
  if (!file)
  {
    return false;
  }

  serializeJson(doc, file);
  file.close();
  return true;
}

bool loadDeviceIdentity()
{
  receiverId[0] = '\0';
  receiverSecret[0] = '\0';
  receiverClaimCode[0] = '\0';

  if (LittleFS.exists(DEVICE_CONFIG_PATH))
  {
    File file = LittleFS.open(DEVICE_CONFIG_PATH, "r");
    if (file)
    {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      const char* id = doc["receiverId"] | "";
      const char* secret = doc["receiverSecret"] | "";
      const char* claimCode = doc["claimCode"] | "";
      if (!error && id[0] != '\0' && secret[0] != '\0' && claimCode[0] != '\0')
      {
        strlcpy(receiverId, id, sizeof(receiverId));
        strlcpy(receiverSecret, secret, sizeof(receiverSecret));
        strlcpy(receiverClaimCode, claimCode, sizeof(receiverClaimCode));
        return true;
      }
    }
  }

  uint64_t mac = ESP.getEfuseMac();
  snprintf(receiverId, sizeof(receiverId), "MSV2%012llX", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
  randomHex(receiverSecret, sizeof(receiverSecret));
  generateClaimCode(receiverClaimCode, sizeof(receiverClaimCode));
  return writeDeviceIdentity();
}

String claimInfoJson()
{
  StaticJsonDocument<512> doc;
  doc["receiverId"] = receiverId;
  doc["claimCode"] = receiverClaimCode;
  doc["name"] = "MSV2 Display";
  doc["build"] = MSV2_BUILD_TAG;
#if MSV2_ENABLE_WIFI
  doc["networkMode"] = wifiStationConnected ? "station" : "access-point";
  doc["ip"] = currentIpAddress().toString();
#else
  doc["networkMode"] = "offline";
  doc["ip"] = "0.0.0.0";
#endif
  doc["claimPath"] = "/claim";

  String output;
  serializeJson(doc, output);
  return output;
}

bool loadStoredWifiConfig()
{
  storedWifiLoaded = false;
  configuredWifiSsid[0] = '\0';
  configuredWifiPassword[0] = '\0';

  if (!LittleFS.exists(WIFI_CONFIG_PATH))
  {
    return false;
  }

  File file = LittleFS.open(WIFI_CONFIG_PATH, "r");
  if (!file)
  {
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error)
  {
    return false;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  if (ssid == nullptr || ssid[0] == '\0')
  {
    return false;
  }

  strlcpy(configuredWifiSsid, ssid, sizeof(configuredWifiSsid));
  strlcpy(configuredWifiPassword, password == nullptr ? "" : password, sizeof(configuredWifiPassword));
  storedWifiLoaded = true;
  return true;
}

bool loadCloudConfig()
{
  cloudEnabled = false;
  cloudUploadUrl[0] = '\0';
  cloudApiToken[0] = '\0';
  cloudSyncIntervalSeconds = 900;
  nextCloudSyncMs = 0;
  cloudLastAttemptMs = 0;
  cloudLastSuccessMs = 0;
  cloudLastHttpStatus = 0;
  cloudLastError[0] = '\0';
  nextCloudBootstrapMs = 0;
  cloudBootstrapLastAttemptMs = 0;
  cloudBootstrapLastSuccessMs = 0;
  cloudBootstrapLastHttpStatus = 0;
  cloudBootstrapLastError[0] = '\0';

  if (!LittleFS.exists(CLOUD_CONFIG_PATH))
  {
    return false;
  }

  File file = LittleFS.open(CLOUD_CONFIG_PATH, "r");
  if (!file)
  {
    return false;
  }

  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error)
  {
    return false;
  }

  cloudEnabled = doc["enabled"] | false;
  strlcpy(cloudUploadUrl, doc["uploadUrl"] | "", sizeof(cloudUploadUrl));
  strlcpy(cloudApiToken, doc["apiToken"] | "", sizeof(cloudApiToken));
  cloudSyncIntervalSeconds = constrain((uint32_t)(doc["syncIntervalSeconds"] | 900), 60UL, 86400UL);
  nextCloudSyncMs = cloudEnabled ? millis() + 5000UL : 0;
  return true;
}

String cloudConfigJson()
{
  StaticJsonDocument<1536> doc;
  doc["enabled"] = cloudEnabled;
  doc["uploadUrl"] = cloudUploadUrl;
  doc["syncIntervalSeconds"] = cloudSyncIntervalSeconds;
  doc["hasApiToken"] = cloudApiToken[0] != '\0';
  doc["status"] = cloudEnabled ? "enabled" : "disabled";
  doc["note"] = cloudEnabled ? "Snapshot uploader is enabled on the receiver." : "Stored locally; snapshot upload is disabled.";
  JsonObject upload = doc.createNestedObject("upload");
  upload["lastAttemptMs"] = cloudLastAttemptMs;
  upload["lastSuccessMs"] = cloudLastSuccessMs;
  upload["lastHttpStatus"] = cloudLastHttpStatus;
  upload["lastError"] = cloudLastError;
  upload["nextAttemptMs"] = cloudEnabled ? nextCloudSyncMs : 0;
  JsonObject bootstrap = doc.createNestedObject("bootstrap");
  bootstrap["configured"] = strlen(MSV2_CLOUD_BOOTSTRAP_URL) > 0;
  bootstrap["url"] = MSV2_CLOUD_BOOTSTRAP_URL;
  bootstrap["lastAttemptMs"] = cloudBootstrapLastAttemptMs;
  bootstrap["lastSuccessMs"] = cloudBootstrapLastSuccessMs;
  bootstrap["lastHttpStatus"] = cloudBootstrapLastHttpStatus;
  bootstrap["lastError"] = cloudBootstrapLastError;
  bootstrap["nextAttemptMs"] = nextCloudBootstrapMs;

  String output;
  serializeJson(doc, output);
  return output;
}

bool persistCloudConfig()
{
  StaticJsonDocument<768> stored;
  stored["enabled"] = cloudEnabled;
  stored["uploadUrl"] = cloudUploadUrl;
  stored["apiToken"] = cloudApiToken;
  stored["syncIntervalSeconds"] = cloudSyncIntervalSeconds;

  File file = LittleFS.open(CLOUD_CONFIG_PATH, "w");
  if (!file)
  {
    return false;
  }

  serializeJson(stored, file);
  file.close();
  return true;
}

bool writeCloudConfigJson(const String& body)
{
  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error)
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return false;
  }

  bool enabled = doc["enabled"] | false;
  const char* uploadUrl = doc["uploadUrl"] | "";
  const char* apiToken = doc["apiToken"] | "";
  bool clearToken = doc["clearToken"] | false;
  uint32_t syncIntervalSeconds = constrain((uint32_t)(doc["syncIntervalSeconds"] | cloudSyncIntervalSeconds), 60UL, 86400UL);

  if (enabled && (uploadUrl == nullptr || uploadUrl[0] == '\0'))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"uploadUrl required when enabled\"}");
    return false;
  }
  if (uploadUrl != nullptr && strlen(uploadUrl) >= sizeof(cloudUploadUrl))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"uploadUrl too long\"}");
    return false;
  }
  if (apiToken != nullptr && strlen(apiToken) >= sizeof(cloudApiToken))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"apiToken too long\"}");
    return false;
  }

  cloudEnabled = enabled;
  strlcpy(cloudUploadUrl, uploadUrl == nullptr ? "" : uploadUrl, sizeof(cloudUploadUrl));
  if (clearToken)
  {
    cloudApiToken[0] = '\0';
  }
  else if (apiToken != nullptr && apiToken[0] != '\0')
  {
    strlcpy(cloudApiToken, apiToken, sizeof(cloudApiToken));
  }
  cloudSyncIntervalSeconds = syncIntervalSeconds;
  nextCloudSyncMs = cloudEnabled ? millis() + 5000UL : 0;
  cloudLastError[0] = '\0';

  if (!persistCloudConfig())
  {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not write cloud config\"}");
    return false;
  }

  server.send(200, "application/json", "{\"ok\":true}");
  return true;
}

bool resetCloudConfig()
{
  if (LittleFS.exists(CLOUD_CONFIG_PATH))
  {
    LittleFS.remove(CLOUD_CONFIG_PATH);
  }
  cloudEnabled = false;
  cloudUploadUrl[0] = '\0';
  cloudApiToken[0] = '\0';
  cloudSyncIntervalSeconds = 900;
  nextCloudSyncMs = 0;
  cloudLastAttemptMs = 0;
  cloudLastSuccessMs = 0;
  cloudLastHttpStatus = 0;
  cloudLastError[0] = '\0';
  nextCloudBootstrapMs = millis() + CLOUD_BOOTSTRAP_FIRST_TRY_MS;
  server.send(200, "application/json", "{\"ok\":true}");
  return true;
}

String wifiStatusJson()
{
  StaticJsonDocument<512> doc;
  doc["networkMode"] = wifiStationConnected ? "station" : "access-point";
  doc["ip"] = currentIpAddress().toString();
  doc["setupSsid"] = AP_SSID;
  doc["hasStoredCredentials"] = storedWifiLoaded;
  doc["configuredSsid"] = configuredWifiSsid;
#if MSV2_ENABLE_WIFI
  doc["connectedSsid"] = wifiStationConnected ? WiFi.SSID() : "";
  doc["rssi"] = wifiStationConnected ? WiFi.RSSI() : 0;
#else
  doc["connectedSsid"] = "";
  doc["rssi"] = 0;
#endif

  String output;
  serializeJson(doc, output);
  return output;
}

bool writeWifiConfigJson(const String& body)
{
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error)
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return false;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  if (ssid == nullptr || ssid[0] == '\0')
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return false;
  }
  if (strlen(ssid) >= sizeof(configuredWifiSsid))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid too long\"}");
    return false;
  }
  if (password != nullptr && strlen(password) >= sizeof(configuredWifiPassword))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"password too long\"}");
    return false;
  }

  StaticJsonDocument<512> stored;
  stored["ssid"] = ssid;
  stored["password"] = password == nullptr ? "" : password;

  File file = LittleFS.open(WIFI_CONFIG_PATH, "w");
  if (!file)
  {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not write wifi config\"}");
    return false;
  }
  serializeJson(stored, file);
  file.close();

  strlcpy(configuredWifiSsid, ssid, sizeof(configuredWifiSsid));
  strlcpy(configuredWifiPassword, password == nullptr ? "" : password, sizeof(configuredWifiPassword));
  storedWifiLoaded = true;
  pendingRestartMs = millis() + 900;
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  return true;
}

bool forgetWifiConfig()
{
  if (LittleFS.exists(WIFI_CONFIG_PATH))
  {
    LittleFS.remove(WIFI_CONFIG_PATH);
  }
  storedWifiLoaded = false;
  configuredWifiSsid[0] = '\0';
  configuredWifiPassword[0] = '\0';
  pendingRestartMs = millis() + 900;
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  return true;
}

int clampInt(int value, int low, int high)
{
  if (value < low)
  {
    return low;
  }
  if (value > high)
  {
    return high;
  }
  return value;
}

void applyDisplayRotation(uint8_t requestedRotation, bool forceLandscape)
{
  requestedRotation = (uint8_t)clampInt(requestedRotation, 0, 3);
  tft.setRotation(requestedRotation);
  currentRotation = requestedRotation;

  if (forceLandscape && tft.width() < tft.height())
  {
    const uint8_t rotations[] = { 1, 3, 0, 2 };
    for (uint8_t i = 0; i < sizeof(rotations); i++)
    {
      tft.setRotation(rotations[i]);
      if (tft.width() > tft.height())
      {
        currentRotation = rotations[i];
        return;
      }
    }

    tft.setRotation(currentRotation);
  }
}

bool layoutWantsLandscape(JsonObject layout)
{
  const char* orientation = layout["orientation"] | "landscape";
  return strcmp(orientation, "portrait") != 0;
}

const char* firstString(JsonObject object, const char* key)
{
  JsonVariant value = object[key];
  if (value.is<const char*>())
  {
    const char* text = value.as<const char*>();
    if (text != nullptr && text[0] != '\0')
    {
      return text;
    }
  }
  return nullptr;
}

int textPixelWidth(const char* text, uint8_t size)
{
  if (text == nullptr)
  {
    return 0;
  }
  return (int)strlen(text) * 6 * size;
}

void drawTextLeft(const char* text, int x, int y, uint8_t size, uint16_t color, uint16_t backgroundColor)
{
  tft.setTextSize(size);
  tft.setTextColor(color, backgroundColor);
  tft.setCursor(x, y);
  tft.print(text == nullptr ? "" : text);
}

void drawTextRight(const char* text, int rightX, int y, uint8_t size, uint16_t color, uint16_t backgroundColor)
{
  drawTextLeft(text, rightX - textPixelWidth(text, size), y, size, color, backgroundColor);
}

void drawTextCenter(const char* text, int centerX, int y, uint8_t size, uint16_t color, uint16_t backgroundColor)
{
  drawTextLeft(text, centerX - textPixelWidth(text, size) / 2, y, size, color, backgroundColor);
}

uint8_t moisturePercent(uint16_t moisture, int dry, int wet)
{
  if (wet <= dry)
  {
    wet = dry + 1;
  }
  long percent = ((long)moisture - dry) * 100L / (wet - dry);
  return (uint8_t)clampInt((int)percent, 0, 100);
}

uint16_t healthColor(uint8_t percent, uint16_t accentColor, uint16_t warningColor, uint16_t dangerColor, uint16_t mutedColor)
{
  if (percent == BATTERY_UNKNOWN)
  {
    return mutedColor;
  }
  if (percent < 25)
  {
    return dangerColor;
  }
  if (percent < 50)
  {
    return warningColor;
  }
  return accentColor;
}

void drawBar(int x, int y, int w, int h, uint8_t percent, uint16_t fillColor, uint16_t trackColor)
{
  uint8_t clamped = percent == BATTERY_UNKNOWN ? 0 : (uint8_t)clampInt(percent, 0, 100);
  int fillW = (w - 4) * clamped / 100;

  tft.drawRoundRect(x, y, w, h, 3, trackColor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 2, trackColor);
  if (fillW > 0)
  {
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, fillColor);
  }
}

const PlantSprite* spriteForArt(const char* art)
{
  return findPlantSpriteByKey(art);
}

void drawPlantSpriteScaled(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t frame)
{
  uint8_t scaleDiv = 2;
  while (scaleDiv < 5 && ((sprite.w / scaleDiv) > maxW || (sprite.h / scaleDiv) > maxH))
  {
    scaleDiv++;
  }

  const int drawW = sprite.w / scaleDiv;
  const int drawH = sprite.h / scaleDiv;
  const int startX = centerX - drawW / 2;
  const int startY = topY + max(0, (maxH - drawH) / 2);
  const uint8_t pose = pgm_read_byte(&sprite.sequence[frame % sprite.frameCount]);
  const uint16_t* bitmap = (const uint16_t*)pgm_read_ptr(&sprite.frames[pose]);

  tft.startWrite();
  for (int yy = 0; yy < drawH; yy++)
  {
    const int sourceY = yy * scaleDiv;
    for (int xx = 0; xx < drawW; xx++)
    {
      const int sourceX = xx * scaleDiv;
      const uint16_t c = pgm_read_word(&bitmap[sourceY * sprite.w + sourceX]);
      if (c != PLANT_TRANSPARENT)
      {
        tft.writePixel(startX + xx, startY + yy, c);
      }
    }
  }
  tft.endWrite();
}

void spriteDrawGeometry(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t& scaleDiv, int& drawW, int& drawH, int& startX, int& startY)
{
  scaleDiv = 2;
  while (scaleDiv < 5 && ((sprite.w / scaleDiv) > maxW || (sprite.h / scaleDiv) > maxH))
  {
    scaleDiv++;
  }

  drawW = sprite.w / scaleDiv;
  drawH = sprite.h / scaleDiv;
  startX = centerX - drawW / 2;
  startY = topY + max(0, (maxH - drawH) / 2);
}

uint16_t spriteScaledPixel(const PlantSprite& sprite, const uint16_t* bitmap, uint8_t scaleDiv, int x, int y)
{
  const int sourceX = x * scaleDiv;
  const int sourceY = y * scaleDiv;
  return pgm_read_word(&bitmap[sourceY * sprite.w + sourceX]);
}

void drawPlantSpriteDiffScaled(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t oldFrame, uint8_t newFrame, uint16_t backgroundColor)
{
  uint8_t scaleDiv = 2;
  int drawW = 0;
  int drawH = 0;
  int startX = 0;
  int startY = 0;
  spriteDrawGeometry(sprite, centerX, topY, maxW, maxH, scaleDiv, drawW, drawH, startX, startY);

  const uint8_t oldPose = pgm_read_byte(&sprite.sequence[oldFrame % sprite.frameCount]);
  const uint8_t newPose = pgm_read_byte(&sprite.sequence[newFrame % sprite.frameCount]);
  if (oldPose == newPose)
  {
    return;
  }

  const uint16_t* oldBitmap = (const uint16_t*)pgm_read_ptr(&sprite.frames[oldPose]);
  const uint16_t* newBitmap = (const uint16_t*)pgm_read_ptr(&sprite.frames[newPose]);

  tft.startWrite();
  for (int yy = 0; yy < drawH; yy++)
  {
    for (int xx = 0; xx < drawW; xx++)
    {
      const uint16_t oldColor = spriteScaledPixel(sprite, oldBitmap, scaleDiv, xx, yy);
      const uint16_t newColor = spriteScaledPixel(sprite, newBitmap, scaleDiv, xx, yy);
      if (oldColor == newColor)
      {
        continue;
      }
      tft.writePixel(startX + xx, startY + yy, newColor == PLANT_TRANSPARENT ? backgroundColor : newColor);
    }
  }
  tft.endWrite();
}

void drawAsciiPlantArt(int x, int y, const char* art, uint16_t color, uint16_t backgroundColor)
{
  const char* line1 = "  /\\";
  const char* line2 = " /  \\";
  const char* line3 = "  ||";

  if (art != nullptr && strcmp(art, "leafy") == 0)
  {
    line1 = "\\ | /";
    line2 = " \\|/";
    line3 = "  |";
  }
  else if (art != nullptr && strcmp(art, "tall") == 0)
  {
    line1 = "  |";
    line2 = "\\ |";
    line3 = " \\|/";
  }

  drawTextCenter(line1, x, y, 1, color, backgroundColor);
  drawTextCenter(line2, x, y + 15, 1, color, backgroundColor);
  drawTextCenter(line3, x, y + 30, 1, color, backgroundColor);
}

uint16_t randomizedArtInterval()
{
  uint32_t base = ART_FRAME_INTERVAL_MS;
  uint32_t jitter = ART_FRAME_JITTER_MS;
  uint32_t minInterval = base > jitter ? base - jitter : 40;
  uint32_t maxInterval = base + jitter + 1;
  return (uint16_t)random(minInterval, maxInterval);
}

void drawPlantArt(int x, int y, int maxW, int maxH, const char* art, uint16_t color, uint16_t backgroundColor, uint8_t frame)
{
  const PlantSprite* sprite = spriteForArt(art);
  if (sprite != nullptr)
  {
    drawPlantSpriteScaled(*sprite, x, y, maxW, maxH, frame);
    return;
  }

  drawAsciiPlantArt(x, y + max(0, (maxH - 45) / 2), art, color, backgroundColor);
}

void drawPlantArtInCard(int x, int y, int w, int h, const char* art, uint16_t color, uint16_t surfaceColor, uint8_t frame)
{
  const int artX = x + 7;
  const int artY = y + 24;
  const int artW = w - 14;
  const int artH = max(24, h - 91);

  tft.fillRect(artX, artY, artW, artH, surfaceColor);
  drawPlantArt(x + w / 2, artY, artW, artH, art, color, surfaceColor, frame);
}

void drawPlantArtDiffInCard(int x, int y, int w, int h, const char* art, uint16_t color, uint16_t surfaceColor, uint8_t oldFrame, uint8_t newFrame)
{
  const int artX = x + 7;
  const int artY = y + 24;
  const int artW = w - 14;
  const int artH = max(24, h - 91);
  const PlantSprite* sprite = spriteForArt(art);

  if (sprite != nullptr)
  {
    drawPlantSpriteDiffScaled(*sprite, x + w / 2, artY, artW, artH, oldFrame, newFrame, surfaceColor);
    return;
  }

  tft.fillRect(artX, artY, artW, artH, surfaceColor);
  drawPlantArt(x + w / 2, artY, artW, artH, art, color, surfaceColor, newFrame);
}

void cachePlantArtSlot(
  int x,
  int y,
  int w,
  int h,
  JsonObject card,
  uint16_t backgroundColor,
  uint16_t surfaceColor,
  uint16_t textColor,
  uint16_t activeColor,
  uint16_t mutedColor,
  uint16_t warningColor,
  uint16_t dangerColor
)
{
  if (artSlotCount >= MAX_ART_SLOTS)
  {
    return;
  }

  PlantArtSlot& slot = artSlots[artSlotCount++];
  slot.enabled = true;
  slot.sensorId = card["sensor"] | 0;
  strlcpy(slot.art, card["art"] | "sprout_pot", sizeof(slot.art));
  JsonObject moistureConfig = card["moisture"].as<JsonObject>();
  slot.dry = moistureConfig["dry"] | card["dry"] | 350;
  slot.wet = moistureConfig["wet"] | card["wet"] | 800;
  slot.x = x;
  slot.y = y;
  slot.w = w;
  slot.h = h;
  slot.backgroundColor = backgroundColor;
  slot.surfaceColor = surfaceColor;
  slot.textColor = textColor;
  slot.activeColor = activeColor;
  slot.mutedColor = mutedColor;
  slot.warningColor = warningColor;
  slot.dangerColor = dangerColor;
  slot.frame = 0;
  slot.frameIntervalMs = randomizedArtInterval();
  slot.nextFrameMs = millis() + random(0, ART_FRAME_INTERVAL_MS + ART_FRAME_JITTER_MS + 1);
}

void drawPlantCard(
  int x,
  int y,
  int w,
  int h,
  JsonObject card,
  uint16_t backgroundColor,
  uint16_t surfaceColor,
  uint16_t textColor,
  uint16_t mutedColor,
  uint16_t accentColor,
  uint16_t warningColor,
  uint16_t dangerColor
)
{
  uint16_t sensorId = card["sensor"] | 0;
  const char* plant = firstString(card, "plant");
  if (plant == nullptr)
  {
    plant = firstString(card, "label");
  }
  if (plant == nullptr)
  {
    plant = firstString(card, "name");
  }
  if (plant == nullptr)
  {
    plant = firstString(card, "title");
  }

  char fallbackPlant[16];
  if (plant == nullptr)
  {
    snprintf(fallbackPlant, sizeof(fallbackPlant), "Probe %u", sensorId);
    plant = fallbackPlant;
  }
  const char* art = card["art"] | "sprout";
  JsonObject moistureConfig = card["moisture"].as<JsonObject>();
  int dry = moistureConfig["dry"] | card["dry"] | 350;
  int wet = moistureConfig["wet"] | card["wet"] | 800;

  const bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[sensorId] : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  uint8_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, dry, wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = healthColor(moisturePct, accentColor, warningColor, dangerColor, mutedColor);
  uint16_t batteryColor = healthColor(batteryPct, accentColor, warningColor, dangerColor, mutedColor);

  tft.fillRoundRect(x, y, w, h, 5, surfaceColor);
  tft.drawRoundRect(x, y, w, h, 5, stale ? mutedColor : moistureColor);

  drawTextLeft(plant, x + 7, y + 6, 1, textColor, surfaceColor);

  char batteryText[8];
  if (batteryPct == BATTERY_UNKNOWN)
  {
    strlcpy(batteryText, "--%", sizeof(batteryText));
  }
  else
  {
    snprintf(batteryText, sizeof(batteryText), "%u%%", batteryPct);
  }

  drawPlantArtInCard(x, y, w, h, art, stale ? mutedColor : accentColor, surfaceColor, 0);

  char moistureText[8];
  if (stale)
  {
    strlcpy(moistureText, "--", sizeof(moistureText));
  }
  else
  {
    snprintf(moistureText, sizeof(moistureText), "%u", reading->moisture);
  }

  drawTextLeft("Moist", x + 7, y + h - 62, 1, mutedColor, surfaceColor);
  drawTextRight(moistureText, x + w - 7, y + h - 62, 1, moistureColor, surfaceColor);
  drawBar(x + 7, y + h - 47, w - 14, 9, moisturePct, moistureColor, backgroundColor);

  drawTextLeft("Batt", x + 7, y + h - 29, 1, mutedColor, surfaceColor);
  drawTextRight(batteryText, x + w - 7, y + h - 29, 1, batteryColor, surfaceColor);
  drawBar(x + 7, y + h - 14, w - 14, 8, batteryPct, batteryColor, backgroundColor);
}

void drawPlantCardDataOnly(const PlantArtSlot& slot)
{
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;
  const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[slot.sensorId] : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  uint8_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, slot.dry, slot.wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = healthColor(moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  uint16_t batteryColor = healthColor(batteryPct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);

  tft.drawRoundRect(x, y, w, h, 5, stale ? slot.mutedColor : moistureColor);

  char moistureText[8];
  if (stale)
  {
    strlcpy(moistureText, "--", sizeof(moistureText));
  }
  else
  {
    snprintf(moistureText, sizeof(moistureText), "%u", reading->moisture);
  }

  char batteryText[8];
  if (batteryPct == BATTERY_UNKNOWN)
  {
    strlcpy(batteryText, "--%", sizeof(batteryText));
  }
  else
  {
    snprintf(batteryText, sizeof(batteryText), "%u%%", batteryPct);
  }

  tft.fillRect(x + 7, y + h - 63, w - 14, 26, slot.surfaceColor);
  drawTextLeft("Moist", x + 7, y + h - 62, 1, slot.mutedColor, slot.surfaceColor);
  drawTextRight(moistureText, x + w - 7, y + h - 62, 1, moistureColor, slot.surfaceColor);
  drawBar(x + 7, y + h - 47, w - 14, 9, moisturePct, moistureColor, slot.backgroundColor);

  tft.fillRect(x + 7, y + h - 30, w - 14, 24, slot.surfaceColor);
  drawTextLeft("Batt", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
  drawTextRight(batteryText, x + w - 7, y + h - 29, 1, batteryColor, slot.surfaceColor);
  drawBar(x + 7, y + h - 14, w - 14, 8, batteryPct, batteryColor, slot.backgroundColor);
}

void renderDashboardDataOnly()
{
#if !MSV2_ENABLE_DISPLAY
  return;
#endif

  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    if (artSlots[i].enabled)
    {
      drawPlantCardDataOnly(artSlots[i]);
    }
  }
}

void drawLegacyRow(
  int y,
  const char* label,
  uint16_t sensorId,
  uint16_t textColor,
  uint16_t mutedColor,
  uint16_t accentColor,
  uint16_t warningColor,
  uint16_t backgroundColor,
  int warnBelow
)
{
  const bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[sensorId] : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  const uint16_t valueColor = stale ? mutedColor : (reading->moisture < warnBelow ? warningColor : accentColor);

  tft.fillRect(0, y, tft.width(), 42, backgroundColor);
  drawTextLeft(label == nullptr ? "Probe" : label, 8, y + 2, 1, textColor, backgroundColor);

  char value[12];
  if (stale)
  {
    strlcpy(value, "--", sizeof(value));
  }
  else
  {
    snprintf(value, sizeof(value), "%u", reading->moisture);
  }
  drawTextRight(value, tft.width() - 8, y, 2, valueColor, backgroundColor);
  (void)mutedColor;
}

void renderUi()
{
#if !MSV2_ENABLE_DISPLAY
  return;
#endif

  setCheckpoint("render: read json");
  String json = readUiJson();
  setCheckpoint("render: allocate json doc");
  DynamicJsonDocument doc(8192);
  setCheckpoint("render: parse json");
  DeserializationError error = deserializeJson(doc, json);

  if (error)
  {
    setCheckpoint("render: parse error draw");
    tft.fillScreen(TFT_BLACK);
    drawTextLeft("ui.json parse error", 8, 8, 1, TFT_RED, TFT_BLACK);
    return;
  }

  setCheckpoint("render: colors");
  JsonObject theme = doc["theme"];
  uint16_t backgroundColor = parseColor(theme["background"] | "#101820", TFT_BLACK);
  uint16_t surfaceColor = parseColor(theme["surface"] | "#17212B", TFT_NAVY);
  uint16_t textColor = parseColor(theme["text"] | "#F2F5F7", TFT_WHITE);
  uint16_t mutedColor = parseColor(theme["muted"] | "#8FA3AD", TFT_DARKGREY);
  uint16_t accentColor = parseColor(theme["accent"] | "#20C997", TFT_GREEN);
  uint16_t warningColor = parseColor(theme["warning"] | "#FFB020", TFT_ORANGE);
  uint16_t dangerColor = parseColor(theme["danger"] | "#FF5A5F", TFT_RED);
  staleSeconds = doc["staleSeconds"] | DEFAULT_STALE_SECONDS;
  JsonObject layout = doc["layout"];
  uint8_t requestedRotation = doc["rotation"] | currentRotation;
  applyDisplayRotation(requestedRotation, layoutWantsLandscape(layout));

  setCheckpoint("render: header draw");
  tft.fillScreen(backgroundColor);
  const char* title = doc["title"] | "MSV2";
  drawTextLeft(title, 8, 8, 2, textColor, backgroundColor);

  setCheckpoint("render: cards draw");
  JsonArray cards = doc["cards"].as<JsonArray>();
  artSlotCount = 0;
  if (!cards.isNull() && cards.size() > 0)
  {
    int columns = clampInt(layout["columns"] | 4, 1, 4);
    int rows = clampInt(layout["rows"] | 2, 1, 4);
    int gap = clampInt(layout["gap"] | 6, 2, 12);
    int padding = clampInt(layout["padding"] | 6, 2, 16);
    int top = 34;
    int cardW = (tft.width() - padding * 2 - gap * (columns - 1)) / columns;
    int cardH = (tft.height() - top - padding - gap * (rows - 1)) / rows;
    int index = 0;

    for (JsonVariant cardValue : cards)
    {
      if (index >= columns * rows)
      {
        break;
      }
      JsonObject card = cardValue.as<JsonObject>();
      if (card.isNull())
      {
        continue;
      }
      int col = index % columns;
      int row = index / columns;
      int x = padding + col * (cardW + gap);
      int y = top + row * (cardH + gap);
      drawPlantCard(
        x,
        y,
        cardW,
        cardH,
        card,
        backgroundColor,
        surfaceColor,
        textColor,
        mutedColor,
        accentColor,
        warningColor,
        dangerColor
      );
      cachePlantArtSlot(x, y, cardW, cardH, card, backgroundColor, surfaceColor, textColor, accentColor, mutedColor, warningColor, dangerColor);
      index++;
    }
  }
  else
  {
    tft.drawFastHLine(8, 40, tft.width() - 16, mutedColor);
    int y = 50;
    JsonArray fields = doc["fields"].as<JsonArray>();
    for (JsonVariant fieldValue : fields)
    {
      JsonObject field = fieldValue.as<JsonObject>();
      if (field.isNull())
      {
        continue;
      }
      uint16_t sensorId = field["sensor"] | 0;
      const char* label = field["label"] | nullptr;
      int warnBelow = field["warnBelow"] | 450;
      char fallbackLabel[16];
      if (label == nullptr || label[0] == '\0')
      {
        snprintf(fallbackLabel, sizeof(fallbackLabel), "Probe %u", sensorId);
        label = fallbackLabel;
      }

      drawLegacyRow(
        y,
        label,
        sensorId,
        textColor,
        mutedColor,
        accentColor,
        warningColor,
        backgroundColor,
        warnBelow
      );
      y += 44;
      if (y > tft.height() - 36)
      {
        break;
      }
    }
  }

  setCheckpoint("render: footer draw");
  (void)mutedColor;
  setCheckpoint("render: done");
}

void renderAnimatedPlantArt()
{
#if !MSV2_ENABLE_DISPLAY
  return;
#endif

  uint32_t now = millis();
  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    PlantArtSlot& slot = artSlots[i];
    if (!slot.enabled)
    {
      continue;
    }
    if ((int32_t)(now - slot.nextFrameMs) < 0)
    {
      continue;
    }

    uint16_t sensorId = slot.sensorId;
    const bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;
    const SensorReading* reading = validSensor ? &sensors[sensorId] : nullptr;
    const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);

    uint8_t oldFrame = slot.frame;
    slot.frame += ART_FRAME_STEP;
    slot.frameIntervalMs = randomizedArtInterval();
    slot.nextFrameMs = now + slot.frameIntervalMs;
    drawPlantArtDiffInCard(slot.x, slot.y, slot.w, slot.h, slot.art, stale ? slot.mutedColor : slot.activeColor, slot.surfaceColor, oldFrame, slot.frame);
  }
}

String sensorDataJson()
{
  StaticJsonDocument<4096> doc;
  JsonArray readings = doc.createNestedArray("sensors");
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    if (!sensors[sensorId].seen)
    {
      continue;
    }

    JsonObject item = readings.createNestedObject();
    item["sensor"] = sensorId;
    item["wake"] = sensors[sensorId].wake;
    item["moisture"] = sensors[sensorId].moisture;
    if (sensors[sensorId].batteryPercent != BATTERY_UNKNOWN)
    {
      item["batteryPercent"] = sensors[sensorId].batteryPercent;
    }
    item["rssi"] = sensors[sensorId].rssi;
    item["ageMs"] = millis() - sensors[sensorId].lastSeenMs;
    item["missed"] = sensors[sensorId].missed;
    item["received"] = sensors[sensorId].received;
    item["missedTotal"] = sensors[sensorId].missedTotal;
  }

  String output;
  serializeJson(doc, output);
  return output;
}

String snapshotJson()
{
  DynamicJsonDocument doc(8192);
  doc["receiverId"] = receiverId;
  doc["claimCode"] = receiverClaimCode;
  doc["build"] = MSV2_BUILD_TAG;
  doc["sentAtMs"] = millis();

  JsonObject config = doc.createNestedObject("config");
  config["schema"] = 1;
  config["version"] = uiConfigVersion;
  config["updatedMs"] = uiUpdatedMs;
  config["updatedBy"] = uiUpdatedBy;
#if MSV2_ENABLE_WIFI
  File uiFile = LittleFS.open(UI_PATH, "r");
  if (uiFile)
  {
    DynamicJsonDocument uiDoc(4096);
    DeserializationError uiError = deserializeJson(uiDoc, uiFile);
    uiFile.close();
    if (!uiError)
    {
      config["title"] = uiDoc["title"] | "MSV2 Plants";
      JsonArray sourceCards = uiDoc["cards"].as<JsonArray>();
      if (!sourceCards.isNull())
      {
        JsonArray configCards = config.createNestedArray("cards");
        for (JsonVariant cardValue : sourceCards)
        {
          JsonObject card = cardValue.as<JsonObject>();
          if (card.isNull())
          {
            continue;
          }

          JsonObject item = configCards.createNestedObject();
          const char* plant = firstString(card, "plant");
          if (plant == nullptr)
          {
            plant = firstString(card, "label");
          }
          if (plant == nullptr)
          {
            plant = firstString(card, "name");
          }
          if (plant == nullptr)
          {
            plant = firstString(card, "title");
          }
          if (plant == nullptr)
          {
            plant = "Plant";
          }
          item["sensor"] = card["sensor"] | 0;
          item["plant"] = plant;
          item["art"] = card["art"] | "sprout_pot";
          JsonObject sourceMoisture = card["moisture"].as<JsonObject>();
          int dry = card["dry"] | 350;
          int wet = card["wet"] | 800;
          if (!sourceMoisture.isNull())
          {
            dry = sourceMoisture["dry"] | dry;
            wet = sourceMoisture["wet"] | wet;
          }
          JsonObject moisture = item.createNestedObject("moisture");
          moisture["dry"] = dry;
          moisture["wet"] = wet;
        }
      }
    }
  }
#endif

  JsonObject network = doc.createNestedObject("network");
#if MSV2_ENABLE_WIFI
  network["mode"] = wifiStationConnected ? "station" : "access-point";
  network["ip"] = currentIpAddress().toString();
  network["rssi"] = wifiStationConnected ? WiFi.RSSI() : 0;
#else
  network["mode"] = "offline";
  network["ip"] = "0.0.0.0";
  network["rssi"] = 0;
#endif

  JsonArray plants = doc.createNestedArray("plants");
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    if (!sensors[sensorId].seen)
    {
      continue;
    }

    JsonObject item = plants.createNestedObject();
    item["sensor"] = sensorId;
    item["wake"] = sensors[sensorId].wake;
    item["moisture"] = sensors[sensorId].moisture;
    if (sensors[sensorId].batteryPercent != BATTERY_UNKNOWN)
    {
      item["batteryPercent"] = sensors[sensorId].batteryPercent;
    }
    item["rssi"] = sensors[sensorId].rssi;
    item["ageMs"] = millis() - sensors[sensorId].lastSeenMs;
    item["received"] = sensors[sensorId].received;
    item["missedTotal"] = sensors[sensorId].missedTotal;
  }

  String output;
  serializeJson(doc, output);
  return output;
}

#if MSV2_ENABLE_WIFI
void setCloudLastError(const char* error)
{
  strlcpy(cloudLastError, error == nullptr ? "" : error, sizeof(cloudLastError));
}

void setCloudBootstrapLastError(const char* error)
{
  strlcpy(cloudBootstrapLastError, error == nullptr ? "" : error, sizeof(cloudBootstrapLastError));
}

void runCloudBootstrap(uint32_t now)
{
  if (strlen(MSV2_CLOUD_BOOTSTRAP_URL) == 0)
  {
    nextCloudBootstrapMs = 0;
    return;
  }

  if (cloudUploadUrl[0] != '\0' && cloudApiToken[0] != '\0')
  {
    nextCloudBootstrapMs = 0;
    return;
  }

  if (nextCloudBootstrapMs == 0)
  {
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_FIRST_TRY_MS;
    return;
  }

  if ((int32_t)(now - nextCloudBootstrapMs) < 0)
  {
    return;
  }

  wifiStationConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiStationConnected)
  {
    setCloudBootstrapLastError("waiting for station WiFi");
    nextCloudBootstrapMs = now + 10000UL;
    return;
  }

  const bool isHttps = strncmp(MSV2_CLOUD_BOOTSTRAP_URL, "https://", 8) == 0;
  const bool isHttp = strncmp(MSV2_CLOUD_BOOTSTRAP_URL, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    cloudBootstrapLastAttemptMs = now;
    cloudBootstrapLastHttpStatus = 0;
    setCloudBootstrapLastError("bootstrap URL must start with http:// or https://");
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    return;
  }

  Serial.print("[MSV2 cloud] bootstrapping receiver at ");
  Serial.println(MSV2_CLOUD_BOOTSTRAP_URL);

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool began = false;
  if (isHttps)
  {
    secureClient.setInsecure();
    began = http.begin(secureClient, MSV2_CLOUD_BOOTSTRAP_URL);
  }
  else
  {
    began = http.begin(plainClient, MSV2_CLOUD_BOOTSTRAP_URL);
  }

  cloudBootstrapLastAttemptMs = now;
  cloudBootstrapLastHttpStatus = 0;
  if (!began)
  {
    setCloudBootstrapLastError("could not start HTTP client");
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    return;
  }

  StaticJsonDocument<512> requestDoc;
  requestDoc["receiverId"] = receiverId;
  requestDoc["receiverSecret"] = receiverSecret;
  requestDoc["claimCode"] = receiverClaimCode;
  requestDoc["build"] = MSV2_BUILD_TAG;
  String payload;
  serializeJson(requestDoc, payload);

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  int status = http.POST(payload);
  cloudBootstrapLastHttpStatus = status;

  if (status >= 200 && status < 300)
  {
    String response = http.getString();
    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    const char* uploadUrl = responseDoc["uploadUrl"] | "";
    const char* apiToken = responseDoc["apiToken"] | "";
    uint32_t syncIntervalSeconds = constrain((uint32_t)(responseDoc["syncIntervalSeconds"] | 300), 60UL, 86400UL);

    if (error || uploadUrl[0] == '\0' || apiToken[0] == '\0')
    {
      setCloudBootstrapLastError("invalid bootstrap response");
      nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    }
    else if (strlen(uploadUrl) >= sizeof(cloudUploadUrl) || strlen(apiToken) >= sizeof(cloudApiToken))
    {
      setCloudBootstrapLastError("bootstrap response too long");
      nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    }
    else
    {
      cloudEnabled = true;
      strlcpy(cloudUploadUrl, uploadUrl, sizeof(cloudUploadUrl));
      strlcpy(cloudApiToken, apiToken, sizeof(cloudApiToken));
      cloudSyncIntervalSeconds = syncIntervalSeconds;
      if (persistCloudConfig())
      {
        cloudBootstrapLastSuccessMs = millis();
        setCloudBootstrapLastError("");
        nextCloudBootstrapMs = 0;
        nextCloudSyncMs = millis() + 5000UL;
        Serial.println("[MSV2 cloud] bootstrap ok; cloud sync enabled");
      }
      else
      {
        setCloudBootstrapLastError("could not write cloud config");
        nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
      }
    }
  }
  else if (status > 0)
  {
    String response = http.getString();
    String error = "HTTP ";
    error += status;
    if (response.length() > 0)
    {
      StaticJsonDocument<384> errorDoc;
      DeserializationError parseError = deserializeJson(errorDoc, response);
      const char* serverError = errorDoc["error"] | "";
      error += ": ";
      if (!parseError && serverError[0] != '\0')
      {
        error += serverError;
      }
      else
      {
        error += response.substring(0, 64);
      }
    }
    setCloudBootstrapLastError(error.c_str());
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    Serial.print("[MSV2 cloud] bootstrap failed: ");
    Serial.println(error);
  }
  else
  {
    String error = http.errorToString(status);
    setCloudBootstrapLastError(error.c_str());
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    Serial.print("[MSV2 cloud] bootstrap failed: ");
    Serial.println(error);
  }

  http.end();
}

void runCloudSnapshotUploader(uint32_t now)
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0')
  {
    nextCloudSyncMs = 0;
    return;
  }

  if (nextCloudSyncMs == 0)
  {
    nextCloudSyncMs = now + 5000UL;
    return;
  }

  if ((int32_t)(now - nextCloudSyncMs) < 0)
  {
    return;
  }

  wifiStationConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiStationConnected)
  {
    setCloudLastError("waiting for station WiFi");
    nextCloudSyncMs = now + 10000UL;
    return;
  }

  const bool isHttps = strncmp(cloudUploadUrl, "https://", 8) == 0;
  const bool isHttp = strncmp(cloudUploadUrl, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    cloudLastAttemptMs = now;
    cloudLastHttpStatus = 0;
    setCloudLastError("uploadUrl must start with http:// or https://");
    nextCloudSyncMs = now + cloudSyncIntervalSeconds * 1000UL;
    return;
  }

  Serial.print("[MSV2 cloud] uploading snapshot to ");
  Serial.println(cloudUploadUrl);

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool began = false;
  if (isHttps)
  {
    secureClient.setInsecure();
    began = http.begin(secureClient, cloudUploadUrl);
  }
  else
  {
    began = http.begin(plainClient, cloudUploadUrl);
  }

  cloudLastAttemptMs = now;
  cloudLastHttpStatus = 0;
  if (!began)
  {
    setCloudLastError("could not start HTTP client");
    nextCloudSyncMs = now + cloudSyncIntervalSeconds * 1000UL;
    return;
  }

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  if (cloudApiToken[0] != '\0')
  {
    String auth = "Bearer ";
    auth += cloudApiToken;
    http.addHeader("Authorization", auth);
  }

  String payload = snapshotJson();
  int status = http.POST(payload);
  cloudLastHttpStatus = status;
  if (status >= 200 && status < 300)
  {
    cloudLastSuccessMs = millis();
    setCloudLastError("");
    Serial.print("[MSV2 cloud] upload ok: HTTP ");
    Serial.println(status);
  }
  else if (status > 0)
  {
    String error = "HTTP ";
    error += status;
    setCloudLastError(error.c_str());
    Serial.print("[MSV2 cloud] upload failed: HTTP ");
    Serial.println(status);
  }
  else
  {
    String error = http.errorToString(status);
    setCloudLastError(error.c_str());
    Serial.print("[MSV2 cloud] upload failed: ");
    Serial.println(error);
  }

  http.end();
  nextCloudSyncMs = millis() + cloudSyncIntervalSeconds * 1000UL;
}
#endif

String configMetaJson()
{
  StaticJsonDocument<256> doc;
  doc["schema"] = 1;
  doc["version"] = uiConfigVersion;
  doc["updatedMs"] = uiUpdatedMs;
  doc["updatedBy"] = uiUpdatedBy;
  doc["path"] = UI_PATH;

  String output;
  serializeJson(doc, output);
  return output;
}

String deviceInfoJson()
{
  StaticJsonDocument<1536> doc;
  doc["name"] = "MSV2 Display";
  doc["build"] = MSV2_BUILD_TAG;
  doc["receiverId"] = receiverId;
  doc["claimCode"] = receiverClaimCode;
  doc["maxSensorId"] = MAX_SENSOR_ID;
  JsonObject config = doc.createNestedObject("config");
  config["schema"] = 1;
  config["version"] = uiConfigVersion;
  config["updatedMs"] = uiUpdatedMs;
  config["updatedBy"] = uiUpdatedBy;
  JsonObject cloud = doc.createNestedObject("cloud");
  cloud["enabled"] = cloudEnabled;
  cloud["configured"] = cloudUploadUrl[0] != '\0';
  cloud["syncIntervalSeconds"] = cloudSyncIntervalSeconds;

#if MSV2_ENABLE_WIFI
  IPAddress ip = wifiStationConnected ? WiFi.localIP() : WiFi.softAPIP();
  doc["networkMode"] = wifiStationConnected ? "station" : "access-point";
  doc["ip"] = ip.toString();
  doc["setupSsid"] = AP_SSID;
  doc["configuredSsid"] = configuredWifiSsid;
  doc["hasStoredWifiCredentials"] = storedWifiLoaded;
#else
  doc["networkMode"] = "offline";
  doc["ip"] = "0.0.0.0";
#endif

  JsonObject endpoints = doc.createNestedObject("endpoints");
  endpoints["app"] = "/app";
  endpoints["ui"] = "/ui";
  endpoints["data"] = "/data";
  endpoints["snapshot"] = "/snapshot";
  endpoints["device"] = "/device";
  endpoints["claim"] = "/claim";
  endpoints["configMeta"] = "/config-meta";
  endpoints["cloud"] = "/cloud";
  endpoints["wifi"] = "/wifi";
  endpoints["manifest"] = "/manifest.webmanifest";

  String output;
  serializeJson(doc, output);
  return output;
}

#if MSV2_ENABLE_BLE
class Msv2AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    if (!advertisedDevice.haveManufacturerData())
    {
      return;
    }

    auto data = advertisedDevice.getManufacturerData();
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(data.c_str());
    size_t len = data.length();

    if (len < 12)
    {
      return;
    }

    uint16_t companyId = buffer[0] | (buffer[1] << 8);
    if (companyId != MSV2_COMPANY_ID)
    {
      return;
    }

    if (memcmp(&buffer[2], MSV2_MAGIC, sizeof(MSV2_MAGIC)) != 0)
    {
      return;
    }

    uint8_t sensorId = buffer[6];
    if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
    {
      return;
    }

    uint32_t wake = ((uint32_t)buffer[7]) |
                    ((uint32_t)buffer[8] << 8) |
                    ((uint32_t)buffer[9] << 16);
    uint16_t moisture = buffer[10] | (buffer[11] << 8);
    uint8_t batteryPercent = len >= 13 ? buffer[12] : BATTERY_UNKNOWN;
    SensorReading& reading = sensors[sensorId];

    if (reading.seen && wake == reading.wake)
    {
      return;
    }

    reading.missed = reading.seen ? ((wake - reading.wake - 1) & 0xFFFFFF) : 0;
    reading.missedTotal += reading.missed;
    reading.received++;
    reading.seen = true;
    reading.wake = wake;
    reading.moisture = moisture;
    reading.batteryPercent = batteryPercent;
    reading.rssi = advertisedDevice.getRSSI();
    reading.lastSeenMs = millis();
    sensorDataDirty = true;
  }
};

void bleScanTask(void* parameter)
{
  (void)parameter;
  for (;;)
  {
    if (bleScan != nullptr)
    {
      bleScan->start(BLE_SCAN_SECONDS, false);
      bleScan->clearResults();
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_IDLE_MS));
  }
}
#endif

void setupServer()
{
  server.on("/", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", MSV2_MOBILE_APP_HTML);
  });

  server.on("/app", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /app");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", MSV2_MOBILE_APP_HTML);
  });

  server.on("/manifest.webmanifest", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /manifest.webmanifest");
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "application/manifest+json", MSV2_WEB_MANIFEST);
  });

  server.on("/sw.js", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /sw.js");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/javascript", MSV2_SERVICE_WORKER_JS);
  });

  server.on("/app-icon.svg", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /app-icon.svg");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "image/svg+xml", MSV2_APP_ICON_SVG);
  });

  server.on("/ping", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /ping");
    server.send(200, "text/plain", "pong\n");
  });

  server.on("/ui", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /ui");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", readUiJson());
  });

  server.on("/ui", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /ui");
    if (writeUiJson(server.arg("plain")))
    {
      server.send(200, "application/json", "{\"ok\":true}");
    }
  });

  server.on("/data", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /data");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", sensorDataJson());
  });

  server.on("/snapshot", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /snapshot");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", snapshotJson());
  });

  server.on("/device", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /device");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", deviceInfoJson());
  });

  server.on("/config-meta", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /config-meta");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", configMetaJson());
  });

  server.on("/claim", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /claim");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", claimInfoJson());
  });

  server.on("/cloud", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /cloud");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", cloudConfigJson());
  });

  server.on("/cloud", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /cloud");
    writeCloudConfigJson(server.arg("plain"));
  });

  server.on("/cloud", HTTP_DELETE, []() {
    Serial.println("[MSV2 web] DELETE /cloud");
    resetCloudConfig();
  });

  server.on("/wifi", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /wifi");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", wifiStatusJson());
  });

  server.on("/wifi", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /wifi");
    writeWifiConfigJson(server.arg("plain"));
  });

  server.on("/wifi", HTTP_DELETE, []() {
    Serial.println("[MSV2 web] DELETE /wifi");
    forgetWifiConfig();
  });

  server.onNotFound([]() {
    Serial.print("[MSV2 web] 404 ");
    Serial.println(server.uri());
    server.send(404, "text/plain", "not found\n");
  });

  server.begin();
}

IPAddress currentIpAddress()
{
#if MSV2_ENABLE_WIFI
  return wifiStationConnected ? WiFi.localIP() : WiFi.softAPIP();
#else
  return IPAddress(0, 0, 0, 0);
#endif
}

void setupWifi()
{
#if MSV2_ENABLE_WIFI
  wifiStationConnected = false;
  loadStoredWifiConfig();

  const char* stationSsid = storedWifiLoaded ? configuredWifiSsid : WIFI_SSID;
  const char* stationPassword = storedWifiLoaded ? configuredWifiPassword : WIFI_PASSWORD;

  if (strlen(stationSsid) > 0)
  {
    bootLog("connecting WiFi STA");
    Serial.print("[MSV2 wifi] SSID: ");
    Serial.println(stationSsid);
    Serial.print("[MSV2 wifi] credential source: ");
    Serial.println(storedWifiLoaded ? "LittleFS" : "firmware");

    WiFi.mode(WIFI_STA);
    WiFi.begin(stationSsid, stationPassword);

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000)
    {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
      wifiStationConnected = true;
      Serial.print("[MSV2 wifi] connected: ");
      Serial.println(WiFi.localIP());
      return;
    }

    Serial.println("[MSV2 wifi] station connect failed; falling back to AP");
    WiFi.disconnect(true);
    delay(250);
  }

  bootLog("starting WiFi AP");
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD))
  {
    bootLog("WiFi AP start failed");
  }
  else
  {
    Serial.print("[MSV2 wifi] AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[MSV2 wifi] AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
#endif
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println(MSV2_BUILD_TAG);
  Serial.print("[MSV2 boot] previous checkpoint before reset: ");
  Serial.println(lastCheckpoint);
  Serial.print("[MSV2 boot] reset reason: ");
  Serial.println(resetReasonName(esp_reset_reason()));
  randomSeed(esp_random());
  bootLog("serial ready");

  bootLog("mounting LittleFS");
  if (!LittleFS.begin(true))
  {
    bootLog("LittleFS mount failed");
  }
  ensureDefaultUi();
  ensureUiMetadata();
  loadDeviceIdentity();
  loadCloudConfig();
  bootLog("LittleFS ready");

#if MSV2_ENABLE_DISPLAY
  bootLog("initializing display");
  busPtr = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1);
  if (busPtr == nullptr)
  {
    bootLog("display bus allocation failed");
    return;
  }
  gfxPtr = new Arduino_ST7796(busPtr, TFT_RST, 1, false, 320, 480);
  if (gfxPtr == nullptr)
  {
    bootLog("display allocation failed");
    return;
  }
  if (!tft.begin())
  {
    bootLog("display begin failed");
    return;
  }
  applyDisplayRotation(1, true);
  Serial.print("[MSV2 display] rotation=");
  Serial.print(currentRotation);
  Serial.print(" size=");
  Serial.print(tft.width());
  Serial.print("x");
  Serial.println(tft.height());
  tft.fillScreen(TFT_BLACK);
  drawTextLeft("MSV2 booting", 8, 8, 1, TFT_WHITE, TFT_BLACK);
  bootLog("display ready");
#else
  bootLog("display disabled");
#endif

#if MSV2_ENABLE_WIFI
  setupWifi();
  serverPtr = new WebServer(80);
  if (serverPtr == nullptr)
  {
    bootLog("web server allocation failed");
    return;
  }
  setupServer();
  bootLog("WiFi/server ready");
#else
  bootLog("WiFi disabled");
#endif

#if MSV2_ENABLE_BLE
  bootLog("initializing BLE scanner");
  BLEDevice::init("MSV2-DISPLAY");
  bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new Msv2AdvertisedDeviceCallbacks(), true);
  bleScan->setActiveScan(false);
  bleScan->setInterval(45);
  bleScan->setWindow(45);
  if (xTaskCreatePinnedToCore(bleScanTask, "msv2_ble_scan", 8192, nullptr, 1, &bleScanTaskHandle, 0) == pdPASS)
  {
    bootLog("BLE scanner task running");
  }
  else
  {
    bootLog("BLE scanner task failed");
  }
#else
  bootLog("BLE disabled");
#endif

#if MSV2_ENABLE_WIFI
  Serial.print("MSV2 display runtime ready at http://");
  Serial.println(currentIpAddress());
#else
  Serial.println("MSV2 display runtime ready");
#endif
  bootLog("initial render");
  displayDirty = false;
  renderUi();
  bootLog("setup complete");
}

void loop()
{
  uint32_t now = millis();

#if MSV2_ENABLE_WIFI
  server.handleClient();
  if (pendingRestartMs != 0 && (int32_t)(now - pendingRestartMs) >= 0)
  {
    Serial.println("[MSV2 wifi] restarting after WiFi config change");
    Serial.flush();
    delay(50);
    ESP.restart();
  }
  runCloudBootstrap(now);
  runCloudSnapshotUploader(now);
#endif

#if MSV2_ENABLE_DISPLAY
  if (sensorDataDirty && now - lastSensorRenderMs >= SENSOR_RENDER_INTERVAL_MS)
  {
    sensorDataDirty = false;
    lastSensorRenderMs = now;
    if (artSlotCount > 0)
    {
      renderDashboardDataOnly();
    }
    else
    {
      displayDirty = true;
    }
  }

  if (now - lastArtFrameMs >= ART_RENDER_POLL_MS)
  {
    lastArtFrameMs = now;
    if (!displayDirty)
    {
      renderAnimatedPlantArt();
    }
  }

  if (displayDirty)
  {
    displayDirty = false;
    setCheckpoint("loop: render start");
    renderUi();
    setCheckpoint("loop: render done");
  }
#endif

  delay(1);
}

#endif
