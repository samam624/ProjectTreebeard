# MSV3 FDC2214 ERROR_CONFIG: the watchdog flag was gated, and the status was being thrown away — 2026-09-05

**What was open.** `docs/KNOWN_ISSUES.md` had said since 2026-09-03 that neither MSV3 sketch writes
`ERROR_CONFIG` (0x19), that the register resets to zero on every hardware shutdown, and that an
absent `ERR_WD`/`ERR_AHW`/`ERR_ALW` therefore "proves nothing — those flags are switched off, not
clear". That was read off the datasheet and never demonstrated.

**Two corrections came out of testing it, one of them to my own framing.**

## Correction 1: only ERR_WD was gated

Table 7-37 (local datasheet extract, checked field by field rather than recalled):

| bit | field | destination |
|---:|---|---|
| 13 | `WD_ERR2OUT` | `DATA_CHx.CHx_ERR_WD` |
| 12 | `AH_WARN2OUT` | `DATA_CHx.CHx_ERR_AW` |
| 11 | `AL_WARN2OUT` | `DATA_CHx.CHx_ERR_AW` |
| 5 | `WD_ERR2INT` | **`STATUS.ERR_WD`** |
| 0 | `DRDY_2INT` | `STATUS.DRDY` |

There is **no `AH_WARN2INT`/`AL_WARN2INT` bit**. `STATUS.ERR_AHW` (bit 10) and `STATUS.ERR_ALW`
(bit 9) have no enable in this register at all, so the old wording — which grouped all three
together as "switched off" — was wrong about the amplitude pair. The `*2OUT` bits are a different
destination (`DATA_CHx`), not a `STATUS` enable for them.

Only `ERR_WD` was gated, and it was gated shut.

## Correction 2: ERR_WD was never the detector for a dead sensor

My first framing said the `errWd` branch in `probeAndAdvertise()` was "dead code guarding the exact
failure it exists to catch". That is too strong, and the bench disproved it.

A watchdog timeout **does not produce an unread conversion**. So under a condition consistent with
loss of sensor oscillation, `CH0_UNREADCONV` never sets, `fdcWaitForCh0UnreadConversion()` runs to its timeout, and
`fdcReadChannel0()` returns false. `measurementOk` goes false through the **readiness-timeout path**,
which always worked and is unaffected by `ERROR_CONFIG`. A permanently dead sensor was already
being rejected.

What was actually lost was the *reason*.

## The real defect: a status read and then discarded

`fdcWaitForCh0UnreadConversion()` polled `STATUS` in a loop and returned false on timeout, keeping
nothing. Two datasheet facts make that lossy exactly where it hurts:

- `STATUS` is **read-to-clear** — Table 7-36: an error "is latched and maintained until either the
  STATUS register or the DATA_CHx register … is read". **The polling loop itself consumes the flags
  it is looking past.**
- A watchdog timeout never sets `CH0_UNREADCONV`, so under that condition the loop always runs to
  timeout.

Measured on the first run: under the induced fault, one poll returned `STATUS=0x800` — `ERR_WD`, the
flag this firmware has an entire branch for — and the code discarded it and returned false.

**Fixed.** `fdcWaitForCh0UnreadConversion()` now OR-accumulates every flag it observes and hands it
back to the caller *even on a timeout*. OR rather than last-value, because returning only the final
word would still lose a flag an earlier poll had already cleared.

**This is a behaviour change on the success path, and it should not be described as a pure
diagnostic.** On the timeout path nothing moves: `measurementOk` was already false. But on a
*successful* read `statusOut` is now the union of everything seen while polling rather than the
last word alone, so a watchdog event that occurred during the poll propagates into
`probeAndAdvertise()`'s `errWd` branch and that one reading is discarded. Arm E below is exactly
this case, measured: the first completed conversion after a fault cleared came back as
`STATUS=0x808` with a valid count, and the production code would have thrown that count away.

That is the conservative direction and it is intended — a conversion that overlapped a watchdog
event is not a reading you want. It costs at most one sample per fault, because `STATUS` is
read-to-clear. But it is a change in what gets stored, not merely in what gets logged.

Regression checks on the accumulator, by inspection:

| property | status |
|---|---|
| accumulator zeroed on every call | `uint16_t seen = 0` local, plus `statusOut = 0` |
| only successful reads accumulated | `fdcReadStatus()` reads into a local; `seen \|= status` only after it returns true |
| an immediate I²C failure cannot return stale flags | first-iteration failure returns `seen == 0` |
| caller receives accumulated status on timeout | yes |
| `measurementOk` still false on timeout | yes — driven by the `false` return, not by the flags |
| normal successful read | **changed by design**, see above |

## The experiment

Run as a boot-time self-test inside a **bench build of the production sketch**, so it exercises the
real `probeWake()`/`fdcInit()` path.

> The obvious host would have been `MSV3_Probe_Bringup_Test`'s serial REPL. It cannot be used, and
> the reason is worth recording: that sketch contains **no `ResetSafeBLEDfu`**, so no buttonless DFU
> jump; this board's bootloader is built with `DEFAULT_TO_OTA_DFU`, which removes USB CDC; the PCB
> has no reset button; and no SWD is attached. Flashing it to a probe is a **one-way trip**.

The fault is held constant while the register is toggled underneath it, so the register — not the
stimulus — is what varies:

| | `WD_ERR2INT=1` | `WD_ERR2INT=0` |
|---|---|---|
| healthy tank | A | A0 |
| induced fault (`IDRIVE=0`) | B | C |

Plus D (a repeat of B, for reversibility), E (drive restored), a 40-read healthy SOAK, and 6
`probeSleep()`/`probeWake()` cold boots — the real production cycle, where shutdown clears every FDC
register and `fdcInit()` rewrites it.

### Result

Counts are over settled reads (the first two of each arm are discarded; see read-to-clear below).

| arm | condition | ERR_WD |
|---|---|---|
| A | healthy, gate open | **0 / 8** |
| A0 | healthy, gate closed | **0 / 8** |
| B | fault, gate **open** | **6 / 6** |
| C | fault, gate **closed** | **0 / 6** |
| D | fault, gate open again | **6 / 6** |
| E | restored, gate open | 0 / 6 |
| SOAK | healthy, gate open | **0 / 40** |
| COLD | 6 cold wakes, gate open | **0 / 18** |

`ERROR_CONFIG` read back as `0x0020` in every arm where it was set and `0x0000` where cleared,
including after all six cold wakes — the production path that clears and rewrites it.

**Arm C is the load-bearing one.** Stated precisely: after the previously latched status had been
cleared, the induced no-conversion condition *repeatedly* reported `ERR_WD` with `WD_ERR2INT` set,
did **not newly** report it with `WD_ERR2INT` clear, and reporting was restored when the gate was
reopened. That is what makes "switched off, not clear" a measurement rather than a reading of
Table 7-37.

The unconditional form — "with the bit clear the tank does not report `ERR_WD`" — would be wrong,
and arm C is what shows why: a flag latched before the transition still surfaces on the first read
after it.

### Read-to-clear is visible in the raw data

Arm C's very first read (`i=0`) *did* carry `0x800` — the flag latched under arm B, arriving before
any read had cleared it. That single sample is why the arms are scored on settled reads, and it is
the mechanism working as documented rather than an anomaly.

The same carry-over produced the run's most useful single observation. Arm E's first read was
`STATUS=0x808` — `ERR_WD | CH0_UNREADCONV` — with `ok=1` and a valid count. A **latched watchdog
event returned alongside the first completed conversion after recovery**, which is precisely the
combination `probeAndAdvertise()`'s `errWd` branch acts on, and the only sample in the run showing
that branch is reachable at all. Note what it does *not* say: the watchdog event belonged to the
preceding induced fault, not to that conversion — the count itself was fine. It demonstrates
reachability during fault recovery or intermittence, not that a flagged conversion produces a bad
number.

### A negative result worth keeping: there is no marginal drive current

Before the accumulation fix, the plan was to find an `IDRIVE` that faults a conversion *without*
preventing it from completing. A sweep of codes 0–12, six reads each, found none on this board:

```
idrive=0   completed 1/6   errwd 0      <- oscillation stops outright
idrive=1..12  completed 6/6   errwd 0   <- clean conversions, no flag
```

The transition is a cliff, not a ramp. So "conversion completes but is flagged" cannot be induced
directly here, and the gating had to be demonstrated through the timeout path — which is exactly
what the accumulation fix made observable.

## What this establishes, and what it does not

**Establishes.** `fdcInit()` writes `ERROR_CONFIG=0x0020` and it reads back correctly on every wake,
including six cold sleep/wake cycles. Under an induced no-conversion condition consistent with loss
of sensor oscillation, and after any previously latched status had been cleared, `ERR_WD` was
reported repeatedly with `WD_ERR2INT` set, was not newly reported with it clear, and returned when
the gate was reopened. No false `ERR_WD` appeared in 58 healthy bench measurements (40 soak + 18
post-wake). Reproduced on two independent runs with identical raw numbers.

On the honest history of those two runs: the first exposed an incorrect arm-E assertion, which was
scoring that arm on raw rather than settled reads and so counted the documented read-to-clear
carry-over as a failure to recover. The assertion was corrected to match the other arms and the
experiment was **re-run** rather than re-scored by hand, so the passing verdict comes from the tool
against the corrected predeclared criteria, not from reinterpreting a failed one.

**Does not establish.** 58 healthy bench readings on one board is a limited sample. Environmental
loading, supply voltage, temperature and long-run wake counts are all uncovered. This is *"no false
ERR_WD observed in 58 bench measurements"*, **not** "shipping safety answered".

`ERROR_CONFIG` enables the reporting; the production caller still acts on `ERR_WD` only when a
successful readiness poll returns a `STATUS` containing both `CH0_UNREADCONV` and `ERR_WD`. That
pairing was observed once (arm E) and is not otherwise inducible on this board.

## Shipping

**Staged on probe 99, not probe 4.** The register value is datasheet-correct, readback is verified
through the production wake path, and enabling a report should surface watchdog events rather than
create them — so there is no reason to hold the change. But the healthy-path evidence is a bench
sample, and the failure mode of a false positive is a *healthy* probe taken off the air, which is
worse than the fault it detects because it is invisible. Probe 4 gets it after a longer probe-99 run
under representative conditions.

Any `ERR_WD` on a healthy path should be treated as a **real transient needing review**, not
automatically as a spurious bit.

## Follow-ups

- **Longer probe-99 soak** across real wake cycles, supply and temperature before probe 4.
- **Surface the fault reason on the wire.** The accumulated status is now available at the point
  where the probe decides to advertise a fault, but the advertisement still says only "invalid".
  A reason code would let the receiver distinguish a dead oscillator from a dead bus. That changes
  the probe↔receiver contract and is deliberately **not** folded into this change.
- `DRDY_2INT` stays 0 on purpose. `STATUS.DRDY` is a trap this project has hit three times; the
  readiness poll uses `CH0_UNREADCONV`, which needs no enable bit. Leaving it off keeps the wrong
  approach visibly broken.

## Reproducing

```powershell
# Bench build of the PRODUCTION sketch with the experiment block added in setup() after bleInit().
# Identity and logging are build parameters as of 2026-09-05, so only the temporary experiment
# block itself needs a scratch copy.
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 `
    -Action compile -Sketch <scratch>\MSV3_Moisture_BLE_Sleep `
    -SensorId 99 -SerialLogs -BuildTag 2026-09-05-errcfg
# Sign it, then flash and capture in one step -- the block prints ONCE during setup(), so the
# reader has to be listening before the DFU reset:
python scripts/fdc_error_config_test.py --port COM9 --flash <bench.zip> --probe 99 --json out.json
```
