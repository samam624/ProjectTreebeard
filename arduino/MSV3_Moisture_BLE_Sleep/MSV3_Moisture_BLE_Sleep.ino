// MSV3 probe -- production wake/measure/advertise/sleep firmware.
//
// Same shape as arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino
// (periodic BLE-advertise-only, no GATT peripheral, raw values on the wire --
// see the "raw-values rework" comments in that file for why calibration
// stays off the probe), built on everything bench-verified in
// arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino: the FDC2214
// register config, the TMUX1109 electrode-swap/EC-check mux, the corrected
// battery-divider read (polarity + ADC scaling + RC settle time), the
// corrected charger STAT/PG pull-up dance, the whole-board sleep sequencing
// including the 2026-08-21 gpioDisconnect() sleep-current-leak fix, and the
// LFCLK/RTC1 + watchdog safety work. This file is NOT the bring-up console --
// no interactive commands, no diagnostics-only paths -- it is the sketch
// meant to actually ship on a deployed probe.
//
// Electrode-swap alternation (2026-08-22 addendum): loop() now toggles the
// TMUX1109 between electrode-swap codes 0/1 once per wake, selected while the
// FDC2214 is still in shutdown (before probeWake() deasserts SD) so the tank
// isn't being actively driven across the mux transition. This relies on
// probeWake()'s existing settle path (tSDWAKEUP + CONFIG-last reinit +
// FDC_ACTIVATION_US + WAKE_DISCARD_CONVERSIONS discards) as the settle margin
// for whichever phase is selected -- NOT the bring-up sketch's separately
// measured 300ms *live*-switch settle, which was measured under a different
// condition (mux switched while the FDC was already free-running). This
// switch-during-shutdown assumption is a reasoned hypothesis, not yet
// hardware-proven: the bring-up sketch's `wakeprofile` command cannot
// validate it, because `doWakeProfile()` calls `probeSleep()` first, which
// resets the mux to phase 0 before the cold-wake sequence runs. See the
// "Electrode-swap alternation" addendum in
// docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md for the required
// bench-observation window (watch this sketch's own serial output across
// both phases) before leaving a probe on this behavior unattended overnight,
// and for the fallback if instability is observed.
//
// REQUIRED build path, same as the bring-up sketch: raw arduino-cli/Arduino
// IDE builds will fail at the compile-time guard below unless the installed
// Adafruit nRF52 core's pca10056 variant already has USE_LFRC active. Use:
//   powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 ^
//     -Action compile -Sketch arduino/MSV3_Moisture_BLE_Sleep
//   powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 ^
//     -Action upload -Sketch arduino/MSV3_Moisture_BLE_Sleep -Port COMx
// See docs/KNOWN_ISSUES.md and docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md for why.

#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>

#if !defined(USE_LFRC) || defined(USE_LFXO)
#error "MSV3 requires USE_LFRC and no USE_LFXO. Build with scripts/arduino_msv3_probe.ps1; see docs/KNOWN_ISSUES.md."
#endif

// ============================================================ Pin map ====
// Derived from the real schematic netlist, verified on real hardware during
// the 2026-08-20/21 bring-up sessions -- see
// hardware/MSV3_Probe_PCB/reference/ for the full pin/net cross-reference.

// ---- Status LED (D2 via R16 470R to GND, active HIGH) ----
#define LED_BIT (1UL << 10)  // P1.10

// ---- I2C to FDC2214 (4.7k pullups on-board, R4/R5) ----
#define PIN_SDA 28  // P0.28
#define PIN_SCL 45  // P1.13

// ---- FDC2214 control pins ----
#define PIN_FDC_SD   9   // P0.09 / NFC1 -- SD: HIGH=shutdown, LOW=active
#define PIN_FDC_INTB 10  // P0.10 / NFC2 -- INTB: active-low data-ready/error, unused (polled, not IRQ-driven)

// ---- TMUX1109 electrode-swap / EC-check select ----
#define PIN_MUX_A0 30  // P0.30 / AIN6
#define PIN_MUX_A1 31  // P0.31 / AIN7

// ---- Battery voltage divider (100k/100k, R29/R30). VBAT--[R29]--VBAT_DIV--
// [R30]--this pin. This pin is the divider's GROUND-SIDE return, not a
// high-side enable -- must be driven LOW during a read, not HIGH.
#define PIN_VBAT_DIV_EN 29  // P0.29 / AIN5
#define PIN_VBAT_DIV    2   // P0.02 / AIN0

// ---- EC-check divider midpoint. No LF crystal populated, so P0.00 is free
// for this. Not ADC-capable on nRF52840 -- digital reasonableness only.
#define PIN_DIV_NODE 0  // P0.00 -- unused by this sketch (mux always parked
                         // on an electrode-swap code, see MUX_SLEEP_CODE),
                         // kept disconnected at sleep for completeness.

// ---- BQ25170 charger status, both open-drain active LOW, no board pull-up ----
#define PIN_CHG_STAT 5   // P0.05/AIN3 -- LOW=charging, HIGH=complete/disabled/no battery
#define PIN_CHG_PG   41  // P1.09 -- LOW=input power good

// ======================================================= Configuration ====
const char DEVICE_NAME[] = "MSV3-MOISTURE-004";
const uint16_t BLE_COMPANY_ID = 0xFFFF;
const uint8_t BLE_MAGIC[4] = {'M', 'S', 'V', '2'};  // real production magic --
    // MSV2_ESP32_Display_Runtime.ino's receiver only looks past this once it
    // matches, same family as every deployed MSV2 probe.
// First unused ID as of 2026-08-22: 1=Scindapsus, 2=Alocasia, 3=AFE
// bench-test board (see MOISTURE_INVERT_CEILING_BY_SENSOR in the receiver).
// Easy to change before first deploy if a different id is wanted.
const uint8_t SENSOR_ID = 4;

// Same cadence MSV2 ships with: check every 10s, only radio out every 60s
// unless a real swing is seen. MSV3's own full wake (FDC reconfigure +
// settle) measured ~9.8ms on the bench -- negligible against a 10s period,
// so paying it on every check (not just every advertise) costs nothing
// meaningful and keeps the raw reading fresh for the swing comparison below.
// As of 2026-08-22 this wake also selects the next electrode-swap mux phase
// (see currentTankPhase / loop()) before the FDC leaves shutdown -- no new
// delay was added, so the ~9.8ms figure is expected to still roughly hold,
// but this exact binary (with alternation folded in) has not itself been
// current/time-probed on real hardware yet.
const uint32_t WAKE_PERIOD_MS = 10000;
const uint32_t ADVERTISE_INTERVAL_MS = 60000;
const uint32_t ADVERTISE_MS = 500;
// PROVISIONAL, same status as MSV2's own MOISTURE_SWING_THRESHOLD was before
// real soil data existed for it. Bench-only reference points so far: dry
// electrodes in air ~20.33M, direct finger-touch contact ~18.35-18.5M (~9%
// drop) -- see docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md. Set at ~1.5% of the
// dry baseline as a conservative first guess (a real watering event should
// swing well past this; a touch-level event is the outlier this is
// deliberately NOT trying to catch on its own). Retune once real
// wet-soil/dry-soil raw values are logged -- same "policy, not calibration"
// reasoning as MSV2's threshold, so it stays on the probe.
const uint32_t MOISTURE_SWING_THRESHOLD = 300000UL;
const uint32_t MIN_SWING_ADVERTISE_INTERVAL_MS = 30000;
// Conversions discarded after each cold wake before a reading is trusted.
// MEASURED on real hardware via `wakeprofile`, not guessed -- see
// docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md / msv3_ble_sleep_port notes. Kept
// as cheap (~4ms) insurance for wet-soil/temperature/low-battery conditions
// the dry-electrode bench test didn't cover.
const uint8_t WAKE_DISCARD_CONVERSIONS = 2;

// Production defaults: no USB-host wait/log traffic and no artificial LED load. A bench
// build may temporarily flip either flag, but those binaries must not be deployed as the
// normal solar/battery firmware.
const bool ENABLE_SERIAL_LOGS = false;
const bool OVERNIGHT_LED_BURN_TEST = false;
const uint32_t BOOT_HOST_WAIT_MS = 3000;

// FDC2214 wake-sequencing constants -- see probeWake()/probeSleep() below.
#define FDC_TSDWAKEUP_MS 3      // datasheet tSDWAKEUP = 2ms max; +1ms margin
#define FDC_ACTIVATION_US 400   // sensor activation = 16,384 fINT cycles (~378us @ ~43.3MHz internal osc)
#define MUX_SLEEP_CODE 0        // electrode-swap. NEVER 2 or 3 -- see probeSleep().
#define PIN_CNF_DISCONNECTED 2UL  // nRF52840 reset default: input buffer disconnected, no pull.

#define WDT_TIMEOUT_MS 5000UL

// =================================================================== FDC ==
uint8_t fdcAddr = 0x2A;  // ADDR (pin 4, WQFN-16) tied to GND per netlist.

bool fdcWriteReg(uint8_t reg, uint16_t val)
{
  Wire.beginTransmission(fdcAddr);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  return Wire.endTransmission() == 0;
}

bool fdcReadReg(uint8_t reg, uint16_t &val)
{
  Wire.beginTransmission(fdcAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start, no stop
  if (Wire.requestFrom((uint8_t)fdcAddr, (uint8_t)2) != 2) return false;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  val = (uint16_t)((hi << 8) | lo);
  return true;
}

// Datasheet-verified register config for single-channel (CH0) free-running
// conversion (TI SNOSCZ5B) -- identical to the bring-up sketch's fdcInit().
bool fdcInit()
{
  bool ok = true;
  ok &= fdcWriteReg(0x08, 0x0400);  // RCOUNT_CH0
  ok &= fdcWriteReg(0x10, 0x0064);  // SETTLECOUNT_CH0
  ok &= fdcWriteReg(0x14, 0x1001);  // CLOCK_DIVIDERS_CH0: FIN_SEL=01, FREF_DIVIDER=1
  ok &= fdcWriteReg(0x1E, 0x8000);  // DRIVE_CURRENT_CH0: IDRIVE starting point, not bench-tuned
  // MUX_CONFIG (0x1B) left at power-on-reset default -- single-channel CH0,
  // correct for this board (CH1-3 grounded per netlist).
  ok &= fdcWriteReg(0x1A, 0x1C01);  // CONFIG: CH0, exit sleep, internal osc -- written last, starts conversions
  return ok;
}

bool fdcReadStatus(uint16_t &status) { return fdcReadReg(0x18, status); }

bool fdcReadChannel0(uint32_t &data28, uint16_t &statusOut)
{
  uint16_t msb, lsb;
  if (!fdcReadReg(0x00, msb)) return false;  // MSB must be read before LSB (latches result)
  if (!fdcReadReg(0x01, lsb)) return false;
  data28 = ((uint32_t)(msb & 0x0FFF) << 16) | lsb;
  // A failed status read means ERR_WD/ERR_AHW/ERR_ALW can't be confirmed
  // absent -- treat that the same as any other I2C failure (propagate false)
  // rather than silently leaving statusOut at its caller-supplied default and
  // letting loop()'s measurementOk gate advertise an unconfirmed reading.
  if (!fdcReadStatus(statusOut)) return false;
  return true;
}

// =================================================================== Mux ==
void setMux(uint8_t code)
{
  digitalWrite(PIN_MUX_A0, (code & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_MUX_A1, (code & 0x02) ? HIGH : LOW);
}

// ================================================================ Power ===
// 3.0V ADC reference, 12-bit: 3000mV/4096 counts, x2 for the 100k/100k divider.
#define VBAT_MV_PER_LSB (3000.0f / 4096.0f)
#define VBAT_DIVIDER_RATIO (2.0f)

// Corrected divider polarity (this pin is the ground-side return, drive
// LOW not HIGH) + corrected ADC scaling (12-bit @ 3.0V ref, this core does
// NEITHER by default) + corrected RC settle time (25ms = 5 time constants
// against the R29||R30/C16 100nF filter, not the original 5ms = only 1 time
// constant) -- all three bugs found and fixed 2026-08-21, see
// docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 1 and
// hardware/MSV3_Probe_PCB/reference/COMPONENT_INDEX.md.
float readVbatMv()
{
  pinMode(PIN_VBAT_DIV_EN, OUTPUT);
  digitalWrite(PIN_VBAT_DIV_EN, LOW);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  // 50k source impedance (100k||100k) needs more than this core's 3us
  // default acquisition time; 40us is the longest offered and costs nothing
  // measurable on a once-per-wake read.
  analogSampleTime(40);
  delay(25);  // divider RC + reference settle -- 5 time constants
  (void)analogRead(PIN_VBAT_DIV);  // discard first sample after a reference switch
  int raw = analogRead(PIN_VBAT_DIV);
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  analogSampleTime(3);
  pinMode(PIN_VBAT_DIV_EN, INPUT);  // high-Z between reads
  return raw * VBAT_MV_PER_LSB * VBAT_DIVIDER_RATIO;
}

// BQ25170 STAT/PG are open-drain with no board pull-up -- a bare INPUT read
// floats. Uses the nRF52840's internal pull-up (RPU~13k, close to the
// datasheet's recommended 10k) only for the duration of the read, then
// releases both pins fully (gpioDisconnect below) -- leaving the pull-up on
// continuously would cost ~254uA whenever the charger holds a line LOW
// (PG is LOW for the entire time USB is connected).
static inline void gpioDisconnect(uint32_t arduinoPin);

void readChargerPins(bool &statHigh, bool &pgHigh)
{
  pinMode(PIN_CHG_STAT, INPUT_PULLUP);
  pinMode(PIN_CHG_PG, INPUT_PULLUP);
  delayMicroseconds(200);  // RPU x pad/trace capacitance settles well within this
  statHigh = digitalRead(PIN_CHG_STAT);
  pgHigh = digitalRead(PIN_CHG_PG);
  gpioDisconnect(PIN_CHG_STAT);
  gpioDisconnect(PIN_CHG_PG);
}

bool isBatteryCharging()
{
  bool stat, pg;
  readChargerPins(stat, pg);
  return stat == LOW;
}

// ======================================================= Sleep / wake =====
static inline void ledWrite(bool on)
{
  if (on) NRF_P1->OUTSET = LED_BIT;
  else NRF_P1->OUTCLR = LED_BIT;
}

static inline void gpioDisconnect(uint32_t arduinoPin)
{
  if (arduinoPin < 32) NRF_P0->PIN_CNF[arduinoPin] = PIN_CNF_DISCONNECTED;
  else NRF_P1->PIN_CNF[arduinoPin - 32] = PIN_CNF_DISCONNECTED;
}

// Whole-board low-power sequencing. Every step here is load-bearing --
// see docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 2 for the real measured
// result: 65-71uA before the gpioDisconnect() fix on VBAT_DIV_EN/VBAT_DIV/
// CHG_STAT/CHG_PG, 10-15uA after. Do not remove any of these four without
// re-measuring.
void probeSleep()
{
  digitalWrite(PIN_FDC_SD, HIGH);  // FDC2214 -> Shutdown (0.2uA typ)
  setMux(MUX_SLEEP_CODE);          // parked off the EC-check codes -- an EC-check
                                    // code left selected in wet soil is a continuous
                                    // 3V3->soil->VDIV->R17->GND leak, ~33uA
  gpioDisconnect(PIN_VBAT_DIV_EN);
  gpioDisconnect(PIN_VBAT_DIV);
  // Deliberately retain the temporary LED load while the rest of the probe
  // enters its normal low-power state. This does not change sensor or radio
  // timing; it only prevents D2 from being turned off between wakes.
  ledWrite(OVERNIGHT_LED_BURN_TEST);
  Wire.end();  // disable TWIM -- an enabled TWIM keeps drawing with no traffic,
               // and releases SDA/SCL so R4/R5's 4.7k pullups sit at 3V3 idle
               // instead of ~700uA/line if held low by a driven pin
  gpioDisconnect(PIN_FDC_INTB);
  gpioDisconnect(PIN_DIV_NODE);
  gpioDisconnect(PIN_CHG_STAT);
  gpioDisconnect(PIN_CHG_PG);
}

// Brings everything back up and does not return until the analog front end
// is producing trustworthy data. Returns false only on an I2C failure.
bool probeWake(uint8_t discardConversions)
{
  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);

  digitalWrite(PIN_FDC_SD, LOW);  // leave Shutdown
  delay(FDC_TSDWAKEUP_MS);        // tSDWAKEUP before the part will answer I2C

  // Shutdown resets every register -- full reconfigure is mandatory.
  // fdcInit() writes CONFIG (0x1A, SLEEP_MODE_EN=0) last, which is what
  // actually starts conversions, per the datasheet's configure-then-exit-
  // sleep ordering.
  if (!fdcInit()) return false;

  delayMicroseconds(FDC_ACTIVATION_US);  // sensor activation after sleep exit

  for (uint8_t i = 0; i < discardConversions; i++)
  {
    uint32_t d;
    uint16_t s;
    fdcReadChannel0(d, s);
    delay(1);
  }
  return true;
}

// ================================================================ WDT =====
bool g_lfclkRcOk = false;

// Verifies (does not configure -- see the header comment on why not) that
// LFCLK is running from RC, per docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md.
// Reconfiguring the clock here, after FreeRTOS/RTC1 have already started
// against the boot-time clock state, is the actual root cause that hang
// chase eventually traced down to -- the correct place is the board variant
// (USE_LFRC, enforced by the compile-time guard at the top of this file and
// scripts/arduino_msv3_probe.ps1), which wiring.c's own init() reads before
// FreeRTOS ever starts.
bool initWatchdogSafety()
{
  g_lfclkRcOk = (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) != 0 &&
                (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_SRC_Msk) == CLOCK_LFCLKSTAT_SRC_RC;
  // Skipping Bluefruit/BLE entirely when this verification fails was
  // considered and deliberately deferred (2026-08-22): it would leave a
  // probe silently non-advertising for the rest of the night with no
  // watchdog armed either, arguably worse than today's degrade-gracefully
  // behavior (proceed without a watchdog, keep advertising). Needs a
  // separately-designed recovery path (e.g. timed reset-and-retry), not a
  // same-session addition -- out of scope for this focused change.
  if (!g_lfclkRcOk) return false;  // don't arm a watchdog whose counter can't advance

  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Run << WDT_CONFIG_HALT_Pos) |
                     (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = (WDT_TIMEOUT_MS * 32768UL) / 1000UL;
  NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
  return true;
}

void feedWatchdog() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

// delay() in chunks under WDT_TIMEOUT_MS, feeding the watchdog between
// chunks -- WAKE_PERIOD_MS (10s) exceeds the 5s watchdog timeout, and a
// single feed at the top of a cycle starves it mid-sleep (this exact bug,
// reset reason 0x2 WATCHDOG, was found and fixed on the bring-up sketch).
void delayFeedingWatchdog(uint32_t ms)
{
  const uint32_t CHUNK_MS = 1000;
  while (ms > CHUNK_MS)
  {
    feedWatchdog();
    delay(CHUNK_MS);
    ms -= CHUNK_MS;
  }
  feedWatchdog();
  delay(ms);
}

// =================================================================== BLE ==
// Manufacturer-data layout intentionally diverges from MSV2's 15-byte raw
// packet starting at byte 10: MSV2's raw sensor field is 2 bytes (RC-
// oscillator kHz, max ~65k), but FDC2214's DATA_CH0 is a 28-bit register
// (real values ~18-22 million on this board) -- doesn't fit in 2 bytes.
// Widened to 4. Company ID, magic, sensorId, and the 24-bit wake counter
// stay byte-for-byte identical to MSV2's layout (bytes 0-9), so this is
// still immediately recognized by MSV2_ESP32_Display_Runtime.ino's existing
// company-ID/magic check -- only the receiver's raw-field parsing needs a
// new length-gated branch (see that file's onResult(), MSV2_RAW_PACKET_WIDE_LEN).
//
// [0-1]   companyId (0xFFFF)
// [2-5]   magic "MSV2"
// [6]     sensorId
// [7-9]   wake, 24-bit LE
// [10-13] raw FDC2214 DATA_CH0, 32-bit LE
// [14-15] raw battery mV, 16-bit LE
// [16]    charging, 1 byte
bool g_bleInited = false;
uint32_t wakeCount = 0;

// Electrode-swap phase for the NEXT wake, alternated 0/1 once per completed
// measurement attempt (success or failure) so exposure stays balanced across
// the two electrodes even through a discarded reading. Selected in loop(),
// before probeWake() -- see the file header comment and
// docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md for the settle-margin reasoning
// and its unverified-on-hardware status as of first flash.
uint8_t currentTankPhase = 0;

void bleInit()
{
  Bluefruit.autoConnLed(false);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);
  g_bleInited = true;
}

void startAdv(uint32_t rawDataCh0, float rawBatteryMv, bool isCharging)
{
  uint16_t batteryMvRounded = (uint16_t)(rawBatteryMv + 0.5f);
  uint8_t mfg[17];

  mfg[0] = lowByte(BLE_COMPANY_ID);
  mfg[1] = highByte(BLE_COMPANY_ID);
  memcpy(&mfg[2], BLE_MAGIC, 4);
  mfg[6] = SENSOR_ID;
  mfg[7] = (uint8_t)(wakeCount & 0xFF);
  mfg[8] = (uint8_t)((wakeCount >> 8) & 0xFF);
  mfg[9] = (uint8_t)((wakeCount >> 16) & 0xFF);
  mfg[10] = (uint8_t)(rawDataCh0 & 0xFF);
  mfg[11] = (uint8_t)((rawDataCh0 >> 8) & 0xFF);
  mfg[12] = (uint8_t)((rawDataCh0 >> 16) & 0xFF);
  mfg[13] = (uint8_t)((rawDataCh0 >> 24) & 0xFF);
  mfg[14] = (uint8_t)(batteryMvRounded & 0xFF);
  mfg[15] = (uint8_t)((batteryMvRounded >> 8) & 0xFF);
  mfg[16] = isCharging ? 0x01 : 0x00;

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg, sizeof(mfg));
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(32, 32);  // 20-30ms while awake, same as MSV2
  Bluefruit.Advertising.start(0);
}

// ================================================================= setup ==
uint32_t lastAdvertiseMs = 0;
long lastAdvertisedRaw = -1;

void setup()
{
  initWatchdogSafety();

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.begin(115200);
    uint32_t hostWaitStart = millis();
    while (!Serial && millis() - hostWaitStart < BOOT_HOST_WAIT_MS) delay(10);
    delay(300);
    Serial.println(F("[DIAG] MSV3 production probe booting"));
    Serial.print(F("[DIAG] sensor id: "));
    Serial.println(SENSOR_ID);
    Serial.print(F("[DIAG] LFCLK RC ok / watchdog armed: "));
    Serial.println(g_lfclkRcOk ? F("yes") : F("NO -- hang protection disabled this boot"));
    Serial.flush();
  }

  // LED
  NRF_P1->DIRSET = LED_BIT;
  ledWrite(OVERNIGHT_LED_BURN_TEST);

  // FDC2214 control pins -- bring the chip out of shutdown before any I2C attempt
  pinMode(PIN_FDC_SD, OUTPUT);
  digitalWrite(PIN_FDC_SD, LOW);
  pinMode(PIN_FDC_INTB, INPUT);

  // Mux select pins
  pinMode(PIN_MUX_A0, OUTPUT);
  pinMode(PIN_MUX_A1, OUTPUT);
  setMux(MUX_SLEEP_CODE);

  // Battery divider -- idle high-Z, only driven during an actual read
  pinMode(PIN_VBAT_DIV_EN, INPUT);
  pinMode(PIN_VBAT_DIV, INPUT);
  pinMode(PIN_DIV_NODE, INPUT);

  // Charger status -- plain inputs at boot; readChargerPins() adds the
  // pull-up only for the duration of an actual read.
  pinMode(PIN_CHG_STAT, INPUT);
  pinMode(PIN_CHG_PG, INPUT);

  bleInit();

  // Board starts in the sleep state; the first loop() iteration wakes it
  // properly rather than assuming setup()'s pin config alone is sufficient.
  probeSleep();

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.println(F("[DIAG] setup complete, entering wake/measure/advertise/sleep loop"));
    Serial.flush();
  }
}

// ================================================================== loop ==
void loop()
{
  feedWatchdog();
  uint32_t cycleStartMs = millis();

  // Select this wake's electrode-swap phase while the FDC is still in
  // shutdown (probeSleep() left SD high and the mux parked on
  // MUX_SLEEP_CODE) -- switching here, before probeWake() deasserts SD,
  // means the tank isn't being actively driven across the mux transition.
  // See the file header comment for why no separate settle delay is added.
  uint8_t phaseUsed = currentTankPhase;
  setMux(phaseUsed);

  uint32_t rawData = 0;
  uint16_t fdcStatus = 0;
  bool wakeOk = probeWake(WAKE_DISCARD_CONVERSIONS);
  bool readOk = wakeOk && fdcReadChannel0(rawData, fdcStatus);
  bool measurementOk = readOk;

  if (measurementOk)
  {
    bool errWd = (fdcStatus & (1 << 11)) != 0;   // ERR_WD -- watchdog/no-oscillation
    bool errAhw = (fdcStatus & (1 << 10)) != 0;  // ERR_AHW -- amplitude too high
    bool errAlw = (fdcStatus & (1 << 9)) != 0;   // ERR_ALW -- amplitude too low
    if (errWd)
    {
      // Not a real reading -- same treatment as an outright I2C failure:
      // don't advertise it.
      measurementOk = false;
      if (ENABLE_SERIAL_LOGS)
      {
        Serial.println(F("[WARN] FDC status ERR_WD (watchdog/no-oscillation) -- discarding this reading"));
        Serial.flush();
      }
    }
    else if ((errAhw || errAlw) && ENABLE_SERIAL_LOGS)
    {
      // Amplitude out of range -- IDRIVE (0x8000 in fdcInit()) is not
      // bench-tuned for soil yet. Logged, but NOT suppressed: this is
      // exactly the condition most likely right after watering, and
      // suppressing it risks losing the overnight test's key data point.
      Serial.print(F("[WARN] FDC status"));
      if (errAhw) Serial.print(F(" ERR_AHW(amplitude-too-high)"));
      if (errAlw) Serial.print(F(" ERR_ALW(amplitude-too-low)"));
      Serial.println(F(" -- advertising anyway"));
      Serial.flush();
    }
  }

  // Toggle for the NEXT wake regardless of this wake's outcome, so exposure
  // stays balanced across the two electrodes even through a failed/discarded
  // reading.
  currentTankPhase = phaseUsed ? 0 : 1;

  float batteryMv = readVbatMv();
  bool charging = isBatteryCharging();
  probeSleep();

  bool intervalElapsed = lastAdvertiseMs == 0 || (millis() - lastAdvertiseMs >= ADVERTISE_INTERVAL_MS);
  bool minSwingIntervalElapsed = lastAdvertiseMs == 0 || (millis() - lastAdvertiseMs >= MIN_SWING_ADVERTISE_INTERVAL_MS);
  long rawDiff = lastAdvertisedRaw >= 0 ? (long)rawData - lastAdvertisedRaw : 0;
  bool bigSwing = lastAdvertisedRaw >= 0 && (rawDiff > (long)MOISTURE_SWING_THRESHOLD ||
                                              -rawDiff > (long)MOISTURE_SWING_THRESHOLD) &&
                  minSwingIntervalElapsed;
  bool shouldAdvertise = measurementOk && (intervalElapsed || bigSwing);

  if (shouldAdvertise)
  {
    wakeCount++;

    if (ENABLE_SERIAL_LOGS)
    {
      char buf[144];
      snprintf(buf, sizeof(buf),
               "[DIAG] wake=%lu%s phase=%d DATA_CH0=%lu vbat_mV=%d charging=%d status=0x%X",
               (unsigned long)wakeCount, bigSwing ? " (swing)" : "", (int)phaseUsed,
               (unsigned long)rawData, (int)(batteryMv + 0.5f), charging ? 1 : 0, fdcStatus);
      Serial.println(buf);
      Serial.flush();
    }

    startAdv(rawData, batteryMv, charging);
    delayFeedingWatchdog(ADVERTISE_MS);
    Bluefruit.Advertising.stop();

    lastAdvertiseMs = millis();
    lastAdvertisedRaw = (long)rawData;
  }
  else if (ENABLE_SERIAL_LOGS)
  {
    Serial.print(F("[DIAG] checked, phase="));
    Serial.print(phaseUsed);
    Serial.print(F(", DATA_CH0="));
    Serial.print(rawData);
    if (!wakeOk) Serial.println(F(" -- FDC WAKE FAILED, skipping advertise"));
    else if (!readOk) Serial.println(F(" -- FDC READ FAILED, skipping advertise"));
    else if (!measurementOk) Serial.println(F(" -- FDC ERR_WD, skipping advertise"));
    else Serial.println(F(" -- no advertise this wake"));
    Serial.flush();
  }

  uint32_t awakeMs = millis() - cycleStartMs;
  uint32_t sleepMs = awakeMs < WAKE_PERIOD_MS ? (WAKE_PERIOD_MS - awakeMs) : 1;
  delayFeedingWatchdog(sleepMs);
}
