# Receiver panic/exception reboots: stack exhaustion ruled out (2026-09-02)

**Build under test:** `MSV2 runtime build 2026-08-31 durable-cloud-outbox-v7-smooth-display`
**Device:** live receiver `10.0.0.35` / COM16

## Event

A spontaneous reboot was captured with an independent timestamp. A 2-second poller against
`/data` (running for an unrelated sensor investigation) recorded exactly one outage,
**23:42:03 -> 23:42:17 on 2026-09-01**, in 2,412 consecutive polls. `cloudRadioSlotDiag.atMs =
712501` read at ~23:54 puts boot at ~23:42:10, matching independently.

`/device` health at the time:

```
resetReason              : "panic/exception"
previousBoot.resetReason : "panic/exception"   wasIntentionalRestart: false
minFreeHeapEverBytes     : 14240
freeHeapNowBytes         : 74840
largestFreeBlockBytes    : 31732
```

Two consecutive unintentional panics. Not a brownout, not a watchdog, not an intentional restart.

**Confounder, disclosed:** that 2-second poller had been hammering `/data` for ~46 minutes
(~1,380 requests) before the crash. It may have induced this particular reboot rather than
merely observing it. It has since been stopped.

## Hypothesis tested and rejected: loopTask stack overflow

The reasoning was that `loopTask` (WebServer, display rendering, touch, WiFi maintenance) was
the only task in the system with **no stack telemetry**, ran on Arduino's default 8192-byte
stack, and hosted `sensorDataJson()`'s `StaticJsonDocument<4096>`. A FreeRTOS canary trip
surfaces as exactly this reset reason and would have been invisible to every existing metric.

Two premises turned out to be false.

**1. There was no 4x telemetry bug.** The existing code multiplied
`uxTaskGetStackHighWaterMark()` by `sizeof(StackType_t)`, which looked like an erroneous
words->bytes conversion on top of an API that already returns bytes. It is not: this port
defines `#define portSTACK_TYPE uint8_t` (`freertos/FreeRTOS-Kernel/portable/xtensa/include/
freertos/portmacro.h:88`), so `sizeof(StackType_t) == 1` and the multiply was a **no-op**.
Every previously recorded stack figure was correct. This was briefly believed to mean
`cloudSyncTask` had only 1,414 bytes of headroom; that number was an arithmetic error.

**2. That 4KB document was never on the stack.** This project builds against **ArduinoJson
7.4.3**, where `StaticJsonDocument` is a deprecated alias and `JsonDocument` allocates on the
**heap**. So `sensorDataJson()`'s 4KB lands on the one resource that actually is scarce.

## Measurements

After adding `loopTask` stack telemetry and flashing, under a **366-request `/data` stress
(zero errors) plus normal display/touch/cloud activity over 171 s**:

| task | size | free min | used | % used |
|---|---|---|---|---|
| loopTask | 12288 | 8680 | 3608 | 29% |
| cloudSyncTask | 16384 | 5648 | 10736 | 66% |
| bleScanTask | 8192 | 6804 | 1388 | 17% |

`cloudSyncTask`'s 5,648 free here versus 5,656 reported *before* the telemetry change
independently confirms the multiply was a no-op and that this task's margin is stable.

**No task is near its canary. Stack exhaustion is ruled out as the cause of these panics.**

## Changes kept

- `loopTask` stack telemetry in `/device`: `loopTaskStackHighWaterBytes`,
  `loopTaskStackFreeMinEverBytes`, `loopTaskStackSizeBytes`. Sampled from `loop()` and from
  the `/data` handler after its document exists.
- Configured stack sizes reported next to every high-water mark
  (`cloudTaskStackSizeBytes`, `bleTaskStackSizeBytes`), so margin is readable in one place.
- The `* sizeof(StackType_t)` removed as a clarity fix, documented as a no-op, not a bug.

## Changes reverted

- `loopTask` 8192 -> 12288 was reverted. It was justified by the disproven hypothesis,
  `loopTask` peaks at 3,608 bytes (~56% headroom on the stock 8192), and task stacks are
  allocated from the same heap that is this receiver's real constraint.

## Where this goes next

The remaining suspicion is transient internal-heap pressure or fragmentation from overlap
between web JSON/String construction on `loopTask` and TLS allocation on `cloudSyncTask`,
followed by an allocation failure that some caller mishandles.

Note a premise worth not repeating: `CLOUD_UPLOAD_MIN_SAFE_HEAP_BYTES = 40960` is **admission
control, not a reservation**. It does not prove the 14,240-byte low-water event happened
outside the guarded path - after the check passes, TLS and payload allocation can exceed one
previously measured upload, `loopTask` can allocate concurrently, and total free heap can stay
nonzero while no sufficiently large block is available.

Low heap alone should not normally panic - ArduinoJson and mbedTLS are both expected to
report allocation failure. The panic could equally be an unchecked failure, heap corruption
detected inside `malloc()`/`free()`, an assert/abort, or an unrelated invalid access. Those
have incompatible fixes.

**No further behavioural mitigation should be shipped until a real panic backtrace is
captured.** The next step is continuous serial capture against the exact flashed ELF
(`scripts/serial_capture.py --port COM16`), decoded to symbols. Verbose per-request logging
should NOT be added first; it perturbs heap fragmentation and timing enough to hide the fault.

## Unrelated pre-existing failure noticed

`node DisplayCenter/scripts/verify_display_card_layout.mjs` fails with "compact battery header
formatter is missing". This predates this work: in-flight uncommitted "smooth-display" changes
intentionally replaced `snprintf(out, outSize, "Batt: %s", percent)` with a direct
`formatBatteryPercent(batteryPct, out, outSize)` call without updating the verifier's expected
string. Someone should decide whether the verifier or the code is now wrong.
