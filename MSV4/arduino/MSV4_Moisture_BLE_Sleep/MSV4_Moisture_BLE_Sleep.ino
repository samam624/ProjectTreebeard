// MSV4 probe -- production wake / measure / advertise / sleep firmware.
//
// Two parents, and this file stays in step with both:
// - The hardware layer (pins, SAADC, front end, spectrum sweep and fit, crystal guard) is the code
//   proven on board 1 in arduino/MSV4_Probe_Bringup_BLE. It is copied, not shared, because Arduino
//   compiles only the files inside a sketch folder. Change one, change the other.
// - The duty cycle, advertising window, OTA hold and packet framing follow
//   MSV3/arduino/MSV3_Moisture_BLE_Sleep.
// There is no console. The only GATT services are DIS and the reset-safe buttonless DFU service.
//
// BUILD ONLY WITH
//   MSV4/scripts/arduino_msv4_probe.ps1 -Sketch arduino/MSV4_Moisture_BLE_Sleep -SensorId <n>
// It selects the tracked LFXO variant and the shared RAM-reserving linker script.
//
// Hardware reference: MSV4/hardware/reference/PIN_NET_REFERENCE.md.
// Wire format and what has and has not been measured: MSV4/docs/MSV4_PRODUCTION_FIRMWARE_2026-10-06.md.

#include <Arduino.h>
#include <bluefruit.h>
#include <nrf_nvic.h>
#include <nrf_soc.h>
#include <math.h>

// Canonical reset-safe buttonless DFU service, shared with MSV3 production rather than copied.
// This translation unit compiles from MSV4/.arduino-build/msv4-bringup/sketch/, so the path climbs
// four levels to the repository root. arduino_msv4_probe.ps1 fixes that build directory.
#include "../../../../MSV3/arduino/MSV3_Moisture_BLE_Sleep/ResetSafeBLEDfu.cpp"

#if !defined(MSV4_VARIANT) || !defined(USE_LFXO) || defined(USE_LFRC)
#error "MSV4 requires the tracked msv4_e73 variant (USE_LFXO). Build with MSV4/scripts/arduino_msv4_probe.ps1."
#endif

#ifndef MSV4_APP_VERSION
#error "MSV4_APP_VERSION is supplied by MSV4/scripts/arduino_msv4_probe.ps1."
#endif

// A required build parameter, never a literal edited in place: (commit, sensor id) must reproduce
// a deployed image. Same rule and same reason as MSV3_SENSOR_ID.
#ifndef MSV4_SENSOR_ID
#error "MSV4_SENSOR_ID is required. Build with MSV4/scripts/arduino_msv4_probe.ps1 -SensorId <n>."
#endif
static_assert(MSV4_SENSOR_ID >= 1 && MSV4_SENSOR_ID <= 255,
              "MSV4_SENSOR_ID must fit the single advertisement byte at mfg[6]");

#define MSV4_STRINGIFY_(x) #x
#define MSV4_STRINGIFY(x) MSV4_STRINGIFY_(x)
#ifdef MSV4_BUILD_TAG
#define MSV4_BUILD_ID "MSV4-BUILD:" MSV4_STRINGIFY(MSV4_BUILD_TAG)
#else
#define MSV4_BUILD_ID "MSV4-BUILD:prod"
#endif

// ------------------------------------------------------------------------------------ pins ---
// nRF52840 port 0 pin numbers (== Arduino pin numbers on this identity-mapped variant).
constexpr uint8_t PIN_VBAT_DIV_EN = 6;   // Q1 N-gate, HIGH connects the 100k/100k divider to VBAT
constexpr uint8_t PIN_DRV = 7;           // excitation square wave -> R15 (1k) -> SENSE
constexpr uint8_t PIN_TEMP_READ = 12;    // top of R16 (10k) / NTC divider
constexpr uint8_t PIN_SWAP_CTRL = 13;    // U3 Sel: 0 = A hot, 1 = B hot (100k pulldown)
constexpr uint8_t PIN_SWAP_NOE = 15;     // U3 /OE: HIGH = both electrodes float (100k pullup)
constexpr uint8_t PIN_READ_CNTL = 17;    // U4 Sel, the chopper: 0 = filters on signal, 1 = on GND
constexpr uint8_t PIN_READ_NOE = 20;     // U4 /OE: HIGH = both filters isolated (100k pullup)
constexpr uint8_t PIN_LED_P1 = 10;       // P1.10, active high

// SAADC analog inputs (AINn -> PSELP value n+1), as fabricated (IPC-2581 export 2026-09-18 20:11).
constexpr uint8_t AIN_VBAT = 0;      // P0.02 VBAT_DIV, 50k source: needs TACQ >= 10 us
constexpr uint8_t AIN_TEMP = 1;      // P0.03 TEMP, NTC divider midpoint
constexpr uint8_t AIN_SENSE = 6;     // P0.30 SENSE_OUT (after chopper, R18 100k / C9 1nF)
constexpr uint8_t AIN_DRVMON = 7;    // P0.31 DRV_OUT drift monitor (R19 100k / C16 1nF)
constexpr uint8_t PSEL_VDD = 9;      // SAADC_CH_PSELP_PSELP_VDD

// Declared ahead of every function: the Arduino builder inserts auto-generated prototypes above
// the first function definition, and those prototypes name these types.
enum class SwapState : uint8_t { Open, A, B };
// How much the battery lets a wake do; see the battery saver block above saverWanted().
enum class PowerLevel : uint8_t { Full, Saving };
// Minimal complex type. <complex> cannot be used: Arduino.h defines abs()/round() as macros and
// they break libstdc++'s <complex>.
struct cplx
{
  double re, im;
  cplx(double r = 0, double i = 0) : re(r), im(i) {}
  double real() const { return re; }
  double imag() const { return im; }
  cplx operator+(const cplx& o) const { return cplx(re + o.re, im + o.im); }
  cplx operator-(const cplx& o) const { return cplx(re - o.re, im - o.im); }
  cplx operator*(const cplx& o) const { return cplx(re * o.re - im * o.im, re * o.im + im * o.re); }
  cplx operator/(const cplx& o) const
  {
    const double d = o.re * o.re + o.im * o.im;
    return cplx((re * o.re + im * o.im) / d, (im * o.re - re * o.im) / d);
  }
  cplx& operator+=(const cplx& o) { re += o.re; im += o.im; return *this; }
  cplx& operator/=(double k) { re /= k; im /= k; return *this; }
};

// One frequency of a spectrum sweep: timer period N, phases sampled M, and the measured transfer
// H = X1(SENSE) / X1(DRV_OUT) (bin-1 of the chopper-phase sweep, same switch path on both).
constexpr int SPEC_MAX = 12;
struct SpecSet
{
  int count = 0;
  uint16_t n[SPEC_MAX];
  uint16_t m[SPEC_MAX];
  cplx h[SPEC_MAX];
};

// The last moisture measurement, already in wire units (see the packet layout above startAdv()).
struct MoistureReading
{
  bool valid;
  uint32_t rsoilDeciOhm;
  uint16_t cmDeciPf;
  uint8_t rmsE4;
  uint16_t cdCentiPf;
  bool warm;   // came from an accepted warm-start fit; see runSpectrumFit()
};

struct RetainedRecord
{
  uint32_t encoded;
  uint32_t inverse;
};
static_assert(sizeof(RetainedRecord) == 8, "retained record must fill the reserved 8 bytes");

// The advertised name, built from the sensor ID so the two can never disagree. A constexpr object
// rather than a runtime snprintf() so the literal "MSV4-MOISTURE-NNN" is in the image, where a host
// tool can read it straight out of a signed package.
struct Msv4DeviceName { char text[18]; };

// Free helpers come after every type the auto-generated prototypes mention (see enum note above).
static inline cplx operator*(double k, const cplx& z) { return cplx(k * z.re, k * z.im); }
static inline cplx operator+(double k, const cplx& z) { return cplx(k + z.re, z.im); }
static inline cplx operator/(double k, const cplx& z) { return cplx(k, 0) / z; }
static inline cplx cconj(const cplx& z) { return cplx(z.re, -z.im); }
static inline double cabs2(const cplx& z) { return sqrt(z.re * z.re + z.im * z.im); }
static inline cplx cpolar(double r, double th) { return cplx(r * cos(th), r * sin(th)); }

constexpr Msv4DeviceName makeDeviceName(uint8_t id)
{
  return Msv4DeviceName{{'M', 'S', 'V', '4', '-', 'M', 'O', 'I', 'S', 'T', 'U', 'R', 'E', '-',
                         static_cast<char>('0' + (id / 100) % 10),
                         static_cast<char>('0' + (id / 10) % 10),
                         static_cast<char>('0' + id % 10),
                         '\0'}};
}

// ----------------------------------------------------------------------------- configuration --
const uint8_t SENSOR_ID = MSV4_SENSOR_ID;
constexpr Msv4DeviceName DEVICE_NAME_STORAGE = makeDeviceName(MSV4_SENSOR_ID);
constexpr const char* DEVICE_NAME = DEVICE_NAME_STORAGE.text;
const char FIRMWARE_BUILD_ID[] = MSV4_BUILD_ID;

const uint16_t BLE_COMPANY_ID = 0xFFFF;
// The receiver's production path only looks past this magic, same as every MSV2 and MSV3 probe.
const uint8_t BLE_MAGIC[4] = {'M', 'S', 'V', '2'};

// One advertisement a minute carries battery and tip temperature measured on that wake. The
// spectrum sweep is far more expensive than an FDC read (about 5 s awake with HFXO and the CPU
// running, measured on board 1 through the console on 2026-10-01), so it runs on every
// MOISTURE_EVERY_N_WAKES-th wake and the packets in between repeat its result.
// Ten unless the build says otherwise (-MoistureEveryNWakes on arduino_msv4_probe.ps1). At 1
// every wake measures and every wake opens the connectable window: modelled 2.2-2.4 mAh/day
// against 0.57-0.69 at ten (MSV4/scripts/msv4_power_budget.py), nothing measured.
const uint32_t WAKE_PERIOD_MS = 60000;
#ifdef MSV4_MOISTURE_EVERY_N_WAKES
static_assert(MSV4_MOISTURE_EVERY_N_WAKES >= 1 && MSV4_MOISTURE_EVERY_N_WAKES <= 60,
              "MSV4_MOISTURE_EVERY_N_WAKES must be 1..60");
const uint8_t MOISTURE_EVERY_N_WAKES = MSV4_MOISTURE_EVERY_N_WAKES;
#else
const uint8_t MOISTURE_EVERY_N_WAKES = 10;
#endif

// The 3V3 rail only exists while the BQ25504 asserts VBAT_OK (3.369 V falling), so a running MCU
// already implies some charge. This adds margin for the minutes a BLE DFU spends in the bootloader.
// MSV3's 3.80 V would close the OTA path over most of this board's 3.37-3.89 V operating window,
// and MSV4 has no USB to open it another way.
const uint16_t OTA_MIN_BATTERY_MV = 3500;

// If no advertising cycle has completed for this long, stop feeding the watchdog. A probe that is
// running but silent is unreachable, and this board has no USB; a reset at least hands the
// decision to the boot guard.
// Twenty minutes, twice the spacing of the connectable windows: if the short bursts in between
// ever failed to start, the windows alone must not be able to trip this.
const uint32_t ADVERT_STALL_RESET_MS = 1200000;

// --------------------------------------------------------------------------- front end -------
// One TIMER at 16 MHz, period N ticks (CC5 + CLEAR short). Four compares toggle two GPIOTE task
// channels through four PPI channels:
//   DRV     : toggle at N/2 (CC0) and N (CC5, the wrap)            -> high for ticks [0, N/2)
//   chopper : toggle at a (CC1) and b (CC2), a = k, b = k + N/2    -> high for ticks [k, k+N/2)
// Any edge that lands on tick 0 is scheduled at N instead (the wrap), so no compare is ever 0.
// TIMER0 belongs to the SoftDevice; TIMER3 is unused by this core and has six CC registers.
// PPI channels 0-16 are the application's with S140 enabled; GPIOTE 6/7 are the top two, clear of
// attachInterrupt()'s lowest-free-first allocation (and nothing here calls attachInterrupt).
NRF_TIMER_Type* const FE_TIMER = NRF_TIMER3;
constexpr uint8_t GPIOTE_CH_DRV = 6;
constexpr uint8_t GPIOTE_CH_CHOP = 7;
constexpr uint8_t PPI_CH_BASE = 8;   // uses 8, 9, 10, 11
constexpr uint32_t PPI_MASK = 0xFUL << PPI_CH_BASE;
constexpr uint32_t FE_CLOCK_HZ = 16000000UL;
constexpr uint16_t FE_N_MIN = 4;
constexpr uint16_t FE_N_MAX = 16000;

struct FrontEndState
{
  bool running = false;
  bool hfxoRequested = false;
};
static FrontEndState g_fe;
// Set once in setup(): the SoftDevice accepting sd_ppi_channel_assign() only proves a channel is
// application-accessible, not that nothing else in the image already uses it.
static bool g_feResourcesFree = false;
// After a phase change the 100 us filters need about 1 ms (10 tau leaves 45 ppm of the step).
// The wait is delay(), which is tick-based at 1024 Hz and ends on a tick BOUNDARY: delay(1) can be
// anything from 0 to 0.98 ms, so 2 is the shortest value that guarantees the millisecond
// (0.98-1.95 ms). It was 5 until application version 262154.
constexpr uint32_t FE_SETTLE_MS = 2;
constexpr uint8_t FE_OVERSAMPLE_LOG2 = 6;   // 64x hardware oversample for front-end reads

// --------------------------------------------------------------------- boot/crystal guard ----
// A hand-soldered crystal can fail to start. With USE_LFXO, a dead crystal would stall FreeRTOS's
// RTC1 tick and SoftDevice enable, and with no USB and hand-held SWD that is a brick. The reasoning
// and the source checks behind it are in the bring-up sketch; the sequence here is the same:
// 1. Busy-wait (cycle counter, not delay()) up to 2 s for LFCLK running AND sourced from XTAL.
//    Failure jumps straight to the bootloader's BLE DFU (which runs on LFRC). The watchdog is not
//    started until this passes.
// 2. After SoftDevice enable, LFCLK must still be on XTAL, or the board escapes to DFU the same way.
// 3. A boot counter in the reserved RAM record. On the entry that finds count >= 3 -- the 4th
//    consecutive boot after three that never proved healthy -- the board escapes to DFU.
//    (">= 3 means the 4th entry" is deliberate; do not "fix" the off-by-one.)
// Two production differences from the bring-up counter, both taken from MSV3's guard:
// - Only watchdog, software and lockup resets carry the count. A pin reset or a power-on starts at
//   zero, so pressing RST a few times cannot send a healthy probe into DFU.
// - The count clears after BOOT_GUARD_PROBATION_ADVERTS completed advertising cycles, not when
//   advertising first starts, so an image that advertises once and then dies is still caught.
// The GPREGRET OTA entry has no timeout in this bootloader: an escaped board sits in DFU until an
// image arrives or its supply drops out.
constexpr uint32_t WDT_TIMEOUT_MS = 8000;
constexpr uint32_t LFCLK_START_TIMEOUT_MS = 2000;
constexpr uint8_t BOOT_ESCAPE_LIMIT = 3;
constexpr uint8_t BOOT_GUARD_PROBATION_ADVERTS = 2;
constexpr uint32_t RETAINED_MAGIC = 0x4D344200UL;       // "M4B" + count in the low byte
constexpr uint32_t RETAINED_MAGIC_MASK = 0xFFFFFF00UL;
constexpr uint8_t DFU_MAGIC_OTA_RESET = 0xA8;

static volatile RetainedRecord g_retained
    __attribute__((section(".msv3_boot_guard_retained"), used, aligned(4)));

static uint8_t g_probationAdverts = 0;
static bool g_bootGuardCleared = false;

// ------------------------------------------------------------------------------------ BLE ----
BLEDis bledis;
// Hardware Revision String, in the DIS service; see publishCp().
static BLECharacteristic g_hardwareRevChar(UUID16_CHR_HARDWARE_REVISION_STRING);
static bool g_hardwareRevReady = false;
// The GATT "String" characteristic, also in the DIS service: the mask check's result. BLEDis
// already publishes every DIS string it has a slot for (it fills the serial number itself), so
// this cannot be one of those. See checkMask().
constexpr uint16_t UUID16_CHR_STRING = 0x2A3D;
static BLECharacteristic g_maskChar(UUID16_CHR_STRING);
static bool g_maskCharReady = false;
static char g_maskText[23] = "not checked";
// The checked ADC calibration's outcome, also in the DIS service; see saadcCalibrateChecked().
// A UUID of this sketch's own (4d535634-adc0-4e52-a001-000000000001, "MSV4" then "adc0"),
// least significant byte first as the SoftDevice wants it.
static const uint8_t UUID128_CHR_ADC_CAL[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xa0,
                                                0x52, 0x4e, 0xc0, 0xad, 0x34, 0x56, 0x53, 0x4d};
static BLECharacteristic g_adcCalChar(UUID128_CHR_ADC_CAL);
static bool g_adcCalCharReady = false;
static char g_adcCalText[23] = "not checked";
// The calibration experiment's counts, also in DIS (...0002); see saadcCalibrateArm().
static const uint8_t UUID128_CHR_CAL_EXPERIMENT[16] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xa0,
                                                       0x52, 0x4e, 0xc0, 0xad, 0x34, 0x56, 0x53, 0x4d};
static BLECharacteristic g_calExpChar(UUID128_CHR_CAL_EXPERIMENT);
static bool g_calExpCharReady = false;
static char g_calExpText[23] = "not run";
ResetSafeBLEDfu bledfu;
static bool g_otaServiceReady = false;
static volatile uint16_t g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;

// Counts ADVERTISEMENTS. The receiver calls this field "wake" and uses it to drop repeated frames
// of one advertising window, so it must advance once per window and never otherwise.
static uint32_t g_advertCount = 0;
static uint32_t g_lastAdvertOkMs = 0;

static MoistureReading g_reading = {false, 0, 0, 0, 0, false};
static char g_hardwareRev[24];
static uint8_t g_wakesUntilMoisture = 0;   // 0 = measure on this wake
static uint8_t g_wakesSinceBoot = 0;       // saturates; only compared with small counts

// --------------------------------------------------------------------- DC/DC regulator -------
// The nRF52840's main-stage DC/DC converter (REG1), requested since application version 262155.
// It needs an inductor between DCC and DEC4, which on this board can only be inside the E73
// module, and the product specification says enabling the converter without one "will inhibit
// device operation". So the first wake of every boot runs on the linear regulator and the
// converter is requested only once that wake's connectable window has closed: a board whose
// module has no inductor still offers an updater one full window per reset instead of going
// silent.
// The request is never taken on trust. Every packet carries DCDCEN as read back when the packet
// was built (flags2 bit3): the core clears the register on its way into setup() (TinyUSB's
// nrfx_power_init() with a zeroed configuration), and a later clear would otherwise look exactly
// like a converter that works.
// REG0 (DCDCEN0) stays off: VDD and VDDH share the 3V3 rail, so that stage is bypassed, and the
// module has no DCCH inductor. Evidence for the inductor and the result on board 1:
// MSV4/docs/MSV4_PRODUCTION_FIRMWARE_2026-10-06.md.
static bool g_dcdcRequested = false;

// ------------------------------------------------------------------------- low-level utils ---
static void cycleCounterEnable()
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

// The busy-waits below need the cycle counter only while a wake is doing register work. Trace is
// switched off across the sleep so the debug block cannot hold anything awake.
static void cycleCounterDisable()
{
  DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
}

// Busy-wait in microseconds on the 64 MHz cycle counter: independent of LFCLK/RTC1/FreeRTOS.
static void busyWaitUs(uint32_t us)
{
  const uint32_t start = DWT->CYCCNT;
  const uint32_t cycles = us * (SystemCoreClock / 1000000UL);
  while ((DWT->CYCCNT - start) < cycles) {}
}

static void feedWatchdog() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

static void startWatchdog()
{
  if (NRF_WDT->RUNSTATUS) return;  // survives soft resets only if something else started it
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Run << WDT_CONFIG_HALT_Pos) |
                    (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = (WDT_TIMEOUT_MS * 32768UL) / 1000UL;
  NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

static void delayFeeding(uint32_t ms)
{
  while (ms > 0)
  {
    const uint32_t chunk = ms > 200 ? 200 : ms;
    delay(chunk);
    ms -= chunk;
    feedWatchdog();
  }
}

// The sleep between wakes: one-second chunks, well under WDT_TIMEOUT_MS.
static void sleepFeeding(uint32_t ms)
{
  while (ms > 0)
  {
    feedWatchdog();
    const uint32_t chunk = ms > 1000 ? 1000 : ms;
    delay(chunk);
    ms -= chunk;
  }
  feedWatchdog();
}

static void ledWrite(bool on)
{
  if (on) NRF_P1->OUTSET = (1UL << PIN_LED_P1);
  else NRF_P1->OUTCLR = (1UL << PIN_LED_P1);
}

// ------------------------------------------------------------------ discharge burn (test) ----
// TEST BUILDS ONLY: arduino_msv4_probe.ps1 -BurnFloorMv <mV>. Without that parameter none of this
// is compiled and the functions below are empty. It draws the cell down to a chosen level so a
// day of charging can be watched from a known low start. Between wakes the CPU spins on the
// linear regulator with the LED lit instead of sleeping; the wakes themselves, the packets and
// the OTA window are the production image's.
//
// It ends itself, once per boot, and must never be the reason a board goes dark:
// - a battery reading at or below the floor pauses the burn for that minute, and a second one in
//   a row ends it. Two, because single readings stand 12-25 mV off about once in two hours, and
//   the second is then taken after a minute's rest;
// - BURN_MAX_MS after boot ends it wherever the cell is;
// - a battery reading the wake cannot use pauses it, and three in a row end it;
// - it does not start at all after a watchdog or lockup reset.
// The floor may not be set below 3600 mV. The BQ25504 switches the 3V3 rail off at 3.37 V nominal
// (3.42 V at the top of its tolerance), this ADC's gain is good to 3 %, and once the rail is off
// nothing can reach the board until the panel has brought the cell back above 3.73 V -- where
// this image would boot and burn again.
// NOT retained: "finished" lives in RAM. After a soft reset the burn would run again if the cell
// has charged above the floor since, so put a production image back before trusting a day's
// charge record to this one.
#ifdef MSV4_BURN_FLOOR_MV
static_assert(MSV4_BURN_FLOOR_MV >= 3600 && MSV4_BURN_FLOOR_MV <= 4100,
              "MSV4_BURN_FLOOR_MV must be 3600..4100: see the comment above");
constexpr float BURN_FLOOR_MV = MSV4_BURN_FLOOR_MV;
constexpr uint32_t BURN_MAX_MS = 10UL * 3600UL * 1000UL;
// The cell rests this long before each wake's battery reading.
constexpr uint32_t BURN_REST_MS = 5000;
static bool g_burnDone = false;
static bool g_burnThisCycle = false;
static uint8_t g_burnBelow = 0;
static uint8_t g_burnUnreadable = 0;

static void burnBegin()
{
  g_burnDone = (readResetReason() & (POWER_RESETREAS_DOG_Msk | POWER_RESETREAS_LOCKUP_Msk)) != 0;
}

// Called once per wake with that wake's battery reading; decides the minute that follows.
static void burnJudge(bool vbatOk, float vbatMv)
{
  g_burnThisCycle = false;
  if (g_burnDone) return;
  if (millis() >= BURN_MAX_MS)
  {
    g_burnDone = true;
    return;
  }
  if (!vbatOk)
  {
    if (++g_burnUnreadable >= 3) g_burnDone = true;
    return;
  }
  g_burnUnreadable = 0;
  if (vbatMv <= BURN_FLOOR_MV)
  {
    if (++g_burnBelow >= 2) g_burnDone = true;
    return;
  }
  g_burnBelow = 0;
  g_burnThisCycle = true;
}

static bool burnActive() { return g_burnThisCycle; }
// The converter stays off until the burn is over: the linear regulator is the bigger load.
static bool burnHoldsLinearRegulator() { return !g_burnDone; }

// Spins from now until BURN_REST_MS before the next wake is due. Nothing is advertising by then,
// so the only other work is the SoftDevice's, which preempts this task as it always does.
static void burnUntilRest(uint32_t cycleStartMs)
{
  if (!g_burnThisCycle) return;
  ledWrite(true);
  while (millis() - cycleStartMs < WAKE_PERIOD_MS - BURN_REST_MS) feedWatchdog();
  ledWrite(false);
}
#else
static void burnBegin() {}
static void burnJudge(bool, float) {}
static bool burnActive() { return false; }
static bool burnHoldsLinearRegulator() { return false; }
static void burnUntilRest(uint32_t) {}
#endif

static bool readRetained(uint8_t& count)
{
  const uint32_t e = g_retained.encoded;
  const uint32_t inv = g_retained.inverse;
  if (inv != ~e || (e & RETAINED_MAGIC_MASK) != RETAINED_MAGIC) return false;
  count = static_cast<uint8_t>(e & 0xFF);
  return true;
}

static void writeRetained(uint8_t count)
{
  const uint32_t e = RETAINED_MAGIC | count;
  g_retained.inverse = 0;
  g_retained.encoded = e;
  g_retained.inverse = ~e;
}

// RESETREAS has no bit for power-on or brownout, so zero means a clean slate. SREQ stays in the
// carry set because a crash routed through the SoftDevice/app_error handler resets that way.
static bool resetCarriesBootCount(uint32_t reason)
{
  return (reason & (POWER_RESETREAS_DOG_Msk | POWER_RESETREAS_SREQ_Msk |
                    POWER_RESETREAS_LOCKUP_Msk)) != 0;
}

// Only valid BEFORE the SoftDevice is enabled (direct POWER register access).
[[noreturn]] static void escapeToDfuPreSoftDevice(uint8_t blinks)
{
  writeRetained(0);
  for (uint8_t i = 0; i < blinks; i++)
  {
    ledWrite(true);
    busyWaitUs(80000);
    ledWrite(false);
    busyWaitUs(120000);
  }
  NRF_POWER->GPREGRET = DFU_MAGIC_OTA_RESET;
  NVIC_SystemReset();
  while (true) {}
}

static bool lfclkOnXtal()
{
  const uint32_t stat = NRF_CLOCK->LFCLKSTAT;
  return (stat & CLOCK_LFCLKSTAT_STATE_Msk) &&
         ((stat & CLOCK_LFCLKSTAT_SRC_Msk) >> CLOCK_LFCLKSTAT_SRC_Pos) == CLOCK_LFCLKSTAT_SRC_Xtal;
}

// With the SoftDevice enabled, POWER registers go through SVCs. Returns only on failure.
static void armDfuAndReset()
{
  if (sd_power_gpregret_clr(0, 0xFF) != NRF_SUCCESS ||
      sd_power_gpregret_set(0, DFU_MAGIC_OTA_RESET) != NRF_SUCCESS)
  {
    sd_power_gpregret_clr(0, 0xFF);
    return;
  }
  uint32_t gpregret = 0;
  if (sd_power_gpregret_get(0, &gpregret) != NRF_SUCCESS || (gpregret & 0xFF) != DFU_MAGIC_OTA_RESET)
  {
    sd_power_gpregret_clr(0, 0xFF);
    return;
  }
  // Success does not return. If it does, clear the request so an unrelated later reset cannot
  // drop into DFU with no client.
  if (sd_nvic_SystemReset() != NRF_SUCCESS) sd_power_gpregret_clr(0, 0xFF);
}

// Health means completed advertising cycles: the radio window opened, any OTA hold ran, and the
// loop came back. Called at the end of each one.
static void bootGuardNoteHealthyAdvert()
{
  if (g_bootGuardCleared) return;
  if (++g_probationAdverts < BOOT_GUARD_PROBATION_ADVERTS) return;
  writeRetained(0);
  g_bootGuardCleared = true;
}

// POWER shares its register block with CLOCK and belongs to the SoftDevice while that is enabled:
// writes go through the SVC, reads are direct, as lfclkOnXtal() already does.
static bool dcdcEnabled()
{
  return (NRF_POWER->DCDCEN & POWER_DCDCEN_DCDCEN_Msk) != 0;
}

// ---------------------------------------------------------------------------------- SAADC ----
static bool waitEvent(volatile uint32_t& ev, uint32_t timeoutUs)
{
  const uint32_t start = DWT->CYCCNT;
  const uint32_t cycles = timeoutUs * (SystemCoreClock / 1000000UL);
  while (!ev)
  {
    if ((DWT->CYCCNT - start) > cycles) return false;
  }
  return true;
}

static uint32_t tacqCode(uint32_t us)
{
  if (us <= 3) return SAADC_CH_CONFIG_TACQ_3us;
  if (us <= 5) return SAADC_CH_CONFIG_TACQ_5us;
  if (us <= 10) return SAADC_CH_CONFIG_TACQ_10us;
  if (us <= 15) return SAADC_CH_CONFIG_TACQ_15us;
  if (us <= 20) return SAADC_CH_CONFIG_TACQ_20us;
  return SAADC_CH_CONFIG_TACQ_40us;
}

static volatile int16_t g_saadcResult;

static void saadcStop()
{
  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->TASKS_STOP = 1;
  waitEvent(NRF_SAADC->EVENTS_STOPPED, 2000);
  NRF_SAADC->EVENTS_STOPPED = 0;
}

// Single-ended, gain 1/6, internal 0.6 V reference -> 0..3.6 V full scale at 12 bits. With
// oversampling, BURST takes all 2^osLog2 samples on one SAMPLE task and returns their average.
// The code as the converter gives it, sign and all: a zero input reads a few counts either side
// of nothing, and saadcCalibrateChecked() needs to see which side.
static bool saadcReadCode(uint8_t pselp, uint8_t osLog2, uint32_t tacqUs, int32_t& code)
{
  if (osLog2 > 8) osLog2 = 8;
  NRF_SAADC->ENABLE = 0;
  for (int i = 0; i < 8; i++) NRF_SAADC->CH[i].PSELP = SAADC_CH_PSELP_PSELP_NC;
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
  NRF_SAADC->OVERSAMPLE = osLog2;
  NRF_SAADC->CH[0].CONFIG =
      (SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) |
      (SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos) |
      (SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) |
      (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
      (tacqCode(tacqUs) << SAADC_CH_CONFIG_TACQ_Pos) |
      (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
      ((osLog2 ? SAADC_CH_CONFIG_BURST_Enabled : SAADC_CH_CONFIG_BURST_Disabled)
       << SAADC_CH_CONFIG_BURST_Pos);
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
  NRF_SAADC->CH[0].PSELP = pselp;
  NRF_SAADC->RESULT.PTR = reinterpret_cast<uint32_t>(&g_saadcResult);
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->ENABLE = 1;

  bool ok = true;
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_START = 1;
  ok = ok && waitEvent(NRF_SAADC->EVENTS_STARTED, 2000);
  NRF_SAADC->EVENTS_END = 0;
  if (ok)
  {
    NRF_SAADC->TASKS_SAMPLE = 1;
    // Worst case 256 x (40 us + ~2 us conversion) ~ 11 ms.
    ok = waitEvent(NRF_SAADC->EVENTS_END, 50000);
  }
  saadcStop();
  NRF_SAADC->ENABLE = 0;
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_NC;
  if (!ok) return false;

  code = g_saadcResult;
  return true;
}

static bool saadcReadMv(uint8_t pselp, uint8_t osLog2, uint32_t tacqUs, float& mv)
{
  int32_t code = 0;
  if (!saadcReadCode(pselp, osLog2, tacqUs, code)) return false;
  if (code < 0) code = 0;  // single-ended can report small negative codes near 0 V
  mv = code * 3600.0f / 4096.0f;
  return true;
}

static inline bool readAinMv(uint8_t ain, uint8_t osLog2, uint32_t tacqUs, float& mv)
{
  return saadcReadMv(static_cast<uint8_t>(ain + 1), osLog2, tacqUs, mv);
}

// Offset calibration. Runs at the start of EVERY wake since application version 262153, not once
// at boot. Measured on board 1, 2026-10-06: a boot-time calibration is whatever the supply and
// the SoftDevice happened to be doing in that instant, and it then biases every reading until the
// next reset. Two production boots agreed at 3826 and 3823 mV; the third (262152, which ran this
// earlier in setup()) read the same resting cell 13 mV higher, in the dark. A wake is a quiet,
// repeatable moment, and recalibrating there also follows the offset's drift with temperature,
// which a probe in a window sees every day.
// OVERSAMPLE is forced to bypass first so every calibration is taken in the same configuration,
// whatever the previous read left behind.
// The oversample argument exists only for the calibration experiment (saadcCalibrateArm()).
static bool saadcCalibrate(uint32_t oversample = SAADC_OVERSAMPLE_OVERSAMPLE_Bypass)
{
  NRF_SAADC->ENABLE = 0;
  NRF_SAADC->OVERSAMPLE = oversample;
  NRF_SAADC->ENABLE = 1;
  NRF_SAADC->EVENTS_CALIBRATEDONE = 0;
  NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;
  const bool ok = waitEvent(NRF_SAADC->EVENTS_CALIBRATEDONE, 200000);
  NRF_SAADC->EVENTS_CALIBRATEDONE = 0;
  // nRF52840 erratum: stop after calibration so the next START/END sequence is clean.
  saadcStop();
  NRF_SAADC->ENABLE = 0;
  return ok;
}

// --------------------------------------------------------------------------------- GPIO -------
static void pinDriveHigh(uint8_t pin, bool highDrive)
{
  NRF_P0->PIN_CNF[pin] =
      (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
      (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
      (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
      ((highDrive ? GPIO_PIN_CNF_DRIVE_H0H1 : GPIO_PIN_CNF_DRIVE_S0S1) << GPIO_PIN_CNF_DRIVE_Pos);
}

// Preload OUT before switching to output, so the pin never glitches the other way.
static void pinOutput(uint8_t pin, bool level, bool highDrive = false)
{
  if (level) NRF_P0->OUTSET = (1UL << pin);
  else NRF_P0->OUTCLR = (1UL << pin);
  pinDriveHigh(pin, highDrive);
}

// Input, disconnected buffer, no pull: the board resistor decides the level.
static void pinRelease(uint8_t pin)
{
  NRF_P0->PIN_CNF[pin] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                         (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                         (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos);
}

static void setSwap(SwapState s)
{
  if (s == SwapState::Open)
  {
    pinRelease(PIN_SWAP_NOE);   // pullup -> /OE high -> both electrodes float
    pinRelease(PIN_SWAP_CTRL);  // pulldown
  }
  else
  {
    pinOutput(PIN_SWAP_CTRL, s == SwapState::B);
    pinOutput(PIN_SWAP_NOE, false);
  }
}

static void setChopEnabled(bool on)
{
  if (on) pinOutput(PIN_READ_NOE, false);
  else pinRelease(PIN_READ_NOE);
}

// ---------------------------------------------------------------------------- front end -------
static bool requestHfxo()
{
  if (g_fe.hfxoRequested) return true;
  if (sd_clock_hfclk_request() != NRF_SUCCESS) return false;
  g_fe.hfxoRequested = true;
  const uint32_t start = millis();
  uint32_t running = 0;
  while (millis() - start < 20)
  {
    if (sd_clock_hfclk_is_running(&running) == NRF_SUCCESS && running) return true;
  }
  return false;
}

static void releaseHfxo()
{
  if (!g_fe.hfxoRequested) return;
  sd_clock_hfclk_release();
  g_fe.hfxoRequested = false;
}

// Stop order: PPI off, timer stopped, GPIOTE released, then pins forced to their safe level.
static void frontEndStop()
{
  sd_ppi_channel_enable_clr(PPI_MASK);
  FE_TIMER->TASKS_STOP = 1;
  FE_TIMER->TASKS_CLEAR = 1;
  FE_TIMER->SHORTS = 0;
  NRF_GPIOTE->CONFIG[GPIOTE_CH_DRV] = 0;
  NRF_GPIOTE->CONFIG[GPIOTE_CH_CHOP] = 0;
  pinOutput(PIN_DRV, false);
  pinRelease(PIN_READ_CNTL);  // 100k pulldown holds the chopper at "signal"
  releaseHfxo();
  g_fe.running = false;
}

static uint32_t gpioteTaskConfig(uint8_t pin, bool initHigh)
{
  return (GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos) |
         (static_cast<uint32_t>(pin) << GPIOTE_CONFIG_PSEL_Pos) |
         (0UL << GPIOTE_CONFIG_PORT_Pos) |
         (GPIOTE_CONFIG_POLARITY_Toggle << GPIOTE_CONFIG_POLARITY_Pos) |
         ((initHigh ? GPIOTE_CONFIG_OUTINIT_High : GPIOTE_CONFIG_OUTINIT_Low)
          << GPIOTE_CONFIG_OUTINIT_Pos);
}

static bool frontEndStart(uint16_t n, uint16_t k)
{
  if (n < FE_N_MIN || n > FE_N_MAX || (n & 1)) return false;
  if (k >= n) return false;
  if (!g_feResourcesFree) return false;

  frontEndStop();
  if (!requestHfxo())
  {
    releaseHfxo();
    return false;  // refuse to run the excitation on HFINT
  }

  const uint16_t half = n / 2;
  const uint16_t a = (k == 0) ? n : k;
  const uint16_t bRaw = static_cast<uint16_t>((k + half) % n);
  const uint16_t b = (bRaw == 0) ? n : bRaw;
  // Chopper is high on ticks [k, k+N/2) mod N; at tick 0 that window contains 0 when k == 0 or
  // k > N/2. (k == N/2 starts low and rises at N/2.)
  const bool chopInitHigh = (k == 0) || (k > half);

  FE_TIMER->MODE = TIMER_MODE_MODE_Timer;
  FE_TIMER->BITMODE = TIMER_BITMODE_BITMODE_16Bit;
  FE_TIMER->PRESCALER = 0;  // 16 MHz
  FE_TIMER->CC[5] = n;
  FE_TIMER->CC[0] = half;
  FE_TIMER->CC[1] = a;
  FE_TIMER->CC[2] = b;
  FE_TIMER->SHORTS = TIMER_SHORTS_COMPARE5_CLEAR_Msk;
  for (int i = 0; i < 6; i++) FE_TIMER->EVENTS_COMPARE[i] = 0;

  // High drive on DRV before GPIOTE owns it: Rs is 1k and the electrode can be < 1k when wet, so
  // standard drive (1 mA guaranteed) would put the pin's own impedance into the divider.
  pinOutput(PIN_DRV, true, true);
  pinOutput(PIN_READ_CNTL, chopInitHigh);
  NRF_GPIOTE->CONFIG[GPIOTE_CH_DRV] = gpioteTaskConfig(PIN_DRV, true);
  NRF_GPIOTE->CONFIG[GPIOTE_CH_CHOP] = gpioteTaskConfig(PIN_READ_CNTL, chopInitHigh);

  uint32_t rc = 0;
  rc |= sd_ppi_channel_assign(PPI_CH_BASE + 0, &FE_TIMER->EVENTS_COMPARE[0],
                              &NRF_GPIOTE->TASKS_OUT[GPIOTE_CH_DRV]);
  rc |= sd_ppi_channel_assign(PPI_CH_BASE + 1, &FE_TIMER->EVENTS_COMPARE[5],
                              &NRF_GPIOTE->TASKS_OUT[GPIOTE_CH_DRV]);
  rc |= sd_ppi_channel_assign(PPI_CH_BASE + 2, &FE_TIMER->EVENTS_COMPARE[1],
                              &NRF_GPIOTE->TASKS_OUT[GPIOTE_CH_CHOP]);
  rc |= sd_ppi_channel_assign(PPI_CH_BASE + 3, &FE_TIMER->EVENTS_COMPARE[2],
                              &NRF_GPIOTE->TASKS_OUT[GPIOTE_CH_CHOP]);
  if (rc != NRF_SUCCESS || sd_ppi_channel_enable_set(PPI_MASK) != NRF_SUCCESS)
  {
    frontEndStop();
    return false;
  }

  FE_TIMER->TASKS_CLEAR = 1;
  FE_TIMER->TASKS_START = 1;
  g_fe.running = true;
  return true;
}

// Isolate the analog path first (both /OE lines back to their pullups), then stop the clocks.
// TASKS_SHUTDOWN is the production addition: a TIMER that has run and is only STOPped keeps
// drawing current (nRF52 anomaly 78, the reason nrfx_timer_disable() uses SHUTDOWN). It is issued
// here, once per wake, so the per-phase start/stop path stays exactly what bring-up validated.
static void allSafe()
{
  setChopEnabled(false);
  setSwap(SwapState::Open);
  frontEndStop();
  FE_TIMER->TASKS_SHUTDOWN = 1;
  pinOutput(PIN_VBAT_DIV_EN, false);
  pinOutput(PIN_TEMP_READ, false);
}

// ---------------------------------------------------------------- checked ADC calibration ----
// The offset calibration is checked against a true zero since application version 262163.
// Measured on board 1, 2585 one-minute wakes of 262161 (2026-10-08 to 2026-10-10): one
// calibration in fifty lands about 9 LSB high, and every reading of that wake carries it. The
// battery read 14-22 mV high and the tip thermistor 0.12 C low on those wakes and on no others;
// the sweep is immune, since a constant drops out of its sum over phases. It is the 13 mV boot
// of 2026-10-06 again, one wake at a time. Why a calibration lands there is not established.
// The zero is the battery divider's own midpoint: with VBAT_DIV_EN low Q1 disconnects VBAT and
// R9 holds AIN0 at ground. After each calibration that pin is read as a signed code, and a zero
// SAADC_ZERO_MAX_LSB or more from where a good one sits means the calibration is taken again, up
// to SAADC_CAL_TRIES in all. THE LAST ONE STANDS WHATEVER IT READS: if the zero is not where this
// expects (Q1's leakage across R9, say), a wake costs two more calibrations and is otherwise
// what it was before.
// The outcome is the text of g_adcCalChar, "z+1 n1 r27 b0": this wake's zero in LSB and how many
// calibrations it took, then the wakes of this boot that needed more than one, and those whose
// last zero was still out. That text is an ABI with MSV4/scripts/msv4_adv_scan.py --read-build.
// MEASURED on board 1 with 262163 (2026-10-11, wakes 6 to 29 of that boot): after a good
// calibration the zero reads -3 to -7 LSB, not nothing (-5 in the middle on the wakes where the
// check did not pick the calibration), and the two wakes that ended at +4 and +6 were the high
// kind (the +4 wake's battery read 21 mV above its neighbours). Board 2 then established that
// this offset is a BOARD property: its good zero is around -11 LSB and its high kind is at or
// above zero. A window centred only on board 1 made 694 of board 2's first 697 wakes retry and
// 689 end outside the window, producing +17 to +27 mV battery spikes on 8.8% of stored wakes.
// From 262169 the common window accepts -15..-1 LSB: both measured good populations are inside
// and both measured high populations are outside. Why grounded inputs differ by board is not
// established; the text below keeps reporting the actual zero.
// The zero itself scatters about 2 LSB either way at 16 samples, and the battery read on the same
// pin does not follow it (262163, wakes 6 to 27), so it is this reading's own noise. Since 262165
// it is taken over 64 samples, which is why it is only ever used to tell the two kinds apart and
// never subtracted from a reading.
constexpr int32_t SAADC_ZERO_CENTRE_LSB = -8;
constexpr int32_t SAADC_ZERO_MAX_LSB = 8;   // strict bounds: accept -15..-1, reject zero and above
constexpr uint8_t SAADC_ZERO_OVERSAMPLE_LOG2 = 6;
constexpr uint8_t SAADC_CAL_TRIES = 3;
constexpr uint16_t SAADC_CAL_COUNT_MAX = 9999;   // what the text has room for
static uint16_t g_calRepeatedWakes = 0;
static uint16_t g_calBadWakes = 0;

static bool saadcZeroOut(int32_t zero)
{
  const int32_t off = zero - SAADC_ZERO_CENTRE_LSB;
  return off <= -SAADC_ZERO_MAX_LSB || off >= SAADC_ZERO_MAX_LSB;
}

// ------------------------------------------------------ calibration experiment (TEST, 262165) --
// What makes one calibration in fifty land high is not known, so the board is asked. What the
// record says so far (board 1, 2026-10-06 to 10-11): the high ones are a second, separate outcome
// 9 to 12 LSB above the usual one with nothing in between; they come on 2.0 % of wakes with the
// DC/DC converter on and 0.7 % on the linear regulator; and they keep no rhythm, hour of day or
// memory of the wake before. Nordic's own account of the calibration is a coarse correction
// ("around +-8 LSB" at 12 bits) set from a reference that noise moves.
// So each wake's FIRST calibration is taken one of five ways, in rotation, and the wakes on
// which it landed high are counted per way:
//   0  as built: at once, converter on, RC clock, no oversampling
//   1  with OVERSAMPLE at 32x while it calibrates
//   2  CAL_LATER_US later, the CPU kept awake until then
//   3  with the DC/DC converter switched off for the calibration
//   4  with the 32 MHz crystal running
// A calibration that has to be taken again is always oversampled, the only arm with zero high
// outcomes on both boards (0/896 board 1 and 0/140 board 2). The counts are the text of
// g_calExpChar, "e5 0 6 2 4 w1440": high
// first calibrations for ways 0 to 4, then the wakes so far (a fifth of them each). A way whose
// calibration once failed to finish shows T instead of a count and is not tried again: nothing
// says a calibration completes with oversampling set, and each failure costs 200 ms awake.
// That text is an ABI with MSV4/scripts/msv4_adv_scan.py --read-build. A way that reaches a day
// with none against the others' five or six is the answer; take this block out again once it
// has given one.
constexpr uint8_t CAL_ARM_AS_BUILT = 0;
constexpr uint8_t CAL_ARM_OVERSAMPLED = 1;
constexpr uint8_t CAL_ARM_LATER = 2;
constexpr uint8_t CAL_ARM_NO_DCDC = 3;
constexpr uint8_t CAL_ARM_CRYSTAL = 4;
constexpr uint8_t CAL_ARMS = 5;
constexpr uint32_t CAL_LATER_US = 5000;
constexpr uint32_t CAL_REGULATOR_SETTLE_US = 200;
constexpr uint8_t CAL_ARM_COUNT_MAX = 99;        // what the text has room for
constexpr uint32_t CAL_WAKES_SHOWN_MAX = 99999;
static uint8_t g_calArmHigh[CAL_ARMS] = {};
static bool g_calArmTimedOut[CAL_ARMS] = {};
static uint32_t g_calWakes = 0;

static bool saadcCalibrateArm(uint8_t arm)
{
  if (g_calArmTimedOut[arm]) return saadcCalibrate();
  bool ok = false;
  if (arm == CAL_ARM_OVERSAMPLED)
  {
    ok = saadcCalibrate(SAADC_OVERSAMPLE_OVERSAMPLE_Over32x);
  }
  else if (arm == CAL_ARM_LATER)
  {
    busyWaitUs(CAL_LATER_US);
    ok = saadcCalibrate();
  }
  else if (arm == CAL_ARM_NO_DCDC && dcdcEnabled())
  {
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_DISABLE);
    busyWaitUs(CAL_REGULATOR_SETTLE_US);
    ok = saadcCalibrate();
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
  }
  else if (arm == CAL_ARM_CRYSTAL)
  {
    requestHfxo();
    ok = saadcCalibrate();
    releaseHfxo();
  }
  else
  {
    ok = saadcCalibrate();
  }
  if (!ok) g_calArmTimedOut[arm] = true;
  return ok;
}

static void saadcCalibrateChecked()
{
  pinOutput(PIN_VBAT_DIV_EN, false);   // the zero: divider off, R9 to ground
  const uint8_t arm = static_cast<uint8_t>(g_calWakes % CAL_ARMS);
  g_calWakes++;
  int32_t zero = 0;
  uint8_t tries = 0;
  bool zeroRead = false;
  while (tries < SAADC_CAL_TRIES)
  {
    if (tries == 0) saadcCalibrateArm(arm);
    else saadcCalibrate(SAADC_OVERSAMPLE_OVERSAMPLE_Over32x);
    tries++;
    zeroRead = saadcReadCode(static_cast<uint8_t>(AIN_VBAT + 1), SAADC_ZERO_OVERSAMPLE_LOG2, 40, zero);
    if (tries == 1 && zeroRead && zero - SAADC_ZERO_CENTRE_LSB >= SAADC_ZERO_MAX_LSB &&
        g_calArmHigh[arm] < CAL_ARM_COUNT_MAX)
    {
      g_calArmHigh[arm]++;
    }
    // A zero that cannot be read checks nothing: keep this calibration, as before 262163.
    if (!zeroRead || !saadcZeroOut(zero)) break;
  }
  char cell[CAL_ARMS][4];
  for (uint8_t i = 0; i < CAL_ARMS; i++)
  {
    if (g_calArmTimedOut[i]) snprintf(cell[i], sizeof(cell[i]), "T");
    else snprintf(cell[i], sizeof(cell[i]), "%u", (unsigned)g_calArmHigh[i]);
  }
  snprintf(g_calExpText, sizeof(g_calExpText), "e%s %s %s %s %s w%lu", cell[0], cell[1], cell[2],
           cell[3], cell[4],
           (unsigned long)(g_calWakes > CAL_WAKES_SHOWN_MAX ? CAL_WAKES_SHOWN_MAX : g_calWakes));
  if (g_calExpCharReady) g_calExpChar.write(g_calExpText, strlen(g_calExpText));
  if (!zeroRead)
  {
    snprintf(g_adcCalText, sizeof(g_adcCalText), "read failed");
  }
  else
  {
    const bool out = saadcZeroOut(zero);
    if (tries > 1 && g_calRepeatedWakes < SAADC_CAL_COUNT_MAX) g_calRepeatedWakes++;
    if (out && g_calBadWakes < SAADC_CAL_COUNT_MAX) g_calBadWakes++;
    long shown = zero > 99 ? 99 : (zero < -99 ? -99 : zero);
    const char sign = shown < 0 ? '-' : '+';
    if (shown < 0) shown = -shown;
    snprintf(g_adcCalText, sizeof(g_adcCalText), "z%c%ld n%u r%u b%u", sign, shown,
             (unsigned)tries, (unsigned)g_calRepeatedWakes, (unsigned)g_calBadWakes);
  }
  if (g_adcCalCharReady) g_adcCalChar.write(g_adcCalText, strlen(g_adcCalText));
}

// ------------------------------------------------------------------------------ measurements --
static bool measureVbatMv(float& vbatMv)
{
  float nodeMv = 0;
  pinOutput(PIN_VBAT_DIV_EN, true);
  delay(2);  // C9 is DNP: nothing to charge, only Q1's gate
  const bool ok = readAinMv(AIN_VBAT, 4, 40, nodeMv);  // TACQ 40 us for the 50k source
  pinOutput(PIN_VBAT_DIV_EN, false);
  vbatMv = nodeMv * 2.0f;  // R5/R9 = 100k/100k
  return ok;
}

// NTC R17 (NCP15XH103F03RC, 10k, B25/50 = 3380 K) from TEMP to GND, R16 = 10k from TEMP_READ.
// Ratio against VDD read on the same pass, so the LDO's exact output cancels.
static bool measureTemp(float& tempC)
{
  float tempMv = 0, vddMv = 0;
  pinOutput(PIN_TEMP_READ, true);
  delay(1);  // C16 1 nF x 5k = 5 us
  bool ok = readAinMv(AIN_TEMP, 4, 10, tempMv);
  ok = ok && saadcReadMv(PSEL_VDD, 4, 10, vddMv);
  pinOutput(PIN_TEMP_READ, false);
  if (!ok || vddMv <= 0.0f) return false;
  const float ratio = tempMv / vddMv;
  if (ratio <= 0.001f || ratio >= 0.999f) return false;
  const float rNtc = 10000.0f * ratio / (1.0f - ratio);
  const float invT = 1.0f / 298.15f + logf(rNtc / 10000.0f) / 3380.0f;
  tempC = 1.0f / invT - 273.15f;
  return true;
}

// nRF52840 die temperature through the SoftDevice (it owns TEMP while enabled), in its native
// 0.25 C units. HFXO is requested around the read because the TEMP accuracy specification assumes
// it; MSV3 production does the same. Anything outside the part's -40..+85 C range is rejected
// rather than clamped.
static bool measureDieTempQ4(int16_t& quarterC)
{
  const bool hfxo = requestHfxo();
  int32_t q4 = 0;
  const bool ok = hfxo && sd_temp_get(&q4) == NRF_SUCCESS && q4 >= -160 && q4 <= 340;
  releaseHfxo();
  if (ok) quarterC = (int16_t)q4;
  return ok;
}

static bool readFrontEnd(float& senseMv, float& drvMv)
{
  bool ok = readAinMv(AIN_SENSE, FE_OVERSAMPLE_LOG2, 10, senseMv);
  ok = ok && readAinMv(AIN_DRVMON, FE_OVERSAMPLE_LOG2, 10, drvMv);
  return ok;
}

// ------------------------------------------------------------------------------ mask check ---
// A DC leak test of the solder mask over the plates, since application version 262162.
// The sweep cannot see what this tests. The fit has no DC path in it, and the plates reach DRV
// through R15 and U3 with no blocking capacitor, so the mask is the only thing between the copper
// and the soil. With the mask whole no DC flows and SENSE sits at DRV. Copper touching soil in
// two places (a hot plate and anything grounded) draws current through R15, and SENSE drops.
// DRV is held HIGH instead of toggled, the chopper stays on "signal" (its pulldown), and both
// ADC channels are read through the same U4 and the same 100k / 1 nF filters as the sweep:
//   drop(state)         = DRV_OUT - SENSE_OUT
//   reported, per plate = drop(that plate hot) - drop(both plates floating)
// The floating reading takes out the two channels' own offsets. What is left besides a leak is
// the change in U3's leakage between open and closed: 2 uA at most by its data sheet (IOZ), 2 mV
// across R15. A 1 Mohm path reads 3.3 mV and a 100 kohm path 33 mV.
// It takes about 20 ms, at the 3.3 V the sweep's own square wave reaches, and runs after a sweep:
// on every measurement wake in 262163, and since 262165 once in MASK_CHECK_EVERY_WAKES (a
// day) and on a boot's first measurement wake. A mask does not change by the minute.
// The result is NOT in the packet: it is the DIS "String" characteristic, "A+0.3 B-0.1 mV n2",
// which MSV4/scripts/msv4_adv_scan.py --read-build reads. The number after n counts this boot's
// checks (262165 onward), so a reader can tell a new result from the one it read before. That
// text is an ABI with that script.
// MEASURED on board 1 with 262163 (2026-10-11, 25 checks): -6.2 to +1.8 mV on either plate with
// the mask whole, so one check cannot show a path above about 500 kohm. Nothing here judges the
// number.
constexpr uint32_t MASK_SETTLE_MS = 5;   // 40 or more of the filters' 100 us time constants
constexpr uint16_t MASK_CHECK_EVERY_WAKES = 1440;
constexpr uint16_t MASK_CHECK_COUNT_MAX = 9999;   // what the text has room for
static uint16_t g_wakesSinceMaskCheck = MASK_CHECK_EVERY_WAKES;   // due on a boot's first measurement
static uint16_t g_maskChecks = 0;

static bool maskCheckDue() { return g_wakesSinceMaskCheck >= MASK_CHECK_EVERY_WAKES; }

// Signed tenths of a millivolt as text; this printf has no %f.
static void formatDeciMv(char* out, size_t size, char plate, float mv)
{
  long deci = lroundf(mv * 10.0f);
  const char sign = deci < 0 ? '-' : '+';
  if (deci < 0) deci = -deci;
  snprintf(out, size, "%c%c%ld.%ld", plate, sign, deci / 10, deci % 10);
}

static void checkMask()
{
  frontEndStop();   // DRV low, timer off, chopper select back on its pulldown
  setChopEnabled(true);
  setSwap(SwapState::Open);
  pinOutput(PIN_DRV, true, true);
  static const SwapState states[3] = {SwapState::Open, SwapState::A, SwapState::B};
  float sense[3] = {}, drv[3] = {};
  bool ok = true;
  for (int i = 0; i < 3 && ok; i++)
  {
    setSwap(states[i]);
    delay(MASK_SETTLE_MS);
    ok = readFrontEnd(sense[i], drv[i]);
  }
  // Both plates back to 0 V before they float: B through R15 to DRV, A on its ground throw.
  pinOutput(PIN_DRV, false);
  delay(1);
  allSafe();

  if (ok)
  {
    const float floating = drv[0] - sense[0];
    char a[10], b[10];
    formatDeciMv(a, sizeof(a), 'A', (drv[1] - sense[1]) - floating);
    formatDeciMv(b, sizeof(b), 'B', (drv[2] - sense[2]) - floating);
    // A check that could not be read is tried again on the next measurement wake, not a day on.
    g_wakesSinceMaskCheck = 0;
    if (g_maskChecks < MASK_CHECK_COUNT_MAX) g_maskChecks++;
    snprintf(g_maskText, sizeof(g_maskText), "%s %s mV n%u", a, b, (unsigned)g_maskChecks);
  }
  else
  {
    snprintf(g_maskText, sizeof(g_maskText), "read failed");
  }
  if (g_maskCharReady) g_maskChar.write(g_maskText, strlen(g_maskText));
}

// ------------------------------------------------------------------------------- spectrum ----
// Multi-frequency impedance fit, unchanged from the bring-up sketch; the derivation, the reason Cs
// was removed and the harmonic-aliasing model are documented there and in
// MSV4/docs/MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md.
//
// Electrode model (A hot, B + guard grounded), three parameters:
//     Ye(w) = jw*Cd + 1 / ( 1/(jw*Cm) + 1/Gs )
//   Cm = mask/interface series capacitance, Gs = soil conductance (ions),
//   Cd = direct board/fringe capacitance that never reaches soil.
// Divider node: H(w) = 1 / (1 + Rs*(Ye + jw*Cp)), Rs = R15, Cp = SENSE-node parasitics.
// Rs appears only in the product Rs*Y, so a wrong Rs scales every fitted admittance by the ratio:
// board 2 (R15 = 3.3 kohm) read Cp 28.8 pF and Cd 44.8 pF through a 1 kohm fit, 3.3x its real
// 8.7 / 13.6 pF. A board whose R15 is not the netlist's 1 kohm is built with -RsOhms.
#ifdef MSV4_RS_OHMS
static_assert(MSV4_RS_OHMS >= 500 && MSV4_RS_OHMS <= 20000, "MSV4_RS_OHMS is this board's R15 in ohms: 500..20000");
constexpr double SPEC_RS = MSV4_RS_OHMS;
#else
constexpr double SPEC_RS = 1000.0;       // R15 as in the netlist
#endif
// The value in use until calibrateCpAfterOpenReading() accepts a measured one (board 1 reads
// 8.66-8.83 pF in air). ZERO, so that Cd until then is the whole node capacitance, Cp included.
// Zero and not a typical value (it was 12 pF up to 262159) because the fit keeps Cd positive: a
// Cp larger than the capacitance the fit finds pins Cd at nothing and Rsoil takes the error. On
// synthetic moist soil at 355 ohm, MSV4/scripts/test_msv4_cp_calibration.py reads 0.9 % high
// with Cp 0 and 3.8 % high with a Cp taken with a hand on the blade. A Cp that is too low costs
// only the offset in Cd.
// A build for a probe that is ALREADY IN SOIL may carry the Cp that probe last calibrated, read
// from its DIS string before the update (-CpCentiPf on arduino_msv4_probe.ps1). The update's
// reboot happens in soil, so without it that boot would report the whole node capacitance for as
// long as the probe stays planted. A later calibration in air still replaces it.
#ifdef MSV4_CP_CENTI_PF
static_assert(MSV4_CP_CENTI_PF >= 200 && MSV4_CP_CENTI_PF <= 2550,
              "MSV4_CP_CENTI_PF is this board's last calibrated Cp in 0.01 pF: 200..2550");
constexpr double SPEC_CP_DEFAULT = MSV4_CP_CENTI_PF * 1e-14;
#else
constexpr double SPEC_CP_DEFAULT = 0.0;
#endif
constexpr double SPEC_CP_START = 10e-12;  // where the calibration fit starts
constexpr double SPEC_CP_MIN = 2e-12;
constexpr double SPEC_CP_MAX = 25.5e-12;  // plausibility bound for a calibration
// A calibration must leave at least this much Cd: a Cp that takes the whole node capacitance was
// not a measurement of the parasitics.
constexpr double SPEC_CD_MIN_AFTER_CAL = 1e-12;
// The reading that lets a calibration run must itself be a sane one in air: valid, nothing
// conducting, and a fit residual below this. It is the ceiling of the packet's residual field.
// Board 1's 262 undisturbed open readings of 2026-10-06 to 2026-10-08 are 0.0195-0.0234; the one
// taken while the board was being moved (2026-10-08 16:39) is at or above 0.0255.
constexpr double SPEC_CAL_MAX_RMS = 0.0255;
static double g_specCp = SPEC_CP_DEFAULT;
static bool g_cpCalibrated = false;   // a measured Cp has been accepted this boot
static const uint16_t SPEC_N[] = {64, 48, 32, 24, 16, 12, 8, 6};  // 250 kHz .. 2.67 MHz
constexpr int SPEC_NF = sizeof(SPEC_N) / sizeof(SPEC_N[0]);
static_assert(SPEC_NF <= SPEC_MAX, "SpecSet too small");

static uint16_t specPhases(uint16_t n)
{
  static const uint16_t cand[] = {16, 12, 8, 6, 4};
  for (uint16_t m : cand)
    if (m <= n && n % m == 0) return m;
  return 0;
}

// H = 1 / (1 + Rs*(Ye + jw*Cp)) with Ye = jw*Cd + 1/(1/(jw*Cm) + 1/Gs), written out in real
// arithmetic. The series branch is 1/Gs - j/(w*Cm), so its admittance is (a + jb)/(a^2 + b^2)
// with a = 1/Gs and b = 1/(w*Cm). The bring-up sketch evaluates the same expression through the
// general complex type, which costs about four times the double-precision operations, and this
// function is where the fit spends its time (no double FPU on this core). The two forms agreed to
// 3e-16 over 200,000 random parameter sets, 2026-10-06.
static cplx specNodeH(double w, const double* p, double cp)
{
  // p = {Cm, Gs, Cd}
  const double a = 1.0 / p[1];
  const double b = 1.0 / (w * p[0]);
  const double d = a * a + b * b;
  const double dr = 1.0 + SPEC_RS * (a / d);
  const double di = SPEC_RS * (w * (p[2] + cp) + b / d);
  const double m = dr * dr + di * di;
  return cplx(dr / m, -di / m);
}

// Predicted normalised bin-1 for timer period n sampled at m phases.
static cplx specModel(uint16_t n, uint16_t m, const double* p, double cp)
{
  const double w1 = 2.0 * M_PI * (double)FE_CLOCK_HZ / n;
  cplx num(0, 0);
  double den = 0;
  for (int k = -8; k <= 8; k++)
  {
    const int h = 1 + k * (int)m;
    if ((h & 1) == 0 || h == 0 || h > 63 || h < -63) continue;
    const double wt = 1.0 / ((double)h * h);
    cplx hv = specNodeH(w1 * (h < 0 ? -h : h), p, cp);
    if (h < 0) hv = cconj(hv);
    num += wt * hv;
    den += wt;
  }
  return num / den;
}

// Measures X1(SENSE)/X1(DRV_OUT) at one frequency. False on a front-end or SAADC failure.
static bool specMeasure(uint16_t n, uint16_t m, cplx& h)
{
  cplx xs(0, 0), xd(0, 0);
  for (uint16_t i = 0; i < m; i++)
  {
    const uint16_t k = (uint16_t)((uint32_t)i * n / m);
    if (!frontEndStart(n, k)) return false;
    delayFeeding(FE_SETTLE_MS);
    float vs, vd;
    if (!readFrontEnd(vs, vd)) return false;
    const double th = 2.0 * M_PI * i / m;
    const cplx e = cpolar(1.0, -th);
    xs += (double)vs * e;
    xd += (double)vd * e;
  }
  if (cabs2(xd) < 1e-6) return false;  // DRV_OUT has no fundamental
  h = xs / xd;
  return true;
}

static bool specSweep(SpecSet& s)
{
  s.count = 0;
  for (int i = 0; i < SPEC_NF; i++)
  {
    const uint16_t n = SPEC_N[i], m = specPhases(n);
    cplx h;
    if (!specMeasure(n, m, h)) { frontEndStop(); return false; }
    s.n[s.count] = n; s.m[s.count] = m; s.h[s.count] = h; s.count++;
  }
  frontEndStop();
  return true;
}

// Levenberg-Marquardt on ln(parameters) for the free subset of {Cm, Gs, Cd}.
// Returns RMS residual; fills relative 1-sigma uncertainties (from J^T J) for free parameters.
static double specFit(const SpecSet& s, double* p, const bool* freeP, double cp, double* relSigma)
{
  constexpr int NP = 3;
  int idx[NP], nf = 0;
  for (int i = 0; i < NP; i++) if (freeP[i]) idx[nf++] = i;
  const int nr = 2 * s.count;
  auto resid = [&](const double* q, double* r) {
    for (int i = 0; i < s.count; i++)
    {
      const cplx d = specModel(s.n[i], s.m[i], q, cp) - s.h[i];
      r[2 * i] = d.real(); r[2 * i + 1] = d.imag();
    }
  };
  double r[2 * SPEC_MAX], rt[2 * SPEC_MAX], J[2 * SPEC_MAX][NP];
  double lambda = 1e-3;
  resid(p, r);
  double ss = 0; for (int i = 0; i < nr; i++) ss += r[i] * r[i];
  for (int it = 0; it < 60; it++)
  {
    for (int a = 0; a < nf; a++)
    {
      double q[NP]; memcpy(q, p, sizeof(q));
      q[idx[a]] *= exp(1e-4);
      resid(q, rt);
      for (int i = 0; i < nr; i++) J[i][a] = (rt[i] - r[i]) / 1e-4;
    }
    double A[NP][NP] = {}, g[NP] = {};
    for (int a = 0; a < nf; a++)
    {
      for (int b = 0; b < nf; b++) for (int i = 0; i < nr; i++) A[a][b] += J[i][a] * J[i][b];
      for (int i = 0; i < nr; i++) g[a] -= J[i][a] * r[i];
    }
    bool improved = false;
    for (int tries = 0; tries < 8 && !improved; tries++)
    {
      double M[NP][NP + 1];
      for (int a = 0; a < nf; a++)
      {
        for (int b = 0; b < nf; b++) M[a][b] = A[a][b] * (a == b ? 1.0 + lambda : 1.0);
        M[a][nf] = g[a];
      }
      for (int c = 0; c < nf; c++)  // Gaussian elimination with partial pivoting
      {
        int pv = c;
        for (int r2 = c + 1; r2 < nf; r2++) if (fabs(M[r2][c]) > fabs(M[pv][c])) pv = r2;
        for (int k = 0; k <= nf; k++) { double t = M[c][k]; M[c][k] = M[pv][k]; M[pv][k] = t; }
        if (fabs(M[c][c]) < 1e-300) break;
        for (int r2 = c + 1; r2 < nf; r2++)
        {
          const double f = M[r2][c] / M[c][c];
          for (int k = c; k <= nf; k++) M[r2][k] -= f * M[c][k];
        }
      }
      double dx[NP] = {};
      for (int c = nf - 1; c >= 0; c--)
      {
        double v = M[c][nf];
        for (int k = c + 1; k < nf; k++) v -= M[c][k] * dx[k];
        dx[c] = fabs(M[c][c]) > 1e-300 ? v / M[c][c] : 0;
      }
      double q[NP]; memcpy(q, p, sizeof(q));
      for (int a = 0; a < nf; a++)
      {
        const double step = dx[a] > 3 ? 3 : (dx[a] < -3 ? -3 : dx[a]);
        q[idx[a]] *= exp(step);
      }
      resid(q, rt);
      double ss2 = 0; for (int i = 0; i < nr; i++) ss2 += rt[i] * rt[i];
      if (ss2 < ss)
      {
        memcpy(p, q, sizeof(q)); memcpy(r, rt, sizeof(double) * nr);
        const bool done = (ss - ss2) < 1e-12 * ss;
        ss = ss2; lambda *= 0.3; improved = true;
        if (done) it = 1000;
      }
      else lambda *= 10;
    }
    if (!improved) break;
    feedWatchdog();
  }
  // Covariance ~ s^2 (J^T J)^-1 in ln-parameter space -> relative sigma = sqrt(diag).
  const int dof = nr - nf;
  const double s2 = dof > 0 ? ss / dof : 0;
  for (int a = 0; a < nf; a++)
  {
    double q[NP]; memcpy(q, p, sizeof(q));
    q[idx[a]] *= exp(1e-4);
    resid(q, rt);
    for (int i = 0; i < nr; i++) J[i][a] = (rt[i] - r[i]) / 1e-4;
  }
  double A[NP][NP] = {}, Inv[NP][NP] = {};
  for (int a = 0; a < nf; a++)
    for (int b = 0; b < nf; b++) for (int i = 0; i < nr; i++) A[a][b] += J[i][a] * J[i][b];
  for (int a = 0; a < nf; a++) Inv[a][a] = 1;
  for (int c = 0; c < nf; c++)  // Gauss-Jordan inverse
  {
    int pv = c;
    for (int r2 = c + 1; r2 < nf; r2++) if (fabs(A[r2][c]) > fabs(A[pv][c])) pv = r2;
    for (int k = 0; k < nf; k++) { double t = A[c][k]; A[c][k] = A[pv][k]; A[pv][k] = t;
                                   t = Inv[c][k]; Inv[c][k] = Inv[pv][k]; Inv[pv][k] = t; }
    const double d = A[c][c];
    if (fabs(d) < 1e-300) continue;
    for (int k = 0; k < nf; k++) { A[c][k] /= d; Inv[c][k] /= d; }
    for (int r2 = 0; r2 < nf; r2++)
    {
      if (r2 == c) continue;
      const double f = A[r2][c];
      for (int k = 0; k < nf; k++) { A[r2][k] -= f * A[c][k]; Inv[r2][k] -= f * Inv[c][k]; }
    }
  }
  for (int i = 0; i < NP; i++) relSigma[i] = 0;
  for (int a = 0; a < nf; a++) relSigma[idx[a]] = sqrt(fabs(Inv[a][a] * s2));
  return sqrt(ss / nr);
}

// Electrodes floating (U3 /OE high), so Ye = 0 and the sweep sees the SENSE node's parasitics.
// Returns the fitted value without putting it to use; false on a failed sweep or an out-of-range
// fit.
static bool measureCp(double& cp)
{
  setSwap(SwapState::Open);
  setChopEnabled(true);
  SpecSet s;
  const bool ok = specSweep(s);
  allSafe();
  if (!ok) return false;
  // Fit Cp alone: Cm tiny makes zm huge, so the series branch vanishes, Ye = jw*Cd, and Cd plays
  // Cp's role.
  double p[3] = {1e-15, 1e-12, SPEC_CP_START}, sig[3];
  const bool freeP[3] = {false, false, true};
  specFit(s, p, freeP, 0.0, sig);
  if (p[2] < SPEC_CP_MIN || p[2] > SPEC_CP_MAX) return false;
  cp = p[2];
  return true;
}

// The Cp in use, as text, in the DIS hardware revision string. " default" means no calibration
// has been accepted this boot. The string changes when one is, so it is this sketch's own
// characteristic and not one of BLEDis's write-once strings.
static void publishCp()
{
  const unsigned cpCenti = (unsigned)(g_specCp * 1e14 + 0.5);
  snprintf(g_hardwareRev, sizeof(g_hardwareRev), "Cp %u.%02u pF%s", cpCenti / 100, cpCenti % 100,
           g_cpCalibrated ? "" : " default");
  if (g_hardwareRevReady) g_hardwareRevChar.write(g_hardwareRev, strlen(g_hardwareRev));
}

// A full fit is four cold starts across four decades of Gs, about two seconds of arithmetic.
// Since application version 262157 a measurement first tries ONE fit started from the last full
// fit's answer (the anchor) and keeps it only if
//   - its residual is within FIT_WARM_ACCEPT of the anchor's, and
//   - the answer is still near the anchor: reported Rsoil and Cd within FIT_WARM_MAX_MOVE.
// Anything else -- no anchor yet, a worse residual, an answer that has moved, a non-finite
// result -- runs the four cold starts exactly as before and takes the best of all five, so the
// worst case is the old behaviour plus one short fit.
// - The warm fit starts from the ANCHOR, never from the previous warm result. Cm and Gs are flat
//   directions whenever little conducts, and steps the solver takes there on rounding noise
//   would otherwise accumulate from one measurement to the next.
// - An open anchor (Rsoil at or above the wire ceiling) is not used as it stands: at 1e-16 S the
//   solver has no slope to follow back if the medium starts to conduct. The start is then a cold
//   start's Cm with Gs AT the ceiling, and only Cd is carried over.
// - The move limit is there because this solver can stall short of the minimum on a long path
//   (seen in the synthetic test: 357 ohm where every cold start gave 364). A fit that had to
//   travel means the medium changed, which is when a full fit is wanted anyway.
// - Every FIT_FULL_EVERY_N-th measurement is a full fit regardless, which re-anchors.
// MSV4/scripts/test_msv4_warm_fit.py runs this logic against four cold starts on synthetic
// sweeps. Change one, change the other.
constexpr double FIT_WARM_ACCEPT = 1.10;
// One count of the packet's residual field, so a near-perfect anchor cannot reject everything.
constexpr double FIT_WARM_SLACK = 1e-4;
constexpr double FIT_WARM_MAX_MOVE = 1.25;
constexpr uint8_t FIT_FULL_EVERY_N = 12;   // a full fit every two hours at ten minutes each
constexpr double FIT_OPEN_OHM = 400000.0;  // the wire ceiling; asserted against RSOIL_DECIOHM_MAX
constexpr double FIT_COLD_CM = 500e-12;
constexpr double FIT_COLD_CD = 5e-12;
static double g_fitAnchor[3];
static double g_fitAnchorRms = 0;
static bool g_fitAnchorValid = false;
static uint8_t g_fitsSinceFull = 0;

// Compared as the packet reports it: every resistance above the ceiling is the same "open".
static bool fitNearAnchor(const double* p)
{
  const double r = fmin(1.0 / p[1], FIT_OPEN_OHM);
  const double ra = fmin(1.0 / g_fitAnchor[1], FIT_OPEN_OHM);
  return r <= ra * FIT_WARM_MAX_MOVE && ra <= r * FIT_WARM_MAX_MOVE &&
         p[2] <= g_fitAnchor[2] * FIT_WARM_MAX_MOVE && g_fitAnchor[2] <= p[2] * FIT_WARM_MAX_MOVE;
}

// The averaged sweep the last fit was made from, for the scan response (see SWEEP_RSP_LEN).
// Cleared at the start of every sweep, so a failed one cannot resend the previous points.
static SpecSet g_lastSweep;
static bool g_lastSweepValid = false;

// Sweeps both orientations, averages the complex transfers, and fits {Cm, Gs, Cd}. `warm` says
// whether the answer is an accepted warm fit.
static bool runSpectrumFit(double* best, double& bestRms, bool& warm)
{
  warm = false;
  g_lastSweepValid = false;
  setChopEnabled(true);
  SpecSet acc, s;
  for (int o = 0; o < 2; o++)
  {
    setSwap(o == 0 ? SwapState::A : SwapState::B);
    if (!specSweep(s)) return false;
    if (o == 0) acc = s;
    else for (int i = 0; i < s.count; i++) acc.h[i] += s.h[i];
  }
  // Analog path off before the fit: the arithmetic below takes seconds and needs none of it.
  allSafe();
  for (int i = 0; i < acc.count; i++) acc.h[i] /= 2.0;
  g_lastSweep = acc;
  g_lastSweepValid = true;

  const bool freeP[3] = {true, true, true};
  bestRms = 1e30;
  if (g_fitAnchorValid && g_fitsSinceFull + 1 < FIT_FULL_EVERY_N)
  {
    double p[3], sig[3];
    memcpy(p, g_fitAnchor, sizeof(p));
    if (1.0 / p[1] >= FIT_OPEN_OHM)
    {
      p[0] = FIT_COLD_CM;
      p[1] = 1.0 / FIT_OPEN_OHM;
    }
    const double rms = specFit(acc, p, freeP, g_specCp, sig);
    if (isfinite(rms))
    {
      // Kept as a fifth candidate even when it is not accepted.
      bestRms = rms;
      memcpy(best, p, sizeof(p));
      if (rms <= g_fitAnchorRms * FIT_WARM_ACCEPT + FIT_WARM_SLACK && fitNearAnchor(p))
      {
        g_fitsSinceFull++;
        warm = true;
        return true;
      }
    }
  }

  // Several starting points for Gs across four decades; keep the best fit.
  static const double gStart[] = {1e-5, 1e-4, 1e-3, 1e-2};
  for (double g0 : gStart)
  {
    double p[3] = {FIT_COLD_CM, g0, FIT_COLD_CD}, sig[3];
    const double rms = specFit(acc, p, freeP, g_specCp, sig);
    if (rms < bestRms)
    {
      bestRms = rms;
      memcpy(best, p, 3 * sizeof(double));
    }
  }
  g_fitAnchorValid = isfinite(bestRms) && best[0] > 0.0 && best[1] > 0.0 && best[2] > 0.0;
  if (g_fitAnchorValid)
  {
    memcpy(g_fitAnchor, best, sizeof(g_fitAnchor));
    g_fitAnchorRms = bestRms;
  }
  g_fitsSinceFull = 0;
  return true;
}

// Wire limits. Rsoil saturates at 400 kohm: with a 1 kohm source resistor the divider cannot tell
// larger values apart, and in air the fit returns 1e14 ohm or more. 4,000,000 also sits below the
// 5,000,000-count floor the receiver applies to an MSV3 FDC count, so a receiver that does not yet
// know this packet treats it as a failed reading instead of storing ohms as counts.
constexpr uint32_t RSOIL_DECIOHM_MAX = 4000000UL;
// Rsoil therefore needs only three of its four bytes; the fourth carries the fit residual.
static_assert(RSOIL_DECIOHM_MAX < (1UL << 24), "Rsoil must fit 24 bits: mfg[13] is the residual");
static_assert(static_cast<uint32_t>(FIT_OPEN_OHM * 10.0) == RSOIL_DECIOHM_MAX,
              "the warm fit's idea of open must be the wire ceiling");
// The fit occasionally collapses to ~0 ohm with an enormous uncertainty when there is nothing to
// conduct. That is "no fit", not "very wet".
constexpr double RSOIL_MIN_VALID_OHM = 1.0;

// Cp is calibrated once per boot, straight after the first moisture reading that is a sane one in
// air (valid, nothing conducting, an ordinary fit residual), and never on the strength of any
// other.
// Why: the floating-electrode sweep does not see the SENSE node alone. What the electrodes are
// touching reaches it through U3's off-capacitance. Up to application version 262159 the
// calibration ran at every boot whatever the blade was in, and on 2026-10-08 board 1 reset with a
// hand on it: it calibrated 15.89 pF against 8.66-8.83 pF in air and reported Cd 7 pF low until
// its next boot.
// Cp and Cd enter the model only as their sum, so a wrong Cp moves Cd by the same amount and
// nothing else: Rsoil and Cm do not depend on it. That is also why the reading just taken can be
// put on the new Cp by arithmetic, without a second fit.
// Until a calibration is accepted Cp is the build's value (zero unless the build carries this
// board's own, see SPEC_CP_DEFAULT) and flags2 bit6 says so. A probe that boots in conducting
// soil therefore stays on it for that boot; with zero, its Cd is the whole node capacitance.
// NOT COVERED: a medium too dry to conduct also reads open. Not yet measured in soil.
// Takes the Cd of an open reading and returns the Cd to report.
static double calibrateCpAfterOpenReading(double cd)
{
  feedWatchdog();
  double cp = 0;
  if (!measureCp(cp)) return cd;
  const double shift = g_specCp - cp;   // Cd gains what Cp gives up
  if (cd + shift < SPEC_CD_MIN_AFTER_CAL) return cd;
  g_specCp = cp;
  g_cpCalibrated = true;
  // The anchor is a fit made on the old Cp.
  if (g_fitAnchorValid)
  {
    g_fitAnchor[2] += shift;
    if (!(g_fitAnchor[2] > 0.0)) g_fitAnchorValid = false;
  }
  publishCp();
  return cd + shift;
}

static MoistureReading takeMoistureReading()
{
  MoistureReading out = {false, 0, 0, 0, 0, false};
  double best[3] = {}, bestRms = 0;
  bool warm = false;
  const bool swept = runSpectrumFit(best, bestRms, warm);
  allSafe();
  if (!swept) return out;
  out.warm = warm;

  const double cm = best[0], gs = best[1];
  double cd = best[2];
  const double rsoil = gs > 0.0 ? 1.0 / gs : 0.0;
  if (!(gs > 0.0) || !(cm > 0.0) || !(cd > 0.0) || !isfinite(bestRms) || !isfinite(rsoil) ||
      rsoil < RSOIL_MIN_VALID_OHM)
  {
    // An answer the packet will not carry must not seed the next fit either: the retry on the
    // next wake is a full fit, as it always was.
    g_fitAnchorValid = false;
    return out;
  }
  // Calibrate only on the strength of a sane reading in air; see calibrateCpAfterOpenReading().
  const bool saneOpenReading = rsoil >= FIT_OPEN_OHM && bestRms < SPEC_CAL_MAX_RMS;
  if (!g_cpCalibrated && saneOpenReading) cd = calibrateCpAfterOpenReading(cd);

  const double rDeci = rsoil * 10.0 + 0.5;
  out.rsoilDeciOhm = rDeci >= (double)RSOIL_DECIOHM_MAX ? RSOIL_DECIOHM_MAX : (uint32_t)rDeci;
  const double cmDeci = cm * 1e13 + 0.5;
  out.cmDeciPf = cmDeci >= 65535.0 ? 65535 : (uint16_t)cmDeci;
  const double rmsE4 = bestRms * 1e4 + 0.5;
  out.rmsE4 = rmsE4 >= 255.0 ? 255 : (uint8_t)rmsE4;
  // 0.01 pF units: console repeats on board 1 in air were 12.98-13.03 pF, so anything coarser
  // would hide the measurement's own scatter. 0 is reserved for "not sent".
  const double cdCenti = cd * 1e14 + 0.5;
  out.cdCentiPf = cdCenti >= 65535.0 ? 65535 : (cdCenti < 1.0 ? 1 : (uint16_t)cdCenti);
  out.valid = true;
  return out;
}

// ------------------------------------------------------------------------------------ BLE ----
// Manufacturer data. Bytes 0-18 keep the MSV3 production layout so the receiver's existing
// length and offset logic applies; flag bit7 says the sensor fields mean something else.
//
// [0-1]   companyId (0xFFFF)
// [2-5]   magic "MSV2"
// [6]     sensorId
// [7-9]   advertisement counter, 24-bit LE (the receiver's "wake")
// [10-12] Rsoil, 24-bit LE, units of 0.1 ohm, NOT temperature-corrected. Saturates at
//         RSOIL_DECIOHM_MAX, which means nothing is conducting (blade in air, or no soil
//         contact); it is a ceiling, not a measured 400 kohm. Zero when bit5 is set.
// [13]    fit RMS residual, units of 1e-4 of drive, saturating (layout B, see [25]). This byte
//         was Rsoil's always-zero top byte before, so a reader that still takes [10-13] as one
//         uint32 sees an out-of-range resistance and rejects the reading rather than misreading it.
// [14-15] battery mV, 16-bit LE
// [16]    flags: bit0 = 0 (this board has no charger status line)
//                bit1 = the moisture fields were measured on this wake (0 = repeated)
//                bit2 = tip temperature valid
//                bit3 = 0 always (MSV3 bare-tank reference valid)
//                bit4 = OTA allowed (battery at or above OTA_MIN_BATTERY_MV, or unreadable)
//                bit5 = moisture reading invalid
//                bit6 = 0 (MSV3 capless tank)
//                bit7 = MSV4 conductance packet
// [17-18] blade-tip NTC temperature, signed int16 LE, quarter-degrees C (0 when bit2 is clear)
// [19-20] Cm, uint16 LE, units of 0.1 pF, saturating. Contact quality, not moisture, and only
//         meaningful while something conducts: it reads 0 with the blade in air.
// [21-22] Cd, uint16 LE, units of 0.01 pF, saturating; 0 = not sent (layout B). The direct plate
//         capacitance, and the one fitted quantity that is still a live measurement with the
//         blade in air: 12.98-13.03 pF on board 1 through the console, 2026-10-06.
// [23-24] nRF52840 die temperature, signed int16 LE, quarter-degrees C (0 when flags2 bit0 is
//         clear). The board end of the probe, NOT the soil: [17-18] is the soil.
// [25]    flags2: bit0 = die temperature valid
//                 bit2 = short burst: this wake is advertising non-connectably and no client can
//                        connect. An updater must wait for a packet with this bit clear.
//                 bit1 = layout B: [13] is the residual and [21-22] are Cd. Clear in packets
//                        from application versions 262148/262150 (layout A), where [13] was 0,
//                        [21] was Cp in 0.1 pF and [22] was the residual.
//                 bit3 = the DC/DC regulator is enabled: DCDCEN read back as this packet was
//                        built (262155 onward). Clear on the first wake of every boot.
//                 bit4 = the moisture fields came from an accepted warm-start fit, not the four
//                        cold starts (262157 onward; see runSpectrumFit()). Stays with the
//                        reading, so repeats of it carry the same bit.
//                 bit5 = discharge burn: the probe will spin instead of sleep until its next
//                        wake. Only a test build made with -BurnFloorMv ever sets it.
//                 bit6 = no Cp calibration has been accepted this boot (262160 onward; see
//                        calibrateCpAfterOpenReading()). Cd is then on the build's Cp: with the
//                        usual zero it is the whole node capacitance, Cp included. Rsoil and Cm
//                        are not affected.
//                 bit7 = the battery saver is saving (262165 onward; see saverJudge()):
//                        moisture is measured less often than the build's cadence, so fewer
//                        packets are fresh and fewer wakes can be connected to.
// Cp is no longer in the packet: it is published as the DIS hardware revision string instead
// ("Cp 8.76 pF"; while bit6 is set, "Cp 0.00 pF default" or the build's own value and
// " default"). The mask check (262162 onward) is not in the packet either: see checkMask().
//
// Bytes 23-25 were added in application version 262150. A 23-byte packet from 262148 is still
// valid; readers must treat the tail as optional. The three bytes came from dropping the TX Power
// AD structure, which nothing reads (a receiver measures RSSI itself): Flags(3) + this(2+26) = 31.
//
// THIS LAYOUT IS AN ABI with MSV4/scripts/msv4_adv_scan.py and with the receiver. Change one side,
// change the others.
#define ADV_FLAG_FRESH           (1U << 1)
#define ADV_FLAG_TEMP_OK         (1U << 2)
#define ADV_FLAG_OTA_POWER_OK    (1U << 4)
#define ADV_FLAG_READING_INVALID (1U << 5)
#define ADV_FLAG_MSV4            (1U << 7)

#define ADV_FLAG2_DIE_TEMP_OK    (1U << 0)
#define ADV_FLAG2_LAYOUT_B       (1U << 1)
#define ADV_FLAG2_SHORT_BURST    (1U << 2)
#define ADV_FLAG2_DCDC_ON        (1U << 3)
#define ADV_FLAG2_FIT_WARM       (1U << 4)
#define ADV_FLAG2_BURN           (1U << 5)
#define ADV_FLAG2_CP_DEFAULT     (1U << 6)
#define ADV_FLAG2_SAVER          (1U << 7)

constexpr size_t MFG_LEN = 26;
static_assert(3 /* Flags */ + 2 /* manufacturer-data header */ + MFG_LEN <= 31,
              "advertising payload exceeds the 31-byte legacy PDU limit");

// Scan response on a wake that measured (application version 262172 onward): the raw averaged
// sweep the fit was made from, so electrode models other than the firmware's can be fitted to
// stored data (MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md). Every other long-window
// wake keeps the name there. An OTA client does not need the name: msv4_ble_ota.py accepts the
// production advertisement alone.
//
// It is Service Data, not a second manufacturer-data record: Bleak on Windows merges the
// advertisement and the scan response into one dict keyed by company ID, so a record under 0xFFFF
// would replace the advertisement's and hide the production packet from the PC tools.
//
// [0-1]   16-bit service UUID SWEEP_UUID16, LE (not a SIG-assigned value)
// [2]     layout version, 1
// [3]     sensorId
// [4]     advertisement counter, low byte: the same wake as the advertisement's [7]
// [5-28]  8 points in SPEC_N order (250 kHz .. 2.67 MHz), 3 bytes each: the transfer H's real
//         and imaginary parts as signed 12-bit integers in units of 1/2048 of drive, saturating
//         at -2048..2047. Byte 0 = re[7:0], byte 1 = re[11:8] | im[3:0] << 4, byte 2 = im[11:4].
//         M for each point is specPhases(N); the PC side reproduces it.
// Measured |H| stays under 0.99 from open to salt water, and a 1/2048 step adds 1.4e-4 rms, under
// the 2.7e-4 sweep-to-sweep scatter.
//
// THIS LAYOUT IS AN ABI with MSV4/scripts/msv4_adv_scan.py and with the receiver. Change one side,
// change the others.
constexpr uint16_t SWEEP_UUID16 = 0x4D34;   // "M4"
constexpr uint8_t SWEEP_LAYOUT = 1;
constexpr size_t SWEEP_RSP_LEN = 5 + 3 * SPEC_NF;
static_assert(SPEC_NF == 8, "the scan-response sweep layout carries exactly 8 points");
static_assert(2 /* AD header */ + SWEEP_RSP_LEN <= 31, "scan response exceeds the 31-byte legacy PDU limit");

static int16_t sweepQ12(double v)
{
  const long q = lround(v * 2048.0);
  return (int16_t)(q < -2048 ? -2048 : q > 2047 ? 2047 : q);
}

static void packSweep(uint8_t* out)
{
  out[0] = lowByte(SWEEP_UUID16);
  out[1] = highByte(SWEEP_UUID16);
  out[2] = SWEEP_LAYOUT;
  out[3] = SENSOR_ID;
  out[4] = (uint8_t)(g_advertCount & 0xFF);
  for (int i = 0; i < SPEC_NF; i++)
  {
    const uint16_t re = (uint16_t)sweepQ12(g_lastSweep.h[i].re) & 0x0FFF;
    const uint16_t im = (uint16_t)sweepQ12(g_lastSweep.h[i].im) & 0x0FFF;
    out[5 + 3 * i] = (uint8_t)(re & 0xFF);
    out[6 + 3 * i] = (uint8_t)((re >> 8) | ((im & 0x0F) << 4));
    out[7 + 3 * i] = (uint8_t)(im >> 4);
  }
}

static bool sweepSendable()
{
  if (!g_lastSweepValid || g_lastSweep.count != SPEC_NF) return false;
  for (int i = 0; i < SPEC_NF; i++)
    if (g_lastSweep.n[i] != SPEC_N[i]) return false;
  return true;
}

// Fast for the first second, then 250 ms for the rest of the window: the schedule MSV3 arrived at
// after a single 500 ms check kept losing Windows OTA clients.
const uint32_t OTA_ADV_LISTEN_MS = 5000;
const uint16_t ADV_FAST_INTERVAL_UNITS = 32;   // 20 ms
const uint16_t ADV_SLOW_INTERVAL_UNITS = 400;  // 250 ms
const uint16_t ADV_FAST_TIMEOUT_SECONDS = 1;
const uint16_t OTA_ADV_HARD_TIMEOUT_SECONDS = OTA_ADV_LISTEN_MS / 1000;
const uint32_t OTA_UNAVAILABLE_ADV_MS = 500;
// Since application version 262154 the full connectable window above is opened only on wakes
// that take a moisture measurement (every MOISTURE_EVERY_N_WAKES-th) and on the first
// LONG_WINDOW_BOOT_WAKES wakes after a boot, so a fresh install can be checked or replaced at
// once. Every other wake sends a short non-connectable burst instead: the same packet, but about
// 16 advertising events instead of about 56, and no receive window after each one. Advertising
// was the largest slice of the modelled energy budget and those 40 extra events per minute served
// only an updater that is almost never there.
//
// The burst is spread over four seconds on purpose. The receiver listens 25 ms in every 75 and
// stops scanning for two to three seconds around each cloud upload, so the same number of events
// packed into a few hundred milliseconds would be lost whole whenever they met an upload.
const uint32_t SHORT_ADV_MS = 4000;
const uint16_t SHORT_ADV_INTERVAL_UNITS = 400;  // 250 ms
const uint8_t LONG_WINDOW_BOOT_WAKES = 3;
static_assert(SHORT_ADV_MS % 1000 == 0, "SHORT_ADV_MS must be whole seconds for the SoftDevice timeout");

// Covers connect + service discovery + one write to the DFU control characteristic. The transfer
// itself happens in the bootloader.
const uint32_t OTA_CONNECTED_HOLD_MS = 30000;
// Let the DFU authorize response reach the client before the reset tears the radio down.
const uint32_t DFU_RESPONSE_SETTLE_MS = 150;

// --------------------------------------------------------------------------- battery saver ---
// Since application version 262165 the wake's own battery reading decides how much the probe
// does. The cell works between the BQ25504's load cut-off (3.37 V) and its charge limit (3.82 to
// 3.83 V as this ADC reads it), and a board that reaches the cut-off stays dark until the panel
// has brought the cell back above 3.73 V. Two levels (the maintainer, 2026-10-11: "the low
// should be at the 3500 mv range, and there should be no critical"):
//
//   level     battery, falling (back at)   what changes
//   Full      --                           nothing. The LED blinks twice before each sweep.
//   Saving    below 3500 mV (3550)         no LED. Moisture on every 30th wake at most: the
//                                          sweep and fit, and with them the connectable window,
//                                          which only opens on that wake.
//
// NOT touched at either level: the one-minute wake and its packet (the receiver and the History
// page date a boot by counting wakes of one minute), the +8 dBm transmit power, the short bursts,
// and the connectable window on every measurement wake and on the first wakes of a boot (the
// only way in). Saving begins at the OTA floor, so an update is refused there anyway.
// The level changes only when SAVER_CONFIRM_WAKES wakes in a row read past the threshold: single
// battery readings have stood 14-22 mV high about one wake in fifty. The first usable reading of
// a boot sets the level at once, and a reading that cannot be used changes nothing.
// Board 1's hourly battery readings have only run from 3.71 V (the discharge burn) to 3.84 V, so
// it has never been near this level. On the receiver's battery scale 3500 mV is 3 %.
// flags2 bit7 says the saver is saving. MSV4/scripts/test_msv4_battery_saver.py runs this rule
// on made-up battery records and reads the constants below from this file.
constexpr float SAVER_SAVING_BELOW_MV = 3500.0f;
constexpr float SAVER_RETURN_MV = 50.0f;   // this far above the threshold before Saving is left
constexpr uint8_t SAVER_CONFIRM_WAKES = 2;
constexpr uint8_t SAVER_SAVING_EVERY_N_WAKES = 30;
static PowerLevel g_powerLevel = PowerLevel::Full;
static bool g_powerLevelSet = false;   // a battery reading has set the level this boot
static uint8_t g_powerPendingWakes = 0;

// The level one battery reading asks for, given the level in force: the threshold is crossed
// downward at its own value and upward only SAVER_RETURN_MV above it.
static PowerLevel saverWanted(PowerLevel now, float vbatMv)
{
  const float savingBelow = SAVER_SAVING_BELOW_MV + (now == PowerLevel::Saving ? SAVER_RETURN_MV : 0.0f);
  return vbatMv < savingBelow ? PowerLevel::Saving : PowerLevel::Full;
}

// Called once per wake with that wake's battery reading.
static void saverJudge(bool vbatOk, float vbatMv)
{
  if (!vbatOk)
  {
    g_powerPendingWakes = 0;
    return;
  }
  const PowerLevel wanted = saverWanted(g_powerLevel, vbatMv);
  if (!g_powerLevelSet || wanted == g_powerLevel)
  {
    g_powerLevel = wanted;
    g_powerLevelSet = true;
    g_powerPendingWakes = 0;
    return;
  }
  if (++g_powerPendingWakes >= SAVER_CONFIRM_WAKES)
  {
    g_powerLevel = wanted;
    g_powerPendingWakes = 0;
  }
}

// Wakes from one moisture measurement to the next at the level in force. Never shorter than the
// build's own cadence.
static uint8_t saverMoistureEveryWakes()
{
  const uint8_t floorWakes = g_powerLevel == PowerLevel::Saving ? SAVER_SAVING_EVERY_N_WAKES : 0;
  return MOISTURE_EVERY_N_WAKES > floorWakes ? MOISTURE_EVERY_N_WAKES : floorWakes;
}

// Two short blinks after a completed sweep, at Full with a battery reading to say so. One blink is
// a boot; two are a measurement. D1 draws about 1 mA through R10 (470 ohm) from the same rail the
// sweep is measured on. Application 262165 blinked immediately BEFORE the sweep and had no rail
// settling interval after the second falling edge. Measured after installing it on board 1, local
// reading-to-reading scatter rose from 5.97 to 18.18 ohm in Rsoil and from 0.233 to 0.701 pF in Cm;
// 262163 already had the checked ADC calibration and was quiet. Blinking after takeMoistureReading()
// makes the indication unable to disturb the reading it announces and leaves nearly a minute for
// the rail and electrode to settle before the next one. Lit through a whole one-second measurement
// would add over a quarter to that measurement's charge (modelled); these two blinks add about 2 %.
// Not on a boot's first wake. That wake always measures and setup() has just blinked once: three
// flashes together would read as the boot guard's three-blink escape.
constexpr uint32_t LED_MEASURE_BLINK_MS = 30;
constexpr uint32_t LED_MEASURE_GAP_MS = 120;
static void ledAnnounceMeasurement()
{
  if (!g_powerLevelSet || g_powerLevel != PowerLevel::Full || g_wakesSinceBoot <= 1) return;
  ledWrite(true);
  delay(LED_MEASURE_BLINK_MS);
  ledWrite(false);
  delay(LED_MEASURE_GAP_MS);
  ledWrite(true);
  delay(LED_MEASURE_BLINK_MS);
  ledWrite(false);
}

static void otaConnectCallback(uint16_t connHandle)
{
  // Latched by Bluefruit's BLE event task: polling Bluefruit.connected() alone can miss a
  // connection that completes between the loop's final poll and Advertising.stop().
  g_otaConnectionHandle = connHandle;
}

static void otaDisconnectCallback(uint16_t connHandle, uint8_t reason)
{
  (void)reason;
  if (g_otaConnectionHandle == connHandle) g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
}

static bool startAdv(const MoistureReading& reading, bool fresh, bool vbatOk, float vbatMv,
                     bool tempOk, float tempC, bool dieTempOk, int16_t dieTempQ4, bool otaPowerOk,
                     bool longWindow)
{
  const uint16_t batteryMv = vbatOk ? (uint16_t)(vbatMv + 0.5f) : 0;
  const int16_t tempQ4 = tempOk ? (int16_t)lroundf(tempC * 4.0f) : 0;
  const int16_t dieQ4 = dieTempOk ? dieTempQ4 : 0;
  const uint32_t rsoil = reading.valid ? reading.rsoilDeciOhm : 0;
  const uint16_t cm = reading.valid ? reading.cmDeciPf : 0;
  const uint16_t cd = reading.valid ? reading.cdCentiPf : 0;

  uint8_t mfg[MFG_LEN];
  mfg[0] = lowByte(BLE_COMPANY_ID);
  mfg[1] = highByte(BLE_COMPANY_ID);
  memcpy(&mfg[2], BLE_MAGIC, 4);
  mfg[6] = SENSOR_ID;
  mfg[7] = (uint8_t)(g_advertCount & 0xFF);
  mfg[8] = (uint8_t)((g_advertCount >> 8) & 0xFF);
  mfg[9] = (uint8_t)((g_advertCount >> 16) & 0xFF);
  mfg[10] = (uint8_t)(rsoil & 0xFF);
  mfg[11] = (uint8_t)((rsoil >> 8) & 0xFF);
  mfg[12] = (uint8_t)((rsoil >> 16) & 0xFF);
  mfg[13] = reading.valid ? reading.rmsE4 : 0;
  mfg[14] = (uint8_t)(batteryMv & 0xFF);
  mfg[15] = (uint8_t)((batteryMv >> 8) & 0xFF);
  mfg[16] = ADV_FLAG_MSV4 |
            (fresh ? ADV_FLAG_FRESH : 0) |
            (tempOk ? ADV_FLAG_TEMP_OK : 0) |
            (otaPowerOk ? ADV_FLAG_OTA_POWER_OK : 0) |
            (reading.valid ? 0 : ADV_FLAG_READING_INVALID);
  mfg[17] = (uint8_t)((uint16_t)tempQ4 & 0xFF);
  mfg[18] = (uint8_t)(((uint16_t)tempQ4 >> 8) & 0xFF);
  mfg[19] = (uint8_t)(cm & 0xFF);
  mfg[20] = (uint8_t)((cm >> 8) & 0xFF);
  mfg[21] = (uint8_t)(cd & 0xFF);
  mfg[22] = (uint8_t)((cd >> 8) & 0xFF);
  mfg[23] = (uint8_t)((uint16_t)dieQ4 & 0xFF);
  mfg[24] = (uint8_t)(((uint16_t)dieQ4 >> 8) & 0xFF);
  mfg[25] = ADV_FLAG2_LAYOUT_B | (dieTempOk ? ADV_FLAG2_DIE_TEMP_OK : 0) |
            (longWindow ? 0 : ADV_FLAG2_SHORT_BURST) |
            (dcdcEnabled() ? ADV_FLAG2_DCDC_ON : 0) |
            (reading.valid && reading.warm ? ADV_FLAG2_FIT_WARM : 0) |
            (burnActive() ? ADV_FLAG2_BURN : 0) |
            (g_powerLevel != PowerLevel::Full ? ADV_FLAG2_SAVER : 0) |
            (g_cpCalibrated ? 0 : ADV_FLAG2_CP_DEFAULT);

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  bool ok = true;
  ok &= Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  ok &= Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg, sizeof(mfg));
  Bluefruit.Advertising.restartOnDisconnect(false);
  if (!longWindow)
  {
    // Non-connectable and non-scannable: there must be no scan response for this type, and the
    // radio does not listen after each transmission.
    if (!ok) return false;
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.setInterval(SHORT_ADV_INTERVAL_UNITS, SHORT_ADV_INTERVAL_UNITS);
    Bluefruit.Advertising.setFastTimeout(SHORT_ADV_MS / 1000);
    return Bluefruit.Advertising.start(SHORT_ADV_MS / 1000);
  }
  // The sweep only on the wake that measured it; see SWEEP_RSP_LEN.
  if (fresh && sweepSendable())
  {
    uint8_t sweep[SWEEP_RSP_LEN];
    packSweep(sweep);
    ok &= Bluefruit.ScanResponse.addData(BLE_GAP_AD_TYPE_SERVICE_DATA, sweep, sizeof(sweep));
  }
  else
  {
    ok &= Bluefruit.ScanResponse.addName();
  }
  // A partial packet is useless as telemetry.
  if (!ok) return false;
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.setInterval(ADV_FAST_INTERVAL_UNITS, ADV_SLOW_INTERVAL_UNITS);
  Bluefruit.Advertising.setFastTimeout(ADV_FAST_TIMEOUT_SECONDS);
  return Bluefruit.Advertising.start(OTA_ADV_HARD_TIMEOUT_SECONDS);
}

// Keeps the connectable window open, then holds a central that connected long enough to write the
// DFU jump. Same sequence as MSV3 production's loop(); see the comments there for each step.
static void runAdvertisingWindow()
{
  const uint32_t listenMs = g_otaServiceReady ? OTA_ADV_LISTEN_MS : OTA_UNAVAILABLE_ADV_MS;
  const uint32_t advStart = millis();
  uint16_t connectionHandle = BLE_CONN_HANDLE_INVALID;
  while ((millis() - advStart) < listenMs)
  {
    feedWatchdog();
    const uint16_t latchedHandle = g_otaConnectionHandle;
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

  // Stop first, then make one final connection check: after stop() no new connection can begin.
  if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
  if (connectionHandle == BLE_CONN_HANDLE_INVALID)
  {
    delay(1);  // give Bluefruit's higher-priority event task one scheduling point
    const uint16_t latchedHandle = g_otaConnectionHandle;
    if (latchedHandle != BLE_CONN_HANDLE_INVALID && Bluefruit.connected(latchedHandle))
      connectionHandle = latchedHandle;
    else if (Bluefruit.connected())
      connectionHandle = Bluefruit.connHandle();
  }
  if (connectionHandle == BLE_CONN_HANDLE_INVALID) return;

  const uint32_t holdStart = millis();
  while (Bluefruit.connected(connectionHandle) && (millis() - holdStart) < OTA_CONNECTED_HOLD_MS)
  {
    feedWatchdog();
    if (bledfu.dfuResetPending())
    {
      delayFeeding(DFU_RESPONSE_SETTLE_MS);
      bledfu.completeDfuReset();  // does not return on success
      break;
    }
    delay(50);
  }

  // An idle peer left connected keeps the radio running through every later "sleep".
  if (Bluefruit.connected(connectionHandle))
  {
    Bluefruit.disconnect(connectionHandle);
    const uint32_t disconnectStart = millis();
    while (Bluefruit.connected(connectionHandle) && millis() - disconnectStart < 500)
    {
      feedWatchdog();
      delay(10);
    }
  }
}

// ------------------------------------------------------------------------------- setup/loop ---
void setup()
{
  cycleCounterEnable();

  // Crystal/boot guard, all before the SoftDevice. See the block comment at the top.
  uint8_t count = 0;
  if (!readRetained(count) || !resetCarriesBootCount(readResetReason())) count = 0;
  burnBegin();
  if (count >= BOOT_ESCAPE_LIMIT) escapeToDfuPreSoftDevice(3);
  writeRetained(static_cast<uint8_t>(count + 1));

  const uint32_t t0 = DWT->CYCCNT;
  const uint32_t limit = LFCLK_START_TIMEOUT_MS * (SystemCoreClock / 1000UL);
  while (!lfclkOnXtal())
  {
    feedWatchdog();  // only matters if a WDT was inherited through a soft reset
    if ((DWT->CYCCNT - t0) > limit) escapeToDfuPreSoftDevice(5);
  }
  startWatchdog();

  pinOutput(PIN_DRV, false);
  pinOutput(PIN_VBAT_DIV_EN, false);
  pinOutput(PIN_TEMP_READ, false);
  setChopEnabled(false);
  setSwap(SwapState::Open);
  NRF_P1->DIRSET = (1UL << PIN_LED_P1);
  ledWrite(false);

  Bluefruit.autoConnLed(false);
  // A dead crystal stalls inside begin() and the watchdog takes it. A begin() that returns false
  // gets the same treatment on purpose: with no radio this board cannot be reached at all, so let
  // the watchdog reset it and the boot counter decide.
  if (!Bluefruit.begin())
  {
    for (;;) {}
  }
  Bluefruit.setTxPower(8);
  Bluefruit.setName(DEVICE_NAME);

  // LFCLK after SoftDevice enable is the source that matters. Off the crystal here means the
  // SoftDevice could not hold it: fail closed into DFU rather than run on a clock nobody chose.
  if (!lfclkOnXtal())
  {
    writeRetained(0);
    armDfuAndReset();
  }

  uint32_t ppiEnabled = 0;
  g_feResourcesFree = sd_ppi_channel_enable_get(&ppiEnabled) == NRF_SUCCESS &&
                      (ppiEnabled & PPI_MASK) == 0 &&
                      (NRF_GPIOTE->CONFIG[GPIOTE_CH_DRV] & GPIOTE_CONFIG_MODE_Msk) == 0 &&
                      (NRF_GPIOTE->CONFIG[GPIOTE_CH_CHOP] & GPIOTE_CONFIG_MODE_Msk) == 0;
  feedWatchdog();

  bledis.setManufacturer("Treebeard");
  bledis.setModel("MSV4");
  bledis.setFirmwareRev(FIRMWARE_BUILD_ID);
  bledis.setSoftwareRev(MSV4_STRINGIFY(MSV4_APP_VERSION));
  bledis.begin();
  // Added while DIS is still the last service begun, so it lands in DIS. Cp is no longer
  // calibrated here: see calibrateCpAfterOpenReading(). The first wake's reading comes before its
  // advertising window, so a client that connects in that window already reads the outcome.
  g_hardwareRevChar.setProperties(CHR_PROPS_READ);
  g_hardwareRevChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_hardwareRevChar.setMaxLen(sizeof(g_hardwareRev) - 1);
  g_hardwareRevReady = g_hardwareRevChar.begin() == ERROR_NONE;
  publishCp();
  // Same service, same reason for the order. "not checked" until the first measurement wake.
  g_maskChar.setProperties(CHR_PROPS_READ);
  g_maskChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_maskChar.setMaxLen(sizeof(g_maskText) - 1);
  g_maskCharReady = g_maskChar.begin() == ERROR_NONE;
  if (g_maskCharReady) g_maskChar.write(g_maskText, strlen(g_maskText));
  // And the ADC calibration's outcome. "not checked" until the first wake has calibrated.
  g_adcCalChar.setProperties(CHR_PROPS_READ);
  g_adcCalChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_adcCalChar.setMaxLen(sizeof(g_adcCalText) - 1);
  g_adcCalCharReady = g_adcCalChar.begin() == ERROR_NONE;
  if (g_adcCalCharReady) g_adcCalChar.write(g_adcCalText, strlen(g_adcCalText));
  // And the calibration experiment's counts. "not run" until the first wake has calibrated.
  g_calExpChar.setProperties(CHR_PROPS_READ);
  g_calExpChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  g_calExpChar.setMaxLen(sizeof(g_calExpText) - 1);
  g_calExpCharReady = g_calExpChar.begin() == ERROR_NONE;
  if (g_calExpCharReady) g_calExpChar.write(g_calExpText, strlen(g_calExpText));

  g_otaServiceReady = bledfu.begin() == ERROR_NONE;
  Bluefruit.Periph.setConnectCallback(otaConnectCallback);
  Bluefruit.Periph.setDisconnectCallback(otaDisconnectCallback);

  // One blip so a person holding the board can see the application started. (3 and 5 slow blinks
  // before a reset are the boot-counter and dead-crystal DFU escapes.)
  ledWrite(true);
  delay(40);
  ledWrite(false);

  g_lastAdvertOkMs = millis();
}

void loop()
{
  // No feed on a stalled radio: see ADVERT_STALL_RESET_MS.
  if (millis() - g_lastAdvertOkMs > ADVERT_STALL_RESET_MS)
  {
    for (;;) {}
  }
  feedWatchdog();
  cycleCounterEnable();
  const uint32_t cycleStartMs = millis();

  // Fresh ADC offset first, checked against a zero (see saadcCalibrateChecked()), then the
  // battery, before the front end or the radio loads the rail.
  saadcCalibrateChecked();
  float vbatMv = 0;
  const bool vbatOk = measureVbatMv(vbatMv) && vbatMv >= 2500.0f && vbatMv <= 5000.0f;
  burnJudge(vbatOk, vbatMv);
  saverJudge(vbatOk, vbatMv);
  float tempC = 0;
  const bool tempOk = measureTemp(tempC) && tempC >= -40.0f && tempC <= 85.0f;
  // Taken at the start of the wake, before the sweep and the radio warm the die.
  int16_t dieTempQ4 = 0;
  const bool dieTempOk = measureDieTempQ4(dieTempQ4);

  // The battery saver sets how far apart measurements are. A level that allows them closer is
  // taken up on this wake instead of after the longer wait already counted out.
  const uint8_t moistureEvery = saverMoistureEveryWakes();
  if (g_wakesUntilMoisture > moistureEvery - 1) g_wakesUntilMoisture = moistureEvery - 1;

  // The full connectable window goes with every measurement attempt and with the first few wakes
  // after a boot; see SHORT_ADV_MS.
  const bool longWindow = g_wakesUntilMoisture == 0 || g_wakesSinceBoot < LONG_WINDOW_BOOT_WAKES;
  if (g_wakesSinceBoot < 255) g_wakesSinceBoot++;
  if (g_wakesSinceMaskCheck < MASK_CHECK_EVERY_WAKES) g_wakesSinceMaskCheck++;
  bool fresh = false;
  if (g_wakesUntilMoisture == 0)
  {
    g_reading = takeMoistureReading();
    fresh = g_reading.valid;
    // Announce only after the analog result is complete. The LED shares its rail with the front
    // end; putting these edges before the sweep made 262165's fitted Rsoil and Cm noisy.
    ledAnnounceMeasurement();
    // A failed sweep is retried on the next wake rather than left standing for the whole interval.
    g_wakesUntilMoisture = g_reading.valid ? moistureEvery - 1 : 0;
    // After the sweep, so the DC step cannot be in the reading it follows. Once a day.
    if (maskCheckDue()) checkMask();
  }
  else
  {
    g_wakesUntilMoisture--;
  }
  allSafe();
  feedWatchdog();

  // Fails OPEN on an unusable battery reading, unlike MSV3: there is no USB to fall back on, so a
  // broken divider must not be what makes this board impossible to update.
  const bool otaPowerOk = !vbatOk || vbatMv >= (float)OTA_MIN_BATTERY_MV;
  bledfu.setDfuAllowed(otaPowerOk);

  g_advertCount++;
  // There cannot be a legitimate existing connection here: every one is disconnected below before
  // the probe returns to its sleep cadence.
  g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;
  if (startAdv(g_reading, fresh, vbatOk, vbatMv, tempOk, tempC, dieTempOk, dieTempQ4, otaPowerOk,
               longWindow))
  {
    if (longWindow)
    {
      runAdvertisingWindow();
    }
    else
    {
      // Nothing can connect, so there is nothing to poll for: sleep through the burst.
      sleepFeeding(SHORT_ADV_MS);
      if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
    }
    g_lastAdvertOkMs = millis();
    bootGuardNoteHealthyAdvert();
  }

  // Once per boot, with the first wake's radio window behind it; see g_dcdcRequested.
  if (!g_dcdcRequested && !burnHoldsLinearRegulator())
  {
    g_dcdcRequested = true;
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
  }

  cycleCounterDisable();
  burnUntilRest(cycleStartMs);
  const uint32_t awakeMs = millis() - cycleStartMs;
  sleepFeeding(awakeMs < WAKE_PERIOD_MS ? (WAKE_PERIOD_MS - awakeMs) : 1);
}
