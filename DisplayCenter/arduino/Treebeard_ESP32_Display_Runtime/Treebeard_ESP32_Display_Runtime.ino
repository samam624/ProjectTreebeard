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
#define TREEBEARD_BRINGUP_STAGE 3

#define TREEBEARD_SAFE_BOOT (TREEBEARD_BRINGUP_STAGE == 0)
#define TREEBEARD_DISPLAY_TEST (TREEBEARD_BRINGUP_STAGE == 1)
#define TREEBEARD_ENABLE_DISPLAY (TREEBEARD_BRINGUP_STAGE >= 1)
#define TREEBEARD_ENABLE_WIFI (TREEBEARD_BRINGUP_STAGE >= 2)
#define TREEBEARD_ENABLE_BLE (TREEBEARD_BRINGUP_STAGE >= 3)
#define TREEBEARD_SAFE_BOOT_LED_PIN 2
#define TREEBEARD_SAFE_BOOT_BLINK_MS 500
// 2026-09-02: build-tag prefix changed MSV2 -> Treebeard with the project rename. Given its
// own date/suffix rather than reusing the 2026-08-31 one, so the two prefixes never describe
// the same tag - every historical record at or before 2026-08-31 legitimately reads
// "MSV2 runtime build ..." because that is what those binaries actually reported.
#define TREEBEARD_BUILD_TAG "Treebeard runtime build 2026-10-11 water-now-region-v2-msv4-262166"

#ifndef TREEBEARD_FORCE_WIFI_SETUP
#define TREEBEARD_FORCE_WIFI_SETUP 0
#endif

#include <Arduino.h>
#include "esp_heap_caps.h"
#include <math.h>

#if TREEBEARD_ENABLE_WIFI
#include <FS.h>
using fs::FS;
using fs::File;
#endif

#if TREEBEARD_ENABLE_DISPLAY
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "PlantSpriteCatalog.h"
#endif

#if TREEBEARD_SAFE_BOOT || TREEBEARD_DISPLAY_TEST

void setup()
{
  pinMode(TREEBEARD_SAFE_BOOT_LED_PIN, OUTPUT);
  digitalWrite(TREEBEARD_SAFE_BOOT_LED_PIN, LOW);

  Serial.begin(115200);
  uint32_t startMs = millis();
  while (millis() - startMs < 5000)
  {
    Serial.println("[Treebeard safe boot] waiting for serial");
    digitalWrite(TREEBEARD_SAFE_BOOT_LED_PIN, !digitalRead(TREEBEARD_SAFE_BOOT_LED_PIN));
    delay(250);
  }

  Serial.println();
  Serial.println("[Treebeard safe boot] setup reached");
#if TREEBEARD_DISPLAY_TEST
  // Backlight is gated by Q2 (2N7002) on GPIO11, non-inverted -- see
  // Treebeard_ESP32_Backlight_Test. Arduino_GFX doesn't drive this pin itself,
  // so it has to be switched on explicitly or the panel stays dark even
  // though SPI init/draw succeeds.
  Serial.println("[Treebeard display test] enabling 75% backlight PWM (GPIO11)");
  if (!ledcAttach(11, 1000, 10))
  {
    Serial.println("[Treebeard display test] backlight PWM attach failed");
  }
  ledcWrite(11, 575); // 75%^2 perceived-brightness curve, 10-bit duty

  Serial.println("[Treebeard display test] initializing Arduino_GFX");
  Arduino_DataBus* testBus = new Arduino_ESP32SPI(14, 16, 12, 13, -1);
  Arduino_GFX* testGfx = new Arduino_ST7796(testBus, 15, 1, false, 320, 480);
  if (testBus == nullptr || testGfx == nullptr)
  {
    Serial.println("[Treebeard display test] allocation failed");
    return;
  }
  Serial.println("[Treebeard display test] allocation complete");

  Serial.println("[Treebeard display test] calling begin()");
  testGfx->begin();
  Serial.println("[Treebeard display test] begin() complete");

  Serial.println("[Treebeard display test] drawing");
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
  Serial.println("[Treebeard display test] init/draw complete");
#else
  Serial.println("[Treebeard safe boot] no display, WiFi, BLE, LittleFS, or ArduinoJson loaded");
#endif
  Serial.println("[Treebeard safe boot] GPIO 2 heartbeat active if your board has that LED");
  Serial.flush();
}

void loop()
{
  static uint32_t lastPrintMs = 0;
  static uint32_t lastBlinkMs = 0;
  static bool blinkState = false;
  uint32_t now = millis();

  if (now - lastBlinkMs >= TREEBEARD_SAFE_BOOT_BLINK_MS)
  {
    lastBlinkMs = now;
    blinkState = !blinkState;
    digitalWrite(TREEBEARD_SAFE_BOOT_LED_PIN, blinkState ? HIGH : LOW);
  }

  if (now - lastPrintMs >= 1000)
  {
    lastPrintMs = now;
    Serial.print("[Treebeard safe boot] alive ms=");
    Serial.println(now);
  }
}

#else

#include <ArduinoJson.h>
#if TREEBEARD_ENABLE_BLE
#include <BLEDevice.h>
#include <BLEScan.h>
#endif
#include <esp_system.h>
#include <string.h>
#if TREEBEARD_ENABLE_WIFI
#include <LittleFS.h>
// 2026-08-31: for the postErrno diagnostic in runCloudSnapshotUploader() - lwIP sets errno on
// the failing socket call even though NetworkClientSecure collapses its return to -1.
#include <errno.h>
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
static const char TREEBEARD_TRUSTED_ROOT_CA_PEM[] PROGMEM = R"PEM(
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

#ifndef TREEBEARD_CLOUD_BOOTSTRAP_URL
#define TREEBEARD_CLOUD_BOOTSTRAP_URL "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/receiver-bootstrap"
#endif

#ifndef TREEBEARD_HOSTED_APP_URL
#define TREEBEARD_HOSTED_APP_URL "https://msv2-plants.netlify.app/"
#endif

const char UI_PATH[] = "/ui.json";
const char WIFI_CONFIG_PATH[] = "/wifi.json";
const char FORCE_WIFI_SETUP_UNLOCK_PATH[] = "/wifi-setup-unlock-v2.flag";
const char WIFI_RETRY_STATE_PATH[] = "/wifi-retry.json";
const char DEVICE_CONFIG_PATH[] = "/device.json";
const char CLOUD_CONFIG_PATH[] = "/cloud.json";
const char DISPLAY_SETTINGS_PATH[] = "/display.json";
const char TOUCH_CALIBRATION_PATH[] = "/touch-calibration.json";
const char SOUND_SETTINGS_PATH[] = "/sound.json";
const char AUTOCAL_CACHE_PATH[] = "/autocal-cache-v1.json";
const char AUTOCAL_CACHE_TEMP_PATH[] = "/autocal-cache-v1.tmp";
const char TEMPERATURE_COMPENSATION_CACHE_PATH[] = "/temp-comp-v3.json";
const char TEMPERATURE_COMPENSATION_CACHE_TEMP_PATH[] = "/temp-comp-v3.tmp";
const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint8_t MSV2_MAGIC[] = { 'M', 'S', 'V', '2' };
const uint8_t MAX_SENSOR_ID = 16;

// ---- Bench probes: MSV3 bring-up firmware, deliberately a DIFFERENT magic ----
//
// MSV3_Probe_Bringup_Test.ino advertises "MSV3" instead of "MSV2" so that a production receiver
// drops its packets before it ever looks at sensorId. That is a SAFETY PROPERTY, not an
// oversight: a bench probe sitting in air next to a running receiver must never be able to
// contribute a fabricated reading to a real plant's history or to the cloud.
//
// The Bench page opts back in to seeing those packets, and it does so through a SEPARATE parse
// path (parseBenchAdvertisement) writing a SEPARATE array (benchProbes). That path must never
// touch sensors[], the history staging buffer, the durable outbox, or the cloud snapshot. If a
// future change makes a bench packet reach any of those, the safety property above is gone.
const uint8_t MSV3_BENCH_MAGIC[] = { 'M', 'S', 'V', '3' };
// Bench firmware before the 2026-09-21 widening sent 17 bytes and no format version. Both
// lengths are accepted so an older bring-up image still appears on the page, with the v1-only
// fields (mux code, FDC status) shown as unavailable rather than decoded from absent bytes.
const size_t MSV3_BENCH_PACKET_MIN_LEN = 17;
const size_t MSV3_BENCH_PACKET_V1_LEN = 23;
// Byte [19] in the bench layout. Production sends the low byte of an always-zero legacy field at
// this offset, so any nonzero value here is unambiguous evidence of versioned bench firmware.
// See the bench packet ABI comment above bleStartAdv() in MSV3_Probe_Bringup_Test.ino -- keyed
// on this version exactly so a reused byte can never be decoded against the wrong build, which
// is the mistake CLAUDE.md records being made four times during the DFU work.
const uint8_t MSV3_BENCH_FORMAT_VERSION_1 = 1;
#define BENCH_ADV_FLAG_CHARGING        (1U << 0)
#define BENCH_ADV_FLAG_BOARD_TEMP_OK   (1U << 2)
#define BENCH_ADV_FLAG_OTA_POWER_OK    (1U << 4)
#define BENCH_ADV_FLAG_READING_INVALID (1U << 5)
#define BENCH_ADV_FLAG_LIVE_MODE       (1U << 7)
// Five fits the page's list area at a readable row height. A bench rarely has more than two or
// three probes on it at once; the slot table evicts the stalest entry rather than growing.
const uint8_t BENCH_SLOT_COUNT = 5;
const uint32_t BENCH_STALE_MS = 15000;

// ---- Bench command beacon: receiver -> probe, connectionless ----
//
// ABI shared with benchCommandScanCallback() in MSV3_Probe_Bringup_Test.ino; that function's
// comment is the authoritative description, including why the direction is receiver-advertises /
// probe-scans rather than the other way round, and the explicit statement that this channel is
// unauthenticated and therefore carries nothing but a light.
//
// "TBCM" is a third magic, distinct from both "MSV2" and "MSV3", so a command can never be
// parsed as sensor data nor sensor data as a command.
const uint8_t BENCH_CMD_MAGIC[] = { 'T', 'B', 'C', 'M' };
const uint8_t BENCH_CMD_TARGET_ALL = 0xFF;
const uint8_t BENCH_CMD_OP_IDENTIFY = 0x01;
const size_t BENCH_CMD_PACKET_LEN = 14;
// How long one command stays on air. Long enough that a probe scanning at 50% duty cannot miss
// it, short enough that the radio is back to plain scanning almost immediately.
const uint32_t BENCH_CMD_BROADCAST_MS = 2500;
uint32_t benchCommandUntilMs = 0;
uint32_t benchCommandCounter = 0;
bool benchCommandAdvertising = false;
// Randomised once, lazily, on the first command after boot. The probe compares the counter only
// within one session, because the counter alone restarts at 1 on every receiver boot and would
// then collide with the last value a probe already acted on -- measured 2026-09-21, and it made
// the first identify after a receiver reflash silently do nothing. See
// benchCommandScanCallback()'s comment in MSV3_Probe_Bringup_Test.ino.
uint16_t benchCommandSessionId = 0;

// MSV3 receiver-side temperature compensation. The probe continues to transmit only raw
// observables; this model lives here so raw history is never replaced and the correction can
// be revised/audited without reflashing every probe. It estimates log(Cconnected/Cbare), removes
// the part that covaries with a 2-hour effective board temperature, and retains a 48-hour
// moisture baseline.
//
// Model v3 (2026-09-14) fixes a defect that made v2 actively harmful on some probes. v2 seeded
// every probe with a POSITIVE gain and then rejected any fit that came back negative. Probe 8's
// real coefficient is negative, so all four of its 24-hour fits were rejected, its gain stayed
// pinned at the seed forever, and it applied a wrong-signed correction that made its daily swing
// 2.5x WORSE than no compensation at all. Measured against 10 days of real history:
//
//   probe 8 daily peak-to-peak   uncorrected 0.907%   v2 2.289%   v3 0.701%
//   probe 4 daily peak-to-peak   uncorrected 10.385%  v2 9.334%   v3 9.422%
//
// The three changes are: seed the gain at zero so an unlearned probe corrects nothing rather
// than guessing; accept negative gains, because the sign is a physical property that differs
// between probes; and adopt the first accepted fit outright instead of blending it 3:1 against
// a seed that carries no information. See
// MSV3/docs/MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md for why the sign varies:
// the thermal term is a difference of competing effects whose balance depends on soil wetness.
const uint8_t TEMPERATURE_COMPENSATION_MODEL_VERSION = 3;
const float TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C = 22.0f;
const float TEMPERATURE_COMPENSATION_PROBE_4_REFERENCE_C = 22.34f;
const float TEMPERATURE_COMPENSATION_TEMP_TAU_SECONDS = 2.0f * 60.0f * 60.0f;
const float TEMPERATURE_COMPENSATION_MOISTURE_TAU_SECONDS = 48.0f * 60.0f * 60.0f;
// Zero, deliberately. A probe that has not yet earned a fit must not apply a correction whose
// sign it cannot know. The gain is a per-probe physical property, not a fleet constant.
const float TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C = 0.0f;
const float TEMPERATURE_COMPENSATION_PROBE_4_SEED_GAIN_PER_C = 0.0f;
// Symmetric bound. v2 used 0.0f as the lower limit, which silently discarded every correctly
// fitted negative gain instead of applying it.
const float TEMPERATURE_COMPENSATION_MIN_GAIN_PER_C = -0.04f;
const float TEMPERATURE_COMPENSATION_SEED_PHASE_OFFSET = 0.0000496f;
const uint32_t TEMPERATURE_COMPENSATION_FIT_BLOCK_MS = 24UL * 60UL * 60UL * 1000UL;
const uint32_t TEMPERATURE_COMPENSATION_PERSIST_INTERVAL_MS = 15UL * 60UL * 1000UL;
const uint16_t TEMPERATURE_COMPENSATION_MIN_FIT_SAMPLES = 120;
const float TEMPERATURE_COMPENSATION_MIN_TEMP_SPAN_C = 2.0f;
const float TEMPERATURE_COMPENSATION_MIN_FIT_R2 = 0.40f;
const uint8_t TEMPERATURE_COMPENSATION_REQUIRED_FITS = 2;
// Consecutive-sample jumps above these bounds are handling/configuration changes, not an
// ordinary watering. Probe 4's measured watering step was below 1%; its cup-immersion and
// insertion/removal steps were many percent. Resetting at the latter prevents an EMA or fit
// from blending two physically incompatible epochs.
const float TEMPERATURE_COMPENSATION_MAX_CONNECTED_STEP_FRACTION = 0.03f;
const float TEMPERATURE_COMPENSATION_MAX_BARE_STEP_FRACTION = 0.0035f;

enum TemperatureCompensationMode : uint8_t
{
  TEMPERATURE_COMPENSATION_OFF = 0,
  TEMPERATURE_COMPENSATION_LEARNING = 1,
  TEMPERATURE_COMPENSATION_ACTIVE = 2,
};
// 2026-07-28: probes were reworked to send raw, uncalibrated readings (raw oscillator kHz,
// raw battery mV, raw charging flag) instead of interpreting them on-device - see
// MSV2_Moisture_BLE_Sleep.ino and calibrateRawMoisture()/calibrateRawBattery() below. That
// widened the packet from 13 to 15 bytes, which doubles as a format-version check: probes
// still running pre-rework firmware send the shorter legacy packet with already-calibrated
// values, handled as a fallback in Msv2AdvertisedDeviceCallbacks::onResult().
const size_t MSV2_RAW_PACKET_MIN_LEN = 15;
// 2026-08-22: MSV3 probes (arduino/MSV3_Moisture_BLE_Sleep/) send a real
// sensor - the FDC2214, a 28-bit LC-tank frequency counter - instead of
// MSV2's RC-oscillator kHz count, so the raw field at byte 10 is widened
// from 2 bytes to 4 (real values on the bench are ~18-22 million, nowhere
// near fitting in uint16_t). Same magic/company-id/sensorId/wake layout as
// the narrow raw packet (bytes 0-9 identical), just a wider raw-value field
// and battery mV/charging shifted two bytes later - see
// Msv2AdvertisedDeviceCallbacks::onResult() below, which MUST check this
// (wider) length threshold before MSV2_RAW_PACKET_MIN_LEN, since a wide
// packet also satisfies the narrower >= check and would otherwise be
// misparsed with the old field offsets.
const size_t MSV2_RAW_PACKET_WIDE_LEN = 17;
// 2026-08-25: MSV3_Moisture_BLE_Sleep.ino's software-only board-temperature/bare-tank-reference
// addition widens the wide packet again, 17 -> 23 bytes (byte 16 gains phase/validity flag bits,
// bytes 17-18 are signed board temp in quarter-degrees C, bytes 19-22 are the bare-tank FDC
// count). The 2026-09-14 capless policy keeps that length, zeros/invalidates the retired bare
// field, and uses flag bit6 to select the capless scale. See the probe firmware's wire-format
// comment above startAdv() for the exact layout. A
// packet is wideRaw at length>=17 same as before (first 17 bytes are unchanged, so an older
// receiver parses those fields identically); it additionally carries these diagnostics only at
// length>=23. Named "diagnostic" rather than folded into wideRaw's own meaning because a shorter
// wide packet (an MSV3 probe running older firmware) must keep working exactly as today with no
// diagnostics, not be misparsed or rejected.
const size_t MSV3_DIAGNOSTIC_PACKET_LEN = 23;
// Version the receiver-derived numeric scale written to history. Scale 1 covers the
// existing legacy/MSV2 mappings. Scale 2 is MSV3 with the original 270pF C17 tank capacitor.
// Scale 3 is the 2026-09-14 capless/no-bare-reference MSV3 policy. The probe marks scale 3 with
// flag bit6, allowing old and reworked probes to coexist during rollout. The cloud uses a version
// change to start a fresh calibration epoch without deleting older measurements or mixing
// incompatible numeric ranges.
const uint8_t MSV2_MOISTURE_SCALE_VERSION = 1;
const uint8_t MSV3_MOISTURE_SCALE_VERSION = 2;
const uint8_t MSV3_CAPLESS_MOISTURE_SCALE_VERSION = 3;
// 2026-10-06: MSV4 probes (MSV4/arduino/MSV4_Moisture_BLE_Sleep) keep the 23-byte MSV3 layout and
// set flag bit7. The sensor fields then mean something else entirely: bytes 10-13 are the fitted
// soil resistance in 0.1 ohm (not an FDC count), bytes 17-18 are the blade-tip NTC temperature
// (not die temperature), and bytes 19-22 hold fit diagnostics (not a bare-tank count).
// They travel through the SAME SensorReading/PendingHistoryReading fields as their MSV3
// counterparts so the durable history record and the cloud columns do not change; scale 4 is
// what tells every consumer which meaning applies. See that sketch's layout comment above
// startAdv() -- the packet is an ABI with this parser.
const uint8_t MSV4_MOISTURE_SCALE_VERSION = 4;
const uint8_t MSV4_ADV_FLAG = 0x80;
// The probe saturates Rsoil here (400 kohm): with its 1 kohm source resistor it cannot tell
// larger values apart, and in air the fit runs off to 1e14 ohm. Below 1 ohm is the fit collapsing
// with nothing to conduct, which the probe already marks invalid.
const uint32_t MSV4_RSOIL_DECIOHM_MIN = 10UL;
const uint32_t MSV4_RSOIL_DECIOHM_MAX = 4000000UL;
// Soil conductivity rises about 2 %/C, the standard EC correction. The probe sends Rsoil
// uncorrected with the tip temperature beside it, so the correction lives here with every other
// calibration.
const float MSV4_EC_TEMPCO_PER_C = 0.02f;
const float MSV4_EC_REFERENCE_C = 25.0f;
// Presentation units per decade of resistance; see calibrateMsv4Moisture().
const float MSV4_MOISTURE_UNITS_PER_DECADE = 10000.0f;
// The BQ25504 stops charging this cell at VBAT_OV and switches the load off at VBAT_OK falling
// (nominal resistor values, MSV4/docs/MSV4_PROBE_BOM_2026-09-16.md), so an MSV4 battery only ever
// moves through that slice of the LiPo curve.
const float MSV4_BATTERY_EMPTY_MV = 3369.0f;
const float MSV4_BATTERY_FULL_MV = 3889.0f;
// MSV4 application version 262150 grew the packet from 23 to 26 bytes: bytes 23-24 are the
// nRF52840 die temperature (quarter-degrees C, the board end of the probe, not the soil) and
// byte 25 is a second flags byte whose bit0 marks that temperature valid. The 23-byte packet
// from 262148 stays valid; the tail is optional.
const size_t MSV4_EXTENDED_PACKET_LEN = 26;
const uint8_t MSV4_ADV_FLAG2_DIE_TEMP = 0x01;
// History keeps that die temperature in the record's rawMoistureKHz slot, which an MSV4 row
// otherwise leaves empty, offset so 0 still means "absent" as it does for every use of the slot.
// It reaches the cloud's raw_moisture_khz column as plain quarter-degrees.
const int16_t MSV4_DIE_TEMP_HISTORY_OFFSET = 1000;
// MSV4 application version 262152 ("layout B", flags2 bit1). Rsoil never needs more than 22 bits
// (it saturates at 4,000,000), so the top byte of the 32-bit field now carries the fit residual,
// and the two bytes that used to hold Cp and the residual hold Cd, the direct plate capacitance,
// in 0.01 pF (0 = not sent). Cd is the one fitted quantity that is still a live measurement with
// the blade in air, where Rsoil is only its ceiling and Cm is zero. Cp left the packet; it is a
// per-boot constant and the probe publishes it in its DIS hardware revision string.
//
// Whatever the packet's layout, an MSV4 reading is STORED in one form: rawFdcCounts is the
// resistance alone, rawBareTankFdcCounts is Cm | Cd << 16, and the residual travels beside them
// (msv4FitRmsE4; in the history record, the top byte of rawFdcCounts). A layout A packet is
// converted on arrival, so history never has to know which firmware sent a row.
const uint8_t MSV4_ADV_FLAG2_LAYOUT_B = 0x02;
const uint32_t MSV4_RSOIL_MASK = 0x00FFFFFFUL;
// Keep a passive scan session alive long enough that advertisements from several probes do
// not repeatedly collide with stop/clear/restart gaps. Results are still cleared between
// sessions, bounding NimBLE's discovered-device storage, but the allocator churn now happens
// twice per minute instead of roughly eighteen times per minute.
const uint32_t BLE_SCAN_SECONDS = 30;
const uint32_t BLE_SCAN_IDLE_MS = 50;
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
// 2026-08-30: self-healing backstop for the tap-to-expand diagnostic view. Confirmed live
// (real hardware, real photo): a touch-triggered draw can occasionally leave a stuck, partial
// repaint (observed: only the "Age" stat visible, rest of the card otherwise normal) that
// persists indefinitely - by design, the data-only redraw path never revisits pixels it
// believes are already correct, so a one-off corrupted write never self-corrects. Confirmed
// user-side that tapping the card again (which forces collapsePlantCardInPlace's full clear)
// fixes it immediately and the corruption itself is intermittent, not a state-machine bug -
// consistent with a transient SPI-bus timing issue on hardware with a documented history of
// touch/display bus contention, not a logic error in expandedCardSlot tracking. This doesn't
// fix the root SPI-timing cause (see the 2026-08-30 touch-gesture-redesign comments for the
// mitigation already applied there); it bounds the damage: while a card is expanded, a full
// (not data-only) redraw of the detail view fires periodically regardless of whether anything
// is known to have changed, so any stuck corruption clears itself within this window instead
// of lasting until someone happens to tap it.
const uint32_t DETAIL_FULL_RESYNC_MS = 15000;
const uint32_t CLOUD_BOOTSTRAP_FIRST_TRY_MS = 15000;
const uint32_t CLOUD_BOOTSTRAP_RETRY_MS = 300000;
const uint32_t CLOUD_SYNC_DEFAULT_SECONDS = 60;
// The production default before the 2026-08-23 latency fix. loadCloudConfig() always
// prefers a stored value over the compiled default, so lowering CLOUD_SYNC_DEFAULT_SECONDS
// alone would never reach a receiver that already persisted 300 to /cloud.json - see the
// one-time migration in loadCloudConfig().
const uint32_t CLOUD_SYNC_LEGACY_DEFAULT_SECONDS = 300;
const uint32_t CLOUD_SYNC_SOON_MS = 5000;
// BLE callbacks only touch this RAM ingress ring. A checksum-protected RTC mirror preserves
// it across receiver crashes/resets; cloudSyncTask coalesces many observations into one
// LittleFS transaction instead of synchronously programming flash for every probe wake.
// Sixty-four slots cover well over two minutes at the measured five-sensor ingress rate.
const size_t HISTORY_STAGING_CAPACITY = 64;
const size_t HISTORY_JOURNAL_WRITE_BATCH_SIZE = 32;
// Healthy uploads read directly from the RTC-backed ring, so normal operation performs no
// journal writes at all. These are outage-only spillover limits: commit before the ring fills
// or after two minutes without successful acknowledgement. This removes the measured 29-240ms
// periodic flash pause while still retaining a large, power-safe LittleFS backlog in an outage.
const size_t HISTORY_JOURNAL_FLUSH_RECORDS = 56;
const size_t HISTORY_JOURNAL_EMERGENCY_RECORDS = 60;
const uint32_t HISTORY_JOURNAL_MAX_STAGING_MS = 120000;
const uint32_t HISTORY_FLASH_DISPLAY_QUIET_MS = 100;
const size_t HISTORY_QUEUE_CAPACITY = 8192;
// The backend accepts at most 128 rows. Ninety-six drains the measured ~24-row/minute bench
// load with 4x headroom while retaining stack margin in the 16KB cloud task. Compact uploads
// are streamed a row at a time after TLS connects, so this no longer creates a 30-40KB String.
const size_t HISTORY_UPLOAD_BATCH_SIZE = 96;
// Worst-case MSV3 row now includes the separate temperature-compensated shadow channel.
// The streaming encoder reuses one 480-byte allocation for every row.
const size_t HISTORY_JSON_BYTES_PER_ROW = 480;
const char HISTORY_JOURNAL_PATH[] = "/history-outbox-v1.bin";
const char HISTORY_JOURNAL_COMPACT_PATH[] = "/history-outbox-v1.compact";
const char HISTORY_STATE_A_PATH[] = "/history-outbox-v1-state-a.bin";
const char HISTORY_STATE_B_PATH[] = "/history-outbox-v1-state-b.bin";
const uint32_t HISTORY_JOURNAL_COMPACT_HEAD_BYTES = 65536;
// Deliberately not 30s: config/display pulls also run every 30s, and matching retry
// periods phase-locked a failed history connection directly behind another TLS call.
const uint32_t HISTORY_UPLOAD_RETRY_MS = 17000;
// Absolute floor between the start of one snapshot-upload TLS connection and the next,
// regardless of any backlog nudge below. This intentionally matches the minimum allowed
// by the syncIntervalSeconds clamp so a freshly-queued reading can be nudged forward but
// can never recreate the packet-triggered ~15-35s cadence measured to cause BLE/cloud
// task contention on shared core 0 (see docs/BLE_CLOUD_CONTENTION_FIX_2026-08-11.md).
const uint32_t CLOUD_MIN_UPLOAD_SPACING_MS = 60000;
const uint32_t CLOUD_FULL_SNAPSHOT_INTERVAL_MS = 600000;
// Every cloud operation uses the same TLS client and radio. Space auxiliary pulls away from
// one another so a timer collision cannot launch five handshakes in one cloud-task tick.
const uint32_t CLOUD_NETWORK_MIN_GAP_MS = 15000;
// 2026-08-23 hardening: live /device telemetry caught each failed snapshot-upload attempt
// costing real, unrecovered heap (observed 11-44KB per attempt, before/after readings taken
// around the actual POST call) rather than failing cleanly - a known ESP32 WiFiClientSecure/
// mbedTLS behavior when a TLS connection is attempted but never completes its handshake.
// Below this floor, skip the attempt entirely rather than risk losing more ground - the
// queue is untouched either way, only delayed.
//
// First deployed at 40960 (40KB); a live soak then showed "connection refused" failures at
// freeHeapNow values from 26KB up through 91KB, while successes were ALSO observed across
// that same 26-90KB range (e.g. one success at 90384 with largestFreeBlockBytes=31732,
// immediately followed by a failure at 91436 with an essentially identical largestBlock of
// 31732) - total free heap and largest-free-block do NOT cleanly separate success from
// failure here, so this is very likely not primarily a heap problem, despite the real
// per-attempt heap cost measured separately. Raising this blindly to "be safer" (tried:
// 122880) instead blocked 100% of attempts, because normal steady-state free heap on this
// receiver apparently never climbs that high - worse than the failures it was meant to
// prevent. Left at the original conservative floor pending real evidence of what actually
// distinguishes success from failure (see the sibling-function cleanup fix below for the
// current best lead: TCP/TLS session leakage from OTHER cloud calls, not this one's heap).
//
// 2026-08-26: this guard was only ever checked in runCloudSnapshotUploader(). Live /device
// telemetry caught a real crash (resetReason "panic/exception") with minFreeHeapEverBytes
// down to 7448 bytes on a build that already had the cloud-cleanup-v3 fix - cloudSyncTask()
// calls up to five blocking HTTPClient/WiFiClientSecure functions back-to-back in the same
// 1-second tick whenever their periodic timers land together (config pull and display pull
// share the same 30s interval, so this is routine, not rare). Once the guarded snapshot
// upload passes, the four unguarded calls after it (bootstrap, display settings, config
// pull's 12KB DynamicJsonDocument, auto-cal pull) could still each open a fresh TLS session
// and push heap the rest of the way to empty with nothing left to stop them. Now checked at
// the top of all five, so a low reading during the cascade halts the REST of that tick too,
// not just the first call.
const uint32_t CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES = 40960;
const uint32_t CLOUD_LOW_HEAP_RETRY_MS = 15000;
// Exponential backoff on CONSECUTIVE upload failures (any cause, not just low-heap skips) -
// the fixed 17s retry alone let five-plus failures in a row hammer a connection that was
// already failing, each one compounding the same heap pressure that caused it. Resets to 0
// on the next success. Capped well below cloudSyncIntervalSeconds's own ceiling so a bad
// patch backs off but still eventually retries on its own.
const uint32_t CLOUD_UPLOAD_BACKOFF_MAX_MS = 180000;
// 2026-08-29: relaxed from 30000 (both) to cut Supabase Edge Function invocations - at 30s
// these two polls alone were ~5,760/day (~173,000/month) from a single receiver, ~38% of the
// free-tier 500,000/month invocation quota. A cloud-pushed config/display change now takes up
// to ~2 minutes to reach the receiver instead of ~30s - confirmed acceptable for how this
// feature is actually used (see chat log). If a future receiver relies on near-real-time
// cloud-pushed display updates, lower this back down rather than removing the tradeoff note.
const uint32_t CLOUD_CONFIG_PULL_INTERVAL_MS = 120000;
const uint32_t CLOUD_DISPLAY_PULL_INTERVAL_MS = 120000;
const uint32_t CLOUD_DISPLAY_FAILURE_BACKOFF_MAX_MS = 300000;
const uint32_t CLOUD_AUTOCAL_PULL_INTERVAL_MS = 600000; // 10 min - learned range/schedule changes slowly
const uint32_t CLOUD_AUTOCAL_RETRY_MS = 30000;
// Cloud HTTP/TLS and BLE scanning intentionally stay off the render/WebServer core.
// BLE gets the higher application-task priority so a due cloud request cannot delay the
// next scan window. The ESP32 BLE host/controller tasks retain their own higher priorities.
const UBaseType_t CLOUD_SYNC_TASK_PRIORITY = 1;
const UBaseType_t BLE_SCAN_TASK_PRIORITY = 2;
// 2026-09-02: stack sizes named rather than inlined at the xTaskCreate call sites, so
// /device can report size alongside high-water and a reader can see the real margin
// instead of having to cross-reference two places. Values unchanged from what was already
// being passed - only the loopTask size below is actually new.
const uint32_t CLOUD_SYNC_TASK_STACK_BYTES = 16384;
const uint32_t BLE_SCAN_TASK_STACK_BYTES = 8192;
// Arduino's loopTask default. It runs the WebServer, display rendering, touch handling and
// WiFi maintenance, and was the only task with no stack telemetry until 2026-09-02.
// Briefly raised to 12288 that day on the theory that sensorDataJson()'s
// StaticJsonDocument<8192> sat on this stack. It does not: ArduinoJson 7 (7.4.3 here)
// allocates JsonDocument on the HEAP, and StaticJsonDocument is only a deprecated alias.
// Measurement then showed loopTask peaking at 3,608 bytes with the default 8192 already
// giving ~56% headroom, so this was reverted - task stacks come out of the same heap that is
// this receiver's real constraint. Left as a named constant so /device can report it.
const uint32_t TREEBEARD_LOOP_TASK_STACK_BYTES = 8192;
const uint8_t WIFI_CONNECT_REBOOT_RETRIES = 2;
const uint32_t WIFI_HEALTH_CHECK_INTERVAL_MS = 1000;
const uint32_t WIFI_RECONNECT_MIN_DELAY_MS = 5000;
const uint32_t WIFI_RECONNECT_MAX_DELAY_MS = 60000;
const uint8_t WIFI_RECONNECT_RADIO_RESET_EVERY = 5;

// Buzzer output on GPIO38 through PCB transistor Q3, driven with LEDC PWM (never left
// continuously high). Duty/envelope numbers are bench-tested on the assembled unit, not
// calculated - see docs/DISPLAY_CENTER_BUZZER_SOUND_DESIGN.md. The replacement buzzer
// soldered onto real units is an unknown-spec passive part (the original TDK piezo did
// not work), so don't "improve" these from a datasheet; they were picked by ear.
const uint8_t BUZZER_PIN = 38;
const uint8_t BUZZER_PWM_RESOLUTION_BITS = 8;
const uint16_t BUZZER_PWM_MAX = (1U << BUZZER_PWM_RESOLUTION_BITS) - 1U;
const uint8_t BUZZER_DUTY_PERCENT = 4; // 8% was audibly harsher/louder on the bench unit
const uint16_t BUZZER_ENVELOPE_MS = 8; // attack/release ramp; suppresses PWM switching clicks
// How often the water-warning re-fires while a plant stays critically low. The bench
// audition looped every 60s indefinitely; shipped behavior is once immediately on the
// critical transition, then hourly until watered, to avoid nagging.
const uint32_t BUZZER_WATER_ALERT_REPEAT_MS = 3600000UL;

struct BuzzerStep
{
  uint16_t frequencyHz;
  uint16_t toneMs;
  uint16_t silenceMs;
};

// Ascending major triad, played once after setup() reaches a ready state.
const BuzzerStep BUZZER_STARTUP_SOUND[] = {
  { 1047, 55, 22 },
  { 1319, 60, 22 },
  { 1568, 115, 0 },
};

// Minimal double buzz; serviceWaterAlert() repeats this 3x with a 350ms gap per alert.
const BuzzerStep BUZZER_WATER_WARNING[] = {
  { 2400, 48, 72 },
  { 2400, 58, 0 },
};

// Keep this first helper below BuzzerStep. Arduino inserts generated prototypes immediately
// before the first .ino function definition; moving a function above this struct makes those
// prototypes refer to BuzzerStep before the type exists (see the preprocessor warning at EOF).
static inline uint8_t moistureScaleVersion(bool wideRaw, bool caplessTank, bool msv4)
{
  if (!wideRaw) return MSV2_MOISTURE_SCALE_VERSION;
  if (msv4) return MSV4_MOISTURE_SCALE_VERSION;
  return caplessTank ? MSV3_CAPLESS_MOISTURE_SCALE_VERSION : MSV3_MOISTURE_SCALE_VERSION;
}

// The stored form of an MSV4 reading's diagnostics (see MSV4_ADV_FLAG2_LAYOUT_B): Cm in 0.1 pF
// in the low half of rawBareTankFdcCounts, Cd in 0.01 pF in the high half. bareTankValid stays
// false so nothing mistakes the word for a tank reference.
static inline uint16_t msv4CmDeciPf(uint32_t packed) { return (uint16_t)(packed & 0xFFFFUL); }
static inline uint16_t msv4CdCentiPf(uint32_t packed) { return (uint16_t)(packed >> 16); }

// The MSV4-only raw fields for the hand-built JSON paths. Rsoil itself is already out as
// raw.fdcCounts and the tip temperature as raw.boardTempQuarterC, which is how they reach the
// existing cloud columns.
static inline uint32_t historyFdcCountsField(uint32_t rawFdcCounts, uint8_t moistureScale)
{
  return moistureScale == MSV4_MOISTURE_SCALE_VERSION ? (rawFdcCounts & MSV4_RSOIL_MASK) : rawFdcCounts;
}

static void appendMsv4RawJson(String& output, uint32_t packed, uint8_t fitRmsE4, uint8_t cpDeciPf,
                              bool dieTempValid, int16_t dieTempQuarterC)
{
  output += F(",\"msv4\":{\"cmDeciPf\":");
  output += msv4CmDeciPf(packed);
  output += F(",\"fitRmsE4\":");
  output += fitRmsE4;
  if (msv4CdCentiPf(packed) != 0)
  {
    output += F(",\"cdCentiPf\":");
    output += msv4CdCentiPf(packed);
  }
  if (cpDeciPf != 0)
  {
    output += F(",\"cpDeciPf\":");
    output += cpDeciPf;
  }
  if (dieTempValid)
  {
    output += F(",\"dieTempQuarterC\":");
    output += dieTempQuarterC;
  }
  output += '}';
}

static inline uint16_t msv4EncodeDieTemp(bool valid, int16_t quarterC)
{
  return valid ? (uint16_t)(quarterC + MSV4_DIE_TEMP_HISTORY_OFFSET) : 0;
}

static inline bool msv4DecodeDieTemp(uint16_t encoded, int16_t& quarterC)
{
  if (encoded == 0) return false;
  quarterC = (int16_t)((int32_t)encoded - MSV4_DIE_TEMP_HISTORY_OFFSET);
  return true;
}

// The history-record form: the die temperature comes out of the rawMoistureKHz slot and the
// residual out of the top byte of rawFdcCounts. Cp is not kept in history.
static void appendMsv4HistoryRawJson(String& output, uint32_t packed, uint16_t encodedDieTemp,
                                     uint32_t rawFdcCounts)
{
  int16_t dieTempQuarterC = 0;
  const bool dieTempValid = msv4DecodeDieTemp(encodedDieTemp, dieTempQuarterC);
  appendMsv4RawJson(output, packed, (uint8_t)(rawFdcCounts >> 24), 0, dieTempValid, dieTempQuarterC);
}

uint16_t buzzerDutyRaw()
{
  return (uint16_t)((BUZZER_PWM_MAX * BUZZER_DUTY_PERCENT + 50U) / 100U);
}

// Five samples are enough to reject two isolated spikes while adding only four packets
// of warm-up. The center, not either envelope of the noise cloud, feeds the low-pass.
constexpr uint8_t MOISTURE_CENTER_WINDOW = 5;

struct SensorReading
{
  bool seen = false;
  uint32_t wake = 0;
  // Presentation moisture is deliberately separate from the latest real measurement.
  // Every display/web surface uses moisture; measuredMoisture remains untouched by the
  // centered presentation filter and is what the cloud stores in the original history column for
  // diagnostics and auto-calibration. The filter state is receiver-local and therefore
  // applies uniformly to legacy, MSV2 raw-kHz, and MSV3 FDC packet formats.
  uint16_t moisture = 0;
  uint16_t measuredMoisture = 0;
  uint16_t moistureCenterWindow[MOISTURE_CENTER_WINDOW] = {};
  uint8_t moistureCenterCount = 0;
  uint8_t moistureCenterNext = 0;
  // Q8 fixed point prevents a symmetric 1/8 EMA from rounding every small correction
  // toward one side. `moisture` is the rounded integer presentation of this accumulator.
  uint32_t moistureFilteredQ8 = 0;
  // Counts consecutive robust-center readings above the watering threshold. The robust
  // center already needs three of the last five packets to move, and this second gate
  // prevents a short burst from being presented as watering.
  uint8_t moistureWateringStreak = 0;
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
  // 2026-08-22: MSV3's FDC2214 raw reading (~18-22 million on this board) -
  // does not fit in rawMoistureKHz (uint16_t, and that field's own display
  // formatting hardcodes a "kHz" suffix that would be actively wrong for an
  // LC-tank frequency-counter value). wideRaw distinguishes which of the two
  // raw fields is populated; moisture/batteryPercent above are always the
  // calibrated values regardless of which wire format produced them.
  bool wideRaw = false;
  uint32_t rawFdcCounts = 0;
  // 2026-08-25: present only on a >=23-byte MSV3 diagnostic packet (see
  // MSV3_DIAGNOSTIC_PACKET_LEN). Board (die) temperature and bare-tank FDC reference are
  // separate raw observables, NOT a computed/compensated moisture value - never derive or
  // display a "soil temperature" from these here; that interpretation is deliberately deferred
  // to the receiver/cloud side later. validity booleans, not zero, represent an invalid/absent
  // reading, since 0 is itself a plausible raw value for either field.
  bool hasMsv3Diagnostics = false;
  bool boardTempValid = false;
  int16_t rawBoardTempQuarterC = 0;
  bool bareTankValid = false;
  uint32_t rawBareTankFdcCounts = 0;
  // Probe flag bit6. C17 is physically absent and firmware never selects mux code 2/3, so the
  // bare field must be invalid and the capless presentation scale/calibration epoch must be used.
  bool caplessTank = false;
  uint8_t tankPhase = 0;
  // Probe flag bit7: an MSV4 conductance packet. rawFdcCounts is then Rsoil in 0.1 ohm,
  // rawBoardTempQuarterC is the blade-tip NTC, rawBareTankFdcCounts is Cm | Cd << 16
  // (see msv4CmDeciPf()), and tankPhase is 1 on the wake the probe actually measured and 0 on
  // the wakes that repeat that measurement. See MSV4_MOISTURE_SCALE_VERSION.
  bool msv4 = false;
  // MSV4 26-byte packets only: nRF52840 die temperature. Kept apart from rawBoardTempQuarterC,
  // which on an MSV4 is the tip thermistor.
  bool msv4DieTempValid = false;
  int16_t msv4DieTempQuarterC = 0;
  // MSV4 fit RMS residual in 1e-4 of drive, and Cp in 0.1 pF when the packet still carried it
  // (layout A; 0 = not sent). rawFdcCounts above is the resistance alone.
  uint8_t msv4FitRmsE4 = 0;
  uint8_t msv4CpDeciPf = 0;
  // Receiver-derived shadow channel. This never overwrites rawFdcCounts or the original
  // measuredMoisture. In Active mode (and only after two accepted daily fits), the equivalent
  // corrected count may feed the presentation filter; cloud history still receives both.
  bool temperatureCompensationValid = false;
  int32_t temperatureCompensatedLogRatioPpm = 0;
  uint16_t temperatureCompensatedEquivalentMoisture = 0;
  uint8_t temperatureCompensationModelVersion = 0;
  uint8_t temperatureCompensationConfiguredMode = TEMPERATURE_COMPENSATION_LEARNING;
  uint8_t temperatureCompensationEffectiveMode = TEMPERATURE_COMPENSATION_LEARNING;
  uint8_t temperatureCompensationConfidencePercent = 0;
  float temperatureCompensationGainPerC = TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C;
  float temperatureCompensationReferenceC = TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C;
  uint32_t temperatureCompensationEpoch = 0;
  // Set when the probe advertised but reported its FDC read as failed (flag bit5), or when
  // the count it sent is implausible. The probe is reachable; the number is not a measurement.
  bool sensorFault = false;
};

// Immutable copy of one unique probe wake as received by BLE. The live sensors[] array
// remains the latest-value cache used by the display; this separate queue prevents each
// new wake from overwriting history before the next cloud snapshot. capturedMs is turned
// into queuedAgeMs at upload time, allowing the Edge Function to reconstruct the actual
// observation timestamp instead of assigning every row in a batch the same server time.
struct PendingHistoryReading
{
  // Stored with the row (rather than read from the current boot) so an observation keeps
  // the same idempotency key after a reset and can safely be uploaded again.
  uint32_t bootId = 0;
  uint32_t sequence = 0;
  uint32_t capturedMs = 0;
  uint64_t observedAtMs = 0;
  uint32_t wake = 0;
  uint32_t rawFdcCounts = 0;
  uint16_t moisture = 0;
  uint16_t measuredMoisture = 0;
  uint16_t rawMoistureKHz = 0;
  // Raw battery voltage/charging-state as sent by the probe, alongside the receiver's own
  // presentation percent above - lets the History chart re-derive/re-tune battery smoothing
  // from the original signal instead of only ever seeing what the receiver's filter baked in
  // at ingest time. Only meaningful when hasRaw is true, same as rawMoistureKHz/rawFdcCounts.
  uint16_t rawBatteryMv = 0;
  bool rawCharging = false;
  int16_t rssi = 0;
  uint8_t sensorId = 0;
  uint8_t moistureScale = MSV2_MOISTURE_SCALE_VERSION;
  uint8_t batteryPercent = BATTERY_UNKNOWN;
  bool hasRaw = false;
  bool wideRaw = false;
  // Same meaning/validity convention as SensorReading's fields of the same name - see there.
  bool hasMsv3Diagnostics = false;
  bool boardTempValid = false;
  int16_t rawBoardTempQuarterC = 0;
  bool bareTankValid = false;
  uint32_t rawBareTankFdcCounts = 0;
  uint8_t tankPhase = 0;
};

// Per-sensor online learner. The 4-term daily regression is:
//   logRatio = intercept + dryDownSlope*timeDays + gain*(Eslow*(Teffective-Tref))
//              + phaseOffset*tankPhase
// A slow moisture baseline keeps the thermal term proportional to the amount of capacitance
// actually present in the pot, while the time term prevents ordinary dry-down from being
// mistaken for temperature response. Two independent 24-hour fits are required before Active
// mode can influence the user-facing value.
struct TemperatureCompensationState
{
  bool emaReady = false;
  float effectiveTempC = TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C;
  float slowLogRatio = 0.0f;
  float gainPerC = TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C;
  float referenceTempC = TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C;
  float phaseOffset = TEMPERATURE_COMPENSATION_SEED_PHASE_OFFSET;
  uint32_t lastUpdateMs = 0;
  uint32_t blockStartMs = 0;
  uint32_t restoredBlockElapsedMs = 0;
  uint32_t lastConnectedCounts = 0;
  uint32_t lastBareCounts = 0;
  uint32_t epoch = 0;
  uint32_t sampleCount = 0;
  float minTempC = 1000.0f;
  float maxTempC = -1000.0f;
  double normal[4][4] = {};
  double rhs[4] = {};
  double sumY = 0.0;
  double sumYSquared = 0.0;
  uint8_t acceptedFits = 0;
};

struct TemperatureCompensationResult
{
  bool valid = false;
  int32_t logRatioPpm = 0;
  uint16_t equivalentMoisture = 0;
  uint8_t configuredMode = TEMPERATURE_COMPENSATION_LEARNING;
  uint8_t effectiveMode = TEMPERATURE_COMPENSATION_LEARNING;
  uint8_t confidencePercent = 0;
  float gainPerC = TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C;
  float referenceTempC = TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C;
  uint32_t epoch = 0;
};

// Fixed-version envelope around each append. A checksum lets startup discard only a torn
// power-loss tail instead of treating the whole journal as corrupt. The state is separate
// and double-buffered; losing the newest head update can only replay already-accepted rows,
// which the cloud observation-id uniqueness constraint handles idempotently.
struct HistoryJournalRecord
{
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t payloadSize = 0;
  PendingHistoryReading reading;
  uint32_t checksum = 0;
};

struct HistoryJournalState
{
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t size = 0;
  uint32_t generation = 0;
  uint32_t headOffset = 0;
  uint32_t dropped = 0;
  uint32_t highWaterMark = 0;
  uint32_t checksum = 0;
};

// RTC slow memory survives watchdog/panic/software resets without touching flash. It closes
// the durability gap introduced by batching LittleFS writes: a crash between commits restores
// the exact ingress ring on the next boot. A cold power loss still relies on the last journal
// commit, as expected for memory that has no independent power source.
struct RtcHistoryStaging
{
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t size = 0;
  uint8_t head = 0;
  uint8_t count = 0;
  uint16_t reserved = 0;
  PendingHistoryReading readings[HISTORY_STAGING_CAPACITY];
  uint32_t checksum = 0;
};

// Arduino's sketch preprocessor otherwise emits these prototypes above the custom
// struct definitions, where their parameter types do not exist yet.
uint32_t historyChecksum(const void* data, size_t length);
bool validHistoryJournalRecord(const HistoryJournalRecord& record);
bool validHistoryJournalState(const HistoryJournalState& state);
bool readHistoryJournalState(const char* path, HistoryJournalState& state);
bool persistHistoryJournalState();
bool replaceHistoryJournalPrefix(uint32_t validBytes);
bool enqueueHistoryReading(uint8_t sensorId, const SensorReading& reading);
size_t copyHistoryUploadBatch(PendingHistoryReading* destination, size_t capacity);
size_t acknowledgeHistoryUploadBatch(const PendingHistoryReading* uploaded, size_t count);
bool initializeHistoryJournal();
bool restoreHistoryStagingFromRtc();
void checkpointHistoryStagingToRtcLocked();
void serviceHistoryJournal();
bool compactHistoryJournal();
String historyOutboxJson(const PendingHistoryReading* historyBatch, size_t historyCount);
String historyOutboxPrefixJson(uint32_t serializedAtMs);
void appendHistoryOutboxRowJson(String& output, const PendingHistoryReading& pending,
                                uint32_t serializedAtMs, bool prependComma);
String snapshotJsonWithHistory(const PendingHistoryReading* historyBatch, size_t historyCount);
bool copySensorReading(uint8_t sensorId, SensorReading& destination);
void cacheTemperatureCompensationModes(JsonVariantConst config);
bool loadTemperatureCompensationModes();
bool loadTemperatureCompensationModels();
bool saveTemperatureCompensationModels();
void serviceTemperatureCompensationPersistence();
void resetTemperatureCompensationFit(TemperatureCompensationState& state, uint32_t nowMs);
void resetTemperatureCompensationEpoch(
    TemperatureCompensationState& state, uint8_t sensorId, uint32_t nowMs,
    float boardTempC, float logRatio, uint32_t connectedCounts, uint32_t bareCounts,
    const char* reason);
TemperatureCompensationResult updateTemperatureCompensation(
    uint8_t sensorId, uint32_t connectedCounts, uint32_t bareCounts,
    int16_t boardTempQuarterC, uint8_t tankPhase, uint32_t nowMs,
    bool probeRestarted);

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
  float daysSinceLastWatering = -1.0f; // informational only - no longer drives any alert
  // Epoch ms of the last detected watering event, -1 if none observed yet. Kept alongside
  // daysSinceLastWatering (a rolling float, blind to calendar-day boundaries) because only
  // a real timestamp lets a client render a correct "today"/"yesterday"/"Nd ago" label in
  // its own local timezone - see MobileAppPage.h's wateredAgoText(). Not consumed by
  // firmware itself, just piped through to the local /data endpoint.
  double lastWateredAtMs = -1.0;
  // Retained for compatibility with older cloud responses. Watering-cycle calibration
  // no longer uses the rolling-percentile floor clamp, so new responses leave this false.
  bool dryBoundTrendingLower = false;
  // 2026-10-11: dryBound/wetBound are the cloud's "water now" region (receiver-snapshot's
  // region.ts, asked for with region=1): wetBound is the level the pot settled at after its
  // last watering and dryBound the top of the region the user waters in. Only then does a card
  // say WATER NOW below 0 %. On the older learned range 0 % was wherever the last waterings
  // happened, and saying so there would be a guess.
  bool region = false;
};

AutoCalState autoCal[MAX_SENSOR_ID + 1];
bool copyAutoCalState(uint8_t sensorId, AutoCalState& destination);
bool loadAutoCalCache();
bool saveAutoCalCache();

#if TREEBEARD_ENABLE_DISPLAY
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
// 2026-08-30 gesture redesign: open the receiver brightness controls with a long-press
// (hold, no lift) anywhere on the dashboard, replacing the old double-tap-anywhere gesture.
// The double-tap version required this resistive panel to reliably tell "two clean release
// events" apart from ordinary contact bounce, which it did not do well in practice (field
// report: it mostly just registered as a single tap on whatever plant card it landed on).
// A long-press only needs "is a contact still present", which this hardware handles far
// more reliably - no second-touch coordination, no bounce-as-second-tap risk. This also
// lets a genuine short tap on a plant card act immediately (see handleTouchInput()) instead
// of every tap being deferred for up to 650ms (the old double-tap window) to see if a second
// one followed - that mandatory wait, with no tap-feedback on screen, was the leading suspect
// for taps "freezing" rather than opening a card's diagnostic view.
const uint32_t TOUCH_LONG_PRESS_MS = 550;
// 2026-08-30, same-day follow-up: a raw touch-up sample is not immediately trusted as a real
// release - handleTouchInput() requires it to hold for this long first. Removing the old
// double-tap design's 650ms deferral (above) made ordinary taps instant, but that deferral
// had also been absorbing resistive-panel contact bounce as a side effect the whole time -
// this codebase's own prior comment on the removed TOUCH_DOUBLE_TAP_MIN_GAP_MS said bounce on
// this exact panel can span up to ~120ms between release events. Without an equivalent
// safeguard, a single physical tap could fire the tap/collapse action 2-3 times in rapid
// succession, corrupting the expand/collapse sequence (confirmed field symptom: a card stuck
// half-drawn, or a tap silently toggling back to its prior state). 160ms = the ~120ms bounce
// ceiling plus one TOUCH_POLL_INTERVAL_MS (32ms) of polling-phase margin, rounded up - still
// far below the old 650ms, so a genuine tap still feels immediate.
const uint32_t TOUCH_RELEASE_STABLE_MS = 160;
// 2026-08-30 IRQ-primary redesign, phone-a-friend consultation "display-touch-corruption-
// 2026-08-30": confirmed by isolation test (touch SPI fully disabled -> zero card-art
// corruption over an extended watch) that touch SPI transactions during active gestures, not
// idle polling, are the actual corruption trigger. The gesture logic above (long-press hold +
// release-stability) was correct in shape but implemented by re-calling touchScreen.touched()
// every TOUCH_POLL_INTERVAL_MS the whole time a finger was down or just lifted - a plain tap
// cost 5-20+ SPI transactions clustered right around the TFT writes that follow. handleTouchInput
// now treats the T_IRQ GPIO level (free, no SPI) as the primary down/up signal and only spends a
// real SPI transaction (touchScreen.getPoint()) when coordinates are actually needed: once per
// gesture at the confirmed initial press, and repeatedly only while actively dragging the
// brightness slider. This constant is the settle window enforced after every such SPI read,
// on both the down and release side, before trusting the T_IRQ level again - XPT2046's PENIRQ
// can glitch/deassert during and immediately after a conversion (this is documented ADC
// behavior, not a hardware fault), so a release must never be evaluated from a GPIO sample
// taken inside this window. Value = TOUCH_POLL_INTERVAL_MS (one full poll's worth of margin).
const uint32_t TOUCH_POST_SPI_BLACKOUT_MS = 32;
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
#if TREEBEARD_ENABLE_WIFI
WebServer* serverPtr = nullptr;
DNSServer* dnsServerPtr = nullptr;
#endif
#if TREEBEARD_ENABLE_BLE
BLEScan* bleScan = nullptr;
TaskHandle_t bleScanTaskHandle = nullptr;
volatile bool cloudRadioExclusive = false;
// 2026-08-31 diagnostic, live-hardware investigation into intermittent snapshot-upload
// "connection refused" failures: cloudSyncTask runs at a deliberately LOWER FreeRTOS priority
// than bleScanTask (see CLOUD_SYNC_TASK_PRIORITY's own comment - intentional, so cloud work
// can never delay a BLE scan window) on the same core, and the BLE host/controller tasks
// underneath both of them run higher still. beginCloudRadioSlot() calls bleScan->stop() and
// waits a fixed 75ms before letting the cloud task proceed, on the assumption that's enough
// time for any in-flight scan to actually quiesce - these fields capture whether that
// assumption held on each real attempt, correlated against that attempt's upload outcome via
// /cloud's own lastAttemptMs, without changing any actual scheduling/timing behavior.
uint32_t lastRadioSlotAtMs = 0;
bool lastRadioSlotWasScanning = false;
bool lastRadioSlotStillScanningAfterSettle = false;
uint32_t lastRadioSlotStopCallMs = 0;
#endif
SensorReading sensors[MAX_SENSOR_ID + 1];
TemperatureCompensationState temperatureCompensation[MAX_SENSOR_ID + 1];
uint8_t temperatureCompensationMode[MAX_SENSOR_ID + 1];
volatile bool temperatureCompensationPersistPending = false;
volatile bool temperatureCompensationPersistUrgent = false;
uint32_t lastTemperatureCompensationPersistMs = 0;
PendingHistoryReading pendingHistory[HISTORY_STAGING_CAPACITY];
uint8_t pendingHistoryHead = 0;
uint8_t pendingHistoryCount = 0;
uint32_t pendingHistoryOldestStagedAtMs = 0;
RTC_NOINIT_ATTR RtcHistoryStaging rtcHistoryStaging;
size_t durableHistoryCount = 0;
uint32_t historyJournalHeadOffset = 0;
uint32_t historyJournalValidBytes = 0;
uint32_t historyJournalStateGeneration = 0;
bool historyJournalReady = false;
uint32_t historyJournalStorageErrors = 0;
// Latest outage-spill journal cost. Healthy uploads bypass LittleFS and acknowledge the
// RTC-backed ring directly, so these values should remain unchanged during ordinary online
// operation; they expose the rarer flash cost when WiFi/backend downtime forces spillover.
uint32_t lastJournalWriteAtMs = 0;
uint32_t lastJournalWriteDurationMs = 0;
uint32_t lastJournalWriteRecords = 0;
uint32_t lastJournalCompactDurationMs = 0;
bool lastJournalCompactedThisWrite = false;
uint32_t pendingHistorySequence = 0;
uint32_t pendingHistoryDropped = 0;
uint32_t pendingHistoryHighWaterMark = 0;
uint32_t historyQueueBootId = 0;
uint64_t cloudTimeAnchorEpochMs = 0;
uint32_t cloudTimeAnchorMillis = 0;
portMUX_TYPE historyQueueMux = portMUX_INITIALIZER_UNLOCKED;
// Guards every sensors[] read/write. Readers take a short value copy and release the
// lock before rendering or JSON allocation; the BLE callback publishes one complete
// record while holding the same lock. This prevents logical packet tearing without
// keeping interrupts disabled around slow display/network operations.
portMUX_TYPE sensorsMux = portMUX_INITIALIZER_UNLOCKED;
// Model persistence/config changes run on the main/cloud tasks while BLE updates the learner.
// Copy state under this short lock, do floating-point work outside it, then publish atomically.
portMUX_TYPE temperatureCompensationMux = portMUX_INITIALIZER_UNLOCKED;
// runCloudAutoCalPull() writes on the cloud task while rendering, /data serialization,
// and BLE presentation filtering read on other tasks/cores. Treat each state as one
// record for the same reason as sensors[].
portMUX_TYPE autoCalMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Bench probe table -------------------------------------------------------------------
//
// Written by parseBenchAdvertisement() on the BLE scan task, read by the Bench page on
// loopTask. Same one-record-under-one-short-lock discipline as sensors[], and for the same
// reason: a half-updated row would render a count from one packet beside a wake counter from
// another and look exactly like a real measurement discontinuity.
//
// Kept entirely separate from sensors[] by design - see MSV3_BENCH_MAGIC's comment.
struct BenchProbe
{
  bool seen;
  uint8_t sensorId;          // 99 on an untargeted bring-up build, the fleet id on a targeted one
  uint32_t wake;
  uint32_t fdcCounts;
  bool readingValid;         // false = the probe advertised a placeholder, not a measurement
  uint16_t batteryMv;
  bool charging;
  bool otaPowerOk;
  bool liveMode;             // probe is in `bench on`, refreshing ~4x/s
  bool boardTempValid;
  int16_t boardTempQuarterC;
  uint8_t formatVersion;     // 0 = pre-versioning bench firmware; mux/status below are absent
  uint8_t muxCode;
  uint16_t fdcStatus;
  int rssi;
  uint32_t lastSeenMs;
  uint32_t received;
};
BenchProbe benchProbes[BENCH_SLOT_COUNT];
portMUX_TYPE benchMux = portMUX_INITIALIZER_UNLOCKED;
// Set by the BLE task when any bench row changes. The /bench web page polls rather than
// subscribing, so nothing in firmware consumes this today; it is kept because it is the natural
// hook for a future push/SSE endpoint and costs one byte.
volatile bool benchDataDirty = false;

// Manual prototypes for every bench function whose signature names the struct above.
// REQUIRED, not stylistic: the .ino preprocessor inserts auto-generated prototypes immediately
// before the file's first function definition (color565(), far above these declarations), so an
// auto-generated one would reference BenchProbe before it exists and fail to compile. Declaring them here suppresses that generation - the same reason the PlantArtSlot
// prototype block further down exists. See this file's closing comment for the full account.
void benchSnapshotAll(BenchProbe* out);
bool benchSlotIsFresh(const BenchProbe& probe, uint32_t nowMs);

#if TREEBEARD_ENABLE_DISPLAY
// Defined with the rest of the touch/display state below. Journal commits avoid an active
// touch acquisition and the short post-render SPI settling window whenever capacity permits.
extern volatile bool touchAcquisitionPending;
extern volatile uint32_t lastDisplaySpiActivityMs;
#endif

bool copySensorReading(uint8_t sensorId, SensorReading& destination)
{
  if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
  {
    return false;
  }
  portENTER_CRITICAL(&sensorsMux);
  destination = sensors[sensorId];
  portEXIT_CRITICAL(&sensorsMux);
  return true;
}

bool copyAutoCalState(uint8_t sensorId, AutoCalState& destination)
{
  if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
  {
    return false;
  }
  portENTER_CRITICAL(&autoCalMux);
  destination = autoCal[sensorId];
  portEXIT_CRITICAL(&autoCalMux);
  return true;
}

static_assert(HISTORY_UPLOAD_BATCH_SIZE <= HISTORY_QUEUE_CAPACITY,
              "history upload batch must fit inside the receiver queue");
static_assert(sizeof(PendingHistoryReading) * HISTORY_UPLOAD_BATCH_SIZE <= 8192,
              "history upload batch must leave margin on the cloud task stack");
static_assert(HISTORY_JOURNAL_WRITE_BATCH_SIZE <= HISTORY_STAGING_CAPACITY,
              "journal write batch must fit inside the staging ring");
static_assert(sizeof(RtcHistoryStaging) <= 6144,
              "RTC history staging must leave room for reset checkpoints");

const uint32_t HISTORY_RECORD_MAGIC = 0x31545348UL; // "HST1" little-endian
const uint32_t HISTORY_STATE_MAGIC = 0x31535348UL;  // "HSS1" little-endian
const uint32_t HISTORY_RTC_MAGIC = 0x31525448UL;    // "HTR1" little-endian
const uint16_t HISTORY_JOURNAL_VERSION = 1;

uint32_t historyChecksum(const void* data, size_t length)
{
  // FNV-1a is compact and sufficient here: this is torn-write/corruption detection, not
  // authentication. Authentication remains the receiver bearer token over TLS.
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++)
  {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

void checkpointHistoryStagingToRtcLocked()
{
  // checksum is written last. If a reset interrupts this copy, restore rejects the partial
  // mirror instead of replaying a mixture of old and new ring state.
  rtcHistoryStaging.magic = HISTORY_RTC_MAGIC;
  rtcHistoryStaging.version = HISTORY_JOURNAL_VERSION;
  rtcHistoryStaging.size = sizeof(RtcHistoryStaging);
  rtcHistoryStaging.head = pendingHistoryHead;
  rtcHistoryStaging.count = pendingHistoryCount;
  rtcHistoryStaging.reserved = 0;
  for (size_t i = 0; i < HISTORY_STAGING_CAPACITY; i++)
  {
    rtcHistoryStaging.readings[i] = pendingHistory[i];
  }
  rtcHistoryStaging.checksum = historyChecksum(
      &rtcHistoryStaging, offsetof(RtcHistoryStaging, checksum));
}

bool restoreHistoryStagingFromRtc()
{
  const bool valid = rtcHistoryStaging.magic == HISTORY_RTC_MAGIC &&
                     rtcHistoryStaging.version == HISTORY_JOURNAL_VERSION &&
                     rtcHistoryStaging.size == sizeof(RtcHistoryStaging) &&
                     rtcHistoryStaging.head < HISTORY_STAGING_CAPACITY &&
                     rtcHistoryStaging.count <= HISTORY_STAGING_CAPACITY &&
                     rtcHistoryStaging.checksum == historyChecksum(
                         &rtcHistoryStaging, offsetof(RtcHistoryStaging, checksum));

  portENTER_CRITICAL(&historyQueueMux);
  pendingHistoryHead = 0;
  pendingHistoryCount = 0;
  memset(pendingHistory, 0, sizeof(pendingHistory));
  if (valid)
  {
    pendingHistoryHead = rtcHistoryStaging.head;
    pendingHistoryCount = rtcHistoryStaging.count;
    for (size_t i = 0; i < HISTORY_STAGING_CAPACITY; i++)
    {
      pendingHistory[i] = rtcHistoryStaging.readings[i];
    }
    // capturedMs belongs to the previous boot's millis() epoch. Start a fresh spill timer
    // instead of interpreting that timestamp against this boot and forcing an immediate
    // flash commit during startup.
    pendingHistoryOldestStagedAtMs = pendingHistoryCount > 0 ? millis() : 0;
  }
  else
  {
    memset(&rtcHistoryStaging, 0, sizeof(rtcHistoryStaging));
    pendingHistoryOldestStagedAtMs = 0;
  }
  checkpointHistoryStagingToRtcLocked();
  portEXIT_CRITICAL(&historyQueueMux);

  if (valid && pendingHistoryCount > 0)
  {
    Serial.print("[Treebeard history] restored RTC-staged rows=");
    Serial.println(pendingHistoryCount);
  }
  return valid;
}

bool loadAutoCalCache()
{
  File file = LittleFS.open(AUTOCAL_CACHE_PATH, FILE_READ);
  if (!file)
  {
    return false;
  }
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error || (doc["schema"] | 0) != 1)
  {
    return false;
  }

  AutoCalState restored[MAX_SENSOR_ID + 1];
  JsonArray ranges = doc["ranges"].as<JsonArray>();
  for (JsonObject row : ranges)
  {
    uint16_t sensorId = row["sensor"] | 0;
    if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
    {
      continue;
    }
    const int dry = row["dry"] | 0;
    const int wet = row["wet"] | 0;
    const bool ready = row["ready"] | false;
    restored[sensorId].ready = ready && dry >= 0 && wet > dry && wet <= 65535;
    restored[sensorId].dryBound = dry;
    restored[sensorId].wetBound = wet;
    restored[sensorId].dryBoundTrendingLower = row["trend"] | false;
    restored[sensorId].region = row["region"] | false;
  }

  portENTER_CRITICAL(&autoCalMux);
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    autoCal[sensorId] = restored[sensorId];
  }
  portEXIT_CRITICAL(&autoCalMux);
  Serial.println("[Treebeard cloud] restored cached auto-calibration ranges");
  return true;
}

bool saveAutoCalCache()
{
  DynamicJsonDocument doc(2048);
  doc["schema"] = 1;
  JsonArray ranges = doc.createNestedArray("ranges");
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    AutoCalState learned;
    copyAutoCalState(sensorId, learned);
    JsonObject row = ranges.createNestedObject();
    row["sensor"] = sensorId;
    row["ready"] = learned.ready;
    row["dry"] = learned.dryBound;
    row["wet"] = learned.wetBound;
    row["trend"] = learned.dryBoundTrendingLower;
    row["region"] = learned.region;
  }

  String nextJson;
  serializeJson(doc, nextJson);
  if (LittleFS.exists(AUTOCAL_CACHE_PATH))
  {
    File current = LittleFS.open(AUTOCAL_CACHE_PATH, FILE_READ);
    if (current)
    {
      String currentJson = current.readString();
      current.close();
      if (currentJson == nextJson)
      {
        return true;
      }
    }
  }

  File temp = LittleFS.open(AUTOCAL_CACHE_TEMP_PATH, FILE_WRITE);
  if (!temp)
  {
    return false;
  }
  const bool complete = temp.print(nextJson) == nextJson.length();
  temp.flush();
  temp.close();
  if (!complete)
  {
    LittleFS.remove(AUTOCAL_CACHE_TEMP_PATH);
    return false;
  }
  LittleFS.remove(AUTOCAL_CACHE_PATH);
  if (!LittleFS.rename(AUTOCAL_CACHE_TEMP_PATH, AUTOCAL_CACHE_PATH))
  {
    LittleFS.remove(AUTOCAL_CACHE_TEMP_PATH);
    return false;
  }
  return true;
}

const char* temperatureCompensationModeName(uint8_t mode)
{
  if (mode == TEMPERATURE_COMPENSATION_ACTIVE) return "active";
  if (mode == TEMPERATURE_COMPENSATION_LEARNING) return "learning";
  return "off";
}

uint8_t parseTemperatureCompensationMode(const char* value)
{
  if (value != nullptr && strcmp(value, "active") == 0) return TEMPERATURE_COMPENSATION_ACTIVE;
  if (value != nullptr && strcmp(value, "off") == 0) return TEMPERATURE_COMPENSATION_OFF;
  return TEMPERATURE_COMPENSATION_LEARNING;
}

float temperatureCompensationSeedGain(uint8_t sensorId)
{
  // Both seeds are zero since v3, so this is currently a no-op kept for shape: a probe with a
  // genuinely measured per-probe gain could be seeded here. It must never be used to hand a
  // probe a fleet-average gain, because the sign is not a fleet property. Probe 4 fits about
  // +0.013 and probe 8 about -0.004 in the same week, and v2 shipping probe 8 a positive seed
  // it could never fit its way out of is the exact defect v3 exists to fix.
  return sensorId == 4
      ? TEMPERATURE_COMPENSATION_PROBE_4_SEED_GAIN_PER_C
      : TEMPERATURE_COMPENSATION_DEFAULT_SEED_GAIN_PER_C;
}

float temperatureCompensationSeedReferenceC(uint8_t sensorId)
{
  return sensorId == 4
      ? TEMPERATURE_COMPENSATION_PROBE_4_REFERENCE_C
      : TEMPERATURE_COMPENSATION_DEFAULT_REFERENCE_C;
}

void cacheTemperatureCompensationModes(JsonVariantConst config)
{
  uint8_t nextModes[MAX_SENSOR_ID + 1];
  for (uint8_t sensorId = 0; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    // Shadow learning is safe for existing configurations because it never changes the
    // presentation value. Users must explicitly choose Active before it can do so.
    nextModes[sensorId] = TEMPERATURE_COMPENSATION_LEARNING;
  }

  JsonArrayConst cards = config["cards"].as<JsonArrayConst>();
  for (JsonObjectConst card : cards)
  {
    uint8_t sensorId = card["sensor"] | 0;
    if (sensorId == 0 || sensorId > MAX_SENSOR_ID) continue;
    JsonObjectConst moisture = card["moisture"].as<JsonObjectConst>();
    nextModes[sensorId] = parseTemperatureCompensationMode(
        moisture["temperatureCompensation"] | "learning");
  }

  portENTER_CRITICAL(&temperatureCompensationMux);
  memcpy(temperatureCompensationMode, nextModes, sizeof(nextModes));
  portEXIT_CRITICAL(&temperatureCompensationMux);
}

bool loadTemperatureCompensationModes()
{
  portENTER_CRITICAL(&temperatureCompensationMux);
  for (uint8_t sensorId = 0; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    temperatureCompensationMode[sensorId] = TEMPERATURE_COMPENSATION_LEARNING;
  }
  portEXIT_CRITICAL(&temperatureCompensationMux);
  File file = LittleFS.open(UI_PATH, FILE_READ);
  if (!file)
  {
    return false;
  }
  DynamicJsonDocument doc(12288);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return false;
  cacheTemperatureCompensationModes(doc.as<JsonVariantConst>());
  return true;
}

bool loadTemperatureCompensationModels()
{
  File file = LittleFS.open(TEMPERATURE_COMPENSATION_CACHE_PATH, FILE_READ);
  if (!file) return false;
  DynamicJsonDocument doc(24576);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error || (doc["schema"] | 0) != TEMPERATURE_COMPENSATION_MODEL_VERSION) return false;

  size_t restored = 0;
  JsonArray models = doc["models"].as<JsonArray>();
  for (JsonObject row : models)
  {
    uint8_t sensorId = row["sensor"] | 0;
    float gain = row["gainPerC"] | temperatureCompensationSeedGain(sensorId);
    float referenceTempC = row["referenceTempC"] |
        temperatureCompensationSeedReferenceC(sensorId);
    float phaseOffset = row["phaseOffset"] | TEMPERATURE_COMPENSATION_SEED_PHASE_OFFSET;
    float effectiveTempC = row["effectiveTempC"] |
        temperatureCompensationSeedReferenceC(sensorId);
    float slowLogRatio = row["slowLogRatio"] | 0.0f;
    uint8_t acceptedFits = row["acceptedFits"] | 0;
    bool emaReady = row["emaReady"] | false;
    uint32_t epoch = row["epoch"] | 0;
    uint32_t blockElapsedMs = row["blockElapsedMs"] | 0;
    uint32_t sampleCount = row["sampleCount"] | 0;
    uint32_t lastConnectedCounts = row["lastConnectedCounts"] | 0;
    uint32_t lastBareCounts = row["lastBareCounts"] | 0;
    float minTempC = row["minTempC"] | 1000.0f;
    float maxTempC = row["maxTempC"] | -1000.0f;
    double sumY = row["sumY"] | 0.0;
    double sumYSquared = row["sumYSquared"] | 0.0;
    if (sensorId == 0 || sensorId > MAX_SENSOR_ID || !isfinite(gain) ||
        gain < TEMPERATURE_COMPENSATION_MIN_GAIN_PER_C || gain > 0.04f ||
        !isfinite(referenceTempC) || referenceTempC < -40.0f || referenceTempC > 85.0f ||
        !isfinite(phaseOffset) || fabsf(phaseOffset) > 0.01f ||
        !isfinite(effectiveTempC) || effectiveTempC < -40.0f || effectiveTempC > 85.0f ||
        !isfinite(slowLogRatio) || slowLogRatio < -0.2f || slowLogRatio > 1.0f ||
        blockElapsedMs > TEMPERATURE_COMPENSATION_FIT_BLOCK_MS ||
        sampleCount > 100000UL || (sampleCount > 0 && !emaReady) ||
        (sampleCount > 0 && (lastConnectedCounts == 0 || lastConnectedCounts > 0x0FFFFFFFUL ||
                             lastBareCounts == 0 || lastBareCounts > 0x0FFFFFFFUL ||
                             !isfinite(minTempC) || !isfinite(maxTempC) ||
                             minTempC < -40.0f || maxTempC > 85.0f || minTempC > maxTempC)) ||
        !isfinite(sumY) || !isfinite(sumYSquared))
    {
      continue;
    }
    double normal[4][4] = {};
    double rhs[4] = {};
    bool partialValid = true;
    if (sampleCount > 0)
    {
      JsonArray normalValues = row["normal"].as<JsonArray>();
      JsonArray rhsValues = row["rhs"].as<JsonArray>();
      partialValid = normalValues.size() == 16 && rhsValues.size() == 4;
      for (uint8_t i = 0; partialValid && i < 16; i++)
      {
        const double value = normalValues[i].as<double>();
        if (!isfinite(value)) partialValid = false;
        else normal[i / 4][i % 4] = value;
      }
      for (uint8_t i = 0; partialValid && i < 4; i++)
      {
        const double value = rhsValues[i].as<double>();
        if (!isfinite(value)) partialValid = false;
        else rhs[i] = value;
      }
    }
    if (!partialValid) continue;

    portENTER_CRITICAL(&temperatureCompensationMux);
    TemperatureCompensationState& state = temperatureCompensation[sensorId];
    state.gainPerC = gain;
    state.referenceTempC = referenceTempC;
    state.phaseOffset = phaseOffset;
    state.effectiveTempC = effectiveTempC;
    state.slowLogRatio = slowLogRatio;
    state.acceptedFits = acceptedFits;
    state.emaReady = emaReady;
    state.epoch = max(1UL, epoch);
    state.restoredBlockElapsedMs = sampleCount > 0 ? blockElapsedMs : 0;
    state.sampleCount = sampleCount;
    state.lastConnectedCounts = lastConnectedCounts;
    state.lastBareCounts = lastBareCounts;
    state.minTempC = sampleCount > 0 ? minTempC : 1000.0f;
    state.maxTempC = sampleCount > 0 ? maxTempC : -1000.0f;
    memcpy(state.normal, normal, sizeof(normal));
    memcpy(state.rhs, rhs, sizeof(rhs));
    state.sumY = sumY;
    state.sumYSquared = sumYSquared;
    // millis() starts a new epoch after reset. Re-anchor the saved elapsed duration on the
    // first new sample; do not count the unobserved outage as training time or EMA input.
    state.lastUpdateMs = 0;
    state.blockStartMs = 0;
    portEXIT_CRITICAL(&temperatureCompensationMux);
    restored++;
  }
  if (restored > 0)
  {
    Serial.print("[Treebeard temp-comp] restored models=");
    Serial.println(restored);
  }
  return restored > 0;
}

bool saveTemperatureCompensationModels()
{
  const uint32_t nowMs = millis();
  DynamicJsonDocument doc(24576);
  doc["schema"] = TEMPERATURE_COMPENSATION_MODEL_VERSION;
  JsonArray models = doc.createNestedArray("models");
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    TemperatureCompensationState state;
    portENTER_CRITICAL(&temperatureCompensationMux);
    state = temperatureCompensation[sensorId];
    portEXIT_CRITICAL(&temperatureCompensationMux);
    if (!state.emaReady && state.acceptedFits == 0) continue;
    JsonObject row = models.createNestedObject();
    row["sensor"] = sensorId;
    row["gainPerC"] = state.gainPerC;
    row["referenceTempC"] = state.referenceTempC;
    row["phaseOffset"] = state.phaseOffset;
    row["effectiveTempC"] = state.effectiveTempC;
    row["slowLogRatio"] = state.slowLogRatio;
    row["acceptedFits"] = state.acceptedFits;
    row["emaReady"] = state.emaReady;
    row["epoch"] = state.epoch;
    row["blockElapsedMs"] = state.blockStartMs != 0
        ? (uint32_t)(nowMs - state.blockStartMs)
        : state.restoredBlockElapsedMs;
    row["sampleCount"] = state.sampleCount;
    row["lastConnectedCounts"] = state.lastConnectedCounts;
    row["lastBareCounts"] = state.lastBareCounts;
    row["minTempC"] = state.minTempC;
    row["maxTempC"] = state.maxTempC;
    row["sumY"] = state.sumY;
    row["sumYSquared"] = state.sumYSquared;
    JsonArray normalValues = row.createNestedArray("normal");
    JsonArray rhsValues = row.createNestedArray("rhs");
    for (uint8_t r = 0; r < 4; r++)
    {
      for (uint8_t c = 0; c < 4; c++) normalValues.add(state.normal[r][c]);
      rhsValues.add(state.rhs[r]);
    }
  }
  if (doc.overflowed()) return false;

  File temp = LittleFS.open(TEMPERATURE_COMPENSATION_CACHE_TEMP_PATH, FILE_WRITE);
  if (!temp) return false;
  const size_t written = serializeJson(doc, temp);
  temp.flush();
  temp.close();
  if (written == 0)
  {
    LittleFS.remove(TEMPERATURE_COMPENSATION_CACHE_TEMP_PATH);
    return false;
  }
  LittleFS.remove(TEMPERATURE_COMPENSATION_CACHE_PATH);
  if (!LittleFS.rename(TEMPERATURE_COMPENSATION_CACHE_TEMP_PATH,
                       TEMPERATURE_COMPENSATION_CACHE_PATH))
  {
    LittleFS.remove(TEMPERATURE_COMPENSATION_CACHE_TEMP_PATH);
    return false;
  }
  return true;
}

void serviceTemperatureCompensationPersistence()
{
  const uint32_t nowMs = millis();
  bool shouldSave = false;
  portENTER_CRITICAL(&temperatureCompensationMux);
  if (temperatureCompensationPersistPending &&
      (temperatureCompensationPersistUrgent || lastTemperatureCompensationPersistMs == 0 ||
       (uint32_t)(nowMs - lastTemperatureCompensationPersistMs) >=
           TEMPERATURE_COMPENSATION_PERSIST_INTERVAL_MS))
  {
    shouldSave = true;
    temperatureCompensationPersistPending = false;
    temperatureCompensationPersistUrgent = false;
  }
  portEXIT_CRITICAL(&temperatureCompensationMux);
  if (!shouldSave) return;

  if (saveTemperatureCompensationModels())
  {
    lastTemperatureCompensationPersistMs = nowMs;
  }
  else
  {
    portENTER_CRITICAL(&temperatureCompensationMux);
    temperatureCompensationPersistPending = true;
    temperatureCompensationPersistUrgent = true;
    portEXIT_CRITICAL(&temperatureCompensationMux);
  }
}

bool solveTemperatureCompensationNormal(const double normal[4][4], const double rhs[4],
                                        double solution[4])
{
  double augmented[4][5];
  for (uint8_t row = 0; row < 4; row++)
  {
    for (uint8_t column = 0; column < 4; column++) augmented[row][column] = normal[row][column];
    augmented[row][4] = rhs[row];
  }
  for (uint8_t pivot = 0; pivot < 4; pivot++)
  {
    uint8_t best = pivot;
    for (uint8_t row = pivot + 1; row < 4; row++)
    {
      if (fabs(augmented[row][pivot]) > fabs(augmented[best][pivot])) best = row;
    }
    if (fabs(augmented[best][pivot]) < 1e-10) return false;
    if (best != pivot)
    {
      for (uint8_t column = pivot; column < 5; column++)
      {
        double swap = augmented[pivot][column];
        augmented[pivot][column] = augmented[best][column];
        augmented[best][column] = swap;
      }
    }
    double divisor = augmented[pivot][pivot];
    for (uint8_t column = pivot; column < 5; column++) augmented[pivot][column] /= divisor;
    for (uint8_t row = 0; row < 4; row++)
    {
      if (row == pivot) continue;
      double factor = augmented[row][pivot];
      for (uint8_t column = pivot; column < 5; column++)
      {
        augmented[row][column] -= factor * augmented[pivot][column];
      }
    }
  }
  for (uint8_t row = 0; row < 4; row++) solution[row] = augmented[row][4];
  return true;
}

void resetTemperatureCompensationFit(TemperatureCompensationState& state, uint32_t nowMs)
{
  state.blockStartMs = nowMs;
  state.restoredBlockElapsedMs = 0;
  state.sampleCount = 0;
  state.minTempC = 1000.0f;
  state.maxTempC = -1000.0f;
  memset(state.normal, 0, sizeof(state.normal));
  memset(state.rhs, 0, sizeof(state.rhs));
  state.sumY = 0.0;
  state.sumYSquared = 0.0;
}

void resetTemperatureCompensationEpoch(
    TemperatureCompensationState& state, uint8_t sensorId, uint32_t nowMs,
    float boardTempC, float logRatio, uint32_t connectedCounts, uint32_t bareCounts,
    const char* reason)
{
  const uint32_t nextEpoch = state.epoch == UINT32_MAX ? 1 : state.epoch + 1;
  state = TemperatureCompensationState();
  state.gainPerC = temperatureCompensationSeedGain(sensorId);
  state.referenceTempC = temperatureCompensationSeedReferenceC(sensorId);
  state.phaseOffset = TEMPERATURE_COMPENSATION_SEED_PHASE_OFFSET;
  state.effectiveTempC = boardTempC;
  state.slowLogRatio = logRatio;
  state.emaReady = true;
  state.epoch = max(1UL, nextEpoch);
  state.lastUpdateMs = nowMs;
  state.lastConnectedCounts = connectedCounts;
  state.lastBareCounts = bareCounts;
  resetTemperatureCompensationFit(state, nowMs);
  if (reason != nullptr)
  {
    Serial.print("[Treebeard temp-comp] sensor=");
    Serial.print(sensorId);
    Serial.print(" new epoch=");
    Serial.print(state.epoch);
    Serial.print(" reason=");
    Serial.println(reason);
  }
}

TemperatureCompensationResult updateTemperatureCompensation(
    uint8_t sensorId, uint32_t connectedCounts, uint32_t bareCounts,
    int16_t boardTempQuarterC, uint8_t tankPhase, uint32_t nowMs,
    bool probeRestarted)
{
  TemperatureCompensationResult result;
  if (sensorId == 0 || sensorId > MAX_SENSOR_ID) return result;

  TemperatureCompensationState state;
  uint8_t configuredMode;
  portENTER_CRITICAL(&temperatureCompensationMux);
  state = temperatureCompensation[sensorId];
  configuredMode = temperatureCompensationMode[sensorId];
  portEXIT_CRITICAL(&temperatureCompensationMux);
  result.configuredMode = configuredMode;
  result.effectiveMode = configuredMode;
  result.gainPerC = state.gainPerC;
  result.referenceTempC = state.referenceTempC;
  result.epoch = state.epoch;
  if (configuredMode == TEMPERATURE_COMPENSATION_OFF) return result;

  const uint32_t maxFdcCount = 0x0FFFFFFFUL;
  if (connectedCounts == 0 || connectedCounts > maxFdcCount ||
      bareCounts == 0 || bareCounts > maxFdcCount || tankPhase > 1)
  {
    return result;
  }
  const float boardTempC = boardTempQuarterC / 4.0f;
  const float logRatio = 2.0f * (logf((float)bareCounts) - logf((float)connectedCounts));
  if (!isfinite(logRatio) || logRatio < -0.2f || logRatio > 1.0f) return result;

  const float connectedStep = state.lastConnectedCounts > 0
      ? fabsf((float)connectedCounts / state.lastConnectedCounts - 1.0f) : 0.0f;
  const float bareStep = state.lastBareCounts > 0
      ? fabsf((float)bareCounts / state.lastBareCounts - 1.0f) : 0.0f;
  const char* resetReason = nullptr;
  if (probeRestarted) resetReason = "probe-restart";
  else if (state.emaReady && connectedStep > TEMPERATURE_COMPENSATION_MAX_CONNECTED_STEP_FRACTION)
    resetReason = "connected-count-step";
  else if (state.emaReady && bareStep > TEMPERATURE_COMPENSATION_MAX_BARE_STEP_FRACTION)
    resetReason = "bare-count-step";

  bool epochResetThisUpdate = false;
  if (!state.emaReady || resetReason != nullptr)
  {
    resetTemperatureCompensationEpoch(
        state, sensorId, nowMs, boardTempC, logRatio, connectedCounts, bareCounts,
        resetReason);
    epochResetThisUpdate = resetReason != nullptr;
  }

  float deltaSeconds = 0.0f;
  if (state.lastUpdateMs != 0)
  {
    deltaSeconds = (uint32_t)(nowMs - state.lastUpdateMs) / 1000.0f;
    // A receiver that was suspended for days should rewarm gradually instead of treating the
    // entire outage as one perfectly observed interval.
    deltaSeconds = min(deltaSeconds, 6.0f * 60.0f * 60.0f);
  }
  state.lastUpdateMs = nowMs;
  if (deltaSeconds > 0.0f)
  {
    const float tempAlpha = 1.0f - expf(-deltaSeconds / TEMPERATURE_COMPENSATION_TEMP_TAU_SECONDS);
    const float moistureAlpha = 1.0f - expf(-deltaSeconds / TEMPERATURE_COMPENSATION_MOISTURE_TAU_SECONDS);
    state.effectiveTempC += tempAlpha * (boardTempC - state.effectiveTempC);
    state.slowLogRatio += moistureAlpha * (logRatio - state.slowLogRatio);
  }

  state.lastConnectedCounts = connectedCounts;
  state.lastBareCounts = bareCounts;
  if (state.blockStartMs == 0)
  {
    if (state.sampleCount > 0 && state.restoredBlockElapsedMs > 0)
    {
      // Unsigned subtraction deliberately re-anchors across millis()'s new boot epoch; the
      // subsequent unsigned difference recovers exactly the persisted observed duration.
      state.blockStartMs = nowMs - state.restoredBlockElapsedMs;
      state.restoredBlockElapsedMs = 0;
    }
    else
    {
      resetTemperatureCompensationFit(state, nowMs);
    }
  }
  const uint32_t blockElapsedMs = (uint32_t)(nowMs - state.blockStartMs);
  const double timeDays = blockElapsedMs / (double)TEMPERATURE_COMPENSATION_FIT_BLOCK_MS;
  const float slowExcess = expf(state.slowLogRatio) - 1.0f;
  const double feature[4] = {
    1.0,
    timeDays,
    slowExcess * (state.effectiveTempC - state.referenceTempC),
    (double)tankPhase,
  };
  for (uint8_t row = 0; row < 4; row++)
  {
    state.rhs[row] += feature[row] * logRatio;
    for (uint8_t column = 0; column < 4; column++)
    {
      state.normal[row][column] += feature[row] * feature[column];
    }
  }
  state.sumY += logRatio;
  state.sumYSquared += (double)logRatio * logRatio;
  state.sampleCount++;
  state.minTempC = min(state.minTempC, state.effectiveTempC);
  state.maxTempC = max(state.maxTempC, state.effectiveTempC);

  bool fitAcceptedThisUpdate = false;
  if (blockElapsedMs >= TEMPERATURE_COMPENSATION_FIT_BLOCK_MS)
  {
    double coefficients[4];
    bool accepted = false;
    if (state.sampleCount >= TEMPERATURE_COMPENSATION_MIN_FIT_SAMPLES &&
        state.maxTempC - state.minTempC >= TEMPERATURE_COMPENSATION_MIN_TEMP_SPAN_C &&
        solveTemperatureCompensationNormal(state.normal, state.rhs, coefficients))
    {
      double sse = state.sumYSquared;
      for (uint8_t i = 0; i < 4; i++) sse -= coefficients[i] * state.rhs[i];
      const double sst = state.sumYSquared - state.sumY * state.sumY / state.sampleCount;
      const double r2 = sst > 1e-10 ? 1.0 - max(0.0, sse) / sst : -1.0;
      const float candidateGain = (float)coefficients[2];
      const float candidatePhase = (float)coefficients[3];
      if (isfinite(candidateGain) && candidateGain >= TEMPERATURE_COMPENSATION_MIN_GAIN_PER_C &&
          candidateGain <= 0.04f &&
          isfinite(candidatePhase) && fabsf(candidatePhase) <= 0.01f &&
          r2 >= TEMPERATURE_COMPENSATION_MIN_FIT_R2)
      {
        // The first accepted fit is taken outright. Blending it 3:1 against the seed only makes
        // sense if the seed carries information, and since v3 the seed is deliberately zero.
        // Blending against zero would leave a probe applying a quarter of its own measured gain
        // for several days, which is a worse error than applying none.
        if (state.acceptedFits == 0)
        {
          state.gainPerC = candidateGain;
          state.phaseOffset = candidatePhase;
        }
        else
        {
          state.gainPerC = 0.75f * state.gainPerC + 0.25f * candidateGain;
          state.phaseOffset = 0.75f * state.phaseOffset + 0.25f * candidatePhase;
        }
        if (state.acceptedFits < 0xFF) state.acceptedFits++;
        accepted = true;
      }
      Serial.print("[Treebeard temp-comp] sensor=");
      Serial.print(sensorId);
      Serial.print(" fit r2=");
      Serial.print(r2, 3);
      Serial.print(" candidateGain=");
      Serial.print(candidateGain, 6);
      Serial.print(" accepted=");
      Serial.println(accepted ? "yes" : "no");
    }
    resetTemperatureCompensationFit(state, nowMs);
    fitAcceptedThisUpdate = accepted;
  }

  const float correctedLogRatio = logRatio -
      state.gainPerC * slowExcess * (state.effectiveTempC - state.referenceTempC) -
      state.phaseOffset * tankPhase;
  const float equivalentCounts = bareCounts / expf(correctedLogRatio * 0.5f);
  if (isfinite(correctedLogRatio) && correctedLogRatio >= 0.0f && correctedLogRatio <= 0.65534f &&
      isfinite(equivalentCounts) && equivalentCounts >= 1.0f && equivalentCounts <= maxFdcCount)
  {
    result.valid = true;
    result.logRatioPpm = (int32_t)lroundf(correctedLogRatio * 1000000.0f);
    result.equivalentMoisture = calibrateMsv3Moisture((uint32_t)lroundf(equivalentCounts));
  }
  const uint32_t progress = min(49UL,
      (uint32_t)(((uint64_t)(nowMs - state.blockStartMs) * 50ULL) /
                 TEMPERATURE_COMPENSATION_FIT_BLOCK_MS));
  result.confidencePercent = (uint8_t)min(100UL,
      (uint32_t)state.acceptedFits * 50UL + progress);
  if (configuredMode == TEMPERATURE_COMPENSATION_ACTIVE &&
      state.acceptedFits < TEMPERATURE_COMPENSATION_REQUIRED_FITS)
  {
    result.effectiveMode = TEMPERATURE_COMPENSATION_LEARNING;
  }
  result.gainPerC = state.gainPerC;
  result.referenceTempC = state.referenceTempC;
  result.epoch = state.epoch;

  portENTER_CRITICAL(&temperatureCompensationMux);
  temperatureCompensation[sensorId] = state;
  temperatureCompensationPersistPending = true;
  if (fitAcceptedThisUpdate || epochResetThisUpdate) temperatureCompensationPersistUrgent = true;
  portEXIT_CRITICAL(&temperatureCompensationMux);
  return result;
}

bool validHistoryJournalRecord(const HistoryJournalRecord& record)
{
  return record.magic == HISTORY_RECORD_MAGIC &&
         record.version == HISTORY_JOURNAL_VERSION &&
         record.payloadSize == sizeof(PendingHistoryReading) &&
         record.checksum == historyChecksum(&record, offsetof(HistoryJournalRecord, checksum));
}

bool validHistoryJournalState(const HistoryJournalState& state)
{
  return state.magic == HISTORY_STATE_MAGIC &&
         state.version == HISTORY_JOURNAL_VERSION &&
         state.size == sizeof(HistoryJournalState) &&
         state.checksum == historyChecksum(&state, offsetof(HistoryJournalState, checksum));
}

bool readHistoryJournalState(const char* path, HistoryJournalState& state)
{
  File file = LittleFS.open(path, FILE_READ);
  if (!file)
  {
    return false;
  }
  const bool complete = file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) == sizeof(state);
  file.close();
  return complete && validHistoryJournalState(state);
}

bool persistHistoryJournalState()
{
  HistoryJournalState state = {};
  state.magic = HISTORY_STATE_MAGIC;
  state.version = HISTORY_JOURNAL_VERSION;
  state.size = sizeof(state);
  state.generation = historyJournalStateGeneration + 1;
  portENTER_CRITICAL(&historyQueueMux);
  state.headOffset = historyJournalHeadOffset;
  state.dropped = pendingHistoryDropped;
  state.highWaterMark = pendingHistoryHighWaterMark;
  portEXIT_CRITICAL(&historyQueueMux);
  state.checksum = historyChecksum(&state, offsetof(HistoryJournalState, checksum));

  const char* path = (state.generation & 1U) != 0 ? HISTORY_STATE_A_PATH : HISTORY_STATE_B_PATH;
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file)
  {
    historyJournalStorageErrors++;
    return false;
  }
  const bool complete = file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state)) == sizeof(state);
  file.flush();
  file.close();
  if (!complete)
  {
    historyJournalStorageErrors++;
    return false;
  }

  HistoryJournalState verified = {};
  if (!readHistoryJournalState(path, verified) || verified.generation != state.generation)
  {
    historyJournalStorageErrors++;
    return false;
  }
  historyJournalStateGeneration = state.generation;
  return true;
}

bool replaceHistoryJournalPrefix(uint32_t validBytes)
{
  File source = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_READ);
  File compact = LittleFS.open(HISTORY_JOURNAL_COMPACT_PATH, FILE_WRITE);
  if (!compact)
  {
    if (source) source.close();
    return false;
  }

  uint8_t buffer[256];
  uint32_t copied = 0;
  bool ok = true;
  while (source && copied < validBytes)
  {
    size_t wanted = min((size_t)(validBytes - copied), sizeof(buffer));
    size_t got = source.read(buffer, wanted);
    if (got != wanted || compact.write(buffer, got) != got)
    {
      ok = false;
      break;
    }
    copied += got;
  }
  if (source) source.close();
  compact.flush();
  compact.close();
  if (!ok || copied != validBytes || !LittleFS.rename(HISTORY_JOURNAL_COMPACT_PATH, HISTORY_JOURNAL_PATH))
  {
    LittleFS.remove(HISTORY_JOURNAL_COMPACT_PATH);
    return false;
  }
  return true;
}

bool initializeHistoryJournal()
{
  historyJournalReady = false;

  HistoryJournalState stateA = {};
  HistoryJournalState stateB = {};
  const bool hasA = readHistoryJournalState(HISTORY_STATE_A_PATH, stateA);
  const bool hasB = readHistoryJournalState(HISTORY_STATE_B_PATH, stateB);
  HistoryJournalState recoveredState = {};
  if (hasA && (!hasB || (int32_t)(stateA.generation - stateB.generation) > 0))
  {
    recoveredState = stateA;
  }
  else if (hasB)
  {
    recoveredState = stateB;
  }

  if (!LittleFS.exists(HISTORY_JOURNAL_PATH))
  {
    File created = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_WRITE);
    if (!created)
    {
      historyJournalStorageErrors++;
      return false;
    }
    created.close();
  }

  File file = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_READ);
  if (!file)
  {
    historyJournalStorageErrors++;
    return false;
  }
  const uint32_t fileBytes = (uint32_t)file.size();
  uint32_t validBytes = 0;
  HistoryJournalRecord record = {};
  while (validBytes + sizeof(record) <= fileBytes)
  {
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record) ||
        !validHistoryJournalRecord(record))
    {
      break;
    }
    validBytes += sizeof(record);
  }
  file.close();

  if (validBytes != fileBytes)
  {
    Serial.print("[Treebeard history] removing torn journal tail bytes=");
    Serial.println(fileBytes - validBytes);
    if (!replaceHistoryJournalPrefix(validBytes))
    {
      historyJournalStorageErrors++;
      return false;
    }
  }

  uint32_t recoveredHead = (hasA || hasB) ? recoveredState.headOffset : 0;
  if (recoveredHead > validBytes || recoveredHead % sizeof(HistoryJournalRecord) != 0)
  {
    // Replaying from zero is lossless and safe because observation ids are idempotent.
    recoveredHead = 0;
  }
  portENTER_CRITICAL(&historyQueueMux);
  historyJournalHeadOffset = recoveredHead;
  historyJournalValidBytes = validBytes;
  durableHistoryCount = (validBytes - recoveredHead) / sizeof(HistoryJournalRecord);
  pendingHistoryDropped = (hasA || hasB) ? recoveredState.dropped : pendingHistoryDropped;
  pendingHistoryHighWaterMark = max((uint32_t)(durableHistoryCount + pendingHistoryCount),
                                    (hasA || hasB) ? recoveredState.highWaterMark : 0U);
  portEXIT_CRITICAL(&historyQueueMux);
  historyJournalStateGeneration = (hasA || hasB) ? recoveredState.generation : 0;
  historyJournalReady = true;
  if (!(hasA || hasB))
  {
    persistHistoryJournalState();
  }
  Serial.print("[Treebeard history] durable outbox ready queued=");
  Serial.print(durableHistoryCount);
  Serial.print(" bytes=");
  Serial.println(validBytes);
  return true;
}

size_t queuedHistoryCount()
{
  portENTER_CRITICAL(&historyQueueMux);
  size_t count = durableHistoryCount + pendingHistoryCount;
  portEXIT_CRITICAL(&historyQueueMux);
  return count;
}

uint32_t droppedHistoryCount()
{
  portENTER_CRITICAL(&historyQueueMux);
  uint32_t count = pendingHistoryDropped;
  portEXIT_CRITICAL(&historyQueueMux);
  return count;
}

uint32_t historyQueueHighWaterMark()
{
  portENTER_CRITICAL(&historyQueueMux);
  uint32_t mark = pendingHistoryHighWaterMark;
  portEXIT_CRITICAL(&historyQueueMux);
  return mark;
}

// Preserve the v1 journal record size: rawMoistureKHz is otherwise unused for wide/MSV3 rows,
// so it carries a compact shadow-channel value while queued. Zero remains the legacy/absent
// sentinel; one count represents 10 log-ratio ppm, covering the physically useful 0..655,340
// ppm range without invalidating already-journaled rows after a firmware update.
uint16_t encodeTemperatureCompensatedLogRatioPpm(int32_t ppm)
{
  int32_t quantized = (ppm + 5) / 10;
  quantized = constrain(quantized, 0L, 65534L);
  return (uint16_t)(quantized + 1);
}

bool decodeTemperatureCompensatedLogRatioPpm(uint16_t encoded, int32_t& ppm)
{
  if (encoded == 0)
  {
    return false;
  }
  ppm = (int32_t)(encoded - 1U) * 10;
  return true;
}

bool enqueueHistoryReading(uint8_t sensorId, const SensorReading& reading)
{
  PendingHistoryReading pending;
  pending.bootId = historyQueueBootId;
  pending.capturedMs = reading.lastSeenMs;
  // Once any authenticated cloud response supplies server time, later records carry an
  // absolute timestamp. That remains correct across a reset; pre-anchor rows retain the
  // legacy queued-age fallback and are never assigned an invented wall-clock value.
  portENTER_CRITICAL(&historyQueueMux);
  if (cloudTimeAnchorEpochMs != 0)
  {
    pending.observedAtMs = cloudTimeAnchorEpochMs +
                           (uint32_t)(reading.lastSeenMs - cloudTimeAnchorMillis);
  }
  portEXIT_CRITICAL(&historyQueueMux);
  pending.wake = reading.wake;
  pending.rawFdcCounts = reading.rawFdcCounts;
  if (reading.msv4)
  {
    pending.rawFdcCounts |= ((uint32_t)reading.msv4FitRmsE4 << 24);
  }
  pending.moisture = reading.moisture;
  pending.measuredMoisture = reading.measuredMoisture;
  pending.rawMoistureKHz = reading.wideRaw && reading.temperatureCompensationValid
      ? encodeTemperatureCompensatedLogRatioPpm(reading.temperatureCompensatedLogRatioPpm)
      : reading.rawMoistureKHz;
  if (reading.msv4)
  {
    pending.rawMoistureKHz = msv4EncodeDieTemp(reading.msv4DieTempValid, reading.msv4DieTempQuarterC);
  }
  pending.rawBatteryMv = reading.rawBatteryMv;
  pending.rawCharging = reading.rawCharging;
  pending.rssi = (int16_t)constrain(reading.rssi, -32768, 32767);
  pending.sensorId = sensorId;
  pending.moistureScale = moistureScaleVersion(reading.wideRaw, reading.caplessTank, reading.msv4);
  pending.batteryPercent = reading.batteryPercent;
  pending.hasRaw = reading.hasRaw;
  pending.wideRaw = reading.wideRaw;
  pending.hasMsv3Diagnostics = reading.hasMsv3Diagnostics;
  pending.boardTempValid = reading.boardTempValid;
  pending.rawBoardTempQuarterC = reading.rawBoardTempQuarterC;
  pending.bareTankValid = reading.bareTankValid;
  pending.rawBareTankFdcCounts = reading.rawBareTankFdcCounts;
  pending.tankPhase = reading.tankPhase;

  portENTER_CRITICAL(&historyQueueMux);
  if (durableHistoryCount + pendingHistoryCount >= HISTORY_QUEUE_CAPACITY ||
      pendingHistoryCount >= HISTORY_STAGING_CAPACITY)
  {
    pendingHistoryDropped++;
    portEXIT_CRITICAL(&historyQueueMux);
    return false;
  }

  pending.sequence = ++pendingHistorySequence;
  if (pendingHistoryCount == 0)
  {
    pendingHistoryOldestStagedAtMs = reading.lastSeenMs;
  }
  uint8_t tail = (pendingHistoryHead + pendingHistoryCount) % HISTORY_STAGING_CAPACITY;
  pendingHistory[tail] = pending;
  pendingHistoryCount++;
  const size_t totalQueued = durableHistoryCount + pendingHistoryCount;
  if (totalQueued > pendingHistoryHighWaterMark)
  {
    pendingHistoryHighWaterMark = totalQueued;
  }
  checkpointHistoryStagingToRtcLocked();
  portEXIT_CRITICAL(&historyQueueMux);
  return true;
}

void serviceHistoryJournal()
{
  if (!historyJournalReady && !initializeHistoryJournal())
  {
    return;
  }

  const uint32_t now = millis();
  size_t pendingCountSnapshot = 0;
  uint32_t oldestCapturedMs = 0;
  portENTER_CRITICAL(&historyQueueMux);
  pendingCountSnapshot = pendingHistoryCount;
  if (pendingCountSnapshot > 0)
  {
    oldestCapturedMs = pendingHistoryOldestStagedAtMs;
  }
  portEXIT_CRITICAL(&historyQueueMux);
  if (pendingCountSnapshot == 0)
  {
    return;
  }

  const bool capacityEmergency =
      pendingCountSnapshot >= HISTORY_JOURNAL_EMERGENCY_RECORDS;
  const bool ageDue = (uint32_t)(now - oldestCapturedMs) >= HISTORY_JOURNAL_MAX_STAGING_MS;
  if (!capacityEmergency &&
      pendingCountSnapshot < HISTORY_JOURNAL_FLUSH_RECORDS && !ageDue)
  {
    return;
  }

#if TREEBEARD_ENABLE_DISPLAY
  // A normal commit can wait for a quiet display/touch window. Capacity emergencies cannot:
  // dropping a row is worse than one bounded pause, and the larger RTC staging ring makes
  // this branch exceptional rather than the steady-state path.
  if (!capacityEmergency &&
      (touchAcquisitionPending ||
       (uint32_t)(now - lastDisplaySpiActivityMs) < HISTORY_FLASH_DISPLAY_QUIET_MS))
  {
    return;
  }
#endif

  PendingHistoryReading staged[HISTORY_JOURNAL_WRITE_BATCH_SIZE];
  size_t stagedCount = 0;
  portENTER_CRITICAL(&historyQueueMux);
  stagedCount = min((size_t)pendingHistoryCount, HISTORY_JOURNAL_WRITE_BATCH_SIZE);
  for (size_t i = 0; i < stagedCount; i++)
  {
    staged[i] = pendingHistory[(pendingHistoryHead + i) % HISTORY_STAGING_CAPACITY];
  }
  portEXIT_CRITICAL(&historyQueueMux);
  if (stagedCount == 0)
  {
    return;
  }

  uint32_t writeStartMs = millis();
  File file = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_APPEND);
  if (!file)
  {
    historyJournalStorageErrors++;
    historyJournalReady = false;
    return;
  }
  size_t written = 0;
  for (; written < stagedCount; written++)
  {
    HistoryJournalRecord record = {};
    record.magic = HISTORY_RECORD_MAGIC;
    record.version = HISTORY_JOURNAL_VERSION;
    record.payloadSize = sizeof(PendingHistoryReading);
    record.reading = staged[written];
    record.checksum = historyChecksum(&record, offsetof(HistoryJournalRecord, checksum));
    if (file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) != sizeof(record))
    {
      break;
    }
  }
  file.flush();
  file.close();
  lastJournalWriteAtMs = millis();
  lastJournalWriteDurationMs = lastJournalWriteAtMs - writeStartMs;
  lastJournalWriteRecords = written;
  lastJournalCompactedThisWrite = false;

  portENTER_CRITICAL(&historyQueueMux);
  size_t removed = 0;
  while (removed < written && pendingHistoryCount > 0 &&
         pendingHistory[pendingHistoryHead].bootId == staged[removed].bootId &&
         pendingHistory[pendingHistoryHead].sequence == staged[removed].sequence)
  {
    pendingHistoryHead = (pendingHistoryHead + 1) % HISTORY_STAGING_CAPACITY;
    pendingHistoryCount--;
    durableHistoryCount++;
    historyJournalValidBytes += sizeof(HistoryJournalRecord);
    removed++;
  }
  if (removed > 0)
  {
    pendingHistoryOldestStagedAtMs = pendingHistoryCount > 0 ? millis() : 0;
  }
  const size_t totalQueued = durableHistoryCount + pendingHistoryCount;
  if (totalQueued > pendingHistoryHighWaterMark)
  {
    pendingHistoryHighWaterMark = totalQueued;
  }
  checkpointHistoryStagingToRtcLocked();
  portEXIT_CRITICAL(&historyQueueMux);

  if (written != stagedCount || removed != written)
  {
    historyJournalStorageErrors++;
    // A short write can leave a partial tail. Startup recovery is reused on the next tick
    // before another append, preventing corruption from ever being buried mid-file.
    historyJournalReady = false;
  }
}

size_t copyHistoryUploadBatch(PendingHistoryReading* destination, size_t capacity)
{
  if (destination == nullptr || capacity == 0)
  {
    return 0;
  }

  size_t durableAvailable = 0;
  uint32_t headOffset = 0;
  portENTER_CRITICAL(&historyQueueMux);
  durableAvailable = durableHistoryCount;
  headOffset = historyJournalHeadOffset;
  portEXIT_CRITICAL(&historyQueueMux);
  const size_t durableWanted = min(durableAvailable, capacity);

  size_t copied = 0;
  if (durableWanted > 0)
  {
    if (!historyJournalReady)
    {
      return 0;
    }
    File file = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_READ);
    if (!file || !file.seek(headOffset))
    {
      if (file) file.close();
      historyJournalStorageErrors++;
      return 0;
    }
    for (; copied < durableWanted; copied++)
    {
      HistoryJournalRecord record = {};
      if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record) ||
          !validHistoryJournalRecord(record))
      {
        historyJournalStorageErrors++;
        break;
      }
      destination[copied] = record.reading;
    }
    file.close();
  }

  // Append an immutable snapshot of the RTC-backed ingress prefix. New BLE readings can join
  // the tail during HTTPS without changing any row copied here; acknowledgement below removes
  // only exact bootId/sequence matches from the head.
  portENTER_CRITICAL(&historyQueueMux);
  const size_t stagedWanted = min((size_t)pendingHistoryCount, capacity - copied);
  for (size_t i = 0; i < stagedWanted; i++)
  {
    destination[copied + i] =
        pendingHistory[(pendingHistoryHead + i) % HISTORY_STAGING_CAPACITY];
  }
  portEXIT_CRITICAL(&historyQueueMux);
  return copied + stagedWanted;
}

bool compactHistoryJournal()
{
  if (!historyJournalReady)
  {
    return false;
  }
  size_t remaining = 0;
  uint32_t oldHead = 0;
  portENTER_CRITICAL(&historyQueueMux);
  remaining = durableHistoryCount;
  oldHead = historyJournalHeadOffset;
  portEXIT_CRITICAL(&historyQueueMux);

  File source = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_READ);
  File compact = LittleFS.open(HISTORY_JOURNAL_COMPACT_PATH, FILE_WRITE);
  if (!source || !compact || !source.seek(oldHead))
  {
    if (source) source.close();
    if (compact) compact.close();
    LittleFS.remove(HISTORY_JOURNAL_COMPACT_PATH);
    historyJournalStorageErrors++;
    return false;
  }

  uint8_t buffer[256];
  const uint32_t bytesToCopy = remaining * sizeof(HistoryJournalRecord);
  uint32_t copied = 0;
  bool ok = true;
  while (copied < bytesToCopy)
  {
    size_t wanted = min((size_t)(bytesToCopy - copied), sizeof(buffer));
    size_t got = source.read(buffer, wanted);
    if (got != wanted || compact.write(buffer, got) != got)
    {
      ok = false;
      break;
    }
    copied += got;
  }
  source.close();
  compact.flush();
  compact.close();
  if (!ok || copied != bytesToCopy)
  {
    LittleFS.remove(HISTORY_JOURNAL_COMPACT_PATH);
    historyJournalStorageErrors++;
    return false;
  }

  // Persisting head=0 first can at worst replay rows if power fails before rename. Reversing
  // the order could point past the compacted file and lose rows, so the ordering matters.
  portENTER_CRITICAL(&historyQueueMux);
  historyJournalHeadOffset = 0;
  portEXIT_CRITICAL(&historyQueueMux);
  if (!persistHistoryJournalState() ||
      !LittleFS.rename(HISTORY_JOURNAL_COMPACT_PATH, HISTORY_JOURNAL_PATH))
  {
    portENTER_CRITICAL(&historyQueueMux);
    historyJournalHeadOffset = oldHead;
    portEXIT_CRITICAL(&historyQueueMux);
    persistHistoryJournalState();
    LittleFS.remove(HISTORY_JOURNAL_COMPACT_PATH);
    historyJournalStorageErrors++;
    return false;
  }
  historyJournalValidBytes = bytesToCopy;
  return true;
}

// Only acknowledge the exact prefix copied for this request. New BLE readings may be
// appended while HTTPS is in flight, but they never move the head and therefore remain
// queued for the next batch.
size_t acknowledgeHistoryUploadBatch(const PendingHistoryReading* uploaded, size_t count)
{
  if (uploaded == nullptr || count == 0)
  {
    return 0;
  }

  size_t durableToAcknowledge = 0;
  uint32_t durableHeadOffset = 0;
  portENTER_CRITICAL(&historyQueueMux);
  durableToAcknowledge = min(durableHistoryCount, count);
  durableHeadOffset = historyJournalHeadOffset;
  portEXIT_CRITICAL(&historyQueueMux);

  size_t durableAcknowledged = 0;
  if (durableToAcknowledge > 0)
  {
    if (!historyJournalReady)
    {
      return 0;
    }
    File file = LittleFS.open(HISTORY_JOURNAL_PATH, FILE_READ);
    if (!file || !file.seek(durableHeadOffset))
    {
      if (file) file.close();
      historyJournalStorageErrors++;
      return 0;
    }
    while (durableAcknowledged < durableToAcknowledge)
    {
      HistoryJournalRecord record = {};
      if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record) ||
          !validHistoryJournalRecord(record) ||
          record.reading.bootId != uploaded[durableAcknowledged].bootId ||
          record.reading.sequence != uploaded[durableAcknowledged].sequence)
      {
        break;
      }
      durableAcknowledged++;
    }
    file.close();
    if (durableAcknowledged != durableToAcknowledge)
    {
      historyJournalStorageErrors++;
      return 0;
    }
  }

  portENTER_CRITICAL(&historyQueueMux);
  historyJournalHeadOffset += durableAcknowledged * sizeof(HistoryJournalRecord);
  durableHistoryCount -= min(durableHistoryCount, durableAcknowledged);
  portEXIT_CRITICAL(&historyQueueMux);
  if (durableAcknowledged > 0)
  {
    persistHistoryJournalState();
    const bool queueEmpty = durableHistoryCount == 0;
    if (historyJournalHeadOffset >= HISTORY_JOURNAL_COMPACT_HEAD_BYTES &&
        (queueEmpty || historyJournalHeadOffset >= historyJournalValidBytes / 2U))
    {
      uint32_t compactStartMs = millis();
      compactHistoryJournal();
      lastJournalCompactDurationMs = millis() - compactStartMs;
      lastJournalCompactedThisWrite = true;
    }
  }

  size_t stagedAcknowledged = 0;
  const size_t stagedUploaded = count - durableAcknowledged;
  portENTER_CRITICAL(&historyQueueMux);
  while (stagedAcknowledged < stagedUploaded && pendingHistoryCount > 0)
  {
    const PendingHistoryReading& pending = pendingHistory[pendingHistoryHead];
    const PendingHistoryReading& accepted = uploaded[durableAcknowledged + stagedAcknowledged];
    if (pending.bootId != accepted.bootId || pending.sequence != accepted.sequence)
    {
      break;
    }
    pendingHistoryHead = (pendingHistoryHead + 1) % HISTORY_STAGING_CAPACITY;
    pendingHistoryCount--;
    stagedAcknowledged++;
  }
  if (pendingHistoryCount == 0)
  {
    pendingHistoryOldestStagedAtMs = 0;
  }
  checkpointHistoryStagingToRtcLocked();
  portEXIT_CRITICAL(&historyQueueMux);
  return durableAcknowledged + stagedAcknowledged;
}

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
  bool calibrated = false; // bounds are auto-cal-derived (ready), not manual/preset
  bool region = false; // ...and they are the cloud's water-now region (AutoCalState::region)
  bool rangeTrendWarn = false; // dry bound has been trending lower across recent windows
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
  int16_t lastMoisturePct = 0;
  bool lastMoistureCalibrated = false;
  bool lastMoistureRegion = false;
  uint16_t lastMoistureColor = 0;
  uint8_t lastBatteryPct = 0;
  // MSV3-only, nullable - a probe without board-temp diagnostics (every MSV2 sensor, or an
  // MSV3 still on pre-diagnostics firmware) never sets boardTempValid, so this row simply
  // never appears for those cards. lastBoardTempInitialized is separate from
  // metricsInitialized so a probe whose validity flips (diagnostics firmware flashed mid-
  // session) is picked up as a real change rather than staying stuck on whatever the first
  // draw happened to see.
  bool lastBoardTempInitialized = false;
  bool lastBoardTempValid = false;
  int16_t lastBoardTempQuarterC = 0;
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
  bool lastDetailRangeTrendWarn = false;
  char lastDetailRawText[16] = "";
  bool lastDetailCharging = false;
  // See DETAIL_FULL_RESYNC_MS's own comment - set whenever this slot's detail view is
  // (re)opened, checked by renderDashboardDataOnly() to periodically force a full (not
  // data-only) redraw as a self-healing backstop against a stuck partial repaint.
  uint32_t nextDetailFullResyncMs = 0;
};

// All the formatted text/colors the detail view needs, computed once and shared by both
// drawPlantCardDetail() (full paint) and drawPlantCardDetailDataOnly() (diff-and-redraw)
// so the two can never drift out of sync on what a given row's "current value" is. Defined
// up here (not next to computeDetailStats()/drawPlantCardDetail() where it's actually used)
// for the same reason as the DashboardGrid/PlantArtSlot prototypes below - see that comment.
// Room for a card's moisture text: "WATER NOW" (formatMoistureText()) is the longest.
const size_t MOISTURE_TEXT_SIZE = 12;

struct PlantDetailStats
{
  bool stale;
  int16_t moisturePct;
  uint8_t batteryPct;
  uint16_t moistureColor;
  uint16_t batteryColor;
  uint16_t rawMoisture;
  char moistureText[MOISTURE_TEXT_SIZE];
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
void restoreCardsFromCache();
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

#if TREEBEARD_ENABLE_DISPLAY
#define tft (*gfxPtr)
// gfxPtr's declared type is the base Arduino_GFX, which doesn't expose writeAddrWindow()/
// writePixels() (those are added by the Arduino_TFT subclass); gfxPtr is always actually
// constructed as an Arduino_ST7796, which derives from Arduino_TFT, so this cast is safe.
#define tftBus (*static_cast<Arduino_TFT*>(gfxPtr))
#endif
#if TREEBEARD_ENABLE_WIFI
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
// Separate per-subsystem checkpoints (2026-08-23 telemetry) - the original lastCheckpoint
// above only ever recorded boot/setup and the main-loop render path (verified: every prior
// call site is in setup() or the render section of loop()), so a crash inside the BLE-scan
// or cloud-sync tasks - which run on their own FreeRTOS tasks - left no trace distinct from
// whatever render happened to be doing. All three survive a reset (RTC memory), same as
// lastCheckpoint, but not a full power loss - see the one-time LittleFS boot record in
// setup() for that case.
RTC_DATA_ATTR char lastCheckpointBle[48] = "fresh boot";
RTC_DATA_ATTR char lastCheckpointCloud[48] = "fresh boot";
// Set immediately before every ESP.restart() call site (WiFi-retry-exhausted, pending
// config-change) and consumed/cleared at the top of the next setup(), so a genuine
// crash/watchdog/brownout reset can be told apart from a deliberate one in /device.
RTC_DATA_ATTR bool intentionalRestartPending = false;
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
// Each network function owns a short-lived client. A retained client did avoid repeated
// allocator churn, but also retained tens of kilobytes of TLS state and caused live resets.
// Error-level ESP32-core logging later established the actual snapshot failure: the compact
// JSON String was allocated before mbedtls_ssl_setup(), which then returned
// MBEDTLS_ERR_SSL_ALLOC_FAILED. The uploader below now connects TLS before allocating that
// String; local ownership then releases the session at the end of the single paced request.
// See docs/RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md.
bool cloudEnabled = false;
char cloudUploadUrl[160] = "";
char cloudApiToken[80] = "";
uint32_t cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
uint32_t nextCloudSyncMs = 0;
uint32_t nextCloudFullSnapshotMs = 0;
uint32_t nextCloudNetworkSlotMs = 0;
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
// 2026-08-23 telemetry (diagnostic only, no behavior change). Plain (non-RTC) globals reset
// each boot on purpose - these describe the CURRENT session's memory health, not something
// that needs to survive a reset. minFreeHeapEverBytes starts at UINT32_MAX so the first
// loop()/cloud-task sample always records a real value.
uint32_t minFreeHeapEverBytes = UINT32_MAX;
// 2026-09-02: smallest free loopTask stack seen this boot. Tracked rather than only sampled
// on demand because /device's own handler runs on loopTask at a shallower depth than the
// paths we actually care about (sensorDataJson()'s 8KB document), so an on-demand read alone
// would systematically under-report the real peak.
uint32_t minLoopTaskStackFreeEverBytes = UINT32_MAX;
uint32_t lastCloudUploadFreeHeapBeforeBytes = 0;
uint32_t lastCloudUploadFreeHeapAfterBytes = 0;
uint16_t cloudConsecutiveUploadFailures = 0;
uint8_t currentRotation = 1;
uint32_t lastArtFrameMs = 0;
const uint16_t ART_FRAME_DEFAULT_MS = 500;
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
// 2026-08-30 gesture redesign - see TOUCH_LONG_PRESS_MS's own comment. touchDownStartMs marks
// when the CURRENT contact began (reset on every new touch-down); longPressFired latches once
// the long-press action has fired for this contact, so it can't also fire the ordinary tap
// action on release, and can't fire more than once while still held.
uint32_t touchDownStartMs = 0;
bool longPressFired = false;
// 2026-08-30, same-day follow-up: a raw "not touched" sample on release is not trusted until
// it holds for TOUCH_RELEASE_STABLE_MS - see that constant's own comment for why (contact
// bounce on this resistive panel can otherwise fire the tap/collapse action 2-3 times for one
// physical press once the old 650ms deferral, which absorbed this for free, was removed).
uint32_t releaseCandidateSinceMs = 0;
bool releaseCandidatePending = false;
// 2026-08-30 IRQ-primary redesign - see TOUCH_POST_SPI_BLACKOUT_MS's comment. touchPendingConfirm
// is true for the single poll between "T_IRQ just read LOW" and "spent this gesture's one SPI
// read" - lets touchDownStartMs (above) start ticking from the very first LOW sample while still
// deferring the actual touchScreen.getPoint() call by one poll interval, so it isn't taken on a
// poorly-settled first-contact sample. lastTouchSpiMs is the timestamp of the most recent real
// SPI transaction (initial press or a drag sample) - both the down and release sides of
// handleTouchInput() check against it before trusting a fresh T_IRQ read.
bool touchPendingConfirm = false;
uint32_t lastTouchSpiMs = 0;
// 2026-08-30 bidirectional bus-turnaround guard, phone-a-friend consultation
// "display-touch-corruption-2026-08-30": the IRQ-primary redesign above cut a plain tap to
// exactly one touch SPI transaction, which reduced (but per the user's field report, did not
// eliminate - roughly 1/10 taps) the card-art corruption. TOUCH_POST_SPI_BLACKOUT_MS only ever
// protected touch's own recent SPI activity; it said nothing about how recently the DISPLAY side
// had written something, and drawPlantSpriteComposited()/renderDashboardDataOnly() keep writing
// on their own independent cadence regardless of touch state. touchAcquisitionPending, set the
// moment T_IRQ first reads LOW, tells loop() to skip its three periodic display-writing calls
// (renderDashboardDataOnly/renderAnimatedPlantArt/renderUi) so the gesture's one unavoidable
// coordinate read never lands immediately adjacent to an unrelated TFT burst - see
// handleTouchInput()'s use of it and loop()'s three gated call sites. lastDisplaySpiActivityMs is
// stamped by loop() right after each of those calls runs, so handleTouchInput() can require
// quiet on both sides (not just its own) before spending the read.
//
// 2026-08-31 follow-up, phone-a-friend consultation "treebeard-slider-fix-20260831": the
// brightness slider was still visibly glitchy in the field after the above. The paragraph
// this replaces was half right - loop() DOES skip all three periodic writers unconditionally
// while brightnessOverlayOpen is true, so no THIRD-PARTY display traffic can land next to a
// slider's touch read (see loop()'s own `if (brightnessOverlayOpen) { ...; return; }` gate).
// But it wrongly concluded from that alone that the drag path didn't need its own turnaround
// guard - every slider sample (the first one, embedded in the press-classification block, and
// every drag-continuation sample after it) called previewTouchBrightness() to paint the new
// knob position in the SAME poll as the touch SPI read that produced it, with no gap at all.
// Since TOUCH_POST_SPI_BLACKOUT_MS == TOUCH_POLL_INTERVAL_MS, that read-then-immediate-write
// adjacency repeated on every single poll for as long as a finger was moving on the slider -
// the exact adjacency condition already proven to corrupt the display for taps, just recurring
// dozens of times per drag instead of once per tap. See sliderPaintPending below for the fix:
// split the read and its paint across two separate polls so they're never adjacent, reusing
// this same proven blackout window symmetrically instead of guessing a smaller number with no
// bench/scope evidence to justify it.
volatile bool touchAcquisitionPending = false;
volatile uint32_t lastDisplaySpiActivityMs = 0;
// Set true the instant a long-press opens the overlay; blocks that SAME held contact from
// also being read as a slider-drag on the panel that just appeared under it. Reset on the
// next fresh touch-down.
bool overlayOpenedThisGesture = false;
// 2026-08-30 latched-target fix, phone-a-friend consultation "display-touch-corruption-
// 2026-08-30": field-reported twice - rapid-mashing the brightness "-" button could close the
// overlay instead of decrementing. Root cause (per that consultation): TOUCH_RELEASE_STABLE_MS
// merges any re-press arriving within 160ms of a lift into the SAME ongoing gesture (needed to
// absorb this panel's own documented ~120ms release bounce - see that constant's comment), so
// mashing faster than that never actually completes a release until the user stops entirely.
// While merged, the old code kept re-sampling touch coordinates every poll and overwriting
// lastTouchX/lastTouchY unconditionally (even while sitting on a button, not dragging) - normal
// touch jitter across many rapid re-samples could drift that cached position outside the
// minus button's hitbox (only 4px inset from the modal's own left edge) by the time the merged
// gesture finally released, misfiring the release handler's outside-tap-closes catch-all.
// overlayTouchTarget is classified exactly ONCE, from the gesture's single confirmed press
// coordinate (see touchPendingConfirm's use of it), and never reclassified from a later,
// possibly-drifted sample - the release handler dispatches on this latched target instead of
// re-deriving one from lastTouchX/lastTouchY.
enum class OverlayTouchTarget : uint8_t { None, Minus, Plus, Slider, Screen, Sound, Close };
OverlayTouchTarget overlayTouchTarget = OverlayTouchTarget::None;
// True from the moment a slider touch read (the confirmed press itself, or a later drag
// sample) has a coordinate waiting to be painted, until previewTouchBrightness() actually
// paints it - see the 2026-08-31 comment on lastDisplaySpiActivityMs above for why this
// exists: it lets the read and its paint land on two different polls instead of one, so
// they're never SPI-adjacent. lastTouchX/lastTouchY carry the pending value - no separate
// storage needed. Reset false on every new confirmed press and on release, so a gesture that
// ends between its read and its paint just silently drops that last sample (displaySettings
// stays at whatever was last actually painted) instead of leaking into the next gesture.
bool sliderPaintPending = false;
// 2026-08-30 ISOLATION TEST, phone-a-friend consultation "display-touch-corruption-2026-08-30":
// DisplayCenter/docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md's "Not yet tried" section
// flagged this as the highest-value untried diagnostic and it was never actually run before two
// more rounds of gesture-timing fixes (which only changed touch SPI traffic volume, not whether
// touch SPI traffic is the actual trigger). When true, handleTouchInput() returns before ever
// calling touchScreen.touched()/getPoint() - zero XPT2046 SPI transactions during a touch
// gesture, not just idle-suppressed ones. Taps/holds do nothing while this is true; that is the
// point of the test. touchSpiTransactionCount (exposed on /device) must stay at 0 for the whole
// test run - if it doesn't, something outside handleTouchInput() is also calling the touch
// driver and the isolation is not clean. Revert to false once the test is complete either way.
// RESULT (2026-08-30): isolation test confirmed - zero touch SPI traffic, zero card-art
// corruption over an extended watch. Flipped back to false now that handleTouchInput() has
// been redesigned (see TOUCH_POST_SPI_BLACKOUT_MS) to minimize real touch SPI transactions
// instead of eliminating them outright.
const bool TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL = false;
volatile uint32_t touchSpiTransactionCount = 0;
int8_t expandedCardSlot = -1; // artSlots[] index rendered as the tap-to-expand detail view, -1 = none
uint32_t nextCloudDisplaySyncMs = 0;
uint32_t cloudDisplayLastAttemptMs = 0;
uint32_t cloudDisplayLastSuccessMs = 0;
int cloudDisplayLastHttpStatus = 0;
char cloudDisplayLastError[96] = "";
bool cloudDisplayApiReady = false;
uint16_t cloudDisplayConsecutiveFailures = 0;

bool soundEnabled = true;
bool buzzerReady = false;
bool waterAlertActive = false;
uint32_t nextWaterAlertMs = 0;

enum class BuzzerPhase : uint8_t { Idle, Tone, Gap };

struct BuzzerState
{
  const BuzzerStep* steps = nullptr;
  uint8_t stepCount = 0;
  uint8_t stepIndex = 0;
  uint8_t repeatsRemaining = 0;
  uint16_t interRepeatGapMs = 0;
  uint32_t releaseAtMs = 0;
  uint32_t stepEndMs = 0;
  bool releaseTriggered = false;
  BuzzerPhase phase = BuzzerPhase::Idle;
};

BuzzerState buzzerState;

#if TREEBEARD_ENABLE_WIFI
IPAddress currentIpAddress();
const char* currentNetworkMode();
#endif
#if TREEBEARD_ENABLE_DISPLAY
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
    case ESP_RST_USB:
      return "USB peripheral reset";
    case ESP_RST_JTAG:
      return "JTAG reset";
    case ESP_RST_EFUSE:
      return "efuse error";
    case ESP_RST_PWR_GLITCH:
      return "power glitch";
    case ESP_RST_CPU_LOCKUP:
      return "CPU lockup (double exception)";
    default:
      return "unknown";
  }
}

void setCheckpoint(const char* message)
{
  strlcpy(lastCheckpoint, message, sizeof(lastCheckpoint));
}

void setBleCheckpoint(const char* message)
{
  strlcpy(lastCheckpointBle, message, sizeof(lastCheckpointBle));
}

void setCloudCheckpoint(const char* message)
{
  strlcpy(lastCheckpointCloud, message, sizeof(lastCheckpointCloud));
}

void bootLog(const char* message)
{
  setCheckpoint(message);
  Serial.print("[Treebeard boot] ");
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
    "{\"sensor\":1,\"plant\":\"Basil\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":2,\"plant\":\"Mint\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":3,\"plant\":\"Thyme\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":4,\"plant\":\"Sage\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":5,\"plant\":\"Rosemary\",\"art\":\"wide_planter\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":6,\"plant\":\"Oregano\",\"art\":\"cup_plant\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":7,\"plant\":\"Parsley\",\"art\":\"sprout_pot\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}},"
    "{\"sensor\":8,\"plant\":\"Cilantro\",\"art\":\"cactus_cluster\",\"moisture\":{\"dry\":120,\"wet\":175,\"temperatureCompensation\":\"learning\"}}"
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
  if (nextCloudFullSnapshotMs == 0 || (int32_t)(nextCloudFullSnapshotMs - targetMs) > 0)
  {
    nextCloudFullSnapshotMs = targetMs;
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
  cacheTemperatureCompensationModes(validation.as<JsonVariantConst>());
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
  doc["name"] = "Project Treebeard Display";
  doc["build"] = TREEBEARD_BUILD_TAG;
#if TREEBEARD_ENABLE_WIFI
  doc["networkMode"] = currentNetworkMode();
  doc["ip"] = currentIpAddress().toString();
  doc["localOnly"] = wifiLocalOnly;
#else
  doc["networkMode"] = "offline";
  doc["ip"] = "0.0.0.0";
#endif
  doc["claimPath"] = "/claim";
  doc["appUrl"] = TREEBEARD_HOSTED_APP_URL;
  String claimUrl = TREEBEARD_HOSTED_APP_URL;
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
  nextCloudFullSnapshotMs = 0;
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
  nextCloudFullSnapshotMs = cloudEnabled ? millis() + CLOUD_FULL_SNAPSHOT_INTERVAL_MS : 0;
  nextCloudConfigPullMs = cloudEnabled ? millis() + 10000UL : 0;
  nextCloudDisplaySyncMs = cloudEnabled ? millis() + 7000UL : 0;

  // One-time migration: a receiver that saved /cloud.json before this firmware only ever
  // knew the old 300s production default. Only touch the value if it still exactly matches
  // that old default - a user who deliberately configured some other interval (including
  // 300, on purpose) must not be silently overridden. Any file saved by this-or-newer
  // firmware carries syncIntervalMigratedV2 and is never touched again.
  if (!(doc["syncIntervalMigratedV2"] | false))
  {
    if (cloudSyncIntervalSeconds == CLOUD_SYNC_LEGACY_DEFAULT_SECONDS)
    {
      cloudSyncIntervalSeconds = CLOUD_SYNC_DEFAULT_SECONDS;
    }
    persistCloudConfig();
  }
  return true;
}

String cloudConfigJson()
{
  StaticJsonDocument<2048> doc;
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
  JsonObject historyQueue = upload.createNestedObject("historyQueue");
  historyQueue["queued"] = queuedHistoryCount();
  historyQueue["capacity"] = HISTORY_QUEUE_CAPACITY;
  historyQueue["batchSize"] = HISTORY_UPLOAD_BATCH_SIZE;
  historyQueue["dropped"] = droppedHistoryCount();
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
  displaySync["consecutiveFailures"] = cloudDisplayConsecutiveFailures;
  JsonObject bootstrap = doc.createNestedObject("bootstrap");
  bootstrap["configured"] = strlen(TREEBEARD_CLOUD_BOOTSTRAP_URL) > 0;
  bootstrap["url"] = TREEBEARD_CLOUD_BOOTSTRAP_URL;
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
  stored["syncIntervalMigratedV2"] = true;

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
  nextCloudFullSnapshotMs = cloudEnabled ? millis() + CLOUD_FULL_SNAPSHOT_INTERVAL_MS : 0;
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
  nextCloudFullSnapshotMs = 0;
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
#if TREEBEARD_ENABLE_WIFI
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
  if (TREEBEARD_FORCE_WIFI_SETUP != 0)
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

// Sound mute state is purely local receiver config, not part of the cloud-synced
// DisplaySettings/snapshot schema (no version/pendingCloudUpdate bookkeeping needed) -
// nothing outside this device needs to know whether its buzzer is muted.
void persistSoundSettings()
{
  StaticJsonDocument<64> doc;
  doc["enabled"] = soundEnabled;
  File file = LittleFS.open(SOUND_SETTINGS_PATH, "w");
  if (!file)
  {
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void loadSoundSettings()
{
  if (!LittleFS.exists(SOUND_SETTINGS_PATH))
  {
    return;
  }
  File file = LittleFS.open(SOUND_SETTINGS_PATH, "r");
  if (!file)
  {
    return;
  }
  StaticJsonDocument<64> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (!error)
  {
    soundEnabled = doc["enabled"] | true;
  }
}

void setSoundEnabledLocal(bool enabled)
{
  soundEnabled = enabled;
  if (!enabled)
  {
    // Cut off an in-flight pattern immediately rather than letting it finish playing
    // after the user just asked for silence.
    buzzerState.phase = BuzzerPhase::Idle;
    ledcWrite(BUZZER_PIN, 0);
  }
  persistSoundSettings();
}

void buzzerBeginStep(uint32_t now)
{
  const BuzzerStep& step = buzzerState.steps[buzzerState.stepIndex];
  ledcChangeFrequency(BUZZER_PIN, step.frequencyHz, BUZZER_PWM_RESOLUTION_BITS);
  uint16_t attackMs = step.toneMs < BUZZER_ENVELOPE_MS * 2 ? step.toneMs / 2 : BUZZER_ENVELOPE_MS;
  uint16_t releaseMs = (step.toneMs - attackMs) < BUZZER_ENVELOPE_MS ? (step.toneMs - attackMs) : BUZZER_ENVELOPE_MS;
  ledcFade(BUZZER_PIN, 0, buzzerDutyRaw(), attackMs);
  buzzerState.releaseTriggered = false;
  buzzerState.releaseAtMs = now + (step.toneMs > releaseMs ? step.toneMs - releaseMs : 0);
  buzzerState.stepEndMs = now + step.toneMs + step.silenceMs;
  buzzerState.phase = BuzzerPhase::Tone;
}

// Starts a non-blocking tone sequence: steps[0..count) played in order, the whole
// sequence repeated repeatCount times with interRepeatGapMs of silence between
// repeats. Must never block (delay()) - this runs from the same core-1 loop() that
// also services BLE-derived sensor rendering, touch, and the WebServer, so a blocking
// player would reintroduce the exact stalls the cloud/BLE tasks were split out to avoid.
void startBuzzerPattern(const BuzzerStep* steps, uint8_t count, uint8_t repeatCount, uint16_t interRepeatGapMs)
{
  if (!buzzerReady || !soundEnabled || steps == nullptr || count == 0 || repeatCount == 0)
  {
    return;
  }
  buzzerState.steps = steps;
  buzzerState.stepCount = count;
  buzzerState.stepIndex = 0;
  buzzerState.repeatsRemaining = repeatCount - 1;
  buzzerState.interRepeatGapMs = interRepeatGapMs;
  buzzerBeginStep(millis());
}

// Advances whatever pattern startBuzzerPattern() started, purely by comparing millis()
// against scheduled deadlines - no delay() anywhere. Call every loop() iteration.
void serviceBuzzer(uint32_t now)
{
  if (buzzerState.phase == BuzzerPhase::Idle)
  {
    return;
  }

  if (buzzerState.phase == BuzzerPhase::Gap)
  {
    if ((int32_t)(now - buzzerState.stepEndMs) >= 0)
    {
      buzzerState.stepIndex = 0;
      buzzerBeginStep(now);
    }
    return;
  }

  if (!buzzerState.releaseTriggered && (int32_t)(now - buzzerState.releaseAtMs) >= 0)
  {
    ledcFade(BUZZER_PIN, buzzerDutyRaw(), 0, BUZZER_ENVELOPE_MS);
    buzzerState.releaseTriggered = true;
  }

  if ((int32_t)(now - buzzerState.stepEndMs) < 0)
  {
    return;
  }

  buzzerState.stepIndex++;
  if (buzzerState.stepIndex < buzzerState.stepCount)
  {
    buzzerBeginStep(now);
    return;
  }

  if (buzzerState.repeatsRemaining > 0)
  {
    buzzerState.repeatsRemaining--;
    buzzerState.phase = BuzzerPhase::Gap;
    buzzerState.stepEndMs = now + buzzerState.interRepeatGapMs;
    return;
  }

  buzzerState.phase = BuzzerPhase::Idle;
  ledcWrite(BUZZER_PIN, 0);
}

// Reuses the exact same "critically low" condition that already drives the physical
// blink alert (anyLowMoistureCardVisible/slotNeedsBlink) so the audible and visual
// alerts never disagree about which plants count. Fires once immediately on the
// false->true transition, then again every BUZZER_WATER_ALERT_REPEAT_MS while still
// true, and goes silent again the moment every visible card is watered/healthy - see
// docs/DISPLAY_CENTER_BUZZER_SOUND_DESIGN.md for why an unthrottled repeat was rejected.
void serviceWaterAlert(uint32_t now, bool anyCardNeedsAlert)
{
  if (!anyCardNeedsAlert)
  {
    waterAlertActive = false;
    nextWaterAlertMs = 0;
    return;
  }
  if (waterAlertActive && (int32_t)(now - nextWaterAlertMs) < 0)
  {
    return;
  }
  waterAlertActive = true;
  nextWaterAlertMs = now + BUZZER_WATER_ALERT_REPEAT_MS;
  startBuzzerPattern(BUZZER_WATER_WARNING, sizeof(BUZZER_WATER_WARNING) / sizeof(BUZZER_WATER_WARNING[0]), 3, 350);
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
    Serial.print("[Treebeard touch] input stuck pressed before target irq=");
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
  Serial.print("[Treebeard touch] waiting target x=");
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
      Serial.print("[Treebeard touch] target timeout irq=");
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
    Serial.print("[Treebeard touch] insufficient pressure samples=");
    Serial.print(samples);
    Serial.print(" irq=");
    Serial.println(digitalRead(TOUCH_IRQ));
    tft.fillScreen(TFT_BLACK);
    drawTextCenter("Touch was too brief", tft.width() / 2, tft.height() / 2, 2, TFT_WARNING, TFT_BLACK);
    return false;
  }

  rawX = (int16_t)(sumX / samples);
  rawY = (int16_t)(sumY / samples);
  Serial.print("[Treebeard touch] captured rawX=");
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
    Serial.print("[Treebeard touch] input stuck pressed after target irq=");
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

// A tap, acted on immediately at release (see handleTouchInput()) - either closes the
// currently expanded detail card (tapping anywhere while one is open) or, if none is open,
// expands whichever plant card the tap landed on. Mirrors the brightness overlay's "no hidden
// control activates unless dashboardControlsAllowed" guard.
void handleDashboardSingleTap(int16_t x, int16_t y)
{
#if TREEBEARD_ENABLE_WIFI
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
  // Fixed width covers the longest possible value ("100%"), so dragging from a wider value
  // (e.g. "100%") down to a narrower one (e.g. "9%") can't leave the old leading digit(s)
  // behind - drawTextRight()/tft.print() only clears each glyph cell it actually draws, and
  // right-aligned text shifts its start position right as the string shrinks, exposing
  // whatever was left of that start point. Without this, the old "1" (or "10") from "100%"
  // stayed on screen next to the new value, reading like the brightness had gone over 100%.
  const int valueRightX = panelX + panelW - 16;
  const int valueMaxWidth = textPixelWidth("100%", 2);
  tft.fillRect(valueRightX - valueMaxWidth, panelY + 16, valueMaxWidth, 16, TFT_SURFACE);
  drawTextRight(value, valueRightX, panelY + 16, 2, screenOn ? TFT_ACCENT : TFT_MUTED, TFT_SURFACE);

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

// Redraws only the "Screen on/off" button - not the rest of the modal. Mirrors
// drawBrightnessSliderRow()'s split: drawBrightnessOverlay() calls this once for the initial
// paint, handleTouchInput()'s screen-button release handler calls it after toggling instead of
// repainting the whole panel.
void drawBrightnessScreenButtonRow(bool screenOn)
{
  const int panelX = 34;
  const int panelY = 54;
  const int panelH = tft.height() - panelY - 24;
  // Button text is size 1 (8px glyph height) in a 34px-tall button, so the vertical offset
  // that centers it is (34 - 8) / 2 = 13, not 9 - the old value sat visibly high.
  const int buttonY = panelY + panelH - 50;
  tft.fillRoundRect(panelX + 16, buttonY, 112, 34, 6, screenOn ? TFT_WARNING : TFT_ACCENT);
  drawTextCenter(screenOn ? "Screen off" : "Screen on", panelX + 72, buttonY + 13, 1, TFT_BLACK, screenOn ? TFT_WARNING : TFT_ACCENT);
}

// Redraws only the "Sound on/off" button - see drawBrightnessScreenButtonRow()'s comment,
// same split for the same reason.
void drawBrightnessSoundButtonRow(bool soundOn)
{
  const int panelX = 34;
  const int panelY = 54;
  const int panelW = tft.width() - panelX * 2;
  const int panelH = tft.height() - panelY - 24;
  const int buttonY = panelY + panelH - 50;
  // Centered in the gap between the screen and close buttons - same tap-target-describes-
  // action convention as the screen button (label names what tapping it will do).
  const int soundButtonW = 110;
  const int soundButtonX = panelX + (panelW - soundButtonW) / 2;
  tft.fillRoundRect(soundButtonX, buttonY, soundButtonW, 34, 6, soundOn ? TFT_WARNING : TFT_ACCENT);
  drawTextCenter(soundOn ? "Sound off" : "Sound on", soundButtonX + soundButtonW / 2, buttonY + 13, 1, TFT_BLACK, soundOn ? TFT_WARNING : TFT_ACCENT);
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

  const int buttonY = panelY + panelH - 50;
  drawBrightnessScreenButtonRow(screenOn);
  drawBrightnessSoundButtonRow(soundEnabled);
  tft.fillRoundRect(panelX + panelW - 104, buttonY, 88, 34, 6, TFT_SURFACE_2);
  drawTextCenter("Close", panelX + panelW - 60, buttonY + 13, 1, TFT_MUTED, TFT_SURFACE_2);
}

// ---- Bench probe data helpers -------------------------------------------------------------
//
// The Bench view itself is a WEB page served from this receiver (/bench), not a screen on the
// display. Only the data path and the command beacon live in firmware; the rendering, the
// selection and the demo baseline are all the browser's job. See
// DisplayCenter/docs/BENCH_PAGE_AND_COMMAND_BEACON_2026-09-21.md.

// One short lock, one whole-array copy. Rendering and JSON both happen outside it, for the same
// reason copySensorReading() exists: never hold a spinlock across slow work.
void benchSnapshotAll(BenchProbe* out)
{
  portENTER_CRITICAL(&benchMux);
  memcpy(out, benchProbes, sizeof(BenchProbe) * BENCH_SLOT_COUNT);
  portEXIT_CRITICAL(&benchMux);
}

bool benchSlotIsFresh(const BenchProbe& probe, uint32_t nowMs)
{
  return probe.seen && (nowMs - probe.lastSeenMs) < BENCH_STALE_MS;
}

// Serialises benchProbes[] for GET /bench-data.
//
// Raw observables only. No calibration, no moisture mapping, no smoothing - this is a bench
// instrument readout, and every transform this receiver applies to real plant data exists for
// plants in soil, not for a board held in someone's hand. The browser does the presentation
// (grouping digits, the demo baseline/delta, age formatting).
String benchDataJson()
{
  const uint32_t nowMs = millis();
  BenchProbe snapshot[BENCH_SLOT_COUNT];
  benchSnapshotAll(snapshot);

  StaticJsonDocument<1536> doc;
  doc["ok"] = true;
  doc["nowMs"] = nowMs;
  doc["staleMs"] = BENCH_STALE_MS;
  doc["identifyBroadcasting"] = benchCommandAdvertising;
  JsonArray probes = doc.createNestedArray("probes");
  for (uint8_t i = 0; i < BENCH_SLOT_COUNT; i++)
  {
    if (!snapshot[i].seen)
    {
      continue;
    }
    JsonObject probe = probes.createNestedObject();
    probe["sensorId"] = snapshot[i].sensorId;
    probe["wake"] = snapshot[i].wake;
    // readingValid false means the probe advertised a placeholder, not a measurement (an OTA
    // discovery window, or a failed FDC read). The count is omitted entirely rather than sent as
    // 0, so the page cannot render a fabricated reading even by accident.
    probe["readingValid"] = snapshot[i].readingValid;
    if (snapshot[i].readingValid)
    {
      probe["fdcCounts"] = snapshot[i].fdcCounts;
    }
    probe["batteryMv"] = snapshot[i].batteryMv;
    probe["charging"] = snapshot[i].charging;
    probe["otaPowerOk"] = snapshot[i].otaPowerOk;
    probe["liveMode"] = snapshot[i].liveMode;
    probe["fresh"] = benchSlotIsFresh(snapshot[i], nowMs);
    if (snapshot[i].boardTempValid)
    {
      probe["boardTempC"] = snapshot[i].boardTempQuarterC / 4.0f;
    }
    // Absent, not zero, on pre-versioning bring-up firmware: those builds never wrote these
    // bytes. Gated on the format version, not on packet length.
    probe["formatVersion"] = snapshot[i].formatVersion;
    if (snapshot[i].formatVersion >= MSV3_BENCH_FORMAT_VERSION_1)
    {
      probe["muxCode"] = snapshot[i].muxCode;
      probe["fdcStatus"] = snapshot[i].fdcStatus;
    }
    probe["rssi"] = snapshot[i].rssi;
    probe["ageMs"] = nowMs - snapshot[i].lastSeenMs;
    probe["received"] = snapshot[i].received;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

// Puts one command on air for BENCH_CMD_BROADCAST_MS. Returns false if BLE is not up, so the
// caller can say so rather than silently appearing to work.
//
// The counter is monotonic per boot and is what makes a probe act once on a command it will hear
// dozens of times during the broadcast window. See benchCommandScanCallback()'s replay handling,
// which also tolerates this counter restarting when the receiver reboots.
bool benchSendCommand(uint8_t targetSensorId, uint8_t opcode)
{
#if !TREEBEARD_ENABLE_BLE
  (void)targetSensorId;
  (void)opcode;
  return false;
#else
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising == nullptr)
  {
    return false;
  }

  if (benchCommandSessionId == 0)
  {
    // esp_random() is seeded by the RF/entropy hardware once WiFi or BLE is up, which it is by
    // the time any command can be sent. Zero is reserved as "not yet drawn", so retry past it.
    while (benchCommandSessionId == 0)
    {
      benchCommandSessionId = (uint16_t)(esp_random() & 0xFFFF);
    }
  }
  benchCommandCounter++;
  uint8_t payload[BENCH_CMD_PACKET_LEN];
  payload[0] = (uint8_t)(MSV2_COMPANY_ID & 0xFF);
  payload[1] = (uint8_t)((MSV2_COMPANY_ID >> 8) & 0xFF);
  memcpy(&payload[2], BENCH_CMD_MAGIC, sizeof(BENCH_CMD_MAGIC));
  payload[6] = targetSensorId;
  payload[7] = opcode;
  payload[8] = (uint8_t)(benchCommandCounter & 0xFF);
  payload[9] = (uint8_t)((benchCommandCounter >> 8) & 0xFF);
  payload[10] = (uint8_t)((benchCommandCounter >> 16) & 0xFF);
  payload[11] = (uint8_t)((benchCommandCounter >> 24) & 0xFF);
  payload[12] = (uint8_t)(benchCommandSessionId & 0xFF);
  payload[13] = (uint8_t)((benchCommandSessionId >> 8) & 0xFF);

  // String(ptr, len), never String(ptr). The counter above routinely contains zero bytes and a
  // NUL-terminated construction would truncate the payload mid-field - the probe would then see
  // a short packet and drop it, which looks exactly like a radio problem.
  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(0x06);  // LE General Discoverable, BR/EDR not supported
  advertisementData.setManufacturerData(String((const char*)payload, sizeof(payload)));
  if (!advertising->setAdvertisementData(advertisementData))
  {
    return false;
  }
  advertising->setScanResponse(false);
  // Non-connectable. This receiver has never advertised before and runs no GATT server; the
  // default connectable type would invite any phone in the room to open a connection to it,
  // which is both pointless and radio time taken away from scanning.
  //
  // BACKEND-DEPENDENT, and NOT interchangeable. setAdvertisementType() forwards its argument to
  // two different fields: esp_ble_adv_params_t::adv_type under Bluedroid, and
  // ble_gap_adv_params::conn_mode under NimBLE (see BLEAdvertising.cpp). This build is
  // NimBLE-backed, where the correct value is BLE_GAP_CONN_MODE_NON (0); passing Bluedroid's
  // ADV_TYPE_NONCONN_IND (3) there would set an out-of-range connection mode, not a
  // non-connectable advertisement. Mirroring the library's own #if split is the only way to be
  // right on both.
#if defined(CONFIG_NIMBLE_ENABLED)
  advertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
#elif defined(CONFIG_BLUEDROID_ENABLED)
  advertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
#endif
  advertising->start();
  benchCommandAdvertising = true;
  benchCommandUntilMs = millis() + BENCH_CMD_BROADCAST_MS;
  return true;
#endif
}

// Takes the command back off air once its window closes. Called unconditionally from loop(), so
// a command's broadcast window ends on time whether or not anyone is still looking at /bench.
void serviceBenchCommandBroadcast()
{
#if TREEBEARD_ENABLE_BLE
  if (!benchCommandAdvertising)
  {
    return;
  }
  if ((int32_t)(millis() - benchCommandUntilMs) < 0)
  {
    return;
  }
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (advertising != nullptr)
  {
    advertising->stop();
  }
  benchCommandAdvertising = false;
#endif
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

// 2026-08-30 gesture redesign - see TOUCH_LONG_PRESS_MS's own comment for why long-press
// replaced double-tap-anywhere. Dashboard-wide, same reachability guard as the tap-to-expand
// path below; only opens the overlay (closing it is already handled entirely by the existing
// button/outside-tap release logic further down, unchanged).
void handleDashboardLongPress()
{
#if TREEBEARD_ENABLE_WIFI
  if (wifiSetupPortalActive || pendingRestartMs != 0)
  {
    return;
  }
#endif
  if (brightnessOverlayOpen)
  {
    return;
  }
  brightnessOverlayOpen = true;
  drawBrightnessOverlay();
}

void handleTouchInput(uint32_t now)
{
  // Slider-drag fast path, phone-a-friend consultation "treebeard-slider-fix-20260831" -
  // runs on EVERY call, ahead of the TOUCH_POLL_INTERVAL_MS gate below, instead of sharing
  // that gate's ~32ms-quantized cadence. Read/paint are still hard-limited to at most one
  // transaction per TOUCH_POST_SPI_BLACKOUT_MS by the real elapsed-time checks inside this
  // block - only the CHECKING frequency changes. This matters because lastTouchSpiMs and
  // lastDisplaySpiActivityMs are now stamped with the transaction's actual completion time
  // (see their own comments), not the poll's start time - a blackout that clears mid-poll-
  // interval (the common case, since a real SPI transaction takes measurably nonzero time)
  // would otherwise miss that poll-interval boundary and wait a full extra ~32ms for the
  // next quantized one, roughly doubling the slider's real read-to-paint and paint-to-read
  // cadence versus what TOUCH_POST_SPI_BLACKOUT_MS alone requires. Checking every call
  // (loop() cycles every ~1-2ms while brightnessOverlayOpen, via its own `delay(1)`) lets a
  // cleared blackout get acted on almost immediately instead of rounded up. No SPI is spent
  // by the check itself - digitalRead(TOUCH_IRQ) and the two timestamp comparisons below are
  // free - so this doesn't reintroduce the SPI-polling cost the T_IRQ redesign eliminated.
  if (touchReady && !TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL && touchWasDown &&
      overlayTouchTarget == OverlayTouchTarget::Slider && !touchWakeOnlyThisGesture &&
      digitalRead(TOUCH_IRQ) == LOW)
  {
    const bool sliderInPostSpiBlackout = now - lastTouchSpiMs < TOUCH_POST_SPI_BLACKOUT_MS;
    if (sliderPaintPending)
    {
      // A read is waiting to be painted (from this gesture's confirmed-press classification,
      // or an earlier call through this same fast path). Wait for ITS post-SPI blackout to
      // clear before spending the display write, so the read and the write are never
      // SPI-adjacent - this was the actual glitch: previously both happened in one poll with
      // no gap at all, repeating on every ~32ms poll for as long as the drag continued.
      if (!sliderInPostSpiBlackout)
      {
        // Force both CS lines high before this transaction, same defensive reasoning as the
        // read side below and the confirmed-press read above - don't assume the touch
        // library left its own CS state exactly how the TFT library expects to find it.
        digitalWrite(TFT_CS, HIGH);
        digitalWrite(TOUCH_CS, HIGH);
        previewTouchBrightness(lastTouchX);
        lastDisplaySpiActivityMs = millis();
        sliderPaintPending = false;
      }
    }
    else if (!sliderInPostSpiBlackout && now - lastDisplaySpiActivityMs >= TOUCH_POST_SPI_BLACKOUT_MS)
    {
      // Symmetric to the confirmed-press read: force both CS lines high before this
      // transaction so neither controller can still be mid-address-window, and require the
      // display side quiet too (the previous phase's paint, if any) before spending it.
      digitalWrite(TFT_CS, HIGH);
      digitalWrite(TOUCH_CS, HIGH);
      touchSpiTransactionCount++;
      TS_Point point = touchScreen.getPoint();
      // Re-read the clock rather than using this call's `now` - getPoint() is a real SPI
      // transaction; stamping before it starts understates how recently touch was active.
      lastTouchSpiMs = millis();
      int16_t x = -1;
      int16_t y = -1;
      if (mapRawTouchToScreen(point.x, point.y, x, y))
      {
        lastTouchX = x;
        lastTouchY = y;
        sliderPaintPending = true;
      }
    }
  }

  if (!touchReady || now - lastTouchPollMs < TOUCH_POLL_INTERVAL_MS)
  {
    return;
  }
  lastTouchPollMs = now;

  if (TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL)
  {
    // See TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL's comment - deliberately returns before the T_IRQ
    // read below is even reached, so this whole function is a no-op for the isolation test.
    return;
  }

  // T_IRQ is active-low and can be sampled without touching the shared SPI bus at all - see
  // TOUCH_POST_SPI_BLACKOUT_MS's comment for why this is now the primary down/up signal instead
  // of repeatedly calling touchScreen.touched()/getPoint() (each a real XPT2046 SPI transaction).
  const bool irqLow = digitalRead(TOUCH_IRQ) == LOW;
  const bool inPostSpiBlackout = now - lastTouchSpiMs < TOUCH_POST_SPI_BLACKOUT_MS;

  // Once the read that touchAcquisitionPending was deferring writes for has actually
  // happened (touchPendingConfirm cleared) and its own post-read guard has elapsed, resume
  // letting loop() write to the display again.
  if (touchAcquisitionPending && !touchPendingConfirm && !inPostSpiBlackout)
  {
    touchAcquisitionPending = false;
  }

  if (!touchWasDown && !touchPendingConfirm)
  {
    // Fully idle: watch T_IRQ only, zero SPI transactions.
    releaseCandidatePending = false;
    if (irqLow)
    {
      touchPendingConfirm = true;
      touchDownStartMs = now;
      // Start deferring loop()'s periodic display writes immediately - see
      // touchAcquisitionPending's comment. Better to hold off a card's animation tick for a
      // few ms than let it land right next to the coordinate read this gesture is about to need.
      touchAcquisitionPending = true;
    }
    return;
  }

  if (touchPendingConfirm)
  {
    if (!irqLow)
    {
      // Released/bounced before a confirmed sample was ever taken - noise, not a gesture.
      touchPendingConfirm = false;
      touchAcquisitionPending = false;
      return;
    }

    // Held LOW for one full poll interval - ready to spend this gesture's one SPI read, but
    // only once BOTH sides of the bus have been quiet for a full blackout window. Guards
    // against touch's own recent activity (inPostSpiBlackout) as well as the display's -
    // loop() has been deferring its writes since touchAcquisitionPending went true above, so
    // this is really just waiting for whatever write was already in flight/scheduled before
    // this gesture started to have finished settling.
    if (inPostSpiBlackout || now - lastDisplaySpiActivityMs < TOUCH_POST_SPI_BLACKOUT_MS)
    {
      return;
    }

    // Both sides confirmed quiet - explicitly force both CS lines high before this
    // transaction so neither controller can still be mid-address-window from whatever it was
    // last doing, then take the one read on a settled sample rather than the raw first-contact
    // edge.
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    touchSpiTransactionCount++;
    TS_Point point = touchScreen.getPoint();
    // Re-read the clock rather than reusing the pre-transaction `now` - getPoint() is a real
    // SPI transaction and stamping before it starts understates how recently touch was
    // actually active, the same class of gap already fixed for lastDisplaySpiActivityMs above.
    lastTouchSpiMs = millis();
    int16_t x = -1;
    int16_t y = -1;
    if (!mapRawTouchToScreen(point.x, point.y, x, y))
    {
      // Bad sample while still genuinely held down (irqLow just confirmed it) - retry next
      // poll without resetting touchDownStartMs, so one noisy reading can't cost a real
      // long-press its timing. touchAcquisitionPending stays true so display writes remain
      // deferred for the retry too.
      return;
    }

    touchPendingConfirm = false;
    touchWasDown = true;
    touchGestureChangedBrightness = false;
    touchWakeOnlyThisGesture = false;
    longPressFired = false;
    overlayOpenedThisGesture = false;
    releaseCandidatePending = false;
    overlayTouchTarget = OverlayTouchTarget::None;
    sliderPaintPending = false;
    lastTouchX = x;
    lastTouchY = y;

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
      touchWakeOnlyThisGesture = true;
      return; // Wake-only first touch; never activate a hidden control.
    }

    if (brightnessOverlayOpen)
    {
      // Classified exactly once, from this gesture's one confirmed press coordinate - see
      // overlayTouchTarget's own comment for why this must never be redone from a later
      // sample. Geometry matches the release handler's own hit-tests further down exactly.
      const int panelX = 34;
      const int panelY = 54;
      const int panelW = tft.width() - panelX * 2;
      const int panelH = tft.height() - panelY - 24;
      const int sliderX = panelX + 58;
      const int sliderY = panelY + 82;
      const int sliderW = panelW - 116;
      const int buttonY = panelY + panelH - 50;
      if (pointInside(x, y, panelX + 4, sliderY - 14, 52, 48))
      {
        overlayTouchTarget = OverlayTouchTarget::Minus;
      }
      else if (pointInside(x, y, panelX + panelW - 56, sliderY - 14, 52, 48))
      {
        overlayTouchTarget = OverlayTouchTarget::Plus;
      }
      else if (pointInside(x, y, sliderX - 12, sliderY - 20, sliderW + 24, 56))
      {
        // Paint deferred to a later poll instead of done inline here - see
        // sliderPaintPending's own comment. x/y are already this gesture's confirmed-press
        // read (just stored into lastTouchX/lastTouchY above), so no extra SPI transaction
        // is needed to have a value ready to paint.
        overlayTouchTarget = OverlayTouchTarget::Slider;
        sliderPaintPending = true;
      }
      else if (pointInside(x, y, panelX + 8, buttonY - 6, 128, 46))
      {
        overlayTouchTarget = OverlayTouchTarget::Screen;
      }
      else if (pointInside(x, y, panelX + (panelW - 110) / 2 - 4, buttonY - 6, 118, 46))
      {
        overlayTouchTarget = OverlayTouchTarget::Sound;
      }
      else if (pointInside(x, y, panelX + panelW - 112, buttonY - 6, 104, 46) || !pointInside(x, y, panelX, panelY, panelW, panelH))
      {
        overlayTouchTarget = OverlayTouchTarget::Close;
      }
      // else: dead space inside the panel - stays None, a no-op at release, matching the
      // pre-existing behavior of a tap that hit none of the known controls.
    }
    return;
  }

  // touchWasDown == true: an already-confirmed, active gesture.
  if (irqLow)
  {
    // Still held. Cancel any release candidate in progress without resetting
    // touchDownStartMs - see TOUCH_RELEASE_STABLE_MS's comment. No SPI transaction here at
    // all unless the brightness slider is actively being dragged (below) - this is the whole
    // point of the redesign: a plain tap or long-press spends zero further SPI time while held.
    releaseCandidatePending = false;

    if (!touchWakeOnlyThisGesture && !longPressFired && now - touchDownStartMs >= TOUCH_LONG_PRESS_MS)
    {
      // Fires once, mid-hold - see longPressFired's own comment. The release handler below
      // checks longPressFired and does nothing further for this contact either way, so
      // wherever the finger happens to lift (including right over a button that didn't
      // exist a moment ago) can't also act on it. overlayOpenedThisGesture additionally stops
      // the SAME held contact from being read as a slider-drag on the panel that just
      // appeared under it, on the very next poll while still held. Pure timestamp check - no
      // SPI transaction needed to fire a long-press.
      longPressFired = true;
      // Snapshot before calling - handleDashboardLongPress() is a no-op (early-returns with
      // no draw) if the overlay this same held contact already opened, or was already open
      // for some other reason, so only stamp lastDisplaySpiActivityMs when a draw actually
      // happened. The stale stamp this replaces cost a Slider-target gesture a spurious extra
      // ~32ms of read/paint blackout once per drag, right at the 550ms long-press mark.
      bool overlayWasOpenBeforeLongPress = brightnessOverlayOpen;
      handleDashboardLongPress();
      if (!overlayWasOpenBeforeLongPress && brightnessOverlayOpen)
      {
        lastDisplaySpiActivityMs = millis();
      }
      if (brightnessOverlayOpen)
      {
        overlayOpenedThisGesture = true;
      }
      return;
    }

    // Live slider drag is handled by the fast path at the top of this function instead of
    // here - see that block's own comment for why the normal TOUCH_POLL_INTERVAL_MS-gated
    // cadence below isn't fine-grained enough for it.
    return;
  }

  // irqLow == false: potential release. Never even start qualifying it inside the post-SPI
  // blackout window - a conversion that just finished (initial press or a drag sample) can
  // leave PENIRQ glitched/deasserted for a moment, which would otherwise look like a release.
  if (inPostSpiBlackout)
  {
    return;
  }

  // Qualify the release before acting on it - require TOUCH_RELEASE_STABLE_MS of continuous
  // "T_IRQ high" first. See that constant's own comment for why (contact bounce on this
  // resistive panel can otherwise fire the tap/collapse action 2-3 times for one physical
  // press). This is now a pure GPIO+timestamp debounce - no SPI transaction involved at all.
  if (!releaseCandidatePending)
  {
    releaseCandidatePending = true;
    releaseCandidateSinceMs = now;
    return;
  }
  if (now - releaseCandidateSinceMs < TOUCH_RELEASE_STABLE_MS)
  {
    return;
  }
  releaseCandidatePending = false;
  touchWasDown = false;
  // If the finger lifted with a read still waiting to be painted, drop it rather than paint
  // it here - release processing has its own timing-sensitive blackout requirements, and the
  // dropped sample is at most one ~32ms-old position behind whatever was last actually
  // painted (see sliderPaintPending's own comment).
  sliderPaintPending = false;

  if (touchWakeOnlyThisGesture)
  {
    touchWakeOnlyThisGesture = false;
    return;
  }

  if (longPressFired)
  {
    return;
  }

  if (!brightnessOverlayOpen)
  {
    bool dashboardControlsAllowed = true;
#if TREEBEARD_ENABLE_WIFI
    dashboardControlsAllowed = !wifiSetupPortalActive && pendingRestartMs == 0;
#endif
    if (dashboardControlsAllowed)
    {
      // Immediate, no deferral - the old design queued every tap for up to 650ms to see
      // whether a second one followed. With that gesture gone (long-press owns the overlay
      // now), a plain tap can just act on release.
      handleDashboardSingleTap(lastTouchX, lastTouchY);
      lastDisplaySpiActivityMs = millis();
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
    // Only the value/slider changed - see drawBrightnessSliderRow()'s own comment for why a
    // full-panel repaint here is exactly the flashing anti-pattern it exists to avoid.
    drawBrightnessSliderRow(brightness, true);
    lastDisplaySpiActivityMs = millis();
    return;
  }
  uint8_t brightness;
  bool screenOn;
  portENTER_CRITICAL(&displaySettingsMux);
  brightness = displaySettings.brightnessPercent;
  screenOn = displaySettings.screenOn;
  portEXIT_CRITICAL(&displaySettingsMux);

  // Dispatch on the target LATCHED at this gesture's one confirmed press (see
  // overlayTouchTarget's own comment) - never re-derived from lastTouchX/lastTouchY here.
  // Re-deriving from the current (possibly-drifted, or merged-gesture-stale) coordinate was
  // the actual bug: a button-origin gesture could resolve here as "outside the panel" and
  // close the overlay instead of acting on the button the user was still on.
  switch (overlayTouchTarget)
  {
    case OverlayTouchTarget::Minus:
    {
      // Only the slider row changed - see drawBrightnessSliderRow()'s comment.
      uint8_t newBrightness = (uint8_t)max(1, (int)brightness - 10);
      setDisplaySettingsLocal(newBrightness, true, "receiver-touch");
      drawBrightnessSliderRow(newBrightness, true);
      lastDisplaySpiActivityMs = millis();
      break;
    }
    case OverlayTouchTarget::Plus:
    {
      uint8_t newBrightness = (uint8_t)min(100, (int)brightness + 10);
      setDisplaySettingsLocal(newBrightness, true, "receiver-touch");
      drawBrightnessSliderRow(newBrightness, true);
      lastDisplaySpiActivityMs = millis();
      break;
    }
    case OverlayTouchTarget::Screen:
    {
      bool newScreenOn = !screenOn;
      setDisplaySettingsLocal(brightness, newScreenOn, "receiver-touch");
      if (!screenOn)
      {
        // Only this button's own color/label changed - see
        // drawBrightnessScreenButtonRow()'s comment.
        drawBrightnessScreenButtonRow(newScreenOn);
        lastDisplaySpiActivityMs = millis();
      }
      break;
    }
    case OverlayTouchTarget::Sound:
      setSoundEnabledLocal(!soundEnabled);
      // Only this button's own color/label changed - see drawBrightnessSoundButtonRow()'s
      // comment. setSoundEnabledLocal() updates the soundEnabled global synchronously, so it
      // already reflects the new state here.
      drawBrightnessSoundButtonRow(soundEnabled);
      lastDisplaySpiActivityMs = millis();
      break;
    case OverlayTouchTarget::Close:
      brightnessOverlayOpen = false;
      // Not displayDirty=true - that would force a full renderUi() (re-read/parse ui.json,
      // fillScreen, redraw the header, rebuild every card) purely to erase this modal, which
      // is exactly the "whole display refreshes"/visible flash the surgical-redraw rule
      // exists to avoid. restoreCardsFromCache() gets the same visual result (every card's
      // real content is back, correctly) from PlantArtSlot's own cache alone - see its
      // comment. Also a genuinely large write burst, so tracked by lastDisplaySpiActivityMs
      // the same as loop()'s three periodic writers.
      restoreCardsFromCache();
      lastDisplaySpiActivityMs = millis();
      break;
    case OverlayTouchTarget::Slider:
    case OverlayTouchTarget::None:
    default:
      // Slider: any real drag already committed via touchGestureChangedBrightness above: a
      // release reaching here means the press landed in the slider region but never moved
      // enough to preview a value - a no-op is the right behavior (same as before this fix).
      // None: dead space inside the panel - always was a no-op.
      break;
  }
}

// Resolves the dry/wet bounds to actually use for a card: the self-learned
// auto-calibration range (when the card has "auto" enabled and enough history has
// accumulated server-side), otherwise the manual/default thresholds. Used by every
// moisture-rendering call site (both PlantArtSlot-based and direct) so the web app and
// physical display never disagree. outCalibrated tells the caller whether the bounds it
// got back are auto-cal-derived (used to switch the moisture text from raw to percent -
// see drawPlantCard()) as opposed to the manual/preset fallback.
void resolveMoistureBounds(
  JsonObject moistureConfig,
  JsonObject card,
  uint8_t sensorId,
  int defaultDry,
  int defaultWet,
  int& outDry,
  int& outWet,
  bool& outCalibrated,
  bool& outPending,
  bool& outRegion
)
{
  bool autoEnabled = moistureConfig["auto"] | false;
  bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;

  AutoCalState learned;
  const bool hasLearnedState = validSensor && copyAutoCalState(sensorId, learned);
  if (autoEnabled && hasLearnedState && learned.ready)
  {
    outDry = learned.dryBound;
    outWet = learned.wetBound;
    outCalibrated = true;
    outPending = false;
    outRegion = learned.region;
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
  outCalibrated = false;
  outPending = autoEnabled;
  outRegion = false;
}

// Re-runs the auto-cal half of resolveMoistureBounds() against a cached PlantArtSlot,
// using only fields that were snapshotted from ui.json (autoEnabled/manualDry/manualWet
// - fixed until the next full renderUi() rebuild) plus the live autoCal[] state (updated
// on its own ~10-minute cadence by runCloudAutoCalPull(), running on a separate task).
//
// Without this, slot.dry/wet/calibrated were only ever set once, inside
// cachePlantArtSlot() - itself only called from renderUi(). Ordinary operation almost
// never triggers another full renderUi() once cards exist (see the displayDirty=true
// call sites), so a background auto-cal update - the range finishing calibration, or a
// shifted learned range - could sit applied-but-invisible for arbitrarily long, only
// surfacing whenever something incidental (e.g. closing the brightness overlay) happened
// to force a full re-render. Calling this from both the blink-cadence check and the
// lightweight per-card redraw keeps the fast path honest without needing a JSON re-parse
// or full screen repaint.
void refreshSlotMoistureBounds(PlantArtSlot& slot)
{
  bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  AutoCalState learned;
  const bool hasLearnedState = validSensor && copyAutoCalState(slot.sensorId, learned);
  if (slot.autoEnabled && hasLearnedState && learned.ready)
  {
    slot.dry = learned.dryBound;
    slot.wet = learned.wetBound;
    slot.calibrated = true;
    slot.region = learned.region;
    slot.rangeTrendWarn = learned.dryBoundTrendingLower;
    slot.pending = false;
    return;
  }
  slot.dry = slot.manualDry;
  slot.wet = slot.manualWet;
  slot.calibrated = false;
  slot.region = false;
  slot.rangeTrendWarn = false;
  slot.pending = slot.autoEnabled;
}

// 0-100 is exactly the historical dry-to-wet range, same as it has always been - 0 at the
// driest ever learned, 100 at the wettest. Unlike the old version, this does NOT clamp
// below zero: a reading drier than anything on record keeps counting down as a negative
// number instead of pinning at 0 forever, so "how much drier than ever before" stays
// visible. Still clamped at 100 on top - "more than fully watered" isn't meaningful. Signed
// return type reflects this; BATTERY_UNKNOWN (255) remains a safe stale-data sentinel since
// it's far outside any real percent this can produce.
int16_t moisturePercent(uint16_t moisture, int dry, int wet)
{
  if (wet <= dry)
  {
    wet = dry + 1;
  }
  const long range = wet - dry;
  const long numerator = ((long)moisture - dry) * 100L;
  // Match the web clients' rounded percentage instead of truncating toward zero. This
  // keeps the exact alert boundaries consistent on every display.
  const long percent = numerator >= 0
    ? (numerator + range / 2) / range
    : (numerator - range / 2) / range;
  return (int16_t)min(100L, percent);
}

// 0 is the watering-line inflection point - literally the driest this sensor has ever
// recorded (moisturePercent()'s own zero), not a padded-above-dry cue. 0-100 (this sensor's
// entire learned dry-to-wet range) is normal, unchanged from how it has always displayed.
// Crossing below 0 means "drier than ever observed" and is genuinely new information, which
// is why color/alerts only escalate there: MOISTURE_DANGER_PERCENT is the overdue boundary -
// red and actually blinking/buzzing (slotNeedsBlink()), not just a warning. Between 0 and
// the danger threshold is a quiet yellow warning. MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT is a
// display-layout constant, not an alert threshold: the bar's dotted marker (drawBar()) and
// the boundary between its "0-100" region and its open-ended negative region both sit at
// this fraction of the bar's width, so the unbounded negative range has a fixed, finite
// strip of the bar to render into before the bar goes fully empty. All of this is shared by
// the moisture color, the blink/buzzer trigger, and the bar so every alert on this display -
// and both web clients, see scripts/verify_moisture_alert_thresholds.mjs - fires at exactly
// the same points and renders the same layout.
//
// 2026-10-11, the water-now region: on a region-calibrated sensor 100 % is the level the pot
// settled at after its last watering, 0 % is the top of the region the user waters in, and the
// bottom of that region always lands on -21 % (100 * (0.925 - 1.12) / 0.925, REGION_OVERDUE_PERCENT
// in receiver-snapshot's region.ts). So the danger threshold moved from -10 to -21: at -10 the
// display blinked and buzzed through waterings the maintainer called right on time
// (cloud/docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md).
const int16_t MOISTURE_WATER_LINE_PERCENT = 0;
const int16_t MOISTURE_DANGER_PERCENT = -21;
const uint8_t MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT = 30;

// What a card's moisture row reads. Below its watering line a region-calibrated sensor says
// WATER NOW in place of a negative percent; every other state is as it was ("--" stale, a
// percent once calibrated, the raw number otherwise). The same condition as the warning colour,
// so the words and the yellow arrive together.
const char MOISTURE_WATER_NOW_TEXT[] = "WATER NOW";

void formatMoistureText(char* out, size_t outSize, bool stale, bool calibrated, bool region, int16_t percent, uint16_t rawMoisture)
{
  if (stale)
  {
    strlcpy(out, "--", outSize);
  }
  else if (calibrated && region && percent < MOISTURE_WATER_LINE_PERCENT)
  {
    strlcpy(out, MOISTURE_WATER_NOW_TEXT, outSize);
  }
  else if (calibrated)
  {
    // percent is already signed (moisturePercent() is unclamped below zero).
    snprintf(out, outSize, "%d%%", percent);
  }
  else
  {
    snprintf(out, outSize, "%u", rawMoisture);
  }
}

// WATER NOW and the row's "Moisture" label do not fit side by side on a four-column card
// (98 px inside, 102 px of text), so the label gives way to it.
bool moistureRowShowsLabel(const char* moistureText)
{
  return moistureText == nullptr || strcmp(moistureText, MOISTURE_WATER_NOW_TEXT) != 0;
}

// Moisture semantics: 0-100 (the full learned dry-to-wet range) is normal, exactly as it has
// always been. Below 0 is new territory - drier than ever recorded - so color escalates:
// a quiet yellow warning until the danger threshold, then red/blinking/buzzer overdue.
uint16_t moistureHealthColor(int16_t percent, uint16_t accentColor, uint16_t warningColor, uint16_t dangerColor, uint16_t mutedColor)
{
  if (percent == BATTERY_UNKNOWN)
  {
    return mutedColor;
  }
  if (percent <= MOISTURE_DANGER_PERCENT)
  {
    return dangerColor;
  }
  if (percent < MOISTURE_WATER_LINE_PERCENT)
  {
    return warningColor;
  }
  return accentColor;
}

uint16_t batteryHealthColor(uint8_t percent, uint16_t accentColor, uint16_t warningColor, uint16_t dangerColor, uint16_t mutedColor)
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

// Only the at-or-below-MOISTURE_DANGER_PERCENT red state blinks. The yellow warning band
// between the danger threshold and the water line stays static.
uint16_t applyLowMoistureBlink(uint16_t resolvedColor, uint16_t dangerColor, uint16_t mutedColor)
{
  if (resolvedColor != dangerColor)
  {
    return resolvedColor;
  }
  bool blinkOn = (millis() / 500) % 2 == 0;
  return blinkOn ? dangerColor : mutedColor;
}

void drawBar(int x, int y, int w, int h, int16_t percent, uint16_t fillColor, uint16_t trackColor)
{
  // 0-100 (the learned dry-to-wet range) occupies the right (100 - NEGATIVE_WIDTH)% of the
  // bar's width; the left MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT is reserved for the open-ended
  // "drier than ever recorded" extension. percent itself is unclamped below zero (see
  // moisturePercent()) so the number keeps counting down - only this visual width is
  // clamped, since a bar obviously can't render negative width. That's the fixed floor
  // "goes off the end of the bar and can't be displayed anymore".
  int barPct;
  if (percent == BATTERY_UNKNOWN)
  {
    barPct = 0;
  }
  else
  {
    const long positiveSpan = 100L - MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT;
    const long scaled = MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT + (long)percent * positiveSpan / 100L;
    barPct = clampInt((int)scaled, 0, 100);
  }
  int fillW = (w - 4) * barPct / 100;

  tft.drawRoundRect(x, y, w, h, 3, trackColor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 2, trackColor);
  if (fillW > 0)
  {
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, fillColor);
  }

  // Watering-threshold marker at percent=0 (the driest ever recorded) - i.e. at a fixed
  // MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT of the bar's width regardless of the current
  // reading. Every current caller of drawBar() draws a moisture bar - there is no battery
  // bar on this display, battery is a text/color indicator only (see drawCardHeader()) - so
  // this couples directly to the moisture layout constant rather than taking it as a
  // parameter that would only ever be called with one value. Drawn last, on top of the
  // track/fill, via alternating pixels (TFT_eSPI has no dashed-line primitive) so it reads
  // as a thin marker rather than a solid full-height divider at this bar's ~5-8px height.
  // Already covered by drawBar()'s existing "only redraw when value/color changed" callers -
  // see drawPlantCardDataOnly()'s moistureValueChanged/moistureColorChanged gate - so this
  // adds no new unconditional repaint of its own.
  int lineX = x + 2 + (w - 4) * MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT / 100;
  for (int dy = y + 1; dy < y + h - 1; dy += 2)
  {
    tft.drawPixel(lineX, dy, TFT_WHITE);
  }
}

// The water-now range, marked under a region-calibrated card's meter (2026-10-11; the
// maintainer's pick of the two in DisplayCenter/docs/WATER_NOW_METER_MOCKUP_2026-10-11.png). A
// small bracket from the overdue line (MOISTURE_DANGER_PERCENT) to the watering line
// (MOISTURE_WATER_LINE_PERCENT, the meter's dotted marker): the stretch of the meter in which
// the card says WATER NOW.
//
// It lies wholly BELOW drawBar()'s own rectangle, so a bar repaint (every blink tick while a
// card is overdue) never touches it, and it is drawn only where the moisture row has just been
// painted: once in each full draw, and in the data-only paths only inside the value-changed
// clear, which reaches down over it for that reason. Regular cards only - a compact card
// (h < 76, thirteen cards and up) has no rows left under its bar.
const int MOISTURE_RANGE_MARK_ROWS = 3; // a tick row, then two rows of line

bool moistureRangeMarked(bool stale, bool calibrated, bool region)
{
  return !stale && calibrated && region;
}

void drawWaterNowRange(int barX, int barY, int barW, int barH, uint16_t color)
{
  const long positiveSpan = 100L - MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT;
  const int lowPct = clampInt((int)(MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT + (long)MOISTURE_DANGER_PERCENT * positiveSpan / 100L), 0, 100);
  const int lowX = barX + 2 + (barW - 4) * lowPct / 100;
  const int highX = barX + 2 + (barW - 4) * MOISTURE_BAR_NEGATIVE_WIDTH_PERCENT / 100;
  const int topY = barY + barH;
  tft.drawFastVLine(lowX, topY, MOISTURE_RANGE_MARK_ROWS, color);
  tft.drawFastVLine(highX, topY, MOISTURE_RANGE_MARK_ROWS, color);
  tft.fillRect(lowX, topY + 1, highX - lowX + 1, MOISTURE_RANGE_MARK_ROWS - 1, color);
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

void spriteDrawGeometry(const PlantSprite& sprite, int centerX, int topY, int maxW, int maxH, int& drawW, int& drawH, int& startX, int& startY)
{
  if (maxW <= 0 || maxH <= 0 || sprite.w == 0 || sprite.h == 0)
  {
    drawW = 0;
    drawH = 0;
    startX = centerX;
    startY = topY;
    return;
  }

  // Aspect-fit directly into the available art rectangle. The old whole-number
  // multiplier/divisor scheme reduced an 84px sprite to 42px when the region was 80px
  // tall, wasting almost three quarters of the usable area. Direct source-coordinate
  // mapping remains nearest-neighbor and, because every pose shares one source canvas,
  // cannot move a pose's anchor between frames.
  if ((uint32_t)sprite.w * (uint32_t)maxH >= (uint32_t)sprite.h * (uint32_t)maxW)
  {
    drawW = maxW;
    drawH = max(1, (int)((uint32_t)sprite.h * (uint32_t)maxW / sprite.w));
  }
  else
  {
    drawH = maxH;
    drawW = max(1, (int)((uint32_t)sprite.w * (uint32_t)maxH / sprite.h));
  }

  startX = centerX - drawW / 2;
  startY = topY + max(0, (maxH - drawH) / 2);
}

uint16_t spriteScaledPixel(const PlantSprite& sprite, const uint16_t* bitmap, int drawW, int drawH, int x, int y)
{
  const int sourceX = (int)((uint32_t)x * sprite.w / drawW);
  const int sourceY = (int)((uint32_t)y * sprite.h / drawH);
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
  int drawW = 0;
  int drawH = 0;
  int spriteStartX = 0;
  int spriteStartY = 0;
  spriteDrawGeometry(sprite, centerX, topY, maxW, maxH, drawW, drawH, spriteStartX, spriteStartY);
  if (drawW <= 0 || drawH <= 0)
  {
    return;
  }
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
        const uint16_t c = spriteScaledPixel(sprite, bitmap, drawW, drawH, localSpriteX, localSpriteY);
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

uint16_t artFrameInterval(const char* art, uint8_t frame)
{
  const PlantSprite* sprite = spriteForArt(art);
  if (sprite == nullptr || sprite->durationsMs == nullptr || sprite->frameCount == 0)
  {
    return ART_FRAME_DEFAULT_MS;
  }
  return pgm_read_word(&sprite->durationsMs[frame % sprite->frameCount]);
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
  const int artH = compact ? max(10, h - 29) : max(24, h - 57);

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
  const int artH = compact ? max(10, h - 29) : max(24, h - 57);
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
  slot.frameIntervalMs = artFrameInterval(slot.art, slot.frame);
  // Randomize only the starting phase so multiple cards do not switch on the same SPI
  // tick. Once started, every pose keeps the source GIF's own deterministic duration.
  slot.nextFrameMs = millis() + random(0, (uint32_t)slot.frameIntervalMs + 1U);
}

void formatBatteryPercent(uint8_t batteryPct, char* out, size_t outSize)
{
  if (batteryPct == BATTERY_UNKNOWN)
  {
    strlcpy(out, "--%", outSize);
  }
  else
  {
    snprintf(out, outSize, "%u%%", batteryPct);
  }
}

// 2026-08-31: dropped the "Batt: "/"Temp: " labels that used to live here - see
// cardInfoColumnLeftX()'s own comment for why. Bare "84%"/"24C" is still unambiguous: this
// column is always the same two rows in the same corner of every card, battery first then
// (when present) temperature, so the position alone carries the meaning the label used to.
void formatBatteryHeader(uint8_t batteryPct, char* out, size_t outSize)
{
  formatBatteryPercent(batteryPct, out, outSize);
}

int cardHeaderInset(int h)
{
  return h < 76 ? 5 : 7;
}

// Widest string either header field can ever produce at size-1 (6px/char): "100%", "-40C",
// "--%", "--C" are all <=4 chars. Shared by drawCardHeader's name-width budget and both
// drawCardBatteryHeader/drawCardTempHeader's clear rects via cardInfoColumnLeftX() below, so
// the three can never independently drift apart the way they used to (a fixed-60px
// reservation - sized for the old, longer "Batt: 100%" label - meant the plant name and this
// column were computed from two different formulas that only agreed on wide cards; on a
// narrow card added by growing the grid past ~5-6 columns, the name's leftover share of the
// row shrank to 0 characters while this column kept its full width regardless, reading as
// "the battery/temp text crowded out the name" - field-reported 2026-08-31).
const int CARD_INFO_COLUMN_WIDTH = 24;
const int CARD_INFO_GAP = 4;

// Left edge of the header's right-hand info column (battery, and temp one row below it).
// Clamped to never cross leftX - a future change to grid density/columns must not be able to
// push this past the card's own left content edge, even though no width on this display's
// current grid algorithm actually reaches that clamp (verified down to the narrowest
// realistic card - see the 2026-08-31 consultation this fix came out of).
int cardInfoColumnLeftX(int x, int w, int h)
{
  const int inset = cardHeaderInset(h);
  const int leftX = x + inset;
  const int rightX = x + w - inset;
  return max(leftX, rightX - CARD_INFO_COLUMN_WIDTH);
}

void drawCardBatteryHeader(int x, int y, int w, int h, uint8_t batteryPct, uint16_t batteryColor, uint16_t surfaceColor, bool clearFirst)
{
  char batteryHeader[16];
  formatBatteryHeader(batteryPct, batteryHeader, sizeof(batteryHeader));
  const int inset = cardHeaderInset(h);
  const int rightX = x + w - inset;
  const int infoLeft = cardInfoColumnLeftX(x, w, h);
  const int infoWidth = rightX - infoLeft;
  if (clearFirst)
  {
    // Always exactly CARD_INFO_COLUMN_WIDTH wide (already covers the longest possible
    // value), except on a card too narrow even for that - see cardInfoColumnLeftX()'s clamp -
    // where infoWidth shrinks below it instead of ever extending further left.
    tft.fillRect(infoLeft, y + 5, infoWidth, 10, surfaceColor);
  }
  if (infoWidth >= CARD_INFO_COLUMN_WIDTH)
  {
    drawTextRight(batteryHeader, rightX, y + 6, 1, batteryColor, surfaceColor);
  }
}

// MSV3-only board (die) temperature, quarter-degree C -> whole C for display. Raw/uncompensated
// - see docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md. Returns false (nothing to
// draw) when the sensor never reported a valid reading, same nullable convention as the History
// tab's own board-temp chart.
bool formatBoardTempHeader(bool valid, int16_t tempQuarterC, char* out, size_t outSize)
{
  if (!valid) return false;
  // Matches the nRF52840 die-temp sensor's real spec range, same bounds
  // cloud/supabase/functions/receiver-snapshot/msv3-diagnostics.ts already validates
  // server-side (MSV3_BOARD_TEMP_MIN_Q4C/MAX_Q4C) - but that check happens after this data
  // already reached the display, so a corrupted-but-valid-flagged BLE packet is still
  // possible here. Reject to a neutral sentinel instead of formatting (and, worse, silently
  // saturating into a believable-looking but wrong) an out-of-spec value - this also
  // guarantees the formatted string can never exceed CARD_INFO_COLUMN_WIDTH's 4-char budget.
  if (tempQuarterC < -160 || tempQuarterC > 340)
  {
    strlcpy(out, "--C", outSize);
    return true;
  }
  // Plain ASCII, deliberately no degree symbol - this project uses the GFX library's default
  // built-in font (no setFont() call anywhere in this file), which does not reliably render a
  // UTF-8 degree sign (0xC2 0xB0 came out as garbage glyphs on real hardware) or any other
  // single-byte code point above plain printable ASCII without visually verifying that font's
  // actual glyph table first - not worth the risk for one character.
  snprintf(out, outSize, "%dC", (int)(tempQuarterC / 4));
  return true;
}

// Second header line directly below the battery row, right-aligned at the same rightX. Only
// present for MSV3 probes with board-temp diagnostics live - absent (nothing drawn, no
// reserved blank row) for every other sensor, so this never costs vertical space on a card
// that can't fill it in.
void drawCardTempHeader(int x, int y, int w, int h, bool valid, int16_t tempQuarterC, uint16_t textColor, uint16_t surfaceColor, bool clearFirst)
{
  const int inset = cardHeaderInset(h);
  const int rightX = x + w - inset;
  // One size-1 text line below the battery row (which sits at y+6) - 9px matches this
  // firmware's existing size-1 line spacing elsewhere (see the detail-view row pitch).
  const int rowY = y + 15;
  const int infoLeft = cardInfoColumnLeftX(x, w, h);
  const int infoWidth = rightX - infoLeft;
  char tempHeader[16];
  const bool hasText = formatBoardTempHeader(valid, tempQuarterC, tempHeader, sizeof(tempHeader));
  if (clearFirst)
  {
    // Same fixed-width clear rationale as drawCardBatteryHeader.
    tft.fillRect(infoLeft, rowY - 1, infoWidth, 10, surfaceColor);
  }
  if (hasText && infoWidth >= CARD_INFO_COLUMN_WIDTH)
  {
    drawTextRight(tempHeader, rightX, rowY, 1, textColor, surfaceColor);
  }
}

void drawCardHeader(int x, int y, int w, int h, const char* plant, uint8_t batteryPct, uint16_t textColor, uint16_t batteryColor, uint16_t surfaceColor)
{
  char batteryHeader[16];
  formatBatteryHeader(batteryPct, batteryHeader, sizeof(batteryHeader));
  const int inset = cardHeaderInset(h);
  const int leftX = x + inset;
  const int rightX = x + w - inset;
  const int infoLeft = cardInfoColumnLeftX(x, w, h);
  const int infoWidth = rightX - infoLeft;
  // Always reserve the maximum header width, even while the current value is the shorter
  // "--%" form. A later data-only update to "100%" must not grow leftward across a
  // plant-name glyph painted during the full render - infoLeft is the exact same boundary
  // drawCardBatteryHeader/drawCardTempHeader clear against, via cardInfoColumnLeftX(), so
  // this can never drift out of sync with them again.
  const int nameWidth = max(0, infoLeft - CARD_INFO_GAP - leftX);
  const size_t maxChars = min((size_t)23, (size_t)(nameWidth / 6));
  char clippedPlant[24];
  if (maxChars == 0 || plant == nullptr)
  {
    clippedPlant[0] = '\0';
  }
  else
  {
    strlcpy(clippedPlant, plant, maxChars + 1);
  }
  drawTextLeft(clippedPlant, leftX, y + 6, 1, textColor, surfaceColor);
  // Same width guard as drawCardBatteryHeader's data-only redraw - keeps this consistent
  // with the clamp in cardInfoColumnLeftX() rather than assuming infoWidth is always the
  // full CARD_INFO_COLUMN_WIDTH just because that's true for every width this display's
  // current grid algorithm actually produces.
  if (infoWidth >= CARD_INFO_COLUMN_WIDTH)
  {
    drawTextRight(batteryHeader, rightX, y + 6, 1, batteryColor, surfaceColor);
  }
}

void drawCompactCardMetrics(
  int x,
  int y,
  int w,
  int h,
  const char* moistureText,
  uint8_t moisturePct,
  uint16_t moistureColor,
  uint16_t mutedColor,
  uint16_t surfaceColor,
  uint16_t backgroundColor
)
{
  int moistY = y + max(18, h - 25);
  int moistBarY = moistY + 13;
  if (moistBarY + 5 > y + h - 4)
  {
    int shift = moistBarY + 5 - (y + h - 4);
    moistY -= shift;
    moistBarY -= shift;
  }
  moistY = max(y + 17, moistY);
  moistBarY = max(moistY + 11, moistBarY);

  tft.fillRect(x + 5, moistY - 1, w - 10, min(h - (moistY - y) - 3, 22), surfaceColor);
  if (moistureRowShowsLabel(moistureText))
  {
    drawTextLeft("Moisture", x + 5, moistY, 1, mutedColor, surfaceColor);
  }
  drawTextRight(moistureText, x + w - 5, moistY, 1, moistureColor, surfaceColor);
  drawBar(x + 5, moistBarY, w - 10, 5, moisturePct, moistureColor, backgroundColor);
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
  bool calibrated;
  bool pending;
  bool region;
  resolveMoistureBounds(moistureConfig, card, sensorId, 120, 175, dry, wet, calibrated, pending, region);

  const bool validSensor = sensorId > 0 && sensorId <= MAX_SENSOR_ID;
  SensorReading readingSnapshot;
  const SensorReading* reading = validSensor && copySensorReading(sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  int16_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, dry, wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = moistureHealthColor(moisturePct, accentColor, warningColor, dangerColor, mutedColor);
  if (!stale)
  {
    moistureColor = applyLowMoistureBlink(moistureColor, dangerColor, mutedColor);
  }
  uint16_t batteryColor = batteryHealthColor(batteryPct, accentColor, warningColor, dangerColor, mutedColor);

  tft.fillRoundRect(x, y, w, h, 5, surfaceColor);
  tft.drawRoundRect(x, y, w, h, 5, stale ? mutedColor : moistureColor);

  drawCardHeader(x, y, w, h, plant, batteryPct, textColor, batteryColor, surfaceColor);

  // Auto-cal-derived bounds in use: show percent instead of the raw sensor value, as a
  // visual "calibration done" signal (see docs/STATUS_HANDOFF.md's redesign item 5).
  char moistureText[MOISTURE_TEXT_SIZE];
  formatMoistureText(moistureText, sizeof(moistureText), stale, calibrated, region, moisturePct, stale ? 0 : reading->moisture);

  if (h < 76)
  {
    drawCompactCardMetrics(x, y, w, h, moistureText, moisturePct, moistureColor, mutedColor, surfaceColor, backgroundColor);
    return;
  }

  // Compact cards skip this entirely - h<76 doesn't leave reliable clearance between the
  // header and drawCompactCardMetrics' moisture row (see that function's own y math), and
  // this is a secondary diagnostic stat, not worth the collision risk on the tightest layout.
  const bool boardTempValid = reading != nullptr && reading->boardTempValid;
  drawCardTempHeader(x, y, w, h, boardTempValid, boardTempValid ? reading->rawBoardTempQuarterC : 0, mutedColor, surfaceColor, false);

  if (showArt)
  {
    drawPlantArtInCard(x, y, w, h, art, stale ? mutedColor : accentColor, surfaceColor, 0);
  }

  if (moistureRowShowsLabel(moistureText))
  {
    drawTextLeft("Moisture", x + 7, y + h - 29, 1, mutedColor, surfaceColor);
  }
  drawTextRight(moistureText, x + w - 7, y + h - 29, 1, moistureColor, surfaceColor);
  drawBar(x + 7, y + h - 14, w - 14, 8, moisturePct, moistureColor, backgroundColor);
  if (moistureRangeMarked(stale, calibrated, region))
  {
    drawWaterNowRange(x + 7, y + h - 14, w - 14, 8, warningColor);
  }
}

void drawPlantCardDataOnly(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;
  const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
  SensorReading readingSnapshot;
  const SensorReading* reading = validSensor && copySensorReading(slot.sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
  const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  int16_t moisturePct = stale ? BATTERY_UNKNOWN : moisturePercent(reading->moisture, slot.dry, slot.wet);
  uint8_t batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  uint16_t moistureColor = moistureHealthColor(moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  if (!stale)
  {
    moistureColor = applyLowMoistureBlink(moistureColor, slot.dangerColor, slot.mutedColor);
  }
  uint16_t batteryColor = batteryHealthColor(batteryPct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);

  tft.drawRoundRect(x, y, w, h, 5, stale ? slot.mutedColor : moistureColor);

  char moistureText[MOISTURE_TEXT_SIZE];
  formatMoistureText(moistureText, sizeof(moistureText), stale, slot.calibrated, slot.region, moisturePct, stale ? 0 : reading->moisture);

  const bool moistureValueChanged = !slot.metricsInitialized || stale != slot.lastMoistureStale ||
    slot.calibrated != slot.lastMoistureCalibrated ||
    slot.region != slot.lastMoistureRegion ||
    (!stale && (reading->moisture != slot.lastMoistureRaw || moisturePct != slot.lastMoisturePct));
  const bool moistureColorChanged = moistureColor != slot.lastMoistureColor;
  const bool batteryValueChanged = !slot.metricsInitialized || batteryPct != slot.lastBatteryPct;

  if (h < 76)
  {
    if (batteryValueChanged)
    {
      drawCardBatteryHeader(x, y, w, h, batteryPct, batteryColor, slot.surfaceColor, true);
    }
    drawCompactCardMetrics(x, y, w, h, moistureText, moisturePct, moistureColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    slot.metricsInitialized = true;
    slot.lastMoistureStale = stale;
    slot.lastMoistureRaw = stale ? slot.lastMoistureRaw : reading->moisture;
    slot.lastMoisturePct = moisturePct;
    slot.lastMoistureCalibrated = slot.calibrated;
    slot.lastMoistureRegion = slot.region;
    slot.lastMoistureColor = moistureColor;
    slot.lastBatteryPct = batteryPct;
    return;
  }

  // Only touch a row if something about it actually needs to change. Without this, every
  // low-moisture blink tick (every 500ms, see anyLowMoistureCardVisible) repainted every
  // metric field in full - including battery, which has nothing to do with the moisture
  // alert - producing a visible flash on fields whose values hadn't moved at all.
  //
  // The moist row's *color* legitimately needs to update every blink tick (that's the
  // alert), even when the reading itself hasn't changed, so it's gated separately from the
  // reading/text/bar-width content. The fillRect clear is only needed when the content
  // could have changed width (new value or stale-state flip, e.g. "175" -> "88" or "--") -
  // skipping it on a color-only blink tick avoids the clear-then-redraw flash entirely,
  // since drawTextRight/drawBar always fully repaint their own fixed-size cells anyway.
  if (moistureValueChanged || moistureColorChanged)
  {
    if (moistureValueChanged)
    {
      // Reach exactly to the art band's bottom edge so no uncovered strip can retain a
      // stale sprite pixel after a value-width change.
      const int artBottomY = y + 24 + max(24, h - 57);
      const int moistRowBottom = y + h - 6 + MOISTURE_RANGE_MARK_ROWS; // over the range mark too
      const int moistRowTop = min(y + h - 30, artBottomY);
      tft.fillRect(x + 7, moistRowTop, w - 14, max(1, moistRowBottom - moistRowTop), slot.surfaceColor);
      if (moistureRowShowsLabel(moistureText))
      {
        drawTextLeft("Moisture", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
      }
      if (moistureRangeMarked(stale, slot.calibrated, slot.region))
      {
        drawWaterNowRange(x + 7, y + h - 14, w - 14, 8, slot.warningColor);
      }
    }
    drawTextRight(moistureText, x + w - 7, y + h - 29, 1, moistureColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 14, w - 14, 8, moisturePct, moistureColor, slot.backgroundColor);
  }

  if (batteryValueChanged)
  {
    drawCardBatteryHeader(x, y, w, h, batteryPct, batteryColor, slot.surfaceColor, true);
  }

  // MSV3-only, nullable - see drawCardTempHeader()'s own comment. Gated the same way as every
  // other row here: only clear+redraw when the value (or its presence) actually changed.
  const bool boardTempValid = reading != nullptr && reading->boardTempValid;
  const int16_t boardTempQuarterC = boardTempValid ? reading->rawBoardTempQuarterC : 0;
  const bool boardTempChanged = !slot.lastBoardTempInitialized ||
    boardTempValid != slot.lastBoardTempValid ||
    (boardTempValid && boardTempQuarterC != slot.lastBoardTempQuarterC);
  if (boardTempChanged)
  {
    drawCardTempHeader(x, y, w, h, boardTempValid, boardTempQuarterC, slot.mutedColor, slot.surfaceColor, true);
    slot.lastBoardTempInitialized = true;
    slot.lastBoardTempValid = boardTempValid;
    slot.lastBoardTempQuarterC = boardTempQuarterC;
  }

  slot.metricsInitialized = true;
  slot.lastMoistureStale = stale;
  slot.lastMoistureRaw = stale ? slot.lastMoistureRaw : reading->moisture;
  slot.lastMoisturePct = moisturePct;
  slot.lastMoistureCalibrated = slot.calibrated;
  slot.lastMoistureRegion = slot.region;
  slot.lastMoistureColor = moistureColor;
  slot.lastBatteryPct = batteryPct;
}

// 2026-08-30: restores every visible card's on-screen content from PlantArtSlot's own cache -
// no ui.json re-read, no full-screen clear, no header/title redraw - for use whenever
// something else painted over the dashboard while the normal render loop was paused (currently
// just the brightness overlay closing). renderDashboardDataOnly()'s per-tick path can't be
// reused as-is here: it deliberately only repaints a field when its tracked *value* changed,
// which is correct for its normal job but wrong here, since the actual on-screen pixels (not
// any cached value) are what changed underneath. This forces the interior background, plant
// name header, and art fully back first, then invalidates the metrics/temp cache sentinels so
// drawPlantCardDataOnly()'s own field-by-field redraw repaints everything on top of it instead
// of skipping fields whose value happens to be unchanged.
//
// 2026-08-30 gap-fill fix, field-reported twice: this only repainted each card's own x/y/w/h
// rect. The brightness overlay's panel rect doesn't line up with the card grid - it clips
// each card's edge (the grid has a top/bottom/left/right margin the modal doesn't reach) but
// DOES paint straight across the gutters between cards, in the modal's own surfaceColor rather
// than the dashboard's actual backgroundColor. Restoring only card rects left that gutter
// residue sitting there indefinitely (it's not part of any card, so drawPlantCardDataOnly()
// never touches it either) until a later, unrelated full renderUi() happened to fillScreen()
// over it - which read as "the old menu stays up until the default display eventually gets
// written over it." Painting the overlay's own known panel rect with the real background
// color first, before any card redraws itself on top, closes that gap.
void restoreCardsFromCache()
{
  if (artSlotCount > 0)
  {
    const int panelX = 34;
    const int panelY = 54;
    const int panelW = tft.width() - panelX * 2;
    const int panelH = tft.height() - panelY - 24;
    tft.fillRect(panelX, panelY, panelW, panelH, artSlots[0].backgroundColor);
  }
  for (uint8_t i = 0; i < artSlotCount; i++)
  {
    if (!artSlots[i].enabled)
    {
      continue;
    }
    if (i == expandedCardSlot)
    {
      expandPlantCardInPlace(artSlots[i]);
      continue;
    }
    PlantArtSlot& slot = artSlots[i];
    tft.fillRoundRect(slot.x, slot.y, slot.w, slot.h, 5, slot.surfaceColor);
    // Plant name is only ever drawn once by the full drawPlantCard() path and is never
    // touched again by drawPlantCardDataOnly() - see drawCardHeader()'s own comment about
    // reserving header width so a later battery update can't grow across it. The battery
    // value passed here is a throwaway placeholder; metricsInitialized=false below makes
    // drawPlantCardDataOnly() redraw the real one immediately after.
    drawCardHeader(slot.x, slot.y, slot.w, slot.h, slot.plant, BATTERY_UNKNOWN, slot.textColor, slot.mutedColor, slot.surfaceColor);
    if (slot.artEnabled && slot.h >= 76)
    {
      drawPlantArtInCard(slot.x, slot.y, slot.w, slot.h, slot.art, slot.activeColor, slot.surfaceColor, slot.frame);
    }
    slot.metricsInitialized = false;
    slot.lastBoardTempInitialized = false;
    drawPlantCardDataOnly(slot);
  }
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
  SensorReading readingSnapshot;
  const SensorReading* reading = validSensor && copySensorReading(slot.sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
  s.stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);
  s.rawMoisture = reading != nullptr ? reading->moisture : 0;
  s.moisturePct = s.stale ? BATTERY_UNKNOWN : moisturePercent(s.rawMoisture, slot.dry, slot.wet);
  s.batteryPct = (reading == nullptr || reading->batteryPercent == BATTERY_UNKNOWN) ? BATTERY_UNKNOWN : reading->batteryPercent;
  s.moistureColor = moistureHealthColor(s.moisturePct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  if (!s.stale)
  {
    s.moistureColor = applyLowMoistureBlink(s.moistureColor, slot.dangerColor, slot.mutedColor);
  }
  s.batteryColor = batteryHealthColor(s.batteryPct, slot.activeColor, slot.warningColor, slot.dangerColor, slot.mutedColor);
  formatMoistureText(s.moistureText, sizeof(s.moistureText), s.stale, slot.calibrated, slot.region, s.moisturePct, s.rawMoisture);
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
  if (s.hasRaw && reading->wideRaw && reading->msv4)
  {
    // MSV4: soil resistance as sent (0.1 ohm, not temperature-corrected). The saturated value
    // means nothing is conducting, which a number would misrepresent.
    if (reading->rawFdcCounts >= MSV4_RSOIL_DECIOHM_MAX)
      strlcpy(s.rawText, "open", sizeof(s.rawText));
    else
      snprintf(s.rawText, sizeof(s.rawText), "%lu.%lu ohm",
               (unsigned long)(reading->rawFdcCounts / 10UL),
               (unsigned long)(reading->rawFdcCounts % 10UL));
  }
  else if (s.hasRaw && reading->wideRaw)
  {
    // MSV3: FDC2214 raw counts, not an oscillator kHz - no "kHz" suffix here.
    snprintf(s.rawText, sizeof(s.rawText), "%lu", (unsigned long)reading->rawFdcCounts);
  }
  else if (s.hasRaw)
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
  slot.lastDetailRangeTrendWarn = slot.rangeTrendWarn;
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
  slot.lastMoistureCalibrated = slot.calibrated;
  slot.lastMoistureRegion = slot.region;
  slot.lastMoistureColor = s.moistureColor;
  slot.lastBatteryPct = s.batteryPct;
  seedDetailDiagnosticCache(slot, s);
}

// Paints only the region that changes when a normal plant card flips to diagnostics:
// the former sprite band. The card background, border, header, and moisture row stay
// untouched.
void drawPlantCardDetailInfoBand(PlantArtSlot& slot, const PlantDetailStats& s)
{
  const int x = slot.x;
  const int y = slot.y;
  const int w = slot.w;
  const int h = slot.h;
  const int infoY = y + 24;
  const int infoH = max(24, h - 57);
  const int infoBottom = infoY + infoH;
  const int rightX = x + w - 7;
  // See drawPlantCardDetailDataOnly()'s matching comment: recolors the Range row's
  // value when the learned dry bound has been trending lower, instead of adding a new row.
  const uint16_t rangeValueColor = slot.rangeTrendWarn ? slot.warningColor : slot.textColor;

  // This is the exact region previously owned by the animated sprite. Clearing only
  // this band removes the old pose without touching any neighboring card or metric row.
  tft.fillRect(x + 7, infoY, w - 14, infoH, slot.surfaceColor);
  if (w < 200)
  {
    const int rowStep = clampInt(infoH / 7, 12, 20);
    int rowY = infoY;
    drawDetailStatRow(x + 7, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, s.signalColor, slot.surfaceColor);
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
      drawDetailStatRow(x + 7, rightX, rowY, "Range", s.rangeText, slot.mutedColor, rangeValueColor, slot.surfaceColor);
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
    drawDetailStatRow(col2X, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, s.signalColor, slot.surfaceColor);
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
      drawDetailStatRow(col2X, rightX, rowY, "Range", s.rangeText, slot.mutedColor, rangeValueColor, slot.surfaceColor);
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
// drawPlantArtInCard() would have used (y + 24, height max(24, h - 57)) so the extra rows
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
  drawCardHeader(x, y, w, h, slot.plant, s.batteryPct, slot.textColor, s.batteryColor, slot.surfaceColor);

  if (h < 76)
  {
    // Too small for extra rows - same layout as the normal compact card, plus a small
    // signal-quality dot in the corner where the name row would otherwise leave blank
    // space, so even the smallest grid size shows *something* extra on tap.
    drawCompactCardMetrics(x, y, w, h, s.moistureText, s.moisturePct, s.moistureColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    tft.fillCircle(x + w - 8, y + 22, 3, s.signalColor);
    seedDetailCache(slot, s);
    return;
  }

  const int rightX = x + w - 7;
  drawPlantCardDetailInfoBand(slot, s);

  if (moistureRowShowsLabel(s.moistureText))
  {
    drawTextLeft("Moisture", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
  }
  drawTextRight(s.moistureText, rightX, y + h - 29, 1, s.moistureColor, slot.surfaceColor);
  drawBar(x + 7, y + h - 14, w - 14, 8, s.moisturePct, s.moistureColor, slot.backgroundColor);
  if (moistureRangeMarked(s.stale, slot.calibrated, slot.region))
  {
    drawWaterNowRange(x + 7, y + h - 14, w - 14, 8, slot.warningColor);
  }

  seedDetailCache(slot, s);
}

void expandPlantCardInPlace(PlantArtSlot& slot)
{
  refreshSlotMoistureBounds(slot);
  PlantDetailStats s;
  computeDetailStats(slot, s);
  // See DETAIL_FULL_RESYNC_MS's own comment. (Re)schedule on every genuine open, not just the
  // first one after boot, so re-tapping a card back open after collapsing it restarts the
  // window rather than potentially forcing a resync immediately on the next tick.
  slot.nextDetailFullResyncMs = millis() + DETAIL_FULL_RESYNC_MS;

  if (slot.h < 76)
  {
    // Compact cards have no sprite band to replace; the diagnostic view adds only
    // this small signal dot, so that is the only region touched on expansion.
    tft.fillCircle(slot.x + slot.w - 8, slot.y + 22, 3, s.signalColor);
    seedDetailDiagnosticCache(slot, s);
    return;
  }

  drawPlantCardDetailInfoBand(slot, s);
  // Moisture/battery cache fields already describe the unchanged normal-card fields.
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
    tft.fillCircle(x + w - 8, y + 22, 3, slot.surfaceColor);
    return;
  }

  const int infoY = y + 24;
  const int infoH = max(24, h - 57);

  // Remove the one region diagnostics owned, then restore the sprite into its original
  // band. All normal-card chrome and metrics remain exactly as drawn.
  tft.fillRect(x + 7, infoY, w - 14, infoH, slot.surfaceColor);
  if (slot.artEnabled)
  {
    const bool validSensor = slot.sensorId > 0 && slot.sensorId <= MAX_SENSOR_ID;
    SensorReading readingSnapshot;
    const SensorReading* reading = validSensor && copySensorReading(slot.sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
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
    slot.calibrated != slot.lastMoistureCalibrated ||
    slot.region != slot.lastMoistureRegion ||
    (!s.stale && (s.rawMoisture != slot.lastMoistureRaw || s.moisturePct != slot.lastMoisturePct));
  const bool moistureColorChanged = s.moistureColor != slot.lastMoistureColor;
  const bool batteryValueChanged = !slot.metricsInitialized || s.batteryPct != slot.lastBatteryPct;

  if (h < 76)
  {
    if (moistureValueChanged || moistureColorChanged || batteryValueChanged)
    {
      drawCompactCardMetrics(x, y, w, h, s.moistureText, s.moisturePct, s.moistureColor, slot.mutedColor, slot.surfaceColor, slot.backgroundColor);
    }
    if (batteryValueChanged)
    {
      drawCardBatteryHeader(x, y, w, h, s.batteryPct, s.batteryColor, slot.surfaceColor, true);
    }
    if (signalChanged)
    {
      tft.fillCircle(x + w - 8, y + 22, 3, s.signalColor);
    }
    seedDetailCache(slot, s);
    return;
  }

  const int infoY = y + 24;
  const int infoH = max(24, h - 57);
  const int infoBottom = infoY + infoH;
  const int rightX = x + w - 7;

  const bool rssiChanged = strcmp(s.rssiText, slot.lastDetailRssiText) != 0;
  const bool ageChanged = strcmp(s.ageText, slot.lastDetailAgeText) != 0;
  const bool wakeChanged = strcmp(s.wakeText, slot.lastDetailWakeText) != 0;
  const bool lossChanged = strcmp(s.lossText, slot.lastDetailLossText) != 0;
  const bool receivedChanged = strcmp(s.receivedText, slot.lastDetailReceivedText) != 0;
  const bool missedChanged = strcmp(s.missedText, slot.lastDetailMissedText) != 0;
  const bool rangeChanged = strcmp(s.rangeText, slot.lastDetailRangeText) != 0 ||
    slot.rangeTrendWarn != slot.lastDetailRangeTrendWarn;
  const bool rawChanged = strcmp(s.rawText, slot.lastDetailRawText) != 0 || s.rawCharging != slot.lastDetailCharging;
  // Recolors the Range row's value (instead of a whole new row) when the learned dry
  // bound has been drifting lower window-over-window (see msv2_read_sensor_auto_calibration's
  // bucketed trend and receiver-snapshot's deriveAutoCal) - an informational nudge, not
  // an alert, so it reuses the diagnostic row already showing the range rather than
  // adding a new one to the fixed row budget every card size already has to fit.
  const uint16_t rangeValueColor = slot.rangeTrendWarn ? slot.warningColor : slot.textColor;

  if (w < 200)
  {
    const int rowStep = clampInt(infoH / 7, 12, 20);
    int rowY = infoY;
    drawDetailStatRowIfChanged(rssiChanged || signalChanged, x + 7, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, s.signalColor, slot.surfaceColor);
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
      drawDetailStatRowIfChanged(rangeChanged, x + 7, rightX, rowY, "Range", s.rangeText, slot.mutedColor, rangeValueColor, slot.surfaceColor);
    }
  }
  else
  {
    const int colW = (w - 14) / 2;
    const int col2X = x + 7 + colW + 6;
    const int col1RightX = x + 7 + colW - 6;
    const int rowStep = clampInt(infoH / 5, 14, 30);
    int rowY = infoY + 2;
    drawDetailStatRowIfChanged(rssiChanged || signalChanged, col2X, rightX, rowY, "RSSI", s.rssiText, slot.mutedColor, s.signalColor, slot.surfaceColor);
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
      drawDetailStatRowIfChanged(rangeChanged, col2X, rightX, rowY, "Range", s.rangeText, slot.mutedColor, rangeValueColor, slot.surfaceColor);
      rowY += rowStep;
    }
    if (s.hasRaw && rowY + 8 <= infoBottom)
    {
      drawDetailStatRowIfChanged(rawChanged, x + 7, col1RightX, rowY, "Raw osc", s.rawText, slot.mutedColor, slot.textColor, slot.surfaceColor);
      drawDetailStatRowIfChanged(rawChanged, col2X, rightX, rowY, "Charging", s.rawCharging ? "Yes" : "No", slot.mutedColor, slot.textColor, slot.surfaceColor);
    }
  }

  // Moisture row and battery header use the same diff-and-redraw idiom as
  // drawPlantCardDataOnly(); neither touches the expanded diagnostic band.
  if (moistureValueChanged || moistureColorChanged)
  {
    if (moistureValueChanged)
    {
      const int artBottomY = y + 24 + max(24, h - 57);
      const int moistRowBottom = y + h - 6 + MOISTURE_RANGE_MARK_ROWS; // over the range mark too
      const int moistRowTop = min(y + h - 30, artBottomY);
      tft.fillRect(x + 7, moistRowTop, w - 14, max(1, moistRowBottom - moistRowTop), slot.surfaceColor);
      if (moistureRowShowsLabel(s.moistureText))
      {
        drawTextLeft("Moisture", x + 7, y + h - 29, 1, slot.mutedColor, slot.surfaceColor);
      }
      if (moistureRangeMarked(s.stale, slot.calibrated, slot.region))
      {
        drawWaterNowRange(x + 7, y + h - 14, w - 14, 8, slot.warningColor);
      }
    }
    drawTextRight(s.moistureText, rightX, y + h - 29, 1, s.moistureColor, slot.surfaceColor);
    drawBar(x + 7, y + h - 14, w - 14, 8, s.moisturePct, s.moistureColor, slot.backgroundColor);
  }

  if (batteryValueChanged)
  {
    drawCardBatteryHeader(x, y, w, h, s.batteryPct, s.batteryColor, slot.surfaceColor, true);
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
  SensorReading reading;
  if (!copySensorReading(slot.sensorId, reading))
  {
    return false;
  }
  bool stale = !reading.seen || (millis() - reading.lastSeenMs > staleSeconds * 1000UL);
  if (stale)
  {
    return false;
  }
  refreshSlotMoistureBounds(slot);
  int16_t percent = moisturePercent(reading.moisture, slot.dry, slot.wet);
  return percent <= MOISTURE_DANGER_PERCENT;
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
#if !TREEBEARD_ENABLE_DISPLAY
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
      // See DETAIL_FULL_RESYNC_MS's own comment - periodically force a full repaint of the
      // diagnostic info band instead of the usual data-only diff, so a stuck partial/corrupted
      // write from a touch-triggered draw self-heals within a bounded window instead of
      // persisting until the card happens to be tapped again.
      uint32_t nowMs = millis();
      if ((int32_t)(nowMs - artSlots[i].nextDetailFullResyncMs) >= 0)
      {
        expandPlantCardInPlace(artSlots[i]);
      }
      else
      {
        drawPlantCardDetailDataOnly(artSlots[i]);
      }
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
  SensorReading readingSnapshot;
  const SensorReading* reading = validSensor && copySensorReading(sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
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
#if !TREEBEARD_ENABLE_DISPLAY
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
#if !TREEBEARD_ENABLE_DISPLAY
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
#if !TREEBEARD_ENABLE_DISPLAY
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
#if !TREEBEARD_ENABLE_DISPLAY
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
#if !TREEBEARD_ENABLE_DISPLAY
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
#if TREEBEARD_ENABLE_WIFI
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
#if !TREEBEARD_ENABLE_DISPLAY
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
    SensorReading readingSnapshot;
    const SensorReading* reading = validSensor && copySensorReading(sensorId, readingSnapshot) ? &readingSnapshot : nullptr;
    const bool stale = reading == nullptr || !reading->seen || (millis() - reading->lastSeenMs > staleSeconds * 1000UL);

    uint8_t oldFrame = slot.frame;
    slot.frame += ART_FRAME_STEP;
    slot.frameIntervalMs = artFrameInterval(slot.art, slot.frame);
    slot.nextFrameMs = now + slot.frameIntervalMs;
    drawPlantArtDiffInCard(slot.x, slot.y, slot.w, slot.h, slot.art, stale ? slot.mutedColor : slot.activeColor, slot.surfaceColor, oldFrame, slot.frame);
  }
}

String sensorDataJson()
{
  StaticJsonDocument<8192> doc;
  JsonArray readings = doc.createNestedArray("sensors");
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    // Copy the whole record inside a short critical section instead of reading its
    // fields one at a time. onResult() (BLE-scan task, core 0) publishes this same record
    // under sensorsMux; using that lock here prevents a torn mix of an old and a new
    // packet (e.g. new moisture paired with a stale wake/raw field).
    portENTER_CRITICAL(&sensorsMux);
    SensorReading r = sensors[sensorId];
    portEXIT_CRITICAL(&sensorsMux);

    if (!r.seen)
    {
      continue;
    }

    JsonObject item = readings.createNestedObject();
    item["sensor"] = sensorId;
    item["wake"] = r.wake;
    // `moisture` is the presentation value used by every UI. Keep the latest
    // unsmoothed receiver-derived reading beside it for diagnostics and cloud history.
    item["moisture"] = r.moisture;
    item["measuredMoisture"] = r.measuredMoisture;
    item["moistureScale"] = moistureScaleVersion(r.wideRaw, r.caplessTank, r.msv4);
    if (r.batteryPercent != BATTERY_UNKNOWN)
    {
      item["batteryPercent"] = r.batteryPercent;
    }
    item["rssi"] = r.rssi;
    item["ageMs"] = millis() - r.lastSeenMs;
    item["missed"] = r.missed;
    item["received"] = r.received;
    item["missedTotal"] = r.missedTotal;

    // Raw, uncalibrated values as transmitted by 2026-07-28-rework probes - see the
    // SensorReading.hasRaw comment. Only present for probes that actually sent them;
    // omitted (not just zeroed) for probes still on pre-rework legacy firmware so this
    // field's presence itself signals which protocol a given sensor is on.
    if (r.hasRaw)
    {
      JsonObject rawItem = item.createNestedObject("raw");
      // wideRaw (MSV3/FDC2214) sensors report fdcCounts instead of moistureKHz -
      // omitted rather than zeroed, same "presence signals the protocol" reasoning
      // as the hasRaw/legacy split above, so a client can tell the two apart.
      if (r.wideRaw)
      {
        rawItem["fdcCounts"] = r.rawFdcCounts;
        // Two independent raw observables, not a computed/compensated value - see
        // SensorReading's own comment. Each is omitted (not zeroed) when invalid/absent, same
        // convention as fdcCounts/moistureKHz above; tankPhase is included whenever this probe
        // sends diagnostics at all, valid or not, since the phase itself isn't a measurement.
        if (r.hasMsv3Diagnostics)
        {
          if (r.boardTempValid) rawItem["boardTempQuarterC"] = r.rawBoardTempQuarterC;
          if (r.bareTankValid) rawItem["bareTankFdcCounts"] = r.rawBareTankFdcCounts;
          if (r.caplessTank) rawItem["caplessTank"] = true;
          rawItem["tankPhase"] = r.tankPhase;
          if (r.msv4)
          {
            JsonObject msv4Item = rawItem.createNestedObject("msv4");
            msv4Item["cmDeciPf"] = msv4CmDeciPf(r.rawBareTankFdcCounts);
            msv4Item["fitRmsE4"] = r.msv4FitRmsE4;
            if (msv4CdCentiPf(r.rawBareTankFdcCounts) != 0)
            {
              msv4Item["cdCentiPf"] = msv4CdCentiPf(r.rawBareTankFdcCounts);
            }
            if (r.msv4CpDeciPf != 0) msv4Item["cpDeciPf"] = r.msv4CpDeciPf;
            if (r.msv4DieTempValid) msv4Item["dieTempQuarterC"] = r.msv4DieTempQuarterC;
          }
        }
      }
      else
      {
        rawItem["moistureKHz"] = r.rawMoistureKHz;
      }
      rawItem["batteryMv"] = r.rawBatteryMv;
      rawItem["charging"] = r.rawCharging;
    }

    if (r.wideRaw)
    {
      JsonObject compensated = item.createNestedObject("temperatureCompensation");
      compensated["valid"] = r.temperatureCompensationValid;
      compensated["mode"] = temperatureCompensationModeName(
          r.temperatureCompensationConfiguredMode);
      compensated["effectiveMode"] = temperatureCompensationModeName(
          r.temperatureCompensationEffectiveMode);
      compensated["confidencePercent"] = r.temperatureCompensationConfidencePercent;
      compensated["modelVersion"] = r.temperatureCompensationModelVersion;
      compensated["gainPerC"] = r.temperatureCompensationGainPerC;
      compensated["referenceC"] = r.temperatureCompensationReferenceC;
      compensated["epoch"] = r.temperatureCompensationEpoch;
      if (r.temperatureCompensationValid)
      {
        compensated["logRatioPpm"] = r.temperatureCompensatedLogRatioPpm;
        compensated["equivalentMoisture"] = r.temperatureCompensatedEquivalentMoisture;
      }
    }

    // Cached result of the periodic cloud auto-calibration pull (see
    // runCloudAutoCalPull) - lets the browser use the same learned range/schedule the
    // physical display uses, without minting its own share token or hitting the cloud
    // directly (this data changes slowly, so a receiver-cached copy is plenty fresh).
    AutoCalState learned;
    copyAutoCalState(sensorId, learned);
    JsonObject autoCalItem = item.createNestedObject("autoCal");
    autoCalItem["ready"] = learned.ready;
    autoCalItem["dryBound"] = learned.dryBound;
    autoCalItem["wetBound"] = learned.wetBound;
    autoCalItem["daysSinceLastWatering"] = learned.daysSinceLastWatering;
    autoCalItem["lastWateredAtMs"] = learned.lastWateredAtMs;
    autoCalItem["dryBoundTrendingLower"] = learned.dryBoundTrendingLower;
    autoCalItem["region"] = learned.region;
  }

  String output;
  serializeJson(doc, output);
  return output;
}

String historyOutboxPrefixJson(uint32_t serializedAtMs)
{
  size_t seenSensors = 0;
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    SensorReading reading;
    if (copySensorReading(sensorId, reading) && reading.seen)
    {
      seenSensors++;
    }
  }

  // Only the receiver metadata and latest-value cache are retained as one String. History
  // rows are emitted by HistoryOutboxStream below, one bounded row at a time.
  String output;
  if (!output.reserve(512 + seenSensors * 480))
  {
    Serial.println("[Treebeard cloud] could not reserve compact history prefix");
    return "";
  }
  output += F("{\"receiverId\":\"");
  output += receiverId;
  output += F("\",\"claimCode\":\"");
  output += receiverClaimCode;
  output += F("\",\"build\":\"");
  output += TREEBEARD_BUILD_TAG;
  output += F("\",\"sentAtMs\":");
  output += (unsigned long)serializedAtMs;
  output += F(",\"historyOnly\":true,\"latestPlants\":[");

  bool firstPlant = true;
  for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
  {
    SensorReading reading;
    if (!copySensorReading(sensorId, reading) || !reading.seen)
    {
      continue;
    }
    if (!firstPlant) output += ',';
    firstPlant = false;
    output += F("{\"sensor\":");
    output += sensorId;
    output += F(",\"wake\":");
    output += (unsigned long)reading.wake;
    output += F(",\"moisture\":");
    output += reading.moisture;
    output += F(",\"measuredMoisture\":");
    output += reading.measuredMoisture;
    output += F(",\"moistureScale\":");
    output += moistureScaleVersion(reading.wideRaw, reading.caplessTank, reading.msv4);
    if (reading.batteryPercent != BATTERY_UNKNOWN)
    {
      output += F(",\"batteryPercent\":");
      output += reading.batteryPercent;
    }
    output += F(",\"rssi\":");
    output += reading.rssi;
    output += F(",\"ageMs\":");
    output += (unsigned long)(serializedAtMs - reading.lastSeenMs);
    if (reading.wideRaw)
    {
      output += F(",\"temperatureCompensation\":{\"valid\":");
      output += reading.temperatureCompensationValid ? F("true") : F("false");
      output += F(",\"mode\":\"");
      output += temperatureCompensationModeName(reading.temperatureCompensationConfiguredMode);
      output += F("\",\"effectiveMode\":\"");
      output += temperatureCompensationModeName(reading.temperatureCompensationEffectiveMode);
      output += F("\",\"confidencePercent\":");
      output += reading.temperatureCompensationConfidencePercent;
      output += F(",\"modelVersion\":");
      output += reading.temperatureCompensationModelVersion;
      output += F(",\"gainPerC\":");
      output += String(reading.temperatureCompensationGainPerC, 6);
      output += F(",\"referenceC\":");
      output += String(reading.temperatureCompensationReferenceC, 2);
      output += F(",\"epoch\":");
      output += reading.temperatureCompensationEpoch;
      if (reading.temperatureCompensationValid)
      {
        output += F(",\"logRatioPpm\":");
        output += reading.temperatureCompensatedLogRatioPpm;
        output += F(",\"equivalentMoisture\":");
        output += reading.temperatureCompensatedEquivalentMoisture;
      }
      output += '}';
    }
    if (reading.hasRaw)
    {
      if (reading.wideRaw)
      {
        output += F(",\"raw\":{\"fdcCounts\":");
        output += (unsigned long)reading.rawFdcCounts;
        if (reading.hasMsv3Diagnostics)
        {
          if (reading.boardTempValid)
          {
            output += F(",\"boardTempQuarterC\":");
            output += reading.rawBoardTempQuarterC;
          }
          if (reading.bareTankValid)
          {
            output += F(",\"bareTankFdcCounts\":");
            output += (unsigned long)reading.rawBareTankFdcCounts;
          }
          if (reading.caplessTank) output += F(",\"caplessTank\":true");
          output += F(",\"tankPhase\":");
          output += reading.tankPhase;
          if (reading.msv4)
          {
            appendMsv4RawJson(output, reading.rawBareTankFdcCounts, reading.msv4FitRmsE4,
                              reading.msv4CpDeciPf, reading.msv4DieTempValid,
                              reading.msv4DieTempQuarterC);
          }
        }
      }
      else
      {
        output += F(",\"raw\":{\"moistureKHz\":");
        output += reading.rawMoistureKHz;
      }
      output += F(",\"batteryMv\":");
      output += reading.rawBatteryMv;
      output += F(",\"charging\":");
      output += reading.rawCharging ? F("true") : F("false");
      output += '}';
    }
    output += '}';
  }

  output += F("],\"historyBatch\":[");
  return output;
}

void appendHistoryOutboxRowJson(String& output, const PendingHistoryReading& pending,
                                uint32_t serializedAtMs, bool prependComma)
{
  if (prependComma) output += ',';
  char observationId[24];
  snprintf(observationId, sizeof(observationId), "%08lX-%08lX",
           (unsigned long)pending.bootId, (unsigned long)pending.sequence);
  output += F("{\"observationId\":\"");
  output += observationId;
  output += F("\",\"queuedAgeMs\":");
  output += pending.bootId == historyQueueBootId
      ? (unsigned long)(serializedAtMs - pending.capturedMs)
      : 0UL;
  if (pending.observedAtMs != 0)
  {
    char observedAtText[24];
    snprintf(observedAtText, sizeof(observedAtText), "%llu",
             (unsigned long long)pending.observedAtMs);
    output += F(",\"observedAtMs\":");
    output += observedAtText;
  }
  output += F(",\"sensor\":");
  output += pending.sensorId;
  output += F(",\"wake\":");
  output += (unsigned long)pending.wake;
  output += F(",\"moisture\":");
  output += pending.moisture;
  output += F(",\"measuredMoisture\":");
  output += pending.measuredMoisture;
  output += F(",\"moistureScale\":");
  output += pending.moistureScale;
  if (pending.batteryPercent != BATTERY_UNKNOWN)
  {
    output += F(",\"batteryPercent\":");
    output += pending.batteryPercent;
  }
  output += F(",\"rssi\":");
  output += pending.rssi;
  output += F(",\"ageMs\":0");
  int32_t compensatedLogRatioPpm = 0;
  // An MSV4 row keeps its die temperature in this slot (msv4EncodeDieTemp()), not a log ratio.
  if (pending.wideRaw && pending.moistureScale != MSV4_MOISTURE_SCALE_VERSION &&
      decodeTemperatureCompensatedLogRatioPpm(pending.rawMoistureKHz, compensatedLogRatioPpm))
  {
    output += F(",\"temperatureCompensatedLogRatioPpm\":");
    output += compensatedLogRatioPpm;
    output += F(",\"temperatureCompensationModelVersion\":");
    output += TEMPERATURE_COMPENSATION_MODEL_VERSION;
  }
  if (pending.hasRaw)
  {
    if (pending.wideRaw)
    {
      output += F(",\"raw\":{\"fdcCounts\":");
      output += (unsigned long)historyFdcCountsField(pending.rawFdcCounts, pending.moistureScale);
      if (pending.hasMsv3Diagnostics)
      {
        if (pending.boardTempValid)
        {
          output += F(",\"boardTempQuarterC\":");
          output += pending.rawBoardTempQuarterC;
        }
        if (pending.bareTankValid)
        {
          output += F(",\"bareTankFdcCounts\":");
          output += (unsigned long)pending.rawBareTankFdcCounts;
        }
        if (pending.moistureScale == MSV3_CAPLESS_MOISTURE_SCALE_VERSION)
          output += F(",\"caplessTank\":true");
        output += F(",\"tankPhase\":");
        output += pending.tankPhase;
        if (pending.moistureScale == MSV4_MOISTURE_SCALE_VERSION)
          appendMsv4HistoryRawJson(output, pending.rawBareTankFdcCounts, pending.rawMoistureKHz,
                                     pending.rawFdcCounts);
      }
    }
    else
    {
      output += F(",\"raw\":{\"moistureKHz\":");
      output += pending.rawMoistureKHz;
    }
    output += F(",\"batteryMv\":");
    output += pending.rawBatteryMv;
    output += F(",\"charging\":");
    output += pending.rawCharging ? F("true") : F("false");
    output += '}';
  }
  output += '}';
}

// Pull-based JSON body used by HTTPClient::sendRequest(Stream*, size). The prefix is small
// (receiver metadata plus at most 16 latest values); every history row reuses rowChunk_'s
// single bounded allocation. This keeps the maximum contiguous payload allocation roughly
// constant as the batch grows, while still providing Content-Length to the Edge Function.
class HistoryOutboxStream : public Stream
{
public:
  HistoryOutboxStream(const String& prefix, const PendingHistoryReading* historyBatch,
                      size_t historyCount, uint32_t serializedAtMs)
      : prefix_(prefix), historyBatch_(historyBatch), historyCount_(historyCount),
        serializedAtMs_(serializedAtMs)
  {
    valid_ = prefix_.length() > 0 && historyBatch_ != nullptr && historyCount_ > 0 &&
             rowChunk_.reserve(HISTORY_JSON_BYTES_PER_ROW + 32);
    totalBytes_ = prefix_.length() + 2; // closing "]}"
    for (size_t i = 0; valid_ && i < historyCount_; i++)
    {
      rowChunk_ = "";
      appendHistoryOutboxRowJson(rowChunk_, historyBatch_[i], serializedAtMs_, i > 0);
      valid_ = rowChunk_.endsWith("}");
      totalBytes_ += rowChunk_.length();
    }
    resetReader();
  }

  bool valid() const { return valid_; }
  size_t contentLength() const { return valid_ ? totalBytes_ : 0; }

  int available() override
  {
    if (!valid_ || emittedBytes_ >= totalBytes_) return 0;
    size_t remaining = totalBytes_ - emittedBytes_;
    return remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
  }

  int read() override
  {
    uint8_t byte = 0;
    return readBytes(&byte, 1) == 1 ? byte : -1;
  }

  int peek() override
  {
    if (!ensureReadableSegment()) return -1;
    if (section_ == PREFIX) return (uint8_t)prefix_[prefixOffset_];
    if (section_ == ROWS) return (uint8_t)rowChunk_[rowOffset_];
    return (uint8_t)closing_[closingOffset_];
  }

  size_t readBytes(uint8_t* buffer, size_t length) override
  {
    if (buffer == nullptr || length == 0 || !valid_) return 0;
    size_t copied = 0;
    while (copied < length && ensureReadableSegment())
    {
      const char* source = nullptr;
      size_t availableBytes = 0;
      if (section_ == PREFIX)
      {
        source = prefix_.c_str() + prefixOffset_;
        availableBytes = prefix_.length() - prefixOffset_;
      }
      else if (section_ == ROWS)
      {
        source = rowChunk_.c_str() + rowOffset_;
        availableBytes = rowChunk_.length() - rowOffset_;
      }
      else
      {
        source = closing_ + closingOffset_;
        availableBytes = 2 - closingOffset_;
      }
      const size_t take = min(length - copied, availableBytes);
      memcpy(buffer + copied, source, take);
      copied += take;
      emittedBytes_ += take;
      if (section_ == PREFIX) prefixOffset_ += take;
      else if (section_ == ROWS) rowOffset_ += take;
      else closingOffset_ += take;
    }
    return copied;
  }

  size_t write(uint8_t) override { return 0; }

private:
  enum Section : uint8_t { PREFIX, ROWS, CLOSING, DONE };
  const String& prefix_;
  const PendingHistoryReading* historyBatch_ = nullptr;
  size_t historyCount_ = 0;
  uint32_t serializedAtMs_ = 0;
  String rowChunk_;
  size_t totalBytes_ = 0;
  size_t emittedBytes_ = 0;
  size_t prefixOffset_ = 0;
  size_t historyIndex_ = 0;
  size_t rowOffset_ = 0;
  size_t closingOffset_ = 0;
  Section section_ = PREFIX;
  bool valid_ = false;
  static constexpr char closing_[3] = "]}";

  void resetReader()
  {
    emittedBytes_ = 0;
    prefixOffset_ = 0;
    historyIndex_ = 0;
    rowOffset_ = 0;
    closingOffset_ = 0;
    rowChunk_ = "";
    section_ = PREFIX;
  }

  bool ensureReadableSegment()
  {
    while (valid_ && section_ != DONE)
    {
      if (section_ == PREFIX)
      {
        if (prefixOffset_ < prefix_.length()) return true;
        section_ = ROWS;
      }
      else if (section_ == ROWS)
      {
        if (rowOffset_ < rowChunk_.length()) return true;
        if (historyIndex_ >= historyCount_)
        {
          section_ = CLOSING;
          continue;
        }
        rowChunk_ = "";
        appendHistoryOutboxRowJson(rowChunk_, historyBatch_[historyIndex_],
                                   serializedAtMs_, historyIndex_ > 0);
        historyIndex_++;
        rowOffset_ = 0;
        if (!rowChunk_.endsWith("}"))
        {
          valid_ = false;
          return false;
        }
      }
      else if (section_ == CLOSING)
      {
        if (closingOffset_ < 2) return true;
        section_ = DONE;
      }
    }
    return false;
  }
};

constexpr char HistoryOutboxStream::closing_[3];

String historyOutboxJson(const PendingHistoryReading* historyBatch, size_t historyCount)
{
  const uint32_t serializedAtMs = millis();
  String output = historyOutboxPrefixJson(serializedAtMs);
  if (output.length() == 0 ||
      !output.reserve(output.length() + historyCount * HISTORY_JSON_BYTES_PER_ROW + 2))
  {
    return "";
  }
  for (size_t i = 0; historyBatch != nullptr && i < historyCount; i++)
  {
    appendHistoryOutboxRowJson(output, historyBatch[i], serializedAtMs, i > 0);
  }
  output += F("]}");
  return output;
}

String snapshotJsonWithHistory(const PendingHistoryReading* historyBatch, size_t historyCount)
{
  // Keep the ArduinoJson document limited to the already-proven latest-state snapshot.
  // Queue rows are numeric plus a generated hex ID, so they are appended below after
  // serialization. Building 32 nested JsonObjects here required a ~30KB temporary heap
  // allocation immediately before TLS connect and caused repeat connections to fail on
  // the non-PSRAM build even though total free heap remained adequate.
  DynamicJsonDocument doc(18432);
  const uint32_t serializedAtMs = millis();
  doc["receiverId"] = receiverId;
  doc["claimCode"] = receiverClaimCode;
  doc["build"] = TREEBEARD_BUILD_TAG;
  doc["sentAtMs"] = serializedAtMs;

  JsonObject config = doc.createNestedObject("config");
  config["schema"] = 1;
  config["version"] = uiConfigVersion;
  config["updatedMs"] = uiUpdatedMs;
  config["updatedBy"] = uiUpdatedBy;
#if TREEBEARD_ENABLE_WIFI
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
          bool autoCalEnabled = false;
          const char* temperatureCompensationSetting = "learning";
          if (!sourceMoisture.isNull())
          {
            dry = sourceMoisture["dry"] | dry;
            wet = sourceMoisture["wet"] | wet;
            autoCalEnabled = sourceMoisture["auto"] | false;
            temperatureCompensationSetting =
                sourceMoisture["temperatureCompensation"] | "learning";
          }
          JsonObject moisture = item.createNestedObject("moisture");
          moisture["dry"] = dry;
          moisture["wet"] = wet;
          moisture["auto"] = autoCalEnabled;
          moisture["temperatureCompensation"] = temperatureCompensationSetting;
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
#if TREEBEARD_ENABLE_WIFI
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
    SensorReading r;
    copySensorReading(sensorId, r);
    if (!r.seen)
    {
      continue;
    }

    JsonObject item = plants.createNestedObject();
    item["sensor"] = sensorId;
    item["wake"] = r.wake;
    // The cloud latest snapshot should render the same clean value as the receiver,
    // while receiver-snapshot stores measuredMoisture in the original history series.
    item["moisture"] = r.moisture;
    item["measuredMoisture"] = r.measuredMoisture;
    item["moistureScale"] = moistureScaleVersion(r.wideRaw, r.caplessTank, r.msv4);
    if (r.batteryPercent != BATTERY_UNKNOWN)
    {
      item["batteryPercent"] = r.batteryPercent;
    }
    item["rssi"] = r.rssi;
    item["ageMs"] = millis() - r.lastSeenMs;
    item["received"] = r.received;
    item["missedTotal"] = r.missedTotal;

    // Preserve the original probe wire value in the cloud snapshot/history as well as
    // locally under /data. The display transform can then be revised or audited without
    // throwing away any FDC/oscillator resolution.
    if (r.hasRaw)
    {
      JsonObject rawItem = item.createNestedObject("raw");
      if (r.wideRaw)
      {
        rawItem["fdcCounts"] = r.rawFdcCounts;
        if (r.hasMsv3Diagnostics)
        {
          if (r.boardTempValid) rawItem["boardTempQuarterC"] = r.rawBoardTempQuarterC;
          if (r.bareTankValid) rawItem["bareTankFdcCounts"] = r.rawBareTankFdcCounts;
          if (r.caplessTank) rawItem["caplessTank"] = true;
          rawItem["tankPhase"] = r.tankPhase;
          if (r.msv4)
          {
            JsonObject msv4Item = rawItem.createNestedObject("msv4");
            msv4Item["cmDeciPf"] = msv4CmDeciPf(r.rawBareTankFdcCounts);
            msv4Item["fitRmsE4"] = r.msv4FitRmsE4;
            if (msv4CdCentiPf(r.rawBareTankFdcCounts) != 0)
            {
              msv4Item["cdCentiPf"] = msv4CdCentiPf(r.rawBareTankFdcCounts);
            }
            if (r.msv4CpDeciPf != 0) msv4Item["cpDeciPf"] = r.msv4CpDeciPf;
            if (r.msv4DieTempValid) msv4Item["dieTempQuarterC"] = r.msv4DieTempQuarterC;
          }
        }
      }
      else
      {
        rawItem["moistureKHz"] = r.rawMoistureKHz;
      }
      rawItem["batteryMv"] = r.rawBatteryMv;
      rawItem["charging"] = r.rawCharging;
    }

    if (r.wideRaw)
    {
      JsonObject compensated = item.createNestedObject("temperatureCompensation");
      compensated["valid"] = r.temperatureCompensationValid;
      compensated["mode"] = temperatureCompensationModeName(
          r.temperatureCompensationConfiguredMode);
      compensated["effectiveMode"] = temperatureCompensationModeName(
          r.temperatureCompensationEffectiveMode);
      compensated["confidencePercent"] = r.temperatureCompensationConfidencePercent;
      compensated["modelVersion"] = r.temperatureCompensationModelVersion;
      compensated["gainPerC"] = r.temperatureCompensationGainPerC;
      compensated["referenceC"] = r.temperatureCompensationReferenceC;
      compensated["epoch"] = r.temperatureCompensationEpoch;
      if (r.temperatureCompensationValid)
      {
        compensated["logRatioPpm"] = r.temperatureCompensatedLogRatioPpm;
        compensated["equivalentMoisture"] = r.temperatureCompensatedEquivalentMoisture;
      }
    }

    // Same cached auto-calibration snapshot sensorDataJson() sends to the local /data
    // endpoint - without this, the cloud (and therefore the Netlify dashboard) has no way
    // to know a sensor is auto-calibrated, and falls back to the static manual/preset
    // dry/wet bounds even when the display itself is using learned ones.
    AutoCalState learned;
    copyAutoCalState(sensorId, learned);
    JsonObject autoCalItem = item.createNestedObject("autoCal");
    autoCalItem["ready"] = learned.ready;
    autoCalItem["dryBound"] = learned.dryBound;
    autoCalItem["wetBound"] = learned.wetBound;
    autoCalItem["daysSinceLastWatering"] = learned.daysSinceLastWatering;
    autoCalItem["lastWateredAtMs"] = learned.lastWateredAtMs;
    autoCalItem["dryBoundTrendingLower"] = learned.dryBoundTrendingLower;
    autoCalItem["region"] = learned.region;
  }

  if (doc.overflowed())
  {
    Serial.println("[Treebeard cloud] snapshot JSON capacity exceeded; retaining history queue");
    return "";
  }

  String output;
  serializeJson(doc, output);
  // Presence of historyBatch (even when empty) tells the cloud this receiver supports
  // lossless event ingestion. All appended string content is fixed JSON syntax, integers,
  // or our own hexadecimal observation ID, so no untrusted string escaping is bypassed.
  if (output.length() == 0 || output[output.length() - 1] != '}' ||
      !output.reserve(output.length() + historyCount * HISTORY_JSON_BYTES_PER_ROW + 24))
  {
    Serial.println("[Treebeard cloud] could not reserve history batch payload");
    return "";
  }
  output.remove(output.length() - 1);
  output += F(",\"historyBatch\":[");
  for (size_t i = 0; historyBatch != nullptr && i < historyCount; i++)
  {
    const PendingHistoryReading& pending = historyBatch[i];
    if (i > 0) output += ',';
    char observationId[24];
    snprintf(observationId, sizeof(observationId), "%08lX-%08lX",
             (unsigned long)pending.bootId, (unsigned long)pending.sequence);
    output += F("{\"observationId\":\"");
    output += observationId;
    output += F("\",\"queuedAgeMs\":");
    output += pending.bootId == historyQueueBootId
        ? (unsigned long)(serializedAtMs - pending.capturedMs)
        : 0UL;
    if (pending.observedAtMs != 0)
    {
      char observedAtText[24];
      snprintf(observedAtText, sizeof(observedAtText), "%llu",
               (unsigned long long)pending.observedAtMs);
      output += F(",\"observedAtMs\":");
      output += observedAtText;
    }
    output += F(",\"sensor\":");
    output += pending.sensorId;
    output += F(",\"wake\":");
    output += (unsigned long)pending.wake;
    output += F(",\"moisture\":");
    output += pending.moisture;
    output += F(",\"measuredMoisture\":");
    output += pending.measuredMoisture;
    output += F(",\"moistureScale\":");
    output += pending.moistureScale;
    if (pending.batteryPercent != BATTERY_UNKNOWN)
    {
      output += F(",\"batteryPercent\":");
      output += pending.batteryPercent;
    }
    output += F(",\"rssi\":");
    output += pending.rssi;
    output += F(",\"ageMs\":0");
    int32_t compensatedLogRatioPpm = 0;
    // An MSV4 row keeps its die temperature in this slot (msv4EncodeDieTemp()), not a log ratio.
    if (pending.wideRaw && pending.moistureScale != MSV4_MOISTURE_SCALE_VERSION &&
        decodeTemperatureCompensatedLogRatioPpm(pending.rawMoistureKHz,
                                                 compensatedLogRatioPpm))
    {
      output += F(",\"temperatureCompensatedLogRatioPpm\":");
      output += compensatedLogRatioPpm;
      output += F(",\"temperatureCompensationModelVersion\":");
      output += TEMPERATURE_COMPENSATION_MODEL_VERSION;
    }
    if (pending.hasRaw)
    {
      if (pending.wideRaw)
      {
        output += F(",\"raw\":{\"fdcCounts\":");
        output += (unsigned long)historyFdcCountsField(pending.rawFdcCounts, pending.moistureScale);
        if (pending.hasMsv3Diagnostics)
        {
          if (pending.boardTempValid)
          {
            output += F(",\"boardTempQuarterC\":");
            output += pending.rawBoardTempQuarterC;
          }
        if (pending.bareTankValid)
        {
          output += F(",\"bareTankFdcCounts\":");
          output += (unsigned long)pending.rawBareTankFdcCounts;
        }
        if (pending.moistureScale == MSV3_CAPLESS_MOISTURE_SCALE_VERSION)
          output += F(",\"caplessTank\":true");
          output += F(",\"tankPhase\":");
          output += pending.tankPhase;
          if (pending.moistureScale == MSV4_MOISTURE_SCALE_VERSION)
            appendMsv4HistoryRawJson(output, pending.rawBareTankFdcCounts, pending.rawMoistureKHz,
                                     pending.rawFdcCounts);
        }
      }
      else
      {
        output += F(",\"raw\":{\"moistureKHz\":");
        output += pending.rawMoistureKHz;
      }
      output += F(",\"batteryMv\":");
      output += pending.rawBatteryMv;
      output += F(",\"charging\":");
      output += pending.rawCharging ? F("true") : F("false");
      output += '}';
    }
    output += '}';
  }
  output += F("]}");
  return output;
}

String snapshotJson()
{
  return snapshotJsonWithHistory(nullptr, 0);
}

#if TREEBEARD_ENABLE_WIFI
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
  cacheTemperatureCompensationModes(nextConfig.as<JsonVariantConst>());
  cacheUiMetadata(nextConfig["_meta"]);
  displayDirty = true;
  scheduleCloudSnapshotAfterStateChange();
  return true;
}

// A failed HTTPClient::begin() can leave resources live until destruction. Each paced
// operation now owns fresh client objects (a reused secure-client context repeatedly failed
// after its first successful live request); explicitly stop both paths before their scope
// ends so TLS is released while the request payload still has a deterministic lifetime.
void stopCloudClients(WiFiClientSecure& secureClient, WiFiClient& plainClient)
{
  secureClient.stop();
  plainClient.stop();
}

void beginCloudRadioSlot()
{
#if TREEBEARD_ENABLE_BLE
  cloudRadioExclusive = true;
  lastRadioSlotAtMs = millis();
  lastRadioSlotWasScanning = bleScan != nullptr && bleScan->isScanning();
  uint32_t stopStartMs = millis();
  if (bleScan != nullptr)
  {
    bleScan->stop();
  }
  lastRadioSlotStopCallMs = millis() - stopStartMs;
  // Let the controller finish the scan-stop event before starting TCP/TLS on the same RF.
  vTaskDelay(pdMS_TO_TICKS(75));
  lastRadioSlotStillScanningAfterSettle = bleScan != nullptr && bleScan->isScanning();
#endif
}

void endCloudRadioSlot()
{
#if TREEBEARD_ENABLE_BLE
  cloudRadioExclusive = false;
#endif
}

// 2026-08-30: total free heap alone does not guarantee the snapshot uploader's TLS connect
// can proceed - WiFiClientSecure's mbedTLS session needs a contiguous allocation, and
// fragmentation can leave the largest free block well under total free heap even when the
// plain free-heap guard above passes. Live evidence: a running receiver reported 60648 bytes
// total free (comfortably over CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES) but only 20468 bytes in its
// largest internal free block, and its snapshot uploads were failing with a generic
// "connection refused".
//
// CORRECTED same day, after a first attempt at this fix used 49152 (48KiB) and a threshold
// this high blocked ALL FIVE cloud functions on a freshly-booted device, whose largest
// contiguous internal block never exceeded ~34804 bytes even brand new (this receiver's own
// GFX framebuffer/BLE/WebServer/LittleFS static allocations apparently leave less headroom
// than assumed) - including config-pull/display-sync, which were succeeding fine before any
// of this change. That ruled out "single 30-40KB contiguous allocation" as this board's real
// constraint. Scoped down to ONLY the snapshot uploader (see its use below, after the JSON
// batch payload - the largest, most fragmentation-prone allocation in this whole call chain -
// has already been built) with a lower threshold: comfortably above the one confirmed-failing
// datapoint (20468) while leaving real operating margin below the ~34804 fresh-boot ceiling.
// This is calibration from two data points, not a proven exact mbedTLS requirement - if
// snapshot uploads still intermittently fail at this threshold, check the added
// largestFreeBlockInternalBytes/psramFreeBytes health fields before raising it again.
const uint32_t CLOUD_SNAPSHOT_MIN_LARGEST_INTERNAL_BLOCK_BYTES = 24576;

bool cloudHeapHasContiguousRoomForTls()
{
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
         CLOUD_SNAPSHOT_MIN_LARGEST_INTERNAL_BLOCK_BYTES;
}

void runCloudBootstrap(uint32_t now)
{
  if (strlen(TREEBEARD_CLOUD_BOOTSTRAP_URL) == 0)
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

  uint32_t freeHeapBeforeBootstrap = ESP.getFreeHeap();
  if (freeHeapBeforeBootstrap < minFreeHeapEverBytes) minFreeHeapEverBytes = freeHeapBeforeBootstrap;
  if (freeHeapBeforeBootstrap < CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES)
  {
    setCloudBootstrapLastError("skipped: low heap");
    nextCloudBootstrapMs = now + CLOUD_LOW_HEAP_RETRY_MS;
    return;
  }

  const bool isHttps = strncmp(TREEBEARD_CLOUD_BOOTSTRAP_URL, "https://", 8) == 0;
  const bool isHttp = strncmp(TREEBEARD_CLOUD_BOOTSTRAP_URL, "http://", 7) == 0;
  if (!isHttps && !isHttp)
  {
    cloudBootstrapLastAttemptMs = now;
    cloudBootstrapLastHttpStatus = 0;
    setCloudBootstrapLastError("bootstrap URL must start with http:// or https://");
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    return;
  }

  Serial.print("[Treebeard cloud] bootstrapping receiver at ");
  Serial.println(TREEBEARD_CLOUD_BOOTSTRAP_URL);

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(TREEBEARD_TRUSTED_ROOT_CA_PEM);
    began = http.begin(secureClient, TREEBEARD_CLOUD_BOOTSTRAP_URL);
  }
  else
  {
    began = http.begin(plainClient, TREEBEARD_CLOUD_BOOTSTRAP_URL);
  }

  cloudBootstrapLastAttemptMs = now;
  cloudBootstrapLastHttpStatus = 0;
  if (!began)
  {
    // 2026-08-23: same defensive cleanup as runCloudSnapshotUploader() - a failed connect
    // attempt has been observed to leave real resources (heap, and possibly a TCP/TLS
    // session) unrecovered without an explicit stop(), across every one of these five
    // cloud call sites, not just the snapshot uploader.
    stopCloudClients(secureClient, plainClient);
    setCloudBootstrapLastError("could not start HTTP client");
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    return;
  }

  StaticJsonDocument<512> requestDoc;
  requestDoc["receiverId"] = receiverId;
  requestDoc["receiverSecret"] = receiverSecret;
  requestDoc["claimCode"] = receiverClaimCode;
  requestDoc["build"] = TREEBEARD_BUILD_TAG;
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
        nextCloudFullSnapshotMs = millis() + CLOUD_FULL_SNAPSHOT_INTERVAL_MS;
        Serial.println("[Treebeard cloud] bootstrap ok; cloud sync enabled");
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
    Serial.print("[Treebeard cloud] bootstrap failed: ");
    Serial.println(error);
  }
  else
  {
    String error = http.errorToString(status);
    setCloudBootstrapLastError(error.c_str());
    nextCloudBootstrapMs = now + CLOUD_BOOTSTRAP_RETRY_MS;
    Serial.print("[Treebeard cloud] bootstrap failed: ");
    Serial.println(error);
  }

  http.end();
  stopCloudClients(secureClient, plainClient);
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

  uint32_t freeHeapBeforeConfigPull = ESP.getFreeHeap();
  if (freeHeapBeforeConfigPull < minFreeHeapEverBytes) minFreeHeapEverBytes = freeHeapBeforeConfigPull;
  if (freeHeapBeforeConfigPull < CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES)
  {
    setCloudConfigLastError("skipped: low heap");
    nextCloudConfigPullMs = now + CLOUD_LOW_HEAP_RETRY_MS;
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

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(TREEBEARD_TRUSTED_ROOT_CA_PEM);
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
    stopCloudClients(secureClient, plainClient);
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
      Serial.print("[Treebeard cloud] applied config v");
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
  stopCloudClients(secureClient, plainClient);
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

  uint32_t freeHeapBeforeDisplaySync = ESP.getFreeHeap();
  if (freeHeapBeforeDisplaySync < minFreeHeapEverBytes) minFreeHeapEverBytes = freeHeapBeforeDisplaySync;
  if (freeHeapBeforeDisplaySync < CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES)
  {
    setCloudDisplayLastError("skipped: low heap");
    nextCloudDisplaySyncMs = now + CLOUD_LOW_HEAP_RETRY_MS;
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

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(TREEBEARD_TRUSTED_ROOT_CA_PEM);
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
    stopCloudClients(secureClient, plainClient);
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
  // Always retain the Edge Function's structured error body. The old path reduced every
  // HTTP failure to just "HTTP 400", which made the deployed display-settings RPC failure
  // impossible to distinguish from payload/auth/version errors at the receiver.
  String response = status > 0 ? http.getString() : "";
  bool displaySyncSucceeded = false;

  if (status >= 200 && status < 300)
  {
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
      displaySyncSucceeded = true;
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
      displaySyncSucceeded = true;
      Serial.print("[Treebeard cloud] display settings v");
      Serial.println(version);
    }
  }
  else if (status > 0)
  {
    String error = "HTTP ";
    error += status;
    if (response.length() > 0)
    {
      StaticJsonDocument<384> errorDoc;
      DeserializationError parseError = deserializeJson(errorDoc, response);
      const char* backendError = parseError ? nullptr : errorDoc["error"] | nullptr;
      if (backendError != nullptr && backendError[0] != '\0')
      {
        error += ": ";
        error += backendError;
      }
    }
    setCloudDisplayLastError(error.c_str());
  }
  else
  {
    String error = http.errorToString(status);
    setCloudDisplayLastError(error.c_str());
  }

  http.end();
  stopCloudClients(secureClient, plainClient);
  bool stillPending;
  portENTER_CRITICAL(&displaySettingsMux);
  stillPending = displaySettings.pendingCloudUpdate;
  portEXIT_CRITICAL(&displaySettingsMux);
  if (displaySyncSucceeded)
  {
    cloudDisplayConsecutiveFailures = 0;
    nextCloudDisplaySyncMs = millis() +
      (stillPending && cloudDisplayApiReady ? 1000UL :
        (cloudDisplayApiReady ? CLOUD_DISPLAY_PULL_INTERVAL_MS : CLOUD_BOOTSTRAP_RETRY_MS));
  }
  else
  {
    // A pending local update used to retry every second even after an HTTP error. When
    // migration 033 was missing, that produced a continuous HTTP 400/TLS loop which
    // starved snapshot uploads until the 512-row history queue filled. Back off failures
    // independently of pending state; the fast 1s follow-up is only for successful writes
    // where a newer local touch arrived while the request was in flight.
    if (cloudDisplayConsecutiveFailures < 0xFFFF)
    {
      cloudDisplayConsecutiveFailures++;
    }
    uint32_t backoffMs = CLOUD_DISPLAY_PULL_INTERVAL_MS;
    for (uint16_t i = 1;
         i < cloudDisplayConsecutiveFailures && backoffMs < CLOUD_DISPLAY_FAILURE_BACKOFF_MAX_MS;
         i++)
    {
      backoffMs *= 2;
    }
    if (backoffMs > CLOUD_DISPLAY_FAILURE_BACKOFF_MAX_MS)
    {
      backoffMs = CLOUD_DISPLAY_FAILURE_BACKOFF_MAX_MS;
    }
    nextCloudDisplaySyncMs = millis() + backoffMs;
  }
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
    // The scheduler already supplied a paced network slot. Do the boot-time restore now
    // instead of consuming that slot merely to arm another ten-second timer.
    nextCloudAutoCalPullMs = now;
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

  uint32_t freeHeapBeforeAutoCalPull = ESP.getFreeHeap();
  if (freeHeapBeforeAutoCalPull < minFreeHeapEverBytes) minFreeHeapEverBytes = freeHeapBeforeAutoCalPull;
  if (freeHeapBeforeAutoCalPull < CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES)
  {
    nextCloudAutoCalPullMs = now + CLOUD_LOW_HEAP_RETRY_MS;
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
  // region=1: the cloud's water-now region in place of the range learned from where the last
  // waterings happened (migration 041, region.ts). It answers in the same field names.
  autoCalUrl += "autocal=1&region=1&receiverId=";
  autoCalUrl += receiverId;

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(TREEBEARD_TRUSTED_ROOT_CA_PEM);
    began = http.begin(secureClient, autoCalUrl);
  }
  else
  {
    began = http.begin(plainClient, autoCalUrl);
  }

  if (!began)
  {
    stopCloudClients(secureClient, plainClient);
    nextCloudAutoCalPullMs = now + CLOUD_AUTOCAL_RETRY_MS;
    return;
  }

  http.setTimeout(8000);
  String auth = "Bearer ";
  auth += cloudApiToken;
  http.addHeader("Authorization", auth);

  bool pullSucceeded = false;
  int status = http.GET();
  if (status >= 200 && status < 300)
  {
    String response = http.getString();
    DynamicJsonDocument responseDoc(4096);
    DeserializationError error = deserializeJson(responseDoc, response);
    if (!error && (responseDoc["ok"] | false))
    {
      // Publish one complete response. Starting from defaults prevents a sensor removed from
      // the cloud response from retaining a stale learned range forever.
      AutoCalState refreshed[MAX_SENSOR_ID + 1];
      JsonArray sensorsArray = responseDoc["sensors"].as<JsonArray>();
      for (JsonObject sensorRow : sensorsArray)
      {
        uint16_t sensorId = sensorRow["sensorId"] | 0;
        if (sensorId == 0 || sensorId > MAX_SENSOR_ID)
        {
          continue;
        }
        AutoCalState learned;
        learned.ready = sensorRow["ready"] | false;
        learned.dryBound = sensorRow["dryBound"] | 0;
        learned.wetBound = sensorRow["wetBound"] | 0;
        learned.daysSinceLastWatering = sensorRow["daysSinceLastWatering"] | -1.0f;
        learned.lastWateredAtMs = sensorRow["lastWateredAtMs"] | -1.0;
        learned.dryBoundTrendingLower = sensorRow["dryBoundTrendingLower"] | false;
        learned.region = sensorRow["region"] | false;
        if (learned.ready &&
            (learned.dryBound < 0 || learned.wetBound <= learned.dryBound || learned.wetBound > 65535))
        {
          learned.ready = false;
        }
        refreshed[sensorId] = learned;
      }
      portENTER_CRITICAL(&autoCalMux);
      for (uint8_t sensorId = 1; sensorId <= MAX_SENSOR_ID; sensorId++)
      {
        autoCal[sensorId] = refreshed[sensorId];
      }
      portEXIT_CRITICAL(&autoCalMux);
      saveAutoCalCache();
      // Ensure cached PlantArtSlot bounds/text switch back to percent without waiting for
      // the next probe packet to happen to dirty the display.
      sensorDataDirty = true;
      pullSucceeded = true;
      Serial.println("[Treebeard cloud] auto-calibration updated");
    }
  }

  http.end();
  stopCloudClients(secureClient, plainClient);
  nextCloudAutoCalPullMs = millis() +
      (pullSucceeded ? CLOUD_AUTOCAL_PULL_INTERVAL_MS : CLOUD_AUTOCAL_RETRY_MS);
}

// Splits "https://host[:port]/path" into host and port for the pre-connect in
// runCloudSnapshotUploader() below. Deliberately minimal: the cloud upload URL is a plain
// Supabase hostname, so userinfo ("user@host") and IPv6 literals are rejected rather than
// parsed wrongly - a false return simply skips the pre-connect and lets HTTPClient::POST()
// do its own connect exactly as before.
bool parseCloudUrlHostPort(const char* url, char* hostOut, size_t hostCap, uint16_t& portOut)
{
  if (url == nullptr || hostOut == nullptr || hostCap == 0)
  {
    return false;
  }
  const char* schemeEnd = strstr(url, "://");
  if (schemeEnd == nullptr)
  {
    return false;
  }
  const bool isHttpsUrl = strncmp(url, "https://", 8) == 0;
  const char* hostStart = schemeEnd + 3;
  const char* pathStart = strchr(hostStart, '/');
  const char* hostEnd = (pathStart != nullptr) ? pathStart : (hostStart + strlen(hostStart));
  if (memchr(hostStart, '@', (size_t)(hostEnd - hostStart)) != nullptr || *hostStart == '[')
  {
    return false;
  }
  const char* colon = (const char*)memchr(hostStart, ':', (size_t)(hostEnd - hostStart));
  const char* nameEnd = (colon != nullptr) ? colon : hostEnd;
  const size_t nameLen = (size_t)(nameEnd - hostStart);
  if (nameLen == 0 || nameLen >= hostCap)
  {
    return false;
  }
  memcpy(hostOut, hostStart, nameLen);
  hostOut[nameLen] = '\0';
  uint16_t port = isHttpsUrl ? 443 : 80;
  if (colon != nullptr)
  {
    const long parsed = strtol(colon + 1, nullptr, 10);
    if (parsed <= 0 || parsed > 65535)
    {
      return false;
    }
    port = (uint16_t)parsed;
  }
  portOut = port;
  return true;
}

// Exponential backoff on consecutive failures (2026-08-23): the fixed 17s retry alone let a bad
// patch retry fast enough to compound the same heap pressure that caused it in the first place
// (observed 7+ failures in a row, 17s apart, before this was added). Doubles per additional
// failure, capped at CLOUD_UPLOAD_BACKOFF_MAX_MS, resets to the base interval the moment an
// upload succeeds again. Shared by the TLS pre-connect failure path and the post-POST failure
// path so both escalate identically.
uint32_t noteCloudUploadFailureAndGetBackoffMs()
{
  if (cloudConsecutiveUploadFailures < 0xFFFF)
  {
    cloudConsecutiveUploadFailures++;
  }
  uint32_t backoffMs = max(HISTORY_UPLOAD_RETRY_MS, CLOUD_MIN_UPLOAD_SPACING_MS);
  for (uint16_t i = 1; i < cloudConsecutiveUploadFailures && backoffMs < CLOUD_UPLOAD_BACKOFF_MAX_MS; i++)
  {
    backoffMs *= 2;
  }
  if (backoffMs > CLOUD_UPLOAD_BACKOFF_MAX_MS)
  {
    backoffMs = CLOUD_UPLOAD_BACKOFF_MAX_MS;
  }
  return backoffMs;
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

  // Backlog-triggered nudge: if the history queue just transitioned from empty to
  // non-empty while the uploader was idle waiting out the full syncIntervalSeconds
  // interval, pull the next attempt forward so a freshly-queued reading does not sit
  // for up to five minutes. Never nudge sooner than CLOUD_MIN_UPLOAD_SPACING_MS after
  // the previous attempt started, so this cannot recreate the removed packet-triggered
  // fast path.
  static bool historyQueueWasEmptyLastCheck = true;
  size_t pendingHistoryNow = queuedHistoryCount();
  if (pendingHistoryNow > 0 && historyQueueWasEmptyLastCheck)
  {
    uint32_t earliestAllowedMs = (cloudLastAttemptMs == 0)
                                      ? now
                                      : cloudLastAttemptMs + CLOUD_MIN_UPLOAD_SPACING_MS;
    uint32_t nudgedTargetMs = ((int32_t)(now - earliestAllowedMs) > 0) ? now : earliestAllowedMs;
    if ((int32_t)(nextCloudSyncMs - nudgedTargetMs) > 0)
    {
      nextCloudSyncMs = nudgedTargetMs;
    }
  }
  historyQueueWasEmptyLastCheck = (pendingHistoryNow == 0);

  if ((int32_t)(now - nextCloudSyncMs) < 0)
  {
    return;
  }

  // This is the authoritative upload-attempt floor. Retry branches may choose an earlier
  // diagnostic re-check, but none may build another payload or touch the TLS client until
  // a full minute has elapsed since the previous attempt began.
  if (cloudLastAttemptMs != 0 &&
      (uint32_t)(now - cloudLastAttemptMs) < CLOUD_MIN_UPLOAD_SPACING_MS)
  {
    nextCloudSyncMs = cloudLastAttemptMs + CLOUD_MIN_UPLOAD_SPACING_MS;
    return;
  }

  wifiStationConnected = hasUsableStationConnection();
  if (!wifiStationConnected)
  {
    setCloudLastError("waiting for station WiFi");
    nextCloudSyncMs = now + 10000UL;
    return;
  }

  uint32_t freeHeapBeforeAttempt = ESP.getFreeHeap();
  if (freeHeapBeforeAttempt < minFreeHeapEverBytes) minFreeHeapEverBytes = freeHeapBeforeAttempt;
  if (freeHeapBeforeAttempt < CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES)
  {
    setCloudLastError("skipped: low heap");
    nextCloudSyncMs = now + CLOUD_LOW_HEAP_RETRY_MS;
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

  Serial.print("[Treebeard cloud] uploading snapshot to ");
  Serial.println(cloudUploadUrl);

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  bool began = false;
  if (isHttps)
  {
    secureClient.setCACert(TREEBEARD_TRUSTED_ROOT_CA_PEM);
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
    // Defensive cleanup even on this early-exit path: a TLS connect attempt that fails
    // before http.begin() itself returns true has still been observed to leave real heap
    // unrecovered (see CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES above) - explicit stop() rather
    // than relying solely on these stack objects' destructors, which is the standard
    // mitigation for this class of ESP32 WiFiClientSecure behavior.
    stopCloudClients(secureClient, plainClient);
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

  // Heap telemetry (2026-08-23, diagnostic only): this is the single largest, most
  // frequent allocation burst on the whole receiver - a fresh TLS session plus the
  // snapshot/history JSON payload, every cloudSyncIntervalSeconds. Bracketing it lets
  // /device show whether free heap is actually trending down around cloud activity,
  // instead of guessing from the reboot symptom alone.
  uint32_t heapBeforeUpload = ESP.getFreeHeap();
  if (heapBeforeUpload < minFreeHeapEverBytes) minFreeHeapEverBytes = heapBeforeUpload;
  setCloudCheckpoint("snapshot upload: building payload");

  PendingHistoryReading historyBatch[HISTORY_UPLOAD_BATCH_SIZE];
  if (nextCloudFullSnapshotMs == 0)
  {
    nextCloudFullSnapshotMs = now + CLOUD_FULL_SNAPSHOT_INTERVAL_MS;
  }
  const bool periodicFullSnapshotDue = (int32_t)(now - nextCloudFullSnapshotMs) >= 0;
  size_t historyCount = periodicFullSnapshotDue
      ? 0
      : copyHistoryUploadBatch(historyBatch, HISTORY_UPLOAD_BATCH_SIZE);
  const bool sendingFullSnapshot = periodicFullSnapshotDue || historyCount == 0;
  if (periodicFullSnapshotDue)
  {
    // A full-state refresh is useful but replaceable. Whether this attempt succeeds or not,
    // do not let its larger JSON document preempt compact history recovery again next minute.
    nextCloudFullSnapshotMs = now + CLOUD_FULL_SNAPSHOT_INTERVAL_MS;
  }

  // 2026-08-30: re-check the contiguous-block guard here, not just at the top of this
  // function. copyHistoryUploadBatch() above only reads the queue (see its own definition) -
  // it does not dequeue anything, so returning here leaves the queued history rows completely
  // untouched for the next attempt.
  if (!cloudHeapHasContiguousRoomForTls())
  {
    http.end();
    stopCloudClients(secureClient, plainClient);
    setCloudLastError("skipped: low heap before TLS connect");
    nextCloudSyncMs = now + CLOUD_LOW_HEAP_RETRY_MS;
    return;
  }

  // 2026-08-31 ROOT-CAUSE FIX. The intermittent "connection refused" failures were never
  // network failures: they were mbedtls_ssl_setup() returning MBEDTLS_ERR_SSL_ALLOC_FAILED
  // (-0x7F00 = -32512), i.e. it could not obtain its large *contiguous* SSL record buffers.
  // The old order built the ~10KB history-batch String FIRST and connected inside POST()
  // afterwards, so the payload carved up the largest free block (measured ceiling on this
  // board: ~32.7KB contiguous internal, with PSRAM disabled) right before mbedTLS needed it.
  // Connecting first lets TLS take its buffers from the least-fragmented heap, and the
  // payload is then serialized into whatever remains. HTTPClient::connect() returns early
  // when the client is already connected, so POST() below reuses this exact connection.
  // Full writeup: DisplayCenter/docs/RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md
  setCloudCheckpoint("snapshot upload: connecting");
  beginCloudRadioSlot();
  if (isHttps)
  {
    char cloudHost[128];
    uint16_t cloudPort = 443;
    const bool parsedCloudEndpoint =
        parseCloudUrlHostPort(cloudUploadUrl, cloudHost, sizeof(cloudHost), cloudPort);
    if (!parsedCloudEndpoint || secureClient.connect(cloudHost, cloudPort, 8000) != 1)
    {
      // Report the real reason rather than POST()'s generic "connection refused". Rows stay
      // queued (nothing was dequeued above), so this is a clean retry.
      endCloudRadioSlot();
      http.end();
      stopCloudClients(secureClient, plainClient);
      setCloudLastError("TLS connect failed (pre-connect)");
      Serial.println("[Treebeard cloud] upload failed: TLS pre-connect");
      nextCloudSyncMs = millis() + noteCloudUploadFailureAndGetBackoffMs();
      return;
    }
  }

  setCloudCheckpoint("snapshot upload: posting");
  size_t payloadBytes = 0;
  int status = 0;
  int postErrno = 0;
  // 2026-08-31 diagnostic: NetworkClientSecure collapses socket()/connect()/select()/
  // getsockopt() failures to a bare -1 before mbedTLS runs, which is why lastError() was
  // already ruled useless here - but lwIP still sets the thread's errno on the call that
  // actually failed. Cleared first so a stale value can't be misread, and captured before
  // endCloudRadioSlot() or any other call can clobber it. This is the only thing that can
  // separate a real ECONNREFUSED from resource exhaustion (ENOMEM/ENOBUFS/EADDRNOTAVAIL).
  if (sendingFullSnapshot)
  {
    String payload = snapshotJsonWithHistory(nullptr, 0);
    if (payload.length() == 0)
    {
      endCloudRadioSlot();
      http.end();
      stopCloudClients(secureClient, plainClient);
      setCloudLastError("could not serialize full snapshot");
      nextCloudSyncMs = millis() + HISTORY_UPLOAD_RETRY_MS;
      return;
    }
    payloadBytes = payload.length();
    errno = 0;
    status = http.POST(payload);
    postErrno = errno;
  }
  else
  {
    // TLS is already holding its contiguous record buffers. Build only the small latest-value
    // prefix, then let HTTPClient pull one reusable history-row chunk at a time. Batch capacity
    // can therefore exceed the arrival rate without recreating the pre-TLS allocation failure.
    const uint32_t serializedAtMs = millis();
    String prefix = historyOutboxPrefixJson(serializedAtMs);
    HistoryOutboxStream payloadStream(prefix, historyBatch, historyCount, serializedAtMs);
    if (!payloadStream.valid())
    {
      endCloudRadioSlot();
      http.end();
      stopCloudClients(secureClient, plainClient);
      setCloudLastError("could not initialize streaming history payload");
      nextCloudSyncMs = millis() + HISTORY_UPLOAD_RETRY_MS;
      return;
    }
    payloadBytes = payloadStream.contentLength();
    errno = 0;
    status = http.sendRequest("POST", &payloadStream, payloadBytes);
    postErrno = errno;
  }
  endCloudRadioSlot();
  uint32_t heapAfterUpload = ESP.getFreeHeap();
  if (heapAfterUpload < minFreeHeapEverBytes) minFreeHeapEverBytes = heapAfterUpload;
  lastCloudUploadFreeHeapBeforeBytes = heapBeforeUpload;
  lastCloudUploadFreeHeapAfterBytes = heapAfterUpload;
  setCloudCheckpoint("snapshot upload: posted");
  cloudLastHttpStatus = status;
  bool uploadAccepted = status >= 200 && status < 300;
  size_t historyAccepted = historyCount == 0 ? 0 : SIZE_MAX;
  if (uploadAccepted)
  {
    String responseBody = http.getString();
    StaticJsonDocument<384> responseDoc;
    DeserializationError responseError = deserializeJson(responseDoc, responseBody);
    if (!responseError)
    {
      uint64_t serverTimeMs = responseDoc["serverTimeMs"] | 0ULL;
      if (serverTimeMs != 0)
      {
        portENTER_CRITICAL(&historyQueueMux);
        cloudTimeAnchorEpochMs = serverTimeMs;
        cloudTimeAnchorMillis = millis();
        portEXIT_CRITICAL(&historyQueueMux);
      }
    }

    if (historyCount > 0)
    {
      int accepted = responseError ? -1 : (responseDoc["historyAccepted"] | -1);
      if (accepted < 0 || (size_t)accepted != historyCount)
      {
        uploadAccepted = false;
        setCloudLastError("history batch was not fully acknowledged");
        Serial.print("[Treebeard cloud] history acknowledgement mismatch: sent=");
        Serial.print(historyCount);
        Serial.print(" accepted=");
        Serial.println(accepted);
      }
      else
      {
        historyAccepted = (size_t)accepted;
      }
    }
  }

  if (uploadAccepted)
  {
    size_t removed = acknowledgeHistoryUploadBatch(historyBatch, historyAccepted);
    if (removed != historyAccepted)
    {
      uploadAccepted = false;
      setCloudLastError("history queue acknowledgement race");
      Serial.print("[Treebeard cloud] queue acknowledgement mismatch: accepted=");
      Serial.print(historyAccepted);
      Serial.print(" removed=");
      Serial.println(removed);
    }
  }

  if (uploadAccepted)
  {
    cloudConsecutiveUploadFailures = 0;
    cloudLastSuccessMs = millis();
    setCloudLastError("");
    Serial.print("[Treebeard cloud] upload ok: HTTP ");
    Serial.print(status);
    Serial.print(sendingFullSnapshot ? " full" : " compact");
    Serial.print(" history=");
    Serial.print(historyAccepted);
    Serial.print(" queued=");
    Serial.println(queuedHistoryCount());
  }
  else if (status > 0)
  {
    // Preserve the more specific acknowledgement/queue error set above for a 2xx
    // response. Non-2xx responses still report their HTTP status as before.
    if (!(status >= 200 && status < 300))
    {
      String error = "HTTP ";
      error += status;
      setCloudLastError(error.c_str());
      Serial.print("[Treebeard cloud] upload failed: HTTP ");
      Serial.println(status);
    }
  }
  else
  {
    String error = http.errorToString(status);
    setCloudLastError(error.c_str());
    Serial.print("[Treebeard cloud] upload failed: ");
    Serial.println(error);
  }

#if TREEBEARD_ENABLE_BLE
  // 2026-08-31 diagnostic - correlate this attempt's outcome (above) against whether BLE was
  // actually mid-scan when beginCloudRadioSlot() ran for it - see lastRadioSlotWasScanning's
  // own comment (declared alongside cloudRadioExclusive).
  Serial.print("[Treebeard cloud] radio slot: wasScanning=");
  Serial.print(lastRadioSlotWasScanning);
  Serial.print(" stopCallMs=");
  Serial.print(lastRadioSlotStopCallMs);
  Serial.print(" stillScanningAfterSettle=");
  Serial.println(lastRadioSlotStillScanningAfterSettle);
#endif

  // Latest outage-spill journal write. Healthy uploads acknowledge RTC staging directly, so
  // this timestamp should not advance periodically while WiFi and the backend are available.
  Serial.print("[Treebeard cloud] journal write: durationMs=");
  Serial.print(lastJournalWriteDurationMs);
  Serial.print(" records=");
  Serial.print(lastJournalWriteRecords);
  Serial.print(" compacted=");
  Serial.print(lastJournalCompactedThisWrite);
  Serial.print(" compactDurationMs=");
  Serial.print(lastJournalCompactedThisWrite ? lastJournalCompactDurationMs : 0);
  // RSSI is a read-only register read, not a network op - safe to log per-attempt without
  // adding a competing DNS/TCP operation. Tests the separate, also-untested "RF margin"
  // possibility from this doc's "genuinely left untested" list at the same time.
  Serial.print(" staRssi=");
  Serial.println(wifiStationConnected ? WiFi.RSSI() : 0);

  // 2026-08-31 diagnostic - see postErrno's capture site above. payloadBytes is logged
  // alongside it because the first capture hinted (2/2, small n) that the larger 32-row
  // compact batches fail while smaller full snapshots do not.
  Serial.print("[Treebeard cloud] transport: postErrno=");
  Serial.print(postErrno);
  Serial.print(" payloadBytes=");
  Serial.print(payloadBytes);
  Serial.print(" fullSnapshot=");
  Serial.println(sendingFullSnapshot);

  http.end();
  stopCloudClients(secureClient, plainClient);
  size_t remainingHistory = queuedHistoryCount();
  if (!uploadAccepted)
  {
    nextCloudSyncMs = millis() + noteCloudUploadFailureAndGetBackoffMs();
  }
  else if (remainingHistory > 0)
  {
    // Never turn a backlog into a TLS burst. A streamed 96-row batch at the minimum
    // 60-second cadence is four times the measured multi-sensor arrival rate while leaving
    // BLE the radio and heap headroom it needs between uploads.
    nextCloudSyncMs = millis() + max(cloudSyncIntervalSeconds * 1000UL,
                                     CLOUD_MIN_UPLOAD_SPACING_MS);
  }
  else
  {
    nextCloudSyncMs = millis() + cloudSyncIntervalSeconds * 1000UL;
  }
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
  StaticJsonDocument<2304> doc;
  doc["name"] = "Project Treebeard Display";
  doc["build"] = TREEBEARD_BUILD_TAG;
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
  cloud["queuedHistory"] = queuedHistoryCount();
  cloud["historyQueueCapacity"] = HISTORY_QUEUE_CAPACITY;
  cloud["historyQueueDropped"] = droppedHistoryCount();
  cloud["historyQueueHighWaterMark"] = historyQueueHighWaterMark();
  portENTER_CRITICAL(&historyQueueMux);
  cloud["historyStagingCount"] = pendingHistoryCount;
  cloud["historyDurableCount"] = durableHistoryCount;
  cloud["historyJournalHeadBytes"] = historyJournalHeadOffset;
  cloud["historyJournalBytes"] = historyJournalValidBytes;
  portEXIT_CRITICAL(&historyQueueMux);
  cloud["historyJournalReady"] = historyJournalReady;
  cloud["historyJournalStorageErrors"] = historyJournalStorageErrors;
  cloud["networkMinGapMs"] = CLOUD_NETWORK_MIN_GAP_MS;

  // 2026-08-23 telemetry (diagnostic only): reset-reason/checkpoint/heap/stack visibility
  // that previously existed only as a Serial print at boot, per the reboot investigation.
  JsonObject health = doc.createNestedObject("health");
  health["resetReason"] = resetReasonName(esp_reset_reason());
  health["lastCheckpointMain"] = lastCheckpoint;
  health["lastCheckpointBle"] = lastCheckpointBle;
  health["lastCheckpointCloud"] = lastCheckpointCloud;
  health["freeHeapNowBytes"] = ESP.getFreeHeap();
  health["minFreeHeapEverBytes"] = minFreeHeapEverBytes;
  // loopTask (WebServer + display render + touch + WiFi maintenance) was the ONLY task with
  // no stack telemetry, and it is the one running sensorDataJson()'s StaticJsonDocument<8192>
  // - 4KB of stack inside a web handler. Sampled from loop() itself and from the /data
  // handler after that document exists; see minLoopTaskStackFreeEverBytes.
  health["loopTaskStackHighWaterBytes"] = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  health["loopTaskStackFreeMinEverBytes"] =
      minLoopTaskStackFreeEverBytes == UINT32_MAX ? 0 : minLoopTaskStackFreeEverBytes;
  health["loopTaskStackSizeBytes"] = TREEBEARD_LOOP_TASK_STACK_BYTES;
  health["largestFreeBlockBytes"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  // 2026-08-30: added while calibrating the snapshot uploader's contiguous-heap guard above -
  // largestFreeBlockBytes alone (MALLOC_CAP_8BIT, unrestricted) can't tell whether this board
  // has PSRAM inflating that number, or whether internal-only room is the real ceiling. Cheap,
  // diagnostic-only reads; not used in any guard/branch themselves.
  health["largestFreeBlockInternalBytes"] =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  health["psramSizeBytes"] = ESP.getPsramSize();
  health["psramFreeBytes"] = ESP.getFreePsram();
  // 2026-08-30 isolation test - see TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL's comment. Must read 0 for
  // the whole test run; a nonzero value here means the isolation isn't clean.
  health["touchSpiDiagnosticDisableAll"] = TOUCH_SPI_DIAGNOSTIC_DISABLE_ALL;
  health["touchSpiTransactionCount"] = touchSpiTransactionCount;
  health["cloudUploadFreeHeapBeforeBytes"] = lastCloudUploadFreeHeapBeforeBytes;
  health["cloudUploadFreeHeapAfterBytes"] = lastCloudUploadFreeHeapAfterBytes;
  health["cloudConsecutiveUploadFailures"] = cloudConsecutiveUploadFailures;
#if TREEBEARD_ENABLE_BLE
  // 2026-08-31 diagnostic - see lastRadioSlotWasScanning's own comment (declared alongside
  // cloudRadioExclusive above).
  JsonObject radioSlot = health.createNestedObject("cloudRadioSlotDiag");
  radioSlot["atMs"] = lastRadioSlotAtMs;
  radioSlot["wasScanning"] = lastRadioSlotWasScanning;
  radioSlot["stillScanningAfterSettle"] = lastRadioSlotStillScanningAfterSettle;
  radioSlot["stopCallMs"] = lastRadioSlotStopCallMs;
#endif
  // 2026-08-31 diagnostic - see lastJournalWriteAtMs's own comment (declared alongside
  // historyJournalStorageErrors above). Times the LittleFS append that runs immediately
  // before each upload attempt's own connect, to test the flash-contention hypothesis in
  // DisplayCenter/docs/RECEIVER_CLOUD_UPLOAD_CONNECTION_REFUSED_2026-08-31.md.
  JsonObject journalWriteDiag = health.createNestedObject("cloudJournalWriteDiag");
  journalWriteDiag["atMs"] = lastJournalWriteAtMs;
  journalWriteDiag["durationMs"] = lastJournalWriteDurationMs;
  journalWriteDiag["records"] = lastJournalWriteRecords;
  journalWriteDiag["compacted"] = lastJournalCompactedThisWrite;
  journalWriteDiag["compactDurationMs"] = lastJournalCompactedThisWrite ? lastJournalCompactDurationMs : 0;
#if TREEBEARD_ENABLE_WIFI
  // 2026-09-02: dropped a `* sizeof(StackType_t)` that used to be here. It was a no-op, NOT
  // a bug: ESP-IDF's uxTaskGetStackHighWaterMark() already returns BYTES (its task.h says
  // "in bytes (as opposed to words in the standard FreeRTOS documentation)"), and this port
  // defines portSTACK_TYPE as uint8_t (xtensa/portmacro.h), so sizeof(StackType_t) == 1.
  // Removed only because it reads like a words->bytes conversion and invites exactly the
  // misdiagnosis it caused on 2026-09-02, when it was briefly believed to be inflating every
  // figure 4x. Reported values are unchanged. Measured margins from that session, and why
  // stack exhaustion was ruled out as the panic cause, are in
  // docs/RECEIVER_PANIC_STACK_RULED_OUT_2026-09-02.md - kept there rather than inlined here
  // because those numbers are specific to one build and workload and will drift.
  health["cloudTaskStackHighWaterBytes"] = cloudSyncTaskHandle != nullptr
      ? (uint32_t)uxTaskGetStackHighWaterMark(cloudSyncTaskHandle)
      : 0;
  health["cloudTaskStackSizeBytes"] = CLOUD_SYNC_TASK_STACK_BYTES;
#endif
#if TREEBEARD_ENABLE_BLE
  health["bleTaskStackHighWaterBytes"] = bleScanTaskHandle != nullptr
      ? (uint32_t)uxTaskGetStackHighWaterMark(bleScanTaskHandle)
      : 0;
  health["bleTaskStackSizeBytes"] = BLE_SCAN_TASK_STACK_BYTES;
#endif
  // Previous boot's RTC state, persisted once at the top of setup() before this boot
  // overwrote it - survives a full power loss, unlike the live lastCheckpoint* fields above.
  if (LittleFS.exists("/boot-record.json"))
  {
    File bootRecordFile = LittleFS.open("/boot-record.json", "r");
    if (bootRecordFile)
    {
      StaticJsonDocument<384> prevBoot;
      if (!deserializeJson(prevBoot, bootRecordFile))
      {
        JsonObject previousBoot = health.createNestedObject("previousBoot");
        previousBoot["resetReason"] = prevBoot["resetReason"].as<const char*>();
        previousBoot["wasIntentionalRestart"] = prevBoot["wasIntentionalRestart"].as<bool>();
        previousBoot["checkpointMain"] = prevBoot["prevCheckpointMain"].as<const char*>();
        previousBoot["checkpointBle"] = prevBoot["prevCheckpointBle"].as<const char*>();
        previousBoot["checkpointCloud"] = prevBoot["prevCheckpointCloud"].as<const char*>();
      }
      bootRecordFile.close();
    }
  }

#if TREEBEARD_ENABLE_WIFI
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
    0,    // sensor 4: MSV3 probe - FDC counts, calibrated by calibrateMsv3Moisture, not this path
    0,    // sensor 5: MSV3 probe - as sensor 4
    0,    // sensor 6: MSV3 probe - as sensor 4
    0,    // sensor 7: MSV3 probe - as sensor 4
    0,    // sensor 8: MSV3 probe - as sensor 4
    3300, // sensor 9: hybrid AFE rig - the same v1.2 front end as sensor 3 but with an MSV3 probe
          // end in place of the module's own pad (2026-09-14). Raw is millivolts on the identical
          // ESP32 ADC, so it takes sensor 3's ceiling: the two rigs are being compared directly,
          // and giving them different ceilings would bake a presentation difference into an A/B
          // whose entire point is that only the probe end differs. Without an entry here it would
          // fall through to DEFAULT_MOISTURE_INVERT_CEILING (200) and every reading would clamp.
};

// Map the useful MSV3 FDC window into uint16 without first compressing the absolute
// ~20-million-count reading. Bench/nail-polish tests show roughly 20M dry -> 19M wet:
// this mapping produces 40,000 -> 50,000, retaining 10,000 steps across that real 1M
// signal span. The observed ~3,000-count noise becomes ~30 derived steps and is handled
// by the receiver presentation filter, while rawFdcCounts itself is preserved unchanged
// in /data, cloud snapshots, and Supabase history.
//
// The 24M reference and 100-count divisor cover raw inputs from 24M (derived 0) down to
// 17,446,500 (derived 65,535), comfortably enclosing the observed ~18-22M operating
// window. Out-of-window readings clamp instead of wrapping.
const uint32_t MSV3_FDC_MOISTURE_REFERENCE_COUNTS = 24000000UL;
const uint32_t MSV3_FDC_COUNTS_PER_MOISTURE_UNIT = 100UL;

// C17-free tanks run at much higher counts: Probe 8 measured ~39.4M dry and ~27M under a
// repeatable two-hand load. Map the FDC's useful connected-tank range into approximately the same
// uint16 presentation neighborhood as scale 2 while preserving a ~12,000-unit span for that test.
// Raw counts remain untouched in local/cloud history; this is only the receiver presentation
// transform. 80M also stays above the largest valid count expected from the configured 8.75MHz
// input band across the internal-reference tolerance, avoiding a dry-end clamp.
const uint32_t MSV3_CAPLESS_FDC_MOISTURE_REFERENCE_COUNTS = 80000000UL;
const uint32_t MSV3_CAPLESS_FDC_COUNTS_PER_MOISTURE_UNIT = 1000UL;

uint16_t calibrateMsv3Moisture(uint32_t rawFdcCounts)
{
  if (rawFdcCounts >= MSV3_FDC_MOISTURE_REFERENCE_COUNTS)
  {
    return 0;
  }
  uint32_t moisture = (MSV3_FDC_MOISTURE_REFERENCE_COUNTS - rawFdcCounts) /
      MSV3_FDC_COUNTS_PER_MOISTURE_UNIT;
  return moisture > 0xFFFF ? 0xFFFF : (uint16_t)moisture;
}

uint16_t calibrateMsv3CaplessMoisture(uint32_t rawFdcCounts)
{
  if (rawFdcCounts >= MSV3_CAPLESS_FDC_MOISTURE_REFERENCE_COUNTS)
  {
    return 0;
  }
  uint32_t moisture = (MSV3_CAPLESS_FDC_MOISTURE_REFERENCE_COUNTS - rawFdcCounts) /
      MSV3_CAPLESS_FDC_COUNTS_PER_MOISTURE_UNIT;
  return moisture > 0xFFFF ? 0xFFFF : (uint16_t)moisture;
}

// MSV4 measures soil RESISTANCE, which falls as the soil gets wetter and spans decades, so the
// presentation number is the log of how far below "open" it is:
//     moisture = 10000 * log10(400 kohm / Rsoil@25C)
// 0 with nothing conducting, about 30500 at 355 ohm, capped at 65535. Equal ratios of resistance
// are equal steps, and like every other scale this is only an ordering: the per-plant dry/wet
// bounds turn it into a percentage. The 25 C correction uses the blade-tip NTC when the probe
// sent a valid one and is skipped otherwise.
uint16_t calibrateMsv4Moisture(uint32_t rsoilDeciOhm, bool tipTempValid, int16_t tipTempQuarterC)
{
  if (rsoilDeciOhm < MSV4_RSOIL_DECIOHM_MIN || rsoilDeciOhm >= MSV4_RSOIL_DECIOHM_MAX)
  {
    return 0;
  }
  float rsoilDeci = (float)rsoilDeciOhm;
  if (tipTempValid)
  {
    float factor = 1.0f + MSV4_EC_TEMPCO_PER_C * ((float)tipTempQuarterC * 0.25f - MSV4_EC_REFERENCE_C);
    // -40 C would otherwise drive the linear correction through zero.
    if (factor < 0.25f) factor = 0.25f;
    rsoilDeci *= factor;
  }
  if (rsoilDeci >= (float)MSV4_RSOIL_DECIOHM_MAX)
  {
    return 0;
  }
  float moisture = MSV4_MOISTURE_UNITS_PER_DECADE * log10f((float)MSV4_RSOIL_DECIOHM_MAX / rsoilDeci);
  if (moisture <= 0.0f) return 0;
  return moisture >= 65535.0f ? 0xFFFF : (uint16_t)lroundf(moisture);
}

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

// Estimate the useful signal span without assuming one probe generation's numeric scale.
// Once auto-calibration is ready its learned range is the best available scale. Before
// that, use 20% of the current signal with a small absolute floor so legacy low-valued
// sensors and MSV3's much larger FDC-derived value both receive proportionate thresholds.
uint16_t moisturePresentationSpan(uint8_t sensorId, uint16_t measuredMoisture)
{
  AutoCalState learned;
  if (copyAutoCalState(sensorId, learned) && learned.ready &&
      learned.wetBound > learned.dryBound)
  {
    uint32_t learnedSpan = (uint32_t)(learned.wetBound - learned.dryBound);
    return learnedSpan > 0xFFFF ? 0xFFFF : (uint16_t)learnedSpan;
  }

  uint16_t estimatedSpan = measuredMoisture / 5U;
  return estimatedSpan < 20U ? 20U : estimatedSpan;
}

uint16_t maxMoistureThreshold(uint16_t left, uint16_t right)
{
  return left > right ? left : right;
}

const uint8_t MOISTURE_WATERING_CONFIRM_PACKETS = 2;
const uint8_t MOISTURE_TRACKING_DIVISOR = 8; // symmetric EMA alpha = 1/8

uint16_t medianMoistureValue(const uint16_t* values, uint8_t count)
{
  uint16_t sorted[MOISTURE_CENTER_WINDOW];
  for (uint8_t i = 0; i < count; ++i)
  {
    sorted[i] = values[i];
  }
  for (uint8_t i = 1; i < count; ++i)
  {
    uint16_t value = sorted[i];
    uint8_t j = i;
    while (j > 0 && sorted[j - 1] > value)
    {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = value;
  }
  return sorted[count / 2U];
}

// Display-only robust center tracker:
// - a median of the latest five unique packets rejects as many as two isolated spikes;
// - a symmetric 1/8 EMA follows that median in BOTH directions, so stationary noise cannot
//   ratchet the display toward its lower envelope;
// - Q8 fixed point keeps small upward and downward corrections equally representable;
// - normal tracking is capped to 5% of span per packet;
// - a watering-sized robust-center rise must persist twice, then snaps to the center.
//
// The actual derived reading is always retained in measuredMoisture and sent to the
// original Supabase `moisture` history column. This function only controls presentation.
uint16_t smoothMoistureForDisplay(uint8_t sensorId, uint16_t measuredMoisture, uint16_t span)
{
  SensorReading& reading = sensors[sensorId];

  reading.moistureCenterWindow[reading.moistureCenterNext] = measuredMoisture;
  reading.moistureCenterNext = (uint8_t)((reading.moistureCenterNext + 1U) % MOISTURE_CENTER_WINDOW);
  if (reading.moistureCenterCount < MOISTURE_CENTER_WINDOW)
  {
    reading.moistureCenterCount++;
  }

  if (reading.moistureCenterCount == 1U)
  {
    reading.moisture = measuredMoisture;
    reading.moistureFilteredQ8 = (uint32_t)measuredMoisture << 8;
    reading.moistureWateringStreak = 0;
    return reading.moisture;
  }

  // Hold the first real reading until the robust window is full. Letting a two-sample
  // "median" move at boot would merely reintroduce startup sensitivity to one packet.
  if (reading.moistureCenterCount < MOISTURE_CENTER_WINDOW)
  {
    return reading.moisture;
  }

  uint16_t center = medianMoistureValue(reading.moistureCenterWindow, MOISTURE_CENTER_WINDOW);
  uint16_t deviations[MOISTURE_CENTER_WINDOW];
  for (uint8_t i = 0; i < MOISTURE_CENTER_WINDOW; ++i)
  {
    uint16_t sample = reading.moistureCenterWindow[i];
    deviations[i] = sample >= center ? sample - center : center - sample;
  }
  uint16_t robustNoise = maxMoistureThreshold(
      1U, medianMoistureValue(deviations, MOISTURE_CENTER_WINDOW));

  uint16_t wateringThreshold = maxMoistureThreshold(
      4U, (uint16_t)(((uint32_t)span * 12U + 99U) / 100U));
  wateringThreshold = maxMoistureThreshold(wateringThreshold, reading.moisture / 50U);
  uint32_t noiseWateringThreshold = (uint32_t)robustNoise * 4U;
  if (noiseWateringThreshold > 0xFFFF)
  {
    noiseWateringThreshold = 0xFFFF;
  }
  wateringThreshold = maxMoistureThreshold(
      wateringThreshold, (uint16_t)noiseWateringThreshold);

  bool wateringRise = center > reading.moisture &&
      (uint32_t)(center - reading.moisture) >= wateringThreshold;
  if (wateringRise)
  {
    if (reading.moistureWateringStreak < 0xFF)
    {
      reading.moistureWateringStreak++;
    }
    if (reading.moistureWateringStreak >= MOISTURE_WATERING_CONFIRM_PACKETS)
    {
      // Snap to the robust center, not the newest raw packet. This preserves the useful
      // watering edge without overshooting to the insertion/pour transient's highest spike.
      reading.moisture = center;
      reading.moistureFilteredQ8 = (uint32_t)center << 8;
      reading.moistureWateringStreak = 0;
    }
    return reading.moisture;
  }

  reading.moistureWateringStreak = 0;
  int32_t targetQ8 = (int32_t)((uint32_t)center << 8);
  int32_t filteredQ8 = (int32_t)reading.moistureFilteredQ8;
  int32_t stepQ8 = (targetQ8 - filteredQ8) / MOISTURE_TRACKING_DIVISOR;
  int32_t maxStepQ8 = (int32_t)maxMoistureThreshold(1U, span / 20U) << 8;
  if (stepQ8 > maxStepQ8)
  {
    stepQ8 = maxStepQ8;
  }
  else if (stepQ8 < -maxStepQ8)
  {
    stepQ8 = -maxStepQ8;
  }

  reading.moistureFilteredQ8 = (uint32_t)(filteredQ8 + stepQ8);
  reading.moisture = (uint16_t)((reading.moistureFilteredQ8 + 128U) >> 8);
  return reading.moisture;
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

// Use the measured cell voltage directly. The former global 400mV charging offset was
// inferred from one older probe and made a measured 3.74V MSV3 cell display as 1-3% while
// USB was connected. Charge-path voltage effects are hardware/current dependent and cannot
// be corrected by one cross-probe constant; preserve rawCharging as a diagnostic instead.
uint8_t batteryMvToPercent(float mvolts)
{
  return (uint8_t)(interpolateBatteryPercent(mvolts) + 0.5f);
}

// Per-sensor EMA smoothing state - was a single global on the probe (one probe, one
// battery); now one slot per sensor here, since a single receiver interprets every probe's
// raw readings. Smooths out per-sample ADC noise on the History chart's charging curve
// (0.2 factor, same as the moisture oscillator's own outlier handling). File-scope arrays
// zero-initialize, so smoothedBatteryReady[] starts all-false as intended.
float smoothedBatteryPercent[MAX_SENSOR_ID + 1];
bool smoothedBatteryReady[MAX_SENSOR_ID + 1];

// MSV4 only: the share of the cell's USABLE charge, i.e. the standard curve rescaled between the
// harvester's load cut-off and its charge limit. On the plain curve a full MSV4 cell reads about
// 62 % and an about-to-shut-down one about 1 %.
uint8_t msv4BatteryMvToPercent(float mvolts)
{
  const float empty = interpolateBatteryPercent(MSV4_BATTERY_EMPTY_MV);
  const float full = interpolateBatteryPercent(MSV4_BATTERY_FULL_MV);
  float percent = (interpolateBatteryPercent(mvolts) - empty) * 100.0f / (full - empty);
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  return (uint8_t)(percent + 0.5f);
}

uint8_t calibrateRawBattery(uint8_t sensorId, float rawMv, bool msv4)
{
  if (sensorId > MAX_SENSOR_ID)
  {
    return msv4 ? msv4BatteryMvToPercent(rawMv) : batteryMvToPercent(rawMv);
  }

  float raw = msv4 ? msv4BatteryMvToPercent(rawMv) : batteryMvToPercent(rawMv);

  if (!smoothedBatteryReady[sensorId])
  {
    smoothedBatteryPercent[sensorId] = raw;
    smoothedBatteryReady[sensorId] = true;
  }
  else
  {
    smoothedBatteryPercent[sensorId] += 0.2f * (raw - smoothedBatteryPercent[sensorId]);
  }

  return (uint8_t)(smoothedBatteryPercent[sensorId] + 0.5f);
}

#if TREEBEARD_ENABLE_BLE
// Decodes a bring-up ("MSV3" magic) advertisement into benchProbes[] and NOTHING ELSE.
//
// Read MSV3_BENCH_MAGIC's comment before changing this. The one hard rule: this function must
// never write sensors[], enqueue history, touch the durable outbox, or mark sensorDataDirty. A
// bench probe is a board on a desk, frequently in air, often mid-experiment; its numbers are
// real measurements of nothing in particular and must never become a plant's record.
//
// Runs on the BLE scan task. `buffer`/`len` have already been checked for company ID and a
// length of at least 12 by the caller.
void parseBenchAdvertisement(const uint8_t* buffer, size_t len, int packetRssi)
{
  if (len < MSV3_BENCH_PACKET_MIN_LEN)
  {
    return;
  }

  const uint8_t sensorId = buffer[6];
  const uint32_t wake = ((uint32_t)buffer[7]) |
                        ((uint32_t)buffer[8] << 8) |
                        ((uint32_t)buffer[9] << 16);
  const uint32_t fdcCounts = ((uint32_t)buffer[10]) | ((uint32_t)buffer[11] << 8) |
                             ((uint32_t)buffer[12] << 16) | ((uint32_t)buffer[13] << 24);
  const uint16_t batteryMv = buffer[14] | (buffer[15] << 8);
  const uint8_t flags = buffer[16];

  // Everything below byte 17 exists only in versioned bench firmware. Gate on the version byte
  // rather than on length alone: length says the bytes are present, version says what they mean.
  uint8_t formatVersion = 0;
  uint8_t muxCode = 0;
  uint16_t fdcStatus = 0;
  bool boardTempValid = false;
  int16_t boardTempQuarterC = 0;
  if (len >= MSV3_BENCH_PACKET_V1_LEN)
  {
    const int16_t tempField = (int16_t)(buffer[17] | (buffer[18] << 8));
    // Independent range check, not just trust of the probe's own validity bit - the same
    // defence the production path applies to this identical field.
    boardTempValid = (flags & BENCH_ADV_FLAG_BOARD_TEMP_OK) != 0 &&
                     tempField >= -160 && tempField <= 340;
    if (boardTempValid)
    {
      boardTempQuarterC = tempField;
    }
    if (buffer[19] >= MSV3_BENCH_FORMAT_VERSION_1)
    {
      formatVersion = buffer[19];
      muxCode = buffer[20] & 0x03;
      fdcStatus = buffer[21] | (buffer[22] << 8);
    }
  }

  const uint32_t receivedAtMs = millis();

  portENTER_CRITICAL(&benchMux);
  // Slot lookup: existing id, then any free slot, then evict the stalest. Five slots scanned
  // three times is cheaper than keeping a sorted structure, and this runs once per packet.
  int8_t slot = -1;
  for (uint8_t i = 0; i < BENCH_SLOT_COUNT; i++)
  {
    if (benchProbes[i].seen && benchProbes[i].sensorId == sensorId)
    {
      slot = (int8_t)i;
      break;
    }
  }
  if (slot < 0)
  {
    for (uint8_t i = 0; i < BENCH_SLOT_COUNT; i++)
    {
      if (!benchProbes[i].seen)
      {
        slot = (int8_t)i;
        break;
      }
    }
  }
  if (slot < 0)
  {
    uint32_t oldestSeenMs = benchProbes[0].lastSeenMs;
    slot = 0;
    for (uint8_t i = 1; i < BENCH_SLOT_COUNT; i++)
    {
      if ((int32_t)(benchProbes[i].lastSeenMs - oldestSeenMs) < 0)
      {
        oldestSeenMs = benchProbes[i].lastSeenMs;
        slot = (int8_t)i;
      }
    }
  }

  BenchProbe& probe = benchProbes[slot];
  if (probe.seen && probe.sensorId != sensorId)
  {
    // A recycled slot must not inherit the evicted probe's packet count.
    probe.received = 0;
  }
  probe.seen = true;
  probe.sensorId = sensorId;
  probe.wake = wake;
  probe.fdcCounts = fdcCounts;
  probe.readingValid = (flags & BENCH_ADV_FLAG_READING_INVALID) == 0;
  probe.batteryMv = batteryMv;
  probe.charging = (flags & BENCH_ADV_FLAG_CHARGING) != 0;
  probe.otaPowerOk = (flags & BENCH_ADV_FLAG_OTA_POWER_OK) != 0;
  probe.liveMode = (flags & BENCH_ADV_FLAG_LIVE_MODE) != 0;
  probe.boardTempValid = boardTempValid;
  probe.boardTempQuarterC = boardTempQuarterC;
  probe.formatVersion = formatVersion;
  probe.muxCode = muxCode;
  probe.fdcStatus = fdcStatus;
  probe.rssi = packetRssi;
  probe.lastSeenMs = receivedAtMs;
  probe.received++;
  portEXIT_CRITICAL(&benchMux);

  // Deliberately NOT sensorDataDirty. That flag drives the dashboard's render gate and the
  // cloud snapshot; bench traffic must move neither.
  benchDataDirty = true;
}

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

    // Bring-up firmware first, and it returns unconditionally. The production path below is
    // reached only by a genuine "MSV2" packet, exactly as before this branch existed - a bench
    // probe still cannot reach sensors[], history or the cloud by any route.
    if (memcmp(&buffer[2], MSV3_BENCH_MAGIC, sizeof(MSV3_BENCH_MAGIC)) == 0)
    {
      parseBenchAdvertisement(buffer, len, advertisedDevice.getRSSI());
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

    // len threshold doubles as the format-version check: MSV3_RAW_PACKET_WIDE_LEN
    // MUST be checked before MSV2_RAW_PACKET_MIN_LEN, since a wide packet also
    // satisfies the narrower >= check - checking narrow first would misparse a wide
    // packet's shifted battery-mV/charging bytes as part of a 2-byte raw field. See
    // the MSV2_RAW_PACKET_WIDE_LEN comment above for the full field-layout story.
    uint16_t moisture;
    uint8_t batteryPercent = BATTERY_UNKNOWN;
    bool wideRaw = len >= MSV2_RAW_PACKET_WIDE_LEN;
    bool hasRaw = wideRaw || len >= MSV2_RAW_PACKET_MIN_LEN;
    uint16_t rawMoistureKHz = 0;
    uint16_t rawBatteryMv = 0;
    bool rawCharging = false;
    uint32_t rawFdcCounts = 0;
    bool hasMsv3Diagnostics = false;
    // Defaults to true so every pre-MSV3 packet and every older MSV3 firmware behaves exactly as
    // before: only a probe that explicitly reports a fault, or reports an implausible count, is
    // treated as faulted.
    bool readingValid = true;
    bool boardTempValid = false;
    int16_t rawBoardTempQuarterC = 0;
    bool bareTankValid = false;
    uint32_t rawBareTankFdcCounts = 0;
    bool caplessTank = false;
    bool msv4 = false;
    bool msv4DieTempValid = false;
    int16_t msv4DieTempQuarterC = 0;
    uint8_t msv4FitRmsE4 = 0;
    uint8_t msv4CpDeciPf = 0;
    uint8_t tankPhase = 0;
    if (wideRaw)
    {
      rawFdcCounts = ((uint32_t)buffer[10]) | ((uint32_t)buffer[11] << 8) |
                     ((uint32_t)buffer[12] << 16) | ((uint32_t)buffer[13] << 24);
      rawBatteryMv = buffer[14] | (buffer[15] << 8);
      rawCharging = (buffer[16] & 0x01) != 0;

      // 2026-08-25: board-temp/bare-tank-reference diagnostics extension - see
      // MSV3_DIAGNOSTIC_PACKET_LEN's comment. startAdv() always sends the full 23 bytes once its
      // firmware supports this (an invalid reading sends a zeroed field with its validity bit
      // clear, not a shorter packet), so a packet shorter than 23 here means older MSV3 firmware
      // and is left with hasMsv3Diagnostics=false and every diagnostic field at its default.
      hasMsv3Diagnostics = len >= MSV3_DIAGNOSTIC_PACKET_LEN;
      if (hasMsv3Diagnostics)
      {
        uint8_t flags = buffer[16];
        tankPhase = (flags & 0x02) ? 1 : 0;
        int16_t tempField = (int16_t)(buffer[17] | (buffer[18] << 8));
        uint32_t bareTankField = ((uint32_t)buffer[19]) | ((uint32_t)buffer[20] << 8) |
                                  ((uint32_t)buffer[21] << 16) | ((uint32_t)buffer[22] << 24);
        // Independent receiver-side range check, not just trust of the probe's own validity bit
        // - a corrupted/malformed packet could have the bit set with a nonsense value.
        boardTempValid = (flags & 0x04) != 0 &&
                          tempField >= -160 && tempField <= 340;
        rawBoardTempQuarterC = boardTempValid ? tempField : 0;
        bareTankValid = (flags & 0x08) != 0;
        rawBareTankFdcCounts = bareTankValid ? bareTankField : 0;
        caplessTank = (flags & 0x40) != 0;
        // The capless policy and a valid bare reference are mutually exclusive. Fail closed if a
        // malformed packet asserts both: retain the capless hardware identity and discard the
        // impossible reference field rather than feeding it into the retired compensation path.
        if (caplessTank)
        {
          bareTankValid = false;
          rawBareTankFdcCounts = 0;
        }

        // 2026-09-04, MSV3 probe flag bit5: the probe advertised but its FDC read failed.
        //
        // The probe used to go silent on a sensor fault, which also closed the only channel for
        // reaching or updating a coated probe over the radio. It now always advertises and marks
        // the reading instead, so this bit is what stops a sensor fault from being ingested as a
        // moisture value.
        //
        // Same belt-and-braces as the temperature field directly above: do not trust the probe's
        // bit alone. Legacy C17 tanks sit around 15-23M; Probe 8's capless tank measured 39.4M
        // dry and 27M under a two-hand load. The probe zeroes the field when it marks the reading
        // invalid, and anything outside the hardware-specific sane band is not a reading whatever
        // the flags claim.
        const uint32_t MSV3_PLAUSIBLE_COUNT_MIN = 5000000UL;
        const uint32_t MSV3_LEGACY_PLAUSIBLE_COUNT_MAX = 40000000UL;
        const uint32_t MSV3_CAPLESS_PLAUSIBLE_COUNT_MAX = 80000000UL;
        const uint32_t plausibleCountMax = caplessTank
            ? MSV3_CAPLESS_PLAUSIBLE_COUNT_MAX
            : MSV3_LEGACY_PLAUSIBLE_COUNT_MAX;
        readingValid = (flags & 0x20) == 0 &&
                       rawFdcCounts >= MSV3_PLAUSIBLE_COUNT_MIN &&
                       rawFdcCounts <= plausibleCountMax;

        // 2026-10-06, MSV4 (flag bit7). Same bytes, different quantities - see
        // MSV4_MOISTURE_SCALE_VERSION - so replace the MSV3 reading of them rather than layer on
        // it. There is no tank and no bare reference: the four diagnostic bytes are kept verbatim
        // with bareTankValid false, and the plausibility band is a resistance, not a count. The
        // temperature range check above applies unchanged; for MSV4 that field is the blade tip.
        msv4 = (flags & MSV4_ADV_FLAG) != 0;
        if (msv4)
        {
          caplessTank = false;
          bareTankValid = false;
          // Split the 32-bit field before anything judges it as a resistance.
          const uint8_t msv4TopByte = (uint8_t)(rawFdcCounts >> 24);
          rawFdcCounts &= MSV4_RSOIL_MASK;
          if (len >= MSV4_EXTENDED_PACKET_LEN && (buffer[25] & MSV4_ADV_FLAG2_LAYOUT_B) != 0)
          {
            // Layout B: residual in the top byte, bytes 19-22 already Cm | Cd << 16.
            msv4FitRmsE4 = msv4TopByte;
            rawBareTankFdcCounts = bareTankField;
          }
          else
          {
            // Layout A (262148/262150): Cm, then Cp, then the residual, and no Cd.
            msv4CpDeciPf = (uint8_t)((bareTankField >> 16) & 0xFFUL);
            msv4FitRmsE4 = (uint8_t)(bareTankField >> 24);
            rawBareTankFdcCounts = bareTankField & 0xFFFFUL;
          }
          readingValid = (flags & 0x20) == 0 &&
                         rawFdcCounts >= MSV4_RSOIL_DECIOHM_MIN &&
                         rawFdcCounts <= MSV4_RSOIL_DECIOHM_MAX;
          if (len >= MSV4_EXTENDED_PACKET_LEN && (buffer[25] & MSV4_ADV_FLAG2_DIE_TEMP) != 0)
          {
            // Same range and same distrust of the probe's own validity bit as the field above.
            int16_t dieField = (int16_t)(buffer[23] | (buffer[24] << 8));
            msv4DieTempValid = dieField >= -160 && dieField <= 340;
            msv4DieTempQuarterC = msv4DieTempValid ? dieField : 0;
          }
        }
      }
      if (msv4)
      {
        moisture = calibrateMsv4Moisture(rawFdcCounts, boardTempValid, rawBoardTempQuarterC);
      }
      else
      {
        moisture = caplessTank
            ? calibrateMsv3CaplessMoisture(rawFdcCounts)
            : calibrateMsv3Moisture(rawFdcCounts);
      }
    }
    else if (hasRaw)
    {
      rawMoistureKHz = buffer[10] | (buffer[11] << 8);
      rawBatteryMv = buffer[12] | (buffer[13] << 8);
      rawCharging = (buffer[14] & 0x01) != 0;
      moisture = calibrateRawMoisture(sensorId, rawMoistureKHz);
    }
    else
    {
      moisture = buffer[10] | (buffer[11] << 8);
      batteryPercent = len >= 13 ? buffer[12] : BATTERY_UNKNOWN;
    }

    const int packetRssi = advertisedDevice.getRSSI();
    const uint32_t receivedAtMs = millis();

    if (!readingValid)
    {
      // A faulted probe is ALIVE and must look alive -- it is still reachable, still reporting its
      // battery, and still updatable over BLE, which is the entire reason it now advertises at all.
      // What it must not do is contribute a number to this plant's history.
      //
      // Deliberately placed before smoothMoistureForDisplay() and before the history enqueue: the
      // display filter is stateful, so feeding it a fault would corrupt the smoothed value for
      // several subsequent good readings, and a stored row would be indistinguishable from a real
      // measurement once it reached the cloud.
      portENTER_CRITICAL(&sensorsMux);
      SensorReading& faulted = sensors[sensorId];
      if (faulted.seen && wake == faulted.wake)
      {
        portEXIT_CRITICAL(&sensorsMux);
        return;
      }
      faulted.seen = true;
      faulted.wake = wake;
      faulted.received++;
      faulted.sensorFault = true;
      faulted.rssi = packetRssi;
      faulted.lastSeenMs = receivedAtMs;
      if (hasRaw)
      {
        faulted.rawBatteryMv = rawBatteryMv;
        faulted.rawCharging = rawCharging;
        faulted.batteryPercent = calibrateRawBattery(sensorId, (float)rawBatteryMv, msv4);
      }
      portEXIT_CRITICAL(&sensorsMux);
      sensorDataDirty = true;

      Serial.print("[Treebeard] sensor ");
      Serial.print(sensorId);
      Serial.print(" reported a FAILED measurement (wake ");
      Serial.print(wake);
      Serial.println("); still reachable, reading not stored");
      return;
    }
    // Reject duplicates before advancing either the display filter or the temperature
    // learner. A second check remains inside the publish transaction below for completeness.
    portENTER_CRITICAL(&sensorsMux);
    bool duplicateWake = sensors[sensorId].seen && wake == sensors[sensorId].wake;
    bool probeRestarted = sensors[sensorId].seen && wake < sensors[sensorId].wake;
    portEXIT_CRITICAL(&sensorsMux);
    if (duplicateWake) return;

    TemperatureCompensationResult temperatureCompensationResult;
    if (caplessTank)
    {
      // No reference exists by design. Report the derived channel as off rather than leaving the
      // UI indefinitely claiming that a model with no possible input is still "learning".
      temperatureCompensationResult.configuredMode = TEMPERATURE_COMPENSATION_OFF;
      temperatureCompensationResult.effectiveMode = TEMPERATURE_COMPENSATION_OFF;
    }
    else if (msv4)
    {
      // Same reasoning: this learner models an FDC tank against a bare reference, and MSV4 has
      // neither. Its temperature correction is the fixed one in calibrateMsv4Moisture().
      temperatureCompensationResult.configuredMode = TEMPERATURE_COMPENSATION_OFF;
      temperatureCompensationResult.effectiveMode = TEMPERATURE_COMPENSATION_OFF;
    }
    else if (wideRaw && hasMsv3Diagnostics && boardTempValid && bareTankValid)
    {
      temperatureCompensationResult = updateTemperatureCompensation(
          sensorId, rawFdcCounts, rawBareTankFdcCounts,
          rawBoardTempQuarterC, tankPhase, receivedAtMs, probeRestarted);
    }

    // Snapshot calibration before entering sensorsMux so the packet path never nests
    // sensorsMux and autoCalMux. smoothMoistureForDisplay() only touches this sensor's
    // SensorReading and therefore runs under the single lock below.
    const uint16_t presentationSpan = moisturePresentationSpan(sensorId, moisture);
    SensorReading publishedReading;
    portENTER_CRITICAL(&sensorsMux);
    SensorReading& reading = sensors[sensorId];

    if (reading.seen && wake == reading.wake)
    {
      portEXIT_CRITICAL(&sensorsMux);
      return;
    }

    // A probe generation/packet-format change under the same sensor ID also changes its
    // numeric scale. Do not blend across those incompatible scales.
    if (reading.seen && (reading.hasRaw != hasRaw || reading.wideRaw != wideRaw ||
                         reading.caplessTank != caplessTank || reading.msv4 != msv4))
    {
      reading.moistureCenterCount = 0;
      reading.moistureCenterNext = 0;
      reading.moistureWateringStreak = 0;
    }
    if (reading.seen && wideRaw &&
        reading.temperatureCompensationEffectiveMode != temperatureCompensationResult.effectiveMode)
    {
      // Switching into/out of Active changes the signal feeding the presentation filter.
      // Reset its short memory so the two channels are never blended together.
      reading.moistureCenterCount = 0;
      reading.moistureCenterNext = 0;
      reading.moistureWateringStreak = 0;
    }
    // Battery calibration is stateful too. Keep it after duplicate rejection and inside
    // the same packet transaction so repeated advertisement frames cannot overweight one
    // physical wake in the EMA or race another callback for this sensor's filter state.
    if (hasRaw)
    {
      batteryPercent = calibrateRawBattery(sensorId, (float)rawBatteryMv, msv4);
    }
    const bool useTemperatureCompensatedPresentation =
        temperatureCompensationResult.valid &&
        temperatureCompensationResult.effectiveMode == TEMPERATURE_COMPENSATION_ACTIVE;
    const uint16_t presentationInput = useTemperatureCompensatedPresentation
        ? temperatureCompensationResult.equivalentMoisture
        : moisture;
    uint16_t displayMoisture = smoothMoistureForDisplay(
        sensorId, presentationInput, presentationSpan);

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
    // Publish the whole record inside the critical section begun before duplicate/filter
    // handling. smoothMoistureForDisplay() mutates the filter fields in this same record,
    // so placing the lock after that call would still expose a half-updated packet.
    reading.missedTotal += reading.missed;
    reading.received++;
    reading.seen = true;
    reading.wake = wake;
    reading.sensorFault = false;
    reading.moisture = displayMoisture;
    reading.measuredMoisture = moisture;
    reading.batteryPercent = batteryPercent;
    reading.hasRaw = hasRaw;
    reading.wideRaw = wideRaw;
    reading.rawMoistureKHz = rawMoistureKHz;
    reading.rawFdcCounts = rawFdcCounts;
    reading.rawBatteryMv = rawBatteryMv;
    reading.rawCharging = rawCharging;
    reading.hasMsv3Diagnostics = hasMsv3Diagnostics;
    reading.boardTempValid = boardTempValid;
    reading.rawBoardTempQuarterC = rawBoardTempQuarterC;
    reading.bareTankValid = bareTankValid;
    reading.rawBareTankFdcCounts = rawBareTankFdcCounts;
    reading.caplessTank = caplessTank;
    reading.msv4 = msv4;
    reading.msv4DieTempValid = msv4DieTempValid;
    reading.msv4DieTempQuarterC = msv4DieTempQuarterC;
    reading.msv4FitRmsE4 = msv4FitRmsE4;
    reading.msv4CpDeciPf = msv4CpDeciPf;
    reading.tankPhase = tankPhase;
    reading.temperatureCompensationValid = temperatureCompensationResult.valid;
    reading.temperatureCompensatedLogRatioPpm = temperatureCompensationResult.logRatioPpm;
    reading.temperatureCompensatedEquivalentMoisture = temperatureCompensationResult.equivalentMoisture;
    reading.temperatureCompensationModelVersion = temperatureCompensationResult.valid
        ? TEMPERATURE_COMPENSATION_MODEL_VERSION : 0;
    reading.temperatureCompensationConfiguredMode = temperatureCompensationResult.configuredMode;
    reading.temperatureCompensationEffectiveMode = temperatureCompensationResult.effectiveMode;
    reading.temperatureCompensationConfidencePercent = temperatureCompensationResult.confidencePercent;
    reading.temperatureCompensationGainPerC = temperatureCompensationResult.gainPerC;
    reading.temperatureCompensationReferenceC = temperatureCompensationResult.referenceTempC;
    reading.temperatureCompensationEpoch = temperatureCompensationResult.epoch;
    reading.rssi = packetRssi;
    reading.lastSeenMs = receivedAtMs;
    publishedReading = reading;
    portEXIT_CRITICAL(&sensorsMux);
    sensorDataDirty = true;

    // Queue this immutable wake before another advertisement can overwrite sensors[].
    // Duplicate BLE packets for the same wake returned above, so each queued item is one
    // real receiver-observed sample rather than one row per repeated advertisement frame.
    if (!enqueueHistoryReading(sensorId, publishedReading))
    {
      Serial.print("[Treebeard cloud] history queue full; dropped sensor ");
      Serial.print(sensorId);
      Serial.print(" wake ");
      Serial.println(wake);
    }

    // 2026-07-28: temporary debug print for live erratic-battery-reading investigation -
    // remove once resolved (see chat log / scindapsus_probe memory for context).
    Serial.print("[Treebeard debug] sensor ");
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
    Serial.print(" rawFdcCounts=");
    Serial.print(rawFdcCounts);
    Serial.print(" measuredMoisture=");
    Serial.print(moisture);
    Serial.print(" displayMoisture=");
    Serial.print(displayMoisture);
    if (temperatureCompensationResult.valid)
    {
      Serial.print(" tempCompLogRatioPpm=");
      Serial.print(temperatureCompensationResult.logRatioPpm);
      Serial.print(" tempCompMode=");
      Serial.print(temperatureCompensationModeName(temperatureCompensationResult.effectiveMode));
      Serial.print(" tempCompConfidence=");
      Serial.print(temperatureCompensationResult.confidencePercent);
      Serial.print(" tempCompGain=");
      Serial.print(temperatureCompensationResult.gainPerC, 6);
      Serial.print(" tempCompEpoch=");
      Serial.print(temperatureCompensationResult.epoch);
    }
    if (hasMsv3Diagnostics)
    {
      Serial.print(" tankPhase=");
      Serial.print(tankPhase);
      Serial.print(" boardTempQ4C=");
      Serial.print(boardTempValid ? rawBoardTempQuarterC : 0);
      Serial.print(" boardTempValid=");
      Serial.print(boardTempValid);
      Serial.print(" bareTankFdcCounts=");
      Serial.print(bareTankValid ? rawBareTankFdcCounts : 0);
      Serial.print(" bareTankValid=");
      Serial.print(bareTankValid);
    }
    Serial.println();

    // Do not schedule cloud work from packet reception. The periodic uploader drains all
    // queued wakes at cloudSyncIntervalSeconds cadence, so retaining every sample no longer
    // requires turning frequent BLE advertisements into frequent TLS handshakes.
  }
};

void bleScanTask(void* parameter)
{
  (void)parameter;
  for (;;)
  {
    if (cloudRadioExclusive)
    {
      vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_IDLE_MS));
      continue;
    }
    if (bleScan != nullptr)
    {
      setBleCheckpoint("ble task: scan start");
      bleScan->start(BLE_SCAN_SECONDS, false);
      bleScan->clearResults();
      setBleCheckpoint("ble task: scan done");
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_IDLE_MS));
  }
}
#endif

#if TREEBEARD_ENABLE_WIFI
// Runs the five cloud network calls (bootstrap, display settings, config pull,
// auto-cal pull, snapshot
// upload) on their own task instead of inline in loop(), mirroring bleScanTask above -
// the same pattern that already fixed BLE scanning contention with rendering/WebServer.
// Each of these does a blocking HTTPClient/WiFiClientSecure call (fresh TLS handshake
// every time, no session reuse) capped at an 8s timeout, but observed taking ~1.6-2.1s
// even on success. Running them inline in loop() meant the shared render/WebServer loop
// was blocked for that long on every one of these calls - the config pull alone fires
// periodically (CLOUD_CONFIG_PULL_INTERVAL_MS), which was frequent enough on its own to
// read as "periodic freezes" even with a perfectly calm probe (see 2026-07-26 chat log).
// Pinned to core 0, same as bleScanTask, leaving core 1 free for rendering/WebServer.
// bleScanTask runs one priority level higher, preventing this task's blocking TLS work from
// delaying scan-window scheduling. Packet reception no longer accelerates snapshot uploads.
//
// Shared sensor and auto-calibration records are copied/published under sensorsMux and
// autoCalMux respectively. Other small connectivity/config flags remain single-word
// volatile values. applyCloudUiConfig() cannot rewrite artSlots[] out
// from under a mid-render read: that function only writes
// ui.json to LittleFS and sets displayDirty, and renderUi() (which owns rebuilding
// artSlots[] from that file) only ever runs from the main loop, so this task never
// touches artSlots[] directly.
void cloudSyncTask(void* parameter)
{
  (void)parameter;
  for (;;)
  {
    uint32_t now = millis();
    setCloudCheckpoint("cloud task: iteration start");
    // Spill RTC staging to the power-safe journal only when age/capacity thresholds say an
    // outage is developing. Healthy uploads acknowledge RTC rows directly and never enter
    // this flash-writing path.
    serviceHistoryJournal();
    // Checkpoint EMA and partial-fit state every 15 minutes, and accepted fits/epoch resets
    // immediately. Keep all LittleFS work out of the BLE callback.
    serviceTemperatureCompensationPersistence();
    if (wifiStationConnected && (int32_t)(now - nextCloudNetworkSlotMs) >= 0)
    {
      const auto due = [now](uint32_t deadline) {
        return deadline == 0 || (int32_t)(now - deadline) >= 0;
      };
      const bool bootstrapNeeded = strlen(TREEBEARD_CLOUD_BOOTSTRAP_URL) > 0 &&
                                   (cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0');
      const size_t historyQueued = queuedHistoryCount();
      // A non-empty queue is normal on a receiver continuously hearing probes and must not
      // starve state pulls. Reserve every network slot for recovery only when the durable
      // outbox is genuinely endangered; below that threshold, paced uploads have far more
      // drain capacity (96/min) than measured ingress (~24/min).
      const bool historyEmergency =
          historyQueued >= (HISTORY_QUEUE_CAPACITY * 3U) / 4U;
      bool usedNetworkSlot = false;

      // Exactly one potentially-blocking connection is selected per lane slot. Bootstrap is
      // first when credentials are absent; otherwise history keeps its due-time priority.
      // Auto-cal comes before cosmetic/config state so learned percentages return promptly
      // after boot, without ever launching multiple TLS sessions in one task iteration.
      if (bootstrapNeeded && due(nextCloudBootstrapMs))
      {
        runCloudBootstrap(now);
        usedNetworkSlot = true;
      }
      else if (cloudEnabled && due(nextCloudSyncMs))
      {
        runCloudSnapshotUploader(now);
        usedNetworkSlot = true;
      }
      else if (!historyEmergency && cloudEnabled && due(nextCloudAutoCalPullMs))
      {
        runCloudAutoCalPull(now);
        usedNetworkSlot = true;
      }
      else if (!historyEmergency && cloudEnabled && due(nextCloudDisplaySyncMs))
      {
        runCloudDisplaySettings(now);
        usedNetworkSlot = true;
      }
      else if (!historyEmergency && cloudEnabled && due(nextCloudConfigPullMs))
      {
        runCloudConfigPull(now);
        usedNetworkSlot = true;
      }

      if (usedNetworkSlot)
      {
        nextCloudNetworkSlotMs = millis() + CLOUD_NETWORK_MIN_GAP_MS;
      }
      else if (historyEmergency)
      {
        setCloudCheckpoint("cloud task: history recovery priority");
      }
    }
    setCloudCheckpoint("cloud task: iteration done");
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
    Serial.println("[Treebeard web] GET /");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", TREEBEARD_MOBILE_APP_HTML);
  });

  server.on("/app", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /app");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", TREEBEARD_MOBILE_APP_HTML);
  });

  server.on("/manifest.webmanifest", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /manifest.webmanifest");
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "application/manifest+json", TREEBEARD_WEB_MANIFEST);
  });

  server.on("/sw.js", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /sw.js");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/javascript", TREEBEARD_SERVICE_WORKER_JS);
  });

  server.on("/app-icon.svg", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /app-icon.svg");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "image/svg+xml", TREEBEARD_APP_ICON_SVG);
  });

  // ---- Bench instrument readout, http://<receiver>/bench ----
  //
  // Not reachable from the physical display by design; the panel shows plant data only. These
  // routes serve MSV3 bring-up probes, which the dashboard drops on purpose (see
  // MSV3_BENCH_MAGIC's comment) - so nothing here can put a bench reading into plant history.
  // The bench readout is the app's own Bench TAB, not a separate page - one implementation, so
  // the two cannot drift apart. This route survives only so an existing /bench bookmark still
  // lands somewhere useful.
  server.on("/bench", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /bench -> /app (Bench tab)");
    server.sendHeader("Location", "/app");
    server.send(302, "text/plain", "The bench readout is the Bench tab in the app.\n");
  });

  server.on("/bench-data", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", benchDataJson());
  });

  server.on("/bench-identify", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /bench-identify");
    StaticJsonDocument<128> body;
    if (deserializeJson(body, server.arg("plain")) != DeserializationError::Ok ||
        !body["sensorId"].is<int>())
    {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"sensorId required\"}");
      return;
    }
    const int sensorId = body["sensorId"].as<int>();
    if (sensorId < 0 || sensorId > 255)
    {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"sensorId out of range\"}");
      return;
    }
    // Addressed to one probe, never BENCH_CMD_TARGET_ALL. On a bench with several boards on air,
    // "blink whichever one hears me" is not an identification.
    if (!benchSendCommand((uint8_t)sensorId, BENCH_CMD_OP_IDENTIFY))
    {
      server.send(503, "application/json", "{\"ok\":false,\"error\":\"BLE advertiser unavailable\"}");
      return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/ping", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /ping");
    server.send(200, "text/plain", "pong\n");
  });

  server.on("/generate_204", HTTP_GET, redirectToSetupApp);
  server.on("/gen_204", HTTP_GET, redirectToSetupApp);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToSetupApp);
  server.on("/library/test/success.html", HTTP_GET, redirectToSetupApp);
  server.on("/connecttest.txt", HTTP_GET, redirectToSetupApp);
  server.on("/ncsi.txt", HTTP_GET, redirectToSetupApp);

  server.on("/ui", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /ui");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", readUiJson());
  });

  server.on("/ui", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /ui");
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
    Serial.println("[Treebeard web] GET /data");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", sensorDataJson());
    // Sampled here, not just in loop(): this is the deepest loopTask call path in the build
    // (handler frame + sensorDataJson()'s StaticJsonDocument<8192> + the returned String).
    recordLoopTaskStackFree();
  });

  server.on("/display", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /display");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", displaySettingsJson());
  });

  server.on("/display", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /display");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeDisplaySettingsJson(server.arg("plain"));
  });

  server.on("/sound", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /sound");
    server.sendHeader("Cache-Control", "no-store");
    StaticJsonDocument<64> doc;
    doc["ok"] = true;
    doc["enabled"] = soundEnabled;
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  });

  server.on("/sound", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /sound");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    StaticJsonDocument<64> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error || !doc["enabled"].is<bool>())
    {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"enabled (boolean) is required\"}");
      return;
    }
    setSoundEnabledLocal(doc["enabled"].as<bool>());
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/snapshot", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /snapshot");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", snapshotJson());
  });

  server.on("/device", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /device");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", deviceInfoJson());
  });

  server.on("/config-meta", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /config-meta");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", configMetaJson());
  });

  server.on("/claim", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /claim");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", claimInfoJson());
  });

  server.on("/cloud", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /cloud");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", cloudConfigJson());
  });

  server.on("/cloud", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /cloud");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeCloudConfigJson(server.arg("plain"));
  });

  server.on("/cloud", HTTP_DELETE, []() {
    Serial.println("[Treebeard web] DELETE /cloud");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    resetCloudConfig();
  });

  // Nudges the ~10-minute autocal poll cadence (runCloudAutoCalPull) to run again in
  // ~10s instead of waiting out the rest of the interval. Used by the local app right
  // after a successful calibration reset - resetSensorCalibration() in MobileAppPage.h
  // calls straight through to Supabase, bypassing this receiver entirely, so without this
  // the receiver has no way to know a reset just happened and the physical display/local
  // dashboard would keep showing the stale pre-reset range for up to ~10 minutes. No
  // sensor-specific targeting needed - the pull re-fetches every sensor's state in one
  // request regardless, so "refresh soon" is all this needs to communicate.
  server.on("/autocal-refresh", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /autocal-refresh");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    nextCloudAutoCalPullMs = 0;
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/wifi", HTTP_GET, []() {
    Serial.println("[Treebeard web] GET /wifi");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", wifiStatusJson());
  });

  server.on("/wifi", HTTP_POST, []() {
    Serial.println("[Treebeard web] POST /wifi");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    writeWifiConfigJson(server.arg("plain"));
  });

  server.on("/wifi", HTTP_DELETE, []() {
    Serial.println("[Treebeard web] DELETE /wifi");
    if (!isAdminActionAllowed())
    {
      server.send(401, "application/json", "{\"ok\":false,\"error\":\"admin session expired - power-cycle to reconfigure\"}");
      return;
    }
    forgetWifiConfig();
  });

  server.onNotFound([]() {
    Serial.print("[Treebeard web] 404 ");
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
#if TREEBEARD_ENABLE_WIFI
  return wifiStationConnected ? WiFi.localIP() : WiFi.softAPIP();
#else
  return IPAddress(0, 0, 0, 0);
#endif
}

const char* currentNetworkMode()
{
#if TREEBEARD_ENABLE_WIFI
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
#if TREEBEARD_ENABLE_WIFI
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
  if (strlen(TREEBEARD_CLOUD_BOOTSTRAP_URL) > 0 && (cloudUploadUrl[0] == '\0' || cloudApiToken[0] == '\0'))
  {
    nextCloudBootstrapMs = now;
  }
  if (cloudEnabled)
  {
    const uint32_t earliestUploadMs = cloudLastAttemptMs == 0
        ? now
        : cloudLastAttemptMs + CLOUD_MIN_UPLOAD_SPACING_MS;
    nextCloudSyncMs = (int32_t)(now - earliestUploadMs) >= 0 ? now : earliestUploadMs;
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
#if TREEBEARD_ENABLE_WIFI
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
      Serial.print("[Treebeard wifi] connection restored: ");
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
    Serial.print("[Treebeard wifi] connection lost; status=");
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
  Serial.print("[Treebeard wifi] reconnect attempt ");
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
#if TREEBEARD_ENABLE_WIFI
  wifiStationConnected = false;
  wifiStationDesired = false;
  wifiSetupPortalActive = false;
  activeWifiSsid[0] = '\0';
  activeWifiPassword[0] = '\0';
  loadStoredWifiConfig();

  const bool forceSetupUnlocked = TREEBEARD_FORCE_WIFI_SETUP != 0 && LittleFS.exists(FORCE_WIFI_SETUP_UNLOCK_PATH);
  const bool forceSetupMode = TREEBEARD_FORCE_WIFI_SETUP != 0 && !forceSetupUnlocked;
  const bool useLocalOnly = wifiLocalOnly && !forceSetupMode;
  const char* stationSsid = (forceSetupMode || useLocalOnly) ? "" : (storedWifiLoaded ? configuredWifiSsid : WIFI_SSID);
  const char* stationPassword = (forceSetupMode || useLocalOnly) ? "" : (storedWifiLoaded ? configuredWifiPassword : WIFI_PASSWORD);

  if (forceSetupMode)
  {
    Serial.println("[Treebeard wifi] forced setup mode; no saved phone setup choice found");
  }
  else if (useLocalOnly)
  {
    Serial.println("[Treebeard wifi] local-only mode enabled; starting receiver access point");
  }
  else if (forceSetupUnlocked)
  {
    Serial.println("[Treebeard wifi] forced setup completed; using saved phone setup choice");
  }

  if (strlen(stationSsid) > 0)
  {
    wifiStationDesired = true;
    strlcpy(activeWifiSsid, stationSsid, sizeof(activeWifiSsid));
    strlcpy(activeWifiPassword, stationPassword, sizeof(activeWifiPassword));
    bootLog("connecting WiFi STA");
    Serial.print("[Treebeard wifi] SSID: ");
    Serial.println(stationSsid);
    Serial.print("[Treebeard wifi] credential source: ");
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
      Serial.print("[Treebeard wifi] connected: ");
      Serial.println(WiFi.localIP());
      return;
    }

    Serial.println("[Treebeard wifi] station connect failed; falling back to AP");
    if (storedWifiLoaded)
    {
      uint8_t retryCount = loadWifiRetryCount();
      if (retryCount < WIFI_CONNECT_REBOOT_RETRIES)
      {
        retryCount++;
        saveWifiRetryCount(retryCount);
        Serial.print("[Treebeard wifi] saved credential retry reboot ");
        Serial.print(retryCount);
        Serial.print("/");
        Serial.println(WIFI_CONNECT_REBOOT_RETRIES);
        renderWifiRetryScreen(stationSsid, retryCount, WIFI_CONNECT_REBOOT_RETRIES);
        Serial.flush();
        delay(1500);
        intentionalRestartPending = true;
        ESP.restart();
      }
      clearWifiRetryState();
      Serial.println("[Treebeard wifi] saved credential retries exhausted; starting setup AP");
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
    Serial.println("[Treebeard wifi] AP start failed; retrying radio reset");
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
    Serial.print("[Treebeard wifi] AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[Treebeard wifi] AP IP: ");
    Serial.println(WiFi.softAPIP());
    if (dnsServerPtr == nullptr)
    {
      dnsServerPtr = new DNSServer();
    }
    if (dnsServerPtr != nullptr)
    {
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.println("[Treebeard wifi] captive DNS started");
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
  Serial.println(TREEBEARD_BUILD_TAG);
  Serial.print("[Treebeard boot] previous checkpoint before reset: ");
  Serial.println(lastCheckpoint);
  Serial.print("[Treebeard boot] reset reason: ");
  Serial.println(resetReasonName(resetReason));

  // 2026-08-23 telemetry: snapshot the prior boot's RTC-held state before this boot's own
  // setCheckpoint()/bootLog() calls below overwrite it, so it can be persisted to LittleFS
  // (see the one-time boot record after LittleFS mounts). RTC memory survives any reset but
  // not a full power loss, which is exactly the gap that LittleFS record covers.
  char prevCheckpointMain[48];
  char prevCheckpointBle[48];
  char prevCheckpointCloud[48];
  strlcpy(prevCheckpointMain, lastCheckpoint, sizeof(prevCheckpointMain));
  strlcpy(prevCheckpointBle, lastCheckpointBle, sizeof(prevCheckpointBle));
  strlcpy(prevCheckpointCloud, lastCheckpointCloud, sizeof(prevCheckpointCloud));
  bool wasIntentionalRestart = intentionalRestartPending;
  intentionalRestartPending = false;

  randomSeed(esp_random());
  historyQueueBootId = esp_random();
  if (historyQueueBootId == 0)
  {
    historyQueueBootId = 1;
  }
  restoreHistoryStagingFromRtc();
  bootLog("serial ready");

  adminUnlockUntilMs = millis() + ADMIN_GRACE_WINDOW_MS;
  displaySettingsFileMutex = xSemaphoreCreateMutex();

  bootLog("mounting LittleFS");
  bool littleFsMounted = LittleFS.begin(true);
  if (!littleFsMounted)
  {
    bootLog("LittleFS mount failed");
  }
  else
  {
    // One compact record per boot, overwritten each time (not a periodic heartbeat, which
    // would wear the flash for no benefit) - captures what the PREVIOUS boot's RTC state
    // was, so a full power loss (which clears RTC memory, unlike a plain reset) still
    // leaves one prior data point instead of none.
    StaticJsonDocument<384> bootRecord;
    bootRecord["resetReason"] = resetReasonName(resetReason);
    bootRecord["wasIntentionalRestart"] = wasIntentionalRestart;
    bootRecord["prevCheckpointMain"] = prevCheckpointMain;
    bootRecord["prevCheckpointBle"] = prevCheckpointBle;
    bootRecord["prevCheckpointCloud"] = prevCheckpointCloud;
    File bootRecordFile = LittleFS.open("/boot-record.json", "w");
    if (bootRecordFile)
    {
      serializeJson(bootRecord, bootRecordFile);
      bootRecordFile.close();
    }
  }
  if (littleFsMounted)
  {
    bootLog("recovering durable history outbox");
    if (!initializeHistoryJournal())
    {
      bootLog("history outbox recovery failed; using RAM ingress until retry");
    }
  }
  ensureDefaultUi();
  ensureUiMetadata();
  loadTemperatureCompensationModes();
  loadDeviceIdentity();
  loadCloudConfig();
  if (littleFsMounted)
  {
    loadAutoCalCache();
    loadTemperatureCompensationModels();
  }
  loadDisplaySettings();
  loadTouchCalibration();
  loadSoundSettings();
  bootLog("LittleFS ready");

  bootLog("initializing buzzer PWM (GPIO38)");
  buzzerReady = ledcAttach(BUZZER_PIN, 1000, BUZZER_PWM_RESOLUTION_BITS);
  if (buzzerReady)
  {
    ledcWrite(BUZZER_PIN, 0);
  }
  else
  {
    bootLog("buzzer PWM attach failed");
  }

#if TREEBEARD_ENABLE_DISPLAY
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
  Serial.print("[Treebeard display] rotation=");
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
  Serial.print("[Treebeard touch] mode=polling cs=GPIO");
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

#if TREEBEARD_ENABLE_WIFI
  setupWifi();
  serverPtr = new WebServer(80);
  if (serverPtr == nullptr)
  {
    bootLog("web server allocation failed");
    return;
  }
  setupServer();
  bootLog("WiFi/server ready");
  if (xTaskCreatePinnedToCore(cloudSyncTask, "msv2_cloud_sync", CLOUD_SYNC_TASK_STACK_BYTES, nullptr, CLOUD_SYNC_TASK_PRIORITY, &cloudSyncTaskHandle, 0) == pdPASS)
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

#if TREEBEARD_ENABLE_BLE
#if TREEBEARD_ENABLE_WIFI
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
    if (xTaskCreatePinnedToCore(bleScanTask, "msv2_ble_scan", BLE_SCAN_TASK_STACK_BYTES, nullptr, BLE_SCAN_TASK_PRIORITY, &bleScanTaskHandle, 0) == pdPASS)
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

#if TREEBEARD_ENABLE_WIFI
  Serial.print("MSV2 display runtime ready at http://");
  Serial.println(currentIpAddress());
#else
  Serial.println("MSV2 display runtime ready");
#endif
  bootLog("initial render");
  displayDirty = false;
#if TREEBEARD_ENABLE_WIFI
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
  // Once per successful boot/reset - setup() runs on every real power-cycle and every
  // ESP.restart() (WiFi retry-exhausted reboot, config-change reboot), and "the
  // receiver came alive" is still an accurate chime in each of those cases. The WiFi
  // self-heal radio-reset path (maintainWifiConnection) does not call ESP.restart() and
  // does not replay this.
  startBuzzerPattern(BUZZER_STARTUP_SOUND, sizeof(BUZZER_STARTUP_SOUND) / sizeof(BUZZER_STARTUP_SOUND[0]), 1, 0);
  bootLog("setup complete");
}

void loop()
{
  uint32_t now = millis();
  uint32_t freeHeapNow = ESP.getFreeHeap();
  if (freeHeapNow < minFreeHeapEverBytes)
  {
    minFreeHeapEverBytes = freeHeapNow;
  }
  recordLoopTaskStackFree();
  serviceBuzzer(now);
  // Takes an identify command back off air once its window closes. Unconditional and outside the
  // display guard: the command is triggered over HTTP now, so nothing about it depends on the
  // panel being present or awake. Returns immediately when no command is in flight.
  serviceBenchCommandBroadcast();

#if TREEBEARD_ENABLE_WIFI
  maintainWifiConnection(now);
  if (dnsServerPtr != nullptr && !wifiStationConnected)
  {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  if (pendingRestartMs != 0 && (int32_t)(now - pendingRestartMs) >= 0)
  {
    Serial.println("[Treebeard wifi] restarting after WiFi config change");
    Serial.flush();
    delay(50);
    intentionalRestartPending = true;
    ESP.restart();
  }
  // Cloud bootstrap/config-pull/auto-cal-pull/snapshot-upload now run on their own task
  // (cloudSyncTask, created in setup()) instead of inline here - see that function's
  // comment for why: these are blocking HTTPClient calls that were stalling this shared
  // render/WebServer loop every time they fired, most noticeably the ~30s config pull.
#endif

#if TREEBEARD_ENABLE_DISPLAY
  handleTouchInput(now);
  if (brightnessOverlayOpen)
  {
    // Keep the modal stable while a finger is dragging. Sensor data remains
    // cached and triggers its normal redraw as soon as the overlay closes.
    delay(1);
    return;
  }
#if TREEBEARD_ENABLE_WIFI
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
  serviceWaterAlert(now, blinkNeeded);
  bool detailOpen = expandedCardSlot >= 0;
  uint32_t renderIntervalMs = blinkNeeded ? 500 : SENSOR_RENDER_INTERVAL_MS;
  // touchAcquisitionPending gates all three periodic display writers below - see its own
  // comment. A gesture in the middle of acquiring its one coordinate read needs a genuinely
  // quiet bus, so these just wait their turn for a few ms rather than racing the touch read.
  if (!touchAcquisitionPending && (sensorDataDirty || blinkNeeded || detailOpen) && now - lastSensorRenderMs >= renderIntervalMs)
  {
    bool realDataChanged = sensorDataDirty;
    sensorDataDirty = false;
    lastSensorRenderMs = now;
    if (artSlotCount > 0)
    {
      renderDashboardDataOnly(!realDataChanged);
      // Re-read the clock - `now` is loop()'s top-of-iteration snapshot, taken before this
      // blocking call ran. Stamping with it instead of the actual completion time understates
      // how recently the display bus was active and can let handleTouchInput() spend its touch
      // SPI read while a slow render is still finishing on the wire.
      lastDisplaySpiActivityMs = millis();
    }
    else
    {
      displayDirty = true;
    }
  }

  if (!touchAcquisitionPending && now - lastArtFrameMs >= ART_RENDER_POLL_MS)
  {
    lastArtFrameMs = now;
    if (!displayDirty)
    {
      renderAnimatedPlantArt();
      // See the comment on the renderDashboardDataOnly() stamp above - same stale-`now` risk.
      lastDisplaySpiActivityMs = millis();
    }
  }

  if (!touchAcquisitionPending && displayDirty)
  {
    displayDirty = false;
    setCheckpoint("loop: render start");
    renderUi();
    // See the comment on the renderDashboardDataOnly() stamp above - same stale-`now` risk,
    // and renderUi() is the slowest of the three (full JSON reparse + full-screen rebuild).
    lastDisplaySpiActivityMs = millis();
    setCheckpoint("loop: render done");
  }
#endif

  delay(1);
}

#endif

// --- 2026-09-02 loopTask stack instrumentation -------------------------------------------
// Deliberately at the END of this file. Both definitions below (SET_LOOP_TASK_STACK_SIZE
// expands to a function definition too) were first placed up beside the task constants,
// which broke the build: the Arduino .ino preprocessor inserts every auto-generated
// prototype immediately before the file's FIRST function definition, so putting one above
// the struct declarations pushed prototypes referencing `BuzzerStep` above `BuzzerStep`
// itself ("error: 'BuzzerStep' does not name a type"). Keep new file-scope function
// definitions below the existing ones.

SET_LOOP_TASK_STACK_SIZE(TREEBEARD_LOOP_TASK_STACK_BYTES);

// Records the smallest free loopTask stack seen this boot. Call from any code that actually
// runs ON loopTask - loop() itself, and the /data handler after its 8KB document exists.
void recordLoopTaskStackFree()
{
  uint32_t freeBytes = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  if (freeBytes < minLoopTaskStackFreeEverBytes)
  {
    minLoopTaskStackFreeEverBytes = freeBytes;
  }
}
