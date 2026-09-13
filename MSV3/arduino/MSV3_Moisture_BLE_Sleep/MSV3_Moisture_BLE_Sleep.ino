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

#if !defined(MSV3_BOOT_GUARD_RETAINED_LINKER) || MSV3_BOOT_GUARD_RETAINED_LINKER != 1
#error "MSV3 requires the tracked boot-guard linker reservation. Build with MSV3/scripts/arduino_msv3_probe.ps1."
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
// 2026-09-03: keep this at the fleet identity. A release is built straight from this source, so
// leaving a bench identity here makes a version number mean two different binaries. The bench
// board currently runs 099, built by editing this line locally and NOT committed -- see
// MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md.
//
// 2026-09-04: DEVICE_NAME is derived from SENSOR_ID (see makeDeviceName()) rather than being a
// second hand-edited constant. A half-finished re-identification used to be able to ship a probe
// advertising one sensor ID under another probe's name -- silently mixing two plants' data in the
// receiver, with no build error and no way to notice over the radio. Unrepresentable now.
// 2026-09-05: SENSOR_ID itself is no longer editable here either. It is a required build
// parameter; see its definition below.
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
// 2026-09-05: this is no longer a constant edited in place. It is a REQUIRED build parameter,
// supplied as -DMSV3_SENSOR_ID=<n> by MSV3/scripts/arduino_msv3_probe.ps1 -SensorId <n>, and the
// build FAILS if it is missing.
//
// Why it changed. As an in-place literal it was a trap with three separate bites, all of which
// were taken:
//   * committed source sat at id 5 -- a probe that cannot even accept an OTA -- so every build
//     for the deployed id-4 probe began with an uncommitted edit;
//   * that edit had to be reverted afterwards, and "never leave a bench identity in committed
//     source" was a rule enforced by memory;
//   * worst, it made a DEPLOYED IMAGE UNREPRODUCIBLE. Probe 4's running firmware was built from
//     a working tree that no commit describes, and its package is gone. You could not rebuild
//     what is flying from anything in this repository.
// As a build parameter, (commit, sensor id) reproduces an image exactly, and forgetting it is a
// compile error rather than a wrong probe.
//
// What stops the wrong image reaching the wrong probe is not care alone. MSV3/fleet.json binds
// each sensor ID to its BLE addresses, and scripts/ble_ota_update.py refuses to jump a board
// whose advertised identity disagrees with the identity compiled into the package.
#ifndef MSV3_SENSOR_ID
#error "MSV3_SENSOR_ID is required. Build through MSV3/scripts/arduino_msv3_probe.ps1 -SensorId <n> (99 = bench board). Do not hardcode it -- a deployed image must be reproducible from (commit, sensor id)."
#endif
static_assert(MSV3_SENSOR_ID >= 0 && MSV3_SENSOR_ID <= 255,
              "MSV3_SENSOR_ID must fit the single advertisement byte at mfg[6]");
const uint8_t SENSOR_ID = MSV3_SENSOR_ID;

// A build identity, so you can tell WHICH image a board is running.
//
// Sensor ID and advertised name identify the board, not the firmware on it: two different builds
// for probe 99 are indistinguishable over the air and over serial. That is fine day to day and
// useless the moment you interrupt an update and need to know whether the old or the new image
// came back. 2026-09-04's interrupted-transfer campaign is exactly that situation.
//
// The bootloader is no help here: dfu_init.c performs NO application-version check at all (its
// version block is an unimplemented upstream template), so the DFU package's application_version
// is never stored on the device and cannot be read back. This string can, in two ways:
//   * printed in the boot banner over USB CDC, whenever the application is valid;
//   * left as a literal in the image, so scripts/msv3_fleet.py reads it straight out of a package
//     and a test can assert that what booted is what it sent.
// Keep it a plain literal for that second reason -- do not build it at runtime.
// Overridable as -DMSV3_BUILD_TAG=<tag> (arduino_msv3_probe.ps1 -BuildTag), so a bench image can
// be told apart from the fleet image it was built beside without editing this file.
//
// A TOKEN, stringified here, rather than a quoted string passed on the command line: getting a
// literal `"` through PowerShell, arduino-cli's --build-property and then the compiler intact is
// fragile and fails quietly, and a build id that silently reverted to the default would defeat the
// one thing it exists for. Stringifying keeps the value alphanumeric all the way down, and the
// result is still a compile-time literal in the image -- which is the property msv3_fleet.py
// depends on to read the build id straight out of a package.
#define MSV3_STRINGIFY_(x) #x
#define MSV3_STRINGIFY(x) MSV3_STRINGIFY_(x)
#ifdef MSV3_BUILD_TAG
#define MSV3_BUILD_ID "MSV3-BUILD:" MSV3_STRINGIFY(MSV3_BUILD_TAG)
#else
#define MSV3_BUILD_ID "MSV3-BUILD:2026-09-05-guard1"
#endif
const char FIRMWARE_BUILD_ID[] = MSV3_BUILD_ID;

// KEEPING THE STRING IN THE IMAGE, which is not automatic and was got wrong once.
//
// The first version of this only referenced FIRMWARE_BUILD_ID from the boot banner. That banner
// lives inside `if (ENABLE_SERIAL_LOGS)`, and ENABLE_SERIAL_LOGS is false in production -- so the
// reference was dead, the Arduino core builds with -fdata-sections -Wl,--gc-sections, and the
// linker discarded the string. Two consecutive test images that differed ONLY in this build id
// came out byte-for-byte identical, which is how it was caught.
//
// A volatile pointer written in setup() fixes it: the write cannot be elided, so the pointer is
// live, so the string it points at is reachable and survives section GC regardless of how the
// serial flag is set. Do not "tidy" this away -- it is load-bearing, and its absence is silent.
volatile const char* g_firmwareBuildIdAnchor = nullptr;

// The advertised name, built from SENSOR_ID so the two can never disagree.
//
// Held in a struct because a constexpr function cannot return a bare array. The result is a
// constexpr object in .rodata, so the literal text "MSV3-MOISTURE-NNN" still appears in the
// compiled image -- scripts/ble_ota_update.py reads it straight out of the signed application
// binary to confirm a package is for the probe it is about to be sent to. Do not replace this
// with a runtime snprintf(): that would leave only the format string in the image and blind
// that check.
struct Msv3DeviceName { char text[18]; };

constexpr Msv3DeviceName makeDeviceName(uint8_t id)
{
  return Msv3DeviceName{{'M', 'S', 'V', '3', '-', 'M', 'O', 'I', 'S', 'T', 'U', 'R', 'E', '-',
                         static_cast<char>('0' + (id / 100) % 10),
                         static_cast<char>('0' + (id / 10) % 10),
                         static_cast<char>('0' + id % 10),
                         '\0'}};
}

constexpr Msv3DeviceName DEVICE_NAME_STORAGE = makeDeviceName(SENSOR_ID);
constexpr const char* DEVICE_NAME = DEVICE_NAME_STORAGE.text;

// SENSOR_ID travels as ONE byte in the advertisement (mfg[6]) while the name shows three decimal
// digits, so a value above 255 could not survive the wire even though it would render. uint8_t
// already bounds it; this pins the assumption in place if the type is ever widened.
static_assert(sizeof(SENSOR_ID) == 1, "SENSOR_ID must stay one byte -- it is mfg[6] on the wire");
static_assert(sizeof(DEVICE_NAME_STORAGE.text) == 18,
              "Advertised name must stay MSV3-MOISTURE-NNN plus its NUL");
static_assert(DEVICE_NAME_STORAGE.text[14] == '0' + (SENSOR_ID / 100) % 10 &&
              DEVICE_NAME_STORAGE.text[15] == '0' + (SENSOR_ID / 10) % 10 &&
              DEVICE_NAME_STORAGE.text[16] == '0' + SENSOR_ID % 10,
              "Advertised name digits must equal SENSOR_ID");

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
#ifndef MSV3_WAKE_DISCARD_CONVERSIONS
#define MSV3_WAKE_DISCARD_CONVERSIONS 2
#endif
static_assert(MSV3_WAKE_DISCARD_CONVERSIONS >= 0 && MSV3_WAKE_DISCARD_CONVERSIONS <= 255,
              "MSV3_WAKE_DISCARD_CONVERSIONS must fit in uint8_t");
const uint8_t WAKE_DISCARD_CONVERSIONS = MSV3_WAKE_DISCARD_CONVERSIONS;

// Production defaults: no USB-host wait/log traffic and no artificial LED load.
//
// 2026-09-05: serial logging is a BUILD FLAG (-DMSV3_ENABLE_SERIAL_LOGS=1, via
// arduino_msv3_probe.ps1 -SerialLogs) rather than a line to edit and remember to put back. It
// defaults off, so the committed source always describes a deployable image -- which is the same
// reproducibility property SENSOR_ID above now has, and for the same reason.
#ifndef MSV3_ENABLE_SERIAL_LOGS
#define MSV3_ENABLE_SERIAL_LOGS 0
#endif
const bool ENABLE_SERIAL_LOGS = (MSV3_ENABLE_SERIAL_LOGS != 0);
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
// ERROR_CONFIG (0x19), added 2026-09-05. Until now this register was never written, and it resets
// to all zeros after every hardware shutdown -- which this firmware performs on every wake. Table
// 7-37 (verified against the local datasheet extract, not recalled):
//
//   bit 13  WD_ERR2OUT   report watchdog timeouts in DATA_CHx.CHx_ERR_WD
//   bit 12  AH_WARN2OUT  report amplitude-high warnings in DATA_CHx.CHx_ERR_AW
//   bit 11  AL_WARN2OUT  report amplitude-low warnings in DATA_CHx.CHx_ERR_AW
//   bit  5  WD_ERR2INT   report watchdog timeouts by updating STATUS.ERR_WD
//   bit  0  DRDY_2INT    report data-ready by updating STATUS.DRDY
//
// WHY THIS MATTERS: probeAndAdvertise() clears measurementOk on STATUS.ERR_WD (bit 11) -- the
// "the tank is not oscillating" signal, which is what a shorted, waterlogged or disconnected
// sensor looks like. With WD_ERR2INT at its reset value of 0 the datasheet says STATUS.ERR_WD is
// never updated, so that check could not fire and the probe would report a dead sensor's garbage
// count as a moisture reading. It was dead code guarding the failure it exists to catch.
//
// NOT SET: DRDY_2INT (bit 0). It stays 0 deliberately. STATUS.DRDY is a trap this project has
// fallen into three times (see docs/KNOWN_ISSUES.md); the readiness poll uses the per-channel
// CH0_UNREADCONV flag, which needs no enable bit. Turning DRDY_2INT on would make a DRDY poll
// start working on the bench and fail again on any board or path that misses this write. Leave it
// off so the wrong approach stays visibly broken.
//
// ONE CORRECTION TO THE OLD KNOWN_ISSUES WORDING: it grouped ERR_WD, ERR_AHW and ERR_ALW together
// as "switched off, not clear". Only ERR_WD is gated. ERROR_CONFIG has no AH_WARN2INT/AL_WARN2INT
// bit at all, so STATUS.ERR_AHW (bit 10) and STATUS.ERR_ALW (bit 9) have no enable and were
// already live -- the amplitude half of probeAndAdvertise()'s check has been working all along.
// Only the watchdog half was dead.
//
// NOT SET EITHER: the three *2OUT bits (13/12/11). Their destination is DATA_CHx bits 13/12, and
// everything they would report there is already in STATUS, which this firmware reads first and
// deliberately (see fdcReadChannel0()). Setting them would be configuration nothing consumes.
#define FDC_ERROR_CONFIG_VALUE 0x0020

// Diagnostic build parameters. Production defaults preserve the committed configuration, while
// the guarded build wrapper can make one-at-a-time FDC settling experiments reproducible without
// editing this file between releases. The build tag records the selected stage in the image/DIS.
#ifndef MSV3_FDC_SETTLECOUNT_CH0
#define MSV3_FDC_SETTLECOUNT_CH0 0x0064
#endif
#ifndef MSV3_FDC_MUX_CONFIG
#define MSV3_FDC_MUX_CONFIG 0x020D
#endif
static_assert(MSV3_FDC_SETTLECOUNT_CH0 >= 1 && MSV3_FDC_SETTLECOUNT_CH0 <= 0xFFFF,
              "MSV3_FDC_SETTLECOUNT_CH0 must be 1..65535");
static_assert(MSV3_FDC_MUX_CONFIG == 0x020D || MSV3_FDC_MUX_CONFIG == 0x020F,
              "MSV3_FDC_MUX_CONFIG must select the reviewed 10MHz or 33MHz CH0 configuration");

bool fdcInit()
{
  bool ok = true;
  ok &= fdcWriteReg(0x08, 0xFFFF);  // RCOUNT_CH0 -- see 2026-08-30 comment above
  ok &= fdcWriteReg(0x10, MSV3_FDC_SETTLECOUNT_CH0);  // SETTLECOUNT_CH0
  ok &= fdcWriteReg(0x14, 0x1001);  // CLOCK_DIVIDERS_CH0: FIN_SEL=01, FREF_DIVIDER=1
  ok &= fdcWriteReg(0x1E, 0xC800);  // DRIVE_CURRENT_CH0: IDRIVE=25 (0.644mA), bench-tuned 2026-09-03
  // MUX_CONFIG (0x1B): AUTOSCAN_EN=0 / RR_SEQUENCE=00 keeps single-channel CH0, which is
  // correct for this board (CH1-3 grounded per netlist) and is also the POR default. What is
  // NOT the POR default is DEGLITCH: its reset value is b111 (33MHz), the widest filter.
  //
  // 2026-09-09: measured this board's real tank frequency from its own telemetry rather than
  // the design range. fSENSOR = (DATA / 2^28) * fREF, with CLOCK_DIVIDERS_CH0=0x1001
  // (CH0_FIN_SEL=1, FREF_DIVIDER=1) and fREF = the ~43.3MHz internal oscillator. Probe 4's
  // field data gives 19.69M counts connected -> 3.18MHz, and 22.48M counts on the bare-tank
  // reference -> 3.63MHz. So the tank spans roughly 3.2-3.6MHz depending on electrode loading.
  //
  // The datasheet says to select the lowest deglitch setting that EXCEEDS the sensor
  // oscillation frequency. That is b101 (10MHz) here, not b100 (3.3MHz): 3.3MHz sits BELOW the
  // 3.63MHz bare-tank reading, so b100 would filter the reference channel's own signal. b101
  // keeps margin over the whole 3.2-3.6MHz span while admitting 3.3x less EMI/ringing
  // bandwidth than the 33MHz default.
  //
  // Bits [12:3] are reserved and the datasheet requires them to be written as 00 0100 0001,
  // so the full word is 0x208 | DEGLITCH -> 0x020D. Do not write a bare 0x0005 here.
  //
  // This is a noise-bandwidth change only. It is NOT expected to affect the daily thermal
  // drift on soil-connected probes -- that separates connected (1.245% 24h p-p) from the
  // bare-tank reference (0.096%) on the same chip, so it enters through the electrode/soil
  // path, not through anything this register controls.
  //
  // Datasheet trap: Table 7-11 lists 33MHz as b011 while the Address 0x1B field description
  // lists b111. The field description carries the reset value and is what this follows.
  ok &= fdcWriteReg(0x1B, MSV3_FDC_MUX_CONFIG);  // MUX_CONFIG: CH0 only, reviewed deglitch setting
  ok &= fdcWriteReg(0x19, FDC_ERROR_CONFIG_VALUE);  // see FDC_ERROR_CONFIG_VALUE above -- MUST be
                                                    // written before CONFIG starts conversions
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
// 2026-09-05: statusOut now ACCUMULATES every flag seen while polling, instead of holding only
// the most recent read, and is returned to the caller even when this times out.
//
// Two datasheet facts make the old version lose information exactly when it is most wanted.
// STATUS is read-to-clear -- Table 7-36: an error "is latched and maintained until either the
// STATUS register or the DATA_CHx register ... is read" -- so THIS POLLING LOOP consumes the very
// flags it is looking past. And a watchdog timeout does not produce an unread conversion, so on a
// tank that has stopped oscillating CH0_UNREADCONV never sets and the loop runs to timeout,
// throwing away each status word as it goes.
//
// That was measured on 2026-09-05 (MSV3_FDC_ERROR_CONFIG_2026-09-05.md): with the tank killed by
// IDRIVE=0, one poll returned STATUS=0x800 -- ERR_WD, the flag this firmware has an entire branch
// for -- and the old code discarded it and returned false. Behaviour was unaffected, because
// measurementOk goes false on the timeout anyway, but the REASON was lost: "the oscillator is
// dead" became indistinguishable from "the I2C bus is dead" and from "the part is misconfigured".
//
// OR-accumulating is what preserves a transient: returning only the final word would still lose a
// flag that a previous poll had already cleared.
bool fdcWaitForCh0UnreadConversion(uint16_t &statusOut)
{
  uint32_t start = millis();
  uint16_t seen = 0;
  statusOut = 0;
  do
  {
    uint16_t status;
    if (!fdcReadStatus(status))
    {
      statusOut = seen;  // hand back whatever was observed before the bus failed
      return false;
    }
    seen |= status;
    if (status & FDC_STATUS_CH0_UNREADCONV_BIT)
    {
      statusOut = seen;
      return true;
    }
  } while (millis() - start < FDC_CONVERSION_POLL_TIMEOUT_MS);
  // Timed out waiting for a fresh conversion. Still the same failure path as an I2C error as far
  // as measurementOk is concerned -- but the caller can now tell the two apart.
  statusOut = seen;
  return false;
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

// NAME IS MISLEADING, KEPT because it is the receiver-visible wire field name. It counts
// ADVERTISEMENTS, not wakes: the increment sits inside loop()'s `if (shouldAdvertise)` block, so
// it advances about once per ADVERTISE_INTERVAL_MS (60s), not once per WAKE_PERIOD_MS (10s)
// check -- plus an extra step whenever a big swing forces an off-cadence advertisement.
//
// This matters when reading a counter off the air: multiplying it by 10s understates uptime by
// roughly 6x. On 2026-09-04 that arithmetic briefly made two healthy probes look as though they
// had both reset that morning, when in fact they had been running continuously overnight.
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

// How long to let the DFU authorize response settle on air before arming GPREGRET and resetting.
// sd_ble_gatts_rw_authorize_reply() returning NRF_SUCCESS only means the SoftDevice accepted the
// response for transmission; resetting immediately can tear the radio down before it is actually
// sent, which leaves the client waiting forever on a write it will never see answered. The reset
// therefore happens here, out of the BLE event callback, one connection interval or two later.
const uint32_t DFU_RESPONSE_SETTLE_MS = 150;

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

// Device Information Service, so a probe can say which firmware it is running.
//
// Until 2026-09-04 there was no way to ask. FIRMWARE_BUILD_ID is in the image and in the boot
// banner, but ENABLE_SERIAL_LOGS is false in production and a coated probe has no cable, so the
// only remote identity was the sensor ID -- which says which BOARD, never which BUILD. After an
// interrupted update that is exactly the question you need answered, and the bootloader cannot
// answer it either: dfu_init.c never implements the application-version check, so the DFU
// package's version is not persisted anywhere on the device.
//
// DIS is the right place for it. It costs ZERO advertising bytes -- startAdv()'s static_assert
// shows the 31-byte legacy PDU is already exactly full -- because it is read over GATT after a
// central connects. scripts/ble_ota_update.py already connects to write the DFU jump, so it reads
// this first and can refuse an update when the probe is not running what the registry expects.
BLEDis bledis;

bool g_otaServiceReady = false;
bool g_disReady = false;
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

  // Registered in GATT only, like the DFU service, and for the same reason: there is no room in
  // the advertisement. A central discovers it after connecting.
  bledis.setManufacturer("Treebeard");
  bledis.setModel("MSV3");
  bledis.setFirmwareRev(FIRMWARE_BUILD_ID);
  g_disReady = (bledis.begin() == ERROR_NONE);

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
//
// bit5 (2026-09-04) is a SAFETY flag, not a data flag. Until today a failed FDC read suppressed
// the whole advertisement -- see the comment on shouldAdvertise in loop() -- which meant a dead
// sensor also closed the only way to reach a coated probe over the radio. The probe now always
// advertises on its interval and marks the reading instead. The count field is zeroed when this
// bit is set, so a receiver that ignores the bit stores an obviously-wrong 0 rather than a
// plausible wrong number.
//
// RECEIVER COUPLING: Treebeard_ESP32_Display_Runtime must skip ingestion when this bit is set.
// Until that ships, an un-updated receiver will store calibrateMsv3Moisture(0) for a faulted
// probe. Do not deploy this to a field probe before the receiver understands bit5.
#define ADV_FLAG_CHARGING        (1U << 0)
#define ADV_FLAG_PHASE           (1U << 1)
#define ADV_FLAG_BOARD_TEMP_OK   (1U << 2)
#define ADV_FLAG_BARE_TANK_OK    (1U << 3)
#define ADV_FLAG_READING_INVALID (1U << 5)
#define ADV_FLAG_OTA_POWER_OK    (1U << 4)

bool startAdv(uint32_t rawDataCh0, float rawBatteryMv, bool isCharging, bool otaPowerOk, uint8_t phaseUsed,
              bool boardTempValid, int16_t boardTempQuarterC,
              bool bareTankValid, uint32_t bareTankDataCh0, bool readingValid)
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
            (otaPowerOk ? ADV_FLAG_OTA_POWER_OK : 0) |
            (readingValid ? 0 : ADV_FLAG_READING_INVALID);
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

// ===================================================== boot-failure guard ====
//
// The problem it solves: a firmware image can be correctly signed, pass the bootloader's CRC,
// boot -- and then crash or hang before it ever opens a BLE advertising window. The bootloader has
// already handed over by then, and DEFAULT_TO_OTA_DFU only rescues a board with NO valid
// application, so such a probe is reachable only over SWD. On a coated probe that means never.
//
// The escape: count boots that never reached healthy operation, and after BOOT_GUARD_LIMIT of
// them, jump into BLE DFU instead of trying the same broken path again. initWatchdogSafety() is
// already the first statement in setup(), so a HANG becomes a watchdog reset and feeds this
// counter too -- crashes and hangs are both covered.
//
// RETENTION FIX, 2026-09-08. The original counter used GPREGRET2. A self-reset test appeared to
// prove it, but the only test that mattered -- a real watchdog expiry while the application was
// hung before advertising -- showed boot attempt 1 forever. SWD saw 0xB1 while each app instance
// was hung, proving the write happened, while the next app instance still read no prior count.
// That localizes the loss to the reset/startup path before bootGuardCheck(), NOT to the nRF52840
// hardware itself: the product specification calls GPREGRET2 retained, the visible Adafruit
// bootloader source never writes it, and the MBR/SoftDevice path is binary. Do not turn the
// observation into a claim about which unseen layer clears or overwrites it.
//
// The replacement is an eight-byte .noinit record at 0x2003FFF8. It is a LINKER ABI, not merely a
// section attribute: the application linker excludes that address from its heap/stack, and MSV3
// bootloader 1.0.3 excludes the same address from its stack. Two inverse 32-bit words reject a
// torn or random record. Only watchdog, software-reset and lockup causes carry a valid count;
// power-on/brownout (RESETREAS=0), pin reset and System OFF wake all begin at zero, preserving the
// original false-positive protection for a solar node.
//
// WHAT ENTERING DFU COSTS, because it is not free and this is the guard's real risk. The
// bootloader advertises CONTINUOUSLY, where the application advertises ~5 s per 70 s, and it does
// not sense. It also does not time out: main.c only passes a 3 s timeout on the USB/UF2 branch,
// and a GPREGRET OTA jump takes bootloader_dfu_start(_ota_dfu, 0, false). So a probe that escapes
// stays in DFU until someone flashes it. A false positive therefore costs that probe's data.
//
// LOW BATTERY DEFERS THE ESCAPE, it does not cancel it. The first version escaped unconditionally,
// on the reasoning that a flat solar probe recovers with sun while an unreachable probe never
// does. That reasoning had a hole: a marginal supply can reset the MCU by a route that does NOT
// clear GPREGRET2, so brownout-driven resets can accumulate the counter and dump a probe with
// perfectly good firmware into DFU. And DFU does not time out on this path -- main.c passes a 3 s
// timeout only on the USB/UF2 branch, while a GPREGRET jump takes
// bootloader_dfu_start(_ota_dfu, 0, false) -- so "the sun comes up" does not bring it back. It
// sits there, not sensing, until someone flashes it.
//
// So below BOOT_GUARD_MIN_ESCAPE_MV the counter SATURATES and the boot continues. Two things
// follow, and both are wanted: if the firmware was fine and the resets were undervoltage, the next
// healthy day clears probation and no false positive ever happens; if the firmware really is
// broken, the escape simply waits until there is enough charge to be worth rescuing into.
//
// The check is made only at the escape point, not on every boot, so an ordinary boot still reaches
// the guard with nothing between it and initWatchdogSafety(). It FAILS OPEN: an out-of-range or
// unavailable reading escapes rather than deferring, because being unable to measure the battery
// must never be the reason a probe stays unreachable.
// STAGING FLAG. Guard 2 is bench-proven for the crash-loop (soft-reset) path -- counter
// accumulation, escape at the limit, landing in BLE DFU, and clean rescue without re-escape were
// all demonstrated on probe 99. What has NOT been demonstrated is the WATCHDOG-HANG path: the
// bench crash-loop build self-reset rather than hanging, deliberately, because with no SWD
// attached and no reset button on this PCB a hang before advertising is the exact unrecoverable
// state this guard exists to prevent.
//
// So the first field image carries guard 1 only, and this stays false until that test has been
// run with SWD standing by. Set it true for bench builds and for the field once it has.
// 2026-09-07: now a BUILD FLAG rather than an edited constant, same as SENSOR_ID and
// ENABLE_SERIAL_LOGS before it, and for the same reason -- committed source must always describe
// a deployable image, and "flip this line, build, remember to flip it back" is exactly how probe
// 4's firmware became unreproducible. Pass -BootGuard to arduino_msv3_probe.ps1 to enable it.
#ifndef MSV3_ENABLE_BOOT_FAILURE_GUARD
#define MSV3_ENABLE_BOOT_FAILURE_GUARD 0
#endif
constexpr bool ENABLE_BOOT_FAILURE_GUARD = (MSV3_ENABLE_BOOT_FAILURE_GUARD != 0);

// THE GUARD-2 / BOOTLOADER RAM PAIRING, STATED IN THE IMAGE SO A HOST CAN CHECK IT.
//
// Guard 2's boot counter lives at 0x2003FFF8 and only survives a reset if the BOOTLOADER also
// excludes those eight bytes from its stack. Measured from vector[0] of each release hex:
// bootloader 1.0.1 and 1.0.2 both boot with MSP = 0x20040000, so the record is the FIRST thing
// their stacks descend into; 1.0.3 boots with MSP = 0x2003FFF8 and leaves it alone.
//
// So a guard-enabled application on a pre-1.0.3 bootloader gives you a guard that looks armed,
// never accumulates a count, and never escapes into DFU -- which is precisely the GPREGRET2
// symptom this whole retained-RAM scheme was built to replace. MSV3/bootloader/README.md already
// says "Guard 2 must not be enabled with bootloader 1.0.2 or older"; until now nothing enforced
// it, and probe 4 is deployed, soil-immersed, solar, has no SWD, and runs 1.0.1.
//
// Stamping it into the image is what makes the rule enforceable at the only point that matters:
// ble_ota_update.py reads this marker out of a package and refuses to send a `:1` image to a
// probe whose fleet.json bootloader predates 1.0.3. `:0` is emitted deliberately rather than
// nothing, so "guard off" and "image too old to say" stay distinguishable -- every package built
// before 2026-09-09 has no marker at all, and those are the archived rescue images for probes
// about to be coated, so absence must not become a refusal.
#if MSV3_ENABLE_BOOT_FAILURE_GUARD
#define MSV3_GUARD2_MARKER "MSV3-GUARD2:1"
#else
#define MSV3_GUARD2_MARKER "MSV3-GUARD2:0"
#endif
const char FIRMWARE_GUARD2_MARKER[] = MSV3_GUARD2_MARKER;
// Same load-bearing anchor as g_firmwareBuildIdAnchor above, for the same reason: nothing else
// reads this string, so without a volatile write section GC would discard it and the host check
// would silently see an unmarked image. Assigned beside the build-id anchor in setup().
volatile const char* g_guard2MarkerAnchor = nullptr;

// DELIBERATE HANG FOR THE GUARD-2 ACCEPTANCE TEST. -DMSV3_HANG_BEFORE_ADVERTISE=1.
//
// This is the state guard 2 exists to rescue: an image that boots, is correctly signed, passes
// every integrity check, and then never opens a radio window. Every previous guard-2 test used a
// deliberate SELF-RESET instead, because with no SWD attached and no reset button on this PCB, a
// board that hangs before advertising is unrecoverable -- so the test that mattered was the one
// that could not safely be run. It is runnable now only because SWD is attached.
//
// The hang is placed after initWatchdogSafety() and bootGuardCheck() so the watchdog is armed and
// the counter has already been incremented for this boot. It must NOT be fed: the whole point is
// that the 5 s watchdog fires, resets the MCU, and the boot counter accumulates until
// BOOT_GUARD_LIMIT sends the board into BLE DFU.
//
// NEVER SHIP THIS. The build check in arduino_msv3_probe.ps1 refuses to build it together with a
// fleet sensor id.
#ifndef MSV3_HANG_BEFORE_ADVERTISE
#define MSV3_HANG_BEFORE_ADVERTISE 0
#endif
constexpr bool HANG_BEFORE_ADVERTISE = (MSV3_HANG_BEFORE_ADVERTISE != 0);

constexpr uint32_t BOOT_GUARD_RETAINED_ADDRESS = 0x2003FFF8UL;
constexpr uint32_t BOOT_GUARD_STATE_MAGIC = 0xB0075200UL;
constexpr uint32_t BOOT_GUARD_STATE_MAGIC_MASK = 0xFFFFFF00UL;
constexpr uint8_t BOOT_GUARD_COUNT_MASK = 0x0F;
constexpr uint8_t BOOT_GUARD_LIMIT = 5;
constexpr uint8_t BOOT_GUARD_PROBATION_ADVERTS = 2;
// PROVISIONAL. Shares the OTA policy threshold rather than being derived from measurement: the
// DFU-mode current, and therefore how long a probe can survive unattended in it, has not been
// measured. Revisit once it has.
constexpr uint16_t BOOT_GUARD_MIN_ESCAPE_MV = OTA_MIN_BATTERY_MV;
constexpr uint8_t DFU_MAGIC_OTA_RESET = 0xA8;   // must match the bootloader's own constant

struct BootGuardRetainedState
{
  uint32_t encoded;
  uint32_t inverse;
};
static_assert(sizeof(BootGuardRetainedState) == 8,
              "boot-guard retained state must match the eight-byte linker reservation");

// No initializer: startup must not emit a .data copy or a .bss clear for this object. The custom
// linker script maps this exact input section to a fixed NOLOAD region and asserts its size.
static volatile BootGuardRetainedState g_bootGuardRetained
    __attribute__((section(".msv3_boot_guard_retained"), used, aligned(4)));

static uint8_t g_bootAttempt = 0;
static uint8_t g_probationAdverts = 0;
static bool g_bootGuardCleared = false;
static bool g_bootGuardDeferred = false;
static float g_bootGuardBatteryMv = 0.0f;
static uint32_t g_resetReason = 0;

static bool bootGuardReadRetained(uint8_t& count)
{
  const uint32_t encoded = g_bootGuardRetained.encoded;
  const uint32_t inverse = g_bootGuardRetained.inverse;
  if (inverse != ~encoded ||
      (encoded & BOOT_GUARD_STATE_MAGIC_MASK) != BOOT_GUARD_STATE_MAGIC)
  {
    count = 0;
    return false;
  }

  const uint8_t decoded = (uint8_t)(encoded & 0xFF);
  if (decoded > BOOT_GUARD_COUNT_MASK)
  {
    count = 0;
    return false;
  }
  count = decoded;
  return true;
}

static void bootGuardWriteRetained(uint8_t count)
{
  const uint8_t bounded = count > BOOT_GUARD_COUNT_MASK
                              ? BOOT_GUARD_COUNT_MASK
                              : count;
  const uint32_t encoded = BOOT_GUARD_STATE_MAGIC | bounded;

  // Invalidate first. A reset between either following word write fails open as an invalid record
  // on the next boot rather than manufacturing a high boot count and falsely entering DFU.
  g_bootGuardRetained.inverse = 0;
  __DMB();
  g_bootGuardRetained.encoded = encoded;
  __DMB();
  g_bootGuardRetained.inverse = ~encoded;
  __DSB();
}

static bool bootGuardResetCarriesState(uint32_t reason)
{
  // RESETREAS has no bit for power-on/brownout, so zero deliberately means a clean slate. Carry
  // only causes that can represent an application failure loop. SREQ remains included because
  // both a commanded reset and an app_error/SoftDevice reset can produce it; under-counting the
  // latter would reopen the exact failure class this guard exists to close.
  const uint32_t carryMask = POWER_RESETREAS_DOG_Msk |
                             POWER_RESETREAS_SREQ_Msk |
                             POWER_RESETREAS_LOCKUP_Msk;
  return reason != 0 && (reason & carryMask) != 0;
}

// Prints RESETREAS by name. The guard-2 watchdog-hang test has to SEE "WATCHDOG" on each of the
// successive resets, not infer it from a hex value -- that reading IS the evidence the test
// produces, and it was unavailable while the cause always read 0x0. Mirrors the decoder the
// bring-up sketch has always had.
static void printResetReasonName(uint32_t r)
{
  Serial.print(F(" ("));
  if (r == 0) Serial.print(F("POWER-ON/BROWNOUT -- no reset bits set"));
  if (r & POWER_RESETREAS_RESETPIN_Msk) Serial.print(F("RESETPIN "));
  if (r & POWER_RESETREAS_DOG_Msk)      Serial.print(F("WATCHDOG "));
  if (r & POWER_RESETREAS_SREQ_Msk)     Serial.print(F("SOFTWARE(NVIC_SystemReset) "));
  if (r & POWER_RESETREAS_LOCKUP_Msk)   Serial.print(F("CPU-LOCKUP "));
  if (r & POWER_RESETREAS_OFF_Msk)      Serial.print(F("WAKE-FROM-OFF "));
  Serial.print(F(")"));
}

// Cleared once the probe has proven it can actually run: BOOT_GUARD_PROBATION_ADVERTS complete
// advertising cycles, not merely "advertising was started once". A build that advertises and then
// dies later in its first duty cycle must NOT be able to clear the guard.
// The original GPREGRET2 implementation needed SoftDevice SVCs here and its first raw-register
// version hard-faulted after the second advertisement. The retained-RAM implementation has no
// POWER-peripheral access at all, so it is legal after Bluefruit.begin(). Do not move the counter
// back to a protected peripheral just to reuse that old SVC plumbing.
void bootGuardNoteHealthyAdvert()
{
  if (!ENABLE_BOOT_FAILURE_GUARD) return;
  if (g_bootGuardCleared) return;
  if (++g_probationAdverts < BOOT_GUARD_PROBATION_ADVERTS) return;

  bootGuardWriteRetained(0);
  g_bootGuardCleared = true;
  if (ENABLE_SERIAL_LOGS)
  {
    Serial.print(F("[DIAG] boot guard cleared after "));
    Serial.print(g_probationAdverts);
    Serial.println(F(" advertising cycles"));
    Serial.flush();
  }
}

// Runs before anything that can fail. The only raw POWER-register access left is GPREGRET at the
// actual DFU escape, before Bluefruit.begin(); the boot counter itself is ordinary retained RAM.
static void bootGuardCheck()
{
  // Take the reset cause from the CORE, not from the hardware register.
  //
  // 2026-09-06, correcting a wrong diagnosis that has been on record since 2026-09-04. This used
  // to read NRF_POWER->RESETREAS directly and always got 0x0, and that was written up as "the
  // bootloader runs first and appears to consume it". The bootloader does no such thing --
  // bootloader src/main.c says so explicitly: "RESETREAS is not clear by bootloader, it should be
  // cleared by application upon init()".
  //
  // The Arduino core consumes it, before setup() ever runs. cores/nRF5/wiring.c init():
  //     _reset_reason = NRF_POWER->RESETREAS;
  //     NRF_POWER->RESETREAS |= NRF_POWER->RESETREAS;   // write-1-to-clear
  // and it saves the value for exactly this purpose, behind readResetReason() (wiring.h, reached
  // via Arduino.h). So the old direct read was guaranteed to return 0 on every boot, on any board,
  // with any bootloader. The bring-up sketch already used the core accessor and has always
  // reported real causes -- RESETPIN on the four-board bring-up, and 0x2 WATCHDOG mid-cycle.
  //
  // Same class of bug as the one that made the FIRST version of this guard reset every ~76 s:
  // reaching for a register directly where the framework has already mediated it. Third instance
  // in this file's history. Do not "simplify" this back to NRF_POWER->RESETREAS.
  g_resetReason = readResetReason();
  if (!ENABLE_BOOT_FAILURE_GUARD)
  {
    // Guard-off builds are the field default. Keep the shared word at a known zero so enabling the
    // guard in a later OTA cannot inherit stale probation from an earlier bench image.
    bootGuardWriteRetained(0);
    return;
  }

  // WHY THE COUNTER STILL COUNTS EVERY BOOT, now that the cause is actually available.
  //
  // The obvious next step is to count only DOG/LOCKUP so the return boot after a commanded DFU
  // jump stops consuming an attempt. Deliberately NOT done, because SREQ is ambiguous in the one
  // direction that matters: a commanded DFU jump is an SREQ, but so is a crash routed through the
  // SoftDevice/app_error handler, which resets via NVIC_SystemReset(). Filtering SREQ out would
  // silently stop counting a whole class of real crash loops -- exactly what this guard exists to
  // catch -- to save one attempt out of five in a case that is already self-correcting, since
  // probation clears the counter within ~140 s of healthy running.
  //
  // Over-counting is the safe direction here and under-counting is not, so the cause is used for
  // reporting only. Revisit ONLY with evidence about what this application's fault handlers
  // actually do on a hard fault; that is a bench question with SWD attached, not an assumption.

  uint8_t retainedCount = 0;
  const bool retainedValid = bootGuardReadRetained(retainedCount);
  g_bootAttempt = retainedValid && bootGuardResetCarriesState(g_resetReason)
                      ? retainedCount
                      : 0;

  if (g_bootAttempt >= BOOT_GUARD_LIMIT)
  {
    // Saturate first, so a deferred decision survives to the next boot instead of being lost.
    bootGuardWriteRetained(BOOT_GUARD_COUNT_MASK);

    g_bootGuardBatteryMv = readVbatMv();
    const bool readingUsable =
        g_bootGuardBatteryMv >= 2500.0f && g_bootGuardBatteryMv <= 5000.0f;
    if (!readingUsable || g_bootGuardBatteryMv >= (float)BOOT_GUARD_MIN_ESCAPE_MV)
    {
      // Reset the counter BEFORE arming DFU. Otherwise a good image flashed afterwards would
      // boot, read a saturated count, and bounce straight back into DFU -- turning the rescue
      // into a trap. (Verified on the bench: the rescued build boots once and stays up.)
      bootGuardWriteRetained(0);
      NRF_POWER->GPREGRET = DFU_MAGIC_OTA_RESET;
      __DSB();
      NVIC_SystemReset();
      // does not return
    }
    g_bootGuardDeferred = true;
  }

  const uint8_t next = (uint8_t)(g_bootAttempt + 1);
  bootGuardWriteRetained(next);
}

void setup()
{
  // Anchor before every deliberately failing test path. A hang build never reaches the normal
  // boot banner below, but its signed package still has to remain uniquely identifiable.
  g_firmwareBuildIdAnchor = FIRMWARE_BUILD_ID;
  g_guard2MarkerAnchor = FIRMWARE_GUARD2_MARKER;

  initWatchdogSafety();
  bootGuardCheck();

  if (HANG_BEFORE_ADVERTISE)
  {
    // Announce first so the capture shows WHICH boot is hanging and what reset caused it -- the
    // reset cause is only readable at all since readResetReason() replaced the direct
    // NRF_POWER->RESETREAS read, which always returned 0. Serial.flush() because the watchdog
    // fires in 5 s and an unflushed USB CDC buffer would be lost with it.
    if (ENABLE_SERIAL_LOGS)
    {
      Serial.begin(115200);
      uint32_t w = millis();
      while (!Serial && millis() - w < BOOT_HOST_WAIT_MS) delay(10);
      Serial.print(F("[HANGTEST] boot attempt "));
      Serial.print(g_bootAttempt + 1);
      Serial.print(F(" of "));
      Serial.print(BOOT_GUARD_LIMIT);
      Serial.print(F("; RESETREAS=0x"));
      Serial.print(g_resetReason, HEX);
      printResetReasonName(g_resetReason);
      Serial.print(F(" -- "));
      Serial.print(DEVICE_NAME);
      Serial.print(F(" "));
      Serial.print(FIRMWARE_BUILD_ID);
      Serial.println(F(" hanging now, NOT feeding the watchdog"));
      Serial.flush();
    }
    // Keep DEVICE_NAME referenced even when serial logging is off. Without this the linker
    // dead-strips the whole advertising path -- the hang makes it unreachable -- and the
    // "MSV3-MOISTURE-NNN" literal goes with it. That is not cosmetic: msv3_fleet.py extracts that
    // string to bind a package to a probe, so a stripped image cannot be identified, the release
    // build refuses to sign it, and ble_ota_update.py would refuse to send it. Found the hard way
    // -- the first hang build came out 93 KB instead of 136 KB and its own release script deleted
    // it. asm volatile with a memory clobber so the reference cannot be optimised away.
    { const char* keep = DEVICE_NAME; asm volatile("" :: "r"(keep) : "memory"); }
    for (;;) { /* no feedWatchdog() -- the WDT must fire */ }
  }

  if (ENABLE_SERIAL_LOGS)
  {
    Serial.begin(115200);
    uint32_t hostWaitStart = millis();
    while (!Serial && millis() - hostWaitStart < BOOT_HOST_WAIT_MS) delay(10);
    delay(300);
    Serial.println(F("[DIAG] MSV3 production probe booting"));
    Serial.print(F("[DIAG] build: "));
    Serial.println(FIRMWARE_BUILD_ID);
    Serial.print(F("[DIAG] boot attempt "));
    Serial.print(g_bootAttempt + 1);
    Serial.print(F(" of "));
    Serial.print(BOOT_GUARD_LIMIT);
    Serial.print(F(" before forced DFU; RESETREAS=0x"));
    Serial.print(g_resetReason, HEX);
    printResetReasonName(g_resetReason);
    Serial.println();
    if (g_bootGuardDeferred)
    {
      Serial.print(F("[WARN] boot guard reached its limit but DEFERRED the DFU escape: "));
      Serial.print((int)(g_bootGuardBatteryMv + 0.5f));
      Serial.print(F(" mV is below "));
      Serial.print(BOOT_GUARD_MIN_ESCAPE_MV);
      Serial.println(F(" mV. It will retry every boot until the battery recovers."));
    }
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

  // Reported here rather than in the boot banner above: that runs before bleInit(), so it could
  // only ever have printed "not ready".
  if (ENABLE_SERIAL_LOGS)
  {
    Serial.print(F("[DIAG] firmware revision readable over BLE (DIS 0x2A26): "));
    Serial.println(g_disReady ? F("yes") : F("NO -- registration failed"));
    Serial.flush();
  }

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
  else if (ENABLE_SERIAL_LOGS)
  {
    // The read failed. Since 2026-09-05 fdcWaitForCh0UnreadConversion() hands back every flag it
    // saw while polling even on a timeout, so this can say WHY rather than just "failed" -- an
    // ERR_WD here means the oscillator is not running, which a bare timeout could not distinguish
    // from a dead bus. Diagnostic only: measurementOk is already false either way, and nothing on
    // the wire changes.
    Serial.print(F("[WARN] FDC read failed; flags seen while polling: 0x"));
    Serial.print(fdcStatus, HEX);
    if (!wakeOk) Serial.print(F(" (wake failed)"));
    else if (fdcStatus & (1 << 11)) Serial.print(F(" ERR_WD -- oscillator not running"));
    else if (fdcStatus == 0) Serial.print(F(" (no flags at all -- bus or configuration fault)"));
    Serial.println();
    Serial.flush();
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
  // 2026-09-04: NO LONGER gated on measurementOk. It used to read
  //     shouldAdvertise = measurementOk && (intervalElapsed || bigSwing)
  // which meant a failed FDC wake, a failed I2C read or an ERR_WD status suppressed the whole
  // advertisement -- so a dead sensor, a stuck bus or water in the connector silently closed the
  // only channel through which a coated probe can be reached or updated. A sensor fault must not
  // be able to turn into an unrecoverable probe.
  //
  // bigSwing still requires a valid reading: it compares rawData against the last advertised
  // value, and letting a garbage count trigger an off-cadence advertisement would both waste power
  // and pollute the swing logic. An invalid reading therefore goes out on the normal interval only.
  bool shouldAdvertise = intervalElapsed || (measurementOk && bigSwing);

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
    // Zero the count when the reading is not real, so a receiver that does not yet understand
    // ADV_FLAG_READING_INVALID stores an obviously-wrong 0 rather than a believable wrong number.
    bool advStarted = startAdv(measurementOk ? rawData : 0u,
                               batteryMv, charging, otaPowerOk, phaseUsed,
                               boardTempValid, boardTempQuarterC,
                               bareTankValid, bareTankData, measurementOk);

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

          // The authorize callback only records the request; the handoff itself happens here so
          // the ATT response has time to reach the client first. completeDfuReset() does not
          // return when it succeeds -- reaching the line after it means the handoff failed and
          // the normal idle-peer path below should run.
          if (bledfu.dfuResetPending())
          {
            delayFeedingWatchdog(DFU_RESPONSE_SETTLE_MS);
            bledfu.completeDfuReset();
            break;
          }

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

          // Recorded inside the authorize callback, printed here -- Serial work in a BLE event
          // handler delays the ATT response and can change the timing being measured.
          ResetSafeDfuDiag diag;
          if (bledfu.takeDiag(diag))
          {
            Serial.print(F("[DIAG] dfu authorize: checkpoint="));
            Serial.print(diag.checkpoint);
            Serial.print(F(" op="));
            Serial.print(diag.op);
            Serial.print(F(" conn="));
            Serial.print(diag.connHandle);
            Serial.print(F(" gattStatus=0x"));
            Serial.print(diag.gattStatus, HEX);
            Serial.print(F(" replyRc="));
            Serial.println(diag.replyRc);
          }
          Serial.flush();
        }
      }

      // Only advance the cadence/baseline after the SoftDevice actually accepted the advertising
      // start. The old code silently suppressed retries for a full minute after any payload or
      // start failure even though no packet had gone over the air.
      lastAdvertiseMs = millis();
      // Only a real reading may become the swing baseline. Advertising a fault must not silently
      // re-zero the comparison the next big-swing test is made against.
      if (measurementOk) lastAdvertisedRaw = (long)rawData;

      // The probe has completed an advertising cycle, which is the milestone the boot-failure
      // guard treats as evidence of health. Deliberately placed AFTER the OTA window and the
      // connected hold, not at startAdv(), so an image that advertises and then dies later in the
      // same cycle cannot clear the guard.
      bootGuardNoteHealthyAdvert();
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
