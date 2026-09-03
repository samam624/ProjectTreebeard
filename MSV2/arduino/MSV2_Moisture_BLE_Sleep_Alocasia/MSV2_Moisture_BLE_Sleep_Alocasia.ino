/*********************************************************************
  Periodic BLE advertising sensor test for Adafruit nRF52 Bluefruit.

  This uses a timed low-power System ON sleep pattern. It is suitable for
  periodic wakeups. The deepest nRF52 System OFF sleep generally cannot wake
  itself from a normal internal timer; it wakes from reset, GPIO, NFC, or
  similar external events.
*********************************************************************/
// 2026-07-31: recovered from git history (commit 50f200f, the last version of
// MSV2_Moisture_BLE_Sleep.ino before that shared file was overwritten same-day in
// 45578e5 to become Scindapsus-specific dual-plate/10k firmware). Alocasia is still
// the original single-plate/100k hardware - the dual-plate file's PLATE_B_PIN (D2)
// drive would be driving straight into Alocasia's hardwired ground-referenced second
// comb, so it must NOT be flashed there as-is (see that file's own SENSOR_ID comment).
// This file is Alocasia's own maintained copy going forward. Two changes made here
// vs. the recovered version, both ported over from the Scindapsus quantization fix
// investigation (see MSV2_Moisture_BLE_Sleep.ino's OSC_SAMPLE_WINDOW_MS comment for
// the full root-cause writeup): the RTC1/LFCLK ~0.977ms tick-granularity effect this
// fixes is a chip-level property of the nRF52840's millis()/micros() (shared by every
// sketch on this core), not specific to the dual-plate topology, so the same fix
// should generalize here - confirmed post-flash via direct serial monitoring (burst
// duration consistency), same method used on Scindapsus, rather than re-deriving it
// from scratch on this hardware.
#include "Adafruit_SPIFlash.h"
#include <bluefruit.h>

const char DEVICE_NAME[] = "MSV2-MOISTURE-001";
const uint16_t MSV2_COMPANY_ID = 0xFFFF; // Test/development company id.
const uint8_t SENSOR_ID = 2; // This is the Alocasia probe (COM11). Confirmed 2026-07-27 against
                              // live dashboard data: sensor 1 = Scindapsus, sensor 2 = Alocasia.

// 2026-08-09: back down to 10s measure/check cadence (was 1,800,000 / 30min for the
// vacation run) now that the QSPI power-down bug is fixed - see powerDownExternalFlash()
// below and msv2_probe_battery_drain_hypotheses memory. This reactivates the
// bigSwing/MIN_SWING_ADVERTISE_INTERVAL_MS logic below, which was dead weight at 30min
// (ADVERTISE_INTERVAL_MS/60s was always already elapsed by the next wake, so every wake
// advertised regardless). Doubles as a quick overnight battery test: at this cadence the
// active-energy budget dominates enough that if the QSPI fix weren't actually working,
// the ~1.5mA leak would show up as a large, obvious overnight battery drop (~8-12% over
// 8-10h) vs a healthy ~1% or less if it's genuinely fixed.
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
// MOISTURE_SWING_THRESHOLD on every single WAKE_PERIOD_MS measurement, forcing an
// advertise every wake instead of the normal 60s indefinitely. That's a jump in BLE
// traffic, which was enough to keep re-triggering the receiver's scheduleCloudSyncSoon()
// faster than its own blocking cloud-sync HTTP call (capped at 8s) could clear, leaving the
// receiver's shared render/WebServer loop blocked roughly half the time for as long as the
// probe stayed noisy - the "display freezes for long periods" symptom traced back to this.
// A real watering/removal event is a one-time step change, not continuous noise, so it
// doesn't need to fire more often than this floor to still be caught quickly.
const uint32_t MIN_SWING_ADVERTISE_INTERVAL_MS = 30000;
const uint32_t ADVERTISE_MS = 500;
// 2026-08-09: bounded wait for a host to attach before boot-time diagnostics print (see
// logFlashDiagnostic() and scripts/serial_capture.py) - native USB CDC here doesn't buffer
// prints for a not-yet-attached host, so a reconnecting capture script started right after
// an upload needs the board to hold briefly rather than racing it blind. Bounded, not
// indefinite, so a real power-on with nothing attached still boots normally. One-time cost
// at boot only (setup() runs once per power cycle, not per wake), so harmless to leave in.
const uint32_t BOOT_HOST_WAIT_MS = 5000;
const uint32_t LED_PULSE_MS = 80;
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = false; // onboard RGB disabled for the battery-impact test - see EXTERNAL_LED_PIN below
const bool ENABLE_SERIAL_LOGS = false; // disabled for the unattended 10-day vacation run starting 2026-07-31 - flip back to true to debug on the bench

// External LED on D1: no longer a per-wake/per-advertise heartbeat (that cost battery
// for no real benefit) - now purely a low-moisture alert, blinking 3x every wake while
// moisture is low. LOW_MOISTURE_THRESHOLD is a FIXED cutoff, not the live auto-learned
// or per-plant manual range configured on the receiver - this probe is advertise-only
// (no BLE receive path), so it has no way to learn either of those without a real
// architecture change. Matches the same 25%-of-default-range "danger" cutoff already
// used everywhere else in this project (Treebeard_ESP32_Display_Runtime.ino's healthColor()),
// off the default dry=120/wet=175 calibration scale: 120 + 0.25*(175-120) = 134.
const int EXTERNAL_LED_PIN = D1;
const bool EXTERNAL_LED_ACTIVE_HIGH = true;
const bool ENABLE_EXTERNAL_LED = true;
const int LOW_MOISTURE_THRESHOLD = 134;

// Moisture sensing node: free-running RC relaxation oscillator, not one-shot timing.
// The earlier precharge/discharge-timing approach (see git history) read cleanly on
// the bench but saturated once actually inserted in soil - no resistor value made a
// single-shot timed discharge survive real, variable soil conductivity. Replaced with
// the architecture validated in MSV2_Moisture_Probe_Test.ino and
// docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md: charge until HIGH, immediately discharge
// until LOW, immediately recharge, repeat continuously, and count cycles in a fixed
// window - the same free-running-astable-plus-frequency-counting principle as this
// project's own MSV1 555-timer proof-of-concept. Requires a 100k pulldown resistor
// populated on this node (the earlier unpopulated/DNC footprint was part of why the
// old approach never stabilized).
const int SENSE_PIN = D0;
// 2026-07-31: was 1ms, widened to 5ms - same RTC-tick-quantization fix as
// MSV2_Moisture_BLE_Sleep.ino's OSC_SAMPLE_WINDOW_MS (see the top-of-file note and
// that file's own comment for the full root-cause writeup). millis()/micros() on this
// chip come from RTC1 running off the raw 32.768kHz LFCLK, ~0.977ms/tick - at a 1ms
// window that's ~3% of the whole window landing unpredictably relative to the next
// tick edge, the same mechanism regardless of which frequency range the oscillator
// itself runs at (it's a ratio of tick-to-window, not tied to the sensed signal).
// 5ms trades that down to ~0.6%, for 25ms total active time across the 5-sample
// median instead of 5ms - still well under the original pre-optimization 50ms design.
const uint32_t OSC_SAMPLE_WINDOW_MS = 5;
const uint8_t OSC_MEDIAN_SAMPLES = 5; // 5 x OSC_SAMPLE_WINDOW_MS bursts, median-of-5 rejects scheduler-jitter outliers
// Safety cap per half-cycle so a disconnected/open probe can't hang a burst.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL;
// Generous headroom above the ~10-90kHz range observed so far - see the inversion
// comment in readAveragedMoisture() below.
const uint16_t MOISTURE_INVERT_CEILING = 200;

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

// Unused for now (timed wake/send/sleep only) - commented out rather than deleted since
// true System OFF deep sleep is real future work. Re-enabling needs WAKE_PIN moved off
// A0 first (see note below on why).
//
// #define USE_SYSTEM_OFF_AFTER_SEND 1
//
// // In System OFF mode, connect WAKE_PIN to GND to wake/reset the nRF52840.
// // Change this to a pin driven by an external RTC/timer/sensor interrupt for
// // periodic true-deep-sleep wakeups.
// // NOTE: A0 and D0 are the same physical pin (0), which is also SENSE_PIN above -
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
  uint32_t hostWaitStart = millis();
  while (!Serial && millis() - hostWaitStart < BOOT_HOST_WAIT_MS)
  {
    delay(10);
  }
  // `Serial` reads true almost immediately post-DFU-reset on this board (the USB CDC
  // interface looks "up" before a specific terminal has actually attached/opened it), so
  // the loop above exits far earlier than a real capture script can reconnect - confirmed
  // empirically 2026-08-09 on the Scindapsus sketch (missed the first two boot prints,
  // caught ones ~200-300ms later). This flat extra delay is the actual margin; the loop
  // above is left in as a harmless best-effort head start.
  delay(1500);

  setupStatusLeds();
  ledsOff();
  pinMode(EXTERNAL_LED_PIN, OUTPUT);
  externalLedWrite(false);
  pinMode(CHG_STATUS_PIN, INPUT_PULLUP);
  logFlashDiagnostic();
  powerDownExternalFlash();

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

// Free-running relaxation oscillator: charge until HIGH, immediately discharge until
// LOW, immediately recharge, repeat, for windowMs. Returns the number of complete
// HIGH->LOW discharge cycles observed - ported near-verbatim from
// MSV2_Moisture_Probe_Test.ino's countOscCycles(), which has no BLE dependency.
uint32_t countOscCycles(uint32_t windowMs)
{
  uint32_t windowStartMs = millis();
  uint32_t windowStartUs = micros(); // diagnostic only, see the burst-duration log below
  uint32_t cycles = 0;

  while (millis() - windowStartMs < windowMs)
  {
    pinMode(SENSE_PIN, OUTPUT);
    digitalWrite(SENSE_PIN, HIGH);

    // Wait for the plate to actually reach a real HIGH before releasing it to be timed
    // on discharge - this was supposed to already be ported from Scindapsus's
    // countOscCycles() poll-loop fix (2026-07-28, see the top-of-file note) but was
    // missing here. That fix replaced a fixed-delay charge guess after it was
    // root-caused as the leading suspect for a "locks onto a wrong value for hours,
    // then jumps" quantization artifact. Alocasia's 100k pulldown gives a 10x longer RC
    // time constant than Scindapsus's 10k, so under-charging here is if anything a
    // bigger risk, not a smaller one.
    uint32_t chargeStartUs = micros();
    bool chargeTimedOut = false;
    while (digitalRead(SENSE_PIN) == LOW)
    {
      if (micros() - chargeStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        chargeTimedOut = true;
        break; // never reached a clean HIGH (shorted/disconnected plate) - bail this half-cycle
      }
    }

    pinMode(SENSE_PIN, INPUT);
    uint32_t waitStartUs = micros();
    bool dischargeTimedOut = false;
    while (digitalRead(SENSE_PIN) == HIGH)
    {
      if (micros() - waitStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        dischargeTimedOut = true;
        break; // disconnected/open probe - bail this half-cycle
      }
    }

    // Only count a cycle that actually completed both halves inside the timeout budget.
    // A timed-out half is the "shorted/disconnected plate"/"disconnected/open probe" fault
    // the comments above already call out - previously it still got counted as a real
    // cycle, which for a fully open sense line (broken pulldown, cracked pad - the same
    // field-failure class as MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md)
    // deterministically produced exactly one fake "cycle" per averaging burst and read as
    // the LOWEST possible oscillator frequency - which readAveragedMoisture() below inverts
    // into the HIGHEST possible moisture reading, silently defeating LOW_MOISTURE_THRESHOLD
    // for a sensor that isn't measuring anything at all.
    if (!chargeTimedOut && !dischargeTimedOut)
    {
      cycles++;
    }
  }

  // 2026-07-31 diagnostic, ported from the Scindapsus quantization-fix investigation:
  // confirms burst durations land close to OSC_SAMPLE_WINDOW_MS consistently rather than
  // snapping between RTC-tick-quantized durations. Safe to remove once confirmed.
  if (ENABLE_SERIAL_LOGS)
  {
    uint32_t burstUs = micros() - windowStartUs;
    Serial.print("  [osc] burstUs=");
    Serial.print(burstUs);
    Serial.print(" cycles=");
    Serial.println(cycles);
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
// The division is spelled out (rather than assuming windowMs=1 makes it a no-op) so this
// stays correct as the window gets retuned - see the OSC_SAMPLE_WINDOW_MS comment above.
// Lower value = wetter - the oscillator slows down as sensed capacitance rises.
uint16_t readAveragedMoisture()
{
  uint32_t samples[OSC_MEDIAN_SAMPLES];
  uint8_t validCount = 0;
  for (uint8_t i = 0; i < OSC_MEDIAN_SAMPLES; i++)
  {
    // countOscCycles() now only counts a cycle that completed both halves within budget
    // (see its own comment), so 0 here means this whole burst never saw one - exclude it
    // from the median instead of letting a run of these fault samples drag a mix of
    // real+fault bursts down to a false "very low frequency" (i.e. false "very wet").
    uint32_t cycles = countOscCycles(OSC_SAMPLE_WINDOW_MS);
    if (cycles > 0)
    {
      samples[validCount++] = cycles;
    }
  }

  if (validCount == 0)
  {
    // Every burst in this averaging pass failed to complete even one real charge/discharge
    // cycle - a genuinely working sensor at any realistic soil capacitance oscillates far
    // faster than OSC_CYCLE_TIMEOUT_US, so this means the sense line itself is shorted or
    // open, not "maximally wet". Report as maximally DRY instead of running 0 cycles through
    // the ceiling-inversion below (which would otherwise compute the ceiling itself, i.e.
    // the false "fully saturated" reading this fix exists to prevent) - moisture=0 is <=
    // LOW_MOISTURE_THRESHOLD, so the existing low-moisture LED/cloud alert path fires and
    // draws attention to the probe instead of silently looking healthy forever.
    return 0;
  }

  uint32_t medianCycles = medianOf(samples, validCount);
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
// A candidate charging-state flip awaiting confirmation - see the debounce comment
// inside readSmoothedBatteryPercent() below.
bool pendingChargingState = false;
bool chargingStatePending = false;

uint8_t readSmoothedBatteryPercent()
{
  bool measuredCharging = isBatteryCharging();
  bool charging;

  // isBatteryCharging() is a single un-debounced digitalRead() of the BQ25100 ~CHG pin.
  // A misread there (noise, or the genuinely uncertain signal right at end-of-charge
  // termination) used to be indistinguishable from a real transition below, which would
  // discard all prior smoothing and jump straight to one fresh sample read under
  // whatever might be the WRONG IR-compensation curve for the true battery state -
  // producing a visible spurious jump on the History chart. Require the same new state
  // to be observed on a second consecutive wake before treating it as real; a one-off
  // disagreement is held as a candidate and this wake still uses the last confirmed
  // state's curve, mirroring the moisture oscillator's watering-rise debounce.
  if (smoothedBatteryPercent < 0 || measuredCharging == lastChargingState)
  {
    charging = measuredCharging;
    chargingStatePending = false;
  }
  else if (chargingStatePending && pendingChargingState == measuredCharging)
  {
    charging = measuredCharging; // confirmed on a second consecutive wake
  }
  else
  {
    pendingChargingState = measuredCharging;
    chargingStatePending = true;
    charging = lastChargingState;
  }

  float raw = batteryMvToPercent(readBatteryMv(), charging);

  // A confirmed charge/discharge transition is a real, instantaneous jump in what the
  // corrected curve reads (the IR-drop offset direction flips), not sensor noise -
  // blending it through the normal EMA would just smear the jump across several wakes
  // instead of reflecting it, and would mix samples from two different curves together.
  // Snap straight to the new curve's reading on the wake where the state actually
  // changes, then resume normal smoothing within that state.
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

// 2026-08-09: temporary bench-check for the QSPI power-down bug below - prints the flash
// chip's raw JEDEC ID and whether Adafruit_SPIFlash's device table recognizes it. Expect
// "recognized=0": the XIAO nRF52840's onboard chip is a Puya P25Q16H, which is not in this
// library's possible_devices[] list (GD25Q/S25FL/W25Q/AT25/MB85RS parts only). Safe to
// delete once confirmed - runs once at boot, before advertising starts, so it doesn't affect
// steady-state sleep current either way.
void logFlashDiagnostic(void)
{
#if HAS_EXTERNAL_FLASH
  uint8_t jedecIds[4] = {0, 0, 0, 0};
  flashTransport.begin();
  flashTransport.readCommand(SFLASH_CMD_READ_JEDEC_ID, jedecIds, 4);
  flashTransport.end();

  bool recognized = flash.begin();
  if (recognized)
  {
    flash.end();
  }

  Serial.print("[flash diag] raw JEDEC ID: 0x");
  for (uint8_t i = 0; i < 3; i++)
  {
    if (jedecIds[i] < 0x10)
    {
      Serial.print('0');
    }
    Serial.print(jedecIds[i], HEX);
  }
  Serial.print("  Adafruit_SPIFlash recognized=");
  Serial.println(recognized ? 1 : 0);
  Serial.flush();
#endif
}

// 2026-08-09: previously gated on flash.begin(), which fails on this board - see
// logFlashDiagnostic() above. Adafruit_SPIFlashBase::begin() calls _trans->begin() (enabling
// the QSPI peripheral) BEFORE checking its device table, then returns false with no matching
// _trans->end() when the chip isn't recognized - so the old `if (!flash.begin()) return;`
// guard meant the flash chip never got the deep-power-down command below AND the QSPI
// peripheral was left enabled for the rest of the probe's life, every single boot. Likely
// the dominant contributor to the ~1.5mA "sleep" current inferred from the 2026-08 vacation
// battery data (150mAh cell drained in ~4-6 days instead of the expected 100s of days).
// runCommand(0xB9) is a raw opcode that doesn't need device-table recognition, so talk to
// flashTransport directly instead of routing through flash.begin()'s detection path.
void powerDownExternalFlash(void)
{
#if HAS_EXTERNAL_FLASH
  flashTransport.begin();
  flashTransport.runCommand(0xB9); // SPI/QSPI deep power-down command.
  delay(10);
  flashTransport.end(); // nrfx_qspi_uninit() - disables the QSPI peripheral.
#endif
}

// enterSystemOff/connect_callback/disconnect_callback commented out along with their
// call sites above (USE_SYSTEM_OFF_AFTER_SEND, Periph connect callbacks) - unused for
// now. Needs "nrf_gpio.h" re-included to bring back.
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
