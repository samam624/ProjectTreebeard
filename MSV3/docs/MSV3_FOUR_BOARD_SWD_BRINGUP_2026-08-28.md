# MSV3 Four-Board SWD Bring-Up + EC-Check Analog Fix (2026-08-28)

> **2026-08-30 source warning:** The field production sketch was corrected to wait on
> `STATUS.CH0_UNREADCONV` after a temporary global-DRDY implementation stopped advertisements.
> The currently modified `MSV3_Probe_Bringup_Test.ino` still calls `fdcWaitForDrdy()` without
> explicitly enabling `ERROR_CONFIG.DRDY_2INT`. Reconcile it with the production readiness logic
> before treating its FDC read/stream/wake commands as authoritative. This warning concerns the
> current working-tree edit, not the 2026-08-28 binaries used for the four-board results below.

## Summary

- All 4 freshly-assembled MSV3 boards: SWD bootloader install, then USB DFU flash of
  `arduino/MSV3_Probe_Bringup_Test`. All four confirmed alive with no hangs: LFRC active (not the
  crystal path that's hung this hardware before), FDC2214 correctly identified, BLE
  init/advertise/stop clean with no reset in between.
- `scripts/swd_flash_hex.py` gained a one-shot `--bootloader` mode for the batch-flash workflow.
  Its first version only required a single successful contact read before starting — confirmed
  insufficient on real hardware (a flickering hand contact passed the check, mass-erase started,
  then genuinely lost contact mid-write). Now requires 3 continuous seconds of stable,
  consistent-DPIDR contact before doing anything.
- `DIV_NODE`/`VDIV` (the EC-check midpoint) bodged from the digital-only `P0.00` to the
  AIN-capable `P0.03`/AIN1 (module pin 3), per the fix plan already on record in
  `docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 5. Firmware updated to read it as a real graded
  `analogRead()` (reusing `readVbatMv()`'s reference/resolution/sample-time settings) instead of a
  binary digital threshold.
- Found and fixed a real bug introduced by that change: the old `P0.00` trace is still physically
  present on the PCB in parallel with the new bodge (confirmed in the netlist: `VDIV` net members
  are `MUX-7, MUX-11, R17-2, U1-11, VDIV-1` — `U1-11` is `P0.00`). Repointing the `PIN_DIV_NODE`
  macro silently dropped the `pinMode()` call that used to keep that pin an inert input, leaving it
  fully unconfigured instead of disconnected. Fixed with an explicit `gpioDisconnect()` on the old
  pin, in both `setup()` and the sleep path.
- Found a real per-board hardware fault, unrelated to the EC-check bodge: one board's mux never
  switches state at all — `muxsweep` showed zero capacitance jump across all 4 codes (should be a
  clean ~11% step between electrode-swap and EC-check codes). Confirmed independently by
  multimeter (Node A pinned, no code-dependent change).
- On a second, separately-confirmed-good-mux board, `VDIV` read pegged near the 3.0V ADC ceiling
  regardless of mux code, even after the `P0.00` fix above (no change). Ruled out a permanent
  electrode-to-electrode short (Node A/B tracked correctly with mux code — both 0V in code 0, both
  3.3V in code 2). Points at `R17` or the mux's `VDIV`-throw pins (7/11) specifically. **Not yet
  confirmed** with a direct multimeter reading of `R17`'s individual legs — open item.
- On a third board (no wire at first), the EC-check path measured correctly (0V dry via
  multimeter) and the **digital** path gave a clean LOW→HIGH transition on a real wire bridge
  across the electrodes — the first confirmed real-bridge test on this design, closing the open
  item from `docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 5 ("30 seconds. Open since 8/20.").
  The same board then got the `P0.03` analog bodge added and passed there too: clean 0V dry
  baseline, pegs at the ADC ceiling on a wire-short bridge, drops back to 0V released.

## Four-board SWD + USB bring-up results

Every board: SWD mass-erase + `pca10056_bootloader-0.11.0_s140_6.1.1.hex` install
(`scripts/swd_flash_hex.py --bootloader`), then USB DFU flash of the bring-up console. All four
passed the same fast go/no-go sequence with no reset between `blestart` and `blestop`:

| Check | Result (all 4 boards) |
|---|---|
| `wdtstatus` reset reason | `RESETPIN` (post-DFU) / clean, no watchdog or lockup reset |
| `LFCLKSTAT` source | `RC` (LFRC confirmed active, not the crystal path) |
| `i2cscan` | device found at `0x2A` |
| `fdcid` | `MANUFACTURER_ID=0x5449`, `DEVICE_ID=0x3055` (both correct for FDC2214) |
| `blestart` → `wdtstatus` → `blestop` | advertised as `MSV3-PROBE-BRINGUP`, no reset mid-advertise, stopped cleanly |

No battery or solar panel was attached to any board for this session (batteries and printed
enclosures still pending) — all four ran USB-only. `chgstatus` on one board read `STAT=LOW
("charging")` with no battery attached, which contradicts the "expect HIGH, no battery" claim in
`docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 1. The BQ25170 datasheet's own STAT table (Table
7-3) only defines HIGH (complete/disabled/sleep), LOW (charging), and blink (fault) — no explicit
"no battery" state — so this may not generalize as cleanly as the earlier doc assumed. Not yet
compared across all 4 boards; open item.

## `scripts/swd_flash_hex.py` — `--bootloader` one-shot mode

```powershell
python scripts/swd_flash_hex.py --port COM3 --bootloader
```

Waits for **3 continuous seconds** of stable SWD contact (drives the adapter's own `probe` command,
resets the stability window if DPIDR reads inconsistently between checks — real noise-passing-
parity-check risk, not hypothetical), then always mass-erases (never skippable in this mode — a
non-erased bootloader write risks leaving old app code that re-corrupts it, see
`docs/SWD_FLASHER.md`), then flashes the bundled bootloader hex. `--contact-stable` and
`--contact-timeout` tune the window/budget. Existing `--hex`/`--mass-erase` behavior without
`--bootloader` is unchanged — this was purely additive, since the other documented use of this
script (updating an app `.hex` over SWD on a board that already has a bootloader) must *not*
mass-erase.

Full detail: `docs/SWD_FLASHER.md`.

## EC-check (`DIV_NODE`/`VDIV`) analog fix

Root cause (already on record in `docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 5): `P0.00`/XL1
has zero analog capability on the nRF52840 (SAADC/COMP/LPCOMP are hard-restricted to AIN0-AIN7),
so the original wiring could only ever be a binary digital threshold, never the graded EC/soil-
salinity reading the design wants. `P0.03`/AIN1 (module pin 3, castellated edge pad) is the fix —
confirmed against the real Ebyte datasheet pin table.

`arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino` changes:

- `PIN_DIV_NODE` moved from `0` to `3`.
- New `readDivNodeMv()` / `doDivNode()`, reusing `readVbatMv()`'s exact
  `AR_INTERNAL_3_0`/12-bit/40µs-sample-time settings rather than re-deriving ADC config. No RC
  settle delay needed (unlike `VBAT_DIV`) — `VDIV` has no capacitor on the net, just `R17` pulldown
  + mux switch resistance, settles in microseconds per the existing `doSenseSweep()` comment.
  `divnode` command output changed from `LOW`/`HIGH` to raw counts + mV.
- New `PIN_DIV_NODE_OLD` (`0`, `P0.00`) — the old trace is still physically present in parallel on
  the PCB (real netlist connectivity, not assumption). Must stay explicitly
  `gpioDisconnect()`'d (added to both `setup()` and the sleep path), not just left alone — leaving
  it unconfigured after repointing the macro was the actual bug that caused one board's `divnode`
  to read pegged high initially, before that board's separate `R17`-area fault was found underneath
  it (see below).
- `doSenseSweep()`'s own EC-check read is **not** yet updated to analog — still `digitalRead()` on
  `PIN_DIV_NODE`. Works fine post-move (`P0.03` is a "Common I/O" pin, not analog-only), just still
  binary there. Not in scope of today's fix; flagged for later if the graded reading is wanted
  inside that cycle too.

## Per-board hardware findings

**Bad mux (unrelated to the EC-check bodge).** `muxsweep` on this board showed all 4 codes reading
within ~0.02% of each other — no capacitance jump between electrode-swap and EC-check codes at all
(a healthy board shows a clean ~11% step). Confirmed independently: Node A pinned regardless of
mux code via multimeter. Likely a TMUX1109 solder-joint or part fault on this specific unit — worth
a visual/reflow check, or probing `MUX0`/`MUX1` directly to rule out the E73 side.

**`VDIV`-pegged-high board (separate fault, confirmed-good mux).** This board's `muxsweep` passed
cleanly (~11.4% jump, matching the healthy reference signature) *before* the `P0.03` bodge was
added. After the bodge and the `P0.00`-disconnect fix, `VDIV` still read pegged near the 3.0V ADC
ceiling in **all 4 mux codes**, including codes 0/1 where the mux never routes anything to `VDIV`
at all — ruling out mux state as the cause. Multimeter confirmed the same thing independently
(~3.11V at `VDIV`, ~3.11V across `R17`). Node A/B tracked mux code correctly (both 0V in code 0,
both 3.3V in code 2 — Node A legitimately at 3.3V there by design), which rules out a permanent
electrode-to-electrode short and points at `R17` or the mux's `VDIV`-throw pins (7/11) specifically.
**Open item**: never got a direct multimeter reading of `R17`'s two legs individually to confirm
which side is actually at fault.

**Confirmed-good board (digital + analog both validated end-to-end).** No wire initially: `VDIV`
measured 0V dry via multimeter, matching expectation. Digital `divnode` (`P0.00`) gave a clean
LOW→HIGH transition on a real wire bridge across the electrodes, held for the bridge's duration,
dropped back to LOW on release — the first real confirmed bridge test on this design (open since
8/20 per the bench test plan). Then got the same `P0.03` analog bodge: clean `0/4095 (0.0mV)` dry
baseline, pegs at the ADC ceiling (`4095/4095`, ~3.0V) on a real wire-short bridge, drops back to 0
on release. One brief false alarm during this test turned out to be the operator's own hand losing
contact momentarily while getting positioned, not a flaky joint — worth remembering that a genuine
wire-short bridge should peg the ADC hard, not produce a partial reading, so a clean full-scale
result is the expected positive, not a red flag.

## Testing methodology note

Opening a **new** serial connection to a board mid-session can appear to reset it — `mux <code>`
state does not survive if a fresh connection is opened between setting it and reading it back
(confirmed: a mux-park command followed by a separate script invocation for a read-loop produced a
stale/wrong result, traced to the mux having silently reverted to its power-on default of code 0).
The core's `readResetReason()` captures `POWER->RESETREAS` once at boot and only ever prints that
cached value, so it can't distinguish a genuinely-fresh reset from stale data — not a reliable way
to confirm this on its own. The reliable fix: keep any `mux <code>` + subsequent read(s) inside one
continuous serial connection, never split across separate script invocations.

## Open items

1. **`R17`/`VDIV` fault root cause** on the pegged-high board — needs a direct multimeter reading
   of `R17`'s two legs individually (one should be a clean 0V/GND-continuity; if it isn't, that
   confirms a miswired/bridged ground return).
2. **`chgstatus` STAT behavior with no battery** — one board read `LOW` ("charging") with nothing on
   `VBAT`, contradicting the earlier bench doc's "expect HIGH" note. BQ25170's datasheet doesn't
   define a "no battery" STAT state at all (only complete/charging/fault), and `VBAT` isn't
   actually floating even with no cell attached (still tied to BQ25504's `VBAT` pin) — plausible
   but unconfirmed explanation. Not yet compared across all 4 boards.
3. **Bad-mux board** — needs a visual/reflow check on the TMUX1109's solder joints, or a direct
   probe of `MUX0`/`MUX1` to rule out the E73 GPIO side before writing off the mux chip itself.
4. **Fourth board** (never got the `P0.03` bodge or a `muxsweep` run this session) — not yet
   retested with the analog EC-check fix.
5. **`doSenseSweep()`** still reads `DIV_NODE` digitally, not analog — fine as-is, just not
   upgraded to match `divnode`'s new graded reading.
