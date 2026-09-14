// Interactive bring-up/diagnostic console for the real, assembled MSV3 probe PCB
// (not the bare bench E73 module MSV3_E73_LED_Test targeted) -- covers everything
// testable over USB power alone: status LED, FDC2214 sensing IC + LC tank, the
// TMUX1109 electrode-swap/EC-check mux, and the battery-voltage-divider readback
// path. Charger (BQ25170), harvester (BQ25504), and the battery/solar power mux
// (Q1) are NOT exercised here -- they need a battery and/or solar input present,
// which this bench session doesn't have.
//
// Pin map below is derived directly from the real schematic netlist
// (C:\Users\Public\Documents\Altium\MSV3\..._sch.NET) cross-referenced against
// Ebyte's E73-2G4M08S1C module pin table (module pin# -> nRF52840 port/pin),
// then against adafruit:nrf52 core 1.7.0's pca10056 variant, whose
// g_ADigitalPinMap[] is a straight identity map (Arduino pin N == P0.N for
// N<32, P1.(N-32) for N>=32) -- so these Arduino pin numbers are exact, not
// approximate.
//
// REQUIRED build path: use scripts/arduino_msv3_probe.ps1. It applies the
// flags below, verifies/repairs the installed variant's LFRC selection, and
// forces a clean build so a stale LFXO-compiled Bluefruit object cannot be
// uploaded. Direct Arduino IDE/raw arduino-cli builds deliberately fail at
// the compile-time guard below if the stock PCA10056 variant is still active.
// P0.09/P0.10 default to NFC antenna function on this
// chip, not GPIO, until this is set -- confirmed against this core's
// system_nrf52840.c, which self-heals the UICR and reboots once on first run
// with this flag, then behaves as plain GPIO forever after):
//   powershell -ExecutionPolicy Bypass -File \
//     scripts/arduino_msv3_probe.ps1 -Action compile
// Board FQBN must stay adafruit:nrf52:pca10056 to match the exact
// bootloader/SoftDevice already flashed onto this chip (see docs/SWD_FLASHER.md).
//
// CORRECTED 2026-08-21 (was wrong and caused a multi-hour hang chase -- see
// docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md for the full story): this core's
// FreeRTOS port (freertos/portable/CMSIS/nrf52/port_cmsis_systick.c) uses
// RTC1 -- an LFCLK-driven peripheral -- as its tick source UNCONDITIONALLY,
// despite the filename. delay()/millis() do NOT run off SysTick on this
// core. This board has no populated 32.768kHz LF crystal (P0.00/P0.01 are
// reused as VDIV/spare per the netlist), so LFCLK source selection matters
// a great deal and must be set correctly in the board variant (USE_LFRC,
// enforced by scripts/arduino_msv3_probe.ps1) BEFORE FreeRTOS/RTC1 start --
// never reconfigure LFCLK from inside a sketch's setup(), which runs after
// the scheduler is already relying on it.

#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include <nrf_nvic.h>

#ifdef MSV3_SENSOR_ID
// A targeted diagnostic image can outlive USB access once a probe is coated. Reuse the exact
// reset-safe DFU implementation exercised by production rather than maintaining a third copy of
// this security-sensitive handoff. The guarded wrapper compiles from
// MSV3/.arduino-build/msv3-probe/sketch/, so this path deliberately resolves from that generated
// translation-unit location back to the canonical production source. Raw IDE builds are already
// forbidden for MSV3 by the LFRC/linker guards above.
#include "../../../arduino/MSV3_Moisture_BLE_Sleep/ResetSafeBLEDfu.cpp"
#endif

// This is a compile-time safety property, not merely documentation. Both
// wiring.c (FreeRTOS/RTC1 boot) and Bluefruit.begin() consume these variant
// macros. LFXO on this crystal-less PCB can hang SoftDevice initialization
// until the watchdog resets the board and USB re-enumerates.
#if !defined(USE_LFRC) || defined(USE_LFXO)
#error "MSV3 requires USE_LFRC and no USE_LFXO. Build with scripts/arduino_msv3_probe.ps1; see docs/KNOWN_ISSUES.md."
#endif

// ---- Status LED (D2 via R16 470R to GND, active HIGH) ----
#define LED_BIT (1UL << 10)  // P1.10

// ---- I2C to FDC2214 (4.7k pullups already on-board, R4/R5) ----
#define PIN_SDA 28  // P0.28
#define PIN_SCL 45  // P1.13 (32 + 13)

// ---- FDC2214 control pins ----
#define PIN_FDC_SD   9   // P0.09 / NFC1 -- SD: HIGH=shutdown, LOW=active
#define PIN_FDC_INTB 10  // P0.10 / NFC2 -- INTB: active-low data-ready/error

// ---- TMUX1109 electrode-swap / EC-check select ----
#define PIN_MUX_A0 30  // P0.30 / AIN6 -> MUX0 (TMUX1109 pin 1)
#define PIN_MUX_A1 31  // P0.31 / AIN7 -> MUX1 (TMUX1109 pin 16)

// ---- Battery voltage divider (100k/100k, R29/R30). Divider topology is
// VBAT --[R29]-- VBAT_DIV --[R30]-- this pin -- it's the divider's ground
// return, not a high-side enable, so it must be driven LOW during a read
// (see doVbat()), not HIGH.
#define PIN_VBAT_DIV_EN 29  // P0.29 / AIN5
#define PIN_VBAT_DIV    2   // P0.02 / AIN0 -- analogRead here

// ---- EC-check divider midpoint (DIV_NODE in the design doc). Originally
// wired to P0.00 (module pin 11) -- free since no LF crystal is populated,
// but NOT AIN-capable on nRF52840 (SAADC/COMP/LPCOMP are hard-restricted to
// AIN0-AIN7), so that wiring could only ever be a digital threshold, never
// the graded EC reading the design actually wants. Bodged 2026-08-28 to
// P0.03/AIN1 (module pin 3, castellated edge pad, otherwise unused on this
// board) per the fix plan in docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 5.
#define PIN_DIV_NODE 3  // P0.03 / AIN1 -- analogRead here now

// The old trace to P0.00 (module pin 11) is still physically present on the
// PCB in parallel with the new P0.03 bodge -- confirmed in the real netlist
// (VDIV net members: MUX-7, MUX-11, R17-2, U1-11, VDIV-1). The fix plan
// always expected this ("leaving the existing trace in place is fine
// electrically... firmware just needs to stop reading it"), but repointing
// PIN_DIV_NODE away from 0 silently dropped the setup()-time pinMode() that
// used to keep P0.00 an inert input -- left fully unconfigured, it can sit
// in whatever state the bootloader left it in, which measured as a real
// ~3.1V, present regardless of mux code (confirmed 2026-08-28: pegged
// full-scale on P0.03's ADC in all 4 mux codes, including codes 0/1 where
// the mux doesn't route anything to VDIV at all -- ruling out the mux/
// bridging entirely and pointing at this pin instead). Must stay explicitly
// gpioDisconnect()'d, not just left alone.
#define PIN_DIV_NODE_OLD 0  // P0.00 -- still wired to VDIV in parallel, disconnect only

// ---- BQ25170 charger status pins -- resolved via the pcb-reference library
// 2026-08-21 (previously unidentified nets in this project's own notes for
// weeks). Both open-drain, active LOW per the BQ25170 datasheet.
#define PIN_CHG_STAT 5   // P0.05/AIN3 -- LOW=charging, HIGH=complete/disabled
#define PIN_CHG_PG   41  // P1.09 -- LOW=input power good

// Forward declarations -- these live in the sleep/wake power-management
// section further down, but are used by doChgStatus()/doBleCycle() above it.
static inline void gpioDisconnect(uint32_t arduinoPin);
void probeSleep();
bool probeWake(uint8_t discardConversions);
void readChargerPins(bool &statHigh, bool &pgHigh);
bool isBatteryCharging();

uint8_t fdcAddr = 0x2A;  // ADDR is FDC2214 pin 4 (WQFN-16 RGH package -- verified against the real
                          // datasheet pin table, not the earlier guess of pin 8), tied to GND per
                          // netlist -> 0x2A per datasheet. Empirically confirmed via i2cscan too.

// Forensic breadcrumb for the still-unresolved I2C hang: stamped before/after
// every individual Wire call in fdcWriteReg/fdcReadReg so that if the CPU
// locks up, reading these three words over SWD (they're plain SRAM, readable
// live without halting -- see the "rd <addr>" SWD console command) shows
// EXACTLY which underlying Wire call (beginTransmission/write/
// endTransmission/requestFrom/read) was in flight and never returned, and
// how many prior I2C transactions completed cleanly before it happened.
// Step codes: 1/2/3 = fdcWriteReg before-begin/before-end/after-end,
// 10/11/12/13/14 = fdcReadReg before-begin/before-end(false)/before-
// requestFrom/before-read/complete, 90/91 = fdcReadReg's own two early-exit
// failure returns (endTransmission or requestFrom reported failure, not a
// hang -- distinguishes "it errored cleanly" from "it's still stuck here").
volatile uint32_t g_i2cStep = 0;
volatile uint32_t g_i2cStepMs = 0;
volatile uint32_t g_i2cCallCount = 0;

// ---------------------------------------------------------------- LED ----
static inline void ledWrite(bool on) {
  if (on) NRF_P1->OUTSET = LED_BIT;
  else NRF_P1->OUTCLR = LED_BIT;
}

// ------------------------------------------------------------- FDC2214 ----
bool fdcWriteReg(uint8_t reg, uint16_t val) {
  g_i2cStep = 1; g_i2cStepMs = millis();
  Wire.beginTransmission(fdcAddr);
  Wire.write(reg);
  Wire.write((uint8_t)(val >> 8));
  Wire.write((uint8_t)(val & 0xFF));
  g_i2cStep = 2; g_i2cStepMs = millis();
  bool ok = (Wire.endTransmission() == 0);
  g_i2cStep = 3; g_i2cStepMs = millis();
  g_i2cCallCount++;
  return ok;
}

bool fdcReadReg(uint8_t reg, uint16_t &val) {
  g_i2cStep = 10; g_i2cStepMs = millis();
  Wire.beginTransmission(fdcAddr);
  Wire.write(reg);
  g_i2cStep = 11; g_i2cStepMs = millis();
  if (Wire.endTransmission(false) != 0) { g_i2cStep = 90; g_i2cCallCount++; return false; }  // repeated start, no stop
  g_i2cStep = 12; g_i2cStepMs = millis();
  if (Wire.requestFrom((uint8_t)fdcAddr, (uint8_t)2) != 2) { g_i2cStep = 91; g_i2cCallCount++; return false; }
  g_i2cStep = 13; g_i2cStepMs = millis();
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  val = (uint16_t)((hi << 8) | lo);
  g_i2cStep = 14; g_i2cStepMs = millis();
  g_i2cCallCount++;
  return true;
}

// Datasheet-verified register config for single-channel (CH0) free-running
// conversion. Values derived from TI SNOSCZ5B (fetched/read this session),
// not recalled from memory -- see chat log for the bit-by-bit derivation.
// 2026-08-30: ported the same three review-driven fixes as the production sketch
// (MSV3_Moisture_BLE_Sleep.ino), so wakeprofile here actually profiles the config that ships,
// not the old one. See that file's header comment for the full datasheet backing.
bool fdcInit() {
  bool ok = true;
  ok &= fdcWriteReg(0x08, 0xFFFF);  // RCOUNT_CH0: was 0x0400 (~14 ENOB); 0xFFFF ~24.2ms, near-max resolution
  ok &= fdcWriteReg(0x10, 0x0064);  // SETTLECOUNT_CH0: tS0 = 100*16/fREF0 (~37us) -- default 0 is too short, causes amplitude warnings
  ok &= fdcWriteReg(0x14, 0x1001);  // CLOCK_DIVIDERS_CH0: FIN_SEL=01 (divide-by-1, 0.01-8.75MHz range), FREF_DIVIDER=1
  ok &= fdcWriteReg(0x1E, 0xC800);  // DRIVE_CURRENT_CH0: IDRIVE=11001b (code 25, 0.644mA) -- bench-tuned
                                    // 2026-09-03 against a scope (was 0x8000 / code 25's predecessor code 16,
                                    // ~0.169mA, which ran at ~0.5Vpk dry: a THIRD of the 1.2Vpk VSENSORMIN).
                                    // Code 25 measures 1.9Vpk dry / 1.3Vpk under heavy load. Changing this
                                    // shifts DATA_CH0 by ~0.2%; see MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 4.
  // MUX_CONFIG (0x1B) left at its power-on-reset default (0x020F): AUTOSCAN_EN=0
  // (single-channel CH0 mode, correct for this board -- only CH0 is wired to
  // real hardware, CH1-3 are grounded per netlist), DEGLITCH=111 (33MHz, safely
  // above the ~1.3-4.1MHz design target).
  // ERROR_CONFIG (0x19), added 2026-09-05 alongside the same fix in MSV3_Moisture_BLE_Sleep.ino,
  // whose fdcInit() carries the full rationale and the Table 7-37 bit map. Short version: this
  // register resets to zero on every hardware shutdown, and its WD_ERR2INT bit (bit 5) is what
  // permits STATUS.ERR_WD to be updated at all. Without this write, `fdcstatus` and every
  // amplitude/oscillation diagnostic in this sketch reports a watchdog-clean part no matter how
  // dead the tank is -- the flag is switched off, not clear. DRDY_2INT (bit 0) stays 0 on
  // purpose; see the CH0_UNREADCONV comment below.
  ok &= fdcWriteReg(0x19, 0x0020);  // ERROR_CONFIG: WD_ERR2INT only
  ok &= fdcWriteReg(0x1A, 0x1401);  // CONFIG: was 0x1C01 (SENSOR_ACTIVATE_SEL=1, Low Power) --
                                    // now b0, Full Current Activation Mode, TI's recommendation.
                                    // Every other bit already matched its required value.
  return ok;
}

bool fdcReadStatus(uint16_t &status) { return fdcReadReg(0x18, status); }

// CH0_UNREADCONV, Address 0x18 STATUS bit 3 (Table 7-36): an unread conversion is present for
// channel 0, and DATA_CH0 should be read to retrieve it. Poll this per-channel flag rather than
// DRDY: ERROR_CONFIG.DRDY_2INT resets to b0 after every hardware shutdown, and Table 7-37 says
// that setting prevents STATUS.DRDY from being updated. Since 2026-09-05 fdcInit() DOES write
// ERROR_CONFIG (0x19), but deliberately leaves DRDY_2INT at 0 -- it enables WD_ERR2INT and
// nothing else. So STATUS.DRDY is still never updated on this board and a DRDY poll still times
// out forever -- every fdcread/fdcstream/idrive/muxsweep then prints the shared "I2C read failed"
// despite a perfectly healthy bus. The tell is that `fdcid` still reads 0x7E/0x7F correctly
// while every conversion read fails.
// 2026-09-03: ported from MSV3_Moisture_BLE_Sleep.ino, which hit and fixed this exact bug on
// 2026-08-30 (there it timed out forever and suppressed every BLE advertisement). The bring-up
// sketch kept the broken DRDY poll until now, so this is the third time this project has been
// bitten by it -- see MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md and
// MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md. Do not "simplify" this back to STATUS.DRDY without
// also explicitly writing and verifying ERROR_CONFIG.DRDY_2INT after every shutdown reset.
#define FDC_STATUS_CH0_UNREADCONV_BIT 0x0008
// Generous bound over the RCOUNT-driven worst-case conversion time (see fdcInit()'s RCOUNT_CH0
// comment, ~24.2ms at RCOUNT=0xFFFF) -- a fail-fast ceiling for a genuinely stuck part or a tank
// that truly isn't oscillating, not a value tuned to be tight.
#define FDC_CONVERSION_POLL_TIMEOUT_MS 60

bool fdcWaitForCh0UnreadConversion(uint16_t &statusOut) {
  uint32_t start = millis();
  do {
    if (!fdcReadStatus(statusOut)) return false;
    if (statusOut & FDC_STATUS_CH0_UNREADCONV_BIT) return true;
  } while (millis() - start < FDC_CONVERSION_POLL_TIMEOUT_MS);
  return false;  // no fresh conversion -- same failure path as a real I2C error
}

// 2026-08-30: waits for DRDY and reads STATUS before DATA, not after -- a latched error is
// cleared by reading either STATUS or DATA_CHx (Table 7-36), so the old DATA-then-STATUS order
// could silently lose a real ERR_WD/ERR_AHW/ERR_ALW before STATUS ever saw it.
bool fdcReadChannel0(uint32_t &data28, uint16_t &statusOut) {
  if (!fdcWaitForCh0UnreadConversion(statusOut)) return false;
  uint16_t msb, lsb;
  if (!fdcReadReg(0x00, msb)) return false;  // must read MSB before LSB (latches result)
  if (!fdcReadReg(0x01, lsb)) return false;
  data28 = ((uint32_t)(msb & 0x0FFF) << 16) | lsb;
  return true;
}

// Appends onto whatever's already in buf (caller writes the DATA_CH0 part
// first) -- snprintf into a fixed buffer, zero heap allocation. Replaces an
// earlier String-concatenation version: repeated String '+' in a tight,
// hundreds-of-iterations loop fragments this chip's small heap over a
// stream, eventually failing an allocation -- confirmed as the real cause
// of the "dies around 100-200 samples" symptom, not the TX-buffer issue
// that was fixed first and helped but didn't fully solve it.
void appendFdcStatus(char *buf, size_t bufSize, uint16_t status) {
  size_t len = strlen(buf);
  len += snprintf(buf + len, bufSize > len ? bufSize - len : 0, "STATUS=0x%X", status);
  if (status & (1 << 6)) len += snprintf(buf + len, bufSize > len ? bufSize - len : 0, " DRDY");
  if (status & (1 << 3)) len += snprintf(buf + len, bufSize > len ? bufSize - len : 0, " CH0_UNREAD");
  if (status & (1 << 11)) len += snprintf(buf + len, bufSize > len ? bufSize - len : 0, " ERR_WD(watchdog-no-oscillation!)");
  if (status & (1 << 10)) len += snprintf(buf + len, bufSize > len ? bufSize - len : 0, " ERR_AHW(amplitude-too-high)");
  if (status & (1 << 9)) snprintf(buf + len, bufSize > len ? bufSize - len : 0, " ERR_ALW(amplitude-too-low)");
}

// This core's Serial.print()/println() BLOCKS INDEFINITELY if the native USB
// CDC transmit FIFO stays full (confirmed by reading Adafruit_USBD_CDC.cpp's
// write(): `while (remain && tud_cdc_n_connected(...)) { ... yield(); }`, no
// timeout at all) -- a slow/lagging host (e.g. a terminal app's UI stalling
// for a moment) can freeze the entire firmware permanently, not just the
// print. availableForWrite() checks free FIFO space without blocking, so
// every repeated/streaming print goes through this instead of a bare
// Serial.println() -- skipping a line beats hanging the device.
uint32_t g_lastSuccessfulOutputMs = 0;

bool safePrintln(const char *line) {
  if (Serial.availableForWrite() < (int)(strlen(line) + 2)) {
    return false;
  }
  Serial.println(line);
  g_lastSuccessfulOutputMs = millis();
  return true;
}

// Two DIFFERENT failure modes turned out to be in play here, confirmed by
// testing -- a fixed-buffer rewrite (removing all heap allocation from this
// path) still hung at nearly the same point, which rules out heap
// fragmentation as the cause and points squarely at TinyUSB's CDC write
// path itself (the confirmed-but-unpatched upstream race in
// tud_cdc_n_write_flush(), see the watchdog comment below).
//
// The critical realization: if that TX path gets genuinely, permanently
// wedged (not just briefly congested), the CPU keeps running fine forever
// -- it's just endlessly retrying a print that will never succeed. A
// hardware watchdog CANNOT catch this: it only detects "the CPU stopped
// executing," and this loop keeps executing just fine, so feeding the
// watchdog from inside a doomed retry (an earlier version of this function
// did exactly that) actively defeats the watchdog's own safety net.
//
// Fix: track wall-clock time since the last successful write, globally,
// across every call to this function -- not just within one call's own
// retry budget. If NO output has gotten out for OUTPUT_DEADMAN_MS, the TX
// path is presumed permanently dead and this forces an actual reset
// directly, rather than waiting on a hardware mechanism that structurally
// can't see this particular failure.
#define OUTPUT_DEADMAN_MS 4000UL

bool printLineResilient(const char *line, uint32_t maxWaitMs = 2000) {
  uint32_t start = millis();
  while (!safePrintln(line)) {
    if (millis() - g_lastSuccessfulOutputMs > OUTPUT_DEADMAN_MS) {
      NVIC_SystemReset();  // never returns
    }
    if (millis() - start > maxWaitMs) return false;
    delay(50);
  }
  return true;
}

// Hardware watchdog: a real, confirmed-but-never-fully-fixed upstream bug
// exists in this core's bundled TinyUSB (a race in tud_cdc_n_write_flush()
// under sustained CDC write volume -- see
// https://github.com/adafruit/Adafruit_nRF52_Arduino/issues/565, patch
// proposed but not merged into this core's vendored copy) that can wedge
// Serial output, and by extension this single-threaded sketch's whole
// command loop, even with safePrintln()'s guard above reducing how often
// it's hit. Rather than trust any from-the-sketch mitigation to be
// bulletproof against a bug in someone else's C code, WDT gives an
// unconditional recovery: if anything hangs for more than WDT_TIMEOUT_MS,
// the chip resets itself and comes back up clean, no manual unplug needed.
// nRF52840's WDT counts LFCLK ticks -- this board has no external 32.768kHz
// crystal populated, so LFCLK must be running from the internal RC source
// for the watchdog's counter to advance at all. That selection now happens
// in the board variant (variants/pca10056/variant.h's USE_LFRC), not here --
// see docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md for why doing it from inside
// setup() (the original approach) actively broke FreeRTOS's RTC1-based
// tick generator instead of just being a redundant no-op.
#define WDT_TIMEOUT_MS 5000UL

bool g_lfclkRcOk = false;

bool initWatchdogSafety() {
  // CORRECTED (2026-08-21): this used to stop/reconfigure/restart LFCLK
  // itself, here in setup(). That was wrong and is the real root cause a
  // long bring-up session chased -- by the time setup() runs, FreeRTOS's
  // scheduler and its RTC1-based tick generator (see
  // freertos/portable/CMSIS/nrf52/port_cmsis_systick.c -- it always uses
  // RTC1, regardless of tickless-idle settings) are ALREADY running against
  // whatever LFCLK was present at boot. Stopping and restarting that clock
  // out from under an already-running RTC1 corrupted its tick generation --
  // confirmed as the actual cause of every "hangs after ~120 I2C reads,
  // stuck in delay(), watchdog doesn't save it" symptom this session, not
  // heap fragmentation, not Wire's unbounded waits, not tickless idle
  // itself (all three were real bugs, correctly fixed, and none of them
  // were this). The real fix is selecting RC before any of that starts --
  // now done in the board variant (variant.h's USE_LFRC, not USE_LFXO) so
  // wiring.c's own init() configures it correctly from the very first
  // instruction. This function now only verifies that took effect.
  g_lfclkRcOk = (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) != 0 &&
                (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_SRC_Msk) == CLOCK_LFCLKSTAT_SRC_RC;

  if (!g_lfclkRcOk) {
    // Don't arm a watchdog that can't actually count -- CRV would never
    // advance and it would silently never fire, same failure as before.
    return false;
  }

  // HALT=RunHalt (not the default-looking Pause) so a debug/SWD session
  // connecting to check on a hung target doesn't itself suspend the one
  // thing that's supposed to recover it -- found via direct SWD readback of
  // xTickCount showing a genuinely frozen CPU during this exact bring-up.
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Run << WDT_CONFIG_HALT_Pos) |
                     (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = (WDT_TIMEOUT_MS * 32768UL) / 1000UL;  // CRV is in LFCLK (32.768kHz) ticks
  NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
  return true;
}

void feedWatchdog() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

// delay() in chunks small enough to stay well under WDT_TIMEOUT_MS (5000ms),
// feeding the watchdog between chunks -- needed for doBleCycle() below,
// whose per-cycle sleep (up to ~BLE_WAKE_PERIOD_MS, 10s) is longer than the
// watchdog timeout. A single feedWatchdog() at the top of each cycle isn't
// enough; this is what actually starved it the first time (reset reason
// 0x2 WATCHDOG, mid-cycle, confirmed via wdtstatus). Each chunk is still a
// real delay()/vTaskDelay(), so this preserves the tickless-idle sleep
// behavior being tested -- it's just broken into WDT-safe pieces.
void delayFeedingWatchdog(uint32_t ms) {
  const uint32_t CHUNK_MS = 1000;
  while (ms > CHUNK_MS) {
    feedWatchdog();
    delay(CHUNK_MS);
    ms -= CHUNK_MS;
  }
  feedWatchdog();
  delay(ms);
}

// ---------------------------------------------------------------- BLE ----
// Ports the wake/measure/advertise/sleep workflow already proven on MSV2
// (arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino) onto this
// board, as new console commands rather than replacing the interactive
// bring-up loop. Advertise-only, same as MSV2 -- no GATT peripheral, no
// connections; a scanner (scripts/ble_periodic_reader.py or a receiver)
// just reads manufacturer-specific data out of the advertisement.
//
// Deliberately uses a DIFFERENT magic ("MSV3" not "MSV2") from the real
// product line. Treebeard_ESP32_Display_Runtime.ino's receiver
// only recognizes packets starting with its own MSV2_MAGIC[]={'M','S','V','2'}
// before it ever looks at sensorId -- this bring-up firmware physically
// cannot be mistaken for real plant data even if a production receiver
// happens to be running nearby while this is on the bench.
//
// Bluefruit.begin() reads the same USE_LFXO/USE_LFRC macro this session's
// LFCLK/RTC1 fix already patched into variants/pca10056/variant.h (see
// docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md) -- bluefruit.cpp's own begin()
// is `#if defined(USE_LFXO) ... #elif defined(USE_LFRC)`, so it picks up
// NRF_CLOCK_LF_SRC_RC automatically, no separate clock config needed here.
const uint16_t BLE_COMPANY_ID = 0xFFFF;
const uint8_t BLE_MAGIC[4] = {'M', 'S', 'V', '3'};

// A fleet diagnostic image needs the same immutable identity contract as the production image:
// ble_ota_update.py reads the NUL-terminated name directly from the signed application before it
// will touch a probe. The ordinary untargeted bench build keeps its historical bring-up name and
// id 99. arduino_msv3_probe.ps1 is the only supported way to provide MSV3_SENSOR_ID/BUILD_TAG.
#define MSV3_STRINGIFY_(x) #x
#define MSV3_STRINGIFY(x) MSV3_STRINGIFY_(x)

#ifdef MSV3_SENSOR_ID
static_assert(MSV3_SENSOR_ID >= 0 && MSV3_SENSOR_ID <= 255,
              "MSV3_SENSOR_ID must fit in the diagnostic advertisement byte");
const uint8_t BLE_TEST_SENSOR_ID = MSV3_SENSOR_ID;
// Keep this as an array initializer rather than a constexpr helper function. Arduino's sketch
// preprocessor emits function prototypes before local type declarations, which makes a helper
// returning a sketch-defined struct fail before the real C++ compiler can see the declaration.
constexpr char BLE_DEVICE_NAME[] = {
  'M', 'S', 'V', '3', '-', 'M', 'O', 'I', 'S', 'T', 'U', 'R', 'E', '-',
  static_cast<char>('0' + (BLE_TEST_SENSOR_ID / 100) % 10),
  static_cast<char>('0' + (BLE_TEST_SENSOR_ID / 10) % 10),
  static_cast<char>('0' + BLE_TEST_SENSOR_ID % 10), '\0'
};
#ifdef MSV3_BUILD_TAG
#define MSV3_BRINGUP_BUILD_ID "MSV3-BUILD:" MSV3_STRINGIFY(MSV3_BUILD_TAG)
#else
#define MSV3_BRINGUP_BUILD_ID "MSV3-BUILD:bringup-diagnostic"
#endif
const char FIRMWARE_BUILD_ID[] = MSV3_BRINGUP_BUILD_ID;
// This image intentionally does not carry production Guard 2. Its recovery invariants are the
// periodic reset-safe OTA window below and the USB `dfu` command plus resident bootloader; state
// the Guard-2 fact explicitly so package preflight never has to guess from an old/unmarked image.
const char FIRMWARE_GUARD2_MARKER[] = "MSV3-GUARD2:0";
volatile const char* g_firmwareBuildIdAnchor = nullptr;
volatile const char* g_guard2MarkerAnchor = nullptr;
#else
const char BLE_DEVICE_NAME[] = "MSV3-PROBE-BRINGUP";
const uint8_t BLE_TEST_SENSOR_ID = 99;  // clearly outside MSV2's real 1-16 sensor range
#endif

// arduino_msv3_probe.ps1 deliberately links every MSV3 sketch with the same RAM contract as
// bootloader 1.0.3. This diagnostic does not count failed boots, but it must still occupy the
// eight-byte NOLOAD section so the linker can prove the stack never descends through that shared
// bootloader/application address. Do not initialize or repurpose this reserved record.
struct BootGuardRetainedReservation {
  uint32_t encoded;
  uint32_t inverse;
};
static_assert(sizeof(BootGuardRetainedReservation) == 8,
              "boot-guard retained reservation must remain exactly eight bytes");
static volatile BootGuardRetainedReservation g_bootGuardRetainedReservation
    __attribute__((section(".msv3_boot_guard_retained"), used, aligned(4)));

// Same cadence as MSV2's production wake/advertise/sleep cycle -- this is
// specifically testing that workflow, not a different one.
const uint32_t BLE_WAKE_PERIOD_MS = 10000;
const uint32_t BLE_ADVERTISE_MS = 500;
// Conversions thrown away after each cold wake before the reading is trusted.
// MEASURED, not guessed: `wakeprofile` across 4 cold wakes on this board showed
// the very first readable conversion is already inside the steady-state noise
// band (sample 0 landed at 20334031 / 20334031 / 20333284 / 20332663 against a
// normal streaming spread of ~20330177-20335274), with zero ERR_AHW/ERR_ALW/
// ERR_WD flags on that first conversion. In other words the analog front end
// is fully settled by the time data is readable at all -- the FDC2214 handles
// this internally via SETTLECOUNT plus the mandated tSDWAKEUP, so discarding
// is not required for correctness here.
// Kept at 2 as deliberate cheap insurance (~4ms) for conditions this bench
// test did NOT cover: wet soil (higher sensor C, different tank Q), temperature
// extremes, and low battery voltage. Revisit with `wakeprofile` if the tank or
// IDRIVE is ever retuned.
const uint8_t BLE_WAKE_DISCARD_CONVERSIONS = 2;

bool g_bleInited = false;
uint32_t g_bleWakeCount = 0;
const uint16_t OTA_MIN_BATTERY_MV = 3800;
const uint16_t BLE_ADV_FAST_INTERVAL_UNITS = 32;  // 20ms

#ifdef MSV3_SENSOR_ID
// Targeted diagnostic builds must remain recoverable after USB is removed or coated over. Match
// production's proven five-second-per-minute discovery opportunity and 30-second connected hold.
// The window is non-blocking while idle, so the USB console remains responsive throughout it.
const uint32_t OTA_ADV_INTERVAL_MS = 60000;
const uint32_t OTA_ADV_LISTEN_MS = 5000;
const uint32_t OTA_CONNECTED_HOLD_MS = 30000;
const uint32_t DFU_RESPONSE_SETTLE_MS = 150;
const uint16_t OTA_ADV_SLOW_INTERVAL_UNITS = 400;  // 250ms for the rest of the window
const uint16_t OTA_ADV_FAST_TIMEOUT_SECONDS = 1;
const uint16_t OTA_ADV_HARD_TIMEOUT_SECONDS = OTA_ADV_LISTEN_MS / 1000;
static_assert(OTA_ADV_LISTEN_MS >= 1000 && OTA_ADV_LISTEN_MS % 1000 == 0,
              "OTA_ADV_LISTEN_MS must be a whole number of seconds");

ResetSafeBLEDfu bledfu;
BLEDis bledis;
bool g_otaServiceReady = false;
bool g_disReady = false;
volatile uint16_t g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
uint32_t g_otaConnectionStartMs = 0;
uint32_t g_otaWindowStartMs = 0;
uint32_t g_lastOtaWindowStartMs = 0;
bool g_otaWindowActive = false;
bool g_otaWindowEverStarted = false;

void otaConnectCallback(uint16_t connHandle) {
  g_otaConnectionHandle = connHandle;
}

void otaDisconnectCallback(uint16_t connHandle, uint8_t reason) {
  (void)reason;
  if (g_otaConnectionHandle == connHandle) g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
}
#endif

void bleInitOnce() {
  if (g_bleInited) return;
  Bluefruit.autoConnLed(false);
  if (!Bluefruit.begin()) return;
  Bluefruit.setTxPower(4);
  Bluefruit.setName(BLE_DEVICE_NAME);
#ifdef MSV3_SENSOR_ID
  bledis.setManufacturer("Treebeard");
  bledis.setModel("MSV3-DIAG");
  bledis.setFirmwareRev(FIRMWARE_BUILD_ID);
  g_disReady = (bledis.begin() == ERROR_NONE);

  g_otaServiceReady = (bledfu.begin() == ERROR_NONE);
  Bluefruit.Periph.setConnectCallback(otaConnectCallback);
  Bluefruit.Periph.setDisconnectCallback(otaDisconnectCallback);
  Bluefruit.Advertising.setFastTimeout(OTA_ADV_FAST_TIMEOUT_SECONDS);
#endif
  g_bleInited = true;
}

// Mirrors MSV2_Moisture_BLE_Sleep.ino's startAdv() layout exactly (company
// ID, magic, sensor id, 24-bit wake counter, battery mV, charging flag) --
// the only change is widening the 2-byte RC-oscillator kHz field to a
// 4-byte raw FDC2214 DATA_CH0, since that reading is up to 28 bits and
// doesn't fit in 2 bytes the way MSV2's oscillator count did.
void bleStartAdv(uint32_t dataCh0, float rawBatteryMv, bool isCharging, bool otaPowerOk,
                 uint16_t slowIntervalUnits, uint16_t timeoutSeconds) {
  uint16_t batteryMvRounded = (uint16_t)(rawBatteryMv + 0.5f);
  uint8_t mfg[17];
  mfg[0] = lowByte(BLE_COMPANY_ID);
  mfg[1] = highByte(BLE_COMPANY_ID);
  memcpy(&mfg[2], BLE_MAGIC, 4);
  mfg[6] = BLE_TEST_SENSOR_ID;
  mfg[7] = (uint8_t)(g_bleWakeCount & 0xFF);
  mfg[8] = (uint8_t)((g_bleWakeCount >> 8) & 0xFF);
  mfg[9] = (uint8_t)((g_bleWakeCount >> 16) & 0xFF);
  mfg[10] = (uint8_t)(dataCh0 & 0xFF);
  mfg[11] = (uint8_t)((dataCh0 >> 8) & 0xFF);
  mfg[12] = (uint8_t)((dataCh0 >> 16) & 0xFF);
  mfg[13] = (uint8_t)((dataCh0 >> 24) & 0xFF);
  mfg[14] = (uint8_t)(batteryMvRounded & 0xFF);
  mfg[15] = (uint8_t)((batteryMvRounded >> 8) & 0xFF);
  // bit0 retains the historical charging meaning. bit4 mirrors production's OTA power gate so
  // the identity-locked updater can distinguish an intentionally shut gate from old firmware.
  mfg[16] = (isCharging ? 0x01 : 0x00) | (otaPowerOk ? 0x10 : 0x00);

#ifdef MSV3_SENSOR_ID
  bledfu.setDfuAllowed(otaPowerOk);
#endif

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg, sizeof(mfg));
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(BLE_ADV_FAST_INTERVAL_UNITS, slowIntervalUnits);
  Bluefruit.Advertising.start(timeoutSeconds);
}

#ifdef MSV3_SENSOR_ID
bool readOtaPowerState(float& batteryMv, bool& charging) {
  batteryMv = readVbatMv(nullptr);
  bool statHigh = true;
  bool pgHigh = true;
  readChargerPins(statHigh, pgHigh);
  charging = statHigh == LOW;
  const bool usbPowerGood = pgHigh == LOW;
  const bool batteryReadingValid = batteryMv >= 2500.0f && batteryMv <= 5000.0f;
  return usbPowerGood || (batteryReadingValid && batteryMv >= (float)OTA_MIN_BATTERY_MV);
}

// Non-blocking OTA maintenance for a targeted/fleet diagnostic image. Untargeted bench builds
// deliberately keep their old on-demand BLE behavior so low-power experiments are not changed.
void servicePeriodicOtaWindow() {
  bleInitOnce();
  if (!g_bleInited || !g_otaServiceReady) return;

  uint16_t connectionHandle = g_otaConnectionHandle;
  if (connectionHandle == BLE_CONN_HANDLE_INVALID && Bluefruit.connected()) {
    connectionHandle = Bluefruit.connHandle();
    g_otaConnectionHandle = connectionHandle;
  }

  if (connectionHandle != BLE_CONN_HANDLE_INVALID && Bluefruit.connected(connectionHandle)) {
    g_otaWindowActive = false;
    if (g_otaConnectionStartMs == 0) g_otaConnectionStartMs = millis();

    // The authorize callback records the request only. Let its ATT response settle before the
    // loop performs the reset-safe handoff, exactly as the production application does.
    if (bledfu.dfuResetPending()) {
      delayFeedingWatchdog(DFU_RESPONSE_SETTLE_MS);
      bledfu.completeDfuReset();
      return;  // reached only if the reset unexpectedly failed
    }

    if (millis() - g_otaConnectionStartMs >= OTA_CONNECTED_HOLD_MS) {
      Bluefruit.disconnect(connectionHandle);
      g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
      g_otaConnectionStartMs = 0;
    }
    return;
  }
  g_otaConnectionStartMs = 0;

  if (g_otaWindowActive) {
    if (millis() - g_otaWindowStartMs >= OTA_ADV_LISTEN_MS) {
      if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
      g_otaWindowActive = false;
    }
    return;
  }

  // A user-requested `blestart` owns the advertiser until `blestop`; its GATT table still carries
  // the same DFU service, and the connection/pending-reset path above remains active.
  if (Bluefruit.Advertising.isRunning()) return;

  const uint32_t nowMs = millis();
  const bool due = !g_otaWindowEverStarted ||
                   (nowMs - g_lastOtaWindowStartMs >= OTA_ADV_INTERVAL_MS);
  if (!due) return;

  float batteryMv = 0;
  bool charging = false;
  const bool otaPowerOk = readOtaPowerState(batteryMv, charging);
  g_bleWakeCount++;
  bleStartAdv(0, batteryMv, charging, otaPowerOk,
              OTA_ADV_SLOW_INTERVAL_UNITS, OTA_ADV_HARD_TIMEOUT_SECONDS);
  g_otaWindowEverStarted = true;
  g_lastOtaWindowStartMs = nowMs;
  g_otaWindowStartMs = nowMs;
  g_otaWindowActive = Bluefruit.Advertising.isRunning();
}
#endif

// Reads real sensor data (FDC2214 must already be fdcinit'd) + battery +
// charger, builds the same manufacturer-data packet shape MSV2 sends, and
// runs the same wake/measure/advertise/sleep shape as
// MSV2_Moisture_BLE_Sleep.ino's loop() -- advertise for BLE_ADVERTISE_MS,
// then delay() the remaining wake period as the "sleep" (relies on
// FreeRTOS tickless idle, configUSE_TICKLESS_IDLE, re-enabled this session
// -- see docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md's "open follow-up").
// Deliberately bounded to n cycles rather than an infinite loop, so control
// returns to the console afterward instead of requiring a reset.
void doBleCycle(int n) {
  bleInitOnce();
  for (int i = 0; i < n; i++) {
    feedWatchdog();
    uint32_t cycleStartMs = millis();

    // Full wake: FDC2214 out of shutdown, reconfigured, and settled before we
    // trust a reading (see probeWake()). This is the whole point of the
    // sleep/wake work -- sampling immediately on wake, before the LC tank has
    // stabilized, is what produces bad data.
    uint32_t data = 0;
    uint16_t status = 0;
    if (probeWake(BLE_WAKE_DISCARD_CONVERSIONS)) {
      fdcReadChannel0(data, status);
    }

    // Shares readVbatMv() with doVbat() rather than duplicating the divider/ADC
    // math -- the duplicate copy that used to live here inherited the same wrong
    // scaling constants (see readVbatMv()'s comment), so this is now the single
    // place that math exists.
    float vBatMv = readVbatMv(nullptr);

    // Must go through readChargerPins()' pull-up path -- STAT is open-drain
    // with no board pull-up, so a bare digitalRead() here floats and reported
    // a bogus charging=1 even with no battery attached.
    bool statHigh = true;
    bool pgHigh = true;
    readChargerPins(statHigh, pgHigh);
    bool charging = statHigh == LOW;
    bool batteryReadingValid = vBatMv >= 2500.0f && vBatMv <= 5000.0f;
    bool otaPowerOk = pgHigh == LOW ||
                      (batteryReadingValid && vBatMv >= (float)OTA_MIN_BATTERY_MV);

    g_bleWakeCount++;
    char buf[160];
    snprintf(buf, sizeof(buf),
             "  wake=%lu DATA_CH0=%lu vbat_mV=%d charging=%d -- advertising %lums",
             (unsigned long)g_bleWakeCount, (unsigned long)data, (int)(vBatMv + 0.5f),
             charging ? 1 : 0, (unsigned long)BLE_ADVERTISE_MS);
    printLineResilient(buf);

    bleStartAdv(data, vBatMv, charging, otaPowerOk, BLE_ADV_FAST_INTERVAL_UNITS, 0);
    delayFeedingWatchdog(BLE_ADVERTISE_MS);
    Bluefruit.Advertising.stop();

    // Everything back down before the sleep interval -- FDC2214 to Shutdown
    // (0.2uA), mux parked off the EC-check codes, TWIM disabled, floating
    // inputs disconnected. See probeSleep().
    probeSleep();

    uint32_t awakeMs = millis() - cycleStartMs;
    uint32_t sleepMs = awakeMs < BLE_WAKE_PERIOD_MS ? (BLE_WAKE_PERIOD_MS - awakeMs) : 1;
    delayFeedingWatchdog(sleepMs);
  }
  printLineResilient("  blecycle: done");
}

// --------------------------------------------------------------- setup ----
void setup() {
#ifdef MSV3_SENSOR_ID
  // These anchors are load-bearing package metadata. Without a volatile reference, section-GC
  // can remove a build/guard string that the OTA preflight must inspect before touching Probe 6.
  g_firmwareBuildIdAnchor = FIRMWARE_BUILD_ID;
  g_guard2MarkerAnchor = FIRMWARE_GUARD2_MARKER;
#endif
  initWatchdogSafety();
  g_lastSuccessfulOutputMs = millis();  // baseline the output deadman timer at boot, not epoch-0

  // LED pin
  NRF_P1->DIRSET = LED_BIT;
  ledWrite(false);

  // FDC2214 control pins -- bring chip out of shutdown BEFORE any I2C attempt
  pinMode(PIN_FDC_SD, OUTPUT);
  digitalWrite(PIN_FDC_SD, LOW);  // LOW = active (SD is active-HIGH shutdown per datasheet)
  pinMode(PIN_FDC_INTB, INPUT);

  // Mux select pins
  pinMode(PIN_MUX_A0, OUTPUT);
  pinMode(PIN_MUX_A1, OUTPUT);
  digitalWrite(PIN_MUX_A0, LOW);
  digitalWrite(PIN_MUX_A1, LOW);

  // Battery divider -- idle as high-Z INPUT (see doVbat(): the divider's
  // bottom leg only gets driven LOW, briefly, during an actual reading).
  pinMode(PIN_VBAT_DIV_EN, INPUT);
  pinMode(PIN_VBAT_DIV, INPUT);

  pinMode(PIN_DIV_NODE, INPUT);
  gpioDisconnect(PIN_DIV_NODE_OLD);  // P0.00 still wired to VDIV in parallel -- must not be left unconfigured

  // Open-drain outputs on the BQ25170 side -- plain inputs here, no pull
  // needed since the charger itself drives them (external pull-up per its
  // own datasheet recommendation would live on the charger side if fitted;
  // reading them as INPUT is enough to sense LOW vs floating/HIGH either way).
  pinMode(PIN_CHG_STAT, INPUT);
  pinMode(PIN_CHG_PG, INPUT);

  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) { /* wait briefly for a real terminal */ }
  delay(300);

  Serial.println();
  Serial.println(F("=== MSV3 Probe Bring-up Console ==="));
#ifdef MSV3_SENSOR_ID
  Serial.print(F("target: "));
  Serial.print(BLE_DEVICE_NAME);
  Serial.print(F("  build: "));
  Serial.println(FIRMWARE_BUILD_ID);
#endif
  printResetReason();
  printWatchdogStatus();
  Serial.println(F("type 'help' for commands"));
  printHelp();
}

// Direct hardware ground truth for "did this boot come from a real power
// cycle, or a soft reset (watchdog / NVIC_SystemReset / RESET pin)?" --
// needed because this session found soft resets on this board don't appear
// to make Windows/the host re-detect the USB device the way a real VBUS
// power cycle does, so "silent after a suspected hang" was ambiguous
// between "genuinely still hung" and "recovered fine, host just didn't
// notice." readResetReason() is this core's own wiring.c capturing
// POWER->RESETREAS before clearing it -- not something added here.
void printResetReason() {
  uint32_t r = readResetReason();
  Serial.print(F("reset reason: 0x"));
  Serial.print(r, HEX);
  Serial.print(F(" ("));
  if (r == 0) Serial.print(F("POWER-ON/VBUS (no soft-reset bits set)"));
  if (r & POWER_RESETREAS_RESETPIN_Msk) Serial.print(F("RESETPIN "));
  if (r & POWER_RESETREAS_DOG_Msk) Serial.print(F("WATCHDOG "));
  if (r & POWER_RESETREAS_SREQ_Msk) Serial.print(F("SOFTWARE(NVIC_SystemReset) "));
  if (r & POWER_RESETREAS_LOCKUP_Msk) Serial.print(F("CPU-LOCKUP "));
  if (r & POWER_RESETREAS_OFF_Msk) Serial.print(F("WAKE-FROM-OFF "));
  Serial.println(F(")"));
}

// Ground truth from the actual hardware status registers, not inference --
// LFCLKSTAT.STATE/SRC confirm the clock is genuinely running from RC (not
// just that EVENTS_LFCLKSTARTED fired once at some point), RUNSTATUS
// confirms the WDT peripheral itself is actually counting. Callable anytime
// via the `wdtstatus` command, not just read once at boot.
void printWatchdogStatus() {
  Serial.print(F("watchdog: "));
  Serial.println(g_lfclkRcOk ? F("ARMED (5s timeout, auto-recovers on hang)")
                              : F("NOT ARMED -- LFCLK RC failed to start, no hang protection this boot!"));
  uint32_t lfstat = NRF_CLOCK->LFCLKSTAT;
  bool lfRunning = (lfstat & CLOCK_LFCLKSTAT_STATE_Msk) != 0;
  uint32_t lfSrc = lfstat & CLOCK_LFCLKSTAT_SRC_Msk;
  Serial.print(F("  LFCLKSTAT: running="));
  Serial.print(lfRunning ? "YES" : "NO");
  Serial.print(F(" src="));
  Serial.println(lfSrc == CLOCK_LFCLKSTAT_SRC_RC ? "RC" : lfSrc == CLOCK_LFCLKSTAT_SRC_Xtal ? "Xtal" : "Synth");
  Serial.print(F("  WDT RUNSTATUS: "));
  Serial.println((NRF_WDT->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUS_Msk) ? "RUNNING" : "NOT RUNNING");

  // 2026-08-21: chasing the ~65uA sleep-current excess (see
  // docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 2). The nRF52840 PS is explicit
  // (section 6.35.7.1): "software must explicitly place the USBD in low power
  // mode through writing LowPower to register LOWPOWER" on suspend/VBUS-not-
  // present, and the HFXO crystal likewise "must explicitly" be disabled then --
  // neither is automatic, and nothing in this sketch touches either one.
  // probeSleep()'s per-IC survey only covers the sensor-side ICs, never the
  // MCU's own USB peripheral -- and every sleep-current test so far started
  // from a boot where USB was connected at some point (required, it's the only
  // way to get a console), so if TinyUSB doesn't fully tear this down when
  // VBUS disappears, that's a real, unaccounted-for, BLE-independent excess.
  // Reading ground truth here instead of guessing.
  uint32_t hfstat = NRF_CLOCK->HFCLKSTAT;
  bool hfRunning = (hfstat & CLOCK_HFCLKSTAT_STATE_Msk) != 0;
  uint32_t hfSrc = hfstat & CLOCK_HFCLKSTAT_SRC_Msk;
  Serial.print(F("  HFCLKSTAT: running="));
  Serial.print(hfRunning ? "YES" : "NO");
  Serial.print(F(" src="));
  Serial.println(hfSrc == CLOCK_HFCLKSTAT_SRC_Xtal ? "Xtal(HFXO)" : "RC(HFINT)");
  Serial.print(F("  USBD: ENABLE="));
  Serial.print(NRF_USBD->ENABLE ? "1" : "0");
  Serial.print(F(" LOWPOWER="));
  Serial.println(NRF_USBD->LOWPOWER ? "1(low-power)" : "0(normal power)");
}

void printHelp() {
  Serial.println(F("  led on|off|blink        - status LED (P1.10)"));
  Serial.println(F("  i2cscan                 - scan I2C bus 0x08-0x77"));
  Serial.println(F("  addr <hex>               - set FDC2214 I2C address (default 0x2A)"));
  Serial.println(F("  fdcid                    - read MANUFACTURER_ID/DEVICE_ID (works even before fdcinit)"));
  Serial.println(F("  fdcinit                  - configure FDC2214 for CH0 free-running conversion"));
  Serial.println(F("  errcfg [hex]             - read ERROR_CONFIG (0x19), or override it; 'errcfg 0' restores the"));
  Serial.println(F("                             reset value that keeps STATUS.ERR_WD permanently dead"));
  Serial.println(F("  idrive <0-31>            - set DRIVE_CURRENT_CH0 (datasheet Table 7-41), reads back DATA_CH0+STATUS"));
  Serial.println(F("  fdcread                  - one-shot STATUS + DATA_CH0 read"));
  Serial.println(F("  fdcstream <n>            - stream n reads, ~150ms apart (touch electrodes live)"));
  Serial.println(F("  usbtest <n>              - isolation test: n dummy prints, same pacing as fdcstream, zero I2C"));
  Serial.println(F("  fdctest <n>              - isolation test: n real I2C reads, no per-sample output, final checksum only"));
  Serial.println(F("  mux <0-3>                - set TMUX1109 select code (A1 A0), report pins driven"));
  Serial.println(F("  hotcold                  - toggle electrode-swap phase (codes 0/1 only), holds for multimeter bench test"));
  Serial.println(F("  hotcoldstream <n>        - live DATA_CH0 stream tagged with orientation; 'f'=flip electrodes, 'q'=stop"));
  Serial.println(F("  muxsweep                 - cycle all 4 mux codes, one fdcread each (finds osc vs EC-check states)"));
  Serial.println(F("  sensesweep <n>           - n cycles alternating electrode-swap phase + EC-check phase together (default 10)"));
  Serial.println(F("  vbat                     - read VBAT_DIV (battery divider) -- informational, no battery attached"));
  Serial.println(F("  vbathold <secs>          - hold VBAT_DIV_EN LOW (default 120s) for manual multimeter probing, no ADC reads"));
  Serial.println(F("  chgstatus                - read BQ25170 STAT/PG charger status pins"));
  Serial.println(F("  divnode                  - analogRead DIV_NODE (EC-check midpoint), P0.03/AIN1 bodge"));
  Serial.println(F("  wdtstatus                - report watchdog/LFCLK hardware status registers"));
  Serial.println(F("  i2cdebug                 - report the I2C forensic breadcrumb (last step/call count)"));
#ifdef MSV3_SENSOR_ID
  Serial.println(F("  dfu                      - USB-gated reset into resident BLE recovery bootloader"));
  Serial.println(F("  OTA recovery             - automatic 5s connectable window every 60s (USB or VBAT >= 3800mV)"));
#endif
  Serial.println(F("  blestart                 - init BLE, advertise continuously (one-shot reading) until blestop"));
  Serial.println(F("  blestop                  - stop advertising"));
  Serial.println(F("  blecycle <n>             - run n MSV2-style wake/measure/advertise/sleep cycles (~10s each)"));
  Serial.println(F("  sleep                    - put all ICs into their lowest-power state"));
  Serial.println(F("  wake                     - full wake incl. FDC reconfigure + settle"));
  Serial.println(F("  wakeprofile <n>          - cold-wake then print n UNDISCARDED conversions (measures real settle time)"));
  Serial.println(F("  sleepbench <secs>        - sleep silently for n secs so USB can be unplugged and current measured"));
}

void doI2cScan() {
  Serial.println(F("Scanning I2C bus..."));
  int found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  found device at 0x"));
      Serial.println(a, HEX);
      found++;
    }
  }
  if (!found) Serial.println(F("  nothing responded -- check SD pin (must be LOW), SDA/SCL wiring, 3V3 rail"));
}

void doFdcId() {
  uint16_t mfr, dev;
  bool okMfr = fdcReadReg(0x7E, mfr);
  bool okDev = fdcReadReg(0x7F, dev);
  if (!okMfr || !okDev) {
    Serial.println(F("  I2C read failed -- no ACK from FDC2214. Check addr, SD pin, wiring."));
    return;
  }
  Serial.print(F("  MANUFACTURER_ID=0x"));
  Serial.print(mfr, HEX);
  Serial.print(mfr == 0x5449 ? F(" (matches TI, good)") : F(" (UNEXPECTED, expected 0x5449)"));
  Serial.println();
  Serial.print(F("  DEVICE_ID=0x"));
  Serial.print(dev, HEX);
  Serial.print(dev == 0x3055 ? F(" (matches FDC2214, good)") : F(" (unexpected -- expected 0x3055 for FDC2214)"));
  Serial.println();
}

// 2026-08-22: datasheet Table 7-41 IDRIVE codes in mA -- nonlinear/roughly
// logarithmic steps, not evenly spaced, so this has to be a table (not a
// formula) to report a real mA figure back while bench-tuning against a
// coated (extra-loaded) sensor.
const float IDRIVE_MA[32] = {
  0.016, 0.018, 0.021, 0.025, 0.028, 0.033, 0.038, 0.044,
  0.052, 0.060, 0.069, 0.081, 0.093, 0.108, 0.126, 0.146,
  0.169, 0.196, 0.228, 0.264, 0.307, 0.356, 0.413, 0.479,
  0.555, 0.644, 0.747, 0.867, 1.006, 1.167, 1.354, 1.571
};

// Kept in sync with fdcInit()'s DRIVE_CURRENT_CH0 starting value (0xC800 ->
// code 25 since the 2026-09-03 bench tuning) so `idrive` reports the true
// setting even before it's ever been called. fdcInit() rewrites 0x1E, so any
// hand-set `idrive` is lost across a re-init -- re-issue it after fdcinit.
uint8_t g_idriveCode = 25;

void doIdrive(int code) {
  if (code < 0 || code > 31) {
    Serial.println(F("  code must be 0-31 (see datasheet Table 7-41, DRIVE_CURRENT_CH0)"));
    return;
  }
  g_idriveCode = (uint8_t)code;
  uint16_t regVal = (uint16_t)g_idriveCode << 11;
  if (!fdcWriteReg(0x1E, regVal)) {
    Serial.println(F("  I2C write failed"));
    return;
  }
  Serial.print(F("  IDRIVE code="));
  Serial.print(g_idriveCode);
  Serial.print(F(" ("));
  Serial.print(IDRIVE_MA[g_idriveCode], 3);
  Serial.println(F("mA) set -- reading back one conversion:"));

  uint32_t data;
  uint16_t status;
  char buf[160];
  if (fdcReadChannel0(data, status)) {
    snprintf(buf, sizeof(buf), "  DATA_CH0=%lu  ", (unsigned long)data);
    appendFdcStatus(buf, sizeof(buf), status);
  } else {
    snprintf(buf, sizeof(buf), "  I2C read failed");
  }
  printLineResilient(buf);
  Serial.println(F("  target: 1.2Vpk <= oscillation amplitude <= 1.8Vpk -- ERR_ALW means raise this, ERR_AHW means lower it"));
}

void setMux(uint8_t code) {
  digitalWrite(PIN_MUX_A0, (code & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_MUX_A1, (code & 0x02) ? HIGH : LOW);
}

// Confirmed 2026-08-21 against TMUX1109's real Table 6-1 truth table (EN=1,
// A1, A0 -> S1x/S2x/S3x/S4x) cross-referenced with this board's actual net
// assignments (S1A/S1B=TANK_HOT/TANK_REF, S2A/S2B=TANK_REF/TANK_HOT,
// S3A/S3B=3V3/VDIV, S4A/S4B=VDIV/3V3) -- previously this sketch deliberately
// didn't assume which code was which and made muxsweep prove it empirically
// each time; now it's known with datasheet-level certainty, not just
// pattern-matched from bench data.
const char *muxCodeLabel(uint8_t code) {
  switch (code) {
    case 0: return "electrode-swap phase 0 (A=TANK_HOT B=TANK_REF)";
    case 1: return "electrode-swap phase 1 (A=TANK_REF B=TANK_HOT)";
    case 2: return "EC-check phase 0 (A=3V3 B=VDIV)";
    case 3: return "EC-check phase 1 (A=VDIV B=3V3)";
    default: return "?";
  }
}

void doMuxSweep() {
  Serial.println(F("Sweeping all 4 mux codes (labels confirmed against TMUX1109's real truth table)."));
  for (uint8_t code = 0; code < 4; code++) {
    feedWatchdog();
    setMux(code);
    delay(300);
    uint32_t data;
    uint16_t status;
    char buf[160];
    int len = snprintf(buf, sizeof(buf), "code=%u %s: ", code, muxCodeLabel(code));
    if (fdcReadChannel0(data, status)) {
      len += snprintf(buf + len, sizeof(buf) - len, "DATA_CH0=%lu  ", (unsigned long)data);
      appendFdcStatus(buf, sizeof(buf), status);
    } else {
      snprintf(buf + len, sizeof(buf) - len, "I2C read failed");
    }
    printLineResilient(buf);
  }
}

// 2026-08-22: dedicated manual toggle for bench multimeter testing of the
// electrode-swap function alone -- restricted to codes 0/1 only, so a
// fat-fingered `mux 2`/`mux 3` can't land the probe on the EC-check path
// (3V3/VDIV) by accident mid-test. g_hotColdPhase always mirrors the CURRENT
// physical code (flip-then-apply, not apply-then-arm-next) so `hotcold` and
// `hotcoldstream` below can share it and hand off to each other correctly.
bool g_hotColdPhase = false;

void printHotColdOrientation(uint8_t code) {
  if (code == 0) {
    Serial.println(F("  MS_NODE_A (electrode A) = HOT (TANK_HOT)   MS_NODE_B (electrode B) = COLD (TANK_REF)"));
  } else {
    Serial.println(F("  MS_NODE_A (electrode A) = COLD (TANK_REF)  MS_NODE_B (electrode B) = HOT (TANK_HOT)"));
  }
}

void doHotColdToggle() {
  g_hotColdPhase = !g_hotColdPhase;
  uint8_t code = g_hotColdPhase ? 1 : 0;
  setMux(code);
  Serial.print(F("  code="));
  Serial.print(code);
  Serial.print(F("  -> "));
  Serial.println(muxCodeLabel(code));
  printHotColdOrientation(code);
  Serial.println(F("  TANK_HOT/TANK_REF is an always-running LC oscillator, not a static DC level --"));
  Serial.println(F("  meter in AC-volts (or resistance/continuity to GND) on the MS_NODE_A/MS_NODE_B"));
  Serial.println(F("  test points should show which one is live. Send 'hotcold' again to flip back."));
}

// 2026-08-22: Step 2 of the electrode-swap bench test. `hotcold` only proves
// the mux electrically switches; this proves the FDC2214 gives a comparable
// moisture (DATA_CH0) reading regardless of which physical electrode is
// wired to TANK_HOT -- which is what actually validates the swap is safe to
// use for DC-bias cancellation in production firmware. Streams readings
// continuously, tagged with the live orientation on every line, and polls
// Serial for a bare 'f'/'F' (no Enter needed once typed into most terminals;
// works either way since a trailing newline byte is just ignored) to flip
// electrodes mid-stream, or 'q'/'Q' to stop early. This can't just be
// `fdcstream` + `hotcold` typed by hand: fdcstream's loop blocks on delay()
// and never drains Serial until it's done, so anything typed mid-stream
// would just queue up and land after the last sample instead of taking
// effect live.
void doHotColdStream(int maxSamples) {
  if (maxSamples <= 0) maxSamples = 100000;  // effectively "until 'q'"
  Serial.println(F("Live hot/cold stream. Send 'f' to flip electrodes, 'q' to stop."));
  uint8_t code = g_hotColdPhase ? 1 : 0;
  printHotColdOrientation(code);
  setMux(code);
  delay(300);  // LC-tank settle margin, same as doMuxSweep/sensesweep

  for (int i = 0; i < maxSamples; i++) {
    feedWatchdog();

    uint32_t data;
    uint16_t status;
    char buf[180];
    int len = snprintf(buf, sizeof(buf), "%d: code=%u (A=%s B=%s) ", i, code,
                        code == 0 ? "HOT " : "COLD", code == 0 ? "COLD" : "HOT ");
    if (fdcReadChannel0(data, status)) {
      len += snprintf(buf + len, sizeof(buf) - len, "DATA_CH0=%lu  ", (unsigned long)data);
      appendFdcStatus(buf, sizeof(buf), status);
    } else {
      snprintf(buf + len, sizeof(buf) - len, "I2C read failed");
    }
    printLineResilient(buf);

    delay(150);  // sample pacing, matches fdcstream's cadence

    bool flip = false, stop = false;
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == 'f' || c == 'F') flip = true;
      else if (c == 'q' || c == 'Q') stop = true;
    }
    if (stop) {
      printLineResilient("  hotcoldstream: stopped by user");
      break;
    }
    if (flip) {
      g_hotColdPhase = !g_hotColdPhase;
      code = g_hotColdPhase ? 1 : 0;
      setMux(code);
      printLineResilient("  >> flipped <<");
      printHotColdOrientation(code);
      delay(300);  // settle before trusting the next sample
    }
  }
}

// 2026-08-22: two real gaps found while reviewing this sketch against what's
// actually been exercised on hardware. (1) The electrode-swap mux codes
// (0/1) have only ever been read individually via `mux`/`muxsweep` -- never
// actually ALTERNATED cycle-to-cycle the way MSV2's PLATE_A/PLATE_B swap is
// (see MSV2_Moisture_BLE_Sleep.ino's driveIsPlateA), which is what actually
// cancels any DC-bias-driven drift between the two physical electrodes over
// time. (2) The EC-check path (codes 2/3, DIV_NODE) has NEVER been read as
// part of any repeated/periodic cycle -- only ever a single `divnode` call
// at rest, and never with a real conductive bridge across the electrodes
// (still an open item from 8/20, see docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md
// Test 5). This command exercises both gaps together in one repeated cycle,
// specifically so both can be verified against real hardware before either
// piece is considered proven enough to fold into production firmware.
//
// The mux's own address bits make the phase-pairing free: code = A1<<1|A0,
// so tank-sensing codes (0,1) and EC-check codes (2,3) share the same A0
// ("phase") bit -- toggling one phase bit alternates BOTH mode's polarity in
// lockstep, matching the mux's real hardware structure rather than adding a
// second, independent toggle.
bool g_sensePhase = false;

void doSenseSweep(int n) {
  if (n <= 0) n = 10;
  Serial.println(F("Alternating electrode-swap phase + EC-check phase together, one pair per cycle."));
  Serial.println(F("(TANK_HOT/TANK_REF via the LC tank, then DIV_NODE via the resistive EC-check path)"));
  for (int i = 0; i < n; i++) {
    feedWatchdog();
    uint8_t tankCode = g_sensePhase ? 1 : 0;
    uint8_t ecCode = g_sensePhase ? 3 : 2;
    char buf[180];

    setMux(tankCode);
    delay(300);  // same LC-tank settle margin as doMuxSweep()
    uint32_t data;
    uint16_t status;
    int len = snprintf(buf, sizeof(buf), "cycle=%d tank code=%u %s: ", i, tankCode, muxCodeLabel(tankCode));
    if (fdcReadChannel0(data, status)) {
      len += snprintf(buf + len, sizeof(buf) - len, "DATA_CH0=%lu  ", (unsigned long)data);
      appendFdcStatus(buf, sizeof(buf), status);
    } else {
      len += snprintf(buf + len, sizeof(buf) - len, "I2C read failed");
    }
    printLineResilient(buf);

    setMux(ecCode);
    // DIV_NODE is a plain resistive divider (R17 pulldown + mux switch
    // resistance, no capacitor on this net per the netlist) -- settles in
    // microseconds, not milliseconds. 20ms is generous margin, not a
    // measured requirement the way the LC tank's 300ms is.
    delay(20);
    bool divHigh = digitalRead(PIN_DIV_NODE);
    snprintf(buf, sizeof(buf), "cycle=%d EC code=%u %s: DIV_NODE=%s", i, ecCode, muxCodeLabel(ecCode),
             divHigh ? "HIGH (bridged/conductive)" : "LOW (open/dry, expected at rest)");
    printLineResilient(buf);

    g_sensePhase = !g_sensePhase;
  }
  setMux(0);  // electrode-swap phase 0 -- same code MUX_SLEEP_CODE uses (defined later
              // in this file, in the sleep/wake section, not yet visible here)
  printLineResilient("  sensesweep: done, mux parked back on electrode-swap phase 0");
}

// New 2026-08-21: BQ25170 STAT/PG were never read before this -- resolved as
// distinct nets only once the pcb-reference library cross-checked the real
// datasheet pin table against the netlist. Both open-drain, active LOW.
// Meaningful even tonight (VBUS present, no battery): PG specifically only
// asserts when VIN is in range AND VOUT+VSLEEPZ < VIN < VIN_OV, so its state
// with no battery attached is itself informative -- not a placeholder test.
// CORRECTED 2026-08-21: this used to read both pins as plain `INPUT`, which
// was wrong and made every reading it ever produced untrustworthy. BQ25170's
// datasheet is explicit that STAT and PG are open-drain and says "Connect to
// pull-up rail via 10-kOhm resistor" for each -- and this board has NO pull-up
// on either net (verified in the netlist: NetU1_15 contains only U1-15 and
// USB_IC-5; NetU1_17 only U1-17 and USB_IC-6). An open-drain output with no
// pull-up only ever drives LOW; the HIGH state is high-Z, so a plain INPUT
// read floats and can report anything. The nRF52840's internal pull-up
// (RPU = 13k typ, per its own datasheet -- close to the 10k TI asks for) is
// the fix available in firmware.
//
// The pull-up is enabled only for the duration of the read, then the pin is
// released to a fully disconnected input: whenever the charger IS asserting
// one of these lines LOW, the pull-up sources 3.3V/13k = ~254uA. Leaving that
// on continuously would swamp the entire sleep budget -- and PG in particular
// is asserted LOW the whole time USB is connected.
// Single implementation of the pull-up/read/release dance, so every caller
// gets a valid reading. Both status pins are read together because the
// pull-up cost is per-read, not per-pin.
void readChargerPins(bool &statHigh, bool &pgHigh) {
  pinMode(PIN_CHG_STAT, INPUT_PULLUP);
  pinMode(PIN_CHG_PG, INPUT_PULLUP);
  delayMicroseconds(200);  // RPU(13k) x pad+trace capacitance settles well within this
  statHigh = digitalRead(PIN_CHG_STAT);
  pgHigh = digitalRead(PIN_CHG_PG);
  gpioDisconnect(PIN_CHG_STAT);  // stop the ~254uA the pull-up costs while a line is held LOW
  gpioDisconnect(PIN_CHG_PG);
}

// True only while the charger is actually pushing current into a cell.
// STAT LOW = charge in progress; HIGH = complete, disabled, or no battery.
bool isBatteryCharging() {
  bool stat, pg;
  readChargerPins(stat, pg);
  return stat == LOW;
}

void doChgStatus() {
  bool stat, pg;
  readChargerPins(stat, pg);
  Serial.print(F("  STAT (P0.05) = "));
  Serial.print(stat ? "HIGH" : "LOW");
  Serial.println(stat ? F(" (complete/disabled/no-battery)") : F(" (charging)"));
  Serial.print(F("  PG   (P1.09) = "));
  Serial.print(pg ? "HIGH" : "LOW");
  Serial.println(pg ? F(" (input NOT good)") : F(" (input power good)"));
}

// CORRECTED 2026-08-21 (found via the pcb-reference library's netlist audit,
// never actually exercised with a real battery attached before this): the
// divider is VBAT --[R29 100k]-- VBAT_DIV --[R30 100k]-- VBAT_DIV_EN. R30
// goes to the GPIO, not R29 -- the GPIO is the divider's GROUND-SIDE return,
// not a high-side enable switch. Driving it HIGH (the original code) would
// average VBAT against 3.3V instead of against GND, giving a wrong,
// non-obviously-wrong voltage (VBAT+3.3)/2 instead of VBAT/2. It must be
// driven LOW during the read, and released to high-Z (INPUT) afterward --
// leaving it OUTPUT/LOW permanently would keep ~18uA continuously flowing
// through the divider from VBAT, which matters on a battery-powered design
// this size.
// CORRECTED AGAIN 2026-08-21 (second, independent bug in this same function --
// the first was the divider polarity, see the comment block above): the ADC
// scaling was wrong in BOTH constants. The old code computed
// `raw * (3.3f / 16383.0f)`, i.e. it assumed a 14-bit result against a 3.3V
// full scale. This core does neither by default:
//   - `wiring_analog_nRF52.c` has `static int readResolution = 10;` -- reads
//     return 0-1023, not 0-16383, unless analogReadResolution() is called.
//   - Its default reference is the internal 0.6V band-gap with 1/6 gain, i.e.
//     a 0-3.6V full scale (that file's own comment states this explicitly),
//     not 3.3V.
// Nothing in this sketch ever called analogReadResolution()/analogReference(),
// so every battery reading printed so far was understated by ~17.5x -- which is
// why `vbat` reported a plausible-looking but meaningless ~0.3V. It was never
// caught earlier because no battery has ever been attached to check it against,
// exactly the same blind spot that hid the divider-polarity bug.
// Now sets both explicitly, mirroring what MSV2_Moisture_BLE_Sleep.ino's
// readBatteryMv() already does on the XIAO: AR_INTERNAL_3_0 (3.0V full scale)
// + 12-bit (4096 counts) -> 3000/4096 mV per LSB, doubled for the 100k/100k
// divider. Max readable is 6.0V, comfortably above a full 4.2V LiPo.
#define VBAT_MV_PER_LSB (3000.0f / 4096.0f)
#define VBAT_DIVIDER_RATIO (2.0f)

float readVbatMv(int *rawOut) {
  pinMode(PIN_VBAT_DIV_EN, OUTPUT);
  digitalWrite(PIN_VBAT_DIV_EN, LOW);  // divider's ground-side return -- LOW, not HIGH
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  // This divider is 100k/100k, so the ADC sees a ~50k source impedance. The
  // core's default SAADC acquisition time is 3us (wiring_analog_nRF52.c's
  // `saadcSampleTime = SAADC_CH_CONFIG_TACQ_3us`), which is only adequate for
  // a low-impedance source -- at 50k the sample-and-hold capacitor can't
  // settle in that window, so the conversion is taken before the input has
  // actually charged. 40us is the longest this core offers (analogSampleTime()
  // accepts 3/5/10/15/20/40), deliberately chosen as the unambiguously-safe
  // end of the range rather than trying to pin the exact threshold: this
  // reading is taken once per wake, not streamed, so the extra ~37us costs
  // nothing measurable.
  analogSampleTime(40);
  // 2026-08-21: the 5ms this used to wait here is exactly ONE R29||R30/C16
  // time constant (100k||100k = 50k, C16 = 100nF -> RC = 5ms) -- only 63% of
  // the way to the final value, not settled. VBAT_DIV_EN idles as high-Z
  // between reads (see below), so with no current path pulling the node down
  // it drifts toward VBAT itself while idle; every read then starts from
  // near-VBAT and only got 1 RC constant to relax toward the true VBAT/2
  // before being sampled, landing partway between the two and reading high --
  // this is what was actually behind vbat reading ~5.3-5.6V while charging
  // instead of the true ~4.2V (confirmed by holding VBAT_DIV_EN low with
  // `vbathold` and directly probing VBAT_DIV: it measured correctly, right at
  // VBAT/2, so the divider itself was always fine -- only this read's settle
  // time was too short). 25ms is 5 RC constants (~99.3% settled), same
  // "costs nothing measurable, this is once per wake" reasoning as the
  // acquisition-time choice above.
  delay(25);                      // divider RC + reference settle
  (void)analogRead(PIN_VBAT_DIV);  // discard first sample after a reference switch
  int raw = analogRead(PIN_VBAT_DIV);
  analogReference(AR_DEFAULT);   // restore core defaults for any other analogRead user
  analogReadResolution(10);
  analogSampleTime(3);
  pinMode(PIN_VBAT_DIV_EN, INPUT);  // high-Z -- stops divider current between reads
  if (rawOut) *rawOut = raw;
  return raw * VBAT_MV_PER_LSB * VBAT_DIVIDER_RATIO;
}

void doVbat() {
  int raw = 0;
  float vBatMv = readVbatMv(&raw);
  Serial.print(F("  raw="));
  Serial.print(raw);
  Serial.print(F("/4095  Vbat="));
  Serial.print(vBatMv / 1000.0f, 3);
  Serial.print(F("V ("));
  Serial.print((int)(vBatMv + 0.5f));
  Serial.println(F(" mV) -- NOT a real battery reading unless a cell is actually attached;"));
  Serial.println(F("    with the header empty this is a floating divider node, so treat it as"));
  Serial.println(F("    a chain-is-alive check only, not a voltage measurement."));
}

// Bench aid for tracing the vbat divider discrepancy by hand: hold
// VBAT_DIV_EN LOW (the divider's ground-side return) continuously, touching
// nothing else -- no analogReference/analogReadResolution/analogSampleTime
// changes, no ADC reads at all -- so the node sits in a steady DC state a
// multimeter can actually probe at VBAT (R29 top), VBAT_DIV (R29/R30
// midpoint test point), and VBAT_DIV_EN (R30 bottom) without this firmware
// disturbing anything mid-measurement. readVbatMv() only asserts this for a
// few ms per read, nowhere near long enough to physically move a probe.
void doVbatHold(int secs) {
  Serial.print(F("  holding VBAT_DIV_EN LOW for "));
  Serial.print(secs);
  Serial.println(F("s -- probe now:"));
  Serial.println(F("    VBAT        (R29 top / battery rail)"));
  Serial.println(F("    VBAT_DIV    (R29/R30 midpoint test point) -- expect ~VBAT/2 if the divider's healthy"));
  Serial.println(F("    VBAT_DIV_EN (R30 bottom) -- expect a clean 0V while this command is running"));
  Serial.flush();
  pinMode(PIN_VBAT_DIV_EN, OUTPUT);
  digitalWrite(PIN_VBAT_DIV_EN, LOW);
  delayFeedingWatchdog((uint32_t)secs * 1000UL);
  pinMode(PIN_VBAT_DIV_EN, INPUT);  // back to high-Z, same idle state as everywhere else
  Serial.println(F("  vbathold finished -- VBAT_DIV_EN released back to high-Z."));
}

// EC-check divider midpoint, now that PIN_DIV_NODE lives on P0.03/AIN1
// (bodged 2026-08-28) instead of the digital-only P0.00 it started on.
// Reuses readVbatMv()'s exact reference/resolution/sample-time settings
// rather than re-deriving ADC config from scratch, per the fix plan in
// docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 5. Unlike VBAT_DIV, this net
// has no enable pin and no capacitor (plain R17 pulldown + mux switch
// resistance per the netlist -- see doSenseSweep()'s comment), so it settles
// in microseconds; no RC settle delay needed here, just the same
// discard-first-sample-after-reference-switch as readVbatMv().
// No divider-ratio multiplier: unlike VBAT_DIV (which reads half of VBAT by
// design), this net IS the voltage of interest directly.
#define DIV_NODE_MV_PER_LSB (3000.0f / 4096.0f)

float readDivNodeMv(int *rawOut) {
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  analogSampleTime(40);
  (void)analogRead(PIN_DIV_NODE);  // discard first sample after a reference switch
  int raw = analogRead(PIN_DIV_NODE);
  analogReference(AR_DEFAULT);   // restore core defaults for any other analogRead user
  analogReadResolution(10);
  analogSampleTime(3);
  if (rawOut) *rawOut = raw;
  return raw * DIV_NODE_MV_PER_LSB;
}

void doDivNode() {
  int raw = 0;
  float mv = readDivNodeMv(&raw);
  Serial.print(F("  DIV_NODE analog: raw="));
  Serial.print(raw);
  Serial.print(F("/4095  "));
  Serial.print(mv, 1);
  Serial.println(F(" mV  (3V3 side higher = more conductive; ~0V dry/open at rest)"));
}

// ------------------------------------------- sleep / wake power mgmt ----
// Whole-board low-power sequencing, added 2026-08-21. Every number below is
// from the real datasheet in hardware/MSV3_Probe_PCB/reference/datasheets/,
// not estimated. Per-IC survey of what can actually be slept on this board:
//
//   FDC2214 (MS_IC)  -- two low-power states, and the choice matters a lot:
//                       Sleep Mode (CONFIG.SLEEP_MODE_EN=1): IDDSL = 35uA typ
//                         / 60uA max, registers retained, tSLEEPWAKEUP = 0.05ms.
//                       Shutdown (SD pin HIGH):              ISD   = 0.2uA typ
//                         / 1uA max, ALL REGISTERS RESET, tSDWAKEUP = 2ms.
//                       We use SHUTDOWN. 35uA would be ~10x the nRF52840's
//                       entire System-ON-with-RTC sleep floor (3.16uA typ), so
//                       Sleep Mode would dominate the whole power budget and
//                       make the rest of this effort pointless. Shutdown costs
//                       2ms + 5 I2C register writes per wake, which is nothing
//                       at any realistic sensing cadence.
//   TMUX1109 (MUX)   -- no sleep mode (passive analog switch, ~nA leakage) and
//                       switching is ns-scale, so it is not a settle concern at
//                       all. BUT it must be PARKED OUT OF THE EC-CHECK CODES
//                       (2/3) before sleeping: those tie electrode Node A to
//                       3V3, so if the electrodes are bridged by wet soil the
//                       board burns 3V3 -> soil -> VDIV -> R17(100k) -> GND
//                       continuously, up to ~33uA, forever, while "asleep".
//                       That is a bigger drain than everything else combined.
//   TPS7A0233 (LDO)  -- IQ = 25nA typ. Its EN is tied to IN on this board
//                       (self-enable, confirmed in the netlist), so it is not
//                       controllable and does not need to be: 25nA is noise.
//   BQ25170 (charger)-- fully autonomous, no enable line routed to the MCU.
//                       Nothing to sequence. Its STAT/PG pins do need care --
//                       see doChgStatus().
//   BQ25504 (harvest)-- autonomous by design; must stay running to harvest at
//                       all. No MCU control routed. Nothing to sequence.
//   nRF52840 (U1)    -- System ON + FreeRTOS tickless idle (already validated).
//
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
#define FDC_ACTIVATION_US 400   // sensor activation = 16,384 fINT cycles; at the
                                 // ~43.3MHz internal osc that is ~378us
#define MUX_SLEEP_CODE 0        // electrode-swap. NEVER 2 or 3 here -- see above.

// nRF52840 PIN_CNF value for "input buffer disconnected, no pull" -- the
// chip's own reset default. A floating pin with its input buffer CONNECTED
// sits near mid-rail and burns shoot-through current in the input stage,
// which is exactly the kind of thing that quietly dominates a uA budget.
// INTB and VDIV are the two pins this matters for (neither is driven while
// the FDC is shut down, and neither has an external pull-up -- confirmed
// against the netlist: INTB's net contains only MS_IC-5 and U1-43).
#define PIN_CNF_DISCONNECTED 2UL

static inline void gpioDisconnect(uint32_t arduinoPin) {
  if (arduinoPin < 32) NRF_P0->PIN_CNF[arduinoPin] = PIN_CNF_DISCONNECTED;
  else NRF_P1->PIN_CNF[arduinoPin - 32] = PIN_CNF_DISCONNECTED;
}

bool g_probeAsleep = false;

void probeSleep() {
  // 1. FDC2214 -> Shutdown. Registers are lost; probeWake() reconfigures.
  digitalWrite(PIN_FDC_SD, HIGH);
  // 2. Park the mux off the EC-check codes (the 3V3-to-electrodes path).
  setMux(MUX_SLEEP_CODE);
  // 3. Battery divider fully released (no current through R29/R30).
  // 2026-08-21: this used to be pinMode(INPUT), which on this core means
  // "high-Z direction, but input buffer still CONNECTED" -- not actually
  // disconnected. Same class of bug already fixed for INTB/DIV_NODE below
  // (see that comment): a floating pin with its input buffer connected can
  // sit mid-rail and burn CMOS shoot-through current, and this divider's
  // pins are the worst-case version of that -- both can float toward VBAT
  // (up to ~4.2V) while the chip's own IO supply is 3.3V. gpioDisconnect()
  // instead, matching INTB/DIV_NODE's treatment.
  gpioDisconnect(PIN_VBAT_DIV_EN);
  gpioDisconnect(PIN_VBAT_DIV);
  // 4. LED off.
  ledWrite(false);
  // 5. Disable the TWIM peripheral. On nRF52 an enabled TWIM keeps drawing
  //    even with no traffic, and it is easy to leave running by accident --
  //    this is the same class of bug as MSV2's QSPI-left-enabled drain.
  //    Wire.end() releases SDA/SCL too, so the 4.7k pullups (R4/R5) sit at
  //    3V3 with no current instead of being held low by a driven-low pin
  //    (which would cost ~700uA per line).
  Wire.end();
  // 6. Undriven, pull-up-less inputs -> disconnect their input buffers.
  gpioDisconnect(PIN_FDC_INTB);
  gpioDisconnect(PIN_DIV_NODE);
  gpioDisconnect(PIN_DIV_NODE_OLD);  // P0.00, still wired to VDIV in parallel
  // 2026-08-21: charger STAT/PG have the same gap -- readChargerPins()
  // already gpioDisconnect()s them after every chgstatus/isBatteryCharging()
  // call, but only after one has actually run. If sleep happens before any
  // charger read this boot, they're still sitting in whatever plain-INPUT
  // state setup() left them in. Disconnect explicitly here so sleep state is
  // correct regardless of what commands ran before it.
  gpioDisconnect(PIN_CHG_STAT);
  gpioDisconnect(PIN_CHG_PG);
  g_probeAsleep = true;
}

// Brings everything back up and, critically, does not return until the
// analog front end is actually producing trustworthy data. Returns false if
// the FDC failed to reconfigure (I2C problem), true once settled.
bool probeWake(uint8_t discardConversions) {
  // I2C peripheral back up first -- needed to talk to the FDC at all.
  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);

  digitalWrite(PIN_FDC_SD, LOW);   // leave Shutdown
  delayMicroseconds(FDC_TSDWAKEUP_US);  // tSDWAKEUP before the part will answer I2C

  // Shutdown reset every register, so a full reconfigure is mandatory, not
  // optional. fdcInit() deliberately writes CONFIG (0x1A) LAST: the datasheet
  // says to configure the device while it is still in Sleep Mode and then
  // exit sleep, and our CONFIG value has SLEEP_MODE_EN=0, so that final write
  // is what actually starts conversions.
  if (!fdcInit()) return false;

  delayMicroseconds(FDC_ACTIVATION_US);  // sensor activation after sleep exit

  // Then throw away the first conversions while the LC tank's oscillation
  // amplitude stabilizes. How many is genuinely needed was MEASURED on this
  // board with the `wakeprofile` command below rather than guessed.
  for (uint8_t i = 0; i < discardConversions; i++) {
    uint32_t d; uint16_t s;
    fdcReadChannel0(d, s);
    delay(1);
  }
  g_probeAsleep = false;
  return true;
}

// Measures how long the front end actually takes to settle after a cold
// wake, instead of assuming. Shuts the FDC fully down, wakes it, then reads
// N conversions back-to-back with NO discards and prints every one, so the
// settling profile is directly visible: if early samples differ materially
// from the steady-state value, that is exactly the "not enough wake time ->
// bad data" failure mode, and the point where it flattens out is the real
// minimum discard count.
void doWakeProfile(int n) {
  if (n <= 0 || n > 40) n = 12;
  probeSleep();
  delay(50);
  char buf[160];

  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);
  digitalWrite(PIN_FDC_SD, LOW);
  uint32_t t0 = micros();
  delayMicroseconds(FDC_TSDWAKEUP_US);
  if (!fdcInit()) { printLineResilient("  wakeprofile: fdcInit FAILED"); return; }
  delayMicroseconds(FDC_ACTIVATION_US);
  uint32_t tReady = micros() - t0;
  snprintf(buf, sizeof(buf), "  wake sequence took %luus, now reading %d RAW conversions (no discards):",
           (unsigned long)tReady, n);
  printLineResilient(buf);

  uint32_t first = 0;
  for (int i = 0; i < n; i++) {
    feedWatchdog();
    uint32_t d = 0; uint16_t s = 0;
    uint32_t tsRead = micros() - t0;
    if (fdcReadChannel0(d, s)) {
      if (i == 0) first = d;
      long delta = (long)d - (long)first;
      snprintf(buf, sizeof(buf), "  [%2d] t=%6luus DATA_CH0=%lu  d_vs_first=%+ld  ",
               i, (unsigned long)tsRead, (unsigned long)d, delta);
      appendFdcStatus(buf, sizeof(buf), s);
    } else {
      snprintf(buf, sizeof(buf), "  [%2d] I2C read failed", i);
    }
    printLineResilient(buf);
  }
  g_probeAsleep = false;
}

// --------------------------------------------------------------- loop ----
String cmdBuf;

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "help") {
    printHelp();
  } else if (cmd == "led on") {
    ledWrite(true);
    Serial.println(F("  LED on"));
  } else if (cmd == "led off") {
    ledWrite(false);
    Serial.println(F("  LED off"));
  } else if (cmd == "led blink") {
    Serial.println(F("  blinking 6x..."));
    for (int i = 0; i < 6; i++) {
      ledWrite(true);
      delay(200);
      ledWrite(false);
      delay(200);
    }
  } else if (cmd == "i2cscan") {
    doI2cScan();
  } else if (cmd.startsWith("addr ")) {
    fdcAddr = (uint8_t)strtol(cmd.c_str() + 5, nullptr, 16);
    Serial.print(F("  fdcAddr set to 0x"));
    Serial.println(fdcAddr, HEX);
  } else if (cmd == "fdcid") {
    doFdcId();
  } else if (cmd == "fdcinit") {
    Serial.println(fdcInit() ? F("  fdcinit OK, CH0 free-running") : F("  fdcinit FAILED (I2C write error)"));
  } else if (cmd == "errcfg" || cmd.startsWith("errcfg ")) {
    // Read or override ERROR_CONFIG (0x19) at runtime.
    //
    // This exists so the "is STATUS.ERR_WD actually alive?" question can be answered on one board
    // without two flashes. fdcInit() now writes 0x0020 (WD_ERR2INT), and the control arm of that
    // experiment -- proving the flag really was dead before, rather than that the tank simply
    // never faults -- needs ERROR_CONFIG back at its reset value of 0x0000 with everything else
    // identical. `errcfg 0` gives exactly that; `errcfg` alone reads back what is in the part.
    //
    // Note the value does NOT survive a shutdown: probeWake()/fdcinit rewrite it. That is the
    // point of the register, and it is why an override here is a diagnostic and not a setting.
    if (cmd == "errcfg") {
      uint16_t value;
      if (!fdcReadReg(0x19, value)) { Serial.println(F("  I2C read failed")); return; }
      char buf[120];
      snprintf(buf, sizeof(buf),
               "  ERROR_CONFIG=0x%04X  WD_ERR2OUT=%d AH_WARN2OUT=%d AL_WARN2OUT=%d "
               "WD_ERR2INT=%d DRDY_2INT=%d",
               value, (value >> 13) & 1, (value >> 12) & 1, (value >> 11) & 1,
               (value >> 5) & 1, value & 1);
      printLineResilient(buf);
      if (!((value >> 5) & 1)) {
        Serial.println(F("  WD_ERR2INT is 0: STATUS.ERR_WD will NEVER update, so a clean"));
        Serial.println(F("  ERR_WD proves nothing about oscillation (Table 7-37)."));
      }
    } else {
      uint16_t value = (uint16_t)strtol(cmd.c_str() + 7, nullptr, 16);
      Serial.println(fdcWriteReg(0x19, value) ? F("  ERROR_CONFIG written")
                                             : F("  I2C write failed"));
    }
  } else if (cmd.startsWith("idrive ")) {
    int code = cmd.substring(7).toInt();
    doIdrive(code);
  } else if (cmd == "fdcread") {
    uint32_t data;
    uint16_t status;
    char buf[160];
    if (fdcReadChannel0(data, status)) {
      snprintf(buf, sizeof(buf), "  DATA_CH0=%lu  ", (unsigned long)data);
      appendFdcStatus(buf, sizeof(buf), status);
    } else {
      snprintf(buf, sizeof(buf), "  I2C read failed");
    }
    printLineResilient(buf);
  } else if (cmd.startsWith("fdcstream ")) {
    int n = cmd.substring(10).toInt();
    if (n <= 0) n = 20;
    for (int i = 0; i < n; i++) {
      feedWatchdog();
      uint32_t data;
      uint16_t status;
      char buf[160];
      if (fdcReadChannel0(data, status)) {
        snprintf(buf, sizeof(buf), "%d: DATA_CH0=%lu  ", i, (unsigned long)data);
        appendFdcStatus(buf, sizeof(buf), status);
      } else {
        snprintf(buf, sizeof(buf), "  I2C read failed");
      }
      printLineResilient(buf);
      delay(150);
    }
  } else if (cmd.startsWith("usbtest ")) {
    // Isolation test: identical pacing/output volume to fdcstream, but never
    // touches Wire/I2C at all. If this hangs at the same point fdcstream
    // does, the cause is purely USB/output-path, not I2C -- if it runs
    // clean, that rules USB out and points squarely at the I2C side.
    int n = cmd.substring(8).toInt();
    if (n <= 0) n = 20;
    for (int i = 0; i < n; i++) {
      feedWatchdog();
      char buf[160];
      snprintf(buf, sizeof(buf), "%d: DATA_CH0=20340000  STATUS=0x8 CH0_UNREAD", i);
      printLineResilient(buf);
      delay(150);
    }
    Serial.println(F("  usbtest: completed all iterations"));
  } else if (cmd.startsWith("fdctest ")) {
    // Isolation test: identical I2C read volume/pacing to fdcstream, but no
    // per-sample Serial output at all (just an LED blip and a final
    // checksum) -- if this ALSO hangs, the cause is purely I2C/TWIM, not
    // USB output competing for the CPU.
    int n = cmd.substring(8).toInt();
    if (n <= 0) n = 20;
    uint32_t checksum = 0;
    int failures = 0;
    int completed = 0;
    for (int i = 0; i < n; i++) {
      feedWatchdog();
      uint32_t data;
      uint16_t status;
      if (fdcReadChannel0(data, status)) {
        checksum += data;
      } else {
        failures++;
      }
      completed = i + 1;
      if ((i % 10) == 0) ledWrite((i / 10) % 2);  // cheap visual heartbeat, no Serial at all
      delay(150);
    }
    ledWrite(false);
    char buf[160];
    snprintf(buf, sizeof(buf), "  fdctest: completed=%d/%d failures=%d checksum=%lu",
             completed, n, failures, (unsigned long)checksum);
    printLineResilient(buf);
  } else if (cmd.startsWith("mux ")) {
    int code = cmd.substring(4).toInt();
    if (code < 0 || code > 3) {
      Serial.println(F("  code must be 0-3"));
    } else {
      setMux((uint8_t)code);
      Serial.print(F("  MUX_A1="));
      Serial.print((code >> 1) & 1);
      Serial.print(F(" MUX_A0="));
      Serial.print(code & 1);
      Serial.print(F("  -> "));
      Serial.println(muxCodeLabel((uint8_t)code));
    }
  } else if (cmd == "hotcold") {
    doHotColdToggle();
  } else if (cmd.startsWith("hotcoldstream")) {
    int n = cmd.length() > 14 ? cmd.substring(14).toInt() : 0;
    doHotColdStream(n);
  } else if (cmd == "muxsweep") {
    doMuxSweep();
  } else if (cmd.startsWith("sensesweep")) {
    int n = cmd.length() > 11 ? cmd.substring(11).toInt() : 10;
    doSenseSweep(n);
  } else if (cmd == "vbat") {
    doVbat();
  } else if (cmd.startsWith("vbathold")) {
    int secs = cmd.length() > 9 ? cmd.substring(9).toInt() : 120;
    if (secs <= 0) secs = 120;
    if (secs > 1800) secs = 1800;
    doVbatHold(secs);
  } else if (cmd == "chgstatus") {
    doChgStatus();
  } else if (cmd == "divnode") {
    doDivNode();
  } else if (cmd == "wdtstatus") {
    printResetReason();
    printWatchdogStatus();
  } else if (cmd == "i2cdebug") {
    char buf[100];
    snprintf(buf, sizeof(buf), "  i2cStep=%lu i2cStepMs=%lu i2cCallCount=%lu (now=%lu)",
             (unsigned long)g_i2cStep, (unsigned long)g_i2cStepMs,
             (unsigned long)g_i2cCallCount, (unsigned long)millis());
    printLineResilient(buf);
#ifdef MSV3_SENSOR_ID
  } else if (cmd == "dfu") {
    // This is the deliberate return path from a coated board's temporary diagnostic image. The
    // custom bootloader has no usable unsigned UF2/Arduino-upload route; arming GPREGRET 0xA8 and
    // resetting enters its continuous signed BLE recovery service. Require USB power-good (or a
    // comfortably charged battery) before entering a flash mode that remains awake continuously.
    bool statHigh = true;
    bool pgHigh = true;
    readChargerPins(statHigh, pgHigh);
    const float batteryMv = readVbatMv(nullptr);
    const bool usbPowerGood = (pgHigh == LOW);
    const bool batteryPowerGood = batteryMv >= 3800.0f && batteryMv <= 5000.0f;
    if (!usbPowerGood && !batteryPowerGood) {
      Serial.print(F("  DFU REFUSED -- USB power-good is absent and VBAT is "));
      Serial.print(batteryMv, 0);
      Serial.println(F("mV (need at least 3800mV)"));
      Serial.flush();
    } else {
      Serial.print(F("  entering resident BLE recovery bootloader; power="));
      Serial.print(usbPowerGood ? F("USB") : F("battery"));
      Serial.print(F(" VBAT="));
      Serial.print(batteryMv, 0);
      Serial.println(F("mV"));
      Serial.flush();
      delay(150);

      if (g_bleInited) {
        // Once Bluefruit has enabled the SoftDevice, POWER and reset operations must go through
        // its SVC APIs. A raw POWER write in that state can fault instead of entering recovery.
        if (sd_power_gpregret_clr(0, 0xFF) != NRF_SUCCESS ||
            sd_power_gpregret_set(0, 0xA8) != NRF_SUCCESS) {
          Serial.println(F("  DFU REFUSED -- SoftDevice could not arm GPREGRET"));
          Serial.flush();
          return;
        }
        uint32_t gpregret = 0;
        if (sd_power_gpregret_get(0, &gpregret) != NRF_SUCCESS ||
            (gpregret & 0xFF) != 0xA8) {
          sd_power_gpregret_clr(0, 0xFF);
          Serial.println(F("  DFU REFUSED -- GPREGRET readback did not verify"));
          Serial.flush();
          return;
        }
        sd_nvic_SystemReset();
        sd_power_gpregret_clr(0, 0xFF);  // only reached if reset unexpectedly failed
      } else {
        NRF_POWER->GPREGRET = 0xA8;
        __DSB();
        if ((NRF_POWER->GPREGRET & 0xFF) == 0xA8) NVIC_SystemReset();
        NRF_POWER->GPREGRET = 0;  // only reached if reset unexpectedly failed
      }
      Serial.println(F("  DFU reset failed; bootloader request cleared"));
      Serial.flush();
    }
#endif
  } else if (cmd == "blestart") {
    // Defensive state repair: this used to call fdcReadChannel0() while the
    // FDC could still be in Shutdown and Wire could still be ended after
    // sleep/sleepbench. blecycle already wakes first. The USB/watchdog reset
    // observed while this was added was later proven to occur earlier, inside
    // Bluefruit.begin(), because the build had silently reverted to USE_LFXO;
    // it was not evidence of an I2C hang. Keep this wake guard anyway because
    // the old direct read was invalid from the documented asleep state.
    bleInitOnce();
    bool wokeOk = probeWake(BLE_WAKE_DISCARD_CONVERSIONS);
    if (!wokeOk) {
      Serial.println(F("  blestart aborted -- probeWake() failed (FDC/I2C not responding), not attempting a read"));
    } else {
      uint32_t data = 0;
      uint16_t status = 0;
      fdcReadChannel0(data, status);
      float batteryMv = 0;
      bool charging = false;
#ifdef MSV3_SENSOR_ID
      bool otaPowerOk = readOtaPowerState(batteryMv, charging);
#else
      batteryMv = readVbatMv(nullptr);
      bool statHigh = true;
      bool pgHigh = true;
      readChargerPins(statHigh, pgHigh);
      charging = statHigh == LOW;
      bool batteryReadingValid = batteryMv >= 2500.0f && batteryMv <= 5000.0f;
      bool otaPowerOk = pgHigh == LOW ||
                        (batteryReadingValid && batteryMv >= (float)OTA_MIN_BATTERY_MV);
#endif
      g_bleWakeCount++;
      bleStartAdv(data, batteryMv, charging, otaPowerOk, BLE_ADV_FAST_INTERVAL_UNITS, 0);
      Serial.print(F("  advertising as \""));
      Serial.print(BLE_DEVICE_NAME);
      Serial.println(F("\" continuously (until blestop)"));
    }
  } else if (cmd == "sleep") {
    probeSleep();
    Serial.println(F("  all ICs -> low-power state:"));
    Serial.println(F("    FDC2214 = Shutdown (SD high, ISD ~0.2uA typ, registers cleared)"));
    Serial.println(F("    TMUX1109 = parked on electrode-swap code (off the 3V3 EC-check path)"));
    Serial.println(F("    VBAT divider released, LED off, TWIM disabled, INTB/VDIV disconnected"));
    Serial.println(F("    (LDO 25nA + charger + harvester are autonomous -- nothing to sequence)"));
  } else if (cmd.startsWith("sleepbench")) {
    // Bench aid for measuring real sleep current: drop every IC into its
    // low-power state and STAY there, silently, long enough to unplug USB and
    // put a meter in series with the battery. Sleep current has to be measured
    // on the battery line with USB disconnected -- with USB attached the CDC
    // peripheral and the charger path swamp the microamps being measured.
    //
    // Note the nRF52840's watchdog CANNOT be stopped once started (only a reset
    // clears it), so the CPU still surfaces briefly ~1x/sec to feed it. That
    // wake is only microseconds of CPU time, so its contribution is a fraction
    // of a microamp -- and it is what the shipping firmware would really do
    // anyway, so this measures the honest number rather than an artificial floor.
    int secs = cmd.length() > 11 ? cmd.substring(11).toInt() : 60;
    if (secs <= 0) secs = 60;
    if (secs > 1800) secs = 1800;
    Serial.print(F("  sleeping "));
    Serial.print(secs);
    Serial.println(F("s with all ICs down. Unplug USB now and measure on the battery line."));
    Serial.print(F("  BLE/SoftDevice: "));
    Serial.println(g_bleInited ? F("ENABLED (LFRC calibration running -- realistic shipping condition)")
                                : F("not initialized (lowest achievable floor)"));
    Serial.flush();
    delay(300);
    probeSleep();
    // 2026-08-21: chasing the ~65uA sleep-current excess (see
    // docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md Test 2). Per the nRF52840 PS
    // section 6.35.7.1, USBD's idle current is only reduced once software
    // explicitly writes LowPower to the LOWPOWER register -- this is not
    // automatic, and nothing else in this sketch ever does it. Deliberately
    // placed only here (not inside probeSleep() itself, which also runs on
    // every blecycle iteration and plain sleep/wake -- neither of those imply
    // USB is about to be removed the way sleepbench's whole premise does), and
    // deliberately NOT touching HFCLK/HFXO here: the SoftDevice manages that
    // clock itself once BLE has been initialized, and blindly stopping a
    // clock peripheral out from under something else that depends on it is
    // exactly the mechanism behind today's earlier LFCLK/RTC1 disaster (see
    // docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md) -- not repeating that mistake
    // with a different clock without a lot more certainty first.
    NRF_USBD->LOWPOWER = 1;
    // USBD LOWPOWER alone made no measurable difference (still ~65-71uA) --
    // ruled out. Next candidate: HFCLK/HFXO. Native USB needs the 64MHz
    // crystal running, and nothing anywhere stops it once USB goes away.
    // Only touching this when BLE was never initialized this boot: once the
    // SoftDevice is enabled it manages HFXO itself for radio timing, and
    // stopping a clock peripheral out from under something else that depends
    // on it is exactly the mechanism behind today's earlier LFCLK/RTC1
    // disaster (docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md) -- g_bleInited=false
    // means there's no SoftDevice to conflict with here. FreeRTOS's own tick
    // runs off RTC1/LFCLK, confirmed independent of HFCLK, so this shouldn't
    // touch anything the scheduler needs either.
    bool hfclkStoppedHere = false;
    if (!g_bleInited) {
      NRF_CLOCK->TASKS_HFCLKSTOP = 1;
      hfclkStoppedHere = true;
    }
    delayFeedingWatchdog((uint32_t)secs * 1000UL);
    if (hfclkStoppedHere) {
      NRF_CLOCK->TASKS_HFCLKSTART = 1;  // restart before USB needs to re-enumerate
    }
    NRF_USBD->LOWPOWER = 0;  // restore before the "reconnect USB" message below needs it
    Serial.println(F("  sleepbench finished -- reconnect USB to see this."));
  } else if (cmd == "wake") {
    uint32_t t0 = micros();
    bool ok = probeWake(BLE_WAKE_DISCARD_CONVERSIONS);
    uint32_t dt = micros() - t0;
    char buf[120];
    snprintf(buf, sizeof(buf), "  wake %s in %luus (incl. %u discarded conversions)",
             ok ? "OK" : "FAILED", (unsigned long)dt, BLE_WAKE_DISCARD_CONVERSIONS);
    printLineResilient(buf);
  } else if (cmd.startsWith("wakeprofile")) {
    int n = cmd.length() > 12 ? cmd.substring(12).toInt() : 12;
    doWakeProfile(n);
  } else if (cmd == "blestop") {
    Bluefruit.Advertising.stop();
    Serial.println(F("  advertising stopped"));
  } else if (cmd.startsWith("blecycle ")) {
    int n = cmd.substring(9).toInt();
    if (n <= 0) n = 3;
    Serial.print(F("  running "));
    Serial.print(n);
    Serial.println(F(" wake/measure/advertise/sleep cycles..."));
    doBleCycle(n);
  } else {
    Serial.print(F("  unknown command: "));
    Serial.println(cmd);
  }
}

void loop() {
  feedWatchdog();
#ifdef MSV3_SENSOR_ID
  servicePeriodicOtaWindow();
#endif
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuf.length() > 0) {
        handleCommand(cmdBuf);
        cmdBuf = "";
      }
    } else {
      cmdBuf += c;
    }
  }
}
