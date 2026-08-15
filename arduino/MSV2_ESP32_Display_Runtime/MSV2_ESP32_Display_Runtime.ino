/*********************************************************************
  MSV2 ESP32 BLE advertisement display runtime.

  Flash this sketch once. After that, update the screen layout by pushing
  ui.json over WiFi:

    python scripts/push_display_ui.py --file ui/display_default.json

  Arduino library requirements:
    - ESP32 board package
    - GFX Library for Arduino
    - ArduinoJson
    - XPT2046_Touchscreen

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
#define MSV2_BUILD_TAG "MSV2 runtime build 2026-08-13 wifi-self-heal-card-redraw-v1"

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
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
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
  // Backlight is gated by Q2 (2N7002) on GPIO11, non-inverted -- see
  // MSV2_ESP32_Backlight_Test. Arduino_GFX doesn't drive this pin itself,
  // so it has to be switched on explicitly or the panel stays dark even
  // though SPI init/draw succeeds.
  Serial.println("[MSV2 display test] enabling 75% backlight PWM (GPIO11)");
  if (!ledcAttach(11, 1000, 10))
  {
    Serial.println("[MSV2 display test] backlight PWM attach failed");
  }
  ledcWrite(11, 575); // 75%^2 perceived-brightness curve, 10-bit duty

  Serial.println("[MSV2 display test] initializing Arduino_GFX");
  Arduino_DataBus* testBus = new Arduino_ESP32SPI(14, 16, 12, 13, -1);
  Arduino_GFX* testGfx = new Arduino_ST7796(testBus, 15, 1, false, 320, 480);
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
const char DISPLAY_SETTINGS_PATH[] = "/display.json";
const char TOUCH_CALIBRATION_PATH[] = "/touch-calibration.json";
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
// 2026-08-13: back down from 3900s (65 min). That value was sized for a 30-minute
// probe wake/advertise cadence that's since been superseded (see 2026-08-09 in
// docs/STATUS_HANDOFF.md) - probes are back on the same ~10s-measure/60s-advertise
// bench cadence 90s was originally tuned for, with immediate advertises on a big
// moisture swing making steady-state gaps typically shorter than 60s, not longer.
// 180s tolerates two fully missed 60s advertises (packet loss or a bad scan window)
// plus margin before a card actually goes stale, while still flagging a truly
// dead/dead-battery probe within a few minutes instead of over an hour.
const uint32_t DEFAULT_STALE_SECONDS = 180;
const uint8_t BATTERY_UNKNOWN = 255;
const uint8_t MAX_ART_SLOTS = 16;
const uint32_t SENSOR_RENDER_INTERVAL_MS = 2000;
const uint32_t CLOUD_BOOTSTRAP_FIRST_TRY_MS = 15000;
const uint32_t CLOUD_BOOTSTRAP_RETRY_MS = 300000;
const uint32_t CLOUD_SYNC_DEFAULT_SECONDS = 60;
const uint32_t CLOUD_SYNC_SOON_MS = 5000;
const uint32_t CLOUD_CONFIG_PULL_INTERVAL_MS = 30000;
const uint32_t CLOUD_DISPLAY_PULL_INTERVAL_MS = 30000;
const uint32_t CLOUD_AUTOCAL_PULL_INTERVAL_MS = 600000; // 10 min - learned range/schedule changes slowly
// Cloud HTTP/TLS and BLE scanning intentionally stay off the render/WebServer core.
// BLE gets the higher application-task priority so a due cloud request cannot delay the
// next scan window. The ESP32 BLE host/controller tasks retain their own higher priorities.
const UBaseType_t CLOUD_SYNC_TASK_PRIORITY = 1;
const UBaseType_t BLE_SCAN_TASK_PRIORITY = 2;
const uint8_t WIFI_CONNECT_REBOOT_RETRIES = 2;
const uint32_t WIFI_HEALTH_CHECK_INTERVAL_MS = 1000;
const uint32_t WIFI_RECONNECT_MIN_DELAY_MS = 5000;
const uint32_t WIFI_RECONNECT_MAX_DELAY_MS = 60000;
const uint8_t WIFI_RECONNECT_RADIO_RESET_EVERY = 5;

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
  // Raw, uncalibrated values as transmitted by 2026-07-28-rework probes (see
  // calibrateRawMoisture()/calibrateRawBattery()) - kept alongside the calibrated fields
  // above instead of being discarded after use, specifically so the raw sensed signal
  // stays visible for troubleshooting instead of only ever being seen through whatever
  // calibration transform happened to apply. hasRaw is false for probes still on
  // pre-rework firmware (legacy packet, no raw fields to show).
  bool hasRaw = false;
  uint16_t rawMoistureKHz = 0;
  uint16_t rawBatteryMv = 0;
  bool rawCharging = false;
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
const int TOUCH_IRQ = 7;
const int TOUCH_MISO = 8;
const int TOUCH_CS = 9;
const int TFT_MISO = 10; // Wired for optional ST7796S reads; runtime SPI input uses TOUCH_MISO.
const int TFT_BACKLIGHT = 11;
const int TFT_DC = 14;
const int TFT_RST = 15;
const int TFT_CS = 16;
const uint32_t BACKLIGHT_PWM_FREQUENCY_HZ = 1000;
const uint8_t BACKLIGHT_PWM_RESOLUTION_BITS = 10;
const uint16_t BACKLIGHT_PWM_MAX = (1U << BACKLIGHT_PWM_RESOLUTION_BITS) - 1;
const uint8_t DEFAULT_BRIGHTNESS_PERCENT = 75;
// XPT2046_Touchscreen's polling-mode update() (see the comment on touchScreen below)
// performs a real SPI transaction on every touched() call, forever, whether or not a
// finger is present - this is new bus traffic since touch was added, interleaved with
// display writes on every loop() iteration. Widened from 16ms to reduce how often the
// bus switches devices per second; still comfortably responsive for a slider drag (30Hz).
const uint32_t TOUCH_POLL_INTERVAL_MS = 32;
// Open the receiver brightness controls with two completed taps anywhere on the
// dashboard. A minimum release-to-release gap rejects resistive-panel contact
// bounce; the generous upper bound is easier to use than the old 48x40 corner target.
const uint32_t TOUCH_DOUBLE_TAP_MIN_GAP_MS = 120;
const uint32_t TOUCH_DOUBLE_TAP_WINDOW_MS = 650;
const int TOUCH_TARGET_MARGIN = 48;
// Arduino_GFX defaults an ESP32 SPI bus to 40MHz. That was fine when the ST7796S was the
// only device on the bus; now XPT2046 is electrically tapped onto the same SCK/MOSI traces
// (deselected via TOUCH_CS between reads, but still an extra chip's input capacitance on
// the line). Dropped 40MHz -> 20MHz -> 10MHz; neither step fixed the reported animation-art
// corruption (stray colored pixels, "two frames superimposed" look), which is evidence
// against a purely clock-speed-driven signal integrity cause. Left at 10MHz since it's still
// a reasonable margin and isn't implicated as having made anything worse. See
// docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md for the full investigation - this bug
// is UNRESOLVED, do not assume this constant is the fix.
const uint32_t DISPLAY_SPI_HZ = 10000000;

uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Every full-screen state (WiFi setup/calibration/error screens) and the touch overlay
// draw against this fixed palette instead of the ui.json theme, since it renders before
// - or on top of - ui.json is loaded. Values are the exact web dashboard tokens
// (web/plant-dashboard/index.html :root) so the physical display never looks like a
// different app from the PWA and the themed plant cards in renderUi().
const uint16_t TFT_BLACK = color565(0x0E, 0x14, 0x1B);    // --bg
const uint16_t TFT_SURFACE = color565(0x17, 0x21, 0x2B);  // --panel
const uint16_t TFT_SURFACE_2 = color565(0x1D, 0x2A, 0x35); // --panel-2, nested controls
const uint16_t TFT_MUTED = color565(0x8F, 0xA3, 0xAD);    // --muted
const uint16_t TFT_WHITE = color565(0xF5, 0xF7, 0xFA);    // --ink
const uint16_t TFT_DANGER = color565(0xFF, 0x5A, 0x5F);   // --danger
const uint16_t TFT_ACCENT = color565(0x20, 0xC9, 0x97);   // --accent
const uint16_t TFT_WARNING = color565(0xFF, 0xB0, 0x20);  // --warn

Arduino_DataBus* busPtr = nullptr;
Arduino_GFX* gfxPtr = nullptr;
SPIClass displaySpi(FSPI);
// Poll pressure over SPI instead of allowing the library to gate all reads behind T_IRQ.
// This board exposes T_IRQ for diagnostics, but a missed/floating IRQ edge otherwise leaves
// XPT2046_Touchscreen's isrWake false forever and makes every later touch look absent.
XPT2046_Touchscreen touchScreen(TOUCH_CS);
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
  char plant[24] = "";
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
  // Inputs to refreshSlotMoistureBounds(), cached once from ui.json since they can't
  // change without a full renderUi() rebuild anyway. autoCal[] itself (read using these)
  // is live, updated by a background cloud task - see refreshSlotMoistureBounds().
  bool autoEnabled = false;
  int manualDry = 120;
  int manualWet = 175;
  uint8_t frame = 0;
  uint16_t frameIntervalMs = 200;
  uint32_t nextFrameMs = 0;
  // Last-drawn state for drawPlantCardDataOnly()'s moist/batt rows, so a redraw pass that
  // finds nothing actually different (e.g. a low-moisture blink tick with no new sensor
  // data) can skip that row entirely instead of clearing and repainting it every time.
  // metricsInitialized starts false so the first data-only draw after a fresh layout always
  // paints (cheap, happens once per full renderUi(), and avoids needing to duplicate
  // drawPlantCard's value computation here just to seed a matching cache).
  bool metricsInitialized = false;
  bool lastMoistureStale = true;
  uint16_t lastMoistureRaw = 0;
  uint8_t lastMoisturePct = 0;
  uint16_t lastMoistureColor = 0;
  uint8_t lastBatteryPct = 0;
  // Last-drawn state for drawPlantCardDetailDataOnly()'s diagnostic rows - same "only
  // repaint what changed" principle as the fields above, see seedDetailCache() and the
  // display-surgical-redraw skill. Formatted text is cached (not raw values) so the
  // comparison is a single strcmp against exactly what's on screen, with no separate
  // "how is this value bucketed for display" logic to keep in sync in two places.
  char lastDetailSignalLabel[10] = "";
  uint16_t lastDetailSignalColor = 0;
  char lastDetailRssiText[10] = "";
  char lastDetailAgeText[12] = "";
  char lastDetailWakeText[12] = "";
  char lastDetailLossText[8] = "";
  char lastDetailReceivedText[12] = "";
  char lastDetailMissedText[12] = "";
  char lastDetailRangeText[16] = "";
  char lastDetailRawText[16] = "";
  bool lastDetailCharging = false;
};

// All the formatted text/colors the detail view needs, computed once and shared by both
// drawPlantCardDetail() (full paint) and drawPlantCardDetailDataOnly() (diff-and-redraw)
// so the two can never drift out of sync on what a given row's "current value" is. Defined
// up here (not next to computeDetailStats()/drawPlantCardDetail() where it's actually used)
// for the same reason as the DashboardGrid/PlantArtSlot prototypes below - see that comment.
struct PlantDetailStats
{
  bool stale;
  uint8_t moisturePct;
  uint8_t batteryPct;
  uint16_t moistureColor;
  uint16_t batteryColor;
  uint16_t rawMoisture;
  char moistureText[8];
  char batteryText[8];
  bool hasSignal;
  uint16_t signalColor;
  const char* signalLabel;
  char rssiText[10];
  char ageText[12];
  char wakeText[12];
  char lossText[8];
  char receivedText[12];
  char missedText[12];
  char rangeText[16];
  char probeText[8];
  bool hasRaw;
  char rawText[16];
  bool rawCharging;
};

struct DashboardGrid
{
  int columns = 4;
  int rows = 2;
};

struct DisplaySettings
{
  uint8_t brightnessPercent = DEFAULT_BRIGHTNESS_PERCENT;
  bool screenOn = true;
  uint32_t version = 0; // Assigned by the server; local/offline changes keep this base version.
  bool pendingCloudUpdate = false;
  uint32_t localGeneration = 0;
  char updatedBy[24] = "factory-default";
};

// Three raw XPT2046 points define an affine transform into the current 480x320
// display coordinate space. This handles swapped and inverted panel axes without
// baking a particular module's resistor tolerances into firmware.
struct TouchCalibration
{
  bool valid = false;
  int16_t topLeftX = 300;
  int16_t topLeftY = 300;
  int16_t topRightX = 3800;
  int16_t topRightY = 300;
  int16_t bottomLeftX = 300;
  int16_t bottomLeftY = 3800;
};

PlantArtSlot artSlots[MAX_ART_SLOTS];
uint8_t artSlotCount = 0;

// Arduino's auto-generated prototypes are inserted before these struct definitions, so any
// function using DashboardGrid/PlantArtSlot in its signature needs a manual prototype here
// (after the struct exists) or the sketch fails with "'DashboardGrid' does not name a type".
DashboardGrid gridForCardCount(size_t count);
void drawPlantCardDataOnly(PlantArtSlot& slot);
void drawPlantCardDetail(PlantArtSlot& slot);
void drawPlantCardDetailDataOnly(PlantArtSlot& slot);
void expandPlantCardInPlace(PlantArtSlot& slot);
void collapsePlantCardInPlace(PlantArtSlot& slot);
void computeDetailStats(PlantArtSlot& slot, PlantDetailStats& s);
void seedDetailDiagnosticCache(PlantArtSlot& slot, const PlantDetailStats& s);
void seedDetailCache(PlantArtSlot& slot, const PlantDetailStats& s);
void drawPlantCardDetailInfoBand(PlantArtSlot& slot, const PlantDetailStats& s);
void refreshSlotMoistureBounds(PlantArtSlot& slot);
bool slotNeedsBlink(PlantArtSlot& slot);

#if MSV2_ENABLE_DISPLAY
#define tft (*gfxPtr)
// gfxPtr's declared type is the base Arduino_GFX, which doesn't expose writeAddrWindow()/
// writePixels() (those are added by the Arduino_TFT subclass); gfxPtr is always actually
// constructed as an Arduino_ST7796, which derives from Arduino_TFT, so this cast is safe.
#define tftBus (*static_cast<Arduino_TFT*>(gfxPtr))
#endif
#if MSV2_ENABLE_WIFI
#define server (*serverPtr)
#define dnsServer (*dnsServerPtr)
TaskHandle_t cloudSyncTaskHandle = nullptr;
#endif

// volatile: both written from applyCloudUiConfig() on the cloud-sync task (pinned to
// core 0) and read/cleared from loop() on the main render task (core 1) - same
// producer/consumer shape as sensorDataDirty just below, which already had volatile.
volatile bool displayDirty = true;
volatile bool sensorDataDirty = false;
bool wifiSetupScreenDrawn = false;
uint32_t lastSensorRenderMs = 0;
volatile uint32_t staleSeconds = DEFAULT_STALE_SECONDS;
RTC_DATA_ATTR char lastCheckpoint[48] = "fresh boot";
volatile bool wifiStationConnected = false;
volatile bool wifiStationDesired = false;
bool wifiLocalOnly = false;
bool wifiSetupPortalActive = false;
bool storedWifiLoaded = false;
char configuredWifiSsid[64] = "";
char configuredWifiPassword[96] = "";
char activeWifiSsid[64] = "";
char activeWifiPassword[96] = "";
uint32_t wifiLastHealthCheckMs = 0;
uint32_t wifiDisconnectedSinceMs = 0;
uint32_t wifiLastConnectedMs = 0;
uint32_t wifiLastReconnectAttemptMs = 0;
uint32_t wifiDisconnectCount = 0;
uint32_t wifiReconnectAttemptTotal = 0;
uint32_t wifiReconnectAttempts = 0;
uint32_t nextWifiReconnectMs = 0;
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
  // Rollover-safe comparison (matches the (int32_t)(now - deadline) idiom used
  // elsewhere in this file, e.g. scheduleCloudSyncSoon()) - a plain millis() >=
  // adminUnlockUntilMs comparison would misfire once millis() wraps at ~49.7 days:
  // post-wrap, a small live millis() briefly reads as "less than" the small
  // adminUnlockUntilMs left over from boot, so the window would look still-open
  // (or freshly reopened) on a receiver that's been up for weeks, defeating the
  // whole post-boot-only admin gate.
  const uint32_t now = millis();
  if ((int32_t)(now - adminUnlockUntilMs) >= 0)
  {
    return false;
  }
  adminUnlockUntilMs = now + ADMIN_GRACE_WINDOW_MS;
  return true;
}
bool cloudEnabled = false;
char cloudUploadUrl[160] = "";
char cloudApiToken[80] = "";
uint32_t cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
uint32_t nextCloudSyncMs = 0;
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
DisplaySettings displaySettings;
TouchCalibration touchCalibration;
portMUX_TYPE displaySettingsMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t displaySettingsFileMutex = nullptr;
bool touchReady = false;
bool brightnessOverlayOpen = false;
bool touchWasDown = false;
bool touchGestureChangedBrightness = false;
bool touchWakeOnlyThisGesture = false;
int16_t lastTouchX = -1;
int16_t lastTouchY = -1;
uint32_t lastTouchPollMs = 0;
uint32_t lastDashboardTapMs = 0;
int16_t pendingSingleTapX = -1;
int16_t pendingSingleTapY = -1;
bool singleTapPending = false;
int8_t expandedCardSlot = -1; // artSlots[] index rendered as the tap-to-expand detail view, -1 = none
uint32_t nextCloudDisplaySyncMs = 0;
uint32_t cloudDisplayLastAttemptMs = 0;
uint32_t cloudDisplayLastSuccessMs = 0;
int cloudDisplayLastHttpStatus = 0;
char cloudDisplayLastError[96] = "";
bool cloudDisplayApiReady = false;

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
    "\"staleSeconds\":3900,"
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

// Used only for an explicit local/remote configuration change that should be acknowledged
// promptly. Sensor advertisements must not call this: routine snapshot cadence is governed
// exclusively by cloudSyncIntervalSeconds so frequent BLE traffic cannot create frequent
// blocking HTTPS/TLS work on the BLE core.
void scheduleCloudSnapshotAfterStateChange()
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0')
  {
    return;
  }

  const uint32_t now = millis();
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
  scheduleCloudSnapshotAfterStateChange();
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
  nextCloudDisplaySyncMs = 0;
  cloudDisplayLastAttemptMs = 0;
  cloudDisplayLastSuccessMs = 0;
  cloudDisplayLastHttpStatus = 0;
  cloudDisplayLastError[0] = '\0';
  cloudDisplayApiReady = false;
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
  nextCloudDisplaySyncMs = cloudEnabled ? millis() + 7000UL : 0;
  return true;
}

String cloudConfigJson()
{
  StaticJsonDocument<1792> doc;
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
  upload["packetTriggered"] = false;
  upload["stateChangeDelayMs"] = CLOUD_SYNC_SOON_MS;
  JsonObject tasking = doc.createNestedObject("tasking");
  tasking["core"] = 0;
  tasking["cloudPriority"] = CLOUD_SYNC_TASK_PRIORITY;
  tasking["bleScanPriority"] = BLE_SCAN_TASK_PRIORITY;
  JsonObject configPull = doc.createNestedObject("configPull");
  configPull["lastAttemptMs"] = cloudConfigLastAttemptMs;
  configPull["lastSuccessMs"] = cloudConfigLastSuccessMs;
  configPull["lastHttpStatus"] = cloudConfigLastHttpStatus;
  configPull["lastError"] = cloudConfigLastError;
  configPull["nextAttemptMs"] = cloudEnabled ? nextCloudConfigPullMs : 0;
  JsonObject displaySync = doc.createNestedObject("displaySync");
  displaySync["lastAttemptMs"] = cloudDisplayLastAttemptMs;
  displaySync["lastSuccessMs"] = cloudDisplayLastSuccessMs;
  displaySync["lastHttpStatus"] = cloudDisplayLastHttpStatus;
  displaySync["lastError"] = cloudDisplayLastError;
  displaySync["nextAttemptMs"] = cloudEnabled ? nextCloudDisplaySyncMs : 0;
  displaySync["apiReady"] = cloudDisplayApiReady;
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
  nextCloudDisplaySyncMs = cloudEnabled ? millis() + 7000UL : 0;
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
  nextCloudDisplaySyncMs = 0;
  cloudDisplayLastAttemptMs = 0;
  cloudDisplayLastSuccessMs = 0;
  cloudDisplayLastHttpStatus = 0;
  cloudDisplayLastError[0] = '\0';
  cloudDisplayApiReady = false;
  nextCloudAutoCalPullMs = 0;
  nextCloudBootstrapMs = millis() + CLOUD_BOOTSTRAP_FIRST_TRY_MS;
  server.send(200, "application/json", "{\"ok\":true}");
  return true;
}

String wifiStatusJson()
{
  StaticJsonDocument<768> doc;
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
  doc["stationDesired"] = wifiStationDesired;
  doc["wifiStatus"] = (int)WiFi.status();
  doc["disconnectCount"] = wifiDisconnectCount;
  doc["disconnectedSinceMs"] = wifiDisconnectedSinceMs;
  doc["lastConnectedMs"] = wifiLastConnectedMs;
  doc["lastReconnectAttemptMs"] = wifiLastReconnectAttemptMs;
  doc["reconnectAttempts"] = wifiReconnectAttempts;
  doc["reconnectAttemptTotal"] = wifiReconnectAttemptTotal;
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

uint16_t brightnessPercentToDuty(uint8_t percent)
{
  if (percent == 0)
  {
    return 0;
  }

  // A squared curve is a small, deterministic gamma correction: the existing
  // bench sweep confirmed PWM is electrically linear, while perceived LED
  // brightness is not. Preserve a small non-zero floor for very low settings.
  uint32_t scaled = (uint32_t)percent * (uint32_t)percent * BACKLIGHT_PWM_MAX;
  uint16_t duty = (uint16_t)((scaled + 5000UL) / 10000UL);
  return duty < 2 ? 2 : duty;
}

uint8_t effectiveBrightnessPercent()
{
  portENTER_CRITICAL(&displaySettingsMux);
  uint8_t value = displaySettings.screenOn ? displaySettings.brightnessPercent : 0;
  portEXIT_CRITICAL(&displaySettingsMux);
  return value;
}

void applyBacklightFromSettings()
{
  ledcWrite(TFT_BACKLIGHT, brightnessPercentToDuty(effectiveBrightnessPercent()));
}

bool persistDisplaySettings()
{
  DisplaySettings snapshot;
  portENTER_CRITICAL(&displaySettingsMux);
  snapshot = displaySettings;
  portEXIT_CRITICAL(&displaySettingsMux);

  StaticJsonDocument<384> doc;
  doc["schema"] = 1;
  doc["brightnessPercent"] = snapshot.brightnessPercent;
  doc["screenOn"] = snapshot.screenOn;
  doc["version"] = snapshot.version;
  doc["pendingCloudUpdate"] = snapshot.pendingCloudUpdate;
  doc["localGeneration"] = snapshot.localGeneration;
  doc["updatedBy"] = snapshot.updatedBy;

  if (displaySettingsFileMutex != nullptr && xSemaphoreTake(displaySettingsFileMutex, pdMS_TO_TICKS(1000)) != pdTRUE)
  {
    return false;
  }
  File file = LittleFS.open(DISPLAY_SETTINGS_PATH, "w");
  if (!file)
  {
    if (displaySettingsFileMutex != nullptr) xSemaphoreGive(displaySettingsFileMutex);
    return false;
  }
  serializeJson(doc, file);
  file.close();
  if (displaySettingsFileMutex != nullptr) xSemaphoreGive(displaySettingsFileMutex);
  return true;
}

bool loadDisplaySettings()
{
  DisplaySettings loaded;
  if (LittleFS.exists(DISPLAY_SETTINGS_PATH))
  {
    if (displaySettingsFileMutex != nullptr && xSemaphoreTake(displaySettingsFileMutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
      return false;
    }
    File file = LittleFS.open(DISPLAY_SETTINGS_PATH, "r");
    if (file)
    {
      StaticJsonDocument<384> doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      if (!error)
      {
        loaded.brightnessPercent = (uint8_t)constrain((int)(doc["brightnessPercent"] | DEFAULT_BRIGHTNESS_PERCENT), 1, 100);
        loaded.screenOn = doc["screenOn"] | true;
        loaded.version = doc["version"] | 0;
        loaded.pendingCloudUpdate = doc["pendingCloudUpdate"] | false;
        loaded.localGeneration = doc["localGeneration"] | 0;
        strlcpy(loaded.updatedBy, doc["updatedBy"] | "stored", sizeof(loaded.updatedBy));
      }
    }
    if (displaySettingsFileMutex != nullptr) xSemaphoreGive(displaySettingsFileMutex);
  }

  portENTER_CRITICAL(&displaySettingsMux);
  displaySettings = loaded;
  portEXIT_CRITICAL(&displaySettingsMux);
  return true;
}

void setDisplaySettingsLocal(uint8_t brightnessPercent, bool screenOn, const char* source)
{
  portENTER_CRITICAL(&displaySettingsMux);
  displaySettings.brightnessPercent = (uint8_t)clampInt(brightnessPercent, 1, 100);
  displaySettings.screenOn = screenOn;
  displaySettings.pendingCloudUpdate = true;
  displaySettings.localGeneration++;
  strlcpy(displaySettings.updatedBy, source == nullptr ? "receiver" : source, sizeof(displaySettings.updatedBy));
  portEXIT_CRITICAL(&displaySettingsMux);

  applyBacklightFromSettings();
  persistDisplaySettings();
  scheduleCloudSnapshotAfterStateChange();
  nextCloudDisplaySyncMs = millis();
}

bool applyRemoteDisplaySettings(uint8_t brightnessPercent, bool screenOn, uint32_t version, const char* source)
{
  bool applied = false;
  portENTER_CRITICAL(&displaySettingsMux);
  if (!displaySettings.pendingCloudUpdate && version > displaySettings.version)
  {
    displaySettings.brightnessPercent = (uint8_t)clampInt(brightnessPercent, 1, 100);
    displaySettings.screenOn = screenOn;
    displaySettings.version = version;
    displaySettings.localGeneration++;
    strlcpy(displaySettings.updatedBy, source == nullptr ? "cloud" : source, sizeof(displaySettings.updatedBy));
    applied = true;
  }
  portEXIT_CRITICAL(&displaySettingsMux);

  if (applied)
  {
    applyBacklightFromSettings();
    persistDisplaySettings();
    scheduleCloudSnapshotAfterStateChange();
  }
  return applied;
}

String displaySettingsJson()
{
  DisplaySettings snapshot;
  portENTER_CRITICAL(&displaySettingsMux);
  snapshot = displaySettings;
  portEXIT_CRITICAL(&displaySettingsMux);

  StaticJsonDocument<512> doc;
  doc["ok"] = true;
  doc["brightnessPercent"] = snapshot.brightnessPercent;
  doc["effectiveBrightnessPercent"] = snapshot.screenOn ? snapshot.brightnessPercent : 0;
  doc["screenOn"] = snapshot.screenOn;
  doc["settingsVersion"] = snapshot.version;
  doc["pendingCloudUpdate"] = snapshot.pendingCloudUpdate;
  doc["updatedBy"] = snapshot.updatedBy;
  doc["touchReady"] = touchReady;
  doc["touchCalibrated"] = touchCalibration.valid;

  String output;
  serializeJson(doc, output);
  return output;
}

bool writeDisplaySettingsJson(const String& body)
{
  StaticJsonDocument<384> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error)
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return false;
  }

  int brightness = doc["brightnessPercent"] | -1;
  if (brightness < 1 || brightness > 100 || !doc["screenOn"].is<bool>())
  {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"brightnessPercent 1-100 and screenOn are required\"}");
    return false;
  }
  setDisplaySettingsLocal((uint8_t)brightness, doc["screenOn"].as<bool>(), "receiver-local");
  server.send(200, "application/json", displaySettingsJson());
  return true;
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

// Shared by drawPlantCard() (fresh JSON each full render) and cachePlantArtSlot()
// (caches the same resolved name onto the PlantArtSlot for later data-only/detail
// redraws, which no longer have the JsonObject around to re-resolve it from).
void resolvePlantName(JsonObject card, uint16_t sensorId, char* out, size_t outSize)
{
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
    snprintf(out, outSize, "Probe %u", sensorId);
  }
  else
  {
    strlcpy(out, plant, outSize);
  }
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

bool loadTouchCalibration()
{
  if (!LittleFS.exists(TOUCH_CALIBRATION_PATH))
  {
    touchCalibration.valid = false;
    return false;
  }

  File file = LittleFS.open(TOUCH_CALIBRATION_PATH, "r");
  if (!file)
  {
    return false;
  }
  StaticJsonDocument<384> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error || (doc["schema"] | 0) != 1)
  {
    touchCalibration.valid = false;
    return false;
  }

  touchCalibration.topLeftX = doc["topLeft"]["x"] | 300;
  touchCalibration.topLeftY = doc["topLeft"]["y"] | 300;
  touchCalibration.topRightX = doc["topRight"]["x"] | 3800;
  touchCalibration.topRightY = doc["topRight"]["y"] | 300;
  touchCalibration.bottomLeftX = doc["bottomLeft"]["x"] | 300;
  touchCalibration.bottomLeftY = doc["bottomLeft"]["y"] | 3800;
  int32_t determinant =
    (int32_t)(touchCalibration.topRightX - touchCalibration.topLeftX) *
      (touchCalibration.bottomLeftY - touchCalibration.topLeftY) -
    (int32_t)(touchCalibration.topRightY - touchCalibration.topLeftY) *
      (touchCalibration.bottomLeftX - touchCalibration.topLeftX);
  touchCalibration.valid = abs(determinant) > 100000;
  return touchCalibration.valid;
}

bool persistTouchCalibration()
{
  StaticJsonDocument<384> doc;
  doc["schema"] = 1;
  JsonObject topLeft = doc.createNestedObject("topLeft");
  topLeft["x"] = touchCalibration.topLeftX;
  topLeft["y"] = touchCalibration.topLeftY;
  JsonObject topRight = doc.createNestedObject("topRight");
  topRight["x"] = touchCalibration.topRightX;
  topRight["y"] = touchCalibration.topRightY;
  JsonObject bottomLeft = doc.createNestedObject("bottomLeft");
  bottomLeft["x"] = touchCalibration.bottomLeftX;
  bottomLeft["y"] = touchCalibration.bottomLeftY;

  File file = LittleFS.open(TOUCH_CALIBRATION_PATH, "w");
  if (!file)
  {
    return false;
  }
  serializeJson(doc, file);
  file.close();
  return true;
}

bool mapRawTouchToScreen(int16_t rawX, int16_t rawY, int16_t& screenX, int16_t& screenY)
{
  int64_t basisXX = touchCalibration.topRightX - touchCalibration.topLeftX;
  int64_t basisXY = touchCalibration.topRightY - touchCalibration.topLeftY;
  int64_t basisYX = touchCalibration.bottomLeftX - touchCalibration.topLeftX;
  int64_t basisYY = touchCalibration.bottomLeftY - touchCalibration.topLeftY;
  int64_t determinant = basisXX * basisYY - basisXY * basisYX;
  if (llabs(determinant) < 100000)
  {
    return false;
  }

  int64_t offsetX = rawX - touchCalibration.topLeftX;
  int64_t offsetY = rawY - touchCalibration.topLeftY;
  int64_t normalizedX = offsetX * basisYY - offsetY * basisYX;
  int64_t normalizedY = basisXX * offsetY - basisXY * offsetX;
  const int baseWidth = 480;
  const int baseHeight = 320;
  const int spanX = baseWidth - TOUCH_TARGET_MARGIN * 2;
  const int spanY = baseHeight - TOUCH_TARGET_MARGIN * 2;
  int baseX = TOUCH_TARGET_MARGIN + (int)(normalizedX * spanX / determinant);
  int baseY = TOUCH_TARGET_MARGIN + (int)(normalizedY * spanY / determinant);
  baseX = clampInt(baseX, 0, baseWidth - 1);
  baseY = clampInt(baseY, 0, baseHeight - 1);

  // Calibration is collected in display rotation 1. Convert those calibrated
  // landscape coordinates if a later ui.json selects another rotation.
  switch (currentRotation)
  {
    case 0:
      screenX = baseY;
      screenY = baseWidth - 1 - baseX;
      break;
    case 2:
      screenX = baseHeight - 1 - baseY;
      screenY = baseX;
      break;
    case 3:
      screenX = baseWidth - 1 - baseX;
      screenY = baseHeight - 1 - baseY;
      break;
    case 1:
    default:
      screenX = baseX;
      screenY = baseY;
      break;
  }
  screenX = clampInt(screenX, 0, tft.width() - 1);
  screenY = clampInt(screenY, 0, tft.height() - 1);
  return true;
}

void drawCalibrationTarget(int x, int y, uint16_t color)
{
  // A one-pixel green target was too easy to miss against the bezel at the corner.
  // Use a large bullseye and thick crosshair that remains obvious on the 4-inch panel.
  tft.fillCircle(x, y, 19, TFT_WHITE);
  tft.fillCircle(x, y, 13, color);
  tft.fillCircle(x, y, 5, TFT_BLACK);
  for (int offset = -1; offset <= 1; offset++)
  {
    tft.drawFastHLine(x - 25, y + offset, 51, TFT_WHITE);
    tft.drawFastVLine(x + offset, y - 25, 51, TFT_WHITE);
  }
}

bool captureCalibrationPoint(int targetX, int targetY, int16_t& rawX, int16_t& rawY, uint32_t timeoutMs)
{
  uint32_t releaseStart = millis();
  bool stillPressed = touchScreen.touched();
  while (stillPressed && millis() - releaseStart < 3000UL)
  {
    stillPressed = touchScreen.touched();
    delay(10);
  }
  if (stillPressed)
  {
    Serial.print("[MSV2 touch] input stuck pressed before target irq=");
    Serial.println(digitalRead(TOUCH_IRQ));
    tft.fillScreen(TFT_BLACK);
    drawTextCenter("Release the screen", tft.width() / 2, tft.height() / 2, 2, TFT_WARNING, TFT_BLACK);
    return false;
  }

  tft.fillScreen(TFT_BLACK);
  drawTextCenter("Tap the red target, then release", tft.width() / 2, 10, 2, TFT_WHITE, TFT_BLACK);
  // Complete more than one display transaction, then allow the panel to settle before the
  // input timeout starts. On the ST7796S module the first target was only becoming visible
  // when the later timeout message caused another display transaction.
  drawCalibrationTarget(targetX, targetY, TFT_DANGER);
  delay(40);
  drawCalibrationTarget(targetX, targetY, TFT_DANGER);
  tft.drawPixel(tft.width() - 1, tft.height() - 1, TFT_BLACK);
  delay(350);
  Serial.print("[MSV2 touch] waiting target x=");
  Serial.print(targetX);
  Serial.print(" y=");
  Serial.print(targetY);
  Serial.print(" irq=");
  Serial.println(digitalRead(TOUCH_IRQ));
  uint32_t waitStart = millis();
  while (!touchScreen.touched())
  {
    if (millis() - waitStart >= timeoutMs)
    {
      TS_Point timeoutPoint = touchScreen.getPoint();
      Serial.print("[MSV2 touch] target timeout irq=");
      Serial.print(digitalRead(TOUCH_IRQ));
      Serial.print(" rawX=");
      Serial.print(timeoutPoint.x);
      Serial.print(" rawY=");
      Serial.print(timeoutPoint.y);
      Serial.print(" pressure=");
      Serial.println(timeoutPoint.z);
      tft.fillScreen(TFT_BLACK);
      drawTextCenter("No touch detected", tft.width() / 2, tft.height() / 2 - 12, 2, TFT_WARNING, TFT_BLACK);
      drawTextCenter("Restart to try again", tft.width() / 2, tft.height() / 2 + 18, 1, TFT_WHITE, TFT_BLACK);
      return false;
    }
    delay(5);
  }

  delay(25);
  int32_t sumX = 0;
  int32_t sumY = 0;
  uint8_t samples = 0;
  while (touchScreen.touched() && samples < 16)
  {
    TS_Point point = touchScreen.getPoint();
    if (point.z > 0)
    {
      sumX += point.x;
      sumY += point.y;
      samples++;
    }
    delay(8);
  }
  if (samples < 4)
  {
    Serial.print("[MSV2 touch] insufficient pressure samples=");
    Serial.print(samples);
    Serial.print(" irq=");
    Serial.println(digitalRead(TOUCH_IRQ));
    tft.fillScreen(TFT_BLACK);
    drawTextCenter("Touch was too brief", tft.width() / 2, tft.height() / 2, 2, TFT_WARNING, TFT_BLACK);
    return false;
  }

  rawX = (int16_t)(sumX / samples);
  rawY = (int16_t)(sumY / samples);
  Serial.print("[MSV2 touch] captured rawX=");
  Serial.print(rawX);
  Serial.print(" rawY=");
  Serial.print(rawY);
  Serial.print(" samples=");
  Serial.println(samples);
  tft.fillScreen(TFT_BLACK);
  drawTextCenter("Lift your finger", tft.width() / 2, tft.height() / 2 - 12, 2, TFT_WHITE, TFT_BLACK);
  drawTextCenter("Next target appears after release", tft.width() / 2, tft.height() / 2 + 18, 1, TFT_MUTED, TFT_BLACK);
  releaseStart = millis();
  while (touchScreen.touched() && millis() - releaseStart < 15000UL)
  {
    delay(5);
  }
  if (touchScreen.touched())
  {
    Serial.print("[MSV2 touch] input stuck pressed after target irq=");
    Serial.println(digitalRead(TOUCH_IRQ));
    return false;
  }
  delay(80);
  return true;
}

bool runTouchCalibration(uint32_t firstPointTimeoutMs)
{
  touchScreen.setRotation(0);
  applyDisplayRotation(1, true);
  tft.fillScreen(TFT_BLACK);
  drawTextCenter("Touch calibration", tft.width() / 2, tft.height() / 2 - 18, 2, TFT_WHITE, TFT_BLACK);
  drawTextCenter("Press each crosshair, then release", tft.width() / 2, tft.height() / 2 + 12, 1, TFT_MUTED, TFT_BLACK);

  int16_t tlX, tlY, trX, trY, blX, blY, brX, brY;
  if (!captureCalibrationPoint(TOUCH_TARGET_MARGIN, TOUCH_TARGET_MARGIN, tlX, tlY, firstPointTimeoutMs) ||
      !captureCalibrationPoint(tft.width() - 1 - TOUCH_TARGET_MARGIN, TOUCH_TARGET_MARGIN, trX, trY, 20000UL) ||
      !captureCalibrationPoint(TOUCH_TARGET_MARGIN, tft.height() - 1 - TOUCH_TARGET_MARGIN, blX, blY, 20000UL) ||
      !captureCalibrationPoint(tft.width() - 1 - TOUCH_TARGET_MARGIN, tft.height() - 1 - TOUCH_TARGET_MARGIN, brX, brY, 20000UL))
  {
    delay(1200);
    return false;
  }

  TouchCalibration candidate;
  candidate.topLeftX = tlX;
  candidate.topLeftY = tlY;
  candidate.topRightX = trX;
  candidate.topRightY = trY;
  candidate.bottomLeftX = blX;
  candidate.bottomLeftY = blY;
  candidate.valid = true;
  TouchCalibration previous = touchCalibration;
  touchCalibration = candidate;

  int16_t checkX = 0;
  int16_t checkY = 0;
  bool mapped = mapRawTouchToScreen(brX, brY, checkX, checkY);
  bool accurate = mapped &&
    abs(checkX - (tft.width() - 1 - TOUCH_TARGET_MARGIN)) <= 55 &&
    abs(checkY - (tft.height() - 1 - TOUCH_TARGET_MARGIN)) <= 55;
  if (!accurate || !persistTouchCalibration())
  {
    touchCalibration = previous;
    drawTextCenter("Calibration failed - retry at boot", tft.width() / 2, tft.height() / 2, 1, TFT_DANGER, TFT_BLACK);
    delay(1200);
    return false;
  }

  drawTextCenter("Calibration saved", tft.width() / 2, tft.height() / 2, 2, TFT_ACCENT, TFT_BLACK);
  delay(700);
  return true;
}

bool pointInside(int16_t x, int16_t y, int left, int top, int width, int height)
{
  return x >= left && x < left + width && y >= top && y < top + height;
}

// A confirmed single tap (see resolvePendingDashboardTap()) either closes the currently
// expanded detail card (tapping anywhere while one is open) or, if none is open, expands
// whichever plant card the tap landed on. Mirrors the brightness overlay's "no hidden
// control activates unless dashboardControlsAllowed" guard.
void handleDashboardSingleTap(int16_t x, int16_t y)
{
#if MSV2_ENABLE_WIFI
  if (wifiSetupPortalActive || pendingRestartMs != 0)
  {
    return;
  }
#endif
  if (expandedCardSlot >= 0)
  {
    int8_t slotToCollapse = expandedCardSlot;
    expandedCardSlot = -1;
    collapsePlantCardInPlace(artSlots[slotToCollapse]);
    return;
  }
  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    if (artSlots[i].enabled && pointInside(x, y, artSlots[i].x, artSlots[i].y, artSlots[i].w, artSlots[i].h))
    {
      expandedCardSlot = i;
      expandPlantCardInPlace(artSlots[i]);
      return;
    }
  }
}

// Redraws only the live-value text and the slider track/fill/knob, not the rest of the
// modal chrome. drawBrightnessOverlay() calls this once for the initial paint;
// previewTouchBrightness() calls it on every drag sample instead of repainting the whole
// panel - a full-panel fillRoundRect on every touch poll saturated the shared
// display/touch SPI bus badly enough that the knob looked frozen (only a couple of the
// slow full redraws ever completed during a typical drag) and the whole modal appeared to
// flash on each sample.
void drawBrightnessSliderRow(uint8_t brightness, bool screenOn)
{
  const int panelX = 34;
  const int panelY = 54;
  const int panelW = tft.width() - panelX * 2;
  const int sliderX = panelX + 58;
  const int sliderY = panelY + 82;
  const int sliderW = panelW - 116;

  char value[16];
  snprintf(value, sizeof(value), "%u%%", brightness);
  drawTextRight(value, panelX + panelW - 16, panelY + 16, 2, screenOn ? TFT_ACCENT : TFT_MUTED, TFT_SURFACE);

  // Erase wide enough to cover the knob's full travel range (11px radius) at both ends,
  // but clamped so it can never paint over the +/- buttons sitting right next to the
  // track - their own edges are panelX+50 and panelX+panelW-50 (see the button
  // fillRoundRect calls below). Without the clamp, dragging to either extreme bit a
  // visible notch out of the adjacent button on every redraw.
  const int minusButtonRight = panelX + 50;
  const int plusButtonLeft = panelX + panelW - 50;
  int eraseLeft = max(sliderX - 14, minusButtonRight);
  int eraseRight = min(sliderX + sliderW + 14, plusButtonLeft);
  tft.fillRoundRect(eraseLeft, sliderY - 11, eraseRight - eraseLeft, 38, 8, TFT_SURFACE);

  tft.fillRoundRect(sliderX, sliderY, sliderW, 16, 8, TFT_SURFACE_2);
  int fillW = max(8, sliderW * brightness / 100);
  tft.fillRoundRect(sliderX, sliderY, fillW, 16, 8, screenOn ? TFT_ACCENT : TFT_MUTED);
  int knobX = sliderX + sliderW * brightness / 100;
  tft.fillCircle(knobX, sliderY + 8, 11, TFT_WHITE);
}

void drawBrightnessOverlay()
{
  uint8_t brightness;
  bool screenOn;
  portENTER_CRITICAL(&displaySettingsMux);
  brightness = displaySettings.brightnessPercent;
  screenOn = displaySettings.screenOn;
  portEXIT_CRITICAL(&displaySettingsMux);

  const int panelX = 34;
  const int panelY = 54;
  const int panelW = tft.width() - panelX * 2;
  const int panelH = tft.height() - panelY - 24;
  const int sliderY = panelY + 82;
  tft.fillRoundRect(panelX, panelY, panelW, panelH, 10, TFT_SURFACE);
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 10, TFT_SURFACE_2);
  drawTextLeft("Display brightness", panelX + 16, panelY + 14, 2, TFT_WHITE, TFT_SURFACE);

  drawBrightnessSliderRow(brightness, screenOn);

  tft.fillRoundRect(panelX + 12, sliderY - 8, 38, 34, 6, TFT_SURFACE_2);
  drawTextCenter("-", panelX + 31, sliderY, 2, TFT_WHITE, TFT_SURFACE_2);
  tft.fillRoundRect(panelX + panelW - 50, sliderY - 8, 38, 34, 6, TFT_SURFACE_2);
  drawTextCenter("+", panelX + panelW - 31, sliderY, 2, TFT_WHITE, TFT_SURFACE_2);

  // Button text is size 1 (8px glyph height) in a 34px-tall button, so the vertical
  // offset that centers it is (34 - 8) / 2 = 13, not 9 - the old value sat visibly high.
  const int buttonY = panelY + panelH - 50;
  tft.fillRoundRect(panelX + 16, buttonY, 112, 34, 6, screenOn ? TFT_WARNING : TFT_ACCENT);
  drawTextCenter(screenOn ? "Screen off" : "Screen on", panelX + 72, buttonY + 13, 1, TFT_BLACK, screenOn ? TFT_WARNING : TFT_ACCENT);
  tft.fillRoundRect(panelX + panelW - 104, buttonY, 88, 34, 6, TFT_SURFACE_2);
  drawTextCenter("Close", panelX + panelW - 60, buttonY + 13, 1, TFT_MUTED, TFT_SURFACE_2);
}

void previewTouchBrightness(int16_t x)
{
  const int panelX = 34;
  const int panelW = tft.width() - panelX * 2;
  const int sliderX = panelX + 58;
  const int sliderW = panelW - 116;
  uint8_t brightness = (uint8_t)clampInt(1 + (x - sliderX) * 99 / sliderW, 1, 100);
  portENTER_CRITICAL(&displaySettingsMux);
  displaySettings.brightnessPercent = brightness;
  displaySettings.screenOn = true;
  portEXIT_CRITICAL(&displaySettingsMux);
  applyBacklightFromSettings();
  touchGestureChangedBrightness = true;
  drawBrightnessSliderRow(brightness, true);
}

void handleTouchInput(uint32_t now)
{
  if (!touchReady || now - lastTouchPollMs < TOUCH_POLL_INTERVAL_MS)
  {
    return;
  }
  lastTouchPollMs = now;

  // T_IRQ is active-low and can be sampled without touching the shared SPI bus.
  // In polling mode touchScreen.touched() always performs an XPT2046 transaction,
  // even while the panel is idle. On affected ST7796 modules those command bytes can
  // leak into the TFT's last address window; the composited sprite renderer leaves
  // that window on the bottom art row at x + 7, which is exactly where the two red
  // pixels above "Moist" appeared on every card. Avoid all idle touch transactions,
  // but keep polling once a gesture has started so its release is still detected.
  if (!touchWasDown && digitalRead(TOUCH_IRQ) == HIGH)
  {
    return;
  }

  bool down = touchScreen.touched();
  if (down)
  {
    TS_Point point = touchScreen.getPoint();
    int16_t x = -1;
    int16_t y = -1;
    if (!mapRawTouchToScreen(point.x, point.y, x, y))
    {
      return;
    }
    lastTouchX = x;
    lastTouchY = y;

    if (!touchWasDown)
    {
      touchGestureChangedBrightness = false;
      touchWakeOnlyThisGesture = false;
      bool screenOn;
      uint8_t brightness;
      portENTER_CRITICAL(&displaySettingsMux);
      screenOn = displaySettings.screenOn;
      brightness = displaySettings.brightnessPercent;
      portEXIT_CRITICAL(&displaySettingsMux);
      if (!screenOn)
      {
        setDisplaySettingsLocal(brightness, true, "receiver-touch");
        displayDirty = true;
        lastDashboardTapMs = 0;
        touchWakeOnlyThisGesture = true;
        touchWasDown = true;
        return; // Wake-only first touch; never activate a hidden control.
      }
    }

    if (brightnessOverlayOpen)
    {
      const int panelX = 34;
      const int panelY = 54;
      const int panelW = tft.width() - panelX * 2;
      const int sliderX = panelX + 58;
      const int sliderY = panelY + 82;
      const int sliderW = panelW - 116;
      // The drag-preview hit box used to overlap the +/- buttons' own hit boxes (below,
      // and in the release handler) by ~10px on their slider-facing edge. Any touch
      // sample landing in that sliver during a normal button tap latched
      // touchGestureChangedBrightness, which makes the release handler skip button
      // detection entirely and just commit whatever extreme value that x position
      // mapped to - the button appeared to always jump brightness to 100% or 1%
      // instead of stepping by 10. Excluding the exact same button boxes used at
      // release time makes the two hit-tests mutually exclusive by construction.
      bool onMinusButton = pointInside(x, y, panelX + 4, sliderY - 14, 52, 48);
      bool onPlusButton = pointInside(x, y, panelX + panelW - 56, sliderY - 14, 52, 48);
      if (!onMinusButton && !onPlusButton && pointInside(x, y, sliderX - 12, sliderY - 20, sliderW + 24, 56))
      {
        previewTouchBrightness(x);
      }
    }
    touchWasDown = true;
    return;
  }

  if (!touchWasDown)
  {
    return;
  }
  touchWasDown = false;

  if (!brightnessOverlayOpen)
  {
    if (touchWakeOnlyThisGesture)
    {
      touchWakeOnlyThisGesture = false;
      lastDashboardTapMs = 0;
      singleTapPending = false;
      return;
    }

    bool dashboardControlsAllowed = true;
#if MSV2_ENABLE_WIFI
    dashboardControlsAllowed = !wifiSetupPortalActive && pendingRestartMs == 0;
#endif
    if (!dashboardControlsAllowed)
    {
      lastDashboardTapMs = 0;
      singleTapPending = false;
      return;
    }

    const uint32_t tapGapMs = now - lastDashboardTapMs;
    if (lastDashboardTapMs != 0 &&
        tapGapMs >= TOUCH_DOUBLE_TAP_MIN_GAP_MS &&
        tapGapMs <= TOUCH_DOUBLE_TAP_WINDOW_MS)
    {
      lastDashboardTapMs = 0;
      singleTapPending = false;
      brightnessOverlayOpen = true;
      drawBrightnessOverlay();
      return;
    }

    // A contact bounce is too close to be a deliberate second tap. Keep the
    // original timestamp so the user's real second tap can still complete it.
    if (lastDashboardTapMs == 0 || tapGapMs > TOUCH_DOUBLE_TAP_WINDOW_MS)
    {
      lastDashboardTapMs = now;
      // Resolved later in loop() by resolvePendingDashboardTap() once the double-tap
      // window passes with no second tap. Stashed here rather than re-read from
      // lastTouchX/lastTouchY at resolve time, because a new gesture starting before
      // the window elapses overwrites those before this one would be resolved.
      pendingSingleTapX = lastTouchX;
      pendingSingleTapY = lastTouchY;
      singleTapPending = true;
    }
    return;
  }

  if (touchGestureChangedBrightness)
  {
    uint8_t brightness;
    portENTER_CRITICAL(&displaySettingsMux);
    brightness = displaySettings.brightnessPercent;
    portEXIT_CRITICAL(&displaySettingsMux);
    setDisplaySettingsLocal(brightness, true, "receiver-touch");
    drawBrightnessOverlay();
    return;
  }
  const int panelX = 34;
  const int panelY = 54;
  const int panelW = tft.width() - panelX * 2;
  const int panelH = tft.height() - panelY - 24;
  const int sliderY = panelY + 82;
  const int buttonY = panelY + panelH - 50;
  uint8_t brightness;
  bool screenOn;
  portENTER_CRITICAL(&displaySettingsMux);
  brightness = displaySettings.brightnessPercent;
  screenOn = displaySettings.screenOn;
  portEXIT_CRITICAL(&displaySettingsMux);

  if (pointInside(lastTouchX, lastTouchY, panelX + 4, sliderY - 14, 52, 48))
  {
    setDisplaySettingsLocal((uint8_t)max(1, (int)brightness - 10), true, "receiver-touch");
    drawBrightnessOverlay();
  }
  else if (pointInside(lastTouchX, lastTouchY, panelX + panelW - 56, sliderY - 14, 52, 48))
  {
    setDisplaySettingsLocal((uint8_t)min(100, (int)brightness + 10), true, "receiver-touch");
    drawBrightnessOverlay();
  }
  else if (pointInside(lastTouchX, lastTouchY, panelX + 8, buttonY - 6, 128, 46))
  {
    setDisplaySettingsLocal(brightness, !screenOn, "receiver-touch");
    if (!screenOn)
    {
      drawBrightnessOverlay();
    }
  }
  else if (pointInside(lastTouchX, lastTouchY, panelX + panelW - 112, buttonY - 6, 104, 46) ||
           !pointInside(lastTouchX, lastTouchY, panelX, panelY, panelW, panelH))
  {
    brightnessOverlayOpen = false;
    lastDashboardTapMs = 0;
    displayDirty = true;
  }
}

// A tap-up that isn't completed by a second tap within TOUCH_DOUBLE_TAP_WINDOW_MS is a
// genuine single tap, not the first half of a double-tap gesture. Resolved here on a
// plain elapsed-time check (called from loop() every iteration) rather than inside
// handleTouchInput() itself, since that function only runs when the touch controller is
// actually polled - the panel can sit idle with no finger on the glass for the whole
// window.
void resolvePendingDashboardTap(uint32_t now)
{
  if (!singleTapPending || now - lastDashboardTapMs <= TOUCH_DOUBLE_TAP_WINDOW_MS)
  {
    return;
  }
  singleTapPending = false;
  handleDashboardSingleTap(pendingSingleTapX, pendingSingleTapY);
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
  // Clamped to the domain of the raw moisture value itself (uint16_t) - moisturePercent()'s
  // ((long)moisture - dry) * 100L intermediate can overflow a 32-bit long (signed-overflow
  // UB) for a pathological dry/wet value, which requires an already-authenticated admin
  // write but has no legitimate reason to be outside this range regardless.
  outDry = clampInt(moistureConfig["dry"] | card["dry"] | defaultDry, 0, 65535);
  outWet = clampInt(moistureConfig["wet"] | card["wet"] | defaultWet, 0, 65535);
  outOverdue = false;
  outPending = autoEnabled;
}

// Re-runs the auto-cal half of resolveMoistureBounds() against a cached PlantArtSlot,
// using only fields that were snapshotted from ui.json (autoEnabled/manualDry/manualWet
// - fixed until the next full renderUi() rebuild) plus the live autoCal[] state (updated
// on its own ~10-minute cadence by runCloudAutoCalPull(), running on a separate task).
//
// Without this, slot.dry/wet/overdue were only ever set once, inside cachePlantArtSlot()
// - itself only called from renderUi(). Ordinary operation almost never triggers another
// full renderUi() once cards exist (see the displayDirty=true call sites), so a background
// auto-cal update - a card newly flagged "overdue", or a shifted learned range - could sit
// applied-but-invisible for arbitrarily long, only surfacing whenever something incidental
// (e.g. closing the brightness overlay) happened to force a full re-render. Calling this
// from both the blink-cadence check and the lightweight per-card redraw keeps the fast
// path honest without needing a JSON re-parse or full screen repaint.
void refreshSlotMoistureBounds(PlantArtSlot& slot)
{
  bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  if (slot.autoEnabled && validSensor && autoCal[slot.sensorId].ready)
  {
    slot.dry = autoCal[slot.sensorId].dryBound;
    slot.wet = autoCal[slot.sensorId].wetBound;
    slot.overdue = autoCal[slot.sensorId].overdue;
    slot.pending = false;
    return;
  }
  slot.dry = slot.manualDry;
  slot.wet = slot.manualWet;
  slot.overdue = false;
  slot.pending = slot.autoEnabled;
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

// "Overdue" (see resolveMoistureBounds/refreshSlotMoistureBounds) is a pure watering-
// schedule prediction from the cloud auto-cal function - median historical interval
// between detected waterings vs. days since the last one - with zero awareness of the
// current live reading. Left to force dangerColor unconditionally, a plant that was just
// watered (moisture already back up near/at its wet bound) still blinks red until the
// server-side rise-clustering catches up and clears the flag, which can lag well behind
// the actual watering. Suppress the override once the live percent already contradicts
// it, using healthColor's own "healthy" cutoff (50) rather than a separate threshold.
uint16_t resolveMoistureColor(bool overdue, uint8_t percent, uint16_t accentColor, uint16_t warningColor, uint16_t dangerColor, uint16_t mutedColor)
{
  bool liveReadingContradictsOverdue = percent != BATTERY_UNKNOWN && percent >= 50;
  if (overdue && !liveReadingContradictsOverdue)
  {
    return dangerColor;
  }
  return healthColor(percent, accentColor, warningColor, dangerColor, mutedColor);
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

// Max row width drawPlantSpriteComposited() ever needs to buffer: the panel is 480px wide
// and a row is clamped to tft.width() below, so this always covers it with no slack needed.
const int PLANT_SPRITE_MAX_DRAW_W = 480;
// Static rather than a function-local stack array: drawPlantSpriteComposited() is only ever
// called from the single-threaded main loop (never reentrantly), so one shared buffer is
// safe and keeps a 960-byte array off the call stack.
uint16_t plantSpriteRowBuffer[PLANT_SPRITE_MAX_DRAW_W];

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

// Composites one sprite pose across the *entire* maxW x maxH card art area - not just the
// sprite's own scaled bounding box, which can be smaller than the card - and sends every row
// as one writeAddrWindow()+writePixels() burst. This replaces the old design, which called
// tft.writePixel() per pixel (a full column/row-address-set + memory-write command sequence
// each time, per Arduino_TFT::writePixelPreclipped / Arduino_ST7796::writeAddrWindow),
// relied on a separate tft.fillRect() to clear the margin around a smaller sprite, and
// tracked an old-frame/new-frame diff to decide what needed touching at all.
//
// This is now the *only* way sprite art gets drawn - both the initial full card layout and
// every animation-frame update - since a full composite is cheap enough (one burst per row)
// to just always do in full:
//   - no separate fillRect() means no visible clear-then-rebuild flash between frames;
//   - every pixel in the area is unconditionally rewritten from the current pose on every
//     call, so there's no "the old diff comparison thought this pixel already matched"
//     failure mode - a previously wrong pixel is corrected the moment this card's pose next
//     changes, instead of persisting indefinitely;
//   - command count drops from one per changed/opaque pixel to exactly one per row.
// See docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md for the investigation this closes
// out.
void drawPlantSpriteComposited(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, uint8_t frame, uint16_t backgroundColor)
{
  uint8_t scaleMul = 1;
  uint8_t scaleDiv = 1;
  int drawW = 0;
  int drawH = 0;
  int spriteStartX = 0;
  int spriteStartY = 0;
  spriteDrawGeometry(sprite, centerX, topY, maxW, maxH, scaleMul, scaleDiv, drawW, drawH, spriteStartX, spriteStartY);
  const uint8_t pose = pgm_read_byte(&sprite.sequence[frame % sprite.frameCount]);
  const uint16_t* bitmap = (const uint16_t*)pgm_read_ptr(&sprite.frames[pose]);

  const int panelW = tft.width();
  const int panelH = tft.height();
  const int areaLeft = centerX - maxW / 2;
  const int screenXMin = max(0, areaLeft);
  const int screenXMax = min(panelW, areaLeft + maxW);
  const int screenYMin = max(0, topY);
  const int screenYMax = min(panelH, topY + maxH);
  const int rowWidth = min(screenXMax - screenXMin, PLANT_SPRITE_MAX_DRAW_W);
  if (rowWidth <= 0)
  {
    return;
  }

  uint16_t* rowBuffer = plantSpriteRowBuffer;
  tft.startWrite();
  for (int screenY = screenYMin; screenY < screenYMax; screenY++)
  {
    const int localSpriteY = screenY - spriteStartY;
    const bool rowInSprite = localSpriteY >= 0 && localSpriteY < drawH;
    for (int i = 0; i < rowWidth; i++)
    {
      const int screenX = screenXMin + i;
      uint16_t color = backgroundColor;
      const int localSpriteX = screenX - spriteStartX;
      if (rowInSprite && localSpriteX >= 0 && localSpriteX < drawW)
      {
        const uint16_t c = spriteScaledPixel(sprite, bitmap, scaleMul, scaleDiv, localSpriteX, localSpriteY);
        if (c != PLANT_TRANSPARENT)
        {
          color = c;
        }
      }
      rowBuffer[i] = color;
    }
    tftBus.writeAddrWindow(screenXMin, screenY, rowWidth, 1);
    tftBus.writePixels(rowBuffer, rowWidth);
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
    drawPlantSpriteComposited(*sprite, x, y, maxW, maxH, frame, backgroundColor);
    return;
  }

  // Sprite art self-covers its whole area (see drawPlantSpriteComposited); this ASCII
  // fallback only paints glyph cells, so it still needs its own clear first.
  tft.fillRect(x - maxW / 2, y, maxW, maxH, backgroundColor);
  drawAsciiPlantArt(x, y + max(0, (maxH - 45) / 2), art, color, backgroundColor);
}

void drawPlantArtInCard(int x, int y, int w, int h, const char* art, uint16_t color, uint16_t surfaceColor, uint8_t frame)
{
  const bool compact = h < 76;
  const int artY = y + (compact ? 15 : 24);
  const int inset = compact ? 5 : 7;
  const int artW = w - inset * 2;
  const int artH = compact ? max(10, h - 29) : max(24, h - 91);

  drawPlantArt(x + w / 2, artY, artW, artH, art, color, surfaceColor, frame);
}

void drawPlantArtDiffInCard(int x, int y, int w, int h, const char* art, uint16_t color, uint16_t surfaceColor, uint8_t oldFrame, uint8_t newFrame)
{
  // Cheap early-exit: if the pose sequence held (consecutive frame ticks can legitimately
  // map to the same pose), there's nothing to redraw at all. Otherwise fall through to a
  // full composited redraw of the new pose - see drawPlantSpriteComposited for why this no
  // longer needs to diff against the old frame to decide what to touch.
  const PlantSprite* sprite = spriteForArt(art);
  if (sprite != nullptr)
  {
    const uint8_t oldPose = pgm_read_byte(&sprite->sequence[oldFrame % sprite->frameCount]);
    const uint8_t newPose = pgm_read_byte(&sprite->sequence[newFrame % sprite->frameCount]);
    if (oldPose == newPose)
    {
      return;
    }
  }

  const bool compact = h < 76;
  const int artY = y + (compact ? 15 : 24);
  const int inset = compact ? 5 : 7;
  const int artW = w - inset * 2;
  const int artH = compact ? max(10, h - 29) : max(24, h - 91);
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
  resolvePlantName(card, slot.sensorId, slot.plant, sizeof(slot.plant));
  strlcpy(slot.art, card["art"] | "sprout_pot", sizeof(slot.art));
  JsonObject moistureConfig = card["moisture"].as<JsonObject>();
  slot.autoEnabled = moistureConfig["auto"] | false;
  // Clamped for the same overflow reason as resolveMoistureBounds() above.
  slot.manualDry = clampInt(moistureConfig["dry"] | card["dry"] | 120, 0, 65535);
  slot.manualWet = clampInt(moistureConfig["wet"] | card["wet"] | 175, 0, 65535);
  refreshSlotMoistureBounds(slot);
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
  char plantName[24];
  resolvePlantName(card, sensorId, plantName, sizeof(plantName));
  const char* plant = plantName;
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
  uint16_t moistureColor = resolveMoistureColor(overdue, moisturePct, accentColor, warningColor, dangerColor, mutedColor);
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

void drawPlantCardDataOnly(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;
  const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[slot.sensorId] : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  uint8_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, slot.dry, slot.wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = resolveMoistureColor(slot.overdue, moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
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

  // Only touch a row if something about it actually needs to change. Without this, every
  // low-moisture blink tick (every 500ms, see anyLowMoistureCardVisible) repainted BOTH
  // rows in full - including the battery row, which has nothing to do with the moisture
  // alert - producing a visible flash on fields whose values hadn't moved at all.
  //
  // The moist row's *color* legitimately needs to update every blink tick (that's the
  // alert), even when the reading itself hasn't changed, so it's gated separately from the
  // reading/text/bar-width content. The fillRect clear is only needed when the content
  // could have changed width (new value or stale-state flip, e.g. "175" -> "88" or "--") -
  // skipping it on a color-only blink tick avoids the clear-then-redraw flash entirely,
  // since drawTextRight/drawBar always fully repaint their own fixed-size cells anyway.
  const bool moistureValueChanged = !slot.metricsInitialized || stale != slot.lastMoistureStale ||
    (!stale && (reading->moisture != slot.lastMoistureRaw || moisturePct != slot.lastMoisturePct));
  const bool moistureColorChanged = moistureColor != slot.lastMoistureColor;
  const bool batteryValueChanged = !slot.metricsInitialized || batteryPct != slot.lastBatteryPct;

  if (moistureValueChanged || moistureColorChanged)
  {
    if (moistureValueChanged)
    {
      // See the art-gap comment history: this clear must reach exactly up to the art
      // region's own bottom edge (same artY/artH formula as drawPlantArtInCard) and down
      // to exactly where the batt row's own clear starts, or a stray pixel in the
      // uncovered band between them can never self-heal.
      const int artBottomY = y + 24 + max(24, h - 91);
      const int moistRowBottom = y + h - 37;
      const int moistRowTop = min(y + h - 63, artBottomY);
      tft.fillRect(x + 7, moistRowTop, w - 14, max(1, moistRowBottom - moistRowTop), slot.surfaceColor);
      drawTextLeft("Moist", x + 7, y + h - 62, 1, slot.mutedColor, slot.surfaceColor);
    }
    drawTextRight(moistureText, x + w - 7, y + h - 62, 1, moistureColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 47, w - 14, 9, moisturePct, moistureColor, slot.backgroundColor);
  }

  if (batteryValueChanged)
  {
    const int moistRowBottom = y + h - 37;
    const int battRowBottom = y + h - 6;
    const int battRowTop = min(y + h - 30, moistRowBottom);
    tft.fillRect(x + 7, battRowTop, w - 14, max(1, battRowBottom - battRowTop), slot.surfaceColor);
    drawTextLeft("Batt", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
    drawTextRight(batteryText, x + w - 7, y + h - 29, 1, batteryColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 14, w - 14, 8, batteryPct, batteryColor, slot.backgroundColor);
  }

  slot.metricsInitialized = true;
  slot.lastMoistureStale = stale;
  slot.lastMoistureRaw = stale ? slot.lastMoistureRaw : reading->moisture;
  slot.lastMoisturePct = moisturePct;
  slot.lastMoistureColor = moistureColor;
  slot.lastBatteryPct = batteryPct;
}

void formatAgeText(uint32_t ms, char* out, size_t outSize)
{
  uint32_t seconds = ms / 1000UL;
  if (seconds < 60)
  {
    snprintf(out, outSize, "%lus", (unsigned long)seconds);
    return;
  }
  uint32_t minutes = seconds / 60UL;
  if (minutes < 60)
  {
    snprintf(out, outSize, "%lum", (unsigned long)minutes);
    return;
  }
  snprintf(out, outSize, "%luh", (unsigned long)(minutes / 60UL));
}

void drawDetailStatRow(int leftX, int rightX, int y, const char* label, const char* value, uint16_t labelColor, uint16_t valueColor, uint16_t backgroundColor)
{
  drawTextLeft(label, leftX, y, 1, labelColor, backgroundColor);
  drawTextRight(value, rightX, y, 1, valueColor, backgroundColor);
}

// Data-only counterpart to drawDetailStatRow() - see the display-surgical-redraw skill.
// Only clears+redraws this one row's own rect, and only when `changed` is true, so a
// periodic refresh never touches a row whose value didn't move.
void drawDetailStatRowIfChanged(bool changed, int leftX, int rightX, int y, const char* label, const char* value, uint16_t labelColor, uint16_t valueColor, uint16_t backgroundColor)
{
  if (!changed)
  {
    return;
  }
  tft.fillRect(leftX, y - 1, rightX - leftX, 10, backgroundColor);
  drawDetailStatRow(leftX, rightX, y, label, value, labelColor, valueColor, backgroundColor);
}

void computeDetailStats(PlantArtSlot& slot, PlantDetailStats& s)
{
  const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  const SensorReading* reading = validSensor ? &sensors[slot.sensorId] : nullptr;
  s.stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  s.rawMoisture = reading != nullptr ? reading->moisture : 0;
  s.moisturePct = s.stale ? BATTERY_UNKNOWN : moisturePercent(s.rawMoisture, slot.dry, slot.wet);
  s.batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  s.moistureColor = resolveMoistureColor(slot.overdue, s.moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  if (!s.stale)
  {
    s.moistureColor = applyLowMoistureBlink(s.moistureColor, slot.dangerColor, slot.mutedColor);
  }
  s.batteryColor = healthColor(s.batteryPct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  if (s.stale)
  {
    strlcpy(s.moistureText, "--", sizeof(s.moistureText));
  }
  else
  {
    snprintf(s.moistureText, sizeof(s.moistureText), "%u", s.rawMoisture);
  }
  if (s.batteryPct == BATTERY_UNKNOWN)
  {
    strlcpy(s.batteryText, "--%", sizeof(s.batteryText));
  }
  else
  {
    snprintf(s.batteryText, sizeof(s.batteryText), "%u%%", s.batteryPct);
  }

  s.hasSignal = reading != nullptr && reading->seen;
  const int rssi = s.hasSignal ? reading->rssi : 0;
  s.signalColor = !s.hasSignal ? slot.mutedColor : (rssi >= -75 ? slot.activeColor : (rssi >= -90 ? slot.warningColor : slot.dangerColor));
  s.signalLabel = !s.hasSignal ? "No data" : (rssi >= -75 ? "Strong" : (rssi >= -90 ? "Usable" : "Weak"));
  if (s.hasSignal)
  {
    snprintf(s.rssiText, sizeof(s.rssiText), "%ddBm", rssi);
  }
  else
  {
    strlcpy(s.rssiText, "--", sizeof(s.rssiText));
  }
  if (s.hasSignal)
  {
    formatAgeText(millis() - reading->lastSeenMs, s.ageText, sizeof(s.ageText));
  }
  else
  {
    strlcpy(s.ageText, "--", sizeof(s.ageText));
  }
  snprintf(s.wakeText, sizeof(s.wakeText), "%lu", reading != nullptr ? (unsigned long)reading->wake : 0UL);
  uint32_t expected = reading != nullptr ? reading->received + reading->missedTotal : 0;
  if (expected > 0)
  {
    snprintf(s.lossText, sizeof(s.lossText), "%lu%%", (unsigned long)(reading->missedTotal * 100UL / expected));
  }
  else
  {
    strlcpy(s.lossText, "--", sizeof(s.lossText));
  }
  snprintf(s.receivedText, sizeof(s.receivedText), "%lu", reading != nullptr ? (unsigned long)reading->received : 0UL);
  snprintf(s.missedText, sizeof(s.missedText), "%lu", reading != nullptr ? (unsigned long)reading->missedTotal : 0UL);
  snprintf(s.probeText, sizeof(s.probeText), "%u", slot.sensorId);
  snprintf(s.rangeText, sizeof(s.rangeText), "%d-%d", slot.dry, slot.wet);
  s.hasRaw = reading != nullptr && reading->hasRaw;
  if (s.hasRaw)
  {
    snprintf(s.rawText, sizeof(s.rawText), "%ukHz", reading->rawMoistureKHz);
  }
  else
  {
    s.rawText[0] = '\0';
  }
  s.rawCharging = reading != nullptr && reading->rawCharging;
}

void seedDetailDiagnosticCache(PlantArtSlot& slot, const PlantDetailStats& s)
{
  strlcpy(slot.lastDetailSignalLabel, s.signalLabel, sizeof(slot.lastDetailSignalLabel));
  slot.lastDetailSignalColor = s.signalColor;
  strlcpy(slot.lastDetailRssiText, s.rssiText, sizeof(slot.lastDetailRssiText));
  strlcpy(slot.lastDetailAgeText, s.ageText, sizeof(slot.lastDetailAgeText));
  strlcpy(slot.lastDetailWakeText, s.wakeText, sizeof(slot.lastDetailWakeText));
  strlcpy(slot.lastDetailLossText, s.lossText, sizeof(slot.lastDetailLossText));
  strlcpy(slot.lastDetailReceivedText, s.receivedText, sizeof(slot.lastDetailReceivedText));
  strlcpy(slot.lastDetailMissedText, s.missedText, sizeof(slot.lastDetailMissedText));
  strlcpy(slot.lastDetailRangeText, s.rangeText, sizeof(slot.lastDetailRangeText));
  strlcpy(slot.lastDetailRawText, s.rawText, sizeof(slot.lastDetailRawText));
  slot.lastDetailCharging = s.rawCharging;
}

// Seeds every cache field drawPlantCardDetailDataOnly() diffs against, so the very next
// data-only refresh has an accurate baseline instead of relying on whatever this slot
// index last held (see the moist/batt fields, shared with the normal card's own cache).
void seedDetailCache(PlantArtSlot& slot, const PlantDetailStats& s)
{
  slot.metricsInitialized = true;
  slot.lastMoistureStale = s.stale;
  slot.lastMoistureRaw = s.stale ? slot.lastMoistureRaw : s.rawMoisture;
  slot.lastMoisturePct = s.moisturePct;
  slot.lastMoistureColor = s.moistureColor;
  slot.lastBatteryPct = s.batteryPct;
  seedDetailDiagnosticCache(slot, s);
}

// Paints only the region that changes when a normal plant card flips to diagnostics:
// the former sprite band plus the small signal label at the right of the existing name
// row. The card background, border, name, moisture row, and battery row stay untouched.
void drawPlantCardDetailInfoBand(PlantArtSlot& slot, const PlantDetailStats& s)
{
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;
  const int infoY = y + 24;
  const int infoH = max(24, h - 91);
  const int infoBottom = infoY + infoH;
  const int rightX = x + w - 7;

  // This is the exact region previously owned by the animated sprite. Clearing only
  // this band removes the old pose without touching any neighboring card or metric row.
  tft.fillRect(x + 7, infoY, w - 14, infoH, slot.surfaceColor);
  drawTextRight(s.signalLabel, rightX, y + 6, 1, s.signalColor, slot.surfaceColor);

  if (w < 200)
  {
    const int rowStep = clampInt(infoH / 7, 12, 20);
    int rowY = infoY;
    drawDetailStatRow(x + 7, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    rowY += rowStep;
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, rightX, rowY, "Age", s.ageText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, rightX, rowY, "Wake", s.wakeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, rightX, rowY, "Loss", s.lossText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, rightX, rowY, "Probe", s.probeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, rightX, rowY, "Range", s.rangeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    }
  }
  else
  {
    const int colW = (w - 14) / 2;
    const int col2X = x + 7 + colW + 6;
    const int col1RightX = x + 7 + colW - 6;
    const int rowStep = clampInt(infoH / 5, 14, 30);
    int rowY = infoY + 2;
    drawDetailStatRow(x + 7, col1RightX, rowY, "Probe", s.probeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    drawDetailStatRow(col2X, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    rowY += rowStep;
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, col1RightX, rowY, "Age", s.ageText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRow(col2X, rightX, rowY, "Wake", s.wakeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, col1RightX, rowY, "Received", s.receivedText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRow(col2X, rightX, rowY, "Missed", s.missedText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, col1RightX, rowY, "Loss", s.lossText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRow(col2X, rightX, rowY, "Range", s.rangeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (s.hasRaw && rowY + 8 <= infoBottom)
    {
      drawDetailStatRow(x + 7, col1RightX, rowY, "Raw osc", s.rawText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRow(col2X, rightX, rowY, "Charging", s.rawCharging ? "Yes" : "No", slot.mutedColor, slot.textColor, slot.surfaceColor);
    }
  }
}

// Tap-to-expand detail view, full paint: replaces one plant card in place with a richer
// readout (signal, age, wake/loss counters, moisture range) similar to the phone app's
// Diagnostics tab - see handleDashboardSingleTap(). Reuses the exact art-region geometry
// drawPlantArtInCard() would have used (y + 24, height max(24, h - 91)) so the extra rows
// sit exactly where the sprite used to be, instead of inventing a separate layout system -
// the same "more room at this card size" scaling drawPlantArtInCard/drawCompactCardMetrics
// already rely on is what decides how many diagnostic rows fit here too.
//
// Paints unconditionally and is only called during a genuine full layout rebuild (see
// renderUi()). A tap transition uses expandPlantCardInPlace() instead, so it never routes
// through this whole-card/full-dashboard path - see display-surgical-redraw skill.
// The periodic refresh path (renderDashboardDataOnly()) calls drawPlantCardDetailDataOnly()
// instead, which diffs against the cache this function seeds via seedDetailCache().
void drawPlantCardDetail(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  PlantDetailStats s;
  computeDetailStats(slot, s);
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;

  tft.fillRoundRect(x, y, w, h, 5, slot.surfaceColor);
  tft.drawRoundRect(x, y, w, h, 5, s.stale ? slot.mutedColor : s.moistureColor);
  drawTextLeft(slot.plant, x + 7, y + 6, 1, slot.textColor, slot.surfaceColor);

  if (h < 76)
  {
    // Too small for extra rows - same layout as the normal compact card, plus a small
    // signal-quality dot in the corner where the name row would otherwise leave blank
    // space, so even the smallest grid size shows *something* extra on tap.
    drawCompactCardMetrics(x, y, w, h, s.moistureText, s.batteryText, s.moisturePct, s.batteryPct, s.moistureColor, s.batteryColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    tft.fillCircle(x + w - 8, y + 8, 3, s.signalColor);
    seedDetailCache(slot, s);
    return;
  }

  const int rightX = x + w - 7;
  drawPlantCardDetailInfoBand(slot, s);

  drawTextLeft("Moist", x + 7, y + h - 62, 1, slot.mutedColor, slot.surfaceColor);
  drawTextRight(s.moistureText, rightX, y + h - 62, 1, s.moistureColor, slot.surfaceColor);
  drawBar(x + 7, y + h - 47, w - 14, 9, s.moisturePct, s.moistureColor, slot.backgroundColor);

  drawTextLeft("Batt", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
  drawTextRight(s.batteryText, rightX, y + h - 29, 1, s.batteryColor, slot.surfaceColor);
  drawBar(x + 7, y + h - 14, w - 14, 8, s.batteryPct, s.batteryColor, slot.backgroundColor);

  seedDetailCache(slot, s);
}

void expandPlantCardInPlace(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  PlantDetailStats s;
  computeDetailStats(slot, s);

  if (slot.h < 76)
  {
    // Compact cards have no sprite band to replace; the diagnostic view adds only
    // this small signal dot, so that is the only region touched on expansion.
    tft.fillCircle(slot.x + slot.w - 8, slot.y + 8, 3, s.signalColor);
    seedDetailDiagnosticCache(slot, s);
    return;
  }

  drawPlantCardDetailInfoBand(slot, s);
  // Moisture/battery cache fields already describe the unchanged normal-card rows.
  // Seed only the newly painted diagnostic fields so pending sensor data still causes
  // those existing rows to update on the next data-only pass.
  seedDetailDiagnosticCache(slot, s);
}

void collapsePlantCardInPlace(PlantArtSlot& slot)
{
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;

  if (h < 76)
  {
    tft.fillCircle(x + w - 8, y + 8, 3, slot.surfaceColor);
    return;
  }

  const int infoY = y + 24;
  const int infoH = max(24, h - 91);
  const int rightX = x + w - 7;

  // Remove only the two regions diagnostics owned, then restore the sprite into its
  // original band. All normal-card chrome and metrics remain exactly as drawn.
  tft.fillRect(rightX - 54, y + 5, 54, 10, slot.surfaceColor);
  tft.fillRect(x + 7, infoY, w - 14, infoH, slot.surfaceColor);
  if (slot.artEnabled)
  {
    const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
    const SensorReading* reading = validSensor ? &sensors[slot.sensorId] : nullptr;
    const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
    drawPlantArtInCard(x, y, w, h, slot.art, stale ? slot.mutedColor : slot.activeColor, slot.surfaceColor, slot.frame);
  }
  slot.nextFrameMs = millis() + slot.frameIntervalMs;
}

// Tap-to-expand detail view, data-only: the periodic-refresh counterpart to
// drawPlantCardDetail(), called every render-gate tick while a card is expanded (see
// loop()'s `detailOpen` gate and renderDashboardDataOnly()). Never fills the card
// background or the whole info band - only the border re-strokes unconditionally
// (cheap, matches drawPlantCardDataOnly()'s own precedent), everything else is diffed
// against the cache drawPlantCardDetail() seeds via seedDetailCache() and only
// clears+redraws the one row that actually changed. See the display-surgical-redraw
// skill for why this split exists.
void drawPlantCardDetailDataOnly(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  PlantDetailStats s;
  computeDetailStats(slot, s);
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;

  tft.drawRoundRect(x, y, w, h, 5, s.stale ? slot.mutedColor : s.moistureColor);

  const bool signalChanged = strcmp(s.signalLabel, slot.lastDetailSignalLabel) != 0 || s.signalColor != slot.lastDetailSignalColor;
  const bool moistureValueChanged = !slot.metricsInitialized || s.stale != slot.lastMoistureStale ||
    (!s.stale && (s.rawMoisture != slot.lastMoistureRaw || s.moisturePct != slot.lastMoisturePct));
  const bool moistureColorChanged = s.moistureColor != slot.lastMoistureColor;
  const bool batteryValueChanged = !slot.metricsInitialized || s.batteryPct != slot.lastBatteryPct;

  if (h < 76)
  {
    if (moistureValueChanged || moistureColorChanged || batteryValueChanged)
    {
      drawCompactCardMetrics(x, y, w, h, s.moistureText, s.batteryText, s.moisturePct, s.batteryPct, s.moistureColor, s.batteryColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    }
    if (signalChanged)
    {
      tft.fillCircle(x + w - 8, y + 8, 3, s.signalColor);
    }
    seedDetailCache(slot, s);
    return;
  }

  const int infoY = y + 24;
  const int infoH = max(24, h - 91);
  const int infoBottom = infoY + infoH;
  const int rightX = x + w - 7;

  if (signalChanged)
  {
    tft.fillRect(rightX - 54, y + 5, 54, 10, slot.surfaceColor);
    drawTextRight(s.signalLabel, rightX, y + 6, 1, s.signalColor, slot.surfaceColor);
  }

  const bool rssiChanged = strcmp(s.rssiText, slot.lastDetailRssiText) != 0;
  const bool ageChanged = strcmp(s.ageText, slot.lastDetailAgeText) != 0;
  const bool wakeChanged = strcmp(s.wakeText, slot.lastDetailWakeText) != 0;
  const bool lossChanged = strcmp(s.lossText, slot.lastDetailLossText) != 0;
  const bool receivedChanged = strcmp(s.receivedText, slot.lastDetailReceivedText) != 0;
  const bool missedChanged = strcmp(s.missedText, slot.lastDetailMissedText) != 0;
  const bool rangeChanged = strcmp(s.rangeText, slot.lastDetailRangeText) != 0;
  const bool rawChanged = strcmp(s.rawText, slot.lastDetailRawText) != 0 || s.rawCharging != slot.lastDetailCharging;

  if (w < 200)
  {
    const int rowStep = clampInt(infoH / 7, 12, 20);
    int rowY = infoY;
    drawDetailStatRowIfChanged(rssiChanged, x + 7, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    rowY += rowStep;
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(ageChanged, x + 7, rightX, rowY, "Age", s.ageText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(wakeChanged, x + 7, rightX, rowY, "Wake", s.wakeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(lossChanged, x + 7, rightX, rowY, "Loss", s.lossText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      // Probe # never changes for a live slot without a full config reload, which
      // always re-enters through drawPlantCardDetail() (full paint) anyway.
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(rangeChanged, x + 7, rightX, rowY, "Range", s.rangeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    }
  }
  else
  {
    const int colW = (w - 14) / 2;
    const int col2X = x + 7 + colW + 6;
    const int col1RightX = x + 7 + colW - 6;
    const int rowStep = clampInt(infoH / 5, 14, 30);
    int rowY = infoY + 2;
    drawDetailStatRowIfChanged(rssiChanged, col2X, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, slot.textColor, slot.surfaceColor);
    rowY += rowStep;
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(ageChanged, x + 7, col1RightX, rowY, "Age", s.ageText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRowIfChanged(wakeChanged, col2X, rightX, rowY, "Wake", s.wakeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(receivedChanged, x + 7, col1RightX, rowY, "Received", s.receivedText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRowIfChanged(missedChanged, col2X, rightX, rowY, "Missed", s.missedText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(lossChanged, x + 7, col1RightX, rowY, "Loss", s.lossText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRowIfChanged(rangeChanged, col2X, rightX, rowY, "Range", s.rangeText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (s.hasRaw && rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(rawChanged, x + 7, col1RightX, rowY, "Raw osc", s.rawText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRowIfChanged(rawChanged, col2X, rightX, rowY, "Charging", s.rawCharging ? "Yes" : "No", slot.mutedColor, slot.textColor, slot.surfaceColor);
    }
  }

  // Moist/Batt rows: identical diff-and-redraw idiom (and the same cache fields) as
  // drawPlantCardDataOnly() - see that function's comments for why the clear itself is
  // also conditional, not just the text/bar content.
  if (moistureValueChanged || moistureColorChanged)
  {
    if (moistureValueChanged)
    {
      const int artBottomY = y + 24 + max(24, h - 91);
      const int moistRowBottom = y + h - 37;
      const int moistRowTop = min(y + h - 63, artBottomY);
      tft.fillRect(x + 7, moistRowTop, w - 14, max(1, moistRowBottom - moistRowTop), slot.surfaceColor);
      drawTextLeft("Moist", x + 7, y + h - 62, 1, slot.mutedColor, slot.surfaceColor);
    }
    drawTextRight(s.moistureText, rightX, y + h - 62, 1, s.moistureColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 47, w - 14, 9, s.moisturePct, s.moistureColor, slot.backgroundColor);
  }

  if (batteryValueChanged)
  {
    const int moistRowBottom = y + h - 37;
    const int battRowBottom = y + h - 6;
    const int battRowTop = min(y + h - 30, moistRowBottom);
    tft.fillRect(x + 7, battRowTop, w - 14, max(1, battRowBottom - battRowTop), slot.surfaceColor);
    drawTextLeft("Batt", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
    drawTextRight(s.batteryText, rightX, y + h - 29, 1, s.batteryColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 14, w - 14, 8, s.batteryPct, s.batteryColor, slot.backgroundColor);
  }

  seedDetailCache(slot, s);
}

// Shared by anyLowMoistureCardVisible() (does ANY card need the fast blink cadence) and
// renderDashboardDataOnly(true) (which cards does that redraw actually need to touch).
bool slotNeedsBlink(PlantArtSlot& slot)
{
  if (!slot.enabled || slot.sensorId == 0 || slot.sensorId > MAX_SENSOR_ID)
  {
    return false;
  }
  SensorReading& reading = sensors[slot.sensorId];
  bool stale = !reading.seen || (millis() - reading.lastSeenMs > staleSeconds * 1000UL);
  if (stale)
  {
    return false;
  }
  refreshSlotMoistureBounds(slot);
  uint8_t percent = moisturePercent(reading.moisture, slot.dry, slot.wet);
  if (slot.overdue && percent < 50)
  {
    return true;
  }
  return percent < 25;
}

// blinkOnly redraws only the card(s) driving the 500ms blink cadence, instead of every
// visible card. Without this, one low-moisture card forced a full-board redraw twice a
// second - every healthy card's border/bars got repainted over SPI even though nothing
// about them had changed, which is both wasted shared-bus traffic (see DISPLAY_SPI_HZ)
// and the "random parts of the display update every cycle" a user would actually notice.
// Real sensor updates (sensorDataDirty) still redraw everything, since a single dirty
// flag doesn't track which specific card's data changed.
void renderDashboardDataOnly(bool blinkOnly)
{
#if !MSV2_ENABLE_DISPLAY
  return;
#endif

  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    if (!artSlots[i].enabled)
    {
      continue;
    }
    if (i == expandedCardSlot)
    {
      drawPlantCardDetailDataOnly(artSlots[i]);
      continue;
    }
    if (blinkOnly && !slotNeedsBlink(artSlots[i]))
    {
      continue;
    }
    drawPlantCardDataOnly(artSlots[i]);
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
    if (slotNeedsBlink(artSlots[i]))
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
  drawTextLeft("Connecting to home WiFi...", 16, 48, 1, TFT_ACCENT, TFT_BLACK);
  drawTextLeft("Network", 16, 84, 1, TFT_MUTED, TFT_BLACK);
  drawTextLeft(ssid == nullptr || ssid[0] == '\0' ? "(none)" : ssid, 92, 84, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("Source", 16, 106, 1, TFT_MUTED, TFT_BLACK);
  drawTextLeft(source == nullptr ? "saved setup" : source, 92, 106, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("If this takes too long, setup help will appear here.", 16, 146, 1, TFT_MUTED, TFT_BLACK);
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
  drawTextLeft(reason == nullptr || reason[0] == '\0' ? "Ready for first-time setup" : reason, 16, 43, 1, TFT_WARNING, TFT_BLACK);

  drawSetupStep(78, "1", "On your phone, open WiFi settings.", TFT_WHITE, TFT_ACCENT, TFT_BLACK);
  drawSetupStep(110, "2", "Join network: MSV2-Display", TFT_WHITE, TFT_ACCENT, TFT_BLACK);
  drawSetupStep(142, "3", "No password needed", TFT_WHITE, TFT_ACCENT, TFT_BLACK);
  drawSetupStep(174, "4", "Choose home WiFi or local-only mode.", TFT_WHITE, TFT_ACCENT, TFT_BLACK);

  drawTextLeft("If no page appears, open Safari/Chrome and type:", 16, 216, 1, TFT_MUTED, TFT_BLACK);
  drawTextLeft("192.168.4.1", 16, 238, 2, TFT_ACCENT, TFT_BLACK);
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
  drawTextLeft("Settings Saved", 16, 16, 2, TFT_ACCENT, TFT_BLACK);
  drawTextLeft("The receiver is restarting now.", 16, 54, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("After restart, this screen will show your plant dashboard.", 16, 86, 1, TFT_MUTED, TFT_BLACK);
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
  drawTextLeft("WiFi Retry", 16, 16, 2, TFT_WARNING, TFT_BLACK);
  drawTextLeft("Could not reach your home WiFi yet.", 16, 54, 1, TFT_WHITE, TFT_BLACK);
  drawTextLeft("Network", 16, 88, 1, TFT_MUTED, TFT_BLACK);
  drawTextLeft(ssid == nullptr || ssid[0] == '\0' ? "(saved network)" : ssid, 92, 88, 1, TFT_WHITE, TFT_BLACK);
  char retryText[32];
  snprintf(retryText, sizeof(retryText), "Retry %u of %u", attempt, maxAttempts);
  drawTextLeft(retryText, 16, 122, 1, TFT_ACCENT, TFT_BLACK);
  drawTextLeft("Restarting and trying again...", 16, 154, 1, TFT_MUTED, TFT_BLACK);
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
    drawTextLeft("ui.json parse error", 8, 8, 1, TFT_DANGER, TFT_BLACK);
    return;
  }

  setCheckpoint("render: colors");
  JsonObject theme = doc["theme"];
  uint16_t backgroundColor = parseColor(theme["background"] | "#101820", TFT_BLACK);
  uint16_t surfaceColor = parseColor(theme["surface"] | "#17212B", TFT_SURFACE);
  uint16_t textColor = parseColor(theme["text"] | "#F2F5F7", TFT_WHITE);
  uint16_t mutedColor = parseColor(theme["muted"] | "#8FA3AD", TFT_MUTED);
  uint16_t accentColor = parseColor(theme["accent"] | "#20C997", TFT_ACCENT);
  uint16_t warningColor = parseColor(theme["warning"] | "#FFB020", TFT_WARNING);
  uint16_t dangerColor = parseColor(theme["danger"] | "#FF5A5F", TFT_DANGER);
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
    if (wifiStationDesired && !wifiStationConnected)
    {
      strlcpy(appAddress, "WiFi reconnecting", sizeof(appAddress));
    }
    else
    {
      String ipText = currentIpAddress().toString();
      snprintf(appAddress, sizeof(appAddress), "App %s", ipText.c_str());
    }
    drawTextRight(appAddress, tft.width() - 8, 12, 1, mutedColor, backgroundColor);
  }
#endif
  setCheckpoint("render: cards draw");
  JsonArray cards = doc["cards"].as<JsonArray>();
  artSlotCount = 0;
  if (cards.isNull() || expandedCardSlot >= (int)cards.size())
  {
    // A reconfigure (fewer cards, or none) can leave a stale index pointing at a slot
    // that either no longer exists or now belongs to a different card - drop it rather
    // than have an unrelated card appear "expanded" the next time the count grows back.
    expandedCardSlot = -1;
  }
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
      cachePlantArtSlot(x, y, cardW, cardH, card, showCardArt, backgroundColor, surfaceColor, textColor, accentColor, mutedColor, warningColor, dangerColor);
      if (index == expandedCardSlot)
      {
        drawPlantCardDetail(artSlots[index]);
      }
      else
      {
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
      }
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
    if (!slot.enabled || i == expandedCardSlot)
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

    // Raw, uncalibrated values as transmitted by 2026-07-28-rework probes - see the
    // SensorReading.hasRaw comment. Only present for probes that actually sent them;
    // omitted (not just zeroed) for probes still on pre-rework legacy firmware so this
    // field's presence itself signals which protocol a given sensor is on.
    if (sensors[sensorId].hasRaw)
    {
      JsonObject rawItem = item.createNestedObject("raw");
      rawItem["moistureKHz"] = sensors[sensorId].rawMoistureKHz;
      rawItem["batteryMv"] = sensors[sensorId].rawBatteryMv;
      rawItem["charging"] = sensors[sensorId].rawCharging;
    }

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

  DisplaySettings reportedDisplay;
  portENTER_CRITICAL(&displaySettingsMux);
  reportedDisplay = displaySettings;
  portEXIT_CRITICAL(&displaySettingsMux);
  JsonObject display = doc.createNestedObject("display");
  display["brightnessPercent"] = reportedDisplay.brightnessPercent;
  display["effectiveBrightnessPercent"] = reportedDisplay.screenOn ? reportedDisplay.brightnessPercent : 0;
  display["screenOn"] = reportedDisplay.screenOn;
  display["settingsVersion"] = reportedDisplay.version;
  display["pendingCloudUpdate"] = reportedDisplay.pendingCloudUpdate;
  display["updatedBy"] = reportedDisplay.updatedBy;
  display["touchReady"] = touchReady;
  display["touchCalibrated"] = touchCalibration.valid;

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
  scheduleCloudSnapshotAfterStateChange();
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

  wifiStationConnected = hasUsableStationConnection();
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

  wifiStationConnected = hasUsableStationConnection();
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

void setCloudDisplayLastError(const char* error)
{
  strlcpy(cloudDisplayLastError, error == nullptr ? "" : error, sizeof(cloudDisplayLastError));
}

void runCloudDisplaySettings(uint32_t now)
{
  if (!cloudEnabled || cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0')
  {
    nextCloudDisplaySyncMs = 0;
    return;
  }
  if (nextCloudDisplaySyncMs == 0)
  {
    nextCloudDisplaySyncMs = now + 5000UL;
    return;
  }
  if ((int32_t)(now - nextCloudDisplaySyncMs) < 0)
  {
    return;
  }

  wifiStationConnected = hasUsableStationConnection();
  if (!wifiStationConnected)
  {
    setCloudDisplayLastError("waiting for station WiFi");
    nextCloudDisplaySyncMs = now + 10000UL;
    return;
  }

  DisplaySettings outgoing;
  portENTER_CRITICAL(&displaySettingsMux);
  outgoing = displaySettings;
  portEXIT_CRITICAL(&displaySettingsMux);
  const bool sendingUpdate = outgoing.pendingCloudUpdate && cloudDisplayApiReady;

  String url = cloudUploadUrl;
  url += url.indexOf('?') >= 0 ? "&" : "?";
  url += "display=1&receiverId=";
  url += receiverId;
  if (!sendingUpdate)
  {
    url += "&currentVersion=";
    url += String(outgoing.version);
  }

  const bool isHttps = strncmp(cloudUploadUrl, "https://", 8) == 0;
  const bool isHttp = strncmp(cloudUploadUrl, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    setCloudDisplayLastError("uploadUrl must start with http:// or https://");
    nextCloudDisplaySyncMs = now + CLOUD_DISPLAY_PULL_INTERVAL_MS;
    return;
  }

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(MSV2_TRUSTED_ROOT_CA_PEM);
    began = http.begin(secureClient, url);
  }
  else
  {
    began = http.begin(plainClient, url);
  }
  cloudDisplayLastAttemptMs = now;
  cloudDisplayLastHttpStatus = 0;
  if (!began)
  {
    setCloudDisplayLastError("could not start HTTP client");
    nextCloudDisplaySyncMs = now + CLOUD_DISPLAY_PULL_INTERVAL_MS;
    return;
  }

  http.setTimeout(8000);
  String auth = "Bearer ";
  auth += cloudApiToken;
  http.addHeader("Authorization", auth);
  int status;
  if (sendingUpdate)
  {
    StaticJsonDocument<384> requestDoc;
    requestDoc["receiverId"] = receiverId;
    requestDoc["brightnessPercent"] = outgoing.brightnessPercent;
    requestDoc["screenOn"] = outgoing.screenOn;
    requestDoc["baseVersion"] = outgoing.version;
    String requestBody;
    serializeJson(requestDoc, requestBody);
    http.addHeader("Content-Type", "application/json");
    status = http.POST(requestBody);
  }
  else
  {
    status = http.GET();
  }
  cloudDisplayLastHttpStatus = status;

  if (status >= 200 && status < 300)
  {
    String response = http.getString();
    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    JsonObject settings = responseDoc["settings"].as<JsonObject>();
    if (error || !(responseDoc["ok"] | false))
    {
      setCloudDisplayLastError("invalid display settings response");
    }
    else if ((responseDoc["displaySettingsApi"] | 0) != 1)
    {
      // Safe staged rollout: older receiver-snapshot code ignores display=1
      // and would treat a POST as a normal snapshot. Never send a settings
      // POST until a GET explicitly advertises the v1 display protocol.
      cloudDisplayApiReady = false;
      setCloudDisplayLastError("backend display API upgrade required");
    }
    else if (settings.isNull())
    {
      cloudDisplayApiReady = true;
      setCloudDisplayLastError("");
      cloudDisplayLastSuccessMs = millis();
    }
    else
    {
      uint8_t brightness = (uint8_t)constrain((int)(settings["brightnessPercent"] | DEFAULT_BRIGHTNESS_PERCENT), 1, 100);
      bool screenOn = settings["screenOn"] | true;
      uint32_t version = settings["settingsVersion"] | 0;
      const char* updatedBy = settings["updatedBy"] | "cloud";
      cloudDisplayApiReady = true;
      if (sendingUpdate)
      {
        bool stillCurrent;
        portENTER_CRITICAL(&displaySettingsMux);
        displaySettings.version = max(displaySettings.version, version);
        stillCurrent = displaySettings.localGeneration == outgoing.localGeneration &&
          displaySettings.brightnessPercent == outgoing.brightnessPercent &&
          displaySettings.screenOn == outgoing.screenOn;
        if (stillCurrent)
        {
          displaySettings.pendingCloudUpdate = false;
          strlcpy(displaySettings.updatedBy, updatedBy, sizeof(displaySettings.updatedBy));
        }
        portEXIT_CRITICAL(&displaySettingsMux);
        persistDisplaySettings();
      }
      else if (!outgoing.pendingCloudUpdate)
      {
        applyRemoteDisplaySettings(brightness, screenOn, version, updatedBy);
      }
      setCloudDisplayLastError("");
      cloudDisplayLastSuccessMs = millis();
      Serial.print("[MSV2 cloud] display settings v");
      Serial.println(version);
    }
  }
  else if (status > 0)
  {
    String error = "HTTP ";
    error += status;
    setCloudDisplayLastError(error.c_str());
  }
  else
  {
    String error = http.errorToString(status);
    setCloudDisplayLastError(error.c_str());
  }

  http.end();
  bool stillPending;
  portENTER_CRITICAL(&displaySettingsMux);
  stillPending = displaySettings.pendingCloudUpdate;
  portEXIT_CRITICAL(&displaySettingsMux);
  nextCloudDisplaySyncMs = millis() +
    (stillPending && cloudDisplayApiReady ? 1000UL :
      (cloudDisplayApiReady ? CLOUD_DISPLAY_PULL_INTERVAL_MS : CLOUD_BOOTSTRAP_RETRY_MS));
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

  wifiStationConnected = hasUsableStationConnection();
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

  wifiStationConnected = hasUsableStationConnection();
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
    0,   // sensor 2: Alocasia, legacy pre-calibrated packet - never reaches this raw path
    3300, // sensor 3: AFE bench-test board (Capacitive Soil Moisture Sensor v1.2,
          // MSV2_Moisture_AFE_Test.ino) - raw is millivolts, not kHz. Provisional ceiling
          // (ESP32 ADC's practical max) - tighten once a real dry-air baseline is logged.
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
    bool hasRaw = len >= MSV2_RAW_PACKET_MIN_LEN;
    uint16_t rawMoistureKHz = 0;
    uint16_t rawBatteryMv = 0;
    bool rawCharging = false;
    if (hasRaw)
    {
      rawMoistureKHz = buffer[10] | (buffer[11] << 8);
      rawBatteryMv = buffer[12] | (buffer[13] << 8);
      rawCharging = (buffer[14] & 0x01) != 0;
      moisture = calibrateRawMoisture(sensorId, rawMoistureKHz);
      batteryPercent = calibrateRawBattery(sensorId, (float)rawBatteryMv, rawCharging);
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
    reading.hasRaw = hasRaw;
    reading.rawMoistureKHz = rawMoistureKHz;
    reading.rawBatteryMv = rawBatteryMv;
    reading.rawCharging = rawCharging;
    reading.rssi = advertisedDevice.getRSSI();
    reading.lastSeenMs = millis();
    sensorDataDirty = true;

    // 2026-07-28: temporary debug print for live erratic-battery-reading investigation -
    // remove once resolved (see chat log / scindapsus_probe memory for context).
    Serial.print("[MSV2 debug] sensor ");
    Serial.print(sensorId);
    Serial.print(" wake ");
    Serial.print(wake);
    Serial.print(" hasRaw=");
    Serial.print(hasRaw);
    Serial.print(" rawBatteryMv=");
    Serial.print(rawBatteryMv);
    Serial.print(" rawCharging=");
    Serial.print(rawCharging);
    Serial.print(" batteryPercent=");
    Serial.print(batteryPercent);
    Serial.print(" rawMoistureKHz=");
    Serial.print(rawMoistureKHz);
    Serial.print(" moisture=");
    Serial.println(moisture);

    // Do not schedule cloud work from packet reception. The periodic uploader will include
    // this fresh value at the configured cloudSyncIntervalSeconds cadence; accelerating it
    // here previously turned frequent advertisements into a TLS request every ~15-35s.
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
// Runs the five cloud network calls (bootstrap, display settings, config pull,
// auto-cal pull, snapshot
// upload) on their own task instead of inline in loop(), mirroring bleScanTask above -
// the same pattern that already fixed BLE scanning contention with rendering/WebServer.
// Each of these does a blocking HTTPClient/WiFiClientSecure call (fresh TLS handshake
// every time, no session reuse) capped at an 8s timeout, but observed taking ~1.6-2.1s
// even on success. Running them inline in loop() meant the shared render/WebServer loop
// was blocked for that long on every one of these calls - the config pull alone fires
// every ~30s (CLOUD_CONFIG_PULL_INTERVAL_MS), which was frequent enough on its own to
// read as "periodic freezes" even with a perfectly calm probe (see 2026-07-26 chat log).
// Pinned to core 0, same as bleScanTask, leaving core 1 free for rendering/WebServer.
// bleScanTask runs one priority level higher, preventing this task's blocking TLS work from
// delaying scan-window scheduling. Packet reception no longer accelerates snapshot uploads.
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
      runCloudDisplaySettings(now);
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

  server.on("/display", HTTP_GET, []() {
    Serial.println("[MSV2 web] GET /display");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", displaySettingsJson());
  });

  server.on("/display", HTTP_POST, []() {
    Serial.println("[MSV2 web] POST /display");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeDisplaySettingsJson(server.arg("plain"));
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
  if (wifiStationDesired && !wifiSetupPortalActive)
  {
    return "reconnecting";
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

bool hasUsableStationConnection()
{
#if MSV2_ENABLE_WIFI
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }
  IPAddress ip = WiFi.localIP();
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
#else
  return false;
#endif
}

void scheduleCloudCatchUpAfterWifiRecovery(uint32_t now)
{
  // Do not wait for the old periodic deadlines after an outage. Upload the latest
  // sensor snapshot immediately, then stagger the less urgent pulls so reconnecting
  // does not launch every TLS handshake in one burst.
  if (strlen(MSV2_CLOUD_BOOTSTRAP_URL) > 0 && (cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0'))
  {
    nextCloudBootstrapMs = now;
  }
  if (cloudEnabled)
  {
    nextCloudSyncMs = now;
    nextCloudDisplaySyncMs = now + 1000UL;
    nextCloudConfigPullMs = now + 2000UL;
    nextCloudAutoCalPullMs = now + 3000UL;
  }
}

uint32_t wifiReconnectDelayMs(uint32_t attempt)
{
  // 5s, 10s, 20s, 40s, then one attempt per minute until the AP returns.
  uint8_t shift = attempt > 4 ? 4 : (attempt == 0 ? 0 : (uint8_t)attempt - 1);
  uint32_t delayMs = WIFI_RECONNECT_MIN_DELAY_MS << shift;
  return delayMs > WIFI_RECONNECT_MAX_DELAY_MS ? WIFI_RECONNECT_MAX_DELAY_MS : delayMs;
}

void maintainWifiConnection(uint32_t now)
{
#if MSV2_ENABLE_WIFI
  if (!wifiStationDesired || wifiSetupPortalActive || wifiLocalOnly || activeWifiSsid[0] == '\0')
  {
    return;
  }
  if ((int32_t)(now - wifiLastHealthCheckMs) < (int32_t)WIFI_HEALTH_CHECK_INTERVAL_MS)
  {
    return;
  }
  wifiLastHealthCheckMs = now;

  if (hasUsableStationConnection())
  {
    if (!wifiStationConnected || wifiDisconnectedSinceMs != 0)
    {
      wifiStationConnected = true;
      wifiLastConnectedMs = now;
      wifiDisconnectedSinceMs = 0;
      wifiReconnectAttempts = 0;
      nextWifiReconnectMs = 0;
      Serial.print("[MSV2 wifi] connection restored: ");
      Serial.println(WiFi.localIP());
      if (serverPtr != nullptr)
      {
        // Rebind the local app listener too; a full radio reset invalidates any
        // sockets that were open on the old station interface.
        server.begin();
      }
      scheduleCloudCatchUpAfterWifiRecovery(now);
      displayDirty = true;
      sensorDataDirty = true;
    }
    return;
  }

  wifiStationConnected = false;
  if (wifiDisconnectedSinceMs == 0)
  {
    wifiDisconnectedSinceMs = now;
    wifiDisconnectCount++;
    nextWifiReconnectMs = now;
    Serial.print("[MSV2 wifi] connection lost; status=");
    Serial.println((int)WiFi.status());
    displayDirty = true;
  }
  if ((int32_t)(now - nextWifiReconnectMs) < 0)
  {
    return;
  }

  wifiReconnectAttempts++;
  wifiReconnectAttemptTotal++;
  wifiLastReconnectAttemptMs = now;
  const bool resetRadio = wifiReconnectAttempts % WIFI_RECONNECT_RADIO_RESET_EVERY == 0;
  Serial.print("[MSV2 wifi] reconnect attempt ");
  Serial.print(wifiReconnectAttempts);
  Serial.println(resetRadio ? " (radio reset)" : "");

  if (resetRadio)
  {
    // A normal reconnect is enough for router/channel interruptions. Periodically
    // rebuild the station radio as well so a wedged DHCP/driver state cannot leave
    // the receiver offline forever.
    // Deliberately no WiFi.setSleep(false) here (or in the station connect below).
    // It was added on 2026-08-13 to make reconnects feel snappier and instead starved
    // the BLE scan task of radio airtime: BLE and WiFi share one antenna on the ESP32,
    // and the coexistence arbiter hands BLE its slices out of WiFi's modem-sleep
    // windows. Disabling that sleep produced exactly "good RSSI, huge miss%" on live
    // probes - fixed 2026-08-14, confirmed on hardware. Leave modem sleep at its
    // default (on) unless a *measured* reconnect-latency problem justifies revisiting
    // this, and if so, re-test BLE loss% on real probes before shipping it.
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(activeWifiSsid, activeWifiPassword);
  }
  else if (!WiFi.reconnect())
  {
    WiFi.begin(activeWifiSsid, activeWifiPassword);
  }

  nextWifiReconnectMs = now + wifiReconnectDelayMs(wifiReconnectAttempts);
#endif
}

void setupWifi()
{
#if MSV2_ENABLE_WIFI
  wifiStationConnected = false;
  wifiStationDesired = false;
  wifiSetupPortalActive = false;
  activeWifiSsid[0] = '\0';
  activeWifiPassword[0] = '\0';
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
    wifiStationDesired = true;
    strlcpy(activeWifiSsid, stationSsid, sizeof(activeWifiSsid));
    strlcpy(activeWifiPassword, stationPassword, sizeof(activeWifiPassword));
    bootLog("connecting WiFi STA");
    Serial.print("[MSV2 wifi] SSID: ");
    Serial.println(stationSsid);
    Serial.print("[MSV2 wifi] credential source: ");
    Serial.println(storedWifiLoaded ? "LittleFS" : "firmware");
    renderWifiConnectingScreen(stationSsid, storedWifiLoaded ? "saved from phone app" : "firmware default");

    // No WiFi.setSleep(false) here - see the matching comment in the radio-reset
    // branch of maintainWifiConnection() for why that starves BLE reception.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(activeWifiSsid, activeWifiPassword);

    uint32_t startMs = millis();
    while (!hasUsableStationConnection() && millis() - startMs < 15000)
    {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (hasUsableStationConnection())
    {
      wifiStationConnected = true;
      wifiLastConnectedMs = millis();
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
    wifiStationDesired = false;
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
  displaySettingsFileMutex = xSemaphoreCreateMutex();

  bootLog("mounting LittleFS");
  if (!LittleFS.begin(true))
  {
    bootLog("LittleFS mount failed");
  }
  ensureDefaultUi();
  ensureUiMetadata();
  loadDeviceIdentity();
  loadCloudConfig();
  loadDisplaySettings();
  loadTouchCalibration();
  bootLog("LittleFS ready");

#if MSV2_ENABLE_DISPLAY
  bootLog("initializing backlight PWM (GPIO11)");
  if (!ledcAttach(TFT_BACKLIGHT, BACKLIGHT_PWM_FREQUENCY_HZ, BACKLIGHT_PWM_RESOLUTION_BITS))
  {
    bootLog("backlight PWM attach failed");
  }
  applyBacklightFromSettings();

  // Both SPI devices have 10K CS pull-ups on the PCB. Drive them high before
  // initializing the shared bus as an additional guard against contention.
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_IRQ, INPUT_PULLUP);

  bootLog("initializing display");
  // Arduino_HWSPI deliberately shares one SPIClass with XPT2046_Touchscreen.
  // The ST7796S is write-only at runtime, so the shared peripheral's input is
  // routed to T_MISO/GPIO8; wired TFT_MISO/GPIO10 remains available but unused.
  busPtr = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TOUCH_MISO, &displaySpi, true);
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
  if (!tft.begin(DISPLAY_SPI_HZ))
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

  bootLog("initializing XPT2046 touch");
  touchReady = touchScreen.begin(displaySpi);
  touchScreen.setRotation(0); // Persisted affine calibration handles display rotation.
  Serial.print("[MSV2 touch] mode=polling cs=GPIO");
  Serial.print(TOUCH_CS);
  Serial.print(" irq=GPIO");
  Serial.print(TOUCH_IRQ);
  Serial.print(" irqIdle=");
  Serial.println(digitalRead(TOUCH_IRQ));
  if (!touchReady)
  {
    bootLog("XPT2046 touch init failed");
  }
  else
  {
    bool recalibrateHeld = false;
    if (touchScreen.touched())
    {
      uint32_t holdStart = millis();
      while (touchScreen.touched() && millis() - holdStart < 1200UL)
      {
        delay(10);
      }
      recalibrateHeld = millis() - holdStart >= 1000UL;
    }
    if (!touchCalibration.valid || recalibrateHeld)
    {
      bootLog(recalibrateHeld ? "touch held at boot; starting calibration" : "touch calibration missing; waiting for first target");
      // Calibration must remain visible even if screen-off was persisted before
      // this reboot. This preview does not alter the stored on/off setting.
      ledcWrite(TFT_BACKLIGHT, brightnessPercentToDuty(max((int)DEFAULT_BRIGHTNESS_PERCENT, (int)displaySettings.brightnessPercent)));
      runTouchCalibration(30000UL);
      applyBacklightFromSettings();
    }
    bootLog(touchCalibration.valid ? "touch ready/calibrated" : "touch ready with fallback calibration");
  }
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
  if (xTaskCreatePinnedToCore(cloudSyncTask, "msv2_cloud_sync", 16384, nullptr, CLOUD_SYNC_TASK_PRIORITY, &cloudSyncTaskHandle, 0) == pdPASS)
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
    if (xTaskCreatePinnedToCore(bleScanTask, "msv2_ble_scan", 8192, nullptr, BLE_SCAN_TASK_PRIORITY, &bleScanTaskHandle, 0) == pdPASS)
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
  maintainWifiConnection(now);
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
  handleTouchInput(now);
  resolvePendingDashboardTap(now);
  if (brightnessOverlayOpen)
  {
    // Keep the modal stable while a finger is dragging. Sensor data remains
    // cached and triggers its normal redraw as soon as the overlay closes.
    delay(1);
    return;
  }
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
  bool detailOpen = expandedCardSlot >= 0;
  uint32_t renderIntervalMs = blinkNeeded ? 500 : SENSOR_RENDER_INTERVAL_MS;
  if ((sensorDataDirty || blinkNeeded || detailOpen) && now - lastSensorRenderMs >= renderIntervalMs)
  {
    bool realDataChanged = sensorDataDirty;
    sensorDataDirty = false;
    lastSensorRenderMs = now;
    if (artSlotCount > 0)
    {
      renderDashboardDataOnly(!realDataChanged);
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
