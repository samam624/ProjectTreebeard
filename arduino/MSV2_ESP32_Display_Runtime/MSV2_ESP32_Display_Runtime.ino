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
    password: none
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
#define MSV2_BUILD_TAG "MSV2 runtime build 2026-07-05 reboot-screen-fix"

#ifndef MSV2_FORCE_WIFI_SETUP
#define MSV2_FORCE_WIFI_SETUP 0
#endif

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
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#if __has_include("WiFiCredentials.h")
#include "WiFiCredentials.h"
#endif

// Trusted root CAs used instead of secureClient.setInsecure() so outbound cloud calls actually
// validate the server certificate, rather than accepting anything. Pinning the ROOT (not the
// leaf/intermediate Supabase presents) means this survives routine cert rotation -- roots change
// on a timescale of years/decades, not weeks. Two roots are included because Supabase's edge
// currently presents a GTS Root R4 cross-signed by the long-lived GlobalSign Root CA (for older
// trust stores) rather than the modern self-signed GTS Root R4; either one alone completes a
// chain that the other configuration would need.
static const char MSV2_TRUSTED_ROOT_CA_PEM[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG
A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv
b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw
MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i
YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT
aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ
jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp
xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp
1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG
snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ
U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8
9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E
BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B
AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz
yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE
38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP
AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad
DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME
HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==
-----END CERTIFICATE-----
)PEM";
#endif

const char AP_SSID[] = "MSV2-Display";
const char AP_PASSWORD[] = "";
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

#ifndef MSV2_HOSTED_APP_URL
#define MSV2_HOSTED_APP_URL "https://msv2-plants.netlify.app/"
#endif

const char UI_PATH[] = "/ui.json";
const char WIFI_CONFIG_PATH[] = "/wifi.json";
const char FORCE_WIFI_SETUP_UNLOCK_PATH[] = "/wifi-setup-unlock-v2.flag";
const char WIFI_RETRY_STATE_PATH[] = "/wifi-retry.json";
const char DEVICE_CONFIG_PATH[] = "/device.json";
const char CLOUD_CONFIG_PATH[] = "/cloud.json";
const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint8_t MSV2_MAGIC[] = { 'M', 'S', 'V', '2' };
const uint8_t MAX_SENSOR_ID = 16;
// 2026-07-28: probes were reworked to send raw, uncalibrated readings (raw oscillator kHz,
// raw battery mV, raw charging flag) instead of interpreting them on-device - see
// MSV2_Moisture_BLE_Sleep.ino and calibrateRawMoisture()/calibrateRawBattery() below. That
// widened the packet from 13 to 15 bytes, which doubles as a format-version check: probes
// still running pre-rework firmware send the shorter legacy packet with already-calibrated
// values, handled as a fallback in Msv2AdvertisedDeviceCallbacks::onResult().
const size_t MSV2_RAW_PACKET_MIN_LEN = 15;
const uint32_t BLE_SCAN_SECONDS = 3;
const uint32_t BLE_SCAN_IDLE_MS = 250;
const uint32_t DEFAULT_STALE_SECONDS = 90;
const uint8_t BATTERY_UNKNOWN = 255;
const uint8_t MAX_ART_SLOTS = 16;
const uint32_t SENSOR_RENDER_INTERVAL_MS = 2000;
const uint32_t CLOUD_BOOTSTRAP_FIRST_TRY_MS = 15000;
const uint32_t CLOUD_BOOTSTRAP_RETRY_MS = 300000;
const uint32_t CLOUD_SYNC_DEFAULT_SECONDS = 60;
const uint32_t CLOUD_SYNC_SOON_MS = 5000;
const uint32_t CLOUD_SYNC_SOON_THROTTLE_MS = 15000;
const uint32_t CLOUD_CONFIG_PULL_INTERVAL_MS = 30000;
const uint32_t CLOUD_AUTOCAL_PULL_INTERVAL_MS = 600000; // 10 min - learned range/schedule changes slowly
const uint8_t WIFI_CONNECT_REBOOT_RETRIES = 2;

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

// Cached result of the periodic /autocal cloud pull (see runCloudAutoCalPull) - a
// self-learned moisture range per sensor, replacing manually-guessed dry/wet
// thresholds. "ready" gates whether there's enough history (min sample count + min
// elapsed days, enforced server-side in receiver-snapshot's autocal=1 branch) to trust
// the learned bounds yet; until then, callers should fall back to the manual/default
// thresholds instead of using dryBound/wetBound.
struct AutoCalState
{
  bool ready = false;
  int dryBound = 0;
  int wetBound = 0;
  bool overdue = false;
  float daysSinceLastWatering = -1.0f;
  float predictedIntervalDays = -1.0f;
};

AutoCalState autoCal[MAX_SENSOR_ID + 1];

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
DNSServer* dnsServerPtr = nullptr;
#endif
#if MSV2_ENABLE_BLE
BLEScan* bleScan = nullptr;
TaskHandle_t bleScanTaskHandle = nullptr;
#endif
SensorReading sensors[MAX_SENSOR_ID + 1];

struct PlantArtSlot
{
  bool enabled = false;
  bool artEnabled = true;
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
  int dry = 120;
  int wet = 175;
  bool overdue = false; // forces dangerColor when auto mode thinks watering is overdue
  bool pending = false; // auto mode on but not enough history yet - render like stale
  uint8_t frame = 0;
  uint16_t frameIntervalMs = 200;
  uint32_t nextFrameMs = 0;
};

struct DashboardGrid
{
  int columns = 4;
  int rows = 2;
};

PlantArtSlot artSlots[MAX_ART_SLOTS];
uint8_t artSlotCount = 0;

#if MSV2_ENABLE_DISPLAY
#define tft (*gfxPtr)
#endif
#if MSV2_ENABLE_WIFI
#define server (*serverPtr)
#define dnsServer (*dnsServerPtr)
TaskHandle_t cloudSyncTaskHandle = nullptr;
#endif

bool displayDirty = true;
volatile bool sensorDataDirty = false;
bool wifiSetupScreenDrawn = false;
uint32_t lastSensorRenderMs = 0;
uint32_t staleSeconds = DEFAULT_STALE_SECONDS;
RTC_DATA_ATTR char lastCheckpoint[48] = "fresh boot";
bool wifiStationConnected = false;
bool wifiLocalOnly = false;
bool wifiSetupPortalActive = false;
bool storedWifiLoaded = false;
char configuredWifiSsid[64] = "";
char configuredWifiPassword[96] = "";
char receiverId[20] = "";
char receiverSecret[33] = "";
char receiverClaimCode[10] = "";
uint32_t uiConfigVersion = 0;
uint32_t uiUpdatedMs = 0;
char uiUpdatedBy[24] = "unknown";

// Local admin API (POST/DELETE on /ui, /cloud, /wifi) is only trusted for a grace window after
// boot. Anyone on the LAN can otherwise reach these handlers indefinitely -- including in
// local-only mode, where the device never leaves its own open AP. Power-cycling the device
// reopens the window, matching how other consumer IoT devices gate local admin access to
// "physically present shortly after a (re)boot" rather than "on my network, ever."
const unsigned long ADMIN_GRACE_WINDOW_MS = 15UL * 60UL * 1000UL;
unsigned long adminUnlockUntilMs = 0;

// Sliding expiry: an already-valid window gets extended by each successful admin
// action, so an in-progress editing session (e.g. tuning several plant cards in one
// sitting) doesn't get disruptively cut off mid-session. This doesn't weaken the
// security property above - an already-expired window can't extend itself, so someone
// who missed the post-boot period still can never gain admin access without a reboot.
bool isAdminActionAllowed()
{
  if (millis() >= adminUnlockUntilMs)
  {
    return false;
  }
  adminUnlockUntilMs = millis() + ADMIN_GRACE_WINDOW_MS;
  return true;
}
bool cloudEnabled = false;
char cloudUploadUrl[160] = "";
char cloudApiToken[80] = "";
uint32_t cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
uint32_t nextCloudSyncMs = 0;
uint32_t lastCloudSyncSoonMs = 0;
uint32_t cloudLastAttemptMs = 0;
uint32_t cloudLastSuccessMs = 0;
int cloudLastHttpStatus = 0;
char cloudLastError[96] = "";
uint32_t nextCloudConfigPullMs = 0;
uint32_t cloudConfigLastAttemptMs = 0;
uint32_t cloudConfigLastSuccessMs = 0;
int cloudConfigLastHttpStatus = 0;
char cloudConfigLastError[96] = "";
uint32_t nextCloudAutoCalPullMs = 0; // auto-calibration changes slowly - polled far less often than config
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
const char* currentNetworkMode();
#endif
#if MSV2_ENABLE_DISPLAY
void renderWifiConnectingScreen(const char* ssid, const char* source);
void renderWifiSetupScreen(const char* reason);
void renderWifiSavedScreen();
void renderWifiRetryScreen(const char* ssid, uint8_t attempt, uint8_t maxAttempts);
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
    "{\"sensor\":1,\"plant\":\"Basil\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":2,\"plant\":\"Mint\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":3,\"plant\":\"Thyme\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":4,\"plant\":\"Sage\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":5,\"plant\":\"Rosemary\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":6,\"plant\":\"Oregano\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":7,\"plant\":\"Parsley\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175}},"
    "{\"sensor\":8,\"plant\":\"Cilantro\",\"art\":\"cactus_cluster\",\"moisture\":{\"dry\":120,\"wet\":175}}"
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

void scheduleCloudSyncSoon(bool force = false)
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0')
  {
    return;
  }

  const uint32_t now = millis();
  if (!force && lastCloudSyncSoonMs != 0 && (uint32_t)(now - lastCloudSyncSoonMs) < CLOUD_SYNC_SOON_THROTTLE_MS)
  {
    return;
  }

  lastCloudSyncSoonMs = now;
  const uint32_t targetMs = now + CLOUD_SYNC_SOON_MS;
  if (nextCloudSyncMs == 0 || (int32_t)(nextCloudSyncMs - targetMs) > 0)
  {
    nextCloudSyncMs = targetMs;
  }
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
  scheduleCloudSyncSoon(true);
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
  StaticJsonDocument<768> doc;
  doc["receiverId"] = receiverId;
  doc["claimCode"] = receiverClaimCode;
  doc["name"] = "MSV2 Display";
  doc["build"] = MSV2_BUILD_TAG;
#if MSV2_ENABLE_WIFI
  doc["networkMode"] = currentNetworkMode();
  doc["ip"] = currentIpAddress().toString();
  doc["localOnly"] = wifiLocalOnly;
#else
  doc["networkMode"] = "offline";
  doc["ip"] = "0.0.0.0";
#endif
  doc["claimPath"] = "/claim";
  doc["appUrl"] = MSV2_HOSTED_APP_URL;
  String claimUrl = MSV2_HOSTED_APP_URL;
  claimUrl += "?rid=";
  claimUrl += receiverId;
  claimUrl += "&code=";
  claimUrl += receiverClaimCode;
  claimUrl += "&connect=1";
  doc["claimUrl"] = claimUrl;

  String output;
  serializeJson(doc, output);
  return output;
}

bool loadStoredWifiConfig()
{
  storedWifiLoaded = false;
  wifiLocalOnly = false;
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
  wifiLocalOnly = doc["localOnly"] | false;
  if (ssid == nullptr || ssid[0] == '\0')
  {
    return wifiLocalOnly;
  }

  strlcpy(configuredWifiSsid, ssid, sizeof(configuredWifiSsid));
  strlcpy(configuredWifiPassword, password == nullptr ? "" : password, sizeof(configuredWifiPassword));
  storedWifiLoaded = true;
  return true;
}

void clearWifiRetryState()
{
  if (LittleFS.exists(WIFI_RETRY_STATE_PATH))
  {
    LittleFS.remove(WIFI_RETRY_STATE_PATH);
  }
}

uint8_t loadWifiRetryCount()
{
  if (!LittleFS.exists(WIFI_RETRY_STATE_PATH))
  {
    return 0;
  }

  File file = LittleFS.open(WIFI_RETRY_STATE_PATH, "r");
  if (!file)
  {
    return 0;
  }

  StaticJsonDocument<96> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error)
  {
    return 0;
  }
  return (uint8_t)constrain((int)(doc["count"] | 0), 0, 10);
}

void saveWifiRetryCount(uint8_t count)
{
  StaticJsonDocument<96> doc;
  doc["count"] = count;
  doc["updatedMs"] = millis();

  File file = LittleFS.open(WIFI_RETRY_STATE_PATH, "w");
  if (!file)
  {
    return;
  }

  serializeJson(doc, file);
  file.close();
}

bool loadCloudConfig()
{
  cloudEnabled = false;
  cloudUploadUrl[0] = '\0';
  cloudApiToken[0] = '\0';
  cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
  nextCloudSyncMs = 0;
  cloudLastAttemptMs = 0;
  cloudLastSuccessMs = 0;
  cloudLastHttpStatus = 0;
  cloudLastError[0] = '\0';
  nextCloudConfigPullMs = 0;
  cloudConfigLastAttemptMs = 0;
  cloudConfigLastSuccessMs = 0;
  cloudConfigLastHttpStatus = 0;
  cloudConfigLastError[0] = '\0';
  nextCloudAutoCalPullMs = 0;
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
  cloudSyncIntervalSeconds = constrain((uint32_t)(doc["syncIntervalSeconds"] | CLOUD_SYNC_DEFAULT_SECONDS), 60UL, 86400UL);
  nextCloudSyncMs = cloudEnabled ? millis() + 5000UL : 0;
  nextCloudConfigPullMs = cloudEnabled ? millis() + 10000UL : 0;
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
  JsonObject configPull = doc.createNestedObject("configPull");
  configPull["lastAttemptMs"] = cloudConfigLastAttemptMs;
  configPull["lastSuccessMs"] = cloudConfigLastSuccessMs;
  configPull["lastHttpStatus"] = cloudConfigLastHttpStatus;
  configPull["lastError"] = cloudConfigLastError;
  configPull["nextAttemptMs"] = cloudEnabled ? nextCloudConfigPullMs : 0;
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
  nextCloudConfigPullMs = cloudEnabled ? millis() + 10000UL : 0;
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
  cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
  nextCloudSyncMs = 0;
  cloudLastAttemptMs = 0;
  cloudLastSuccessMs = 0;
  cloudLastHttpStatus = 0;
  cloudLastError[0] = '\0';
  nextCloudConfigPullMs = 0;
  cloudConfigLastAttemptMs = 0;
  cloudConfigLastSuccessMs = 0;
  cloudConfigLastHttpStatus = 0;
  cloudConfigLastError[0] = '\0';
  nextCloudAutoCalPullMs = 0;
  nextCloudBootstrapMs = millis() + CLOUD_BOOTSTRAP_FIRST_TRY_MS;
  server.send(200, "application/json", "{\"ok\":true}");
  return true;
}

String wifiStatusJson()
{
  StaticJsonDocument<512> doc;
  doc["networkMode"] = currentNetworkMode();
  doc["ip"] = currentIpAddress().toString();
  doc["setupSsid"] = AP_SSID;
  doc["setupPassword"] = AP_PASSWORD;
  doc["setupUrl"] = "http://192.168.4.1/app";
  doc["hasStoredCredentials"] = storedWifiLoaded;
  doc["configuredSsid"] = configuredWifiSsid;
  doc["localOnly"] = wifiLocalOnly;
  doc["setupPortalActive"] = wifiSetupPortalActive;
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
  bool localOnly = doc["localOnly"] | false;
  if (!localOnly && (ssid == nullptr || ssid[0] == '\0'))
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return false;
  }
  if (ssid != nullptr && strlen(ssid) >= sizeof(configuredWifiSsid))
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
  stored["localOnly"] = localOnly;
  const bool hasSsid = ssid != nullptr && ssid[0] != '\0';
  const bool preserveExistingPassword = hasSsid && storedWifiLoaded && strcmp(ssid, configuredWifiSsid) == 0 && (password == nullptr || password[0] == '\0');
  char passwordToSave[sizeof(configuredWifiPassword)];
  strlcpy(passwordToSave, preserveExistingPassword ? configuredWifiPassword : (password == nullptr ? "" : password), sizeof(passwordToSave));
  stored["ssid"] = hasSsid ? ssid : "";
  stored["password"] = passwordToSave;

  File file = LittleFS.open(WIFI_CONFIG_PATH, "w");
  if (!file)
  {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not write wifi config\"}");
    return false;
  }
  serializeJson(stored, file);
  file.close();

  wifiLocalOnly = localOnly;
  strlcpy(configuredWifiSsid, hasSsid ? ssid : "", sizeof(configuredWifiSsid));
  strlcpy(configuredWifiPassword, passwordToSave, sizeof(configuredWifiPassword));
  storedWifiLoaded = hasSsid;
  clearWifiRetryState();
  if (MSV2_FORCE_WIFI_SETUP != 0)
  {
    File unlockFile = LittleFS.open(FORCE_WIFI_SETUP_UNLOCK_PATH, "w");
    if (unlockFile)
    {
      unlockFile.print("1");
      unlockFile.close();
    }
  }
  pendingRestartMs = millis() + 900;
  renderWifiSavedScreen();
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  return true;
}

bool forgetWifiConfig()
{
  if (LittleFS.exists(WIFI_CONFIG_PATH))
  {
    LittleFS.remove(WIFI_CONFIG_PATH);
  }
  wifiLocalOnly = false;
  wifiSetupPortalActive = true;
  storedWifiLoaded = false;
  configuredWifiSsid[0] = '\0';
  configuredWifiPassword[0] = '\0';
  if (LittleFS.exists(FORCE_WIFI_SETUP_UNLOCK_PATH))
  {
    LittleFS.remove(FORCE_WIFI_SETUP_UNLOCK_PATH);
  }
  clearWifiRetryState();
  pendingRestartMs = millis() + 900;
  renderWifiSavedScreen();
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

DashboardGrid gridForCardCount(size_t count)
{
  size_t total = count == 0 ? 1 : count;
  if (total == 1)
  {
    return {1, 1};
  }
  if (total == 2)
  {
    return {2, 1};
  }
  if (total == 3)
  {
    return {3, 1};
  }
  if (total == 4)
  {
    return {2, 2};
  }
  if (total <= 6)
  {
    return {3, 2};
  }
  if (total <= 8)
  {
    return {4, 2};
  }
  if (total == 9)
  {
    return {3, 3};
  }
  if (total <= 12)
  {
    return {4, 3};
  }
  return {4, 4};
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

// Resolves the dry/wet bounds and overdue flag to actually use for a card: the
// self-learned auto-calibration range (when the card has "auto" enabled and enough
// history has accumulated server-side), otherwise the manual/default thresholds. Used
// by every moisture-rendering call site (both PlantArtSlot-based and direct) so the web
// app and physical display never disagree.
void resolveMoistureBounds(
  JsonObject moistureConfig,
  JsonObject card,
  uint8_t sensorId,
  int defaultDry,
  int defaultWet,
  int& outDry,
  int& outWet,
  bool& outOverdue,
  bool& outPending
)
{
  bool autoEnabled = moistureConfig["auto"] | false;
  bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;

  if (autoEnabled && validSensor && autoCal[sensorId].ready)
  {
    outDry = autoCal[sensorId].dryBound;
    outWet = autoCal[sensorId].wetBound;
    outOverdue = autoCal[sensorId].overdue;
    outPending = false;
    return;
  }

  // Auto mode either off, or on but not enough history yet (the "ready" gate in
  // msv2_read_sensor_auto_calibration / receiver-snapshot's autocal=1 branch). Either
  // way, use the manually-configured (or preset-default) range as the working estimate
  // - unlike the stale-scale bug this replaced, this range is on the CURRENT sensing
  // scale (the user set it, or picked a preset, after today's oscillator/inversion
  // changes), so it's a reasonable number to show rather than nothing. "pending" just
  // flags "not auto-calibrated yet" for callers that want to note it - it doesn't
  // block or invalidate the percentage anymore.
  outDry = moistureConfig["dry"] | card["dry"] | defaultDry;
  outWet = moistureConfig["wet"] | card["wet"] | defaultWet;
  outOverdue = false;
  outPending = autoEnabled;
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

// Turns a resolved "danger" color into a blink (alternating dangerColor/mutedColor
// every 500ms) so a critically-low or overdue plant is an active alert on the physical
// display, not just a static red border easy to miss at a glance - matches the probe's
// own low-moisture LED alert. Leaves any other color (green/yellow/muted) untouched.
uint16_t applyLowMoistureBlink(uint16_t resolvedColor, uint16_t dangerColor, uint16_t mutedColor)
{
  if (resolvedColor != dangerColor)
  {
    return resolvedColor;
  }
  bool blinkOn = (millis() / 500) % 2 == 0;
  return blinkOn ? dangerColor : mutedColor;
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
  uint8_t scaleMul = 1;
  uint8_t scaleDiv = 1;
  int drawW = 0;
  int drawH = 0;
  int startX = 0;
  int startY = 0;
  spriteDrawGeometry(sprite, centerX, topY, maxW, maxH, scaleMul, scaleDiv, drawW, drawH, startX, startY);
  const uint8_t pose = pgm_read_byte(&sprite.sequence[frame % sprite.frameCount]);
  const uint16_t* bitmap = (const uint16_t*)pgm_read_ptr(&sprite.frames[pose]);

  tft.startWrite();
  for (int yy = 0; yy < drawH; yy++)
  {
    for (int xx = 0; xx < drawW; xx++)
    {
      const uint16_t c = spriteScaledPixel(sprite, bitmap, scaleMul, scaleDiv, xx, yy);
      if (c != PLANT_TRANSPARENT)
      {
        tft.writePixel(startX + xx, startY + yy, c);
      }
    }
  }
  tft.endWrite();
}

void spriteDrawGeometry(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t& scaleMul, uint8_t& scaleDiv, int& drawW, int& drawH, int& startX, int& startY)
{
  scaleMul = 1;
  scaleDiv = 1;
  while (scaleMul < 3 && (sprite.w * (scaleMul + 1)) <= maxW && (sprite.h * (scaleMul + 1)) <= maxH)
  {
    scaleMul++;
  }
  while (scaleMul == 1 && scaleDiv < 5 && ((sprite.w / scaleDiv) > maxW || (sprite.h / scaleDiv) > maxH))
  {
    scaleDiv++;
  }

  drawW = (sprite.w * scaleMul) / scaleDiv;
  drawH = (sprite.h * scaleMul) / scaleDiv;
  startX = centerX - drawW / 2;
  startY = topY + max(0, (maxH - drawH) / 2);
}

uint16_t spriteScaledPixel(const PlantSprite& sprite, const uint16_t* bitmap, uint8_t scaleMul, uint8_t scaleDiv, int x, int y)
{
  const int sourceX = (x * scaleDiv) / scaleMul;
  const int sourceY = (y * scaleDiv) / scaleMul;
  return pgm_read_word(&bitmap[sourceY * sprite.w + sourceX]);
}

void drawPlantSpriteDiffScaled(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t oldFrame, uint8_t newFrame, uint16_t backgroundColor)
{
  uint8_t scaleMul = 1;
  uint8_t scaleDiv = 1;
  int drawW = 0;
  int drawH = 0;
  int startX = 0;
  int startY = 0;
  spriteDrawGeometry(sprite, centerX, topY, maxW, maxH, scaleMul, scaleDiv, drawW, drawH, startX, startY);

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
      const uint16_t oldColor = spriteScaledPixel(sprite, oldBitmap, scaleMul, scaleDiv, xx, yy);
      const uint16_t newColor = spriteScaledPixel(sprite, newBitmap, scaleMul, scaleDiv, xx, yy);
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
  const bool compact = h < 76;
  const int inset = compact ? 5 : 7;
  const int artX = x + inset;
  const int artY = y + (compact ? 15 : 24);
  const int artW = w - inset * 2;
  const int artH = compact ? max(10, h - 29) : max(24, h - 91);

  tft.fillRect(artX, artY, artW, artH, surfaceColor);
  drawPlantArt(x + w / 2, artY, artW, artH, art, color, surfaceColor, frame);
}

void drawPlantArtDiffInCard(int x, int y, int w, int h, const char* art, uint16_t color, uint16_t surfaceColor, uint8_t oldFrame, uint8_t newFrame)
{
  const bool compact = h < 76;
  const int inset = compact ? 5 : 7;
  const int artX = x + inset;
  const int artY = y + (compact ? 15 : 24);
  const int artW = w - inset * 2;
  const int artH = compact ? max(10, h - 29) : max(24, h - 91);
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
  bool artEnabled,
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
  slot.artEnabled = artEnabled;
  slot.sensorId = card["sensor"] | 0;
  strlcpy(slot.art, card["art"] | "sprout_pot", sizeof(slot.art));
  JsonObject moistureConfig = card["moisture"].as<JsonObject>();
  resolveMoistureBounds(moistureConfig, card, slot.sensorId, 120, 175, slot.dry, slot.wet, slot.overdue, slot.pending);
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

void drawCompactCardMetrics(
  int x,
  int y,
  int w,
  int h,
  const char* moistureText,
  const char* batteryText,
  uint8_t moisturePct,
  uint8_t batteryPct,
  uint16_t moistureColor,
  uint16_t batteryColor,
  uint16_t mutedColor,
  uint16_t surfaceColor,
  uint16_t backgroundColor
)
{
  int moistY = y + max(18, h - 43);
  int moistBarY = moistY + 13;
  int battY = moistBarY + 8;
  int battBarY = battY + 13;
  if (battBarY + 5 > y + h - 4)
  {
    int shift = battBarY + 5 - (y + h - 4);
    moistY -= shift;
    moistBarY -= shift;
    battY -= shift;
    battBarY -= shift;
  }
  moistY = max(y + 17, moistY);
  moistBarY = max(moistY + 11, moistBarY);
  battY = max(moistBarY + 6, battY);
  battBarY = max(battY + 11, battBarY);

  tft.fillRect(x + 5, moistY - 1, w - 10, min(h - (moistY - y) - 3, 46), surfaceColor);
  drawTextLeft("Moist", x + 5, moistY, 1, mutedColor, surfaceColor);
  drawTextRight(moistureText, x + w - 5, moistY, 1, moistureColor, surfaceColor);
  drawBar(x + 5, moistBarY, w - 10, 5, moisturePct, moistureColor, backgroundColor);
  drawTextLeft("Batt", x + 5, battY, 1, mutedColor, surfaceColor);
  drawTextRight(batteryText, x + w - 5, battY, 1, batteryColor, surfaceColor);
  drawBar(x + 5, battBarY, w - 10, 5, batteryPct, batteryColor, backgroundColor);
}

void drawPlantCard(
  int x,
  int y,
  int w,
  int h,
  JsonObject card,
  bool showArt,
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
  int dry;
  int wet;
  bool overdue;
  bool pending;
  resolveMoistureBounds(moistureConfig, card, sensorId, 120, 175, dry, wet, overdue, pending);

  const bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[sensorId] : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  uint8_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, dry, wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = overdue ? dangerColor : healthColor(moisturePct, accentColor, warningColor, dangerColor, mutedColor);
  if (!stale)
  {
    moistureColor = applyLowMoistureBlink(moistureColor, dangerColor, mutedColor);
  }
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

  char moistureText[8];
  if (stale)
  {
    strlcpy(moistureText, "--", sizeof(moistureText));
  }
  else
  {
    snprintf(moistureText, sizeof(moistureText), "%u", reading->moisture);
  }

  if (h < 76)
  {
    drawCompactCardMetrics(x, y, w, h, moistureText, batteryText, moisturePct, batteryPct, moistureColor, batteryColor, mutedColor, surfaceColor, backgroundColor);
    return;
  }

  if (showArt)
  {
    drawPlantArtInCard(x, y, w, h, art, stale ? mutedColor : accentColor, surfaceColor, 0);
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
  uint16_t moistureColor = slot.overdue ? slot.dangerColor : healthColor(moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  if (!stale)
  {
    moistureColor = applyLowMoistureBlink(moistureColor, slot.dangerColor, slot.mutedColor);
  }
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

  if (h < 76)
  {
    drawCompactCardMetrics(x, y, w, h, moistureText, batteryText, moisturePct, batteryPct, moistureColor, batteryColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    return;
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

// The normal redraw gate only fires when new sensor data arrives (sensorDataDirty),
// which - now that the probe only advertises every ~60s or on a big swing instead of
// every wake - would make a "blinking" low-moisture alert change color only once a
// minute rather than actually blink. This checks whether any currently-visible,
// non-stale card is low enough to need that blink, so the caller can force a much
// faster redraw cadence just while it's needed (see applyLowMoistureBlink's 500ms
// phase and the render-gate call site in loop()).
bool anyLowMoistureCardVisible()
{
  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    PlantArtSlot& slot = artSlots[i];
    if (!slot.enabled || slot.sensorId == 0 || slot.sensorId > MAX_SENSOR_ID)
    {
      continue;
    }
    SensorReading& reading = sensors[slot.sensorId];
    bool stale = !reading.seen || (millis() - reading.lastSeenMs > staleSeconds * 1000UL);
    if (stale)
    {
      continue;
    }
    if (slot.overdue)
    {
      return true;
    }
    if (moisturePercent(reading.moisture, slot.dry, slot.wet) < 25)
    {
      return true;
    }
  }
  return false;
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

void drawSetupStep(int y, const char* number, const char* text, uint16_t textColor, uint16_t mutedColor, uint16_t backgroundColor)
{
  tft.fillRoundRect(16, y - 2, 22, 22, 11, mutedColor);
  drawTextCenter(number, 27, y + 4, 1, backgroundColor, mutedColor);
  drawTextLeft(text, 48, y + 3, 1, textColor, backgroundColor);
}

void renderWifiConnectingScreen(const char* ssid, const char* source)
{
#if !MSV2_ENABLE_DISPLAY
  (void)ssid;
  (void)source;
  return;
#else
  applyDisplayRotation(1, true);
  tft.fillScreen(TFT_BLACK);
  drawTextLeft("MSV2 Plants", 16, 16, 2, TFT_WHITE, TFT_BLACK);
  drawTextLeft("Connecting to home WiFi...", 16, 48, 1, TFT_GREEN, TFT_BLACK);
  drawTextLeft("Network", 16, 84, 1, TFT_DARKGREY, TFT_BLACK);
  drawTextLeft(ssid == nullptr || ssid[0] == '\0' ? "(none)" : ssid, 92, 84, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("Source", 16, 106, 1, TFT_DARKGREY, TFT_BLACK);
  drawTextLeft(source == nullptr ? "saved setup" : source, 92, 106, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("If this takes too long, setup help will appear here.", 16, 146, 1, TFT_DARKGREY, TFT_BLACK);
#endif
}

void renderWifiSetupScreen(const char* reason)
{
#if !MSV2_ENABLE_DISPLAY
  (void)reason;
  return;
#else
  artSlotCount = 0;
  sensorDataDirty = false;
  displayDirty = false;
  wifiSetupScreenDrawn = true;
  applyDisplayRotation(1, true);
  tft.fillScreen(TFT_BLACK);
  drawTextLeft("WiFi Setup", 16, 14, 2, TFT_WHITE, TFT_BLACK);
  drawTextLeft(reason == nullptr || reason[0] == '\0' ? "Ready for first-time setup" : reason, 16, 43, 1, TFT_ORANGE, TFT_BLACK);

  drawSetupStep(78, "1", "On your phone, open WiFi settings.", TFT_WHITE, TFT_GREEN, TFT_BLACK);
  drawSetupStep(110, "2", "Join network: MSV2-Display", TFT_WHITE, TFT_GREEN, TFT_BLACK);
  drawSetupStep(142, "3", "No password needed", TFT_WHITE, TFT_GREEN, TFT_BLACK);
  drawSetupStep(174, "4", "Choose home WiFi or local-only mode.", TFT_WHITE, TFT_GREEN, TFT_BLACK);

  drawTextLeft("If no page appears, open Safari/Chrome and type:", 16, 216, 1, TFT_DARKGREY, TFT_BLACK);
  drawTextLeft("192.168.4.1", 16, 238, 2, TFT_GREEN, TFT_BLACK);
  drawTextLeft("Then tap Setup > Receiver WiFi.", 16, 268, 1, TFT_WHITE, TFT_BLACK);
#endif
}

void renderWifiSavedScreen()
{
#if !MSV2_ENABLE_DISPLAY
  return;
#else
  artSlotCount = 0;
  sensorDataDirty = false;
  displayDirty = false;
  applyDisplayRotation(1, true);
  tft.fillScreen(TFT_BLACK);
  drawTextLeft("Settings Saved", 16, 16, 2, TFT_GREEN, TFT_BLACK);
  drawTextLeft("The receiver is restarting now.", 16, 54, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("After restart, this screen will show your plant dashboard.", 16, 86, 1, TFT_DARKGREY, TFT_BLACK);
#endif
}

void renderWifiRetryScreen(const char* ssid, uint8_t attempt, uint8_t maxAttempts)
{
#if !MSV2_ENABLE_DISPLAY
  (void)ssid;
  (void)attempt;
  (void)maxAttempts;
  return;
#else
  applyDisplayRotation(1, true);
  tft.fillScreen(TFT_BLACK);
  drawTextLeft("WiFi Retry", 16, 16, 2, TFT_ORANGE, TFT_BLACK);
  drawTextLeft("Could not reach your home WiFi yet.", 16, 54, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("Network", 16, 88, 1, TFT_DARKGREY, TFT_BLACK);
  drawTextLeft(ssid == nullptr || ssid[0] == '\0' ? "(saved network)" : ssid, 92, 88, 1, TFT_WHITE, TFT_BLACK);
  char retryText[32];
  snprintf(retryText, sizeof(retryText), "Retry %u of %u", attempt, maxAttempts);
  drawTextLeft(retryText, 16, 122, 1, TFT_GREEN, TFT_BLACK);
  drawTextLeft("Restarting and trying again...", 16, 154, 1, TFT_DARKGREY, TFT_BLACK);
#endif
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
#if MSV2_ENABLE_WIFI
  if (!wifiSetupPortalActive)
  {
    char appAddress[36];
    String ipText = currentIpAddress().toString();
    snprintf(appAddress, sizeof(appAddress), "App %s", ipText.c_str());
    drawTextRight(appAddress, tft.width() - 8, 12, 1, mutedColor, backgroundColor);
  }
#endif

  setCheckpoint("render: cards draw");
  JsonArray cards = doc["cards"].as<JsonArray>();
  artSlotCount = 0;
  if (!cards.isNull() && cards.size() > 0)
  {
    DashboardGrid grid = gridForCardCount(cards.size());
    int columns = grid.columns;
    int rows = grid.rows;
    int gap = clampInt(layout["gap"] | 6, 2, 12);
    int padding = clampInt(layout["padding"] | 6, 2, 16);
    int top = 34;
    int cardW = (tft.width() - padding * 2 - gap * (columns - 1)) / columns;
    int cardH = (tft.height() - top - padding - gap * (rows - 1)) / rows;
    int visibleCount = min((int)cards.size(), columns * rows);
    bool showCardArt = visibleCount <= 8;
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
      int rowItems = columns;
      int lastRowRemainder = visibleCount % columns;
      if (lastRowRemainder > 0 && row == visibleCount / columns)
      {
        rowItems = lastRowRemainder;
      }
      int rowOffset = ((columns - rowItems) * (cardW + gap)) / 2;
      int x = padding + rowOffset + col * (cardW + gap);
      int y = top + row * (cardH + gap);
      drawPlantCard(
        x,
        y,
        cardW,
        cardH,
        card,
        showCardArt,
        backgroundColor,
        surfaceColor,
        textColor,
        mutedColor,
        accentColor,
        warningColor,
        dangerColor
      );
      cachePlantArtSlot(x, y, cardW, cardH, card, showCardArt, backgroundColor, surfaceColor, textColor, accentColor, mutedColor, warningColor, dangerColor);
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
    if (!slot.artEnabled || slot.h < 76)
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

    // Cached result of the periodic cloud auto-calibration pull (see
    // runCloudAutoCalPull) - lets the browser use the same learned range/schedule the
    // physical display uses, without minting its own share token or hitting the cloud
    // directly (this data changes slowly, so a receiver-cached copy is plenty fresh).
    JsonObject autoCalItem = item.createNestedObject("autoCal");
    autoCalItem["ready"] = autoCal[sensorId].ready;
    autoCalItem["dryBound"] = autoCal[sensorId].dryBound;
    autoCalItem["wetBound"] = autoCal[sensorId].wetBound;
    autoCalItem["overdue"] = autoCal[sensorId].overdue;
    autoCalItem["daysSinceLastWatering"] = autoCal[sensorId].daysSinceLastWatering;
    autoCalItem["predictedIntervalDays"] = autoCal[sensorId].predictedIntervalDays;
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
          int dry = card["dry"] | 120;
          int wet = card["wet"] | 175;
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
  network["mode"] = currentNetworkMode();
  network["ip"] = currentIpAddress().toString();
  network["rssi"] = wifiStationConnected ? WiFi.RSSI() : 0;
  network["localOnly"] = wifiLocalOnly;
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

void setCloudConfigLastError(const char* error)
{
  strlcpy(cloudConfigLastError, error == nullptr ? "" : error, sizeof(cloudConfigLastError));
}

bool applyCloudUiConfig(JsonVariantConst configValue, uint32_t remoteVersion)
{
  if (configValue.isNull())
  {
    return false;
  }

  DynamicJsonDocument nextConfig(12288);
  nextConfig.set(configValue);
  JsonObject root = nextConfig.as<JsonObject>();
  if (root.isNull())
  {
    return false;
  }

  JsonObject meta = root["_meta"].to<JsonObject>();
  meta["schema"] = 1;
  meta["version"] = remoteVersion > 0 ? remoteVersion : uiConfigVersion + 1;
  meta["updatedMs"] = millis();
  meta["updatedBy"] = "cloud";

  File file = LittleFS.open(UI_PATH, "w");
  if (!file)
  {
    return false;
  }

  serializeJson(nextConfig, file);
  file.close();
  staleSeconds = nextConfig["staleSeconds"] | DEFAULT_STALE_SECONDS;
  cacheUiMetadata(nextConfig["_meta"]);
  displayDirty = true;
  scheduleCloudSyncSoon(true);
  return true;
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
    secureClient.setCACert(MSV2_TRUSTED_ROOT_CA_PEM);
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
    uint32_t syncIntervalSeconds = constrain((uint32_t)(responseDoc["syncIntervalSeconds"] | CLOUD_SYNC_DEFAULT_SECONDS), 60UL, 86400UL);

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

void runCloudConfigPull(uint32_t now)
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0')
  {
    nextCloudConfigPullMs = 0;
    return;
  }

  if (nextCloudConfigPullMs == 0)
  {
    nextCloudConfigPullMs = now + 10000UL;
    return;
  }

  if ((int32_t)(now - nextCloudConfigPullMs) < 0)
  {
    return;
  }

  wifiStationConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiStationConnected)
  {
    setCloudConfigLastError("waiting for station WiFi");
    nextCloudConfigPullMs = now + 10000UL;
    return;
  }

  const bool isHttps = strncmp(cloudUploadUrl, "https://", 8) == 0;
  const bool isHttp = strncmp(cloudUploadUrl, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    cloudConfigLastAttemptMs = now;
    cloudConfigLastHttpStatus = 0;
    setCloudConfigLastError("uploadUrl must start with http:// or https://");
    nextCloudConfigPullMs = now + CLOUD_CONFIG_PULL_INTERVAL_MS;
    return;
  }

  String configUrl = cloudUploadUrl;
  configUrl += configUrl.indexOf('?') >= 0 ? "&" : "?";
  configUrl += "config=1&receiverId=";
  configUrl += receiverId;
  configUrl += "&currentVersion=";
  configUrl += String(uiConfigVersion);

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(MSV2_TRUSTED_ROOT_CA_PEM);
    began = http.begin(secureClient, configUrl);
  }
  else
  {
    began = http.begin(plainClient, configUrl);
  }

  cloudConfigLastAttemptMs = now;
  cloudConfigLastHttpStatus = 0;
  if (!began)
  {
    setCloudConfigLastError("could not start HTTP client");
    nextCloudConfigPullMs = now + CLOUD_CONFIG_PULL_INTERVAL_MS;
    return;
  }

  http.setTimeout(8000);
  String auth = "Bearer ";
  auth += cloudApiToken;
  http.addHeader("Authorization", auth);

  int status = http.GET();
  cloudConfigLastHttpStatus = status;
  if (status >= 200 && status < 300)
  {
    String response = http.getString();
    DynamicJsonDocument responseDoc(12288);
    DeserializationError error = deserializeJson(responseDoc, response);
    JsonObject configRow = responseDoc["config"].as<JsonObject>();
    uint32_t remoteVersion = configRow["config_version"] | 0;
    if (error)
    {
      setCloudConfigLastError("invalid config response");
    }
    else if (configRow.isNull())
    {
      setCloudConfigLastError("");
      cloudConfigLastSuccessMs = millis();
    }
    else if (remoteVersion <= uiConfigVersion)
    {
      setCloudConfigLastError("");
      cloudConfigLastSuccessMs = millis();
    }
    else if (applyCloudUiConfig(configRow["config"], remoteVersion))
    {
      setCloudConfigLastError("");
      cloudConfigLastSuccessMs = millis();
      Serial.print("[MSV2 cloud] applied config v");
      Serial.println(remoteVersion);
    }
    else
    {
      setCloudConfigLastError("could not apply cloud config");
    }
  }
  else if (status > 0)
  {
    String error = "HTTP ";
    error += status;
    setCloudConfigLastError(error.c_str());
  }
  else
  {
    String error = http.errorToString(status);
    setCloudConfigLastError(error.c_str());
  }

  http.end();
  nextCloudConfigPullMs = millis() + CLOUD_CONFIG_PULL_INTERVAL_MS;
}

// Pulls the self-learned per-sensor moisture range + watering schedule (see
// docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md and migration 017) on the same throttled
// pull pattern as runCloudConfigPull above, just far less frequently since this data
// changes slowly. Cached in autoCal[] and reused by both the physical display and the
// browser (via sensorDataJson()'s "autoCal" field) - neither needs its own cloud
// round-trip for this.
void runCloudAutoCalPull(uint32_t now)
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0')
  {
    nextCloudAutoCalPullMs = 0;
    return;
  }

  if (nextCloudAutoCalPullMs == 0)
  {
    nextCloudAutoCalPullMs = now + 10000UL;
    return;
  }

  if ((int32_t)(now - nextCloudAutoCalPullMs) < 0)
  {
    return;
  }

  wifiStationConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiStationConnected)
  {
    nextCloudAutoCalPullMs = now + 10000UL;
    return;
  }

  const bool isHttps = strncmp(cloudUploadUrl, "https://", 8) == 0;
  const bool isHttp = strncmp(cloudUploadUrl, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    nextCloudAutoCalPullMs = now + CLOUD_AUTOCAL_PULL_INTERVAL_MS;
    return;
  }

  String autoCalUrl = cloudUploadUrl;
  autoCalUrl += autoCalUrl.indexOf('?') >= 0 ? "&" : "?";
  autoCalUrl += "autocal=1&receiverId=";
  autoCalUrl += receiverId;

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(MSV2_TRUSTED_ROOT_CA_PEM);
    began = http.begin(secureClient, autoCalUrl);
  }
  else
  {
    began = http.begin(plainClient, autoCalUrl);
  }

  if (!began)
  {
    nextCloudAutoCalPullMs = now + CLOUD_AUTOCAL_PULL_INTERVAL_MS;
    return;
  }

  http.setTimeout(8000);
  String auth = "Bearer ";
  auth += cloudApiToken;
  http.addHeader("Authorization", auth);

  int status = http.GET();
  if (status >= 200 && status < 300)
  {
    String response = http.getString();
    DynamicJsonDocument responseDoc(4096);
    DeserializationError error = deserializeJson(responseDoc, response);
    if (!error && (responseDoc["ok"] | false))
    {
      JsonArray sensorsArray = responseDoc["sensors"].as<JsonArray>();
      for (JsonObject sensorRow : sensorsArray)
      {
        uint16_t sensorId = sensorRow["sensorId"] | 0;
        if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
        {
          continue;
        }
        autoCal[sensorId].ready = sensorRow["ready"] | false;
        autoCal[sensorId].dryBound = sensorRow["dryBound"] | 0;
        autoCal[sensorId].wetBound = sensorRow["wetBound"] | 0;
        autoCal[sensorId].overdue = sensorRow["overdue"] | false;
        autoCal[sensorId].daysSinceLastWatering = sensorRow["daysSinceLastWatering"] | -1.0f;
        autoCal[sensorId].predictedIntervalDays = sensorRow["predictedIntervalDays"] | -1.0f;
      }
      Serial.println("[MSV2 cloud] auto-calibration updated");
    }
  }

  http.end();
  nextCloudAutoCalPullMs = millis() + CLOUD_AUTOCAL_PULL_INTERVAL_MS;
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
    secureClient.setCACert(MSV2_TRUSTED_ROOT_CA_PEM);
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
  doc["networkMode"] = currentNetworkMode();
  doc["ip"] = ip.toString();
  doc["setupSsid"] = AP_SSID;
  doc["setupUrl"] = "http://192.168.4.1/app";
  doc["configuredSsid"] = configuredWifiSsid;
  doc["hasStoredWifiCredentials"] = storedWifiLoaded;
  doc["localOnly"] = wifiLocalOnly;
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

// 2026-07-28 raw-values rework: probes now send raw oscillator kHz, raw battery mV, and a
// raw charging flag instead of interpreting them on-device (see MSV2_Moisture_BLE_Sleep.ino
// and the MSV2_RAW_PACKET_MIN_LEN comment above). All calibration - moisture inversion, the
// LiPo mV->percent curve, charge-current IR-drop compensation, and smoothing across wakes -
// lives here now instead, where it can be revised without reflashing a physically-installed
// probe, and the raw sensed values stay visible for debugging instead of only ever being
// seen through whatever transform happened to be baked into probe firmware at the time.

// Bench-observed raw range differs per hardware revision - roughly 10-90kHz on the original
// single-plate/100k probes, 220-360kHz on the reworked dual-plate/10k Scindapsus probe (see
// MSV2_Moisture_BLE_Sleep.ino's git history). Only sensor 1 (Scindapsus) has the rework as
// of this writing; add an entry here whenever another probe gets it too. PROVISIONAL, same
// as every other calibration constant in this project so far - bench-verified (touch/multi-
// finger contact as a moisture stimulus), not yet against real soil.
const uint16_t DEFAULT_MOISTURE_INVERT_CEILING = 200;
const uint16_t MOISTURE_INVERT_CEILING_BY_SENSOR[MAX_SENSOR_ID + 1] = {
    0,   // sensor 0 unused
    500, // sensor 1: Scindapsus, dual-plate/10k rework
};

uint16_t moistureCeilingForSensor(uint8_t sensorId)
{
  if (sensorId <= MAX_SENSOR_ID && MOISTURE_INVERT_CEILING_BY_SENSOR[sensorId] != 0)
  {
    return MOISTURE_INVERT_CEILING_BY_SENSOR[sensorId];
  }
  return DEFAULT_MOISTURE_INVERT_CEILING;
}

// Raw oscillator frequency runs the "wrong" way for a value called moisture - drier soil
// oscillates faster - so invert here (ceiling - freq), same transform the probe itself used
// to apply before 2026-07-28, just centralized and per-sensor instead of baked into one
// probe's firmware. Clamped to 0 rather than underflowing if a raw reading ever exceeds its
// sensor's ceiling.
uint16_t calibrateRawMoisture(uint8_t sensorId, uint16_t rawKHz)
{
  uint16_t ceiling = moistureCeilingForSensor(sensorId);
  return rawKHz >= ceiling ? 0 : (ceiling - rawKHz);
}

// Generic 1S LiPo open-circuit-voltage -> percent curve (a commonly cited hobbyist
// reference shape, e.g. https://blog.ampow.com/lipo-voltage-chart/ - NOT derived from this
// specific 150mAh cell's own discharge data, just a far better-shaped approximation than a
// straight line). Real LiPo terminal voltage stays close to full for a large fraction of
// actual usable capacity, then falls off a cliff near empty - piecewise-linear interpolation
// between real reference points tracks that shape much better than any single-slope formula
// can, without needing a coulomb counter or dedicated fuel-gauge IC. Moved here verbatim
// from the probe as part of the 2026-07-28 raw-values rework - see that file's git history
// for the original derivation/revision notes (2026-07-24 divider cal, 2026-07-25 taper fix).
struct BatteryCurvePoint
{
  float mv;
  float percent;
};

const BatteryCurvePoint BATTERY_CURVE[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
    {3980, 75}, {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55},
    {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35}, {3770, 30},
    {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},
    {3270, 0},
};
const uint8_t BATTERY_CURVE_POINTS = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);

float interpolateBatteryPercent(float mvolts)
{
  if (mvolts >= BATTERY_CURVE[0].mv)
  {
    return BATTERY_CURVE[0].percent;
  }

  if (mvolts <= BATTERY_CURVE[BATTERY_CURVE_POINTS - 1].mv)
  {
    return BATTERY_CURVE[BATTERY_CURVE_POINTS - 1].percent;
  }

  for (uint8_t i = 0; i < BATTERY_CURVE_POINTS - 1; i++)
  {
    if (mvolts <= BATTERY_CURVE[i].mv && mvolts >= BATTERY_CURVE[i + 1].mv)
    {
      float span = BATTERY_CURVE[i].mv - BATTERY_CURVE[i + 1].mv;
      float frac = (mvolts - BATTERY_CURVE[i + 1].mv) / span;
      return BATTERY_CURVE[i + 1].percent + frac * (BATTERY_CURVE[i].percent - BATTERY_CURVE[i + 1].percent);
    }
  }

  return 0; // unreachable - the two bounds checks above cover the full range
}

// Charging current flows through the cell's + path resistance in the opposite direction
// from discharge, so terminal voltage while charging sits ABOVE true rest/open-circuit
// voltage by roughly I*R - documented battery-industry behavior, not a bug (see the
// 2026-07-25 chat log in the probe's git history). Subtracting an estimated offset before
// running the curve approximates "what would this cell read at rest." PROVISIONAL: derived
// from one observed transition, needs refinement from a real logged charge cycle.
const float CHARGING_IR_COMPENSATION_MV = 400.0F;

// The IR-drop overshoot is proportional to actual charge CURRENT, which tapers toward zero
// during the constant-voltage phase as the cell approaches full - a fixed compensation right
// up until charging stops made the correction snap away all at once instead of tapering with
// it, showing up as a jump around 85%->100% (2026-07-25 chat log). Tapering the compensation
// itself to zero as the reading approaches full fixes that.
const float CHARGE_TAPER_START_MV = 4100.0F; // full compensation still applies at/below this
const float CHARGE_TAPER_END_MV = 4200.0F;   // compensation is fully gone at/above this (full)

float chargeCompensationMv(float mvolts)
{
  if (mvolts <= CHARGE_TAPER_START_MV)
  {
    return CHARGING_IR_COMPENSATION_MV;
  }

  if (mvolts >= CHARGE_TAPER_END_MV)
  {
    return 0.0F;
  }

  float remainingFrac = (CHARGE_TAPER_END_MV - mvolts) / (CHARGE_TAPER_END_MV - CHARGE_TAPER_START_MV);
  return CHARGING_IR_COMPENSATION_MV * remainingFrac;
}

uint8_t batteryMvToPercent(float mvolts, bool isCharging)
{
  float restEquivalentMv = isCharging ? mvolts - chargeCompensationMv(mvolts) : mvolts;
  return (uint8_t)(interpolateBatteryPercent(restEquivalentMv) + 0.5f);
}

// Per-sensor EMA smoothing state - was a single global on the probe (one probe, one
// battery); now one slot per sensor here, since a single receiver interprets every probe's
// raw readings. Smooths out per-sample ADC noise on the History chart's charging curve
// (0.2 factor, same as the moisture oscillator's own outlier handling). File-scope arrays
// zero-initialize, so smoothedBatteryReady[] starts all-false as intended.
float smoothedBatteryPercent[MAX_SENSOR_ID + 1];
bool smoothedBatteryReady[MAX_SENSOR_ID + 1];
bool lastChargingStateBySensor[MAX_SENSOR_ID + 1];

// A charge/discharge transition is a real, instantaneous jump in what the corrected curve
// reads (the IR-drop offset direction flips), not sensor noise - blending it through the
// normal EMA would just smear the jump across several readings instead of reflecting it.
// Snap straight to the new curve's reading on the packet where the state actually changes,
// then resume normal smoothing within that state.
uint8_t calibrateRawBattery(uint8_t sensorId, float rawMv, bool charging)
{
  float raw = batteryMvToPercent(rawMv, charging);
  if (sensorId > MAX_SENSOR_ID)
  {
    return (uint8_t)(raw + 0.5f);
  }

  if (!smoothedBatteryReady[sensorId] || charging != lastChargingStateBySensor[sensorId])
  {
    smoothedBatteryPercent[sensorId] = raw;
    smoothedBatteryReady[sensorId] = true;
  }
  else
  {
    smoothedBatteryPercent[sensorId] += 0.2f * (raw - smoothedBatteryPercent[sensorId]);
  }

  lastChargingStateBySensor[sensorId] = charging;
  return (uint8_t)(smoothedBatteryPercent[sensorId] + 0.5f);
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

    // len >= MSV2_RAW_PACKET_MIN_LEN doubles as the format-version check described above
    // that constant: probes running the 2026-07-28 raw-values rework send the wider
    // packet (raw kHz + raw mV + charging flag) and get calibrated here; any probe still
    // on pre-rework firmware sends the shorter legacy packet with already-calibrated
    // values and is passed through unchanged, same as before this rework.
    uint16_t moisture;
    uint8_t batteryPercent;
    if (len >= MSV2_RAW_PACKET_MIN_LEN)
    {
      uint16_t rawMoistureKHz = buffer[10] | (buffer[11] << 8);
      uint16_t rawBatteryMv = buffer[12] | (buffer[13] << 8);
      bool charging = (buffer[14] & 0x01) != 0;
      moisture = calibrateRawMoisture(sensorId, rawMoistureKHz);
      batteryPercent = calibrateRawBattery(sensorId, (float)rawBatteryMv, charging);
    }
    else
    {
      moisture = buffer[10] | (buffer[11] << 8);
      batteryPercent = len >= 13 ? buffer[12] : BATTERY_UNKNOWN;
    }

    SensorReading& reading = sensors[sensorId];

    if (reading.seen && wake == reading.wake)
    {
      return;
    }

    // wake going DOWN (rather than up) means the probe rebooted and its wake counter
    // reset to a small value, not that ~16 million packets were dropped - computing
    // (wake - reading.wake - 1) here would underflow and wrap to a huge spurious number
    // (masked to 24 bits since wake is only ever a 24-bit field on the wire). Previously
    // logged as "cosmetic, isolated to the missedTotal diagnostic stat" (2026-07-24), but
    // a probe reflash on 2026-07-25 produced exactly this condition (wake reset to a low
    // value) right before the receiver's WebServer/render loop hung - suspected trigger
    // via a corrupted value reaching the blocking HTTPClient cloud-sync call, not fully
    // confirmed, but worth fixing either way regardless of whether it was the cause here.
    reading.missed = (reading.seen && wake > reading.wake) ? (wake - reading.wake - 1) : 0;
    reading.missedTotal += reading.missed;
    reading.received++;
    reading.seen = true;
    reading.wake = wake;
    reading.moisture = moisture;
    reading.batteryPercent = batteryPercent;
    reading.rssi = advertisedDevice.getRSSI();
    reading.lastSeenMs = millis();
    sensorDataDirty = true;
    scheduleCloudSyncSoon();
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

#if MSV2_ENABLE_WIFI
// Runs the four cloud network calls (bootstrap, config pull, auto-cal pull, snapshot
// upload) on their own task instead of inline in loop(), mirroring bleScanTask above -
// the same pattern that already fixed BLE scanning contention with rendering/WebServer.
// Each of these does a blocking HTTPClient/WiFiClientSecure call (fresh TLS handshake
// every time, no session reuse) capped at an 8s timeout, but observed taking ~1.6-2.1s
// even on success. Running them inline in loop() meant the shared render/WebServer loop
// was blocked for that long on every one of these calls - the config pull alone fires
// every ~30s (CLOUD_CONFIG_PULL_INTERVAL_MS), which was frequent enough on its own to
// read as "periodic freezes" even with a perfectly calm probe (see 2026-07-26 chat log).
// Pinned to core 0, same as bleScanTask, leaving core 1 free for rendering/WebServer.
//
// Known, accepted simplification (not new risk introduced by this change): this task
// reads/writes cloudUploadUrl/cloudApiToken/wifiStationConnected and the sensors[]/
// autoCal[] arrays without a mutex, same as the existing BLE scan callback already does
// from its own context today - no new class of race condition, just one more reader/
// writer of state that was already being touched across task boundaries. The one thing
// that could have been a real hazard - applyCloudUiConfig() rewriting artSlots[] out
// from under a mid-render read - isn't actually a risk here: that function only writes
// ui.json to LittleFS and sets displayDirty, and renderUi() (which owns rebuilding
// artSlots[] from that file) only ever runs from the main loop, so this task never
// touches artSlots[] directly.
void cloudSyncTask(void* parameter)
{
  (void)parameter;
  for (;;)
  {
    uint32_t now = millis();
    if (wifiStationConnected)
    {
      runCloudBootstrap(now);
      runCloudConfigPull(now);
      runCloudAutoCalPull(now);
      runCloudSnapshotUploader(now);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
#endif

void setupServer()
{
  auto redirectToSetupApp = []() {
    server.sendHeader("Location", "http://192.168.4.1/app", true);
    server.sendHeader("Cache-Control", "no-store");
    server.send(302, "text/plain", "Open MSV2 setup at http://192.168.4.1/app\n");
  };

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

  server.on("/generate_204", HTTP_GET, redirectToSetupApp);
  server.on("/gen_204", HTTP_GET, redirectToSetupApp);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToSetupApp);
  server.on("/library/test/success.html", HTTP_GET, redirectToSetupApp);
  server.on("/connecttest.txt", HTTP_GET, redirectToSetupApp);
  server.on("/ncsi.txt", HTTP_GET, redirectToSetupApp);

  server.on("/ui", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /ui");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", readUiJson());
  });

  server.on("/ui", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /ui");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
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
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeCloudConfigJson(server.arg("plain"));
  });

  server.on("/cloud", HTTP_DELETE, []() {
    Serial.println("[MSV2 web] DELETE /cloud");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    resetCloudConfig();
  });

  server.on("/wifi", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /wifi");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", wifiStatusJson());
  });

  server.on("/wifi", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /wifi");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeWifiConfigJson(server.arg("plain"));
  });

  server.on("/wifi", HTTP_DELETE, []() {
    Serial.println("[MSV2 web] DELETE /wifi");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    forgetWifiConfig();
  });

  server.onNotFound([]() {
    Serial.print("[MSV2 web] 404 ");
    Serial.println(server.uri());
    if (!wifiStationConnected)
    {
      server.sendHeader("Location", "http://192.168.4.1/app", true);
      server.sendHeader("Cache-Control", "no-store");
      server.send(302, "text/plain", "Open MSV2 setup at http://192.168.4.1/app\n");
      return;
    }
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

const char* currentNetworkMode()
{
#if MSV2_ENABLE_WIFI
  if (wifiStationConnected)
  {
    return "station";
  }
  if (wifiLocalOnly && !wifiSetupPortalActive)
  {
    return "local-only";
  }
  return "access-point";
#else
  return "offline";
#endif
}

void setupWifi()
{
#if MSV2_ENABLE_WIFI
  wifiStationConnected = false;
  wifiSetupPortalActive = false;
  loadStoredWifiConfig();

  const bool forceSetupUnlocked = MSV2_FORCE_WIFI_SETUP != 0 && LittleFS.exists(FORCE_WIFI_SETUP_UNLOCK_PATH);
  const bool forceSetupMode = MSV2_FORCE_WIFI_SETUP != 0 && !forceSetupUnlocked;
  const bool useLocalOnly = wifiLocalOnly && !forceSetupMode;
  const char* stationSsid = (forceSetupMode || useLocalOnly) ? "" : (storedWifiLoaded ? configuredWifiSsid : WIFI_SSID);
  const char* stationPassword = (forceSetupMode || useLocalOnly) ? "" : (storedWifiLoaded ? configuredWifiPassword : WIFI_PASSWORD);

  if (forceSetupMode)
  {
    Serial.println("[MSV2 wifi] forced setup mode; no saved phone setup choice found");
  }
  else if (useLocalOnly)
  {
    Serial.println("[MSV2 wifi] local-only mode enabled; starting receiver access point");
  }
  else if (forceSetupUnlocked)
  {
    Serial.println("[MSV2 wifi] forced setup completed; using saved phone setup choice");
  }

  if (strlen(stationSsid) > 0)
  {
    bootLog("connecting WiFi STA");
    Serial.print("[MSV2 wifi] SSID: ");
    Serial.println(stationSsid);
    Serial.print("[MSV2 wifi] credential source: ");
    Serial.println(storedWifiLoaded ? "LittleFS" : "firmware");
    renderWifiConnectingScreen(stationSsid, storedWifiLoaded ? "saved from phone app" : "firmware default");

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
      clearWifiRetryState();
      Serial.print("[MSV2 wifi] connected: ");
      Serial.println(WiFi.localIP());
      return;
    }

    Serial.println("[MSV2 wifi] station connect failed; falling back to AP");
    if (storedWifiLoaded)
    {
      uint8_t retryCount = loadWifiRetryCount();
      if (retryCount < WIFI_CONNECT_REBOOT_RETRIES)
      {
        retryCount++;
        saveWifiRetryCount(retryCount);
        Serial.print("[MSV2 wifi] saved credential retry reboot ");
        Serial.print(retryCount);
        Serial.print("/");
        Serial.println(WIFI_CONNECT_REBOOT_RETRIES);
        renderWifiRetryScreen(stationSsid, retryCount, WIFI_CONNECT_REBOOT_RETRIES);
        Serial.flush();
        delay(1500);
        ESP.restart();
      }
      clearWifiRetryState();
      Serial.println("[MSV2 wifi] saved credential retries exhausted; starting setup AP");
    }
    wifiSetupPortalActive = true;
    renderWifiSetupScreen("Could not join home WiFi");
    WiFi.disconnect(true);
    delay(250);
  }
  else if (useLocalOnly)
  {
    clearWifiRetryState();
    wifiSetupScreenDrawn = false;
  }
  else
  {
    wifiSetupPortalActive = true;
    renderWifiSetupScreen("First-time setup needed");
  }

  bootLog("starting WiFi AP");
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  bool apStarted = AP_PASSWORD[0] == '\0' ? WiFi.softAP(AP_SSID) : WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apStarted)
  {
    Serial.println("[MSV2 wifi] AP start failed; retrying radio reset");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    apStarted = AP_PASSWORD[0] == '\0' ? WiFi.softAP(AP_SSID) : WiFi.softAP(AP_SSID, AP_PASSWORD);
  }
  if (!apStarted)
  {
    bootLog("WiFi AP start failed");
  }
  else
  {
    Serial.print("[MSV2 wifi] AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[MSV2 wifi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    if (dnsServerPtr == nullptr)
    {
      dnsServerPtr = new DNSServer();
    }
    if (dnsServerPtr != nullptr)
    {
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.println("[MSV2 wifi] captive DNS started");
    }
  }
#endif
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.println(MSV2_BUILD_TAG);
  Serial.print("[MSV2 boot] previous checkpoint before reset: ");
  Serial.println(lastCheckpoint);
  Serial.print("[MSV2 boot] reset reason: ");
  Serial.println(resetReasonName(resetReason));
  randomSeed(esp_random());
  bootLog("serial ready");

  adminUnlockUntilMs = millis() + ADMIN_GRACE_WINDOW_MS;

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
  if (xTaskCreatePinnedToCore(cloudSyncTask, "msv2_cloud_sync", 16384, nullptr, 1, &cloudSyncTaskHandle, 0) == pdPASS)
  {
    bootLog("cloud sync task running");
  }
  else
  {
    bootLog("cloud sync task failed");
  }
#else
  bootLog("WiFi disabled");
#endif

#if MSV2_ENABLE_BLE
#if MSV2_ENABLE_WIFI
  if (wifiSetupPortalActive)
  {
    bootLog("BLE scanner deferred until WiFi setup is finished");
  }
  else
#endif
  {
    bootLog("initializing BLE scanner");
    BLEDevice::init("MSV2-DISPLAY");
    bleScan = BLEDevice::getScan();
    bleScan->setAdvertisedDeviceCallbacks(new Msv2AdvertisedDeviceCallbacks(), true);
    bleScan->setActiveScan(false);
    bleScan->setInterval(120);
    bleScan->setWindow(40);
    if (xTaskCreatePinnedToCore(bleScanTask, "msv2_ble_scan", 8192, nullptr, 1, &bleScanTaskHandle, 0) == pdPASS)
    {
      bootLog("BLE scanner task running");
    }
    else
    {
      bootLog("BLE scanner task failed");
    }
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
#if MSV2_ENABLE_WIFI
  if (wifiStationConnected)
  {
    renderUi();
  }
  else if (!wifiSetupPortalActive)
  {
    renderUi();
  }
  else if (!wifiSetupScreenDrawn)
  {
    renderWifiSetupScreen("Setup page ready");
  }
#else
  renderUi();
#endif
  bootLog("setup complete");
}

void loop()
{
  uint32_t now = millis();

#if MSV2_ENABLE_WIFI
  if (dnsServerPtr != nullptr && !wifiStationConnected)
  {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  if (pendingRestartMs != 0 && (int32_t)(now - pendingRestartMs) >= 0)
  {
    Serial.println("[MSV2 wifi] restarting after WiFi config change");
    Serial.flush();
    delay(50);
    ESP.restart();
  }
  // Cloud bootstrap/config-pull/auto-cal-pull/snapshot-upload now run on their own task
  // (cloudSyncTask, created in setup()) instead of inline here - see that function's
  // comment for why: these are blocking HTTPClient calls that were stalling this shared
  // render/WebServer loop every time they fired, most noticeably the ~30s config pull.
#endif

#if MSV2_ENABLE_DISPLAY
#if MSV2_ENABLE_WIFI
  if (pendingRestartMs != 0)
  {
    displayDirty = false;
    sensorDataDirty = false;
    delay(1);
    return;
  }

  if (wifiSetupPortalActive)
  {
    displayDirty = false;
    sensorDataDirty = false;
    delay(1);
    return;
  }
#endif

  // A low-moisture card needs to actually blink (see applyLowMoistureBlink), which
  // needs redraws roughly every 500ms - much faster than the normal dirty-data-gated
  // cadence, since the probe's new adaptive advertising means sensorDataDirty can go a
  // full minute between updates otherwise.
  bool blinkNeeded = anyLowMoistureCardVisible();
  uint32_t renderIntervalMs = blinkNeeded ? 500 : SENSOR_RENDER_INTERVAL_MS;
  if ((sensorDataDirty || blinkNeeded) && now - lastSensorRenderMs >= renderIntervalMs)
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
