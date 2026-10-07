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
const uint32_t WAKE_PERIOD_MS = 60000;
const uint8_t MOISTURE_EVERY_N_WAKES = 10;

// The 3V3 rail only exists while the BQ25504 asserts VBAT_OK (3.369 V falling), so a running MCU
// already implies some charge. This adds margin for the minutes a BLE DFU spends in the bootloader.
// MSV3's 3.80 V would close the OTA path over most of this board's 3.37-3.89 V operating window,
// and MSV4 has no USB to open it another way.
const uint16_t OTA_MIN_BATTERY_MV = 3500;

// If no advertising cycle has completed for this long, stop feeding the watchdog. A probe that is
// running but silent is unreachable, and this board has no USB; a reset at least hands the
// decision to the boot guard.
const uint32_t ADVERT_STALL_RESET_MS = 600000;

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
constexpr uint32_t FE_SETTLE_MS = 5;        // after a phase change: 50 x the 100 us filter tau
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
ResetSafeBLEDfu bledfu;
static bool g_otaServiceReady = false;
static volatile uint16_t g_otaConnectionHandle = BLE_CONN_HANDLE_INVALID;

// Counts ADVERTISEMENTS. The receiver calls this field "wake" and uses it to drop repeated frames
// of one advertising window, so it must advance once per window and never otherwise.
static uint32_t g_advertCount = 0;
static uint32_t g_lastAdvertOkMs = 0;

static MoistureReading g_reading = {false, 0, 0, 0};
static uint8_t g_wakesUntilMoisture = 0;   // 0 = measure on this wake

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
static bool saadcReadMv(uint8_t pselp, uint8_t osLog2, uint32_t tacqUs, float& mv)
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

  int32_t code = g_saadcResult;
  if (code < 0) code = 0;  // single-ended can report small negative codes near 0 V
  mv = code * 3600.0f / 4096.0f;
  return true;
}

static inline bool readAinMv(uint8_t ain, uint8_t osLog2, uint32_t tacqUs, float& mv)
{
  return saadcReadMv(static_cast<uint8_t>(ain + 1), osLog2, tacqUs, mv);
}

static bool saadcCalibrate()
{
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
constexpr double SPEC_RS = 1000.0;       // R15
// Replaced at boot by calibrateCp(). Board 1 measured 8.8 pF through the console on 2026-10-01.
constexpr double SPEC_CP_DEFAULT = 12e-12;
constexpr double SPEC_CP_MIN = 2e-12;
constexpr double SPEC_CP_MAX = 25.5e-12;  // the wire field is one byte of 0.1 pF
static double g_specCp = SPEC_CP_DEFAULT;
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

static cplx specNodeH(double w, const double* p, double cp)
{
  // p = {Cm, Gs, Cd}
  const cplx j(0.0, 1.0);
  const cplx zm = 1.0 / (j * w * p[0]);
  const cplx ye = j * w * p[2] + 1.0 / (zm + 1.0 / p[1]);
  return 1.0 / (1.0 + SPEC_RS * (ye + j * w * cp));
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

// Electrodes floating (U3 /OE high), so Ye = 0 and the sweep sees only the SENSE node's own
// parasitics. Runs once per boot, whatever the blade is standing in, and keeps the result in RAM.
// A failed sweep or an out-of-range fit leaves the default in place; the packet reports which Cp
// was used. NOT YET MEASURED: whether Cp taken with the blade in soil differs from Cp in air.
static void calibrateCp()
{
  setSwap(SwapState::Open);
  setChopEnabled(true);
  SpecSet s;
  const bool ok = specSweep(s);
  allSafe();
  if (!ok) return;
  // Fit Cp alone: Cm tiny makes zm huge, so the series branch vanishes, Ye = jw*Cd, and Cd plays
  // Cp's role.
  double p[3] = {1e-15, 1e-12, g_specCp}, sig[3];
  const bool freeP[3] = {false, false, true};
  specFit(s, p, freeP, 0.0, sig);
  if (p[2] >= SPEC_CP_MIN && p[2] <= SPEC_CP_MAX) g_specCp = p[2];
}

// Sweeps both orientations, averages the complex transfers, and fits {Cm, Gs, Cd}.
static bool runSpectrumFit(double* best, double& bestRms)
{
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

  // Several starting points for Gs across four decades; keep the best fit.
  const bool freeP[3] = {true, true, true};
  bestRms = 1e30;
  static const double gStart[] = {1e-5, 1e-4, 1e-3, 1e-2};
  for (double g0 : gStart)
  {
    double p[3] = {500e-12, g0, 5e-12}, sig[3];
    const double rms = specFit(acc, p, freeP, g_specCp, sig);
    if (rms < bestRms)
    {
      bestRms = rms;
      memcpy(best, p, 3 * sizeof(double));
    }
  }
  return true;
}

// Wire limits. Rsoil saturates at 400 kohm: with a 1 kohm source resistor the divider cannot tell
// larger values apart, and in air the fit returns 1e14 ohm or more. 4,000,000 also sits below the
// 5,000,000-count floor the receiver applies to an MSV3 FDC count, so a receiver that does not yet
// know this packet treats it as a failed reading instead of storing ohms as counts.
constexpr uint32_t RSOIL_DECIOHM_MAX = 4000000UL;
// The fit occasionally collapses to ~0 ohm with an enormous uncertainty when there is nothing to
// conduct. That is "no fit", not "very wet".
constexpr double RSOIL_MIN_VALID_OHM = 1.0;

static MoistureReading takeMoistureReading()
{
  MoistureReading out = {false, 0, 0, 0};
  double best[3] = {}, bestRms = 0;
  const bool swept = runSpectrumFit(best, bestRms);
  allSafe();
  if (!swept) return out;

  const double cm = best[0], gs = best[1];
  if (!(gs > 0.0) || !(cm > 0.0) || !isfinite(bestRms)) return out;
  const double rsoil = 1.0 / gs;
  if (!isfinite(rsoil) || rsoil < RSOIL_MIN_VALID_OHM) return out;

  const double rDeci = rsoil * 10.0 + 0.5;
  out.rsoilDeciOhm = rDeci >= (double)RSOIL_DECIOHM_MAX ? RSOIL_DECIOHM_MAX : (uint32_t)rDeci;
  const double cmDeci = cm * 1e13 + 0.5;
  out.cmDeciPf = cmDeci >= 65535.0 ? 65535 : (uint16_t)cmDeci;
  const double rmsE4 = bestRms * 1e4 + 0.5;
  out.rmsE4 = rmsE4 >= 255.0 ? 255 : (uint8_t)rmsE4;
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
// [10-13] Rsoil, uint32 LE, units of 0.1 ohm, NOT temperature-corrected. Saturates at
//         RSOIL_DECIOHM_MAX. Zero when bit5 is set.
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
// [19-20] Cm, uint16 LE, units of 0.1 pF, saturating. Contact quality, not moisture.
// [21]    Cp the fit used, units of 0.1 pF. 120 means the boot calibration fell back to default.
// [22]    fit RMS residual, units of 1e-4 of drive, saturating
// [23-24] nRF52840 die temperature, signed int16 LE, quarter-degrees C (0 when flags2 bit0 is
//         clear). The board end of the probe, NOT the soil: [17-18] is the soil.
// [25]    flags2: bit0 = die temperature valid; bits 1-7 = 0
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

constexpr size_t MFG_LEN = 26;
static_assert(3 /* Flags */ + 2 /* manufacturer-data header */ + MFG_LEN <= 31,
              "advertising payload exceeds the 31-byte legacy PDU limit");

// Fast for the first second, then 250 ms for the rest of the window: the schedule MSV3 arrived at
// after a single 500 ms check kept losing Windows OTA clients.
const uint32_t OTA_ADV_LISTEN_MS = 5000;
const uint16_t ADV_FAST_INTERVAL_UNITS = 32;   // 20 ms
const uint16_t ADV_SLOW_INTERVAL_UNITS = 400;  // 250 ms
const uint16_t ADV_FAST_TIMEOUT_SECONDS = 1;
const uint16_t OTA_ADV_HARD_TIMEOUT_SECONDS = OTA_ADV_LISTEN_MS / 1000;
const uint32_t OTA_UNAVAILABLE_ADV_MS = 500;
// Covers connect + service discovery + one write to the DFU control characteristic. The transfer
// itself happens in the bootloader.
const uint32_t OTA_CONNECTED_HOLD_MS = 30000;
// Let the DFU authorize response reach the client before the reset tears the radio down.
const uint32_t DFU_RESPONSE_SETTLE_MS = 150;

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
                     bool tempOk, float tempC, bool dieTempOk, int16_t dieTempQ4, bool otaPowerOk)
{
  const uint16_t batteryMv = vbatOk ? (uint16_t)(vbatMv + 0.5f) : 0;
  const int16_t tempQ4 = tempOk ? (int16_t)lroundf(tempC * 4.0f) : 0;
  const int16_t dieQ4 = dieTempOk ? dieTempQ4 : 0;
  const uint32_t rsoil = reading.valid ? reading.rsoilDeciOhm : 0;
  const uint16_t cm = reading.valid ? reading.cmDeciPf : 0;
  const double cpDeci = g_specCp * 1e13 + 0.5;

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
  mfg[13] = (uint8_t)((rsoil >> 24) & 0xFF);
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
  mfg[21] = cpDeci >= 255.0 ? 255 : (uint8_t)cpDeci;
  mfg[22] = reading.valid ? reading.rmsE4 : 0;
  mfg[23] = (uint8_t)((uint16_t)dieQ4 & 0xFF);
  mfg[24] = (uint8_t)(((uint16_t)dieQ4 >> 8) & 0xFF);
  mfg[25] = dieTempOk ? ADV_FLAG2_DIE_TEMP_OK : 0;

  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  bool ok = true;
  ok &= Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  ok &= Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, mfg, sizeof(mfg));
  ok &= Bluefruit.ScanResponse.addName();
  // A partial packet is useless as telemetry and, without the name, invisible to an OTA client.
  if (!ok) return false;
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(ADV_FAST_INTERVAL_UNITS, ADV_SLOW_INTERVAL_UNITS);
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

  bledis.setManufacturer("Treebeard");
  bledis.setModel("MSV4");
  bledis.setFirmwareRev(FIRMWARE_BUILD_ID);
  bledis.setSoftwareRev(MSV4_STRINGIFY(MSV4_APP_VERSION));
  bledis.begin();

  g_otaServiceReady = bledfu.begin() == ERROR_NONE;
  Bluefruit.Periph.setConnectCallback(otaConnectCallback);
  Bluefruit.Periph.setDisconnectCallback(otaDisconnectCallback);
  Bluefruit.Advertising.setFastTimeout(ADV_FAST_TIMEOUT_SECONDS);

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

  saadcCalibrate();
  calibrateCp();
  feedWatchdog();

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

  // Battery first, before the front end or the radio loads the rail.
  float vbatMv = 0;
  const bool vbatOk = measureVbatMv(vbatMv) && vbatMv >= 2500.0f && vbatMv <= 5000.0f;
  float tempC = 0;
  const bool tempOk = measureTemp(tempC) && tempC >= -40.0f && tempC <= 85.0f;
  // Taken at the start of the wake, before the sweep and the radio warm the die.
  int16_t dieTempQ4 = 0;
  const bool dieTempOk = measureDieTempQ4(dieTempQ4);

  bool fresh = false;
  if (g_wakesUntilMoisture == 0)
  {
    g_reading = takeMoistureReading();
    fresh = g_reading.valid;
    // A failed sweep is retried on the next wake rather than left standing for the whole interval.
    g_wakesUntilMoisture = g_reading.valid ? MOISTURE_EVERY_N_WAKES - 1 : 0;
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
  if (startAdv(g_reading, fresh, vbatOk, vbatMv, tempOk, tempC, dieTempOk, dieTempQ4, otaPowerOk))
  {
    runAdvertisingWindow();
    g_lastAdvertOkMs = millis();
    bootGuardNoteHealthyAdvert();
  }

  cycleCounterDisable();
  const uint32_t awakeMs = millis() - cycleStartMs;
  sleepFeeding(awakeMs < WAKE_PERIOD_MS ? (WAKE_PERIOD_MS - awakeMs) : 1);
}
