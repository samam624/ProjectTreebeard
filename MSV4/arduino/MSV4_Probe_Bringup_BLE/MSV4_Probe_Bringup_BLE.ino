// MSV4 probe bring-up console over BLE (Nordic UART Service).
//
// MSV4 has no USB, so the MSV3 USB-serial bring-up sketch has no equivalent transport. This image
// is the first application flashed after the bootloader: connect with
// MSV4/scripts/msv4_ble_console.py (or any NUS app such as nRF Toolbox / Bluefruit Connect), type
// `help`, and every command answers over the same link. A connection is required -- nothing runs
// on its own except advertising.
//
// BUILD ONLY WITH MSV4/scripts/arduino_msv4_probe.ps1. It selects the tracked LFXO variant
// (MSV4/arduino/variants/msv4_e73) and the shared RAM-reserving linker script, and proves both from
// the verbose log and link map. A plain IDE build uses the installed pca10056 variant and fails the
// MSV4_VARIANT check below.
//
// Hardware reference: MSV4/hardware/reference/PIN_NET_REFERENCE.md and COMPONENT_INDEX.md.
// Front-end design: MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md.

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

#define MSV4_STRINGIFY_(x) #x
#define MSV4_STRINGIFY(x) MSV4_STRINGIFY_(x)
#ifdef MSV4_BUILD_TAG
#define MSV4_BUILD_ID "MSV4-BUILD:bringup-" MSV4_STRINGIFY(MSV4_BUILD_TAG)
#else
#define MSV4_BUILD_ID "MSV4-BUILD:bringup"
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

// SAADC analog inputs (AINn -> PSELP value n+1).
// RE-PINNED 2026-09-18 to match the layout (IPC-2581 export 2026-09-18 20:11), which moved three
// analog nets after the rev3 netlist: SENSE_OUT P0.03 -> P0.30, DRV_OUT P0.04 -> P0.31,
// TEMP P0.05 -> P0.03. The layout is what gets fabricated; the rev3 netlist is stale here.
constexpr uint8_t AIN_VBAT = 0;      // P0.02 VBAT_DIV, 50k source: needs TACQ >= 10 us (unchanged)
constexpr uint8_t AIN_TEMP = 1;      // P0.03 TEMP, NTC divider midpoint (C8 = RF bypass at blade top)
constexpr uint8_t AIN_SENSE = 6;     // P0.30 SENSE_OUT (after chopper, R18 100k / C9 1nF)
constexpr uint8_t AIN_DRVMON = 7;    // P0.31 DRV_OUT drift monitor (R19 100k / C16 1nF)
constexpr uint8_t PSEL_VDD = 9;      // SAADC_CH_PSELP_PSELP_VDD

// Declared ahead of every function: the Arduino builder inserts auto-generated prototypes above
// the first function definition, and those prototypes name these types.
enum class SwapState : uint8_t { Open, A, B };
enum class LedMode : uint8_t { Off, On, Blink };
struct PhaseFit
{
  float p, c, s, residualRms;
};
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

// One frequency of a `spectrum` sweep: timer period N, phases sampled M, and the measured transfer
// H = X1(SENSE) / X1(DRV_OUT) (bin-1 of the chopper-phase sweep, same switch path on both).
constexpr int SPEC_MAX = 12;
struct SpecSet
{
  int count = 0;
  uint16_t n[SPEC_MAX];
  uint16_t m[SPEC_MAX];
  cplx h[SPEC_MAX];
};
// Free helpers come after every type the auto-generated prototypes mention (see enum note above).
static inline cplx operator*(double k, const cplx& z) { return cplx(k * z.re, k * z.im); }
static inline cplx operator+(double k, const cplx& z) { return cplx(k + z.re, z.im); }
static inline cplx operator/(double k, const cplx& z) { return cplx(k, 0) / z; }
static inline cplx cconj(const cplx& z) { return cplx(z.re, -z.im); }
static inline double cabs2(const cplx& z) { return sqrt(z.re * z.re + z.im * z.im); }
static inline double carg(const cplx& z) { return atan2(z.im, z.re); }
static inline cplx cpolar(double r, double th) { return cplx(r * cos(th), r * sin(th)); }

// --------------------------------------------------------------------------- front end -------
// One TIMER at 16 MHz, period N ticks (CC5 + CLEAR short). Four compares toggle two GPIOTE task
// channels through four PPI channels:
//   DRV     : toggle at N/2 (CC0) and N (CC5, the wrap)            -> high for ticks [0, N/2)
//   chopper : toggle at a (CC1) and b (CC2), a = k, b = k + N/2    -> high for ticks [k, k+N/2)
// Any edge that lands on tick 0 is scheduled at N instead (the wrap), so no compare is ever 0 --
// whether COMPARE fires on a value of 0 right after CLEAR is not worth relying on. Each edge has
// its own CC register even when two edges share a tick, so no PPI fork is needed.
// TIMER0 belongs to the SoftDevice; TIMER3 is unused by this core and has six CC registers.
// PPI channels 0-16 are the application's with S140 enabled; GPIOTE 6/7 are the top two, clear of
// attachInterrupt()'s lowest-free-first allocation (and nothing here calls attachInterrupt).
NRF_TIMER_Type* const FE_TIMER = NRF_TIMER3;
constexpr uint8_t GPIOTE_CH_DRV = 6;
constexpr uint8_t GPIOTE_CH_CHOP = 7;
constexpr uint8_t PPI_CH_BASE = 8;   // uses 8, 9, 10, 11
constexpr uint32_t PPI_MASK = 0xFUL << PPI_CH_BASE;
constexpr uint32_t FE_CLOCK_HZ = 16000000UL;
// 4 MHz (N = 4) is the fastest this 16 MHz TIMER scheme can produce with exact quadrature. Allowed
// since 2026-09-19: the field solve (MSV4/docs/MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md) shows soil
// conductivity dominating permittivity 24x at 1 MHz but 6x at 4 MHz, and a 10 um soil gap costing
// 42% of |Y| at 1 MHz but 25% at 4 MHz. FSUSB42 (t_on - t_off ~1 ns typ) is 1.4 deg at 4 MHz, which
// the DRV_OUT monitor channel calibrates; characterise it before trusting 4 MHz data.
constexpr uint16_t FE_N_MIN = 4;
constexpr uint16_t FE_N_MAX = 16000;   // 1 kHz
constexpr uint16_t FE_N_DEFAULT = 16;  // 1.000 MHz operating point

struct FrontEndState
{
  bool running = false;
  uint16_t n = FE_N_DEFAULT;
  uint16_t k = 0;
  bool hfxoRequested = false;
};
static FrontEndState g_fe;
// Set once in setup(): the SoftDevice accepting sd_ppi_channel_assign() only proves a channel is
// application-accessible, not that nothing else in the image already uses it.
static bool g_feResourcesFree = false;
static uint32_t g_settleMs = 5;       // after a phase change: 50 x the 100 us filter tau
static uint8_t g_oversampleLog2 = 6;  // 64x hardware oversample for front-end reads

// --------------------------------------------------------------------- boot/crystal guard ----
// A hand-soldered crystal can fail to start. With USE_LFXO, a dead crystal would stall FreeRTOS's
// RTC1 tick and SoftDevice enable, and with no USB and hand-held SWD that is a brick. Facts this
// relies on, checked against source on 2026-09-18:
// - The bootloader's board_teardown() stops RTC1 and LFCLK before jumping, and the core's wiring.c
//   only writes LFCLKSRC=Xtal and triggers LFCLKSTART -- it never waits. So setup() is always
//   reached, with the crystal starting from scratch.
// - The bootloader feeds an already-running WDT in its DFU loop (bootloader.c wait_for_events), so
//   a watchdog inherited through a soft reset cannot kill a BLE DFU transfer.
// - Bootloader 1.0.3's retained-RAM patch only RESERVES 0x2003FFF8..FFFF; it never reads it.
// So, in setup():
// 1. Busy-wait (cycle counter, not delay()) up to 2 s for LFCLK running AND sourced from XTAL.
//    Failure jumps straight to the bootloader's BLE DFU (which runs on LFRC). The watchdog is not
//    started until this passes: a started WDT forces the RC oscillator on for itself, which could
//    muddy what LFCLKSTAT reports.
// 2. After SoftDevice enable, LFCLK must still be on XTAL, or the board escapes to DFU the same way.
// 3. Backstop for a stall between those points: a boot counter in the reserved RAM record. Each
//    entry increments it; reaching advertising clears it. On the entry that finds count >= 3 --
//    i.e. the 4th consecutive boot after three that never reached advertising -- the board escapes
//    to DFU. (">= 3 means the 4th entry" is deliberate; do not "fix" the off-by-one.)
// The GPREGRET OTA entry has no timeout in this bootloader: an escaped board sits in DFU until an
// image arrives or it is power-cycled (which also clears the counter).
// This also covers an MSV3 board mis-flashed with this image (same bootloader device type today).
constexpr uint32_t WDT_TIMEOUT_MS = 8000;
constexpr uint32_t LFCLK_START_TIMEOUT_MS = 2000;
constexpr uint8_t BOOT_ESCAPE_LIMIT = 3;
constexpr uint32_t RETAINED_MAGIC = 0x4D344200UL;       // "M4B" + count in the low byte
constexpr uint32_t RETAINED_MAGIC_MASK = 0xFFFFFF00UL;
constexpr uint8_t DFU_MAGIC_OTA_RESET = 0xA8;

struct RetainedRecord
{
  uint32_t encoded;
  uint32_t inverse;
};
static_assert(sizeof(RetainedRecord) == 8, "retained record must fill the reserved 8 bytes");
static volatile RetainedRecord g_retained
    __attribute__((section(".msv3_boot_guard_retained"), used, aligned(4)));

static uint8_t g_bootCount = 0;
static uint32_t g_lfclkStatAtEntry = 0;
static uint32_t g_lfclkStartMs = 0;

// ------------------------------------------------------------------------------------ BLE ----
BLEUart bleuart;
BLEDis bledis;
ResetSafeBLEDfu bledfu;
static char g_deviceName[20];

constexpr size_t CMD_MAX = 96;
static char g_rxLine[CMD_MAX + 1];
static size_t g_rxLen = 0;
static bool g_rxOverflow = false;
static char g_pendingCmd[CMD_MAX + 1];
static volatile bool g_cmdPending = false;
static volatile bool g_cancel = false;

static LedMode g_ledMode = LedMode::Blink;

// ------------------------------------------------------------------------- low-level utils ---
static void cycleCounterEnable()
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
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

static bool lfclkRunning() { return (NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) != 0; }

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

static const char* lfclkSourceName(uint32_t stat)
{
  switch ((stat & CLOCK_LFCLKSTAT_SRC_Msk) >> CLOCK_LFCLKSTAT_SRC_Pos)
  {
    case CLOCK_LFCLKSTAT_SRC_RC: return "RC";
    case CLOCK_LFCLKSTAT_SRC_Xtal: return "XTAL";
    case CLOCK_LFCLKSTAT_SRC_Synth: return "SYNTH";
    default: return "?";
  }
}

// ------------------------------------------------------------------------------ console out ---
static void out(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char* fmt, ...)
{
  if (!Bluefruit.connected()) return;
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t len = static_cast<size_t>(n) < sizeof(buf) - 2 ? static_cast<size_t>(n) : sizeof(buf) - 3;
  buf[len++] = '\n';
  buf[len] = '\0';
  bleuart.write(reinterpret_cast<const uint8_t*>(buf), len);
  feedWatchdog();
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

static SwapState g_swap = SwapState::Open;
static bool g_chopEnabled = false;

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
  g_swap = s;
}

static void setChopEnabled(bool on)
{
  if (on) pinOutput(PIN_READ_NOE, false);
  else pinRelease(PIN_READ_NOE);
  g_chopEnabled = on;
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

// Returns an error string, or nullptr on success.
static const char* frontEndStart(uint16_t n, uint16_t k)
{
  if (n < FE_N_MIN || n > FE_N_MAX || (n & 1)) return "N must be even, 4..16000";
  if (k >= n) return "k must be 0..N-1";
  if (!g_feResourcesFree) return "PPI 8-11 or GPIOTE 6-7 were already in use at boot; refusing";

  frontEndStop();
  if (!requestHfxo())
  {
    releaseHfxo();
    return "HFXO did not start (refusing to run the excitation on HFINT)";
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
  if (rc != NRF_SUCCESS)
  {
    frontEndStop();
    return "sd_ppi_channel_assign failed";
  }
  if (sd_ppi_channel_enable_set(PPI_MASK) != NRF_SUCCESS)
  {
    frontEndStop();
    return "sd_ppi_channel_enable_set failed";
  }

  FE_TIMER->TASKS_CLEAR = 1;
  FE_TIMER->TASKS_START = 1;
  g_fe.running = true;
  g_fe.n = n;
  g_fe.k = k;
  return nullptr;
}

// Isolate the analog path first (both /OE lines back to their pullups), then stop the clocks.
static void allSafe()
{
  setChopEnabled(false);
  setSwap(SwapState::Open);
  frontEndStop();
  pinOutput(PIN_VBAT_DIV_EN, false);
  pinOutput(PIN_TEMP_READ, false);
}

// ------------------------------------------------------------------------------ measurements --
static bool measureVbatMv(float& vbatMv, float& nodeMv)
{
  pinOutput(PIN_VBAT_DIV_EN, true);
  delay(2);  // C9 is DNP: nothing to charge, only Q1's gate
  const bool ok = readAinMv(AIN_VBAT, 4, 40, nodeMv);  // TACQ 40 us for the 50k source
  pinOutput(PIN_VBAT_DIV_EN, false);
  vbatMv = nodeMv * 2.0f;  // R5/R9 = 100k/100k
  return ok;
}

// NTC R17 (NCP15XH103F03RC, 10k, B25/50 = 3380 K) from TEMP to GND, R16 = 10k from TEMP_READ.
// Ratio against VDD read on the same pass, so the LDO's exact output cancels.
static bool measureTemp(float& tempC, float& rNtc, float& tempMv, float& vddMv)
{
  pinOutput(PIN_TEMP_READ, true);
  delay(1);  // C16 1 nF x 5k = 5 us
  bool ok = readAinMv(AIN_TEMP, 4, 10, tempMv);
  ok = ok && saadcReadMv(PSEL_VDD, 4, 10, vddMv);
  pinOutput(PIN_TEMP_READ, false);
  if (!ok || vddMv <= 0.0f) return false;
  const float ratio = tempMv / vddMv;
  if (ratio <= 0.001f || ratio >= 0.999f) return false;
  rNtc = 10000.0f * ratio / (1.0f - ratio);
  const float invT = 1.0f / 298.15f + logf(rNtc / 10000.0f) / 3380.0f;
  tempC = 1.0f / invT - 273.15f;
  return true;
}

static bool readFrontEnd(float& senseMv, float& drvMv)
{
  bool ok = readAinMv(AIN_SENSE, g_oversampleLog2, 10, senseMv);
  ok = ok && readAinMv(AIN_DRVMON, g_oversampleLog2, 10, drvMv);
  return ok;
}

// Least-squares fit of V(theta) = P + c*cos(theta) + s*sin(theta) over `count` equally spaced
// phases of an N grid. For equally spaced points covering the full circle the normal equations
// diagonalise: P = mean, c = 2/M sum V cos, s = 2/M sum V sin.
static PhaseFit fitPhases(const float* v, const uint16_t* ks, uint16_t m, uint16_t n)
{
  PhaseFit f{0, 0, 0, 0};
  for (uint16_t i = 0; i < m; i++)
  {
    const float th = 2.0f * PI * ks[i] / n;
    f.p += v[i];
    f.c += v[i] * cosf(th);
    f.s += v[i] * sinf(th);
  }
  f.p /= m;
  f.c *= 2.0f / m;
  f.s *= 2.0f / m;
  float ss = 0;
  for (uint16_t i = 0; i < m; i++)
  {
    const float th = 2.0f * PI * ks[i] / n;
    const float e = v[i] - (f.p + f.c * cosf(th) + f.s * sinf(th));
    ss += e * e;
  }
  f.residualRms = sqrtf(ss / m);
  return f;
}

static void reportFit(const char* label, const PhaseFit& f, bool withResidual)
{
  const float amp = sqrtf(f.c * f.c + f.s * f.s);
  const float phiDeg = atan2f(f.s, f.c) * 180.0f / PI;
  if (withResidual)
    out("%s P=%.2f I=%.2f Q=%.2f A=%.2f phi=%.1fdeg resid=%.3f mV", label, f.p, f.c, f.s, amp,
        phiDeg, f.residualRms);
  else
    out("%s P=%.2f I=%.2f Q=%.2f A=%.2f phi=%.1fdeg mV", label, f.p, f.c, f.s, amp, phiDeg);
}

// Runs the chopper through `m` phases of the N grid at the current swap state, reading both
// channels after g_settleMs at each. Leaves the front end stopped only if it was stopped before.
static void runPhaseSet(uint16_t n, uint16_t m, bool withResidual)
{
  if (g_swap == SwapState::Open) out("note: swap is OPEN (reference measurement)");
  if (!g_chopEnabled) out("note: chopper /OE is off -- enabling it for this measurement");
  const bool chopWas = g_chopEnabled;
  const bool wasRunning = g_fe.running;
  const uint16_t prevN = g_fe.n, prevK = g_fe.k;
  setChopEnabled(true);

  constexpr uint16_t MAX_PHASES = 64;
  if (m > MAX_PHASES) m = MAX_PHASES;
  float vs[MAX_PHASES], vd[MAX_PHASES];
  uint16_t ks[MAX_PHASES];
  g_cancel = false;
  for (uint16_t i = 0; i < m; i++)
  {
    ks[i] = static_cast<uint16_t>((static_cast<uint32_t>(i) * n) / m);
    const char* err = frontEndStart(n, ks[i]);
    if (err) { out("ERR %s", err); break; }
    delayFeeding(g_settleMs);
    if (!readFrontEnd(vs[i], vd[i])) { out("ERR SAADC timeout"); m = i; break; }
    if (withResidual) out("k=%u sense=%.2f drv=%.2f mV", ks[i], vs[i], vd[i]);
    if (g_cancel) { out("cancelled"); m = i + 1; break; }
  }
  if (m >= 3)
  {
    out("N=%u f=%lu Hz phases=%u settle=%lums os=%u", n, FE_CLOCK_HZ / n, m,
        static_cast<unsigned long>(g_settleMs), 1u << g_oversampleLog2);
    reportFit("sense", fitPhases(vs, ks, m, n), withResidual);
    reportFit("drv  ", fitPhases(vd, ks, m, n), withResidual);
  }

  if (wasRunning) frontEndStart(prevN, prevK);
  else frontEndStop();
  setChopEnabled(chopWas);
}

// ------------------------------------------------------------------------------- spectrum ----
// Multi-frequency impedance fit. Physics and numbers: MSV4/docs/MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md.
//
// Electrode model (A hot, B + guard grounded). THREE parameters, not four:
//     Ye(w) = jw*Cd + 1 / ( 1/(jw*Cm) + 1/Gs )
//   Cm = mask/interface series capacitance, Gs = soil conductance (ions),
//   Cd = direct board/fringe capacitance that never reaches soil.
//
// Cm IS THE SERIES PAIR OF BOTH ELECTRODES' MASKS, not one mask. The physical path is
//     plate A -> mask A -> soil -> mask B -> plate B
// so there are two interface capacitances, one at each plate. Series impedances add and
// commute, 1/(jw*CmA) + Rsoil + 1/(jw*CmB) = Rsoil + 1/(jw*Cm) with 1/Cm = 1/CmA + 1/CmB, so the
// two-mask topology is bit-identical to this one-mask form (verified: 0.0e+00 difference in |H|
// at every sweep frequency). No code changes; only the INTERPRETATION of the number does:
//   - A fitted Cm of ~671 pF means each plate's own mask is ~1342 pF.
//   - A gap under ONE plate drops Cm by 33 %; a gap under BOTH drops it by 50 %. The size of the
//     drop is a rough one-face/two-face discriminator.
//   - Which plate is faulty is NOT recoverable. The series pair is symmetric in CmA/CmB
//     (CmA 671/CmB 1342 and CmA 1342/CmB 671 give identical |H|), so the A/B swap cannot expose
//     mask asymmetry either. Do not read an A/B difference as "electrode A is the bad one".
// Divider node: H(w) = 1 / (1 + Rs*(Ye + jw*Cp)), Rs = R15, Cp = SENSE-node parasitics measured
// with `spectrum open`.
//
// Cs (soil permittivity) WAS a fourth parameter and was removed 2026-09-23. It was not merely
// unidentifiable -- keeping it made the whole fit ill-posed. Writing the 4-parameter admittance
// as a rational function of s = jw:
//     Ye(s) = s*[ Gs*(Cd+Cm) + s*(Cd*Cs + Cd*Cm + Cm*Cs) ] / [ Gs + s*(Cs+Cm) ]
// and dividing through by Gs leaves only THREE observable combinations --
//     (Cd+Cm),  (Cd*Cs + Cd*Cm + Cm*Cs)/Gs,  (Cs+Cm)/Gs
// -- for four unknowns, so an exact one-dimensional family of parameter sets fits any data
// equally well. Measured, not argued: {Cm 671.0 pF, Rsoil 355.0, Cs 14.90 pF, Cd 5.00 pF} and
// {Cm 656.6 pF, Rsoil 370.8, Cs 0.00017 pF, Cd 19.41 pF} reproduce the same nine-frequency sweep
// to 0 ppm in magnitude and 0 millidegrees in phase. Which one the LM returned depended on its
// starting seed, so the reported Rsoil moved several percent for reasons having nothing to do
// with soil. Fixing Cs = 0 removes exactly one parameter, leaving three unknowns and three
// observables, and the map is invertible -- Rsoil becomes uniquely determined and seed-stable.
//
// The cost is a small moisture-dependent bias (+1.2 % dry to +9.1 % saturated against a truth
// that really does have a Cs). That bias is a property of the lumped model, NOT of this change:
// the old 4-parameter fit carried the same bias, and at the wet end returned bit-identical
// numbers. Trend steps are preserved to ~3 %, so a per-pot calibration still works.
// Evidence and the regression test: MSV4/scripts/test_msv4_spectrum_fit.py.
//
// What the lock-in actually returns is not H(w). Drive and chopper are both 50 % square waves, so
// the chopper-phase sweep's first Fourier bin (sampled at M phases) is
//     X1 = sum over odd n with n = 1 (mod M) of  H(|n|w)^(n<0 ? conj) / n^2
// i.e. harmonics n = 1-M, 1+M, ... alias onto the fundamental. At M = 16 they are negligible; at
// 4 MHz only M = 4 phases exist and the 3rd/5th harmonics weigh 1/9 and 1/25. Dividing by the
// DRV_OUT channel (same switch, H = 1) normalises amplitude and switch phase, and the fit below
// models the aliasing exactly instead of pretending it is absent.
constexpr double SPEC_RS = 1000.0;       // R15
static double g_specCp = 12e-12;          // replaced by `spectrum open`
// N = 4 (4 MHz) dropped 2026-09-23 with Cs. It existed to chase permittivity, and it is the
// worst-conditioned point in the sweep: M = 4 is the fewest phases, so the aliasing correction
// leans hardest there (3rd harmonic weight 1/9 vs 1/81 at M = 8), and the chopper's 18 ns switch
// asymmetry contributes its largest phase error at the top of the band. Dropping it changes the
// recovered Rsoil by 0.0 % across a dry/moist/saturated sweep and leaves noise scatter unchanged
// (test_msv4_spectrum_fit.py). `drive 4` still reaches 4 MHz by hand.
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

// Measures X1(SENSE)/X1(DRV_OUT) at one frequency. False on SAADC timeout or cancel.
static bool specMeasure(uint16_t n, uint16_t m, cplx& h)
{
  cplx xs(0, 0), xd(0, 0);
  for (uint16_t i = 0; i < m; i++)
  {
    const uint16_t k = (uint16_t)((uint32_t)i * n / m);
    const char* err = frontEndStart(n, k);
    if (err) { out("ERR %s", err); return false; }
    delayFeeding(g_settleMs);
    float vs, vd;
    if (!readFrontEnd(vs, vd)) { out("ERR SAADC timeout"); return false; }
    const double th = 2.0 * M_PI * i / m;
    const cplx e = cpolar(1.0, -th);
    xs += (double)vs * e;
    xd += (double)vd * e;
    if (g_cancel) return false;
  }
  if (cabs2(xd) < 1e-6) { out("ERR DRV_OUT has no fundamental at N=%u", n); return false; }
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

// Levenberg-Marquardt on ln(parameters) for the free subset `mask` of {Cm, Gs, Cd}.
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

static void specPrintPoints(const SpecSet& s)
{
  for (int i = 0; i < s.count; i++)
  {
    const double f = (double)FE_CLOCK_HZ / s.n[i];
    out("  %7.0f Hz M=%2u |H| %.4f ph %+7.2f deg", f, s.m[i], cabs2(s.h[i]),
        carg(s.h[i]) * 180.0 / M_PI);
  }
}

// `spectrum open`: electrodes floating (U3 /OE high), so Ye = 0 and the sweep sees only the SENSE
// node's own parasitics. Fits Cp and keeps it for later sweeps (RAM only; re-run after a reboot).
static void cmdSpectrumOpen()
{
  const SwapState was = g_swap; const bool chopWas = g_chopEnabled;
  setSwap(SwapState::Open); setChopEnabled(true);
  SpecSet s;
  g_cancel = false;
  const bool ok = specSweep(s);
  setChopEnabled(chopWas); setSwap(was);
  if (!ok) { out("spectrum open aborted"); return; }
  specPrintPoints(s);
  // Fit Cp alone: model with electrode admittance zero. Reuse the LM with Cd standing in for Cp.
  double p[3] = {1e-15, 1e-12, g_specCp}, sig[3];
  const bool freeP[3] = {false, false, true};
  // Cm tiny makes zm huge, so the series branch vanishes: Ye = jw*Cd, and Cd plays Cp's role.
  const double rms = specFit(s, p, freeP, 0.0, sig);
  g_specCp = p[2];
  out("Cp = %.2f pF (+/- %.1f %%), fit rms %.5f -- stored for `spectrum`", p[2] * 1e12, sig[2] * 100, rms);
}

// `spectrum [a|b|ab]`: sweep 250 kHz..4 MHz with one or both electrode orientations, average the
// complex transfers, and fit {Cm, Gs, Cs, Cd}. 9 frequencies x 2 = 18 equations for 4 unknowns.
// Soil conductivity rises about 2 %/C, the standard EC correction. The tip NTC is what makes it
// correctable here, which is the thing MSV3 never had: its die sensor read the dry end.
constexpr double SPEC_EC_TEMPCO = 0.02;

static inline double specRsoilTo25C(double rsoil, double tC)
{
  return rsoil * (1.0 + SPEC_EC_TEMPCO * (tC - 25.0));
}

// Sweeps one or both orientations, averages the complex transfers, and fits {Cm, Gs, Cd}.
// Returns false if the sweep was cancelled or failed. Restores swap/chopper state either way.
static bool runSpectrumFit(bool doA, bool doB, SpecSet& acc, double* best, double* bestSig,
                           double& bestRms, bool verbose)
{
  const SwapState was = g_swap; const bool chopWas = g_chopEnabled;
  setChopEnabled(true);
  SpecSet s;
  int runs = 0;
  g_cancel = false;
  for (int o = 0; o < 2; o++)
  {
    if ((o == 0 && !doA) || (o == 1 && !doB)) continue;
    setSwap(o == 0 ? SwapState::A : SwapState::B);
    if (!specSweep(s)) { setChopEnabled(chopWas); setSwap(was); return false; }
    if (runs == 0) acc = s;
    else for (int i = 0; i < s.count; i++) acc.h[i] += s.h[i];
    if (verbose) out("orientation %c done", o == 0 ? 'A' : 'B');
    runs++;
  }
  setChopEnabled(chopWas); setSwap(was);
  if (runs == 0) return false;
  for (int i = 0; i < acc.count; i++) acc.h[i] /= (double)runs;

  // Several starting points for Gs across four decades; keep the best fit. With Cs gone the
  // optimum is unique, so this is now only insurance against a bad basin, not a tie-break
  // between equally good answers (which is what it silently was before 2026-09-23).
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
      memcpy(bestSig, sig, 3 * sizeof(double));
    }
  }
  return true;
}

static void cmdSpectrum(const char* which)
{
  const bool doA = !which || strchr(which, 'a');
  const bool doB = !which || strchr(which, 'b');
  if (!doA && !doB) { out("usage: spectrum [a|b|ab|open]"); return; }
  SpecSet acc;
  double best[3] = {}, bestSig[3] = {}, bestRms = 0;
  if (!runSpectrumFit(doA, doB, acc, best, bestSig, bestRms, true)) { out("spectrum aborted"); return; }
  specPrintPoints(acc);

  // +/- figures are statistical only. Validated 2026-09-19 against the field solve: model error
  // (~0.2-1 % of drive) dominates them in moist soil. See MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md.
  const double rsoil = 1.0 / best[1];
  out("fit (Cp %.2f pF, Rs %.0f ohm), rms residual %.5f of drive:", g_specCp * 1e12, SPEC_RS, bestRms);
  out("  Rsoil 1/Gs    %8.1f ohm +/- %5.1f %%  <- MOISTURE. lower = wetter", rsoil, bestSig[1] * 100);
  out("  Cm interface  %8.2f pF  +/- %5.1f %%  <- contact quality: gaps/crusts move this", best[0] * 1e12, bestSig[0] * 100);
  out("  Cd direct     %8.2f pF  +/- %5.1f %%  <- board/fringe, should NOT track moisture", best[2] * 1e12, bestSig[2] * 100);
  out("  interface corner Gs/(2pi Cm) = %.0f kHz", best[1] / (2 * M_PI * best[0]) / 1e3);
  float tC, rN, tMv, vMv;
  if (measureTemp(tC, rN, tMv, vMv))
    out("  Rsoil @25C    %8.1f ohm  (tip %.2f C, %.0f %%/C EC tempco)",
        specRsoilTo25C(rsoil, tC), tC, SPEC_EC_TEMPCO * 100);
  else
    out("  Rsoil @25C    unavailable -- NTC read failed");
}

// The reading to log over time: temperature-corrected soil conductance, plus the contact-quality
// channel that says whether to believe it. Cm collapsing while Rsoil climbs means the blade lost
// contact with the soil, not that the pot dried out.
static void cmdMoisture()
{
  SpecSet acc;
  double best[3] = {}, bestSig[3] = {}, bestRms = 0;
  if (!runSpectrumFit(true, true, acc, best, bestSig, bestRms, false)) { out("moisture aborted"); return; }
  const double rsoil = 1.0 / best[1];
  float tC, rN, tMv, vMv;
  const bool haveT = measureTemp(tC, rN, tMv, vMv);
  out("moisture Rsoil25=%.1f Rsoil=%.1f T=%.2f Cm=%.1f Cd=%.2f rms=%.5f%s",
      haveT ? specRsoilTo25C(rsoil, tC) : rsoil, rsoil, haveT ? tC : 0.0f,
      best[0] * 1e12, best[2] * 1e12, bestRms, haveT ? "" : " NO_TEMP");
}

// -------------------------------------------------------------------------------- commands ----
static void cmdHelp()
{
  out("MSV4 bring-up commands:");
  out(" info | pins | help | safe (everything off)");
  out(" led on|off|blink");
  out(" vbat              battery via Q1 divider");
  out(" temp              NTC at the blade tip");
  out(" vdd               SAADC VDD channel");
  out(" drive <N> [k]     excite at 16MHz/N, chopper phase k (N even 4..16000; 16 = 1 MHz, 4 = 4 MHz)");
  out(" drive off");
  out(" phase <k>         move chopper phase, keep N");
  out(" swap a|b|open     U3 electrode select / float both");
  out(" chop on|off       U4 /OE (connect the averaging filters)");
  out(" read              SENSE_OUT + DRV_OUT (mV, oversampled)");
  out(" iq [N]            4-phase P/I/Q at N (default current or 16)");
  out(" sweep [N]         all N phases + fit residual (N<=64)");
  out(" spectrum open     250k-2.7M sweep, electrodes floating -> SENSE parasitic Cp");
  out(" spectrum [a|b|ab] 250k-2.7M sweep, fit Rsoil (moisture) Cm (contact) Cd");
  out(" moisture          one line: temp-corrected Rsoil + contact quality. LOG THIS");
  out(" settle <ms> | os <0..8>   phase settle time / log2 oversample");
  out(" cal               SAADC offset calibration");
  out(" dfu on|off|enter  allow buttonless DFU / reboot into BLE DFU now");
  out(" reboot | stop (cancel a sweep)");
}

static void cmdInfo()
{
  const uint32_t lf = NRF_CLOCK->LFCLKSTAT;
  uint32_t hf = 0;
  sd_clock_hfclk_is_running(&hf);
  out("%s app_version=%lu", MSV4_BUILD_ID, static_cast<unsigned long>(MSV4_APP_VERSION));
  out("name=%s addr=%08lX%08lX", g_deviceName, static_cast<unsigned long>(NRF_FICR->DEVICEADDR[1] & 0xFFFF),
      static_cast<unsigned long>(NRF_FICR->DEVICEADDR[0]));
  out("uptime=%lus reset_reason=0x%08lX boot_count=%u", millis() / 1000,
      static_cast<unsigned long>(readResetReason()), g_bootCount);
  out("LFCLK now: %s %s | at setup entry: %s %s | start wait %lums",
      lfclkSourceName(lf), (lf & CLOCK_LFCLKSTAT_STATE_Msk) ? "running" : "STOPPED",
      lfclkSourceName(g_lfclkStatAtEntry),
      (g_lfclkStatAtEntry & CLOCK_LFCLKSTAT_STATE_Msk) ? "running" : "stopped",
      static_cast<unsigned long>(g_lfclkStartMs));
  if (!lfclkOnXtal()) out("WARNING: LFCLK is not on the crystal. Check Y1/C2/C3.");
  out("HFXO=%s front_end=%s N=%u k=%u swap=%s chop=%s", hf ? "running" : "off",
      g_fe.running ? "RUNNING" : "off", g_fe.n, g_fe.k,
      g_swap == SwapState::Open ? "open" : (g_swap == SwapState::A ? "A" : "B"),
      g_chopEnabled ? "on" : "off");
  if (!g_feResourcesFree) out("WARNING: PPI 8-11 / GPIOTE 6-7 were claimed at boot; drive disabled");
}

static void pinLine(const char* name, uint8_t pin)
{
  const uint32_t cnf = NRF_P0->PIN_CNF[pin];
  const bool outDir = (cnf & GPIO_PIN_CNF_DIR_Msk) != 0;
  const bool outHigh = (NRF_P0->OUT >> pin) & 1;
  const char* owner = "";
  for (uint8_t ch = 0; ch < 8; ch++)
  {
    const uint32_t g = NRF_GPIOTE->CONFIG[ch];
    if (((g & GPIOTE_CONFIG_MODE_Msk) >> GPIOTE_CONFIG_MODE_Pos) == GPIOTE_CONFIG_MODE_Task &&
        ((g & GPIOTE_CONFIG_PSEL_Msk) >> GPIOTE_CONFIG_PSEL_Pos) == pin &&
        (g & GPIOTE_CONFIG_PORT_Msk) == 0)
      owner = " [GPIOTE]";
  }
  const bool hd = ((cnf & GPIO_PIN_CNF_DRIVE_Msk) >> GPIO_PIN_CNF_DRIVE_Pos) == GPIO_PIN_CNF_DRIVE_H0H1;
  out(" P0.%02u %-12s %s%s%s", pin, name,
      outDir ? (outHigh ? "OUT HIGH" : "OUT LOW") : "released", hd ? " hi-drive" : "", owner);
}

// Whitelist only: the crystal pins, SWD and anything unassigned are never touched or reported.
static void cmdPins()
{
  pinLine("VBAT_DIV_EN", PIN_VBAT_DIV_EN);
  pinLine("DRV", PIN_DRV);
  pinLine("TEMP_READ", PIN_TEMP_READ);
  pinLine("SWAP_CTRL", PIN_SWAP_CTRL);
  pinLine("SWAP_nOE", PIN_SWAP_NOE);
  pinLine("READ_CNTL", PIN_READ_CNTL);
  pinLine("READ_nOE", PIN_READ_NOE);
}

static bool parseUint(const char* s, uint32_t& v)
{
  if (!s || !*s) return false;
  char* end = nullptr;
  const unsigned long x = strtoul(s, &end, 10);
  if (end == s || *end != '\0') return false;
  v = x;
  return true;
}

static void enterDfuNow()
{
  out("rebooting into BLE DFU (bootloader advertises; power-cycle to abort)");
  delayFeeding(300);
  allSafe();
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.disconnect(Bluefruit.connHandle());
  delayFeeding(200);
  armDfuAndReset();
  // Only reached on failure; the link is already down, so restore advertising.
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);
}

static void handleCommand(char* line)
{
  char* argv[4] = {nullptr, nullptr, nullptr, nullptr};
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t"))
  {
    for (char* p = tok; *p; p++) *p = static_cast<char>(tolower(*p));
    argv[argc++] = tok;
  }
  if (argc == 0) return;
  const char* c = argv[0];

  if (!strcmp(c, "help") || !strcmp(c, "?")) cmdHelp();
  else if (!strcmp(c, "info")) cmdInfo();
  else if (!strcmp(c, "pins")) cmdPins();
  else if (!strcmp(c, "safe")) { allSafe(); out("OK all outputs safe"); }
  else if (!strcmp(c, "reboot")) { out("rebooting"); delayFeeding(200); allSafe(); sd_nvic_SystemReset(); }
  else if (!strcmp(c, "led"))
  {
    if (argc > 1 && !strcmp(argv[1], "on")) g_ledMode = LedMode::On;
    else if (argc > 1 && !strcmp(argv[1], "off")) g_ledMode = LedMode::Off;
    else if (argc > 1 && !strcmp(argv[1], "blink")) g_ledMode = LedMode::Blink;
    else { out("usage: led on|off|blink"); return; }
    out("OK");
  }
  else if (!strcmp(c, "vbat"))
  {
    float vbat, node;
    if (measureVbatMv(vbat, node)) out("VBAT=%.0f mV (node %.0f mV)", vbat, node);
    else out("ERR SAADC timeout");
  }
  else if (!strcmp(c, "vdd"))
  {
    float vdd;
    if (saadcReadMv(PSEL_VDD, 4, 10, vdd)) out("VDD=%.0f mV", vdd);
    else out("ERR SAADC timeout");
  }
  else if (!strcmp(c, "temp"))
  {
    float t, r, tm, vdd;
    // Die temperature from the nRF52840 TEMP peripheral, read through the SoftDevice (it owns
    // TEMP while enabled). 0.25 C steps. It sits at the board end, not the blade tip, so the
    // difference from the NTC is the tip-vs-board gradient, not a calibration error.
    int32_t dieQuarterC = 0;
    const bool dieOk = sd_temp_get(&dieQuarterC) == NRF_SUCCESS;
    if (measureTemp(t, r, tm, vdd))
    {
      if (dieOk)
        out("TEMP=%.2f C (NTC %.0f ohm, node %.0f mV, VDD %.0f mV) DIE=%.2f C", t, r, tm, vdd,
            dieQuarterC * 0.25f);
      else out("TEMP=%.2f C (NTC %.0f ohm, node %.0f mV, VDD %.0f mV) DIE=ERR", t, r, tm, vdd);
    }
    else out("ERR temp read failed (SAADC timeout or divider out of range)");
  }
  else if (!strcmp(c, "drive"))
  {
    if (argc > 1 && !strcmp(argv[1], "off")) { frontEndStop(); out("OK drive off"); return; }
    uint32_t n = 0, k = 0;
    if (argc < 2 || !parseUint(argv[1], n) || (argc > 2 && !parseUint(argv[2], k)))
    {
      out("usage: drive <N> [k] | drive off");
      return;
    }
    const char* err = frontEndStart(static_cast<uint16_t>(n > 65535 ? 0 : n), static_cast<uint16_t>(k > 65535 ? 65535 : k));
    if (err) out("ERR %s", err);
    else out("OK DRV %lu Hz (N=%u), chopper phase k=%u = %.1f deg", FE_CLOCK_HZ / g_fe.n, g_fe.n, g_fe.k,
             360.0f * g_fe.k / g_fe.n);
  }
  else if (!strcmp(c, "phase"))
  {
    uint32_t k = 0;
    if (argc < 2 || !parseUint(argv[1], k)) { out("usage: phase <k>"); return; }
    if (!g_fe.running) { out("ERR drive is off"); return; }
    const char* err = frontEndStart(g_fe.n, static_cast<uint16_t>(k > 65535 ? 65535 : k));
    if (err) out("ERR %s", err);
    else out("OK k=%u = %.1f deg", g_fe.k, 360.0f * g_fe.k / g_fe.n);
  }
  else if (!strcmp(c, "swap"))
  {
    if (argc > 1 && !strcmp(argv[1], "a")) setSwap(SwapState::A);
    else if (argc > 1 && !strcmp(argv[1], "b")) setSwap(SwapState::B);
    else if (argc > 1 && !strcmp(argv[1], "open")) setSwap(SwapState::Open);
    else { out("usage: swap a|b|open"); return; }
    out("OK");
  }
  else if (!strcmp(c, "chop"))
  {
    if (argc > 1 && !strcmp(argv[1], "on")) setChopEnabled(true);
    else if (argc > 1 && !strcmp(argv[1], "off")) setChopEnabled(false);
    else { out("usage: chop on|off"); return; }
    out("OK");
  }
  else if (!strcmp(c, "read"))
  {
    float s, d;
    if (readFrontEnd(s, d)) out("SENSE_OUT=%.2f mV DRV_OUT=%.2f mV (os %u)", s, d, 1u << g_oversampleLog2);
    else out("ERR SAADC timeout");
  }
  else if (!strcmp(c, "iq") || !strcmp(c, "sweep"))
  {
    uint32_t n = g_fe.running ? g_fe.n : FE_N_DEFAULT;
    if (argc > 1 && !parseUint(argv[1], n)) { out("usage: %s [N]", c); return; }
    if (n < FE_N_MIN || n > FE_N_MAX || (n % 4) != 0) { out("ERR N must be a multiple of 4, 4..16000"); return; }
    const bool sweep = !strcmp(c, "sweep");
    if (sweep && n > 64) { out("ERR sweep needs N <= 64 (one point per phase)"); return; }
    runPhaseSet(static_cast<uint16_t>(n), sweep ? static_cast<uint16_t>(n) : 4, sweep);
  }
  else if (!strcmp(c, "spectrum"))
  {
    if (argc > 1 && !strcmp(argv[1], "open")) cmdSpectrumOpen();
    else cmdSpectrum(argc > 1 ? argv[1] : nullptr);
  }
  else if (!strcmp(c, "moisture")) cmdMoisture();
  else if (!strcmp(c, "settle"))
  {
    uint32_t ms = 0;
    if (argc < 2 || !parseUint(argv[1], ms) || ms > 2000) { out("usage: settle <0..2000 ms>"); return; }
    g_settleMs = ms;
    out("OK settle=%lums", static_cast<unsigned long>(ms));
  }
  else if (!strcmp(c, "os"))
  {
    uint32_t v = 0;
    if (argc < 2 || !parseUint(argv[1], v) || v > 8) { out("usage: os <0..8> (log2 oversample)"); return; }
    g_oversampleLog2 = static_cast<uint8_t>(v);
    out("OK oversample=%u", 1u << v);
  }
  else if (!strcmp(c, "cal")) out(saadcCalibrate() ? "OK SAADC calibrated" : "ERR calibration timeout");
  else if (!strcmp(c, "dfu"))
  {
    if (argc > 1 && !strcmp(argv[1], "on")) { bledfu.setDfuAllowed(true); out("OK buttonless DFU allowed"); }
    else if (argc > 1 && !strcmp(argv[1], "off")) { bledfu.setDfuAllowed(false); out("OK buttonless DFU refused"); }
    else if (argc > 1 && !strcmp(argv[1], "enter")) enterDfuNow();
    else out("usage: dfu on|off|enter");
  }
  else out("unknown command '%s' -- try help", c);
}

// ---------------------------------------------------------------------------- BLE callbacks ---
// Runs in the Bluefruit callback task: only assembles a line and hands it to loop().
static void onUartRx(uint16_t)
{
  while (bleuart.available())
  {
    const int ch = bleuart.read();
    if (ch < 0) break;
    if (ch == '\r' || ch == '\n')
    {
      if (g_rxOverflow) { g_rxOverflow = false; g_rxLen = 0; continue; }
      if (g_rxLen == 0) continue;
      g_rxLine[g_rxLen] = '\0';
      if (!strcmp(g_rxLine, "stop")) g_cancel = true;  // must work while a sweep holds loop()
      else if (!g_cmdPending)
      {
        memcpy(g_pendingCmd, g_rxLine, g_rxLen + 1);
        g_cmdPending = true;
      }
      g_rxLen = 0;
    }
    else if (g_rxLen < CMD_MAX) g_rxLine[g_rxLen++] = static_cast<char>(ch);
    else g_rxOverflow = true;
  }
}

static void onConnect(uint16_t)
{
  g_rxLen = 0;
  g_rxOverflow = false;
}

// A dropped link must not leave anything energised. Callback context only sets flags; loop() and
// the sweep loop do the register work.
static volatile bool g_disconnected = false;
static void onDisconnect(uint16_t, uint8_t)
{
  g_cancel = true;
  g_disconnected = true;
}

static void startAdvertising()
{
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 800);  // 100 ms fast, 500 ms slow (0.625 ms units)
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// ------------------------------------------------------------------------------- setup/loop ---
void setup()
{
  cycleCounterEnable();

  // Crystal/boot guard, all before the SoftDevice. See the block comment at the top.
  uint8_t count = 0;
  if (!readRetained(count)) count = 0;
  if (count >= BOOT_ESCAPE_LIMIT) escapeToDfuPreSoftDevice(3);
  g_bootCount = static_cast<uint8_t>(count + 1);
  writeRetained(g_bootCount);

  g_lfclkStatAtEntry = NRF_CLOCK->LFCLKSTAT;
  const uint32_t t0 = DWT->CYCCNT;
  const uint32_t limit = LFCLK_START_TIMEOUT_MS * (SystemCoreClock / 1000UL);
  while (!lfclkOnXtal())
  {
    feedWatchdog();  // only matters if a WDT was inherited through a soft reset
    if ((DWT->CYCCNT - t0) > limit) escapeToDfuPreSoftDevice(5);
  }
  g_lfclkStartMs = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000UL);
  startWatchdog();

  pinOutput(PIN_DRV, false);
  pinOutput(PIN_VBAT_DIV_EN, false);
  pinOutput(PIN_TEMP_READ, false);
  setChopEnabled(false);
  setSwap(SwapState::Open);

  // Unique-ish name per board so several can sit on one bench.
  snprintf(g_deviceName, sizeof(g_deviceName), "MSV4-BU-%04lX",
           static_cast<unsigned long>(NRF_FICR->DEVICEADDR[0] & 0xFFFF));

  Bluefruit.autoConnLed(false);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();  // a dead crystal stalls here -> WDT reset -> boot counter
  Bluefruit.setTxPower(4);
  Bluefruit.setName(g_deviceName);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  bledis.setManufacturer("Treebeard");
  bledis.setModel("MSV4");
  bledis.setFirmwareRev(MSV4_BUILD_ID);
  bledis.begin();

  // Bring-up images must stay replaceable over the air: DFU is allowed by default. `dfu off`
  // exists for bench tests that want to refuse it. Packages are signed either way.
  bledfu.begin();
  bledfu.setDfuAllowed(true);

  bleuart.begin();
  bleuart.setRxCallback(onUartRx);

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

  startAdvertising();
  saadcCalibrate();

  // Reached advertising: clear the counter so later resets start fresh.
  writeRetained(0);
}

void loop()
{
  feedWatchdog();

  if (g_disconnected)
  {
    g_disconnected = false;
    g_cancel = false;
    allSafe();  // unconditional: covers swap/chop left on, not just a running front end
  }
  if (g_cancel && g_fe.running)
  {
    g_cancel = false;
    allSafe();
    out("front end stopped");
  }

  if (g_cmdPending)
  {
    char line[CMD_MAX + 1];
    memcpy(line, g_pendingCmd, sizeof(line));
    g_cmdPending = false;
    g_cancel = false;
    handleCommand(line);
    out("> ");
  }

  if (bledfu.dfuResetPending())
  {
    allSafe();
    delayFeeding(300);  // let the ATT response reach the client before the reset
    bledfu.completeDfuReset();
  }

  static uint32_t lastBlink = 0;
  const uint32_t now = millis();
  switch (g_ledMode)
  {
    case LedMode::On: ledWrite(true); break;
    case LedMode::Off: ledWrite(false); break;
    case LedMode::Blink:
      // Short blip: every 2 s advertising, every 0.5 s while connected. (3 and 5 slow blinks
      // before a reset are the boot-counter and dead-crystal DFU escapes.)
      if (now - lastBlink >= (Bluefruit.connected() ? 500UL : 2000UL))
      {
        lastBlink = now;
        ledWrite(true);
        delay(15);
        ledWrite(false);
      }
      break;
  }
  delay(10);
}
