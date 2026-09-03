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
// If the raw cycle count moves at least this much between checks (e.g. a watering event,
// or the probe being pulled out), advertise immediately instead of waiting for the next
// scheduled interval - matches PRODUCT_ROADMAP.md's "if moisture changed meaningfully"
// condition. This is now measured on the RAW, uninverted cycle count (see the 2026-07-28
// raw-values rework below) rather than the old calibrated 120-175 scale - rescaled to the
// same ~27% proportion of the bench-observed 220-360kHz raw range (~140 units wide) that
// the old threshold (15 of ~55 calibrated units) represented. Still just a magnitude
// comparison between two raw readings, not an interpretation of what the value means, so
// it stays on the probe under the same "policy, not calibration" logic as the advertise
// cadence itself.
const int MOISTURE_SWING_THRESHOLD = 38;
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
const uint8_t XIAO_LED_ON = LOW;
const uint8_t XIAO_LED_OFF = HIGH;
const bool ENABLE_STATUS_LEDS = false; // onboard RGB disabled for tonight's battery-impact test - see EXTERNAL_LED_PIN below
const bool ENABLE_SERIAL_LOGS = false; // disabled for the unattended 10-day vacation run starting 2026-07-31 - flip back to true to debug on the bench
// 2026-08-09: bounded wait for a host to attach before boot-time diagnostics print (see
// logFlashDiagnostic() and scripts/serial_capture.py) - native USB CDC here doesn't buffer
// prints for a not-yet-attached host, so a reconnecting capture script started right after
// an upload needs the board to hold briefly rather than racing it blind. Bounded, not
// indefinite, so a real power-on with nothing attached still boots normally. One-time cost
// at boot only (setup() runs once per power cycle, not per wake), so harmless to leave in.
const uint32_t BOOT_HOST_WAIT_MS = 5000;

// External LED on D1: previously blinked 3x per wake as a low-moisture alert, gated on a
// fixed raw-value cutoff (LOW_MOISTURE_THRESHOLD). Removed in the 2026-07-28 raw-values
// rework: deciding "is this reading low" requires a calibrated dry/wet range, which is
// exactly the kind of interpretation now deliberately kept off the probe (see the
// countOscCycles()/readRawMoistureKHz() comments below) - and this probe is
// advertise-only (no BLE receive path) so it never had access to the real per-plant range
// anyway, only ever a hardcoded guess. If a physical low-moisture indicator is wanted
// again, it belongs on the receiver, which already has per-card danger-color logic with
// the real calibrated range.
const int EXTERNAL_LED_PIN = D1;
const bool EXTERNAL_LED_ACTIVE_HIGH = true;
const bool ENABLE_EXTERNAL_LED = true;

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
//   - 2026-07-28: the charge phase was originally a fixed 40-NOP blind delay between
//     "drive HIGH" and "release to read," sized empirically against one bench load
//     condition. That was the wrong shape of fix: it doesn't adapt to the load actually
//     being sensed (moisture IS the load), so it silently undershoots outside whatever
//     condition it was tuned against - already caught once as WET-END-SPECIFIC saturation
//     under heavy contact, and the leading suspect for the "locks onto a wrong value for
//     hours, then jumps" behavior diagnosed the same day (a fixed margin sitting right at
//     a threshold that ambient temperature/voltage can tip either side of - a real analog
//     regime switch, not gradual drift). Replaced with a poll loop - wait for the driven
//     plate to actually read HIGH, capped by the same OSC_CYCLE_TIMEOUT_US used on the
//     discharge side - symmetric with the (already-correct) discharge wait below, and the
//     same self-timing principle the reference 555-timer design already relies on: let the
//     real RC behavior decide when to release, not a guessed constant. This also only ever
//     holds the plate driven HIGH for as long as actually needed instead of always paying a
//     fixed worst-case duration, which matters because that hold time is a direct resistive
//     bleed path (VDD -> GPIO driver -> plate -> its own 10k pulldown -> GND) - shortening
//     it to the true minimum is a real, if unquantified without a current probe, battery win
//     on top of the correctness fix.
const int PLATE_A_PIN = D0; // IO0 on the XIAO silkscreen -- one comb of the sensor.
const int PLATE_B_PIN = D2; // IO2 on the XIAO silkscreen -- the other comb.
// Per Seeed_XIAO_nRF52840_Sense's own variant.cpp pin map: D0/A0 (PLATE_A) = P0.02,
// D2/A2 (PLATE_B) = P0.28 -- both on NRF_P0, so no port-1 handling needed here.
#define PLATE_A_BIT (1UL << 2)  // P0.02
#define PLATE_B_BIT (1UL << 28) // P0.28
// 2026-07-30: was 1ms. Widened after the receiver-side history showed the raw reading
// snapping between a fixed comb of levels ~11-12 units apart, at every moisture level from
// bone-dry to saturated, most of it uncorrelated with battery/handling (see the receiver
// dashboard investigation this same day) - the fingerprint of a fixed *time* quantum, not a
// physical effect. The outer burst boundary below (`while (millis() - windowStartMs <
// windowMs)`) is gated on millis()/micros(), which on this chip come from RTC1 running off
// the raw 32.768kHz LFCLK - the same clock lowPowerDelay()'s comment above already pins at
// ~1024Hz/~0.977ms per hardware tick. At a 1ms nominal window, that ~30us tick granularity is
// ~3% of the entire window: wherever windowStartMs happens to land relative to the next tick
// edge (set by incidental timing of Serial prints/ADC delays elsewhere in the wake cycle, not
// moisture) determines how much of that ~30us gets won or lost, and all 5 median samples
// inherit the same offset since each burst starts right where the previous one exited - hence
// rock-stable within one reading, then a clean jump to a different stable level on the next
// wake. At the ~208kHz raw range this probe runs in, one ~30us quantum is ~6 cycles; landing
// on either side of a tick boundary is ~12 - matches the observed step almost exactly.
// docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md already flagged windows under ~10ms as producing
// timing artifacts (attributed there to rare scheduler jitter); 1ms sat 10x inside that zone.
// 5ms trades the tick-phase uncertainty down to ~0.6% of the window (~2-unit step instead of
// ~11-12), for 25ms total active time across the 5-sample median instead of 5ms - still well
// under the original pre-optimization 50ms design this replaced. readRawMoistureKHz()'s kHz
// conversion already generalizes to any window size, so this is the only constant that needs
// to move. Re-verify against the dashboard after flashing: the histogram of raw readings
// should go from a comb with real gaps to a much finer-grained, near-continuous spread.
const uint32_t OSC_SAMPLE_WINDOW_MS = 5;
const uint8_t OSC_MEDIAN_SAMPLES = 5; // 5 x OSC_SAMPLE_WINDOW_MS bursts, median-of-5 rejects
                                       // scheduler-jitter outliers - this is noise rejection on
                                       // the raw signal (protecting against a single
                                       // scheduler-jitter-corrupted burst), not calibration, so
                                       // it stays on the probe under the 2026-07-28 raw-values
                                       // rework below.
// Safety cap per half-cycle so a disconnected/open probe can't hang a burst.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL;
// 2026-07-28: moisture inversion (raw kHz -> "higher = wetter") moved to the receiver -
// see Treebeard_ESP32_Display_Runtime.ino's moistureCeilingForSensor()/calibrateRawMoisture().
// The probe now transmits the raw median cycle count untouched. Reason: the ceiling this
// inversion needs is a per-hardware-revision calibration constant that keeps changing as
// the sensor design iterates (10-90kHz on the old single-plate/100k build, 220-360kHz on
// this dual-plate/10k rework) - tuning it used to require a firmware reflash of a
// physically-installed, hard-to-reach probe every time. Keeping the raw value on the wire
// also means the actual sensed signal is always visible for debugging, instead of only
// ever seeing it through whatever calibration transform happened to be baked into the
// probe firmware at the time - see the 2026-07-27/28 chat log for the diagnosis session
// this was blocking.

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
// Still read on the probe (only the probe has physical access to this pin), but 2026-07-28
// moved everything downstream of it - the charge-current IR-drop compensation curve, its
// taper, and the LiPo mV->percent lookup table - to the receiver. The probe now just reports
// the raw mV reading plus this one raw boolean and lets the receiver interpret both; see
// Treebeard_ESP32_Display_Runtime.ino's calibrateRawBattery(). Same rationale as the moisture
// ceiling above: those curves are tuned constants that keep getting revised (see the
// 2026-07-25 charge-jump chat log and the taper fix that followed it) and reflashing this
// probe to update them isn't worth it when the raw mV is all the receiver actually needs.
const int CHG_STATUS_PIN = 23; // D23/P0.17

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
  uint32_t hostWaitStart = millis();
  while (!Serial && millis() - hostWaitStart < BOOT_HOST_WAIT_MS)
  {
    delay(10);
  }
  // `Serial` reads true almost immediately post-DFU-reset on this board (the USB CDC
  // interface looks "up" before a specific terminal has actually attached/opened it), so
  // the loop above exits far earlier than a real capture script can reconnect - confirmed
  // empirically 2026-08-09 (missed the first two boot prints, caught ones ~200-300ms later).
  // This flat extra delay is the actual margin; the loop above is left in as a harmless
  // best-effort head start.
  delay(1500);
  Serial.println("[DIAG] serial begun");
  Serial.flush();

  setupStatusLeds();
  ledsOff();
  pinMode(EXTERNAL_LED_PIN, OUTPUT);
  externalLedWrite(false);
  pinMode(CHG_STATUS_PIN, INPUT_PULLUP);
  logFlashDiagnostic();
  powerDownExternalFlash();

  // One-time known-good PIN_CNF for both sensing plates: input buffer connected, no
  // pull, standard drive, sense disabled (PIN_CNF=0 also leaves DIR=input). countOscCycles()
  // only touches DIR/OUT via DIRSET/DIRCLR/OUTSET/OUTCLR after this -- those don't touch
  // the input-buffer-connect bit, so this has to be set correctly once up front or IN
  // reads could be unreliable. Ported from MSV2_Moisture_Probe_Test.ino.
  NRF_P0->PIN_CNF[2] = 0;
  NRF_P0->PIN_CNF[28] = 0;

  Serial.println("[DIAG] before Bluefruit.begin()");
  Serial.flush();
  Bluefruit.autoConnLed(false);
  Bluefruit.begin();
  Serial.println("[DIAG] after Bluefruit.begin()");
  Serial.flush();
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
  int moisture = readRawMoistureKHz();
  float batteryMv = readBatteryMv();
  bool charging = isBatteryCharging();

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
      Serial.print(" ms rawMoistureKHz=");
      Serial.print(moisture);
      Serial.print(" rawBatteryMv=");
      Serial.print(batteryMv);
      Serial.print(charging ? " (charging)" : "");
      Serial.println();
      Serial.flush();
    }

    startAdv(moisture, batteryMv, charging);
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
    Serial.print(" checked, rawMoistureKHz=");
    Serial.print(moisture);
    Serial.println(" - no advertise this wake");
    Serial.flush();
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
// wait for it to actually reach a real HIGH, release the driven plate to INPUT and time
// its own RC decay back to LOW through its own 10k pulldown, immediately swap roles,
// repeat for windowMs. Uses direct NRF_P0 register access instead of pinMode()/
// digitalWrite()/digitalRead() - ported from MSV2_Moisture_Probe_Test.ino after several
// bench-verified fixes there (see the topology comment above for the full history,
// including the 2026-07-28 switch from a fixed-NOP charge guess to this poll loop).
// Returns the total number of complete HIGH->LOW discharge cycles observed across both
// plates - with RA==RB by design, A-driven and B-driven cycles should read near-
// identically, so summing them is just summing more independent samples of the same
// underlying capacitance.
bool driveIsPlateA = true;

uint32_t countOscCycles(uint32_t windowMs)
{
  uint32_t windowStartMs = millis();
  uint32_t windowStartUs = micros(); // diagnostic only, see the burst-duration log below
  uint32_t cycles = 0;

  while (millis() - windowStartMs < windowMs)
  {
    uint32_t driveBit = driveIsPlateA ? PLATE_A_BIT : PLATE_B_BIT;
    uint32_t senseBit = driveIsPlateA ? PLATE_B_BIT : PLATE_A_BIT;

    NRF_P0->DIRSET = senseBit; // plate not driven this cycle -> output
    NRF_P0->OUTCLR = senseBit; // ...and held LOW as a hard reference

    NRF_P0->DIRSET = driveBit; // driven plate -> output
    NRF_P0->OUTSET = driveBit; // ...charge HIGH

    // Wait for the driven plate to actually reach a real HIGH before releasing it -
    // see the topology comment above for why this replaced a fixed-NOP guess. Same
    // shape as the discharge wait below, just polling the opposite level, and it also
    // minimizes how long the plate sits driven HIGH (a direct resistive bleed path
    // through its own 10k pulldown) to whatever the real load actually needs instead of
    // always paying a fixed worst-case duration.
    uint32_t chargeStartUs = micros();
    while (!(NRF_P0->IN & driveBit))
    {
      if (micros() - chargeStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        break; // never reached a clean HIGH (shorted/disconnected plate) - bail this half-cycle
      }
    }

    NRF_P0->DIRCLR = driveBit; // release driven plate to INPUT (float, PIN_CNF input buffer already connected from setup())

    uint32_t dischargeStartUs = micros();
    while (NRF_P0->IN & driveBit)
    {
      if (micros() - dischargeStartUs >= OSC_CYCLE_TIMEOUT_US)
      {
        break; // disconnected/open probe - bail this half-cycle
      }
    }

    cycles++;
    driveIsPlateA = !driveIsPlateA; // swap which plate drives next cycle
  }

  // 2026-07-30 diagnostic: confirms/refutes the OSC_SAMPLE_WINDOW_MS comment above. If the
  // burst-boundary tick-quantization theory is right, burstUs across many readings should
  // cluster into ~30us-spaced buckets (not vary smoothly), and cycles should visibly step
  // in lockstep with which bucket it landed in. Safe to remove once confirmed.
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
// stays correct as the window gets retuned - see the OSC_SAMPLE_WINDOW_MS comment above
// for the 2026-07-30 change from 1ms to 5ms.
// Returned RAW - no inversion, no calibration. Lower value = wetter (the oscillator
// slows down as sensed capacitance rises), but that's left for the receiver to interpret
// (see Treebeard_ESP32_Display_Runtime.ino's calibrateRawMoisture()) - see the 2026-07-28
// raw-values rework note above OSC_CYCLE_TIMEOUT_US.
uint16_t readRawMoistureKHz()
{
  uint32_t samples[OSC_MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < OSC_MEDIAN_SAMPLES; i++)
  {
    samples[i] = countOscCycles(OSC_SAMPLE_WINDOW_MS);
  }

  uint32_t medianCycles = medianOf(samples, OSC_MEDIAN_SAMPLES);
  return (uint16_t)(medianCycles * 1000UL / OSC_SAMPLE_WINDOW_MS / 1000UL);
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

// 2026-07-28: everything downstream of the raw mV reading - the LiPo mV->percent curve,
// the charge-current IR-drop compensation and its taper, and the smoothing across wakes -
// moved to the receiver (see Treebeard_ESP32_Display_Runtime.ino's calibrateRawBattery()). Same
// rationale as the moisture ceiling above: those are tuned constants that have already
// been revised twice (2026-07-24 divider cal, 2026-07-25 charge-jump taper fix) and each
// revision used to mean reflashing this physically-installed probe. The probe now just
// reports raw mV and the raw ~CHG state; the receiver derives percent, applies charge
// compensation, and smooths across wakes.

void startAdv(int rawMoistureKHz, float rawBatteryMv, bool isCharging)
{
  uint16_t batteryMvRounded = (uint16_t)(rawBatteryMv + 0.5f);
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
  manufacturerData[10] = (uint8_t)(rawMoistureKHz & 0xFF);
  manufacturerData[11] = (uint8_t)((rawMoistureKHz >> 8) & 0xFF);
  manufacturerData[12] = (uint8_t)(batteryMvRounded & 0xFF);
  manufacturerData[13] = (uint8_t)((batteryMvRounded >> 8) & 0xFF);
  manufacturerData[14] = isCharging ? 0x01 : 0x00;

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

#if HAS_EXTERNAL_FLASH
void printJedecHex(const uint8_t ids[3])
{
  for (uint8_t i = 0; i < 3; i++)
  {
    if (ids[i] < 0x10)
    {
      Serial.print('0');
    }
    Serial.print(ids[i], HEX);
  }
}
#endif

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
  printJedecHex(jedecIds);
  Serial.print("  Adafruit_SPIFlash recognized=");
  Serial.println(recognized ? 1 : 0);
  Serial.flush();

  // Wake/sleep ping: proves the chip actually answers when awake, rather than just assuming
  // a non-answer means "asleep as intended" - it could equally mean "QSPI can't talk to this
  // part at all" (a real, documented risk for this exact chip: zephyrproject-rtos/zephyr#55543
  // describes Deep-Power-Down EXIT breaking specifically over the nRF QSPI peripheral on a
  // P25Q16H, because Zephyr's driver polls a status register - which a sleeping chip won't
  // answer - *before* it ever sends the release command, so it gives up without waking the
  // part). Deliberately avoids that failure mode: no status-poll loop, just one raw
  // single-shot cinstr wake (0xAB), a fixed settle delay, a raw read, then back to sleep
  // (0xB9) and a second read - can't hang the way that bug does since nothing here retries
  // or waits on the chip's own readiness signal.
  uint8_t awakeIds[4] = {0, 0, 0, 0};
  uint8_t asleepIds[4] = {0, 0, 0, 0};
  flashTransport.begin();
  flashTransport.runCommand(0xAB); // release from deep power-down
  delay(1);                        // generous vs. a typical tRES1 of single-digit-to-tens of us
  flashTransport.readCommand(SFLASH_CMD_READ_JEDEC_ID, awakeIds, 4);
  flashTransport.runCommand(0xB9); // deep power-down again
  delay(10);
  flashTransport.readCommand(SFLASH_CMD_READ_JEDEC_ID, asleepIds, 4);
  flashTransport.end();

  Serial.print("[flash ping] awake ID: 0x");
  printJedecHex(awakeIds);
  Serial.print("  asleep ID: 0x");
  printJedecHex(asleepIds);
  Serial.println();
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
