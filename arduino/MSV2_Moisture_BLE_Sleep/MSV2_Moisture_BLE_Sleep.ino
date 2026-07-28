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
const uint8_t SENSOR_ID = 1; // This is the Scindapsus probe -- the physically-reworked (two-plate,
                              // 10k/10k) unit on COM7, currently the one plugged in for bench testing.
                              // The Alocasia probe (sensor 2) is a separate physical unit running
                              // long-term elsewhere on the old single-plate/100k firmware -- do NOT
                              // flash this build to it as-is, hardware doesn't match. Confirmed
                              // 2026-07-27 against live dashboard data: sensor 1 = Scindapsus, sensor
                              // 2 = Alocasia.

// Measure/check cadence - cheap (a few ms of oscillator sampling), so this can run
// often without much battery cost. Actually advertising over BLE is the expensive part
// (radio on, ~ADVERTISE_MS awake) so it happens far less often - see shouldAdvertise
// in loop() below. Matches the "measure often, radio rarely" pattern sketched in
// docs/PRODUCT_ROADMAP.md's probe section, tightened to this test's own numbers.
const uint32_t WAKE_PERIOD_MS = 10000;
// Normal BLE advertise cadence when nothing eventful is happening.
const uint32_t ADVERTISE_INTERVAL_MS = 60000;
// If moisture moves at least this much between checks (e.g. a watering event, or the
// probe being pulled out), advertise immediately instead of waiting for the next
// scheduled interval - matches PRODUCT_ROADMAP.md's "if moisture changed meaningfully"
// condition. ~27% of the default 120-175 calibration range (see MOISTURE_INVERT_CEILING
// below) - big enough to skip normal sample-to-sample noise, small enough to catch a
// real watering/removal event within one 10s check.
const int MOISTURE_SWING_THRESHOLD = 15;
// bigSwing below had no rate limit of its own - if the reading is genuinely unstable (e.g.
// the probe sitting disconnected in open air instead of soil, which the whole oscillator
// design is prone to being noisy under - see 2026-07-25/26 chat log), it can swing past
// MOISTURE_SWING_THRESHOLD on every single WAKE_PERIOD_MS (10s) measurement, forcing an
// advertise every 10s instead of the normal 60s indefinitely. That's a 6x jump in BLE
// traffic, which was enough to keep re-triggering the receiver's scheduleCloudSyncSoon()
// faster than its own blocking cloud-sync HTTP call (capped at 8s) could clear, leaving the
// receiver's shared render/WebServer loop blocked roughly half the time for as long as the
// probe stayed noisy - the "display freezes for long periods" symptom traced back to this.
// A real watering/removal event is a one-time step change, not continuous noise, so it
// doesn't need to fire more often than this floor to still be caught quickly.
const uint32_t MIN_SWING_ADVERTISE_INTERVAL_MS = 30000;
const uint32_t ADVERTISE_MS = 500;
const uint32_t LED_PULSE_MS = 80;
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = false; // onboard RGB disabled for tonight's battery-impact test - see EXTERNAL_LED_PIN below
const bool ENABLE_SERIAL_LOGS = false; // disabled for the unattended overnight run - flip back to true to debug

// External LED on D1: no longer a per-wake/per-advertise heartbeat (that cost battery
// for no real benefit) - now purely a low-moisture alert, blinking 3x every wake while
// moisture is low. LOW_MOISTURE_THRESHOLD is a FIXED cutoff, not the live auto-learned
// or per-plant manual range configured on the receiver - this probe is advertise-only
// (no BLE receive path), so it has no way to learn either of those without a real
// architecture change. Matches the same 25%-of-default-range "danger" cutoff already
// used everywhere else in this project (MSV2_ESP32_Display_Runtime.ino's healthColor()),
// off the default dry=120/wet=175 calibration scale: 120 + 0.25*(175-120) = 134.
const int EXTERNAL_LED_PIN = D1;
const bool EXTERNAL_LED_ACTIVE_HIGH = true;
const bool ENABLE_EXTERNAL_LED = true;
const int LOW_MOISTURE_THRESHOLD = 134;

// Moisture sensing: free-running RC relaxation oscillator, not one-shot timing (see
// docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md for the original architecture change and
// the 2026-07-27 bench session for everything below it). Bench-validated in
// MSV2_Moisture_Probe_Test.ino through several real hardware/firmware iterations since
// the single-plate/100k version this originally shipped with:
//   - Sensor reworked from one plate (driven/read) + a hardwired ground-referenced
//     second comb, to TWO independently-addressable plates, each with its own 10k
//     pulldown to GND (PLATE_A = original IO0/D0 trace, PLATE_B = new IO2/D2 jumper).
//     countOscCycles() now alternates which plate drives and which reads every single
//     cycle, so the net DC field across the sensor averages toward zero instead of
//     compounding in one direction - this is the actual fix for the steady upward
//     drift seen over back-to-back samples on the old single-plate design.
//   - Rewritten from pinMode()/digitalWrite()/digitalRead() to direct nRF52840 NRF_P0
//     register access, because at 10k the Arduino-call overhead (5 calls/cycle) was
//     eating up to ~88% of each cycle, compressing the moisture range almost flat.
//   - CHARGE_SETTLE_NOPS: a hard-won fix, not a style choice. Removing the Arduino-call
//     overhead also removed the incidental ~1-2us of charge time those calls used to
//     provide between "drive HIGH" and "release to read" - without it, a touch/moisture-
//     loaded plate never reached a real HIGH before release, giving a small, plausible-
//     looking reading that had silently stopped tracking real capacitance at all (zero
//     touch response, confirmed live on the bench). Too little settling time also showed
//     up as a WET-END-SPECIFIC saturation (heavy contact pegged at a floor and wouldn't
//     go lower no matter how much more was added) even after touch response elsewhere
//     was restored - the fixed charge pulse ran out of budget for that much capacitance.
//     40 was sized for the wettest condition tested on the bench (multi-finger heavy
//     contact reading monotonically, no plateau), not just a light single touch -
//     correctly resolving "very wet" vs. "waterlogged" matters more for a soil sensor
//     than maximizing dry-end range. Re-verify touch/moisture response live on real
//     hardware after ever changing this - undershooting fails silently.
const int PLATE_A_PIN = D0; // IO0 on the XIAO silkscreen -- one comb of the sensor.
const int PLATE_B_PIN = D2; // IO2 on the XIAO silkscreen -- the other comb.
// Per Seeed_XIAO_nRF52840_Sense's own variant.cpp pin map: D0/A0 (PLATE_A) = P0.02,
// D2/A2 (PLATE_B) = P0.28 -- both on NRF_P0, so no port-1 handling needed here.
#define PLATE_A_BIT (1UL << 2)  // P0.02
#define PLATE_B_BIT (1UL << 28) // P0.28
const uint32_t OSC_SAMPLE_WINDOW_MS = 1;
const uint8_t OSC_MEDIAN_SAMPLES = 5; // 5 x 1ms bursts, median-of-5 rejects scheduler-jitter outliers
// Safety cap per half-cycle so a disconnected/open probe can't hang a burst.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL;
// Bench-observed range with this topology (10k/10k, 40-NOP charge settle) was roughly
// 220-360kHz -- a totally different scale than the old single-plate/100k design's
// ~10-90kHz this constant used to be tuned for. Generous headroom above the highest
// frequency this sensor is expected to ever report - see the inversion comment in
// readAveragedMoisture() below. PROVISIONAL: only bench-verified (touch/multi-finger
// contact as a moisture stimulus), not yet against real soil - revisit once overnight/
// real-soil data exists, same as every other calibration constant in this project so far.
const uint16_t MOISTURE_INVERT_CEILING = 500;

// 3.0V ADC reference, 12-bit resolution: 3000mV / 4096 counts.
#define VBAT_MV_PER_LSB (0.73242188F)
// XIAO nRF52840 Sense VBAT divider: BAT -> R1(1M) -> P0.31/AIN7_BAT -> R2(510K) -> P0.14 (sunk
// low to enable). Nominal comp = 2.961; calibrated to 2.946 against a multimeter on this board
// (see MSV2_Moisture_RC_Test for the derivation) - within 1%-resistor tolerance of nominal.
#define VBAT_DIVIDER_COMP (2.946F)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

// BQ25100 ~CHG status, active LOW while actively charging - same pin already bench-verified
// as a plain Arduino digital input in MSV2_Moisture_Probe_Test.ino (no core macro exists for
// it, but the raw pin number works fine with pinMode()/digitalRead() on this board/core).
const int CHG_STATUS_PIN = 23; // D23/P0.17

// Charging current flows through the cell's + path resistance in the opposite direction from
// discharge, so terminal voltage while charging sits ABOVE true rest/open-circuit voltage by
// roughly I*R - the same curve read while charging looks like a much higher percent than the
// cell actually holds, and reads as a sudden jump the moment charging starts/stops (documented
// battery-industry behavior, not a bug - see 2026-07-25 chat log). Subtracting an estimated
// offset from the raw mV before running it through the normal curve approximates "what would
// this cell read at rest" without needing a separate full charge-curve calibration.
// PROVISIONAL: derived from one observed transition (2026-07-25, ~20%->80% jump on plug-in,
// implying ~400mV). Needs refinement from a real logged charge cycle - see
// scripts/log_sensor_readings.py - averaged over several plug-in events, ideally tied to the
// actual charge current (PIN_CHARGING_CURRENT/HICHG) once that's pinned to a known value
// instead of left floating.
const float CHARGING_IR_COMPENSATION_MV = 400.0F;

// The IR-drop overshoot above is proportional to actual charge CURRENT (V=IR), which isn't
// constant through a charge cycle: it holds roughly steady during the bulk constant-current
// phase, then tapers toward zero during the constant-voltage phase as the cell approaches
// full - real charger behavior, not something this firmware controls. Applying a FIXED
// compensation right up until the BQ25100 finally stops charging (~CHG flips) meant the
// correction didn't taper down the way the real overshoot did - the reported percent sat
// artificially low through the CV taper, then the full 400mV snapped away all at once the
// instant charging stopped, showing up as a jump around 85%->100% (2026-07-25 chat log).
// Tapering the compensation itself to zero as the raw reading approaches full charge fixes
// this: by the time the chip actually stops charging, the correction has already faded out,
// so there's nothing left to snap away.
const float CHARGE_TAPER_START_MV = 4100.0F; // full compensation still applies at/below this
const float CHARGE_TAPER_END_MV = 4200.0F;   // compensation is fully gone at/above this (full)

// Unused for tonight's test (timed wake/send/sleep only) - commented out rather than
// deleted since true System OFF deep sleep is real future work for the 30-min production
// cadence. Re-enabling needs WAKE_PIN moved off A0 first (see note below on why).
//
// #define USE_SYSTEM_OFF_AFTER_SEND 1
//
// // In System OFF mode, connect WAKE_PIN to GND to wake/reset the nRF52840.
// // Change this to a pin driven by an external RTC/timer/sensor interrupt for
// // periodic true-deep-sleep wakeups.
// // NOTE: A0 and D0 are the same physical pin (0), which is also PLATE_A_PIN above -
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

uint32_t wakeCount = 0; // counts actual advertises, not every measure/check wake
uint32_t lastAdvertiseMs = 0;
int lastAdvertisedMoisture = -1;

void setup()
{
  Serial.begin(115200);

  setupStatusLeds();
  ledsOff();
  pinMode(EXTERNAL_LED_PIN, OUTPUT);
  externalLedWrite(false);
  pinMode(CHG_STATUS_PIN, INPUT_PULLUP);
  powerDownExternalFlash();

  // One-time known-good PIN_CNF for both sensing plates: input buffer connected, no
  // pull, standard drive, sense disabled (PIN_CNF=0 also leaves DIR=input). countOscCycles()
  // only touches DIR/OUT via DIRSET/DIRCLR/OUTSET/OUTCLR after this -- those don't touch
  // the input-buffer-connect bit, so this has to be set correctly once up front or IN
  // reads could be unreliable. Ported from MSV2_Moisture_Probe_Test.ino.
  NRF_P0->PIN_CNF[2] = 0;
  NRF_P0->PIN_CNF[28] = 0;

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
  int moisture = readAveragedMoisture();
  uint8_t batteryPercent = readSmoothedBatteryPercent();

  bool intervalElapsed = lastAdvertiseMs == 0 || (millis() - lastAdvertiseMs >= ADVERTISE_INTERVAL_MS);
  bool minSwingIntervalElapsed = lastAdvertiseMs == 0 || (millis() - lastAdvertiseMs >= MIN_SWING_ADVERTISE_INTERVAL_MS);
  bool bigSwing = lastAdvertisedMoisture >= 0 && abs(moisture - lastAdvertisedMoisture) >= MOISTURE_SWING_THRESHOLD && minSwingIntervalElapsed;
  bool shouldAdvertise = intervalElapsed || bigSwing;

  if (shouldAdvertise)
  {
    wakeCount++;

    if (ENABLE_SERIAL_LOGS)
    {
      Serial.print("Sensor ");
      Serial.print(SENSOR_ID);
      Serial.print(" wake ");
      Serial.print(wakeCount);
      Serial.print(bigSwing ? " (swing) advertising for " : " advertising for ");
      Serial.print(ADVERTISE_MS);
      Serial.print(" ms moisture=");
      Serial.print(moisture);
      Serial.print(" battery=");
      Serial.print(batteryPercent);
      Serial.println("%");
      Serial.flush();
    }

    startAdv(moisture, batteryPercent);
    lowPowerDelay(ADVERTISE_MS);
    Bluefruit.Advertising.stop();

    // System OFF entry point commented out along with USE_SYSTEM_OFF_AFTER_SEND above.
    // enterSystemOff();

    lastAdvertiseMs = millis();
    lastAdvertisedMoisture = moisture;
  }
  else if (ENABLE_SERIAL_LOGS)
  {
    Serial.print("Sensor ");
    Serial.print(SENSOR_ID);
    Serial.print(" checked, moisture=");
    Serial.print(moisture);
    Serial.println(" - no advertise this wake");
    Serial.flush();
  }

  if (moisture <= LOW_MOISTURE_THRESHOLD)
  {
    blinkExternalLed(3, LED_PULSE_MS, LED_PULSE_MS); // alert: moisture is low
  }

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

// Free-running relaxation oscillator, alternating which plate drives and which reads
// every single cycle (see the PLATE_A_PIN/PLATE_B_PIN comment above for why): charge
// the driven plate HIGH while actively holding the other plate LOW as a hard reference,
// hold it briefly to let it actually charge, release the driven plate to INPUT and time
// its own RC decay back to LOW through its own 10k pulldown, immediately swap roles,
// repeat for windowMs. Uses direct NRF_P0 register access instead of pinMode()/
// digitalWrite()/digitalRead() - ported from MSV2_Moisture_Probe_Test.ino after several
// bench-verified fixes there (see the topology comment above for the full history).
// Returns the total number of complete HIGH->LOW discharge cycles observed across both
// plates - with RA==RB by design, A-driven and B-driven cycles should read near-
// identically, so summing them is just summing more independent samples of the same
// underlying capacitance.
bool driveIsPlateA = true;

uint32_t countOscCycles(uint32_t windowMs)
{
  uint32_t windowStartMs = millis();
  uint32_t cycles = 0;

  while (millis() - windowStartMs < windowMs)
  {
    uint32_t driveBit = driveIsPlateA ? PLATE_A_BIT : PLATE_B_BIT;
    uint32_t senseBit = driveIsPlateA ? PLATE_B_BIT : PLATE_A_BIT;

    NRF_P0->DIRSET = senseBit; // plate not driven this cycle -> output
    NRF_P0->OUTCLR = senseBit; // ...and held LOW as a hard reference

    NRF_P0->DIRSET = driveBit; // driven plate -> output
    NRF_P0->OUTSET = driveBit; // ...charge HIGH

    // Charge-settling delay -- see CHARGE_SETTLE_NOPS note above. Unrolled, not a
    // for-loop: a loop's own increment/compare/branch overhead would swamp a target
    // this short and silently make the realized delay longer than intended.
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();

    NRF_P0->DIRCLR = driveBit; // release driven plate to INPUT (float, PIN_CNF input buffer already connected from setup())

    uint32_t waitStartUs = micros();
    while (NRF_P0->IN & driveBit)
    {
      if (micros() - waitStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        break; // disconnected/open probe - bail this half-cycle
      }
    }

    cycles++;
    driveIsPlateA = !driveIsPlateA; // swap which plate drives next cycle
  }

  return cycles;
}

// In-place insertion sort + middle element. Fine for the small, fixed sample counts
// used here (no need for anything fancier than O(n^2)).
uint32_t medianOf(uint32_t vals[], uint8_t n)
{
  for (uint8_t i = 1; i < n; i++)
  {
    uint32_t key = vals[i];
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

// Takes OSC_MEDIAN_SAMPLES independent OSC_SAMPLE_WINDOW_MS bursts and returns the
// median cycle count, scaled to a per-second rate (kHz, since burst windows are ms).
// At the validated 1ms window this numerically equals frequency in kHz directly, but
// the division is spelled out so it stays correct if the window is ever retuned.
// Lower value = wetter - the oscillator slows down as sensed capacitance rises.
uint16_t readAveragedMoisture()
{
  uint32_t samples[OSC_MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < OSC_MEDIAN_SAMPLES; i++)
  {
    samples[i] = countOscCycles(OSC_SAMPLE_WINDOW_MS);
  }

  uint32_t medianCycles = medianOf(samples, OSC_MEDIAN_SAMPLES);
  uint16_t freqKHz = (uint16_t)(medianCycles * 1000UL / OSC_SAMPLE_WINDOW_MS / 1000UL);

  // The oscillator's raw frequency runs the "wrong" way for a value called moisture -
  // drier soil oscillates faster, so raw frequency goes UP as things dry out. Invert
  // it here (ceiling - frequency) so the transmitted value reads the way the name
  // implies (higher = wetter), matching every consumer downstream (dry/wet calibration,
  // charts) and the original pre-oscillator convention. MOISTURE_INVERT_CEILING just
  // needs to comfortably exceed the highest frequency this sensor will ever report -
  // clamped to 0 rather than underflowing if it doesn't.
  return freqKHz >= MOISTURE_INVERT_CEILING ? 0 : (MOISTURE_INVERT_CEILING - freqKHz);
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

// True while the BQ25100 is actively sourcing charge current into the cell.
bool isBatteryCharging(void)
{
  return digitalRead(CHG_STATUS_PIN) == LOW;
}

// Exponential moving average of the battery percent, carried across wakes, to smooth
// out per-sample ADC noise on the History chart's charging curve - same 0.2 smoothing
// factor already validated for the moisture oscillator's frequency in
// MSV2_Moisture_Probe_Test.ino. This only smooths the transmitted/charted value, not
// the sampling rate - still one real ADC read per wake.
float smoothedBatteryPercent = -1.0f;
bool lastChargingState = false;

uint8_t readSmoothedBatteryPercent()
{
  bool charging = isBatteryCharging();
  float raw = batteryMvToPercent(readBatteryMv(), charging);

  // A charge/discharge transition is a real, instantaneous jump in what the corrected
  // curve reads (the IR-drop offset direction flips), not sensor noise - blending it
  // through the normal EMA would just smear the jump across several wakes instead of
  // reflecting it, and would mix samples from two different curves together. Snap
  // straight to the new curve's reading on the wake where the state actually changes,
  // then resume normal smoothing within that state.
  if (smoothedBatteryPercent < 0 || charging != lastChargingState)
  {
    smoothedBatteryPercent = raw;
  }
  else
  {
    smoothedBatteryPercent += 0.2f * (raw - smoothedBatteryPercent);
  }

  lastChargingState = charging;
  return (uint8_t)(smoothedBatteryPercent + 0.5f);
}

// Generic 1S LiPo open-circuit-voltage -> percent curve (a commonly cited hobbyist
// reference shape, e.g. https://blog.ampow.com/lipo-voltage-chart/ - NOT derived from
// this specific 150mAh cell's own discharge data, just a far better-shaped approximation
// than a straight line). Real LiPo terminal voltage stays close to full for a large
// fraction of actual usable capacity, then falls off a cliff near empty - the old
// 2-segment linear-in-mV formula this replaced couldn't represent that shape, which is
// exactly why the reported percent used to sit near 100% for ~12 hours then crater over
// the next 6-12 (see 2026-07-25 chat log): it was reporting something closer to
// "voltage relative to full" than "actual remaining charge relative to full," and those
// two are very different curves for Li-ion chemistry. Piecewise-linear interpolation
// between real reference points tracks true remaining capacity much better than any
// single-slope formula can, without needing a coulomb counter or dedicated fuel-gauge IC.
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

// See CHARGE_TAPER_START_MV/CHARGE_TAPER_END_MV above for why this fades out instead of
// applying at a constant strength right up until charging stops.
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

// While charging, the measured terminal voltage runs high of true rest voltage by
// roughly the charge current's IR drop through the cell/path resistance - see
// CHARGING_IR_COMPENSATION_MV above. Correcting the input before the same curve
// approximates a separate charge-state curve without needing one calibrated from
// scratch.
uint8_t batteryMvToPercent(float mvolts, bool isCharging)
{
  float restEquivalentMv = isCharging ? mvolts - chargeCompensationMv(mvolts) : mvolts;
  return (uint8_t)(interpolateBatteryPercent(restEquivalentMv) + 0.5f);
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

// This used to hand-roll a while(millis()-start<durationMs) waitForEvent() loop, which
// looked like a sleep but wasn't one: waitForEvent() (-> sd_app_evt_wait() with the
// softdevice enabled, which this sketch always has) has no way to tell the scheduler
// how long it intends to wait, so the FreeRTOS port's regular ~0.977ms (1024Hz) RTC
// tick interrupt keeps firing on its normal schedule the whole time, waking the WFE
// almost immediately on every single tick - this specific "1ms spin-loop power waste"
// failure mode is named and fixed in this exact core's own
// port_cmsis_systick.c/vPortSuppressTicksAndSleep() (see
// https://devzone.nordicsemi.com/f/nordic-q-a/63828/vtaskdelay-on-nrf52-freertos-port-wastes-cpu-power),
// but that fix only runs for a task that actually blocks via vTaskDelay() - it never
// engages for a raw waitForEvent() call sitting in an ordinary while loop. Plain
// delay() calls vTaskDelay() under the hood, which lets the idle task disable that
// periodic tick and sleep once for the whole requested duration instead of waking
// ~1024 times a second - almost certainly the dominant power cost, not the moisture/
// battery sensing or BLE advertising duty cycle (see 2026-07-25 chat log for the
// budget math that didn't close until this was found). Unconfirmed with a real current
// probe yet - that's Monday's bench test - but this is a source-level bug, not a guess.
void lowPowerDelay(uint32_t durationMs)
{
  delay(durationMs);
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

void blinkExternalLed(uint8_t times, uint32_t onMs, uint32_t offMs)
{
  if (!ENABLE_EXTERNAL_LED || times == 0 || onMs == 0)
  {
    return;
  }

  for (uint8_t i = 0; i < times; i++)
  {
    externalLedWrite(true);
    delay(onMs);
    externalLedWrite(false);
    if (i + 1 < times)
    {
      delay(offMs);
    }
  }
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
