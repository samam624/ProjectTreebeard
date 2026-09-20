#!/usr/bin/env python3
"""Prove on hardware that FDC2214 STATUS.ERR_WD was dead and that writing ERROR_CONFIG revives it.

THE CLAIM UNDER TEST

`docs/KNOWN_ISSUES.md` has said since 2026-09-03 that neither MSV3 sketch writes ERROR_CONFIG
(0x19), and that an absent ERR_WD therefore "proves nothing -- the flag is switched off, not
clear". That was read off the datasheet and never demonstrated. It matters because
probeAndAdvertise() clears `measurementOk` on STATUS.ERR_WD, the intended hardware path for
noticing a tank that has stopped oscillating -- a shorted, waterlogged or disconnected sensor. If
the flag cannot set, that check is dead code guarding the exact failure it exists to catch.

THE 2x2, AND WHY IT IS NOT A BEFORE/AFTER

"I enabled the bit and now ERR_WD appears" does not exclude that the fault injection, rather than
the register, is what changed. So both variables move independently:

                        WD_ERR2INT=1                WD_ERR2INT=0
    healthy tank        A: expect clean             A0: expect clean
    dead tank           B: expect ERR_WD            C:  expect CLEAN despite a dead tank

C is load-bearing: a clean STATUS from a provably non-oscillating sensor is what turns "switched
off, not clear" into a measurement rather than a reading of Table 7-37. A0 completes the square and
shows the register alone neither creates nor suppresses flags on a healthy part. D repeats B after
C to show the effect is reversible within one session on one board, ruling out drift and ordering.

TWO ARMS THAT ARE ABOUT SHIPPING IT, NOT ABOUT PROVING IT

This change will go to a probe sitting in soil, where a spurious ERR_WD would silently take a
HEALTHY sensor off the air -- the opposite of the fault it is meant to catch, and worse, because a
real fault is at least visible. So:

  SOAK        40 consecutive healthy reads with the flag enabled. Any ERR_WD is disqualifying.
  COLD BOOTS  6 probeSleep()/probeWake() cycles -- the REAL production cycle, where shutdown
              clears every FDC register and fdcInit() rewrites ERROR_CONFIG. If enabling the
              report exposes a transient start-up watchdog it shows in the first reads after a
              wake and nowhere else, so a single mid-session sample would miss it entirely.

WHY THE FIRMWARE RUNS THIS AND NOT A SERIAL REPL

The obvious host would be MSV3_Probe_Bringup_Test's command REPL. It cannot be used: that sketch
contains no ResetSafeBLEDfu and therefore no buttonless DFU jump, and this board's bootloader is
built with DEFAULT_TO_OTA_DFU, which removes USB CDC. With no jump service, no USB DFU, no reset
button on the PCB and no SWD attached, flashing the bring-up sketch to a probe is a ONE-WAY TRIP.
So the matrix runs inside a bench build of the PRODUCTION sketch, which keeps the jump, and it runs
from setup() only after bleInit() has already brought that service up.

That is also the better experiment: it exercises probeWake()/fdcInit() -- the code that actually
ships -- rather than a REPL's separate register writes.

READ-TO-CLEAR

Table 7-36: a reported error "is latched and maintained until either the STATUS register or the
DATA_CHx register corresponding to the Error Channel is read". Every read clears what it reports,
so each arm records its full raw sequence rather than one sample.

USAGE

The experiment block prints ONCE, during setup() on the first boot after the flash, so this must
already be listening when the board resets. Pass --flash to have it start the serial reader and
then drive the flasher itself; that ordering is the whole reason the option exists.

  python scripts/fdc_error_config_test.py --port COM9 \
      --flash <bench-errcfg.zip> --probe 99 --json out.json
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
FLASHER = REPO_ROOT / "scripts" / "ble_ota_update.py"

READ_RE = re.compile(
    r"ERRCFG arm=(\S+) i=(\d+) errcfg=0x([0-9A-Fa-f]+) status=0x([0-9A-Fa-f]+) "
    r"data=(\d+) ok=([01])")
SHIPPED_RE = re.compile(r"ERRCFG shipped errcfg=0x([0-9A-Fa-f]+)")
COLDBOOT_RE = re.compile(r"ERRCFG coldboot=(\d+) wake=([01])")

# Address 0x18 STATUS, Table 7-36. Verified against the local datasheet extract, not recalled.
STATUS_ERR_WD = 1 << 11
STATUS_ERR_AHW = 1 << 10
STATUS_ERR_ALW = 1 << 9
# Address 0x19 ERROR_CONFIG, Table 7-37.
ERRCFG_WD_ERR2INT = 1 << 5


def log(message):
    print(f"[{datetime.now():%H:%M:%S}] {message}", flush=True)


class SerialReader:
    """Capture the port across the DFU reset, the way ota_interrupt_test's banner watcher does.

    The port disappears when the board resets and returns when the new application enumerates.
    A single Serial.open() would die at that moment and miss the entire experiment, which prints
    in the first second of setup().
    """

    def __init__(self, port, baud=115200):
        self.port, self.baud = port, baud
        self.lines = []
        self._stop = threading.Event()
        self._thread = None

    def start(self):
        from serial import Serial, SerialException

        def pump():
            while not self._stop.is_set():
                try:
                    with Serial(self.port, self.baud, timeout=0.5) as handle:
                        while not self._stop.is_set():
                            raw = handle.readline()
                            if not raw:
                                continue
                            text = raw.decode("utf-8", "replace").strip()
                            if text:
                                self.lines.append(text)
                                if text.startswith("ERRCFG"):
                                    print(f"    {text}", flush=True)
                except SerialException:
                    time.sleep(0.3)
                except Exception:  # noqa: BLE001
                    time.sleep(0.3)

        self._thread = threading.Thread(target=pump, daemon=True)
        self._thread.start()
        return self

    def wait_for(self, marker, timeout_s):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if any(marker in line for line in self.lines):
                return True
            time.sleep(0.5)
        return False

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)


def parse(lines):
    arms, cold_wakes, shipped = {}, [], None
    for line in lines:
        match = SHIPPED_RE.search(line)
        if match:
            shipped = int(match.group(1), 16)
            continue
        match = COLDBOOT_RE.search(line)
        if match:
            cold_wakes.append(int(match.group(2)))
            continue
        match = READ_RE.search(line)
        if match:
            arm, _, errcfg, status, _, ok = match.groups()
            arms.setdefault(arm, []).append(
                {"errcfg": int(errcfg, 16), "status": int(status, 16), "ok": ok == "1"})
    return arms, cold_wakes, shipped


# Reads discarded at the start of each arm before judging it. STATUS is read-to-clear (Table
# 7-36: an error "is latched and maintained until either the STATUS register or the DATA_CHx
# register ... is read"), so a flag latched under the PREVIOUS arm's register setting can still be
# reported by the first read taken under the new one. Arm C is where that matters: it follows arm
# B, which deliberately produces ERR_WD, and one leftover latch would read as "the gate does not
# work" when it is really "the previous arm's error had not been read out yet". Two reads is
# generous -- one read clears it -- and the raw sequence is kept either way, so the settling can
# be audited rather than taken on trust.
SETTLE_READS = 2


def summarise(samples):
    """Flags are counted over EVERY sample, completed conversion or not.

    Since 2026-09-05 fdcWaitForCh0UnreadConversion() OR-accumulates what it saw and hands it back
    even on a timeout, so a read with ok=0 still carries a meaningful status word -- and on a tank
    that has stopped oscillating that is the ONLY place ERR_WD can appear, because a watchdog
    timeout never produces an unread conversion. Counting flags only over completed conversions,
    as the first version did, made the dead-tank arms structurally incapable of observing the very
    flag they exist to look for.
    """
    ok = [s for s in samples if s["ok"]]
    settled = samples[SETTLE_READS:]
    return {
        "reads": len(ok),
        "read_failures": len(samples) - len(ok),
        "samples": len(samples),
        "err_wd": sum(1 for s in samples if s["status"] & STATUS_ERR_WD),
        "settled_reads": len(settled),
        "err_wd_settled": sum(1 for s in settled if s["status"] & STATUS_ERR_WD),
        "err_ahw": sum(1 for s in samples if s["status"] & STATUS_ERR_AHW),
        "err_alw": sum(1 for s in samples if s["status"] & STATUS_ERR_ALW),
        "error_config": sorted({f"0x{s['errcfg']:04X}" for s in samples}),
        "raw": [f"0x{s['status']:04X}{'' if s['ok'] else '*'}" for s in samples],
    }


def describe(name, summary):
    return (f"{name}: {summary['reads']} reads "
            f"({summary['read_failures']} produced no STATUS), ERR_WD {summary['err_wd']}, "
            f"ERR_AHW {summary['err_ahw']}, ERR_ALW {summary['err_alw']}, "
            f"ERROR_CONFIG {','.join(summary['error_config'])}")


def check_arm_register(name, summary, expected, failures):
    """A silently-failed register write invalidates an arm, so every arm confirms its own value."""
    want = f"0x{expected:04X}"
    if summary["error_config"] != [want]:
        failures.append(f"arm {name}: ERROR_CONFIG read back as "
                        f"{','.join(summary['error_config']) or 'nothing'}, not {want} -- this "
                        f"arm measured nothing")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="USB CDC port, e.g. COM9")
    parser.add_argument("--flash", type=Path,
                        help="Bench package to install; the reader starts first (see USAGE)")
    parser.add_argument("--probe", type=int, default=99)
    parser.add_argument("--timeout", type=float, default=420.0)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    reader = SerialReader(args.port).start()
    results = {"port": args.port, "started": datetime.now().isoformat(timespec="seconds")}
    failures = []
    try:
        if args.flash:
            log(f"serial reader is up; flashing {args.flash.name} to probe {args.probe}")
            command = [sys.executable, "-u", str(FLASHER), str(args.flash.resolve()),
                       "--probe", str(args.probe), "--reflash-same-build"]
            log(f"$ {' '.join(command)}")
            process = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout)
            for line in (process.stdout or "").splitlines()[-12:]:
                print(f"    | {line}", flush=True)
            results["flash_exit_code"] = process.returncode

        log("waiting for the experiment block")
        if not reader.wait_for("ERRCFG done", args.timeout):
            began = any("ERRCFG begin" in line for line in reader.lines)
            failures.append(
                "the experiment never printed 'ERRCFG done'"
                + (" (it began but did not finish -- check the watchdog)" if began
                   else " (it never began -- is this the errcfg bench build?)"))
    finally:
        time.sleep(2)
        reader.stop()

    arms, cold_wakes, shipped = parse(reader.lines)
    results["error_config_after_fdcinit"] = None if shipped is None else f"0x{shipped:04X}"
    results["cold_boot_wakes"] = cold_wakes
    results["arms"] = {name: summarise(samples) for name, samples in arms.items()}
    results["serial_lines"] = [l for l in reader.lines if l.startswith("ERRCFG")]

    if shipped is None:
        failures.append("the shipped ERROR_CONFIG was never reported")
    elif not shipped & ERRCFG_WD_ERR2INT:
        failures.append(f"fdcInit() left WD_ERR2INT clear (ERROR_CONFIG=0x{shipped:04X}) -- the "
                        f"shipped firmware does not do what this test assumes")

    def arm(name):
        return results["arms"].get(name, {"reads": 0, "read_failures": 0, "samples": 0,
                                          "err_wd": 0, "settled_reads": 0, "err_wd_settled": 0,
                                          "err_ahw": 0, "err_alw": 0, "error_config": [],
                                          "raw": []})

    a, a0, b, c, d, e = (arm("A"), arm("A0"), arm("B"), arm("C"), arm("D"), arm("E"))
    soak, cold = arm("SOAK"), arm("COLD")
    for name, summary in (("A", a), ("A0", a0), ("B", b), ("C", c), ("D", d), ("E", e),
                          ("SOAK", soak), ("COLD", cold)):
        if summary["reads"] or summary["read_failures"]:
            log(describe(name, summary))
    for name, summary, expected in (("A", a, ERRCFG_WD_ERR2INT), ("A0", a0, 0x0000),
                                    ("B", b, ERRCFG_WD_ERR2INT), ("C", c, 0x0000),
                                    ("D", d, ERRCFG_WD_ERR2INT), ("E", e, ERRCFG_WD_ERR2INT),
                                    ("SOAK", soak, ERRCFG_WD_ERR2INT),
                                    ("COLD", cold, ERRCFG_WD_ERR2INT)):
        if summary["reads"] or summary["read_failures"]:
            check_arm_register(name, summary, expected, failures)

    if a["err_wd"]:
        failures.append("arm A: a healthy tank reported ERR_WD, so the fault injection is not "
                        "what is being measured")
    if a0["err_wd"]:
        failures.append("arm A0: ERR_WD with the gate closed on a healthy tank -- "
                        "self-contradictory; treat the whole run as unsound")
    if not b["err_wd_settled"]:
        if b["samples"] == 0:
            failures.append("arm B: no reads at all, so nothing was measured about ERR_WD")
        else:
            failures.append(f"arm B: a dead tank with WD_ERR2INT=1 still reported no ERR_WD "
                            f"(ERR_ALW {b['err_alw']}, ERR_AHW {b['err_ahw']}). The stimulus is "
                            f"probably an amplitude fault rather than a watchdog fault, so it "
                            f"cannot test this register")
    if c["err_wd_settled"]:
        failures.append(f"arm C: ERR_WD appeared in {c['err_wd_settled']}/{c['settled_reads']} "
                        f"settled reads with WD_ERR2INT=0, contradicting Table 7-37 -- that "
                        f"would mean the register was never the gate")
    elif c["samples"] == 0:
        failures.append("arm C: no STATUS was read back at all, so its clean ERR_WD is the "
                        "absence of a measurement rather than a measured absence")
    if not d["err_wd_settled"]:
        failures.append("arm D: ERR_WD did not come back when the bit was re-enabled")
    if e["err_wd_settled"]:
        failures.append("arm E: the board did not recover after the drive current was restored")
    elif e["err_wd"]:
        # Not a failure, and worth naming: the flag latched under arm D arrives on the first
        # COMPLETED conversion after recovery, as STATUS=0x808 (ERR_WD | CH0_UNREADCONV) with a
        # valid count. That pairing is exactly what probeAndAdvertise()'s errWd branch acts on, so
        # this is the one observation in the run that shows the branch is reachable at all -- an
        # IDRIVE sweep of codes 0..12 found no setting that flags a completed conversion directly.
        log("note: arm E's first read carried a latched ERR_WD alongside a completed conversion "
            "(STATUS=0x808) -- read-to-clear carry-over from arm D, and the one sample showing "
            "the production errWd branch is reachable")
    if soak["err_wd"]:
        failures.append(f"SOAK: {soak['err_wd']}/{soak['reads']} healthy reads reported ERR_WD. "
                        f"Shipping this would take healthy probes off the air")
    if cold["err_wd"]:
        failures.append(f"COLD BOOTS: {cold['err_wd']}/{cold['reads']} reads after a wake "
                        f"reported ERR_WD -- enabling the report exposes a transient start-up "
                        f"watchdog, which would fault a healthy probe on every wake")
    if cold_wakes and not all(cold_wakes):
        failures.append(f"COLD BOOTS: {cold_wakes.count(0)}/{len(cold_wakes)} wakes failed")

    results["failures"] = failures
    results["verdict"] = "PASS" if not failures else "FAIL"

    print()
    print("=" * 78)
    print(f"  shipped fdcInit() ERROR_CONFIG: {results['error_config_after_fdcinit']}")
    print("                              gate open        gate closed")
    print(f"  healthy tank      ERR_WD  {a['err_wd']:>3}/{a['reads']:<10}{a0['err_wd']:>3}/{a0['reads']}")
    print(f"  DEAD tank         ERR_WD  {b['err_wd']:>3}/{b['samples']:<10}{c['err_wd']:>3}/{c['samples']}"
          f"   <-- a dead tank reporting clean")
    print(f"     settled (post read-to-clear) {b['err_wd_settled']:>3}/{b['settled_reads']:<8}"
          f"{c['err_wd_settled']:>3}/{c['settled_reads']}")
    print(f"  DEAD, re-enabled  ERR_WD  {d['err_wd']:>3}/{d['samples']}"
          f"   (settled {d['err_wd_settled']}/{d['settled_reads']})")
    print(f"  restored          ERR_WD  {e['err_wd']:>3}/{e['reads']}")
    print(f"  healthy soak      ERR_WD  {soak['err_wd']:>3}/{soak['reads']}       (must be 0)")
    print(f"  after cold wakes  ERR_WD  {cold['err_wd']:>3}/{cold['reads']}       (must be 0, "
          f"{len(cold_wakes)} wakes)")
    print("=" * 78)
    print(f"  {results['verdict']}")
    for failure in failures:
        print(f"    - {failure}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(results, indent=2), encoding="utf-8")
        log(f"wrote {args.json}")

    return 0 if results["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
