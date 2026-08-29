# MSV3 Bring-up Firmware Hang: LFCLK/RTC1 Root Cause

**Date:** 2026-08-21
**Status:** RESOLVED AND REPO-ENFORCED. Verified with 900 consecutive I2C+USB samples (400 + 500, two back-to-back
runs) with zero hangs, on the real MSV3 probe PCB. Every attempt before the fix died at a
strikingly consistent ~115-122 iterations, with no exceptions across many repeated tests.
**Scope:** `arduino/MSV3_Probe_Bringup_Test/`, and by extension any future nRF52840 sketch on a
crystal-less board (E73-2G4M08S1C, or any future custom MSV3/MSV2 nRF52840 board) built on the
`adafruit:nrf52` Arduino core.

## Symptom

During bring-up testing of the real MSV3 probe PCB, streaming I2C reads from the FDC2214 sensing
chip (`fdcstream <n>`, ~150ms/sample: read the chip + print a line over USB serial) would freeze
solid after almost exactly ~115-122 samples, every single time, regardless of sample count
requested, regardless of whether USB output was even present (an isolation test doing pure I2C
reads with no per-sample serial output hung at exactly the same point). The board stopped
responding to everything -- no new serial output, no response to commands, no LED changes -- and
required a full physical unplug/replug to recover. A properly configured, verified-running
hardware watchdog did not recover it either.

## Investigation: four real bugs found and fixed, none of which was the cause

Before finding the actual root cause, this investigation found and fixed four genuinely real
issues. Each is worth keeping fixed on its own merits, but **none of them changed the failure
point at all** when tested individually:

1. **Heap fragmentation.** The original sketch built each output line via Arduino `String`
   concatenation inside the streaming loop -- many small heap allocations per iteration, a classic
   embedded heap-fragmentation risk over hundreds of iterations. Rewrote the entire output path to
   fixed-size `char` buffers + `snprintf`, eliminating heap allocation from the hot path entirely.
   **Result: hang persisted at the same iteration count.**
2. **Wire library's unbounded I2C wait loops.** Direct inspection of the installed core's
   `Wire_nRF52.cpp` found 8 separate `while(!EVENTS_X);` polling loops with **zero timeout** --
   two of them (`EVENTS_STOPPED`/`EVENTS_SUSPENDED`) didn't even check the error flag as a fallback
   exit condition. This is a real, confirmed defect in the vendor library. Patched the installed
   file (`Wire_nRF52.cpp`, in the `adafruit:nrf52` core's `libraries/Wire/`) with a
   `TWIM_WAIT_TIMEOUT_MS`-bounded wrapper around every one of those waits. **Result: hang persisted
   at the same iteration count** -- a later forensic breadcrumb (below) proved the hang was never
   actually inside a Wire call to begin with, so this fix, while correct, was never going to help.
3. **Watchdog HALT misconfiguration.** `NRF_WDT->CONFIG` was set with `WDT_CONFIG_HALT_Pause`
   (pauses the watchdog's countdown while the CPU is halted by an attached debugger) instead of
   `WDT_CONFIG_HALT_Run`. Real bug -- an active SWD debugging session could have suppressed
   recovery -- but the hangs were also occurring with no debugger ever attached, so this wasn't the
   cause either.
4. **FreeRTOS tickless idle.** A direct SWD register read during a live hang showed
   `SysTick->CTRL.ENABLE = 0`, and `FreeRTOSConfig.h` confirmed `configUSE_TICKLESS_IDLE = 1`.
   Disabled it (`configUSE_TICKLESS_IDLE = 0` in the installed core's `freertos/config/
   FreeRTOSConfig.h`) to force an always-on tick, suspecting the CPU was sleeping and never being
   woken. **The hang still occurred at the same ~121 mark.** This negative result was the key clue
   that the SysTick reading itself was misleading.

## Root cause

Reading `cores/nRF5/freertos/portable/CMSIS/nrf52/port_cmsis_systick.c` directly (rather than
continuing to reason about it secondhand) settled the question: **despite the filename, this
core's FreeRTOS port uses RTC1 as its tick source, unconditionally, regardless of
`configUSE_TICKLESS_IDLE`.** `vPortSetupTimerInterrupt()` configures and starts RTC1
(`portNRF_RTC_REG`) as the tick generator every time, with no tickless-idle guard around that
core setup at all -- the `configUSE_TICKLESS_IDLE` macro only gates the *optional*
`vPortSuppressTicksAndSleep()` sleep function. This means:

- `delay()`/`millis()` on this core run off RTC1 (an LFCLK-driven peripheral), **not** ARM SysTick,
  ever -- contradicting what the sketch's own top-of-file comment previously (incorrectly)
  claimed.
- The earlier `SysTick->CTRL.ENABLE = 0` finding was a complete red herring: this port never uses
  SysTick at all, so it reads disabled by default, all the time, on every boot -- not evidence of
  anything going wrong.
- Fix #4 above never touched the actual tick mechanism, which is why disabling tickless idle made
  no difference.

FreeRTOS's scheduler -- and therefore RTC1 -- starts **before** Arduino's `setup()` ever runs.
`setup()`/`loop()` execute as a FreeRTOS task, after the scheduler has already configured and
started RTC1 against whatever LFCLK state existed at that point (the stock `pca10056` variant's
`USE_LFXO` selection -- non-functional on this board, since no 32.768kHz crystal is populated;
`P0.00`/`P0.01` are repurposed elsewhere per the real schematic netlist).

An earlier fix in this same bring-up session had added a hardware watchdog for unrelated
robustness reasons, and its init function did:

```cpp
NRF_CLOCK->TASKS_LFCLKSTOP = 1;
NRF_CLOCK->LFCLKSRC = (CLOCK_LFCLKSRC_SRC_RC << CLOCK_LFCLKSRC_SRC_Pos);
NRF_CLOCK->TASKS_LFCLKSTART = 1;
```

as literally the first lines of `setup()` -- stopping and restarting the LF clock **out from
under an already-running RTC1**. This corrupted RTC1's tick generation without crashing anything
immediately: I2C reads, USB output, everything else kept working normally for the first ~115-122
iterations. But `delay(150)` between each streaming sample depends on that same RTC1-driven tick,
and once its state had drifted far enough, `delay()` simply stopped returning. A forensic
breadcrumb added late in the investigation (stamping the exact I2C call site + a running call
counter into fixed SRAM addresses, readable live over SWD without halting the target) confirmed
this precisely: at the moment of a hang, the counter showed exactly 120 complete samples (360
register reads + `fdcInit()`'s 5 writes = 365 total calls, all cleanly completed) with the last
recorded step being a **successful** I2C read completion -- the CPU was stuck *after* a clean I2C
return, consistent with being stuck inside the following `delay(150)`, not inside I2C at all.

Why the (confirmed armed, confirmed running, correctly configured after fix #3) hardware watchdog
didn't recover this is not fully explained mechanistically -- possibly some interaction between
the corrupted RTC1 state and the watchdog's own LFCLK dependency, not conclusively determined --
but is moot now that the actual bug is fixed at the source.

## The fix

1. **`variants/pca10056/variant.h`** (in the installed `adafruit:nrf52` core): changed
   `#define USE_LFXO` to `#define USE_LFRC`. This makes `wiring.c`'s own `init()` -- which runs
   before FreeRTOS/RTC1 ever start -- select the RC clock source correctly from the very first
   boot instruction, instead of the sketch trying to fix it up after FreeRTOS has already claimed
   the clock.
2. **`MSV3_Probe_Bringup_Test.ino`**: `initWatchdogSafety()` no longer touches
   `TASKS_LFCLKSTOP`/`LFCLKSRC`/`TASKS_LFCLKSTART` at all. It now only *verifies*, via
   `NRF_CLOCK->LFCLKSTAT`, that the variant's boot-time RC selection actually took effect, before
   arming the watchdog.
3. **Portable enforcement added after the manual edit was found missing again:**
   `scripts/arduino_msv3_probe.ps1` now repairs/verifies the active installed variant and always
   performs a clean compile. The sketch has an independent preprocessor guard that rejects an
   LFXO build. `CLAUDE.md` requires the wrapper, and `docs/KNOWN_ISSUES.md` is the portable index.
   This matters because files under Arduino15 are outside Git and do not travel with a clone.

## Verification

`fdcinit` + `fdcstream 400` (400 samples), immediately followed by `fdcstream 500` (500 more
samples) on the same boot: **900/900 samples, zero failures, zero watchdog resets, fully
responsive throughout** (confirmed via `i2cdebug`: `i2cCallCount=1205` exactly matching 5 init
writes + 400x3 reads = 1205 at the first checkpoint, continuing cleanly from there). Every
previous test, across four different fix attempts, had died by iteration ~122 without exception.

## Durable lessons

- **Never reconfigure `NRF_CLOCK->LFCLKSRC`/`TASKS_LFCLKSTART`/`TASKS_LFCLKSTOP` from inside a
  sketch's `setup()` on this core.** By the time `setup()` runs, FreeRTOS has already started and
  configured RTC1 against the current clock state -- touching it there is inherently unsafe. The
  correct place to select the LF clock source for a crystal-less board is the **board variant**
  (`variant.h`'s `USE_LFXO`/`USE_LFRC`), read by `wiring.c`'s `init()` before FreeRTOS ever starts.
  If a future MSV2/MSV3 nRF52840 sketch needs this on a board with no dedicated variant yet, build
  one (copy the closest existing variant, change the macro) rather than patching the clock at
  runtime.
- **A hardware watchdog confirmed armed and running via direct register readback is not proof it
  will actually recover a given hang.** This session had a watchdog that passed every
  register-level check available and still didn't save the board, because the underlying problem
  was entangled with the same clock domain the watchdog itself depended on. Verify recovery with a
  real stress test, not just the arming registers.
- **A remarkably consistent failure point across totally different fixes is itself a diagnostic
  signal.** Four different code changes (heap, Wire timeouts, watchdog config, tickless idle) each
  failed to move a hang that occurred at ~115-122 iterations every time -- that consistency argued
  against anything that only changes *how much load* is on a resource (heap, USB buffer, I2C
  timing) and toward something that breaks deterministically after a fixed amount of elapsed
  time/ticks, which is exactly what a corrupted tick generator produces.
- **When two independent debugging efforts disagree, verify the load-bearing factual claims
  directly against source**, not by reasoning about which analysis sounds more authoritative. The
  decisive step here was actually reading `port_cmsis_systick.c`'s real implementation, not further
  argument about what it probably did.

## Addendum (same day, second machine): the variant.h fix doesn't travel with the repo

This fix lives entirely in `variants/pca10056/variant.h` inside the **installed Arduino core**
(`AppData\Local\Arduino15\packages\adafruit\hardware\nrf52\1.7.0\...` on Windows) — a machine-local
file, not tracked by git, not part of this repository. Reopening this project on a different
machine (or after a core reinstall/update on the same machine) silently reverts to the stock
`#define USE_LFXO`, with no error or warning of any kind.

Symptom this produces is **different** from the original RTC1/delay() hang above, and easy to
mistake for a new bug: `blestart`/`blecycle` (anything that reaches `Bluefruit.begin()`) hangs and
watchdog-resets every time, while everything else — `wdtstatus`, `fdcstream`, `sleep`/`sleepbench`,
plain boot — works fine, including `wdtstatus` reporting `LFCLKSTAT: running=YES src=RC`. That
last part is what makes it a good trap: the plain application-level clock really is running
correctly on RC at boot (this core's own init handles that fine, or the already-flashed
bootloader leaves it that way), so it looks like the LFCLK question is already settled. It isn't —
`Bluefruit.begin()` hands the SoftDevice its own separate clock config, compiled from the same
`USE_LFXO`/`USE_LFRC` macro (`bluefruit.cpp`'s `begin()` is `#if defined(USE_LFXO) ... #elif
defined(USE_LFRC)`), and the SoftDevice takes over clock management once enabled. If that compiled
config says XTAL and this board has no crystal, enabling the SoftDevice is where it actually hangs
— nothing before that point ever touches it.

Confirmed exactly this on a second machine on 2026-08-21: `variant.h` still had the stock
`USE_LFXO`, `blestart` hung and watchdog-reset every single time (reproduced with the battery
fully disconnected too, ruling out a power-wiring red herring that looked plausible at first),
`wdtstatus` showed clean `src=RC` throughout. Re-patched to `USE_LFRC`, cleared the Arduino build
cache (`AppData\Local\arduino\sketches`, so the stale `bluefruit.cpp.o` compiled against the old
macro couldn't be reused), rebuilt, reflashed — `blestart` came up clean immediately after.

**Portable resolution:** the repository now owns the enforcement even though the upstream variant
file remains machine-local. `scripts/arduino_msv3_probe.ps1` repairs/verifies the newest installed
Adafruit nRF52 PCA10056 variant and forces a clean compile; the sketch separately refuses to
compile unless `USE_LFRC` is active and `USE_LFXO` is absent. `CLAUDE.md` requires that wrapper for
MSV3 work. A fresh clone therefore brings both the repair and a fail-closed check, and an Arduino
core reinstall is corrected on the next guarded build. See `docs/KNOWN_ISSUES.md` for the portable
index.

## Current external-core state

The installed core is back to `configUSE_TICKLESS_IDLE=1`. The earlier `0` setting was a discarded
diagnostic experiment, not part of the fix. Likewise, the temporary local `Wire_nRF52.cpp` timeout
patch did not address this failure and is not a required clone/setup mutation. Their current
portable status is recorded in `docs/KNOWN_ISSUES.md` so future investigations do not confuse
experimental machine-local edits with required fixes.
