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
// isn't being actively driven across the mux transition.
//
// 2026-08-29 update: the mux-during-shutdown settle margin was never
// hardware-verified (see docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md's
// "Required bench-observation window" section), and this build is about to
// run a multi-day unattended solar-recharge test with no serial console
// watching it -- BLE advertising (the only remote telemetry for that test,
// battery mV included) is gated on a clean FDC read, so a bad settle
// assumption would go silent rather than just log a warning. Applying that
// doc's own named fallback preemptively rather than running the bench
// window first: MUX_SWITCH_SETTLE_MS (300ms) now runs after every setMux()
// that precedes a probeWake(), reusing the bring-up sketch's own
// proven-on-hardware *live*-switch settle figure as a safety margin here
// too. This makes the settle-timing question moot for this test instead of
// betting the whole test's visibility on an unverified hypothesis.
// REMOVED the same day this comment was added to -- see the 2026-08-30 entry below.
//
// 2026-08-30 update: independent firmware/datasheet review (two Codex review workers, cross-
// checked against the real TI FDC2214 PDF, no firmware changed at review time) found three
// concrete FDC measurement bugs, fixed here -- see each site's own comment for the datasheet
// table/formula backing it:
// - RCOUNT_CH0 raised 0x0400 -> 0xFFFF (fdcInit()) -- was giving only ~14 effective bits at
//   negligible time cost (~24ms of a 10s wake) to fix.
// - CONFIG's SENSOR_ACTIVATE_SEL bit fixed Low-Power -> Full-Current activation, TI's
//   recommended value (fdcInit(): 0x1C01 -> 0x1401) -- every other bit in that register
//   already matched its datasheet-required value, verified field-by-field before changing it.
// - fdcReadChannel0() now waits for CH0_UNREADCONV and reads STATUS before DATA, not after -- the
//   datasheet states a latched error is cleared by reading either STATUS or the DATA_CHx
//   register, so the old DATA-then-STATUS order could silently lose a real error flag before
//   ever seeing it, not just mis-pair it with the wrong sample.
// Explicitly NOT changed, and should not be guessed at without bench data:
// SETTLECOUNT_CH0 (its datasheet formula needs real Vpk/IDRIVE/capacitance values this project
// doesn't have yet).
//
// 2026-09-03: IDRIVE (DRIVE_CURRENT_CH0) IS now bench-tuned -- 0x8000 (code 16) -> 0xC800
// (code 25, 0.644mA), swept against a real oscilloscope per the datasheet's own method. Code 16
// oscillated at only ~0.5Vpk in dry air, roughly a THIRD of the 1.2Vpk VSENSORMIN, and ~0.3Vpk
// under heavy load. Code 25 gives 1.9Vpk dry / 1.3Vpk loaded. The reason for the change is
// amplitude compliance and oscillator robustness in untested conditions (cold, saturated soil,
// low battery) -- NOT noise: the measured SNR gain is only 1.40x, which is negligible against a
// drying cycle that spans millions of counts.
//
// THIS SHIFTS THE READING. DATA_CH0 drops ~39,950 counts (0.200%, about 1% of the dry->saturated
// span) going from code 16 to code 25 -- confirmed by an A-B-A sweep and by 200-sample streams at
// each code, and independent of the level-switching artifact noted below. Deployed probes will
// step ~1 moisture point when they take this firmware. Re-baseline rather than treating it as a
// drop-in. See MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 4 for the full sweep and derivation.
//
// 2026-08-30, later same day: removed MUX_SWITCH_SETTLE_MS entirely (was 300ms, added just
// above on 2026-08-29). Pulled the real TMUX1109 datasheet (SCDS406A) rather than continuing to
// guess: worst case across -40..125C at VS=3V, tTRAN=19ns, tON(EN)=20ns, tOFF(EN)=9ns -- the
// switch itself completes in tens of nanoseconds, and this board's actual mux-adjacent R/C
// can't plausibly stretch that into more than tens of microseconds (see the removed constant's
// own comment, still kept in place near MUX_SLEEP_CODE below, for the full arithmetic). The
// delay ran before probeWake() deasserts SD, so it was never able to settle oscillator ring-up
// or live-tank mux parasitics regardless -- see SETTLECOUNT_CH0 above for what actually governs
// that. Removing it recovers ~300ms of the ~10s wake period on every wake, ~600ms on advertising
// wakes (it ran twice there).
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
// Byte-for-byte copy of MSV3_BLE_OTA_Test/ResetSafeBLEDfu.{h,cpp}. Arduino compiles only the
// files inside a sketch folder, so a shared copy is the practical option; if either copy is
// changed, change both. See MSV3_BLE_OTA_PROOF_2026-09-02.md for why this exists instead of the
// core's stock BLEDfu (bootloader 0.11.0 soft-jump/ACL stall, Adafruit bootloader issue 399).
#include "ResetSafeBLEDfu.h"

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
const char DEVICE_NAME[] = "MSV3-MOISTURE-005";
const uint16_t BLE_COMPANY_ID = 0xFFFF;
const uint8_t BLE_MAGIC[4] = {'M', 'S', 'V', '2'};  // real production magic --
    // Treebeard_ESP32_Display_Runtime.ino's receiver only looks past this once it
    // matches, same family as every deployed MSV2 probe.
// 2026-08-31: bumped to 5 (first unused ID as of 2026-08-22 was 4: 1=Scindapsus,
// 2=Alocasia, 3=AFE bench-test board; 4 went to the field unit documented in
// MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md) for a second MSV3 board added
// to service the same day, health-checked via the bring-up sketch's muxsweep/divnode
// commands first (real ~20% mux step present, divnode reads LOW at rest - neither of the
// two hardware fault signatures MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md documents).
// This constant and DEVICE_NAME above are edited in place per-board rather than kept as
// separate per-board sketch files (unlike MSV2's per-plant .ino convention) - deliberate
// simplicity trade-off. IMPORTANT: this means reflashing the ORIGINAL id=4 field unit
// (e.g. for a firmware update) requires manually setting this back to 4 and DEVICE_NAME
// back to "...-004" first - there is currently no per-board source file to prevent
// accidentally flashing the wrong id onto the wrong physical probe.
const uint8_t SENSOR_ID = 5;

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
// A legacy BLE DFU transfer takes several minutes. Permit the app-to-bootloader handoff only
// while USB input is power-good or a valid LiPo reading is at least 3.80V. This is intentionally
// well above the harvester's ~3.04V VBAT_UV threshold: crossing the hardware cutoff says the
// board can run, not that it has enough stored energy to finish a flash operation. The dual-bank
// recovery bootloader remains available after any interrupted transfer once power returns.
const uint16_t OTA_MIN_BATTERY_MV = 3800;
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
#define FDC_TSDWAKEUP_US 2500   // datasheet tSDWAKEUP = 2ms; 500us margin.
// 2026-09-01: was `delay(FDC_TSDWAKEUP_MS)` with FDC_TSDWAKEUP_MS=3 and a comment claiming
// "+1ms margin". That margin did not exist. Adafruit nRF52 core 1.7.0's delay() is
// vTaskDelay(pdMS_TO_TICKS(ms)) (cores/nRF5/delay.c) against configTICK_RATE_HZ=1024, so one
// tick is 0.9766ms and pdMS_TO_TICKS(3) truncates to 3 ticks. vTaskDelay unblocks on a tick
// BOUNDARY, so the real wait was (3 - f) ticks for f = however far into the current tick the
// call landed: 1.953ms to 2.930ms. It fell UNDER the datasheet's 2ms whenever f > 0.952 --
// about 4.8% of wakes, roughly 1 in 21. A short tSDWAKEUP NACKs fdcInit()'s first write, so
// probeWake() returns false and the wake advertises nothing (no retry, see loop()) -- an
// intermittent silent dropped reading, which matters because unattended solar-test telemetry
// is gated on a clean FDC read. delayMicroseconds() is nrfx_coredep_delay_us() (delay.h), a
// cycle-accurate busy-wait with no tick quantization, so 2500us is really 2500us. The spin
// costs nothing once per ~10s wake. Do not convert this back to delay().
// Sensor activation = 16,384 fINT cycles (~378us @ ~43.3MHz internal osc). This figure is the
// Full-Current Activation Mode number (CONFIG.SENSOR_ACTIVATE_SEL=b0) -- before the 2026-08-30
// fix below, CONFIG was actually left in Low Power Activation Mode (b1), so this delay didn't
// match the mode the chip was really in. fdcReadChannel0()'s fresh-conversion wait (added the same day)
// makes this no longer load-bearing on its own -- it's a warm-up floor before polling starts,
// not the sole guarantee of a settled conversion.
#define FDC_ACTIVATION_US 400
#define MUX_SLEEP_CODE 0        // electrode-swap. NEVER 2 or 3 -- see probeSleep().
// REMOVED 2026-08-30: MUX_SWITCH_SETTLE_MS (300ms, added 2026-08-29 as a preemptive safety
// margin -- see the file header comment for that history). Checked the real TMUX1109 datasheet
// (SCDS406A) against this board's actual load rather than continuing to guess: at VS=3V, worst
// case across -40..125C, tTRAN=19ns, tON(EN)=20ns, tOFF(EN)=9ns, tOPEN(BBM)~1ns -- the switch
// itself is done in low tens of nanoseconds. TI's own methodology note says system RC adds on
// top of that device-only figure, but this board's mux-adjacent capacitance (~38-50pF) and
// resistance (RON ~8.75 ohm; nothing else in the switched path is more than low-MOhm, and nowhere
// near it) cap any real RC settling at tens of microseconds at the absolute worst -- roughly
// 6 GOhm of series resistance would be needed to make an RC time constant reach 300ms against
// that capacitance, which is not physically present anywhere in this signal path.
// This delay ran BEFORE probeWake() deasserts SD (FDC still in shutdown, oscillator not yet
// running), so it was never capable of settling oscillator ring-up or mux-injected tank
// parasitics in the first place -- see SETTLECOUNT_CH0 in fdcInit() for the register that
// actually governs oscillator settling, which is what a real settling problem would need to
// address, bench-tuned against real Vpk/IDRIVE data this project doesn't have yet. The natural
// gap between setMux() and the oscillator actually starting (FDC_TSDWAKEUP_US's 2.5ms, plus the
// I2C transactions inside fdcInit(), plus FDC_ACTIVATION_US's 400us) already leaves the mux
// microseconds-to-nanoseconds switch several orders of magnitude of margin with zero added
// delay. Removing this bought back ~300ms of the ~10s wake period on every wake, and ~600ms on
// every advertising wake (it ran twice there -- once for the electrode-swap phase, once for the
// bare-tank EC-check mux code).
#define PIN_CNF_DISCONNECTED 2UL  // nRF52840 reset default: input buffer disconnected, no pull.

#define WDT_TIMEOUT_MS 5000UL

// sd_temp_get()'s native units (die temperature in 0.25 degC, confirmed against the installed
// SoftDevice's own nrf_soc.h doc comment) and the nRF52840 PS v1.1 TEMP peripheral's specified
// operating range (-40 to +85 degC). Anything outside this range from a "successful" SVC call is
// treated as untrustworthy, not clamped.
#define BOARD_TEMP_MIN_Q4C (-160)
#define BOARD_TEMP_MAX_Q4C 340
// Bounded poll for sd_clock_hfclk_is_running() after sd_clock_hfclk_request() -- HFXO start is
// normally much faster than this on this board; this is a safety ceiling, not a tuned value.
#define BOARD_TEMP_HFCLK_POLL_TIMEOUT_MS 5

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
//
// 2026-08-30 review fixes (docs cross-checked against the real TI PDF, table/bit references
// below all point at the local extracted copy):
// - RCOUNT_CH0 raised 0x0400 -> 0xFFFF. Conversion time is (RCOUNT*16+4)/fREF (Table 7-6);
//   at 0x0400 that's ~0.378ms, corresponding to only ~14 effective bits (the datasheet ties
//   ENOB directly to RCOUNT -- Section 7.6, "if an ENOB of 13 bits is required... 8192 clock
//   cycles... RCOUNT value of 0x0200"). 0xFFFF costs ~24ms, negligible against the 10s wake
//   period, for close to the chip's full 28-bit resolution -- there was no real reason to be
//   leaving that resolution on the table given the sensor's own signal is only a few pF on
//   top of a ~300-370pF tank.
// - CONFIG 0x1C01 -> 0x1401: clears bit 11 (SENSOR_ACTIVATE_SEL), the only bit in 0x1C01 that
//   didn't already match its datasheet-required/recommended value (every other bit was
//   already correct, including the two "reserved, must be b1" bits -- verified against Table
//   7-38 field-by-field before changing anything). Bit 11 was b1 (Low Power Activation Mode --
//   "uses the value programmed in DRIVE_CURRENT_CHx during sensor activation"); TI's field
//   description recommends b0 (Full Current Activation Mode -- "drives maximum sensor current
//   for a shorter sensor activation time"), the more robust-startup choice this file's own
//   FDC_ACTIVATION_US comment already assumed (see that constant's comment above -- it cites
//   the Full-Current-mode 16,384-cycle activation figure, which this fix now actually matches).
//   The power cost of full-current activation only applies to the ~400us activation window
//   inside a 10s duty-cycled wake, not a continuously-driven load -- immaterial here.
bool fdcInit()
{
  bool ok = true;
  ok &= fdcWriteReg(0x08, 0xFFFF);  // RCOUNT_CH0 -- see 2026-08-30 comment above
  ok &= fdcWriteReg(0x10, 0x0064);  // SETTLECOUNT_CH0
  ok &= fdcWriteReg(0x14, 0x1001);  // CLOCK_DIVIDERS_CH0: FIN_SEL=01, FREF_DIVIDER=1
  ok &= fdcWriteReg(0x1E, 0xC800);  // DRIVE_CURRENT_CH0: IDRIVE=25 (0.644mA), bench-tuned 2026-09-03
  // MUX_CONFIG (0x1B) left at power-on-reset default -- single-channel CH0,
  // correct for this board (CH1-3 grounded per netlist).
  ok &= fdcWriteReg(0x1A, 0x1401);  // CONFIG: CH0, exit sleep, internal osc, full-current
                                    // sensor activation -- see 2026-08-30 comment above.
                                    // Written last, starts conversions.
  return ok;
}

bool fdcReadStatus(uint16_t &status) { return fdcReadReg(0x18, status); }

// CH0_UNREADCONV, Address 0x18 STATUS bit 3 (Table 7-36): an unread conversion is present for
// channel 0, and DATA_CH0 should be read to retrieve it. Poll this per-channel flag rather than
// DRDY: ERROR_CONFIG.DRDY_2INT resets to b0 after every hardware shutdown, and Table 7-37 says
// that setting prevents STATUS.DRDY from being updated. The prior DRDY poll therefore timed out
// forever after the 2026-08-30 flash and suppressed every advertisement.
#define FDC_STATUS_CH0_UNREADCONV_BIT 0x0008
// Generous bound over the RCOUNT-driven worst-case conversion time (see fdcInit()'s RCOUNT_CH0
// comment, ~24ms at RCOUNT=0xFFFF) -- this is a fail-fast ceiling for a genuinely stuck part/
// oscillator, not a value tuned to be tight.
#define FDC_CONVERSION_POLL_TIMEOUT_MS 60

// 2026-08-30 review fix: waits for CH0_UNREADCONV before trusting a conversion, and reads STATUS before
// DATA. Neither was true before. Both matter together, not separately:
// - Table 7-36 (STATUS): "When flagged, any reported error is latched and maintained until
//   either the STATUS register OR the DATA_CHx register corresponding to the Error Channel is
//   read." The previous read order (DATA_CH0 MSB/LSB, then STATUS) meant a real ERR_WD/
//   ERR_AHW/ERR_ALW on the very sample just captured could already be cleared by the DATA read
//   before STATUS ever saw it -- not just "describes a later conversion", but potentially lost
//   entirely.
// - Nothing previously confirmed a fresh CH0 conversion before reading DATA at all -- the only
//   settling guarantee was WAKE_DISCARD_CONVERSIONS' fixed discard-and-delay(1) loop in
//   probeWake(), a bench-measured heuristic against the OLD, much shorter RCOUNT=0x0400
//   conversion time. At the new RCOUNT=0xFFFF (~24ms/conversion), a 1ms inter-discard delay is
//   far shorter than one conversion, so without this unread-conversion wait that loop would now just
//   re-read the same stale conversion repeatedly instead of ever advancing to a new one.
bool fdcWaitForCh0UnreadConversion(uint16_t &statusOut)
{
  uint32_t start = millis();
  do
  {
    if (!fdcReadStatus(statusOut)) return false;
    if (statusOut & FDC_STATUS_CH0_UNREADCONV_BIT) return true;
  } while (millis() - start < FDC_CONVERSION_POLL_TIMEOUT_MS);
  return false;  // timed out waiting for a fresh conversion -- same failure path as an I2C error
}

bool fdcReadChannel0(uint32_t &data28, uint16_t &statusOut)
{
  // See the 2026-08-30 comment on fdcWaitForCh0UnreadConversion() above for why this must run, and run,
  // before DATA -- not just as an added check after the fact.
  if (!fdcWaitForCh0UnreadConversion(statusOut)) return false;
  uint16_t msb, lsb;
  if (!fdcReadReg(0x00, msb)) return false;  // MSB must be read before LSB (latches result)
  if (!fdcReadReg(0x01, lsb)) return false;
  data28 = ((uint32_t)(msb & 0x0FFF) << 16) | lsb;
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

// ========================================================= Board temp ====
// nRF52840 die temperature via the SoftDevice's sd_temp_get(), NOT the raw NRF_TEMP peripheral
// directly (that path is only safe when the SoftDevice is disabled -- see wiring.c's
// readCPUTemperature(), which branches on sd_softdevice_is_enabled()). This sketch always has
// the SoftDevice enabled once bleInit() has run, so the SVC path is the only correct one here.
//
// The installed core's own readCPUTemperature() helper calls sd_temp_get() with no HFXO request
// around it at all. Per the nRF52840 datasheet, TEMP's specified accuracy assumes HFXO is
// running; the SoftDevice's own sd_temp_get() doc only promises a blocking measurement, not that
// it requests HFXO for you. Requesting/releasing HFXO here is ordinary SoftDevice clock-request
// API usage (sd_clock_hfclk_request/is_running/release) -- a different mechanism entirely from
// LFCLK/RTC1, and does NOT touch NRF_CLOCK directly. See docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md
// for why direct NRF_CLOCK manipulation from a sketch is the thing that must never happen again;
// this function never does that.
bool readBoardTempQuarterC(int16_t &result)
{
  if (sd_clock_hfclk_request() != NRF_SUCCESS) return false;

  bool hfclkRunning = false;
  uint32_t pollStart = millis();
  do
  {
    uint32_t isRunning = 0;
    if (sd_clock_hfclk_is_running(&isRunning) == NRF_SUCCESS && isRunning)
    {
      hfclkRunning = true;
      break;
    }
  } while (millis() - pollStart < BOARD_TEMP_HFCLK_POLL_TIMEOUT_MS);

  bool ok = false;
  if (hfclkRunning)
  {
    int32_t tempQ4C = 0;
    if (sd_temp_get(&tempQ4C) == NRF_SUCCESS &&
        tempQ4C >= BOARD_TEMP_MIN_Q4C && tempQ4C <= BOARD_TEMP_MAX_Q4C)
    {
      result = (int16_t)tempQ4C;
      ok = true;
    }
  }

  // Balances the successful request above unconditionally -- every return path from this point
  // down (timeout, out-of-range reading, success) must still release what it acquired.
  sd_clock_hfclk_release();
  return ok;
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
// is producing trustworthy data. Returns false on an I2C failure or conversion timeout.
bool probeWake(uint8_t discardConversions)
{
  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);

  digitalWrite(PIN_FDC_SD, LOW);  // leave Shutdown
  delayMicroseconds(FDC_TSDWAKEUP_US);  // tSDWAKEUP before the part will answer I2C

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
    if (!fdcReadChannel0(d, s)) return false;
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
// still immediately recognized by Treebeard_ESP32_Display_Runtime.ino's existing
// company-ID/magic check -- only the receiver's raw-field parsing needs a
// new length-gated branch (see that file's onResult(), MSV2_RAW_PACKET_WIDE_LEN).
//
// [0-1]   companyId (0xFFFF)
// [2-5]   magic "MSV2"
// [6]     sensorId
// [7-9]   wake, 24-bit LE
// [10-13] raw FDC2214 DATA_CH0, 32-bit LE
// [14-15] raw battery mV, 16-bit LE
// [16]    flags: bit0=charging (existing meaning, unchanged), bit1=moisture/reference phase
//         used (0/1), bit2=board temp valid, bit3=bare-tank reference valid,
//         bit4=OTA power gate open (USB PG or VBAT >= OTA_MIN_BATTERY_MV), bits5-7=reserved/0
// [17-18] board temperature, signed int16 LE, quarter-degrees C (sd_temp_get() native units;
//         0/invalid when bit2 is clear) -- see readBoardTempQuarterC(). This is nRF52840 die
//         temperature, NOT soil or blade temperature.
// [19-22] bare-tank FDC2214 DATA_CH0, uint32 LE (0/invalid when bit3 is clear) -- reading taken
//         with the mux on EC-check code 2|phase, disconnecting the electrodes so the FDC is
//         oscillating on the bare L2/C17 tank alone. Board/tank-adjacent reference, NOT soil
//         temperature or a moisture reading.
//
// 23 total bytes. First 17 bytes are byte-for-byte unchanged from the prior wire format --
// Treebeard_ESP32_Display_Runtime.ino's existing MSV2_RAW_PACKET_WIDE_LEN=17 parse path (including
// its `flags & 0x01` charging read) still works unmodified against this packet; only a receiver
// that specifically checks for length>=23 sees the new fields. Sized to exactly fill the 31-byte
// legacy advertising-data payload alongside the Flags (3B) and TX Power (3B) AD structures
// already added below: 3+3+(2+23)=31.
bool g_bleInited = false;
uint32_t wakeCount = 0;

// Electrode-swap phase for the NEXT wake, alternated 0/1 once per completed
// measurement attempt (success or failure) so exposure stays balanced across
// the two electrodes even through a discarded reading. Selected in loop(),
// before probeWake() -- see the file header comment and
// docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md for the settle-margin reasoning
// and its unverified-on-hardware status as of first flash.
uint8_t currentTankPhase = 0;

// OTA maintenance hold. If a central connects during the OTA advertising window, stay awake
// instead of returning to the normal sleep cycle.
//
// This only has to cover connect + service discovery + ONE write to the DFU control
// characteristic. It does NOT have to cover the transfer: resetSafeDfuWrite() sets GPREGRET=0xA8
// and system-resets, so the ~3m40s image transfer happens entirely in the bootloader, which owns
// its own timing and never returns here. 30s is therefore generous for a slow central rather than
// tight, and a failed attempt is retryable on the next advertising window.
//
// The upper bound matters because a central that connects and then does nothing holds periodic
// connection events open. It is deliberately seconds rather than minutes, and loop() now forces
// that idle peer off when the deadline expires.
const uint32_t OTA_CONNECTED_HOLD_MS = 30000;

// How long to keep advertising and actively watch for an incoming connection.
//
// 2026-09-03 bench evidence for why this exists: with only the historical 500ms of advertising and
// a SINGLE Bluefruit.connected() check at the end of it, three consecutive OTA attempts failed
// identically. The probe's own serial log showed no OTA hold was ever entered and the cycle ran
// its normal 9.96s, while the client reported "Could not get GATT services: Unreachable" ~8s after
// connecting. The client's connection completes around 0.6s after the advertisement appears --
// just past a one-shot 500ms check -- so the probe stopped advertising and carried on while the
// client sat on a link the application never acknowledged.
//
// 500ms/min is also simply a small target: the Windows BLE stack needed five 7s scan cycles just
// to SEE the probe before it could attempt a connection.
//
// COST: the application remains in this connectable window for up to five seconds per advertising
// interval. The fast-to-slow schedule immediately below reduces the planned radio events by about
// 74% versus the original all-fast implementation, but the resulting whole-board average current
// is still NOT bench-validated. The loop exits immediately once a connection is seen.
const uint32_t OTA_ADV_LISTEN_MS = 5000;

// Keep the full proven five-second OTA discovery opportunity without transmitting at 20ms for
// all five seconds. Bluefruit advertises fast for the first second, which covers both the normal
// receiver burst and the observed ~0.6s Windows connection, then automatically changes to 250ms
// for the remaining four seconds. That is roughly 66 scheduled advertising events instead of
// 250 (about 74% fewer) while preserving the same listen-window duration. The SoftDevice timeout
// is also set to the full window so an unexpected application stall cannot leave advertising on
// indefinitely.
const uint16_t ADV_FAST_INTERVAL_UNITS = 32;   // 20ms, before BLE's 0-10ms random adv delay
const uint16_t ADV_SLOW_INTERVAL_UNITS = 400;  // 250ms
const uint16_t ADV_FAST_TIMEOUT_SECONDS = 1;
const uint16_t OTA_ADV_HARD_TIMEOUT_SECONDS = OTA_ADV_LISTEN_MS / 1000;
const uint32_t OTA_UNAVAILABLE_ADV_MS = 500;
static_assert(OTA_ADV_LISTEN_MS >= 1000 && OTA_ADV_LISTEN_MS % 1000 == 0,
              "OTA_ADV_LISTEN_MS must be a whole number of seconds for the SoftDevice timeout");

ResetSafeBLEDfu bledfu;

bool g_otaServiceReady = false;
volatile uint16_t g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;

void otaConnectCallback(uint16_t connHandle)
{
  // Latched by Bluefruit's BLE event task. Polling Bluefruit.connected() alone has a boundary
  // race: a connection can complete between the loop's final poll and Advertising.stop().
  g_otaConnectionHandle = connHandle;
}

void otaDisconnectCallback(uint16_t connHandle, uint8_t reason)
{
  (void)reason;
  if (g_otaConnectionHandle == connHandle) g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
}

void bleInit()
{
  Bluefruit.autoConnLed(false);
  if (!Bluefruit.begin()) return;
  // Bench A/B on 2026-09-02: +8 dBm improved mean RSSI by 3.2 dB versus +4 dBm
  // (same fixed placement). The earlier +8 dBm incremental-current estimate was made against the
  // historical 500ms/min window; the fast-to-slow schedule above now needs a direct whole-board
  // current measurement rather than extrapolating that stale estimate.
  Bluefruit.setTxPower(8);
  Bluefruit.setName(DEVICE_NAME);

  // Registered in GATT only -- deliberately NOT added to the advertising payload. startAdv()'s
  // static_assert shows Flags(3) + TxPower(3) + mfg header(2) + mfg(23) = exactly the 31-byte
  // legacy PDU limit, so there is no room for a service UUID and the manufacturer payload stays
  // byte-for-byte unchanged. An updater finds the probe by DEVICE_NAME in the scan response,
  // which is how the 2026-09-02 proof's client located MSV3-OTA-TEST.
  g_otaServiceReady = bledfu.begin() == ERROR_NONE;

  Bluefruit.Periph.setConnectCallback(otaConnectCallback);
  Bluefruit.Periph.setDisconnectCallback(otaDisconnectCallback);
  Bluefruit.Advertising.setFastTimeout(ADV_FAST_TIMEOUT_SECONDS);

  g_bleInited = true;
}

// Flag bits for mfg[16] -- bit0 (charging) is the original, unchanged meaning.
#define ADV_FLAG_CHARGING        (1U << 0)
#define ADV_FLAG_PHASE           (1U << 1)
#define ADV_FLAG_BOARD_TEMP_OK   (1U << 2)
#define ADV_FLAG_BARE_TANK_OK    (1U << 3)
#define ADV_FLAG_OTA_POWER_OK    (1U << 4)

bool startAdv(uint32_t rawDataCh0, float rawBatteryMv, bool isCharging, bool otaPowerOk, uint8_t phaseUsed,
              bool boardTempValid, int16_t boardTempQuarterC,
              bool bareTankValid, uint32_t bareTankDataCh0)
{
  if (!g_bleInited) return false;

  uint16_t batteryMvRounded = (uint16_t)(rawBatteryMv + 0.5f);
  uint16_t boardTempField = boardTempValid ? (uint16_t)boardTempQuarterC : 0;
  uint32_t bareTankField = bareTankValid ? bareTankDataCh0 : 0;
  uint8_t mfg[23];
  // Enforce the "3+3+(2+23)=31" arithmetic from mfg[]'s own declaration comment at compile
  // time, not just in prose. The runtime addData() failure check a few lines down exists too,
  // but ENABLE_SERIAL_LOGS is false in production, which means Serial.begin() itself never
  // runs (see setup()) - there is no field-observable channel for that log to reach, so a
  // future field added to mfg[] without shrinking something else would overflow silently in
  // the field with no diagnostic anyone could actually see. Catching it here instead means
  // the build breaks for the developer making that change, before it ever reaches a probe.
  static_assert(
    3 /* Flags AD structure */ + 3 /* TxPower AD structure */ + 2 /* manufacturer-data AD header */ + sizeof(mfg) <= 31,
    "MSV3 advertising payload (Flags + TxPower + manufacturer data) exceeds the 31-byte legacy PDU limit"
  );

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
  mfg[16] = (isCharging ? ADV_FLAG_CHARGING : 0) |
            (phaseUsed ? ADV_FLAG_PHASE : 0) |
            (boardTempValid ? ADV_FLAG_BOARD_TEMP_OK : 0) |
            (bareTankValid ? ADV_FLAG_BARE_TANK_OK : 0) |
            (otaPowerOk ? ADV_FLAG_OTA_POWER_OK : 0);
  mfg[17] = (uint8_t)(boardTempField & 0xFF);
  mfg[18] = (uint8_t)((boardTempField >> 8) & 0xFF);
  mfg[19] = (uint8_t)(bareTankField & 0xFF);
  mfg[20] = (uint8_t)((bareTankField >> 8) & 0xFF);
  mfg[21] = (uint8_t)((bareTankField >> 16) & 0xFF);
  mfg[22] = (uint8_t)((bareTankField >> 24) & 0xFF);

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  bool ok = true;
  ok &= Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  ok &= Bluefruit.Advertising.addTxPower();
  ok &= Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg, sizeof(mfg));
  ok &= Bluefruit.ScanResponse.addName();
  if (!ok)
  {
    // A missing manufacturer-data structure would produce a useless sensor advertisement, and a
    // missing scan-response name would make the OTA client unable to find this probe. Reject both
    // rather than transmitting a partial packet. sizeof(mfg)=23 plus Flags(3B)+TxPower(3B) is
    // designed to exactly fill the 31-byte legacy limit; a failure usually means that arithmetic
    // or the name no longer fits, not a transient condition.
    if (ENABLE_SERIAL_LOGS)
    {
      Serial.println(F("[ERROR] BLE advertisement/scan response build failed -- not advertising"));
      Serial.flush();
    }
    return false;
  }
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(ADV_FAST_INTERVAL_UNITS, ADV_SLOW_INTERVAL_UNITS);
  return Bluefruit.Advertising.start(OTA_ADV_HARD_TIMEOUT_SECONDS);
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

  // Board (die) temperature -- sampled first, before anything FDC/battery/BLE-related, per
  // readBoardTempQuarterC()'s own requirement that HFCLK be requested/released around a single
  // clean measurement rather than interleaved with other peripheral activity.
  int16_t boardTempQuarterC = 0;
  bool boardTempValid = readBoardTempQuarterC(boardTempQuarterC);

  uint32_t cycleStartMs = millis();

  // Select this wake's electrode-swap phase while the FDC is still in
  // shutdown (probeSleep() left SD high and the mux parked on
  // MUX_SLEEP_CODE) -- switching here, before probeWake() deasserts SD,
  // means the tank isn't being actively driven across the mux transition.
  // No settle delay needed here -- see the removed-MUX_SWITCH_SETTLE_MS comment
  // near MUX_SLEEP_CODE above; the switch is done in tens of nanoseconds and
  // probeWake()'s own tSDWAKEUP/I2C/activation sequence already leaves it
  // orders of magnitude of margin before the oscillator starts.
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
  bool statHigh, pgHigh;
  readChargerPins(statHigh, pgHigh);
  bool charging = statHigh == LOW;
  bool usbPowerGood = pgHigh == LOW;
  bool batteryReadingValid = batteryMv >= 2500.0f && batteryMv <= 5000.0f;
  bool otaPowerOk = usbPowerGood ||
                    (batteryReadingValid && batteryMv >= (float)OTA_MIN_BATTERY_MV);
  bledfu.setDfuAllowed(otaPowerOk);
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
    // Bare-tank (EC-check) reference reading -- only on advertising wakes, so this stays
    // temporally paired with every transmitted moisture sample while limiting extra FDC energy
    // and electrode DC exposure. The FDC is already in shutdown (probeSleep() just ran above)
    // and the mux at MUX_SLEEP_CODE(0); this switches to the EC-check code for the phase just
    // used, takes one reading, then unconditionally returns to shutdown/code-0 before doing
    // anything else -- including on any failure path, so mux code 2/3 is never left selected
    // across sleep, BLE advertising, or an early return (there is none here; this block always
    // runs straight through to its own probeSleep() call below).
    uint8_t bareTankMuxCode = 2 | phaseUsed;  // phase 0 -> code 2, phase 1 -> code 3
    setMux(bareTankMuxCode);
    // No settle delay here either -- same removed-MUX_SWITCH_SETTLE_MS reasoning as the
    // electrode-swap mux switch above.
    bool bareWakeOk = probeWake(WAKE_DISCARD_CONVERSIONS);
    uint32_t bareTankData = 0;
    uint16_t bareTankStatus = 0;
    bool bareReadOk = bareWakeOk && fdcReadChannel0(bareTankData, bareTankStatus);
    bool bareErrWd = bareReadOk && (bareTankStatus & (1 << 11)) != 0;
    bool bareTankValid = bareReadOk && !bareErrWd;
    if (!bareTankValid) bareTankData = 0;

    if (ENABLE_SERIAL_LOGS && bareReadOk)
    {
      bool bareErrAhw = (bareTankStatus & (1 << 10)) != 0;
      bool bareErrAlw = (bareTankStatus & (1 << 9)) != 0;
      if (bareErrAhw || bareErrAlw)
      {
        // Logged only, per spec: amplitude flags do not invalidate the bare-tank reading --
        // retained for initial characterization unless bench evidence shows it's unusable.
        Serial.print(F("[WARN] Bare-tank FDC status"));
        if (bareErrAhw) Serial.print(F(" ERR_AHW(amplitude-too-high)"));
        if (bareErrAlw) Serial.print(F(" ERR_ALW(amplitude-too-low)"));
        Serial.println(F(" -- retaining raw result"));
        Serial.flush();
      }
    }

    // Unconditional, even on failure -- immediately returns the mux to code 0 and shuts the FDC
    // back down. Reuses the exact same safety-critical sequence probeSleep() already performs
    // for the normal moisture reading (see its own comment on the ~33uA EC-check-code leak risk
    // in wet soil).
    probeSleep();

    wakeCount++;

    if (ENABLE_SERIAL_LOGS)
    {
      char buf[220];
      snprintf(buf, sizeof(buf),
               "[DIAG] wake=%lu%s phase=%d DATA_CH0=%lu status=0x%X "
               "tempQ4C=%d tempValid=%d bareDATA_CH0=%lu bareStatus=0x%X bareValid=%d "
               "vbat_mV=%d charging=%d",
               (unsigned long)wakeCount, bigSwing ? " (swing)" : "", (int)phaseUsed,
               (unsigned long)rawData, fdcStatus,
               (int)boardTempQuarterC, boardTempValid ? 1 : 0,
               (unsigned long)bareTankData, bareTankStatus, bareTankValid ? 1 : 0,
               (int)(batteryMv + 0.5f), charging ? 1 : 0);
      Serial.println(buf);
      Serial.flush();
    }

    // Clear the callback latch before advertising begins. There cannot be a legitimate existing
    // connection here: every non-DFU connection is now explicitly disconnected below before the
    // probe returns to its sleep cadence.
    g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
    bool advStarted = startAdv(rawData, batteryMv, charging, otaPowerOk, phaseUsed,
                               boardTempValid, boardTempQuarterC,
                               bareTankValid, bareTankData);

    if (advStarted)
    {
      // If DFU service registration ever failed, preserve normal sensor telemetry but don't pay a
      // five-second OTA window for a service that cannot accept an update. A healthy build uses
      // the full OTA_ADV_LISTEN_MS duration.
      uint32_t listenMs = g_otaServiceReady ? OTA_ADV_LISTEN_MS : OTA_UNAVAILABLE_ADV_MS;
      uint32_t advStart = millis();
      uint16_t connectionHandle = BLE_CONN_HANDLE_INVALID;
      while ((millis() - advStart) < listenMs)
      {
        feedWatchdog();
        uint16_t latchedHandle = g_otaConnectionHandle;
        if (latchedHandle != BLE_CONN_HANDLE_INVALID && Bluefruit.connected(latchedHandle))
        {
          connectionHandle = latchedHandle;
          break;
        }
        if (Bluefruit.connected())
        {
          connectionHandle = Bluefruit.connHandle();
          break;
        }
        delay(20);
      }

      // Stop first, then make one final connection check. This closes the old end-of-window race:
      // after stop() succeeds no new connection can begin, while a connection that won the race
      // makes advertising stop itself and is visible through the callback/connection table.
      if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
      if (connectionHandle == BLE_CONN_HANDLE_INVALID)
      {
        delay(1);  // give Bluefruit's higher-priority event task one scheduling point
        uint16_t latchedHandle = g_otaConnectionHandle;
        if (latchedHandle != BLE_CONN_HANDLE_INVALID && Bluefruit.connected(latchedHandle))
        {
          connectionHandle = latchedHandle;
        }
        else if (Bluefruit.connected())
        {
          connectionHandle = Bluefruit.connHandle();
        }
      }

      // OTA maintenance hold -- see OTA_CONNECTED_HOLD_MS. If the DFU write lands,
      // resetSafeDfuWrite() resets into the bootloader and this code never resumes. If the peer is
      // idle or the handshake fails, explicitly disconnect it at the deadline; the old code left
      // that connection alive indefinitely, keeping periodic connection events (and therefore the
      // radio) running throughout later "sleep" cycles and preventing reliable new advertising.
      if (connectionHandle != BLE_CONN_HANDLE_INVALID)
      {
        uint32_t holdStart = millis();
        while (Bluefruit.connected(connectionHandle) &&
               (millis() - holdStart) < OTA_CONNECTED_HOLD_MS)
        {
          feedWatchdog();
          delay(50);
        }

        bool forcedDisconnect = Bluefruit.connected(connectionHandle);
        if (forcedDisconnect)
        {
          Bluefruit.disconnect(connectionHandle);
          uint32_t disconnectStart = millis();
          while (Bluefruit.connected(connectionHandle) && millis() - disconnectStart < 500)
          {
            feedWatchdog();
            delay(10);
          }
        }

        if (ENABLE_SERIAL_LOGS)
        {
          Serial.print(F("[DIAG] OTA hold ended after "));
          Serial.print(millis() - holdStart);
          Serial.println(forcedDisconnect ? F("ms (idle peer disconnected)") : F("ms"));
          Serial.flush();
        }
      }

      // Only advance the cadence/baseline after the SoftDevice actually accepted the advertising
      // start. The old code silently suppressed retries for a full minute after any payload or
      // start failure even though no packet had gone over the air.
      lastAdvertiseMs = millis();
      lastAdvertisedRaw = (long)rawData;
    }
    else if (ENABLE_SERIAL_LOGS)
    {
      Serial.println(F("[ERROR] Advertising failed to start -- retaining cadence for retry"));
      Serial.flush();
    }
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
