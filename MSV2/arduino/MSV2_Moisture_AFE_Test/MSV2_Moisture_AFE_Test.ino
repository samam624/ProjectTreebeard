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

  2026-09-07 -- THIS RIG NO LONGER READS CONTINUOUSLY BY DEFAULT. It now runs
  MSV3's duty cycle: a measurement every 10 s, an advertisement every 60 s, and
  the radio off in between. The question being asked is whether the
  temperature-shaped artefact in sensor 3's data is caused by the rig running flat
  out. See the "duty-cycle experiment" block below for the three-rung ladder, what
  each rung changes, and why rung 2 (power-cycling the sensor module, which needs a
  small rewire) is the one most likely to matter. Set AFE_MODE back to
  AFE_MODE_CONSTANT to restore exactly the original behaviour for an A/B.

  Consequence worth expecting: sensor 3 now produces roughly one stored row per
  minute instead of one per three seconds, so its history and any chart built from
  it become ~20x sparser from the changeover onward. That is the intended change,
  not a fault, and it brings this rig in line with what the real probes do. The
  receiver's staleness threshold is 180 s, so a 60 s cadence still tolerates two
  consecutive missed advertisements before the card goes stale -- the same margin
  the MSV3 probes have.

  2026-09-14 -- A SECOND BOARD NOW RUNS THIS SKETCH. Sensor 9 is the "hybrid" rig: the same v1.2
  analog front end, but with an MSV3 probe end soldered in place of the module's own sense pad. It
  runs in parallel with sensor 3 (unchanged, module's own pad) so the two log the same days in the
  same room, and the only difference between them is the probe end. The question is whether the
  MSV3 electrode removes the temperature-shaped artefact that rung 1 of the duty-cycle ladder below
  did not. Keep both rigs on the SAME AFE_MODE for that comparison to mean anything -- changing the
  duty cycle on one and not the other reintroduces exactly the confound the ladder exists to avoid.

  Board: ESP32-S3 devkit (native USB). Flash/monitor with:
    powershell -ExecutionPolicy Bypass -File MSV2\scripts\arduino_moisture_afe_test.ps1 upload -Port COM12 -SensorId 3
    powershell -ExecutionPolicy Bypass -File MSV2\scripts\arduino_moisture_afe_test.ps1 upload -Port COM13 -SensorId 9
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
    [6]    sensor ID (3 = this bench rig; 9 = the hybrid rig, see AFE_SENSOR_ID below)
    [7-9]  24-bit wake counter, LE - must change every packet or the receiver dedups it
    [10-11] raw reading (this sketch's medianMv), LE
    [12-13] "battery" mV, LE - fixed placeholder (this board is USB-powered, no cell)
    [14]   charging flag - always 0 here
*********************************************************************/

#include <BLEDevice.h>

const int SENSE_PIN = 4; // IO4/GPIO4 silkscreen label - ADC1_CH3 on ESP32-S3

// 2026-09-14 -- SENSOR IDENTITY IS A BUILD PARAMETER, NOT A LINE TO EDIT. Two boards now run this
// same sketch in parallel: sensor 3 is the original bench rig reading the v1.2 module's own pad,
// and sensor 9 is the hybrid rig with an MSV3 probe end in place of that pad. The entire value of
// running them together is that only the probe end differs, which is worth nothing if a flash
// silently gives both boards the same ID. Hand-editing a constant between flashes is exactly how
// that happens -- MSV3 hit it on the probes and fixed it the same way (see ba76e29, and the
// stale-package story in scripts/msv3_fleet.py).
//
// Pass -SensorId to MSV2/scripts/arduino_moisture_afe_test.ps1. That script also gives each ID its
// own build directory, so one board's image can never be uploaded to the other from a stale
// --input-dir.
#ifndef AFE_SENSOR_ID
#define AFE_SENSOR_ID 3
#endif
const uint8_t SENSOR_ID = AFE_SENSOR_ID; // 1 = Scindapsus probe, 2 = Alocasia probe,
                                         // 3 = AFE bench rig (v1.2 module's own pad),
                                         // 9 = hybrid AFE + MSV3 probe end
const uint16_t MSV2_COMPANY_ID = 0xFFFF;
const uint16_t PLACEHOLDER_BATTERY_MV = 4200; // "full, not charging" - no real battery on this board
BLEAdvertising* pAdvertising = nullptr;
uint32_t wakeCount = 0;

// ===================================================== duty-cycle experiment ====
//
// QUESTION THIS EXISTS TO ANSWER (2026-09-07): does running this rig on MSV3's
// duty cycle, instead of reading continuously, remove the temperature-shaped
// artefact in sensor 3's data?
//
// The rig has always run flat out: a reading every 3 s, the CPU at full clock
// between them, the radio advertising CONTINUOUSLY (setup() calls
// pAdvertising->start() once and Bluedroid then advertises forever, with the
// payload merely rewritten each loop), and the sensor module powered from the
// always-on 3V3 rail. MSV3 does none of those things.
//
// IMPORTANT -- THE LADDER. These are three separate variables and they are
// deliberately not all switched on at once, because turning them all on together
// would answer "did something change" but not "which thing mattered". Climb one
// rung at a time and let a full day elapse at each.
//
//   Rung 1  AFE_MODE_MSV3_CYCLE          firmware only, no rewire.  <-- default
//           Measurement cadence and radio duty cycle both match MSV3.
//   Rung 2  MODULE_POWER_PIN             needs a rewire, see below.
//           Also power-cycles the sensor module, which is what MSV3 actually
//           does to its FDC. Most likely to be decisive -- see the note.
//   Rung 3  ENABLE_IDLE_CPU_DOWNCLOCK    firmware only.
//           Also drops the CPU clock while idle.
//
// WHY RUNG 2 IS PROBABLY THE ONE THAT MATTERS. The heat that can plausibly move a
// capacitive moisture reading is heat AT THE SENSOR. This module has its own
// 555-style oscillator sitting right beside the sense pad, and on the current
// wiring it never stops running -- duty-cycling the ESP32 does not quiet it.
// MSV3, by contrast, performs a full FDC hardware shutdown on every single wake.
// Rung 1 is worth running first because it is free, but do not be surprised if the
// artefact survives it.
//
// REWIRE FOR RUNG 2: move the module's VCC lead off the 3V3 rail and onto a spare
// GPIO, then set MODULE_POWER_PIN to that pin. These modules draw a few mA, well
// inside an ESP32-S3 pin's ~40 mA source limit. Leave GND and AOUT alone.
//
// A/B: set AFE_MODE back to AFE_MODE_CONSTANT to restore the original behaviour
// exactly. Nothing else in the sketch changes between modes, so a capture from
// either mode is directly comparable with the other.
#define AFE_MODE_CONSTANT    0
#define AFE_MODE_MSV3_CYCLE  1
#define AFE_MODE             AFE_MODE_MSV3_CYCLE

// Mirrored from MSV3_Moisture_BLE_Sleep.ino. MSV3 measures far more often than it
// transmits -- 6 measurements per advertisement -- which is why these are two
// separate numbers rather than the single READING_PERIOD_MS this sketch used to
// have. Keep them in step with the MSV3 sketch if it ever changes.
const uint32_t WAKE_PERIOD_MS = 10000;         // MSV3 WAKE_PERIOD_MS
const uint32_t ADVERTISE_INTERVAL_MS = 60000;  // MSV3 ADVERTISE_INTERVAL_MS
// MSV3 holds a ~5 s connectable window per advertisement (OTA_ADV_LISTEN_MS),
// advertising fast for the first second and slower for the rest. This rig has no
// OTA to serve, so it only needs to be discoverable for the same span.
const uint32_t ADVERTISE_WINDOW_MS = 5000;     // MSV3 OTA_ADV_LISTEN_MS

// Original behaviour, kept for AFE_MODE_CONSTANT. Matches
// MSV2_Moisture_Probe_Test.ino's WAKE_INTERVAL_MS so a side-by-side serial capture
// of both sketches lines up on roughly the same cadence.
const uint32_t READING_PERIOD_MS = 3000;

// ---- rung 2: sensor module power control. Undefined = not rewired = no-op. ----
// #define MODULE_POWER_PIN 5
//
// How long to wait after powering the module before its AOUT can be trusted. This
// module rectifies and low-pass filters its oscillator into a slow DC level, so
// unlike MSV3's FDC -- which needs only ~25 ms of reference settling -- there is a
// smoothing capacitor here that has to charge from empty on every power-up. The
// value below is a deliberately generous GUESS and must be replaced with a measured
// one: MODULE_SETTLE_DIAGNOSTIC logs a reading at the settle point and again a
// second later, and if those two disagree the settle time is too short.
const uint32_t MODULE_SETTLE_MS = 500;
const bool MODULE_SETTLE_DIAGNOSTIC = true;

// ---- rung 3: idle downclock ----
// 80 MHz is the floor that keeps the BLE peripheral stable; below it the radio has
// to be off. Off by default so rung 1 changes cadence and radio duty ONLY.
const bool ENABLE_IDLE_CPU_DOWNCLOCK = false;
const uint32_t CPU_MHZ_ACTIVE = 240;
const uint32_t CPU_MHZ_IDLE = 80;

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

#ifdef MODULE_POWER_PIN
  pinMode(MODULE_POWER_PIN, OUTPUT);
  digitalWrite(MODULE_POWER_PIN, LOW); // start unpowered; the first cycle powers it
#endif

  // Name the radio after the identity this image was actually built for. With two AFE rigs on the
  // air at once, a fixed "MSV2-AFE-TEST" on both makes a generic scan useless for telling them
  // apart -- and telling them apart is the one thing a parallel A/B depends on.
  char bleName[24];
  snprintf(bleName, sizeof(bleName), "MSV2-AFE-%u", (unsigned)SENSOR_ID);
  BLEDevice::init(bleName);
  pAdvertising = BLEDevice::getAdvertising();
#if AFE_MODE == AFE_MODE_CONSTANT
  // Bluedroid start() advertises indefinitely; data is pushed via setAdvertisementData() per loop.
  pAdvertising->start();
#else
  // Deliberately NOT started here. In MSV3-cycle mode the radio is only on for
  // ADVERTISE_WINDOW_MS once per ADVERTISE_INTERVAL_MS, so leaving it running from
  // setup() would defeat the whole point of the experiment.
#endif

  Serial.println();
  Serial.println("MSV2 analog-front-end sensor bench test (Capacitive Soil Moisture Sensor v1.2)");
  Serial.printf("Chip: %s rev%d, %d cores @ %dMHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.println("Sense pin: IO4 (ADC), lower mV = wetter");
  // A serial capture that does not say which rig produced it cannot be attributed later, and two
  // rigs now produce identically-shaped CSV.
  Serial.printf("Sensor ID: %u, advertising as %s (%s)\n", (unsigned)SENSOR_ID, bleName,
                SENSOR_ID == 3 ? "v1.2 module's own pad"
                               : (SENSOR_ID == 9 ? "hybrid: MSV3 probe end" : "unregistered rig"));

  // Print the rung the board is actually running, so a serial capture is
  // self-describing and cannot be misattributed later.
#if AFE_MODE == AFE_MODE_CONSTANT
  Serial.println("[MODE] CONSTANT -- reading every 3 s, radio advertising continuously (original behaviour)");
#else
  Serial.printf("[MODE] MSV3-CYCLE -- measure every %lu ms, advertise every %lu ms for %lu ms\n",
                (unsigned long)WAKE_PERIOD_MS, (unsigned long)ADVERTISE_INTERVAL_MS,
                (unsigned long)ADVERTISE_WINDOW_MS);
#endif
#ifdef MODULE_POWER_PIN
  Serial.printf("[MODE] rung 2 ON -- module power-cycled on GPIO%d, %lu ms settle\n",
                (int)MODULE_POWER_PIN, (unsigned long)MODULE_SETTLE_MS);
#else
  Serial.println("[MODE] rung 2 off -- sensor module permanently powered from 3V3 (not rewired)");
#endif
  Serial.printf("[MODE] rung 3 %s\n", ENABLE_IDLE_CPU_DOWNCLOCK ? "ON -- CPU downclocked while idle" : "off -- CPU stays at full clock");

  Serial.println("raw_mv(5),median_mv,smoothed_mv,percent_of_session_range");
}

// Powers the sensor module for a reading, or does nothing when the rewire has not
// been done. Mirrors what MSV3's probeWake()/probeSleep() do to the FDC.
void modulePowerOn()
{
#ifdef MODULE_POWER_PIN
  digitalWrite(MODULE_POWER_PIN, HIGH);
  delay(MODULE_SETTLE_MS);
#endif
}

void modulePowerOff()
{
#ifdef MODULE_POWER_PIN
  digitalWrite(MODULE_POWER_PIN, LOW);
#endif
}

// Idle for ms, optionally at a lower CPU clock. Not a true sleep: this board is
// mains-powered and the goal is thermal parity, not battery life. Keeping the CPU
// merely idle rather than deep-sleeping also keeps USB serial alive, which is how
// this rig is observed -- a deep sleep would drop the port every cycle.
void idleFor(uint32_t ms)
{
  if ((int32_t)ms <= 0) return;
  if (ENABLE_IDLE_CPU_DOWNCLOCK) setCpuFrequencyMhz(CPU_MHZ_IDLE);
  delay(ms);
  if (ENABLE_IDLE_CPU_DOWNCLOCK) setCpuFrequencyMhz(CPU_MHZ_ACTIVE);
}

// Median-of-N on the sense pin. Unchanged sampling; factored out so both modes and
// the settle diagnostic share exactly one reading path.
uint16_t readMedianMv()
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

  return medianOf(samples, MEDIAN_SAMPLES);
}

void loop()
{
  // Pace the whole cycle from one timestamp, so the advertising window is spent
  // INSIDE the measurement period rather than added on top of it. Without this the
  // effective cadence on an advertising cycle would be WAKE_PERIOD_MS +
  // ADVERTISE_WINDOW_MS, and the rig would drift out of step with MSV3.
  const uint32_t cycleStart = millis();

  modulePowerOn();

  // Settle diagnostic. The MODULE_SETTLE_MS above is a guess, and a guess that is
  // too short produces a reading taken while the smoothing cap is still charging --
  // which would look exactly like the drift this experiment is trying to remove.
  // Read once at the settle point and again a second later; if the pair disagree by
  // more than the noise floor, raise MODULE_SETTLE_MS until they agree.
#ifdef MODULE_POWER_PIN
  if (MODULE_SETTLE_DIAGNOSTIC)
  {
    uint16_t atSettle = analogReadMilliVolts(SENSE_PIN);
    delay(1000);
    uint16_t oneSecondLater = analogReadMilliVolts(SENSE_PIN);
    Serial.printf("[SETTLE] at %lums=%umV, +1000ms=%umV, delta=%dmV\n",
                  (unsigned long)MODULE_SETTLE_MS, atSettle, oneSecondLater,
                  (int)oneSecondLater - (int)atSettle);
  }
#endif

  uint16_t medianMv = readMedianMv();

  modulePowerOff();

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

  // Transmit the EMA-smoothed value, not the raw per-cycle median - this data jumps
  // around a lot on the bench even with median-of-N sampling, and smoothedMv was already
  // being computed above for the serial log but never actually sent (see
  // docs/STATUS_HANDOFF.md's 2026-08-16 redesign item 4).
#if AFE_MODE == AFE_MODE_CONSTANT
  advertiseReading((uint16_t)(smoothedMv + 0.5f));
  delay(READING_PERIOD_MS);
#else
  // MSV3 advertises on its own interval, not on every measurement, so most cycles
  // measure and log without transmitting anything. wakeCount therefore advances once
  // per advertisement -- which is what the receiver dedups on, and what makes a
  // missed advertisement show up as a 60 s gap rather than a 3 s one, exactly as it
  // does for a real probe.
  static uint32_t lastAdvertiseMs = 0;
  const bool intervalElapsed =
      (lastAdvertiseMs == 0) || (millis() - lastAdvertiseMs >= ADVERTISE_INTERVAL_MS);

  if (intervalElapsed)
  {
    lastAdvertiseMs = millis();
    advertiseReading((uint16_t)(smoothedMv + 0.5f));
    // Data must be set before start(): Bluedroid latches the payload when
    // advertising begins, and the original sketch only got away with rewriting it
    // live because it never stopped advertising.
    pAdvertising->start();
    idleFor(ADVERTISE_WINDOW_MS);
    pAdvertising->stop();
  }

  const uint32_t elapsed = millis() - cycleStart;
  idleFor(elapsed >= WAKE_PERIOD_MS ? 0 : (WAKE_PERIOD_MS - elapsed));
#endif
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
