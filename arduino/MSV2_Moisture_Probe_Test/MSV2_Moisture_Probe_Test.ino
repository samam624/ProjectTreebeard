/*********************************************************************
  MSV2 moisture probe bench test (Seeed XIAO nRF52840).

  Free-running RC relaxation oscillator, modeled directly on the 555
  astable architecture validated in the MSV1 proof-of-concept paper
  (Amezcua, "Design and Experimental Characterization of a Capacitive
  Soil Moisture Sensor Using a 555 Timer Architecture"). Charge until
  HIGH, immediately discharge until LOW, immediately recharge, repeat,
  continuously -- count how many discharge-complete transitions happen
  in a fixed window and derive a frequency, exactly analogous to
  counting the 555 astable's output edges. Lower frequency = wetter
  (higher effective capacitance).

  Resistor: two independent 10k pulldowns to GND, one on each comb of the
  interdigitated sensor (IO0/A0 = plate A, IO2 = plate B). Originally plate B
  was hardwired straight to GND with only plate A driven/read; that meant
  every cycle pushed the same net DC field across the sensor, and bench
  testing found a steady upward reading drift over back-to-back samples that
  outlasted the fix for the original one-shot-timing residual-voltage issue
  (see docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md root cause #3) -- consistent
  with dielectric absorption/polarization building up in the soldermask-
  covered sense structure under a sustained one-directional field, the same
  reason real capacitive/EC soil probes use AC excitation instead of DC.
  Plate B was rewired off GND onto IO2 with its own 10k pulldown so it can be
  actively driven too. countOscCycles() now alternates which plate is the
  driver and which is the read/discharge pin every single cycle -- half the
  cycles push the field A->B, half push B->A, so the net DC bias across the
  sensor averages toward zero instead of compounding in one direction. Bonus
  side effect: the "off" plate is actively driven LOW (not just left
  floating on its own resistor) right before its next turn as the read pin,
  which also forces a hard reset to 0V between that plate's active uses --
  the same fix root cause #3 wanted, for free.

  LOW-POWER PERIODIC MODE: runs as a wake/measure/sleep cycle instead of
  a continuous loop, mirroring MSV2_Moisture_BLE_Sleep.ino's pattern --
  the oscillator (and the CPU driving it) is only active during the
  brief measurement burst each wake. The red LED on A1 is lit for the
  exact duration of each burst, so you can see (or scope) precisely how
  long the active-power window is.

  MEDIAN-OF-5 + SHORT BURSTS: an earlier window sweep (50/20/10/5/2/1ms)
  showed that shrinking the burst below ~10ms starts producing
  occasional large outlier spikes (e.g. one single 2ms reading came
  back 67% high) -- almost certainly rare RTOS/scheduler tick jitter
  delaying the burst's own millis()-based cutoff check, not a real
  moisture change. A ~1ms delay is invisible on a 50ms burst but a huge
  fraction of a 2-5ms one. Rather than lengthen the burst back out to
  dodge that, each report now takes 5 independent SAMPLE_WINDOW_MS
  bursts and reports the MEDIAN cycle count -- a median-of-5 silently
  drops up to 2 outliers per report, so a rare single-sample glitch
  never reaches the output. This lets the per-sample burst itself stay
  very short (SAMPLE_WINDOW_MS) while staying immune to that jitter.
  Total active time per report is still only
  MEDIAN_SAMPLES * SAMPLE_WINDOW_MS -- far below the original 50ms.

  Open Serial Monitor or Serial Plotter at 115200 baud.

  Battery + charge monitoring (XIAO nRF52840 Sense only):
  The Sense variant has an onboard BQ25100 charger and a VBAT divider
  behind a protection switch. Pin names below come straight from this
  core's variant.h/variant.cpp for Seeed_XIAO_nRF52840_Sense:
    - VBAT_ENABLE (D14/P0.14): drive LOW to connect the VBAT divider to
      PIN_VBAT, HIGH to disconnect it. The core leaves this HIGH
      (disconnected) at boot specifically because leaving the divider
      connected wastes sleep current and, per Seeed's own battery
      charging notes, driving this pin HIGH while it is also configured
      as an analog input can push PIN_VBAT toward its 3.6V limit. So it
      is only pulled LOW for the brief moment of each reading.
    - PIN_VBAT (D32/P0.31): ADC input for the divided battery voltage.
      The onboard divider is a 1M/510k pair (ratio (1000k+510k)/510k),
      so real battery voltage = ADC pin voltage * VBAT_DIVIDER_RATIO.
    - D23/P0.17 (BQ25100 ~CHG, no core macro): open-drain, active LOW
      while the BQ25100 is actively charging the battery.
    - PIN_CHARGING_CURRENT (D22/P0.13, BQ25100 HICHG) selects the BQ25100
      charge current (LOW = 100mA, HIGH = 50mA) and is left floating by
      this core's default init, so charge current follows the chip's
      own default. Not overridden here since the right value depends on
      your battery's mAh rating.
*********************************************************************/

#include <Adafruit_TinyUSB.h> // required on this core for Serial over USB

const int PLATE_A_PIN = A0; // IO0 on the XIAO silkscreen -- one comb of the sensor.
const int PLATE_B_PIN = D2; // IO2 on the XIAO silkscreen -- the other comb.
const int LED_PIN = A1;     // external red LED, HIGH = on (flip if wired active-low)

// CEILING_TEST_MODE (see below) measured this design's max cycle rate at ~233kHz with
// Arduino's pinMode()/digitalWrite()/digitalRead() (5 calls/cycle) -- real dry-soil
// readings were landing at ~205kHz, only ~12% of headroom below that ceiling, which is
// why the moisture range compressed so hard after moving to 10k plates. countOscCycles()
// below bypasses that call overhead with direct nRF52840 GPIO register access instead.
// Per Seeed_XIAO_nRF52840_Sense's own variant.cpp pin map: D0/A0 (PLATE_A) = P0.02,
// D2/A2 (PLATE_B) = P0.28 -- both on NRF_P0, so no port-1 handling needed here.
#define PLATE_A_BIT (1UL << 2)  // P0.02
#define PLATE_B_BIT (1UL << 28) // P0.28

// Sleep duration between moisture test bursts.
const uint32_t WAKE_INTERVAL_MS = 3000;

// Per-sample burst duration and how many samples feed the median. Total
// active time per report is MEDIAN_SAMPLES * SAMPLE_WINDOW_MS = 5ms.
const uint32_t SAMPLE_WINDOW_MS = 1;
const uint8_t MEDIAN_SAMPLES = 5;

// Safety cap per half-cycle so a disconnected/open probe (pin never
// reads LOW) can't hang a burst forever.
const uint32_t OSC_CYCLE_TIMEOUT_US = 5000UL; // 5 ms

// Charge-settling delay before releasing the driven plate to float/read (see the 40
// unrolled __NOP()s in countOscCycles()). The direct-register rewrite closed the ~1-2us
// of incidental charge time Arduino's digitalWrite()+pinMode() call overhead used to
// provide, which turned out to be load-bearing: without it, a touch-loaded plate didn't
// reach a real HIGH before release, producing a small, plausible-looking reading that had
// stopped tracking real capacitance changes (confirmed bench-side: zero touch response
// with no delay, restored with delayMicroseconds(1) at 220-326kHz touch-responsive).
// Trimmed to 20 NOPs (~312ns @ 64MHz) for more range, then bumped to 40 after a heavier
// touch load (4 fingers) turned out to saturate at 20 -- see the fuller note at the NOP
// block itself. Re-verify touch/moisture response live after any change to this value,
// since undershooting fails silently (a plausible-looking number that's just wrong).

// --- Diagnostic: firmware overhead ceiling test ------------------------
// Set true to measure this design's max possible cycle rate independent of
// any real RC delay. Runs the exact same 5-GPIO-call-per-cycle sequence as
// normal operation, but treats the discharge as done after a single
// digitalRead() instead of polling for a real LOW -- the same one-read cost
// the real loop pays on its fastest possible pass. The resulting freq_hz IS
// the firmware-overhead-only ceiling: compare it against a real moisture
// reading's freq_hz to see what fraction of each cycle is fixed overhead
// vs actual RC time. Flip back to false and reflash for normal sensing --
// this mode does not produce real moisture data.
const bool CEILING_TEST_MODE = false;

float smoothedHz = -1.0f;
float sessionMinHz = 1.0e9f;
float sessionMaxHz = 0.0f;

const int CHG_STATUS_PIN = 23; // D23/P0.17, BQ25100 ~CHG, active LOW.
const uint8_t VBAT_ADC_BITS = 12;
const float VBAT_ADC_REF_MV = 3600.0f; // AR_INTERNAL = 0.6V * 6
const float VBAT_DIVIDER_RATIO = (1000.0f + 510.0f) / 510.0f; // onboard 1M/510k divider

void setup()
{
  Serial.begin(115200);

  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000)
  {
    delay(10);
  }

  analogReference(AR_INTERNAL);
  analogReadResolution(VBAT_ADC_BITS);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH); // divider disconnected except while sampling
  pinMode(CHG_STATUS_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // One-time known-good PIN_CNF for both plates: input buffer connected, no pull,
  // standard drive, sense disabled (PIN_CNF=0 also leaves DIR=input, matching where
  // pinMode(..., INPUT) would leave them). countOscCycles() only touches DIR/OUT via
  // DIRSET/DIRCLR/OUTSET/OUTCLR after this -- those don't touch the input-buffer-connect
  // bit, so this has to be set correctly once up front or IN reads could be unreliable.
  NRF_P0->PIN_CNF[2] = 0;
  NRF_P0->PIN_CNF[28] = 0;

  Serial.println();
  Serial.println("MSV2 moisture probe bench test (periodic, median-of-5)");
  Serial.println("Plates: A0/IO0 + IO2, each with its own 10k pulldown, alternating drive. LED: A1, on during each burst.");
  if (CEILING_TEST_MODE)
  {
    Serial.println("*** CEILING_TEST_MODE=true: NOT real moisture data. Reports this design's firmware-overhead-only max rate. ***");
  }
  Serial.println("raw_cycles(5),median_cycles,freq_hz,smoothed_hz,percent_of_session_range");
}

void loop()
{
  digitalWrite(LED_PIN, HIGH); // on for the entire measurement burst below

  // Throwaway warm-up pass, result discarded: every burst's first real sample was
  // reading ~3% low and consistently so (227 vs 233-234 raw cycles, then 745 vs 766-767
  // after the register-access rewrite) -- likely instruction-cache eviction from the
  // Serial/delay() calls between bursts, possibly compounded by LED_PIN (P0.03) being
  // pin-adjacent to PLATE_A (P0.02) and switching right as the first window starts.
  // One extra ~1ms pass costs little against the existing 5ms budget and settles either
  // cause before anything gets recorded.
  countOscCycles(SAMPLE_WINDOW_MS);

  uint32_t samples[MEDIAN_SAMPLES];
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    samples[i] = countOscCycles(SAMPLE_WINDOW_MS);
  }

  Serial.print("raw:");
  for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++)
  {
    Serial.print(samples[i]);
    Serial.print(i + 1 < MEDIAN_SAMPLES ? " " : ",");
  }

  uint32_t medianCycles = medianOf(samples, MEDIAN_SAMPLES);
  float freqHz = (float)medianCycles * 1000.0f / (float)SAMPLE_WINDOW_MS;

  if (smoothedHz < 0)
  {
    smoothedHz = freqHz;
  }
  else
  {
    smoothedHz += 0.2f * (freqHz - smoothedHz);
  }

  if (freqHz < sessionMinHz) sessionMinHz = freqHz;
  if (freqHz > sessionMaxHz) sessionMaxHz = freqHz;

  // Auto-scaled percent relative to what this session has seen so far,
  // inverted since lower frequency = wetter: 100% = closest to the
  // wettest thing seen this session, 0% = closest to the driest.
  float percent = 0.0f;
  if (sessionMaxHz > sessionMinHz)
  {
    percent = 100.0f * (sessionMaxHz - smoothedHz) / (sessionMaxHz - sessionMinHz);
  }

  Serial.print(medianCycles);
  Serial.print(",");
  Serial.print(freqHz, 1);
  Serial.print(",");
  Serial.print(smoothedHz, 1);
  Serial.print(",");
  Serial.println(percent, 1);

  digitalWrite(LED_PIN, LOW); // off while sleeping

  reportBattery();

  delay(WAKE_INTERVAL_MS); // sleep until the next wake
}

void reportBattery()
{
  digitalWrite(VBAT_ENABLE, LOW); // connect the divider
  delayMicroseconds(200);         // let it settle
  uint16_t raw = analogRead(PIN_VBAT);
  digitalWrite(VBAT_ENABLE, HIGH); // disconnect again

  float pinMv = raw * (VBAT_ADC_REF_MV / (float)(1UL << VBAT_ADC_BITS));
  float batteryMv = pinMv * VBAT_DIVIDER_RATIO;
  bool charging = digitalRead(CHG_STATUS_PIN) == LOW;

  Serial.print("battery_mV=");
  Serial.print(batteryMv, 0);
  Serial.print(" charging=");
  Serial.println(charging ? 1 : 0);
}

// Free-running relaxation oscillator, alternating which plate drives and which
// reads every single cycle (see the file header for why): charge the driven
// plate HIGH while actively holding the other plate LOW as a hard reference,
// release the driven plate to INPUT and time its own RC decay back to LOW
// through its own 10k pulldown, immediately swap roles, repeat for windowMs.
// Uses direct NRF_P0 register access instead of pinMode()/digitalWrite()/
// digitalRead() -- CEILING_TEST_MODE measured the Arduino-call version's fixed
// overhead was eating ~88% of the dry-soil cycle time at 10k, leaving almost no
// room for the RC term (the actual signal) to move the reading. Returns the
// total number of complete HIGH->LOW discharge cycles observed across both
// plates -- with RA==RB by design, A-driven and B-driven cycles should read
// near-identically, so summing them is just summing more independent samples
// of the same underlying capacitance.
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

    // Charge-settling delay -- see the longer note where CHARGE_SETTLE_NOPS is declared.
    // Unrolled, not a for-loop: a loop's own increment/compare/branch overhead (several
    // cycles/iteration on Cortex-M4) would swamp a target this short and silently make
    // the realized delay LONGER than intended instead of shorter -- confirmed the hard
    // way (a for-loop version dropped the reading from 326kHz to 250kHz, the wrong
    // direction for a shortened delay). This is 20 literal NOP instructions, no branch.
    // Bumped 20 -> 40: a heavier touch load (4 fingers) was saturating at 244kHz and
    // never going lower no matter how much more contact was added -- not the sensor
    // maxing out, but this delay running out of charge budget for that much capacitance
    // (the fixed charge pulse can no longer get a big-enough plate meaningfully above
    // the read threshold at all, so the reading stops tracking capacitance and flattens).
    // Sized for the wettest case we expect (soaked soil), not just a light single touch.
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();

    NRF_P0->DIRCLR = driveBit; // release driven plate to INPUT (float, PIN_CNF input buffer already connected from setup())

    if (CEILING_TEST_MODE)
    {
      volatile uint32_t discard = NRF_P0->IN; // pay the same one-read cost the real loop's fastest pass would, without waiting on real RC decay
      (void)discard;
    }
    else
    {
      uint32_t waitStartUs = micros();
      while (NRF_P0->IN & driveBit)
      {
        if (micros() - waitStartUs >= OSC_CYCLE_TIMEOUT_US)
        {
          break; // disconnected/open probe -- bail this half-cycle
        }
      }
    }

    cycles++;
    driveIsPlateA = !driveIsPlateA; // swap which plate drives next cycle
  }

  return cycles;
}

// In-place insertion sort + middle element. Fine for the small, fixed
// sample counts used here (no need for anything fancier than O(n^2)).
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
