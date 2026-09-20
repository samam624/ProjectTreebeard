# MSV3 guard 2: watchdog-reset retention failure and replacement

**Date:** 2026-09-08  
**Hardware:** probe 99  
**Result:** the GPREGRET2 guard failed the real watchdog-hang acceptance test. Its linker-reserved
`.noinit` replacement then passed the same end-to-end test on probe 99 with bootloader 1.0.3:
successive five-second watchdog boots ended in automatic BLE DFU, and a healthy signed image
recovered the probe without SWD or a power cycle. Guard 2 remains off on field probes 4 and 5
because their current bootloaders do not reserve its retained-RAM address; it may be enabled in
their final applications only after bootloader 1.0.3 is installed.

## What the test proved

The deliberately hung `2026-09-07` image armed the five-second WDT, ran `bootGuardCheck()`, then
stopped before BLE advertising without feeding the watchdog. Every captured application instance
reported:

```text
[HANGTEST] boot attempt 1 of 5; RESETREAS=0x2 (WATCHDOG )
```

SWD halted one of those instances and read `GPREGRET2 = 0xB1`. Therefore the running application
did write its magic plus count 1. The next application instance nevertheless began from count 0
and wrote `0xB1` again. Across dozens of watchdog resets, the count never reached 2 and could
never reach the escape limit 5.

That establishes a narrow fact: **the value written by one application instance is gone by the
time the next application instance reads it.** It does not establish which layer is responsible.
The nRF52840 product specification describes GPREGRET2 as retained through reset. The visible
Adafruit 0.11.0 bootloader source reads/clears GPREGRET only, not GPREGRET2, and the visible Arduino
core contains no GPREGRET2 write. The MBR/SoftDevice path is binary, and no observation was taken
between reset assertion and application entry. Hardware clearing, binary startup, or another
early overwrite therefore remain hypotheses, not findings.

This distinction does not change the product verdict: GPREGRET2 is not a usable watchdog-loop
counter in the shipping stack.

## What went wrong in the earlier work

The earlier safe crash-loop image used `NVIC_SystemReset()`. Its count advanced, and that result was
generalized to watchdog reset even though the true hang/WDT test was explicitly still outstanding.
The document then said GPREGRET2 retention was "confirmed on hardware" without limiting that claim
to the software-reset route. The test gate was correct; the conclusion was promoted past the gate.

The first SWD recovery helper had a separate two-reset error. It wrote AIRCR/SYSRESETREQ directly
and then sent the adapter's `run` command, but `run` itself also writes AIRCR/SYSRESETREQ. The first
reset could enter DFU and consume its GPREGRET magic; the second could immediately launch the valid
but hung application again. `scripts/swd_enter_dfu.py` now sends only `run` and checks its reply.

The adapter's physical `reset` command also printed success unconditionally. GPIO7 was not wired
to target RESET during this session, so that output described only the adapter pin transition, not
a target reset. The message now says the target connection is unverified. Wire GPIO7 to RESET
before the next SWD session.

## Replacement design

Guard state is now two inverse 32-bit words in `.msv3_boot_guard_retained` at `0x2003FFF8`:

- the application uses tracked linker script `MSV3/linker/msv3_nrf52840_s140_v6.ld`;
- the final eight RAM bytes are removed from ordinary application RAM and exposed only as a
  `NOLOAD` section;
- bootloader patch `msv3-1.0.3-boot-guard-retained-ram.patch` removes the same eight bytes from the
  bootloader RAM/stack arena;
- the Arduino wrapper installs the tracked application linker into the pinned core, selects it
  explicitly, and rejects a production link unless the map places exactly eight bytes at the
  fixed address;
- the inverse word makes random or partially written state invalid. Invalid state fails open at
  count 0;
- only watchdog, software-request, and lockup reset causes carry a valid count. Power-on/brownout
  (`RESETREAS == 0`), pin reset, and System OFF wake start clean;
- healthy probation clears RAM directly. It no longer touches a SoftDevice-protected POWER
  register from `loop()`.

Bootloader 1.0.2 does **not** reserve this word, so guard 2 must remain disabled on it even though
the application compiles with the new section. Bootloader 1.0.3 is the first compatible loader.

## Acceptance run, 2026-09-08

1. **PASS:** bootloader 1.0.3 was installed on probe 99 by verified mass erase + five verified SWD
   segments. `--no-reset` was used because GPIO7 was deliberately left unwired; a manual power
   replug started the target afterwards.
2. **PASS:** signed v65552 (`2026-09-07-rescue52`) installed over BLE, re-seeded the erased
   anti-rollback floor, advertised valid moisture data, and reported its exact build through DIS.
3. **PASS:** signed v65553 (`2026-09-08-guard2ram-hang`) installed over normal buttonless BLE OTA.
4. **PASS, behavioral evidence:** COM9 re-enumerated on the expected five-second watchdog cadence
   and then stopped. The serial logger did not capture the text banners, so this run does not claim
   direct observation of the individual numeric counter values or RESETREAS fields.
5. **PASS, decisive outcome:** AdaDFU appeared automatically at the registered recovery address
   `DB:AD:4D:9D:21:BB`, without an SWD write, reset cable, or power cycle. The guarded application
   is the only code in this test that can request that transition, and its inverse-word state must
   validate at count 5 before it does so. This proves watchdog-loop retention and escape.
6. **PASS:** signed v65554 (`2026-09-08-guard2ram-rescue`) installed from that automatic recovery
   state. DIS then reported the exact build, 4310 mV, and an open OTA gate.
7. **PASS:** a 150-second fleet scan observed two distinct advertising cycles with the wake
   counter advancing by one. The application remained healthy beyond its two-cycle probation.
8. **PASS:** after a manual cold power replug, probe 99 returned as the application at its
   registered address with valid sensor data and OTA open; it did not falsely enter DFU.
9. **NOT RUN, explicitly waived for pre-coating:** a dedicated nRESET-pin test. The operator
   elected not to wire GPIO7 and used manual power replugs instead. The field-relevant watchdog
   escape and cold-power paths both passed; this omission is recorded but is not a rollout blocker.

Artifacts and evidence:

- `dumps/guard2/retained-ram-watchdog-acceptance.log`
- `dumps/guard2/probe99-v65554-probation.json`
- `dumps/guard2/probe99-v65554-cold-start.json`
- v65553 hang package SHA-256
  `d4b9340e0d95d0043acf2d9c214194ebb9deab837b0dd0da8e1d42134ebc261b`
- v65554 rescue package SHA-256
  `98d57e4740868d330cb39e56abb36d65e8339882c733f01a763dba0314a2bc1f`
