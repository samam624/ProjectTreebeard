// MSV2 ESP32-S3 -> nRF52840 SWD flasher.
//
// Turns an ESP32-S3 devkit into a standalone SWD debug adapter that can attach to,
// erase, and program an nRF52840 target (XIAO nRF52840 Sense probes today, MSV3's
// bare E73-2G4M08S1C module once that board exists) without needing the target's
// own USB/DFU bootloader. Useful for boards that don't have a bootloader at all,
// or whose bootloader/USB stack is broken.
//
// Wiring (3.3V logic both sides, no level shifting needed):
//   ESP32-S3 GPIO5  -> target SWCLK
//   ESP32-S3 GPIO6  -> target SWDIO
//   ESP32-S3 GPIO7  -> target RESET (nRESET, active low)
//   ESP32-S3 GND    -> target GND
// Do NOT connect target VDD to the ESP32-S3 -- the target should be powered the
// way it normally is (its own USB/battery). This tool only drives debug signals.
//
// Talk to it over the native USB serial port at 115200 baud. Type `help` for commands.
//
// Protocol references this implements from scratch (no external SWD library):
//   - ARM ADIv5 Debug Interface: SW-DP packet format, DP/AP register access, the
//     JTAG-to-SWD line-reset + 0xE79E switch sequence, AP read pipelining via RDBUFF.
//   - nRF52840 Product Specification: NVMC registers for erase/write, FICR for
//     flash geometry, standard Cortex-M4 DHCSR/AIRCR debug registers for halt/reset.
//
// This has NOT yet been bench-validated against real hardware (no target was wired
// up when this was written). The `id` command is deliberately read-only and is the
// first thing to run against a freshly wired target -- confirm DPIDR/AP-IDR/FICR
// values look sane before trusting `erase` or `flash`. The one magic-looking
// constant here is CSW_WORD_AUTOINCR (see below) -- it's a widely-used literature
// default, not something derived on this bench, so the `id` command reads it back
// after writing it as a sanity check.

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------

static const int PIN_SWCLK = 5;
static const int PIN_SWDIO = 6;
static const int PIN_NRESET = 7;

// Bit-bang timing: SWD has no minimum clock speed requirement, only a maximum,
// so a conservative software delay was the safe starting point. Tightened
// 3->1 after `id`/`probe` were confirmed reliable at the original speed on
// this bench's wiring -- if reads start looking flaky (garbage DPIDR, more
// verify mismatches than expected) after this change, that's a sign the
// physical wiring can't keep up and this should go back up, not down further.
static const uint32_t SWD_HALF_PERIOD_US = 1;

// ---------------------------------------------------------------------------
// ARM ADIv5 constants
// ---------------------------------------------------------------------------

static const uint16_t JTAG_TO_SWD_SEQUENCE = 0xE79E;

// DP register addresses (A[3:2] within the request packet; APnDP=0)
static const uint8_t DP_IDCODE_ABORT = 0x0; // read=DPIDR, write=ABORT
static const uint8_t DP_CTRL_STAT = 0x4;    // read/write
static const uint8_t DP_SELECT = 0x8;       // write-only (read=RESEND, unused here)
static const uint8_t DP_RDBUFF = 0xC;       // read-only

// AP register addresses (A[3:2] within the request packet; APnDP=1), bank 0
static const uint8_t AP_CSW = 0x0;
static const uint8_t AP_TAR = 0x4;
static const uint8_t AP_DRW = 0xC;
static const uint8_t AP_IDR = 0xC; // bank 0xF, handled via apReadBanked()

static const uint8_t ACK_OK = 0x1;
static const uint8_t ACK_WAIT = 0x2;
static const uint8_t ACK_FAULT = 0x4;

// ABORT register bits: clear all sticky error flags, don't touch DAPABORT itself.
static const uint32_t ABORT_CLEAR_STICKY = 0x1E;

// CTRL/STAT bits
static const uint32_t CTRL_STAT_CSYSPWRUPACK = (1UL << 31);
static const uint32_t CTRL_STAT_CSYSPWRUPREQ = (1UL << 30);
static const uint32_t CTRL_STAT_CDBGPWRUPACK = (1UL << 29);
static const uint32_t CTRL_STAT_CDBGPWRUPREQ = (1UL << 28);
static const uint32_t CTRL_STAT_POWERUP_REQ = CTRL_STAT_CSYSPWRUPREQ | CTRL_STAT_CDBGPWRUPREQ;
static const uint32_t CTRL_STAT_POWERUP_ACK = CTRL_STAT_CSYSPWRUPACK | CTRL_STAT_CDBGPWRUPACK;

// AHB-AP CSW: word-size (bits[2:0]=010), auto-increment single (bits[5:4]=01),
// plus the top-byte "Prot" bits (0x23) that are the de-facto standard literature
// default for Cortex-M AHB-AP word transfers across bit-banged SWD tools. Read
// back after writing in `id` to confirm this target's AP actually accepted it.
static const uint32_t CSW_WORD_AUTOINCR = 0x23000052;

// Cortex-M debug registers (memory-mapped, reached over the AHB-AP like any
// other address once the debug domain is powered up)
static const uint32_t REG_DHCSR = 0xE000EDF0;
static const uint32_t REG_DEMCR = 0xE000EDFC;
static const uint32_t REG_AIRCR = 0xE000ED0C;
static const uint32_t DHCSR_DBGKEY = 0xA05F0000;
static const uint32_t DHCSR_C_DEBUGEN = (1UL << 0);
static const uint32_t DHCSR_C_HALT = (1UL << 1);
static const uint32_t DHCSR_S_HALT = (1UL << 17);
static const uint32_t AIRCR_VECTKEY = 0x05FA0000;
static const uint32_t AIRCR_SYSRESETREQ = (1UL << 2);

// ---------------------------------------------------------------------------
// nRF52840 register map (Product Specification)
// ---------------------------------------------------------------------------

static const uint32_t NVMC_READY = 0x4001E400;
static const uint32_t NVMC_CONFIG = 0x4001E504;
static const uint32_t NVMC_ERASEPAGE = 0x4001E508;
static const uint32_t NVMC_ERASEALL = 0x4001E50C;
static const uint32_t NVMC_ERASEUICR = 0x4001E514;
static const uint32_t NVMC_CONFIG_REN = 0x0;
static const uint32_t NVMC_CONFIG_WEN = 0x1;
static const uint32_t NVMC_CONFIG_EEN = 0x2;

static const uint32_t FICR_CODEPAGESIZE = 0x10000010;
static const uint32_t FICR_CODESIZE = 0x10000014;

static const uint32_t DEFAULT_PAGE_SIZE = 4096; // fallback if FICR read looks bogus

// UICR is a separate one-page region, not part of the regular code-flash
// array -- NVMC_ERASEPAGE doesn't touch it, it needs its own erase register.
// A bootloader image's UICR.NRFFW[0] write (the pointer the chip's MBR uses
// to find the bootloader) lands here, so this matters for real images, not
// just theoretically.
static const uint32_t UICR_BASE = 0x10001000;
static const uint32_t UICR_SIZE = 0x1000;

// ---------------------------------------------------------------------------
// Bit-bang primitives
// ---------------------------------------------------------------------------

static void swdClockHigh() {
  digitalWrite(PIN_SWCLK, HIGH);
  delayMicroseconds(SWD_HALF_PERIOD_US);
}

static void swdClockLow() {
  digitalWrite(PIN_SWCLK, LOW);
  delayMicroseconds(SWD_HALF_PERIOD_US);
}

// SWDIO is sampled by the target on the SWCLK rising edge, so drive it while
// clock is low and only raise clock after it's settled.
static void swdWriteBit(int bit) {
  digitalWrite(PIN_SWDIO, bit ? HIGH : LOW);
  swdClockLow();
  swdClockHigh();
}

static int swdReadBit() {
  swdClockLow();
  int bit = digitalRead(PIN_SWDIO);
  swdClockHigh();
  return bit;
}

static void swdWriteBits(uint32_t value, int nBits) {
  for (int i = 0; i < nBits; i++) {
    swdWriteBit((value >> i) & 1);
  }
}

static uint32_t swdReadBits(int nBits) {
  uint32_t value = 0;
  for (int i = 0; i < nBits; i++) {
    if (swdReadBit()) {
      value |= (1UL << i);
    }
  }
  return value;
}

static void swdDirOut() {
  pinMode(PIN_SWDIO, OUTPUT);
}

// Pull-down (not plain INPUT) matters when contact is hand-held against test
// pads rather than soldered: a genuinely disconnected/floating SWDIO would
// otherwise pick up noise, and noise has a real chance of coincidentally
// passing the ACK/parity checks and looking like a false-positive connection.
// Biasing low makes a true disconnect read a clean, repeatable ACK=0 instead.
static void swdDirIn() {
  pinMode(PIN_SWDIO, INPUT_PULLDOWN);
}

// One clock cycle with nobody asserting SWDIO's new direction yet -- required
// between a request/ACK phase and a data phase whenever direction reverses.
static void swdTurnaroundClock() {
  swdClockLow();
  swdClockHigh();
}

static void swdIdleClocks(int n) {
  swdDirOut();
  digitalWrite(PIN_SWDIO, LOW);
  for (int i = 0; i < n; i++) {
    swdClockLow();
    swdClockHigh();
  }
}

static int parityOf(uint32_t value, int nBits) {
  int p = 0;
  for (int i = 0; i < nBits; i++) {
    p ^= (value >> i) & 1;
  }
  return p;
}

// ---------------------------------------------------------------------------
// SWD line-level sequences
// ---------------------------------------------------------------------------

static void swdLineReset() {
  swdDirOut();
  digitalWrite(PIN_SWDIO, HIGH);
  for (int i = 0; i < 56; i++) { // >=50 required, pad a little
    swdClockLow();
    swdClockHigh();
  }
}

// JTAG-to-SWD dormant switch is only needed for ADIv6 multidrop targets;
// nRF52840's Cortex-M4 SW-DP is classic ADIv5, so line-reset + the 16-bit
// select sequence + line-reset + idle is the whole story.
static void swdJtagToSwdSwitch() {
  swdLineReset();
  swdWriteBits(JTAG_TO_SWD_SEQUENCE, 16);
  swdLineReset();
  swdIdleClocks(2);
}

// ---------------------------------------------------------------------------
// SWD packet-level transfer (the core primitive everything else builds on)
// ---------------------------------------------------------------------------

// Returns ACK_OK / ACK_WAIT / ACK_FAULT / 0 (protocol error, e.g. bad parity).
static uint8_t swdTransfer(bool apndp, bool isRead, uint8_t addr, uint32_t *data) {
  uint8_t a2 = (addr >> 2) & 1;
  uint8_t a3 = (addr >> 3) & 1;
  uint8_t parity = (apndp ? 1 : 0) ^ (isRead ? 1 : 0) ^ a2 ^ a3;

  uint8_t request = 0;
  request |= (1 << 0);              // start
  request |= (apndp ? 1 : 0) << 1;  // APnDP
  request |= (isRead ? 1 : 0) << 2; // RnW
  request |= a2 << 3;
  request |= a3 << 4;
  request |= parity << 5;
  request |= (0 << 6); // stop
  request |= (1 << 7); // park

  swdDirOut();
  swdWriteBits(request, 8);

  swdDirIn();
  swdTurnaroundClock();
  uint8_t ack = swdReadBits(3);

  if (ack != ACK_OK) {
    // No data phase on WAIT/FAULT -- just release the bus back to the host.
    swdTurnaroundClock();
    swdDirOut();
    return ack;
  }

  if (isRead) {
    uint32_t value = swdReadBits(32);
    int gotParity = swdReadBit();
    swdTurnaroundClock();
    swdDirOut();
    if (gotParity != parityOf(value, 32)) {
      return 0; // parity error -- caller should treat like a fault and retry/reconnect
    }
    if (data) {
      *data = value;
    }
  } else {
    swdTurnaroundClock();
    swdDirOut();
    swdWriteBits(*data, 32);
    swdWriteBit(parityOf(*data, 32));
  }

  return ACK_OK;
}

// Retries on WAIT, clears sticky flags and bails on FAULT/protocol error.
// This is the function everything above the packet layer should call.
static bool swdTransferRetry(bool apndp, bool isRead, uint8_t addr, uint32_t *data) {
  for (int attempt = 0; attempt < 500; attempt++) {
    uint8_t ack = swdTransfer(apndp, isRead, addr, data);
    if (ack == ACK_OK) {
      return true;
    }
    if (ack == ACK_WAIT) {
      delayMicroseconds(50);
      continue;
    }
    // FAULT or protocol error: clear stickies via ABORT, then give up this call.
    // Caller decides whether to retry the whole operation.
    uint32_t abortVal = ABORT_CLEAR_STICKY;
    swdTransfer(false, false, DP_IDCODE_ABORT, &abortVal);
    return false;
  }
  return false; // exhausted WAIT retries
}

static bool dpRead(uint8_t addr, uint32_t *value) {
  return swdTransferRetry(false, true, addr, value);
}

static bool dpWrite(uint8_t addr, uint32_t value) {
  return swdTransferRetry(false, false, addr, &value);
}

static uint8_t currentApSel = 0xFF; // force a SELECT write on first use
static uint8_t currentApBank = 0xFF;

static bool apSelect(uint8_t apSel, uint8_t regAddr) {
  uint8_t bank = (regAddr >> 4) & 0xF;
  if (apSel == currentApSel && bank == currentApBank) {
    return true;
  }
  uint32_t selectVal = ((uint32_t)apSel << 24) | ((uint32_t)bank << 4);
  if (!dpWrite(DP_SELECT, selectVal)) {
    return false;
  }
  currentApSel = apSel;
  currentApBank = bank;
  return true;
}

static bool apWrite(uint8_t apSel, uint8_t regAddr, uint32_t value) {
  if (!apSelect(apSel, regAddr)) {
    return false;
  }
  return swdTransferRetry(true, false, regAddr & 0xC, &value);
}

// AP reads are pipelined: the transfer below returns the *previous* AP read's
// result, so the actual value comes from the DP RDBUFF flush read afterward.
static bool apRead(uint8_t apSel, uint8_t regAddr, uint32_t *value) {
  if (!apSelect(apSel, regAddr)) {
    return false;
  }
  uint32_t discard = 0;
  if (!swdTransferRetry(true, true, regAddr & 0xC, &discard)) {
    return false;
  }
  return dpRead(DP_RDBUFF, value);
}

// ---------------------------------------------------------------------------
// Memory access via AHB-AP (AP0) -- this is how flashing works without ever
// needing the target CPU to execute a single instruction.
// ---------------------------------------------------------------------------

static const uint8_t AHB_AP = 0;
// nRF52 CTRL-AP: the debugger-side mass erase / recovery access port.
//
// Why mass erase goes through here and NOT through NVMC ERASEALL. Measured on an nRF52840
// (probe 99, 2026-09-07): writing NVMC_ERASEALL over the AHB-AP is ACKed at the SWD layer but
// FAULTS on the AHB bus -- DP CTRL/STAT came back 0xF0000060 with STICKYERR set, NVMC never
// asserted busy, and flash was untouched. Nothing in this adapter had ever examined sticky bits,
// so the failure surfaced only as `erase: OK` on a board that still had its old application.
// CTRL-AP ERASEALL, issued in the same session, reported busy and left the device blank.
static const uint8_t CTRL_AP = 1;
static const uint8_t CTRL_AP_ERASEALL = 0x04;
static const uint8_t CTRL_AP_ERASEALLSTATUS = 0x08;

static bool memRead32(uint32_t addr, uint32_t *value) {
  if (!apWrite(AHB_AP, AP_TAR, addr)) return false;
  return apRead(AHB_AP, AP_DRW, value);
}

static bool memWrite32(uint32_t addr, uint32_t value) {
  if (!apWrite(AHB_AP, AP_TAR, addr)) return false;
  return apWrite(AHB_AP, AP_DRW, value);
}

// ---------------------------------------------------------------------------
// High-level connect / halt / reset
// ---------------------------------------------------------------------------

static bool g_connected = false;
static uint32_t g_pageSize = DEFAULT_PAGE_SIZE;
static uint32_t g_flashPages = 0;

static bool powerUpDebugDomain() {
  if (!dpWrite(DP_CTRL_STAT, CTRL_STAT_POWERUP_REQ)) return false;
  for (int i = 0; i < 100; i++) {
    uint32_t stat = 0;
    if (dpRead(DP_CTRL_STAT, &stat) && (stat & CTRL_STAT_POWERUP_ACK) == CTRL_STAT_POWERUP_ACK) {
      return true;
    }
    delay(1);
  }
  return false;
}

// Cheap, fast contact check: just the switch sequence + a DPIDR read, none of
// swdConnect()'s debug-domain power-up or AP setup. Meant to be called in a
// tight loop while physically holding wires against test pads, so the win
// here is speed and a low false-positive rate, not completeness.
//
// DPIDR bit 0 is architecturally fixed at 1 (ADIv5) -- checking it catches the
// residual case where noise on the line happens to satisfy the ACK+parity
// checks despite INPUT_PULLDOWN, since parity alone is only a 1-bit check.
static bool quickIdRead(uint32_t *dpidr) {
  pinMode(PIN_SWCLK, OUTPUT);
  pinMode(PIN_SWDIO, OUTPUT);
  digitalWrite(PIN_SWCLK, LOW);
  swdJtagToSwdSwitch();
  uint32_t value = 0;
  if (!dpRead(DP_IDCODE_ABORT, &value)) {
    return false;
  }
  if ((value & 1) != 1) {
    return false;
  }
  if (dpidr) *dpidr = value;
  return true;
}

// Read-only: line reset, switch to SWD, read DPIDR, power up, set up AP0 CSW,
// read the target's flash geometry from FICR. Safe to call any time to check
// wiring/connectivity before attempting anything destructive.
static bool swdConnect(bool verbose) {
  currentApSel = 0xFF;
  currentApBank = 0xFF;
  pinMode(PIN_SWCLK, OUTPUT);
  pinMode(PIN_SWDIO, OUTPUT);
  digitalWrite(PIN_SWCLK, LOW);

  swdJtagToSwdSwitch();

  uint32_t dpidr = 0;
  if (!dpRead(DP_IDCODE_ABORT, &dpidr)) {
    if (verbose) Serial.println(F("connect: no response reading DPIDR -- check wiring"));
    return false;
  }
  if (verbose) {
    Serial.print(F("DPIDR = 0x"));
    Serial.println(dpidr, HEX);
  }

  uint32_t abortVal = ABORT_CLEAR_STICKY;
  dpWrite(DP_IDCODE_ABORT, abortVal); // best-effort, ignore result

  if (!powerUpDebugDomain()) {
    if (verbose) Serial.println(F("connect: debug domain power-up ack timed out"));
    return false;
  }

  if (!apWrite(AHB_AP, AP_CSW, CSW_WORD_AUTOINCR)) {
    if (verbose) Serial.println(F("connect: failed writing AP CSW"));
    return false;
  }
  uint32_t cswReadback = 0;
  apRead(AHB_AP, AP_CSW, &cswReadback);
  if (verbose) {
    Serial.print(F("AP CSW wrote 0x"));
    Serial.print(CSW_WORD_AUTOINCR, HEX);
    Serial.print(F(", read back 0x"));
    Serial.println(cswReadback, HEX);
  }

  uint32_t apIdr = 0;
  if (apRead(AHB_AP, AP_IDR | 0xF0, &apIdr) && verbose) {
    Serial.print(F("AP0 IDR = 0x"));
    Serial.println(apIdr, HEX);
  }

  uint32_t pageSize = 0, codeSize = 0;
  bool gotPageSize = memRead32(FICR_CODEPAGESIZE, &pageSize);
  bool gotCodeSize = memRead32(FICR_CODESIZE, &codeSize);
  if (gotPageSize && pageSize != 0 && pageSize != 0xFFFFFFFF) {
    g_pageSize = pageSize;
  } else {
    g_pageSize = DEFAULT_PAGE_SIZE;
  }
  g_flashPages = (gotCodeSize && codeSize != 0xFFFFFFFF) ? codeSize : 0;

  if (verbose) {
    if ((gotPageSize && pageSize == 0xFFFFFFFF) || (gotCodeSize && codeSize == 0xFFFFFFFF)) {
      Serial.println(F("WARNING: FICR reads returned 0xFFFFFFFF -- target may be "
                        "APPROTECT-locked. This tool does not implement the CTRL-AP "
                        "recovery/unlock sequence."));
    }
    Serial.print(F("Flash page size: "));
    Serial.print(g_pageSize);
    Serial.print(F(" bytes, pages: "));
    Serial.print(g_flashPages);
    Serial.print(F(" (total "));
    Serial.print((g_pageSize * g_flashPages) / 1024);
    Serial.println(F(" KB)"));
  }

  g_connected = true;
  return true;
}

static bool haltCore() {
  uint32_t val = DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT;
  if (!memWrite32(REG_DHCSR, val)) return false;
  for (int i = 0; i < 50; i++) {
    uint32_t dhcsr = 0;
    if (memRead32(REG_DHCSR, &dhcsr) && (dhcsr & DHCSR_S_HALT)) {
      return true;
    }
    delay(1);
  }
  return false;
}

static void resumeCore() {
  uint32_t val = DHCSR_DBGKEY | DHCSR_C_DEBUGEN; // debugen on, halt cleared
  memWrite32(REG_DHCSR, val);
}

static void pulseHardwareReset() {
  pinMode(PIN_NRESET, OUTPUT);
  digitalWrite(PIN_NRESET, LOW);
  delay(10);
  pinMode(PIN_NRESET, INPUT); // release; target board supplies its own pull-up
  delay(10);
}

static bool systemResetRequest() {
  uint32_t val = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
  return memWrite32(REG_AIRCR, val);
}

// ---------------------------------------------------------------------------
// NVMC erase / program
// ---------------------------------------------------------------------------

static bool nvmcWaitReady() {
  for (int i = 0; i < 10000; i++) {
    uint32_t ready = 0;
    if (memRead32(NVMC_READY, &ready) && (ready & 1)) {
      return true;
    }
  }
  return false;
}

static bool nvmcSetConfig(uint32_t mode) {
  if (!memWrite32(NVMC_CONFIG, mode)) return false;
  return nvmcWaitReady();
}

// Mass erase by ERASING EVERY PAGE, then UICR, then PROVING the device reads blank.
//
// It does not use NVMC ERASEALL. Measured on an nRF52840 (probe 99, 2026-09-07): ERASEALL is
// silently ignored over this AHB-AP debug path -- the write is ACKed, NVMC never asserts busy
// (READY==1 on all 64 samples across 65 ms), and flash is untouched. CONFIG was confirmed EEN by
// reading it back, APPROTECT read 0xFFFFFFFF, no ACL regions were configured, and halting the core
// made no difference. In the very same session ERASEPAGE worked fine through identical code, so
// this is specific to the ERASEALL register, not to CONFIG, protection or the wait logic.
// Nordic's own tools drive debugger mass erase through CTRL-AP rather than NVMC, which is the
// likely reason. Rather than keep two erase paths and a partial-operation state to reason about,
// the production path is the one proven to work on this hardware.
//
// THE REAL DEFECT THIS FIXES is not that ERASEALL failed. It is that the old code reported
// "erase: OK" having verified nothing, swd_flash_hex.py trusted that string and carried on, and a
// board went out the far side still running its previous application. A verified erase catches
// that regardless of which primitive is used or why it failed. So the postcondition -- persistent
// storage actually reads blank -- is checked in full, not sampled.
static bool massErase() {
  const uint32_t pages = g_flashPages ? g_flashPages : (0x100000 / g_pageSize);

  // CTRL-AP (AP index 1), ERASEALL at 0x04, ERASEALLSTATUS at 0x08. This is how Nordic's own
  // tooling performs a debugger-driven mass erase, and it is the only mechanism MEASURED to work
  // on this hardware. It erases code flash and UICR together.
  if (!apWrite(CTRL_AP, CTRL_AP_ERASEALL, 1)) {
    Serial.println(F("erase: CTRL-AP ERASEALL write failed"));
    return false;
  }
  bool finished = false;
  for (int i = 0; i < 200; i++) {   // ~4 s budget; the operation itself is ~170 ms
    uint32_t status = 0;
    if (apRead(CTRL_AP, CTRL_AP_ERASEALLSTATUS, &status) && status == 0) {
      finished = true;
      break;
    }
    delay(20);
  }
  if (!finished) {
    Serial.println(F("erase: CTRL-AP ERASEALL did not report completion"));
    return false;
  }

  // The mass erase resets the target's debug state; re-establish before reading anything back.
  if (!swdConnect(false)) {
    Serial.println(F("erase: could not reconnect after mass erase"));
    return false;
  }

  // BLANK CHECK -- the postcondition that makes "erase: OK" mean something. Two passes, and the
  // split is a deliberate trade against hold time rather than laziness.
  //
  // Pass 1, one word per page across the whole device: catches the failure that actually happened
  // here, where the erase did not occur at all (84 non-blank pages). CTRL-AP ERASEALL is a single
  // atomic operation rather than a loop, so "some pages erased, others not" is not a mode it has.
  //
  // Pass 2, EVERY word of the regions that could leave a bootable or trusted remnant -- MBR,
  // SoftDevice, application, bootloader and the settings page. A stale application is the whole
  // reason this check exists.
  //
  // A full-word sweep of all 1 MB would add minutes to a hold performed by hand on pogo pins, and
  // losing contact mid-check is its own failure. If this ever runs on a fixed jig, widen pass 2.
  for (uint32_t page = 0; page < pages; page++) {
    uint32_t v = 0;
    uint32_t addr = page * g_pageSize;
    if (!memRead32(addr, &v)) {
      Serial.print(F("erase: blank check could not read 0x"));
      Serial.println(addr, HEX);
      return false;
    }
    if (v != 0xFFFFFFFF) {
      Serial.print(F("erase: NOT BLANK at 0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" = 0x"));
      Serial.println(v, HEX);
      return false;
    }
  }

  const uint32_t denseEnd = (pages * g_pageSize < 0x00100000) ? pages * g_pageSize : 0x00100000;
  for (uint32_t addr = 0; addr < denseEnd; addr += 4) {
    // Skip the gap between the end of the application region and the bootloader: nothing bootable
    // or trusted lives there, and pass 1 already sampled every page of it.
    if (addr >= 0x00047000 && addr < 0x000F4000) { addr = 0x000F4000 - 4; continue; }
    uint32_t v = 0;
    if (!memRead32(addr, &v)) {
      Serial.print(F("erase: blank check could not read 0x"));
      Serial.println(addr, HEX);
      return false;
    }
    if (v != 0xFFFFFFFF) {
      Serial.print(F("erase: NOT BLANK at 0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" = 0x"));
      Serial.println(v, HEX);
      return false;
    }
    if ((addr & 0x7FFF) == 0) {
      Serial.print(F("progress: blank check 0x"));
      Serial.println(addr, HEX);
    }
  }

  // UICR: only the words this tool ever writes. Reserved/implementation-defined UICR locations are
  // not required to read back 0xFFFFFFFF, so demanding that of the whole region would produce
  // false failures.
  const uint32_t uicrChecked[] = { 0x10001014, 0x10001018 };
  for (uint32_t i = 0; i < sizeof(uicrChecked) / sizeof(uicrChecked[0]); i++) {
    uint32_t v = 0;
    if (!memRead32(uicrChecked[i], &v)) return false;
    if (v != 0xFFFFFFFF) {
      Serial.print(F("erase: UICR NOT BLANK at 0x"));
      Serial.print(uicrChecked[i], HEX);
      Serial.print(F(" = 0x"));
      Serial.println(v, HEX);
      return false;
    }
  }
  return true;
}

// A single `flash <addr> <len>` call is always entirely within one region or
// the other in practice -- UICR (0x1000xxxx) and code flash (0x000xxxxx) are
// far enough apart that no real image's contiguous byte range spans both.
static bool eraseRegion(uint32_t startAddr, uint32_t lengthBytes) {
  if (startAddr >= UICR_BASE && startAddr < UICR_BASE + UICR_SIZE) {
    if (!nvmcSetConfig(NVMC_CONFIG_EEN)) return false;
    if (!memWrite32(NVMC_ERASEUICR, 1)) return false;
    if (!nvmcWaitReady()) return false;
    return nvmcSetConfig(NVMC_CONFIG_REN);
  }

  uint32_t firstPage = startAddr / g_pageSize;
  uint32_t lastPage = (startAddr + lengthBytes - 1) / g_pageSize;
  uint32_t totalPages = lastPage - firstPage + 1;
  if (!nvmcSetConfig(NVMC_CONFIG_EEN)) return false;
  for (uint32_t page = firstPage; page <= lastPage; page++) {
    uint32_t pageAddr = page * g_pageSize;
    if (!memWrite32(NVMC_ERASEPAGE, pageAddr)) return false;
    if (!nvmcWaitReady()) return false;
    if (totalPages > 1) {
      Serial.print(F("progress: erasing page "));
      Serial.print(page - firstPage + 1);
      Serial.print(F("/"));
      Serial.println(totalPages);
    }
  }
  return nvmcSetConfig(NVMC_CONFIG_REN);
}

// ---------------------------------------------------------------------------
// Serial command interface
// ---------------------------------------------------------------------------

// Internal-SRAM budget (PSRAM disabled). ~351KB free heap was measured right
// after a fresh boot, but a single contiguous 300KB block stopped being
// available after a long interactive session accumulated heap fragmentation
// (confirmed on the bench 2026-08-20: malloc(300KB) failed mid-session even
// though total free bytes were still high) -- Arduino's String-based command
// parsing is the likely source. Pulled back to a size with real margin below
// that failure, and see `restart` / `mem` (reports max contiguous alloc, not
// just total free) if this ever needs raising again.
static const size_t MAX_IMAGE_BYTES = 200UL * 1024UL;
static uint8_t *g_imageBuf = nullptr;

static void printHelp() {
  Serial.println(F("MSV2 SWD flasher commands:"));
  Serial.println(F("  id                 - connect (read-only) and report target identity"));
  Serial.println(F("  probe              - loop checking for SWD contact (for hand-held wiring)"));
  Serial.println(F("  halt               - hardware-reset target then halt the core"));
  Serial.println(F("  run                - SYSRESETREQ over SWD, then release the halted core"));
  Serial.println(F("  reset              - pulse nRESET only (no SWD)"));
  Serial.println(F("  rd <hexaddr>       - read one 32-bit word"));
  Serial.println(F("  rdblk <hexaddr> <declen> - bulk read, replies with declen raw bytes"));
  Serial.println(F("  eraseall_diag      - instrumented ERASEALL + blank check (DESTRUCTIVE)"));
  Serial.println(F("  wr <hexaddr> <hexval> - write one 32-bit word (raw, expert use)"));
  Serial.println(F("  erase CONFIRM      - mass erase entire flash"));
  Serial.println(F("  flash <hexaddr> <declen> - receive <declen> raw bytes, erase+program+verify at <hexaddr>"));
  Serial.println(F("  mem                - report free heap and largest contiguous block"));
  Serial.println(F("  restart            - soft-reboot this adapter (clears heap fragmentation)"));
  Serial.println(F("  help               - this text"));
}

static bool parseHex(const String &s, uint32_t *out) {
  if (s.length() == 0) return false;
  char *end = nullptr;
  unsigned long v = strtoul(s.c_str(), &end, 16);
  if (end == s.c_str()) return false;
  *out = (uint32_t)v;
  return true;
}

static void cmdId() {
  if (!swdConnect(true)) {
    Serial.println(F("id: connect failed"));
    return;
  }
  Serial.println(F("id: OK"));
}

// For hand-held-wire bring-up: retries a cheap connect check every ~150ms and
// only prints on a state change (contact made / contact lost), plus a
// heartbeat every ~3s so it's clear the loop is still alive. Send any
// character (e.g. hit Enter) to stop.
static void cmdProbe() {
  Serial.println(F("probe: watching for SWD contact. Send any character to stop."));
  bool wasConnected = false;
  uint32_t attempts = 0;
  while (true) {
    if (Serial.available()) {
      while (Serial.available()) Serial.read();
      Serial.println(F("probe: stopped"));
      return;
    }
    attempts++;
    uint32_t dpidr = 0;
    bool ok = quickIdRead(&dpidr);
    if (ok && !wasConnected) {
      Serial.print(F(">>> CONTACT MADE (attempt "));
      Serial.print(attempts);
      Serial.print(F(") DPIDR = 0x"));
      Serial.println(dpidr, HEX);
      wasConnected = true;
    } else if (!ok && wasConnected) {
      Serial.println(F(">>> contact lost"));
      wasConnected = false;
    } else if (!ok && (attempts % 20 == 0)) {
      Serial.print(F("probe: still no response (attempt "));
      Serial.print(attempts);
      Serial.println(F(")"));
    } else if (ok && (attempts % 20 == 0)) {
      Serial.print(F("probe: holding contact, DPIDR = 0x"));
      Serial.println(dpidr, HEX);
    }
    delay(150);
  }
}

static void cmdHalt() {
  pulseHardwareReset();
  if (!swdConnect(false)) {
    Serial.println(F("halt: connect failed"));
    return;
  }
  if (!haltCore()) {
    Serial.println(F("halt: core did not report S_HALT"));
    return;
  }
  Serial.println(F("halt: core halted"));
}

static void cmdRun() {
  if (!g_connected) {
    Serial.println(F("run: not connected, nothing to release"));
    return;
  }
  if (!systemResetRequest()) {
    Serial.println(F("run: SYSRESETREQ write failed; core state unchanged"));
    return;
  }
  // A successful AIRCR write is the one reset request. resumeCore() is only a best-effort release
  // of a debugger-held core after that same reset; it must never be replaced by a second reset.
  resumeCore();
  g_connected = false;
  Serial.println(F("run: core reset and resumed"));
}

static void cmdReset() {
  pulseHardwareReset();
  // This confirms only what the adapter drove. GPIO7 has no way to sense whether a wire actually
  // reaches the target nRESET pad; the 2026-09-07 guard-2 session lost time because this used to
  // print an unqualified success while that wire was absent.
  Serial.println(F("reset: nRESET pulse emitted (target connection not verified)"));
}

// Bulk read: `rdblk <hexaddr> <declen>` -> "rdblk: <n> bytes" then n bytes of raw binary.
//
// Why this exists (2026-09-07): `rd` reads ONE word per serial round-trip, which makes capturing
// a 132 KB application region ~33,000 round-trips. That made the pre-erase readback the coating
// plan requires -- "archive probe 4's running image, this is the last moment it can ever be
// recovered" -- effectively impossible, and it made forensics on probe 99's surviving application
// impossible too. Reads are non-destructive, so this is the safe half of the adapter.
//
// Raw binary out rather than hex text: 2x the bytes on a link that has already demonstrated it
// can stall, and the host side already knows how to frame a binary payload from `flash`.
static void cmdReadBlock(const String &addrArg, const String &lenArg) {
  uint32_t addr;
  if (!parseHex(addrArg, &addr)) {
    Serial.println(F("rdblk: usage: rdblk <hexaddr> <declen>"));
    return;
  }
  long len = lenArg.toInt();
  if (len <= 0 || (len & 3)) {
    Serial.println(F("rdblk: length must be a positive multiple of 4"));
    return;
  }
  if (addr & 3) {
    Serial.println(F("rdblk: address must be word-aligned"));
    return;
  }
  if (!g_connected && !swdConnect(false)) {
    Serial.println(F("rdblk: connect failed"));
    return;
  }

  Serial.print(F("rdblk: "));
  Serial.print(len);
  Serial.println(F(" bytes"));

  uint8_t out[256];
  size_t held = 0;
  for (long off = 0; off < len; off += 4) {
    uint32_t v = 0;
    if (!memRead32(addr + off, &v)) {
      // Flush what we have, then report. The host sees a short payload and knows to distrust it.
      if (held) Serial.write(out, held);
      Serial.flush();
      Serial.print(F("\nrdblk: read failed at 0x"));
      Serial.println(addr + off, HEX);
      return;
    }
    out[held++] = (uint8_t)(v & 0xFF);
    out[held++] = (uint8_t)((v >> 8) & 0xFF);
    out[held++] = (uint8_t)((v >> 16) & 0xFF);
    out[held++] = (uint8_t)((v >> 24) & 0xFF);
    if (held == sizeof(out)) {
      Serial.write(out, held);
      held = 0;
      yield();
    }
  }
  if (held) Serial.write(out, held);
  Serial.flush();
  Serial.println();
  Serial.println(F("rdblk: OK"));
}

// `eraseall_diag` -- instrumented ERASEALL that answers WHY a mass erase can report success while
// leaving flash intact (measured on probe 99, 2026-09-07: `erase: OK`, application survived).
//
// The suspicion is that nvmcWaitReady() returns on the FIRST poll where READY==1, which right
// after the ERASEALL write can still be the stale pre-erase value -- so massErase() then writes
// CONFIG=REN while a ~170 ms erase is in flight. This command does NOT print anything during the
// critical interval (serial I/O there would perturb the very timing under test); it records a
// trace in RAM and dumps it afterwards.
//
// Discriminating outcomes, decided before running it:
//   first sample READY=1 and a later sample READY=0  -> stale-READY race CONFIRMED
//   first sample READY=0 then returns to 1           -> race REFUTED, look elsewhere
//   READY never observed 0                           -> ERASEALL was never accepted
//   BUSY then READY seen but flash still not blank   -> completion reporting is not the issue
static void cmdEraseAllDiag() {
  if (!g_connected && !swdConnect(false)) {
    Serial.println(F("eraseall_diag: connect failed"));
    return;
  }
  if (!haltCore()) {
    Serial.println(F("eraseall_diag: halt failed"));
    return;
  }

  static const int TRACE_N = 64;
  uint32_t tUs[TRACE_N];
  uint8_t rdy[TRACE_N];
  bool ok[TRACE_N];
  int n = 0;

  uint32_t before = 0;
  bool beforeOk = memRead32(NVMC_READY, &before);

  if (!nvmcSetConfig(NVMC_CONFIG_EEN)) {
    Serial.println(F("eraseall_diag: CONFIG=EEN failed"));
    return;
  }

  const uint32_t t0 = micros();
  const bool wrOk = memWrite32(NVMC_ERASEALL, 1);

  // Capture DP CTRL/STAT IMMEDIATELY, before any helper clears sticky bits. MEM-AP writes are
  // posted: the SWD ACK only says the AP accepted the transfer, not that the downstream AHB write
  // completed. A peripheral rejection surfaces later as STICKYERR (bit 5) / STICKYORUN (bit 1) /
  // WDATAERR (bit 7). The very first READY read after this write has failed reproducibly across
  // two separate runs, which is what a posted-write fault looks like -- so read the evidence
  // rather than infer it from the failure.
  uint32_t ctrlStat = 0;
  const bool ctrlOk = dpRead(0x4, &ctrlStat);

  // Sample as fast as the link allows for up to 400 ms -- ERASEALL is ~170 ms, so a correct
  // trace should show READY low for a large part of that and then high again.
  while (n < TRACE_N && (micros() - t0) < 400000UL) {
    uint32_t v = 0;
    bool r = memRead32(NVMC_READY, &v);
    tUs[n] = micros() - t0;
    rdy[n] = (uint8_t)(v & 1);
    ok[n] = r;
    n++;
    if (n >= 8 && rdy[n - 1] == 1 && rdy[n - 2] == 1) {
      bool sawLow = false;
      for (int i = 0; i < n; i++) if (ok[i] && rdy[i] == 0) sawLow = true;
      if (sawLow) break; // busy observed and finished -- trace is complete
    }
  }

  // Deliberately NOT restoring CONFIG=REN yet: that write is the suspected culprit, so the trace
  // above must be captured with it absent.
  Serial.print(F("eraseall_diag: READY before CONFIG=EEN: "));
  if (beforeOk) Serial.println(before & 1); else Serial.println(F("read failed"));
  Serial.print(F("eraseall_diag: ERASEALL write "));
  Serial.println(wrOk ? F("acked") : F("FAILED"));
  Serial.print(F("eraseall_diag: DP CTRL/STAT right after the write: "));
  if (!ctrlOk) {
    Serial.println(F("read failed"));
  } else {
    Serial.print(F("0x"));
    Serial.print(ctrlStat, HEX);
    Serial.print(F("  STICKYERR="));
    Serial.print((ctrlStat >> 5) & 1);
    Serial.print(F(" STICKYCMP="));
    Serial.print((ctrlStat >> 4) & 1);
    Serial.print(F(" STICKYORUN="));
    Serial.print((ctrlStat >> 1) & 1);
    Serial.print(F(" WDATAERR="));
    Serial.println((ctrlStat >> 7) & 1);
  }

  // Discriminator: does CTRL-AP ERASEALL work where NVMC ERASEALL does not? On nRF52 the CTRL-AP
  // is AP index 1, with ERASEALL at 0x04 and ERASEALLSTATUS at 0x08. This is how Nordic's own
  // tooling performs a debugger-driven mass erase, and if it succeeds here that explains the whole
  // anomaly as an access-path issue rather than anything about NVMC state.
  Serial.println(F("eraseall_diag: trying CTRL-AP (AP1) ERASEALL as a discriminator..."));
  if (!apWrite(1, 0x04, 1)) {
    Serial.println(F("  CTRL-AP ERASEALL write FAILED"));
  } else {
    uint32_t st = 0;
    bool sawBusy = false;
    for (int i = 0; i < 40; i++) {
      if (apRead(1, 0x08, &st) && st != 0) sawBusy = true;
      delay(20);
    }
    Serial.print(F("  CTRL-AP ERASEALLSTATUS busy observed: "));
    Serial.println(sawBusy ? F("YES") : F("no"));
    uint32_t probe = 0;
    if (memRead32(0x00026000, &probe)) {
      Serial.print(F("  word at 0x26000 after CTRL-AP attempt: 0x"));
      Serial.println(probe, HEX);
    } else {
      Serial.println(F("  0x26000 unreadable after CTRL-AP attempt (expected if it reset the AP)"));
    }
  }
  Serial.print(F("eraseall_diag: "));
  Serial.print(n);
  Serial.println(F(" samples (us,ready):"));
  bool sawLow = false;
  for (int i = 0; i < n; i++) {
    if (ok[i] && rdy[i] == 0) sawLow = true;
    Serial.print(F("  "));
    Serial.print(tUs[i]);
    Serial.print(F(","));
    if (ok[i]) Serial.print(rdy[i]); else Serial.print(F("?"));
    if (((i + 1) % 8) == 0) Serial.println(); else Serial.print(F("  "));
  }
  Serial.println();
  Serial.print(F("eraseall_diag: BUSY (READY==0) observed: "));
  Serial.println(sawLow ? F("YES") : F("NO"));

  nvmcSetConfig(NVMC_CONFIG_REN);

  // Blank-check one word per 4 KB page across the whole 1 MB, plus the settings page.
  int notBlank = 0;
  uint32_t firstNotBlank = 0xFFFFFFFF;
  for (uint32_t a = 0; a < 0x100000; a += 0x1000) {
    uint32_t v = 0;
    if (memRead32(a, &v) && v != 0xFFFFFFFF) {
      if (!notBlank) firstNotBlank = a;
      notBlank++;
    }
  }
  Serial.print(F("eraseall_diag: non-blank pages (1 word/page sample): "));
  Serial.print(notBlank);
  if (notBlank) {
    Serial.print(F(", first at 0x"));
    Serial.print(firstNotBlank, HEX);
  }
  Serial.println();
  Serial.println(F("eraseall_diag: done"));
}

static void cmdRead(const String &arg) {
  uint32_t addr;
  if (!parseHex(arg, &addr)) {
    Serial.println(F("rd: usage: rd <hexaddr>"));
    return;
  }
  if (!g_connected && !swdConnect(false)) {
    Serial.println(F("rd: connect failed"));
    return;
  }
  uint32_t value = 0;
  if (!memRead32(addr, &value)) {
    Serial.println(F("rd: read failed"));
    return;
  }
  Serial.print(F("0x"));
  Serial.print(addr, HEX);
  Serial.print(F(" = 0x"));
  Serial.println(value, HEX);
}

static void cmdWrite(const String &addrArg, const String &valArg) {
  uint32_t addr, value;
  if (!parseHex(addrArg, &addr) || !parseHex(valArg, &value)) {
    Serial.println(F("wr: usage: wr <hexaddr> <hexval>"));
    return;
  }
  if (!g_connected && !swdConnect(false)) {
    Serial.println(F("wr: connect failed"));
    return;
  }
  if (!memWrite32(addr, value)) {
    Serial.println(F("wr: write failed"));
    return;
  }
  Serial.println(F("wr: OK"));
}

static void cmdErase(const String &arg) {
  if (arg != "CONFIRM") {
    Serial.println(F("erase: this destroys the entire target flash. Re-run as: erase CONFIRM"));
    return;
  }
  pulseHardwareReset();
  if (!swdConnect(false) || !haltCore()) {
    Serial.println(F("erase: connect/halt failed, aborting"));
    return;
  }
  Serial.println(F("erase: mass erasing..."));
  if (!massErase()) {
    Serial.println(F("erase: FAILED"));
    return;
  }
  Serial.println(F("erase: OK"));
}

static void cmdFlash(const String &addrArg, const String &lenArg) {
  uint32_t targetAddr;
  if (!parseHex(addrArg, &targetAddr)) {
    Serial.println(F("flash: usage: flash <hexaddr> <declen>"));
    return;
  }
  long len = lenArg.toInt();
  if (len <= 0 || (size_t)len > MAX_IMAGE_BYTES) {
    Serial.print(F("flash: length must be 1.."));
    Serial.println(MAX_IMAGE_BYTES);
    return;
  }
  if (!g_imageBuf) {
    g_imageBuf = (uint8_t *)malloc(MAX_IMAGE_BYTES);
    if (!g_imageBuf) {
      Serial.println(F("flash: out of memory allocating image buffer"));
      return;
    }
  }

  Serial.println(F("flash: send raw binary now"));
  // Chunked with an explicit ack after each chunk, not one continuous stream
  // -- timing-based fixes (yield() in the drain loop, paced host-side writes)
  // measurably helped but didn't fully eliminate transfers stalling partway
  // through on this board's native USB CDC (confirmed on the bench
  // 2026-08-20). An ack makes each chunk's receipt a fact the host waits on
  // instead of a timing assumption, so it's correct regardless of whatever
  // the underlying cause of the stalling was.
  static const size_t RECV_CHUNK = 512;
  size_t received = 0;
  while (received < (size_t)len) {
    size_t want = (size_t)len - received;
    if (want > RECV_CHUNK) want = RECV_CHUNK;
    size_t gotThisChunk = 0;
    uint32_t lastByteAt = millis();
    while (gotThisChunk < want) {
      if (Serial.available()) {
        g_imageBuf[received + gotThisChunk] = (uint8_t)Serial.read();
        gotThisChunk++;
        lastByteAt = millis();
      } else if (millis() - lastByteAt > 5000) {
        Serial.print(F("flash: timed out after "));
        Serial.print(received + gotThisChunk);
        Serial.println(F(" bytes"));
        return;
      } else {
        yield();
      }
    }
    received += gotThisChunk;
    Serial.print(F("chunkack "));
    Serial.println(received);
  }

  uint32_t sum = 0;
  for (size_t i = 0; i < received; i++) sum += g_imageBuf[i];
  Serial.print(F("flash: received "));
  Serial.print(received);
  Serial.print(F(" bytes, byte-sum checksum 0x"));
  Serial.println(sum, HEX);

  pulseHardwareReset();
  if (!swdConnect(true) || !haltCore()) {
    Serial.println(F("flash: connect/halt failed, aborting"));
    return;
  }

  size_t paddedLen = (received + 3) & ~((size_t)3);
  Serial.println(F("flash: erasing target region..."));
  if (!eraseRegion(targetAddr, paddedLen)) {
    Serial.println(F("flash: erase failed"));
    return;
  }

  Serial.println(F("flash: programming..."));
  if (!nvmcSetConfig(NVMC_CONFIG_WEN)) {
    Serial.println(F("flash: failed to enable write mode"));
    return;
  }

  // Batched instead of one full ready-poll per word: CSW already has
  // auto-increment enabled, so TAR only needs setting once per batch, and the
  // ~40us nRF52840 word-write time (per Nordic's spec) is comfortably shorter
  // than a single bit-banged DRW-write transaction even at this tighter
  // timing -- so back-to-back writes within a batch don't need to wait on
  // NVMC individually. This trades guaranteed-safe (poll every word, as
  // erase still does) for probably-safe-and-verify-checked: if timing ever
  // is marginal on some word, the verify pass below reads every word back
  // and will catch it as a mismatch rather than silently losing data.
  static const size_t WRITE_BATCH_WORDS = 64;
  size_t totalWords = paddedLen / 4;
  size_t wordsDone = 0;
  int lastPct = -1;
  bool writeOk = true;

  while (wordsDone < totalWords && writeOk) {
    size_t batchWords = totalWords - wordsDone;
    if (batchWords > WRITE_BATCH_WORDS) batchWords = WRITE_BATCH_WORDS;
    uint32_t batchAddr = targetAddr + wordsDone * 4;

    if (!apWrite(AHB_AP, AP_TAR, batchAddr)) {
      writeOk = false;
      break;
    }
    for (size_t i = 0; i < batchWords; i++) {
      size_t off = (wordsDone + i) * 4;
      uint32_t word = 0xFFFFFFFF;
      memcpy(&word, g_imageBuf + off, (off + 4 <= received) ? 4 : (received - off));
      if (!apWrite(AHB_AP, AP_DRW, word)) {
        writeOk = false;
        break;
      }
    }
    if (!writeOk || !nvmcWaitReady()) {
      Serial.print(F("flash: write failed at offset 0x"));
      Serial.println(wordsDone * 4, HEX);
      writeOk = false;
      break;
    }

    wordsDone += batchWords;
    int pct = (int)((wordsDone * 100) / totalWords);
    if (pct != lastPct) {
      Serial.print(F("progress: writing "));
      Serial.print(wordsDone);
      Serial.print(F("/"));
      Serial.print(totalWords);
      Serial.print(F(" words ("));
      Serial.print(pct);
      Serial.println(F("%)"));
      lastPct = pct;
    }
  }
  nvmcSetConfig(NVMC_CONFIG_REN);
  if (!writeOk) {
    return;
  }

  Serial.println(F("flash: verifying..."));
  bool ok = true;
  lastPct = -1;
  for (size_t off = 0; off < paddedLen; off += 4) {
    uint32_t expected = 0xFFFFFFFF;
    memcpy(&expected, g_imageBuf + off, (off + 4 <= received) ? 4 : (received - off));
    uint32_t actual = 0;
    if (!memRead32(targetAddr + off, &actual) || actual != expected) {
      Serial.print(F("flash: MISMATCH at offset 0x"));
      Serial.print(off, HEX);
      Serial.print(F(": expected 0x"));
      Serial.print(expected, HEX);
      Serial.print(F(", got 0x"));
      Serial.println(actual, HEX);
      ok = false;
      break;
    }
    int pct = (int)(((off + 4) * 100) / paddedLen);
    if (pct != lastPct) {
      Serial.print(F("progress: verifying "));
      Serial.print(off + 4);
      Serial.print(F("/"));
      Serial.print(paddedLen);
      Serial.print(F(" bytes ("));
      Serial.print(pct);
      Serial.println(F("%)"));
      lastPct = pct;
    }
  }

  if (ok) {
    Serial.println(F("flash: verify OK"));
  } else {
    Serial.println(F("flash: verify FAILED"));
  }
}

static void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp1 = line.indexOf(' ');
  String cmd = (sp1 < 0) ? line : line.substring(0, sp1);
  String rest = (sp1 < 0) ? "" : line.substring(sp1 + 1);
  rest.trim();

  if (cmd == "help") {
    printHelp();
  } else if (cmd == "id") {
    cmdId();
  } else if (cmd == "probe") {
    cmdProbe();
  } else if (cmd == "halt") {
    cmdHalt();
  } else if (cmd == "run") {
    cmdRun();
  } else if (cmd == "reset") {
    cmdReset();
  } else if (cmd == "erase") {
    cmdErase(rest);
  } else if (cmd == "flash") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) {
      Serial.println(F("flash: usage: flash <hexaddr> <declen>"));
    } else {
      cmdFlash(rest.substring(0, sp2), rest.substring(sp2 + 1));
    }
  } else if (cmd == "mem") {
    Serial.print(F("free heap: "));
    Serial.print(ESP.getFreeHeap());
    Serial.print(F(" bytes, largest contiguous block: "));
    Serial.print(ESP.getMaxAllocHeap());
    Serial.println(F(" bytes (this is what actually bounds `flash`'s image "
                      "buffer, not total free -- run `restart` if it's much "
                      "smaller than free heap)"));
  } else if (cmd == "restart") {
    Serial.println(F("restarting..."));
    delay(50); // let the line actually get out over USB CDC before reset
    ESP.restart();
  } else if (cmd == "rd") {
    cmdRead(rest);
  } else if (cmd == "rdblk") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) {
      Serial.println(F("rdblk: usage: rdblk <hexaddr> <declen>"));
    } else {
      cmdReadBlock(rest.substring(0, sp2), rest.substring(sp2 + 1));
    }
  } else if (cmd == "eraseall_diag") {
    cmdEraseAllDiag();
  } else if (cmd == "wr") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) {
      Serial.println(F("wr: usage: wr <hexaddr> <hexval>"));
    } else {
      cmdWrite(rest.substring(0, sp2), rest.substring(sp2 + 1));
    }
  } else {
    Serial.print(F("unknown command: "));
    Serial.println(cmd);
    printHelp();
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  // Native USB CDC's default RX ring buffer is small enough that a bulk
  // transfer (the `flash` command's binary payload) can outrun it and
  // silently drop bytes once full -- confirmed on the bench 2026-08-20:
  // receives were stalling out partway through transfers as small as ~350
  // bytes, well under anything a scheduling/yield fix alone should affect.
  // Must be called before begin().
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(1500); // let the host terminal actually attach before we print anything
  pinMode(PIN_NRESET, INPUT); // idle high-Z; only driven low during a reset pulse

  Serial.println();
  Serial.println(F("MSV2 ESP32-S3 SWD flasher ready."));
  Serial.println(F("SWCLK=GPIO5  SWDIO=GPIO6  nRESET=GPIO7  (target GND to ESP32 GND)"));
  printHelp();
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      handleLine(line);
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}
