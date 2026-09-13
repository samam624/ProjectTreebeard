# MSV3: two guards against a probe that can never be reached again — 2026-09-04

> **Superseded guard-2 implementation note (2026-09-08):** this document preserves the reasoning
> and early tests, but its GPREGRET2 design is not the shipping result. A real watchdog-hang test
> proved GPREGRET2 unusable as the counter in this stack. The linker-reserved `.noinit` replacement
> with bootloader 1.0.3 passed automatic DFU escape and BLE recovery on probe 99. Use
> `MSV3_FINAL_OTA_PRECOATING_HANDOFF_2026-09-08.md` and
> `MSV3_BOOT_GUARD_WATCHDOG_RETENTION_2026-09-08.md` for current status.

**Question behind this work:** can a probe be updated purely over Bluetooth without a small chance
that a bad image crashes in a way that never lets it connect back?

**No — not to zero.** The bootloader hands control to the application. Once an image is signed,
CRC-valid and boots, nothing downstream can help if it then dies before opening a radio window.
Dual-bank does not help (by activation the old image is already gone) and the signature does not
help (it is your image, correctly signed, just buggy). That residual class is irreducible.

The floor can be moved a long way down, though, and almost all of the distance is in the
application — deployable over BLE, no bootloader change, no SWD. These are the two layers.

## Guard 1 — a sensor fault must not close the radio

Before today:

```c
bool shouldAdvertise = measurementOk && (intervalElapsed || bigSwing);
```

`measurementOk` is false when the FDC fails to wake, fails to read over I2C, or reports `ERR_WD`.
So a dead sensor, a stuck bus, or water in the connector **silently closed the only channel
through which a coated probe can be reached or updated**. A hardware fault became an unrecoverable
probe.

It was latent, not firing. Across a week of history, every gap over 10 minutes except three
reboots showed ~70 s per wake-counter increment — the probe kept advertising and the *receiver*
missed it (worst case 87 consecutive advertisements unheard, 1h41m). Cheap to close anyway.

**Probe side.** Advertise on the interval regardless; mark the reading instead:

```c
bool shouldAdvertise = intervalElapsed || (measurementOk && bigSwing);
```

`bigSwing` still requires a valid reading — a garbage count must not trigger an off-cadence
advertisement — so an invalid reading goes out on the normal interval only. Flag bit5
(`ADV_FLAG_READING_INVALID`) is set, the count field is zeroed, and `lastAdvertisedRaw` is left
alone so a fault never becomes the swing baseline.

**Receiver side, and it is not optional.** The receiver parses by length and ignored flag bits
4–7, so without a matching change it would happily store `calibrateMsv3Moisture(0)` for a faulted
probe. It now:

- reads bit5, **plus** an independent plausibility band (5 M–40 M counts) rather than trusting the
  probe's own bit — the same belt-and-braces the temperature field already used;
- takes a liveness-only path that updates RSSI, battery, wake and last-seen and sets a
  `sensorFault` flag, but does **not** feed the stateful display smoother and does **not** enqueue
  a history row. A faulted probe is alive, reachable and updatable, and must look that way — it
  just must not contribute a number to the plant's history.

The liveness path sits before `smoothMoistureForDisplay()` deliberately: that filter is stateful,
so feeding it one fault would corrupt several subsequent good readings.

## Guard 2 — a boot-attempt counter

`initWatchdogSafety()` was already the first statement in `setup()`, so the 5 s watchdog is armed
before any risky init and turns a **hang** into a reset. `bootGuardCheck()` now runs immediately
after it: read a counter, and if it has reached `BOOT_GUARD_LIMIT` (5) boots without ever reaching
healthy operation, arm `GPREGRET = 0xA8` and reset into BLE DFU — the same mechanism
`ResetSafeBLEDfu` uses for a commanded jump. Otherwise increment and continue.

That covers every bug after the guard runs: sensor init hangs, BLE misconfiguration, sleep bugs,
bad thresholds. It cannot cover a crash in the first few instructions.

**Storage: `GPREGRET2`.** Survives a soft reset, cleared by power-on/brownout reset — exactly the
wanted behaviour. Watchdog and fault reset loops accumulate; a user power-cycling a probe, or a
solar node browning out overnight, gets a clean slate and is never pushed into DFU for it.
`GPREGRET` (id 0) is the bootloader's DFU magic and must not be reused; `GPREGRET2` (id 1) is
touched by neither the Adafruit core nor the bootloader — checked, not assumed. A magic nibble
(`0xB0 | count`) means whatever happened to be in the register is not read as a boot count.

**Probation is two complete advertising cycles, not "advertising started".** A build that
advertises once and then dies later in its first duty cycle must not be able to clear the guard.
The clear therefore happens after the OTA window and the connected hold, roughly 140 s of proven
healthy running.

**The counter is reset before DFU is armed.** Without that, a good image flashed afterwards would
boot, read a saturated count, and bounce straight back into DFU — turning the rescue into a trap.

### Low battery defers the escape — it does not cancel it

Entering DFU is not free: the bootloader advertises continuously where the application advertises
~5 s per 70 s, it does not sense, and **it does not time out**. `main.c` passes a 3 s timeout only
on the USB/UF2 branch; a GPREGRET OTA jump takes `bootloader_dfu_start(_ota_dfu, 0, false)`. So an
escaped probe sits in DFU until someone flashes it, and a false positive costs that probe's data.

The first implementation escaped unconditionally, arguing that a flat solar probe recovers with sun
while an unreachable probe never does, and that if it flattened in DFU the brownout would clear
`GPREGRET2` and bring the application back. **Review found the hole in that**, and it is worth
recording because the argument sounded good:

- A marginal supply can reset the MCU by a route that does **not** clear `GPREGRET2`. So
  brownout-driven resets can accumulate the counter and dump a probe whose firmware is perfectly
  fine into DFU.
- And because DFU has no timeout, "the sun comes up" does **not** bring it back. Voltage recovering
  without a register-clearing power collapse leaves it sitting in DFU indefinitely.

So the self-healing property the ungated design relied on is not reliable, while the false-positive
path is very real for a solar node.

The gate is therefore a **defer, not a veto**. Below `BOOT_GUARD_MIN_ESCAPE_MV` the counter
saturates and the boot continues; the decision is retaken on the next boot. Both outcomes are
wanted: if the firmware was fine and the resets were undervoltage, the next healthy day clears
probation and no false positive ever happens; if the firmware really is broken, the escape waits
until there is enough charge to be worth rescuing into.

Two details that matter:

- The check happens **only at the escape point**, not on every boot, so an ordinary boot still
  reaches the guard with nothing between it and `initWatchdogSafety()`.
- It **fails open**. An out-of-range or unavailable reading escapes rather than defers, because
  being unable to measure the battery must never be the reason a probe stays unreachable.

`BOOT_GUARD_MIN_ESCAPE_MV` is set to `OTA_MIN_BATTERY_MV` (3800). That is **provisional and shares
the OTA policy threshold rather than being derived from measurement** — the DFU-mode current, and
therefore how long a probe survives unattended in it, has not been measured. Revisit once it has.

### Reset-cause filtering is not possible today

> **WRONG, corrected 2026-09-06. The paragraph below is kept because it was acted on for two days,
> and its conclusion was carried into the pre-coating plan as a reason to rebuild the bootloader.
> It is not a bootloader problem at all — see the section immediately after it.**

`RESETREAS` reads `0x0` on every boot — after a watchdog reset and after `NVIC_SystemReset` alike.
The bootloader runs first and appears to consume it. So the counter counts *every* boot, including
the return boot after a commanded DFU jump. That is more conservative than ideal; probation clears
it within ~140 s of healthy running, which makes it tolerable. If the bootloader is ever rebuilt,
preserving `RESETREAS` for the application would let this be tightened.

### The reset cause was always available — 2026-09-06

The bootloader never consumed it. **The Adafruit core does, before `setup()` runs, and it already
saves the value for the application.**

```c
// bootloader src/main.c:307
//   "Note: RESETREAS is not clear by bootloader, it should be cleared by application upon init()"

// core cores/nRF5/wiring.c:35
void init( void ) {
  _reset_reason = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS |= NRF_POWER->RESETREAS;   // write-1-to-clear
  ...
// core cores/nRF5/wiring.c:72
uint32_t readResetReason(void) { return _reset_reason; }
```

`bootGuardCheck()` was reading the hardware register directly, after `init()` had already cleared
it, so it was guaranteed to read `0x0` on any board, with any bootloader. The corroborating
evidence was in this repository the whole time: the bring-up sketch calls the core accessor and has
always reported real causes — `RESETPIN` on the 2026-08-28 four-board bring-up, and `0x2 WATCHDOG`
mid-cycle confirmed via `wdtstatus`.

**Fixed in the application**, deployable over BLE, no bootloader change and no SWD:
`g_resetReason = readResetReason();`. `MSV3/scripts/arduino_msv3_probe.ps1` now fails the build on
any direct `NRF_POWER->RESETREAS` access under `MSV3/arduino/`, and the boot banner decodes the
cause by name so the guard-2 watchdog-hang test can *read* `WATCHDOG` on each successive reset
instead of inferring it.

**This is the third time this project has reached past a framework-mediated register**, and the
other two are in this same file's history: the first guard-2 build wrote `NRF_POWER->GPREGRET2`
from `loop()` with the SoftDevice up and reset every ~76 s, and the FDC `ERROR_CONFIG`/`DRDY`
family is the same shape. This instance was the quietest of the three — nothing crashed, a value
was simply always zero — which is why it survived two days as a documented hardware fact and came
close to justifying a bootloader rebuild it never needed.

**The counter still counts every boot, deliberately.** The obvious follow-on is to count only
`DOG`/`LOCKUP` so a commanded DFU jump stops consuming an attempt. Considered and rejected: `SREQ`
is ambiguous in the direction that matters, because a commanded jump is an `SREQ` but so is a crash
routed through the SoftDevice/`app_error` handler, which resets via `NVIC_SystemReset()`. Filtering
`SREQ` out would silently stop counting a whole class of real crash loops — the thing this guard
exists to catch — to save one attempt in five in a case that already self-corrects within ~140 s of
healthy running. Over-counting is the safe direction here; under-counting is not. Revisit only with
bench evidence, SWD attached, about what this application's fault handlers actually do on a hard
fault.

## The bench test caught the guard creating the crash loop it exists to prevent

Worth recording in full, because it is the sharpest possible argument for testing a safety net.

The first build **rebooted every ~76 s**, right after its second advertisement, and
`boot guard cleared` never printed. `bootGuardNoteHealthyAdvert()` runs from `loop()`, where the
SoftDevice is enabled, and it wrote `NRF_POWER->GPREGRET2` directly. Those registers are
SoftDevice-protected; the write faults. The comment on the sibling function said *"never call this
after the SoftDevice is enabled"* — and then it was called from `loop()`.

Fixed by routing through `sd_power_gpregret_clr/set(BOOT_GUARD_GPREGRET_ID, ...)`. On SVC failure
it decrements the probation counter and retries on the next cycle; it never falls back to a raw
write. `bootGuardCheck()` may still use the raw registers because it runs before
`Bluefruit.begin()`.

## Bench evidence

All four on probe 99, delivered over BLE.

| test | result |
|---|---|
| Healthy build, 300 s | **Zero reboots** (was one every 76 s). `boot guard cleared after 2 advertising cycles` printed once. Wake climbing 2→3→4→5 uninterrupted. No false positive. |
| Crash-loop build | `boot attempt 2 → 3 → 4 → 5` at ~21 s intervals. `GPREGRET2` retention across soft resets confirmed on hardware. |
| At the limit | App stopped booting, USB CDC vanished, BLE showed `AdaDFU` at `DB:AD:4D:9D:21:BB`. The escape fires and lands reachable. |
| Rescue | Flashed the good build over BLE: booted once, cleared the guard, **did not re-escape**, zero reboots in 240 s. Validates the counter-reset-before-arming. |
| Escape re-test *with* the defer gate | Still escapes at the bench's ~4.3 V, no defer line, counter 2→3→4→5 then DFU. The gate does not break the escape it guards. |
| Final code (`guards3`) | Zero reboots, guard cleared after 2 cycles. The bench board is left running this. |

The crash-loop build was constructed so its failure mode was safe: probation never clears and the
app self-resets after 15 s, but it is otherwise **fully functional and advertises every boot**. If
the escape had not fired, the board would have kept rebooting and stayed flashable. A build that
hangs before advertising would have been the realistic test and was deliberately not used — with
no SWD attached and no reset button on this PCB, a hung-but-valid application is exactly the
unrecoverable state this work exists to prevent.

## A transfer failure worth recording: the final-packet receipt

`guards3` failed to flash **three times, deterministically, at exactly 136,616 bytes sent — which
is its entire image size.** So every byte had gone out and `ReceiptGuard` was waiting on the final
packet receipt.

The mechanism: 136,616 bytes at 244 per packet is 560 packets, and 560 is an exact multiple of both
PRN 4 and PRN 16. When the last packet completes a PRN window, `dfu_lib` waits for one more receipt
— and that receipt races the bootloader's end-of-image flush. When it does not (the previous build
was 561 packets), it goes straight to Receive Firmware Image, which has a far more generous
timeout.

Confirmed by prediction: PRN 32 gives 17.5 windows for the same image, and it flashed first try.

    PRN  4: 560 packets -> 140.00 windows  EXACT (races the flush)
    PRN 16: 560 packets ->  35.00 windows  EXACT (races the flush)
    PRN 32: 560 packets ->  17.50 windows  partial (skips to completion cmd)

**Fixed.** `ReceiptGuard` now treats a late receipt as a desync only while there is still image
left to stream. Once `sent` reaches the full image size the guard's whole rationale is spent —
there is nothing further to stream, and the only thing after that point is Receive Firmware Image,
whose status, CRC and P-256 signature are the real integrity boundary and are all still enforced.
The wait is noted and counted, and the transfer is allowed to proceed to the bootloader for
adjudication.

The relaxation is deliberately narrow, and the tests pin every edge of it: one packet short of the
end still aborts, halfway still aborts, a **wrong-offset** receipt at the end still aborts (this is
about a *missing* receipt, never a wrong one), and with no image size known nothing is relaxed at
all.

Proven on the exact failing case — same 136,616-byte image, same PRN 16 that failed 3/3:

```
Uploading 136616 bytes...
  final packet-receipt was late at 136616/136616 bytes -- the whole image is sent, so this is
  the bootloader's end-of-image flush, not a desync. Letting Receive Firmware Image adjudicate.
Verifying Upload... Validating... Activating & Resetting... DFU Complete.
Transfer completed in 0m 37s, 3.62 kB/s.
```

The board came back healthy: zero reboots, boot guard cleared after 2 advertising cycles.

Three other aborts today (at 113 KB, 105 KB and 19 KB sent) were mid-transfer rather than at the
end. Those are at window boundaries too, but only because every receipt is — they look like genuine
latency and are **not** explained by the above. Not chased.

## Guard 1, proven end to end — 2026-09-05

The receiver came back on a USB port, so both remaining gaps closed.

**Receiver deployed.** Flashed to COM16. The diff is purely additive (65 insertions, 0 deletions)
and inert for existing traffic: `readingValid` defaults true and the check lives only inside the
MSV3-diagnostics branch. Confirmed live — all five sensors ingesting, cloud HTTP 200, and sensor 3
(an MSV2 node whose `rawFdcCounts` is legitimately 0) still stored normally, which is the
compatibility case that would have broken had the plausibility band been applied too broadly.

**Fault injected deterministically.** A bench build forced `measurementOk = false` for the first
four minutes, placed immediately after the real FDC call so packet construction, the advertise
decision and scheduling all stayed production-identical — only the verdict differed. Sensor ID 12
was used because `MAX_SENSOR_ID` is 16, so the bench board's usual 99 is rejected by the receiver
before it reaches any of this code.

Receiver log across one boot:

```
00:27:38  FAULT   sensor 12 reported a FAILED measurement (wake 1); still reachable, reading not stored
00:28:48  FAULT   sensor 12 reported a FAILED measurement (wake 2); ...
00:29:58  FAULT   sensor 12 reported a FAILED measurement (wake 3); ...
00:31:08  FAULT   sensor 12 reported a FAILED measurement (wake 4); ...
00:32:18  STORED  sensor 12 wake 5  rawFdcCounts=20419034
00:33:28  STORED  sensor 12 wake 6
00:34:38  STORED  sensor 12 wake 7
```

And the decisive half — the cloud:

| time | wake | raw_fdc |
|---|---:|---:|
| 00:26:56 | 8 | 20,418,817 |
| *00:27:38 – 00:31:08* | *1–4* | **no rows at all** |
| 00:32:18 | 5 | 20,419,034 |

**Zero history rows across the whole fault window**, a clean gap covering exactly the four faulted
advertisements, and rows resuming the instant the probe recovered. What it proves together:

- an actual invalid packet crossed the BLE boundary and the *production* receiver executed the
  liveness-only branch;
- good → fault → repeated fault → good recovery, in one run;
- the failure cadence is right: four faults at exactly 70 s, so a faulted probe neither floods
  advertisements nor stops them — it stays reachable on its normal schedule, which is the entire
  point;
- `sensorFault` asserts during the fault and clears on the first valid frame.

Afterwards the bench probe was restored to the clean sensor-99 build and the eight sensor-12 test
rows were deleted from `msv2_sensor_history`; the five real sensors are untouched.

## Shipped to probe 4 — 2026-09-05

First field deployment, guard 1 only. `ENABLE_BOOT_FAILURE_GUARD` is a staging flag, set false, so
this image carries the advertise-on-fault change without guard 2 — the watchdog-hang path is still
unproven and gates the combined binary.

Probe 5 was not a candidate and cannot be: it is still on the stock Adafruit bootloader with
pre-OTA firmware and exposes no DFU service, so BLE cannot reach it at all. `MSV3/fleet.json`
records it as `ota_capable: false` and the flasher refuses it. It needs the USB
SoftDevice+bootloader migration first.

Sequence, all over the radio:

1. Pre-flight scan: 3984 mV, gate open, −71 dBm, wake advancing.
2. `--preflight-only`: signature verified against the key in `msv3-1.0.1`, device type `0x3356`,
   identity `MSV3-MOISTURE-004` matched to probe 4.
3. First transfer **aborted mid-image at 117,120 of 132,368 bytes** — a genuine receipt desync at
   −71 dBm, not the final-window case. The probe landed in BLE recovery exactly as designed.
4. Retry at `--prn 4` completed: 1 m 39 s, 1.31 kB/s.
5. Verified back on air: `MSV3-MOISTURE-004` at its registered address, 3983 mV, flags `0x1c`,
   wake advancing, gate open.
6. **Build read back over BLE: `2026-09-05-guard1`.** Serial logging is off in this image, so the
   Device Information Service added earlier the same day is the only thing that could have
   answered that — its first real use on a field probe.
7. Receiver ingesting sensor 4 normally, **zero false faults**.

Step 3 is the useful part of this record: a live field probe went into recovery on a marginal link
and came back with one retry, which is the property the whole interrupted-transfer campaign was
about. At −71 dBm expect to need `--prn 4`.

## Still not tested

- **Guard 1 assumes the FDC failure modes set `measurementOk = false`.** They do for a failed wake,
  a failed I2C read and `ERR_WD` — that is what the injection exercised. A hard I²C *hang* would
  not reach that variable at all and relies on guard 2 instead.
- **A true watchdog-hang escape for guard 2.** The crash-loop test self-reset rather than hanging,
  deliberately: with no SWD attached and no reset button on this PCB, a hang before advertising is
  the very unrecoverable state this work exists to prevent. That gates a combined guard-1 +
  guard-2 field binary and needs SWD standing by.

Independent review landed at both checkpoints. Planning contributed the magic-nibble saturating
counter, two-cycle probation, and the independent receiver-side plausibility check. Verification
overturned the ungated-escape decision, on the argument recorded above. It also listed what should
block guard 1 from the field, which is the section above this one, and noted that a **true
watchdog-hang test** — an infinite loop before advertising, five watchdog resets, AdaDFU entry,
BLE rescue — has still not been run. The self-reset test proved the `GPREGRET2` mechanics but not
the watchdog path specifically. That gates a combined guard-1 + guard-2 field binary and needs SWD
standing by, because a hang before advertising is the unrecoverable state itself.

## Where this leaves the original question

| layer | covers |
|---|---|
| bench-validate every image on probe 99 first | "this build does not boot" — cheapest and strongest |
| guard 1 | a sensor fault taking the radio down with it |
| guard 2 | any crash or hang after the first instructions of `setup()` |
| *(needs bootloader + SWD)* a boot counter in the bootloader | even an app that hardfaults immediately |

With the first three, the residual is *an image that crashes in the first few instructions and was
never bench-tested*. Close to unreachable, not zero. Add the bootloader-side counter and you would
need a bootloader bug or hardware death. The realistic target is a fleet where SWD-requiring
failures are dominated by hardware failure rather than firmware, and that is now within reach.
