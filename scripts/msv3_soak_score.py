#!/usr/bin/env python3
"""Score an MSV3 probe soak capture against an acceptance rule fixed BEFORE the run.

WHY THE RULE IS WRITTEN DOWN FIRST

This exists to answer one question: does enabling FDC2214 `ERROR_CONFIG.WD_ERR2INT` ever report a
watchdog error on a HEALTHY probe? A false `ERR_WD` is worse than the fault it detects, because it
silently takes a working sensor off the air while a real fault at least announces itself.

The bench evidence behind that change is 58 readings. That is enough to say "no false positive was
observed", and not enough to ship to a probe sitting in soil. So the acceptance rule is stated as
a threshold and a count, decided in advance, rather than as a judgement made after looking at the
numbers -- which is how a marginal result becomes a pass.

WHAT IT READS

The production sketch built with `-SerialLogs` prints one line per advertised wake:

    [DIAG] wake=123 phase=0 DATA_CH0=20422165 status=0x8 tempQ4C=92 tempValid=1 \\
           bareDATA_CH0=22663668 bareStatus=0x8 bareValid=1 vbat_mV=4326 charging=0

and, on a wake it declines to advertise:

    [DIAG] checked, phase=0, DATA_CH0=0 -- FDC READ FAILED, skipping advertise
    [WARN] FDC read failed; flags seen while polling: 0x800 ERR_WD -- oscillator not running

Capture it with `scripts/serial_capture.py`, which survives the port vanishing and returning.

WHAT COUNTS AS A PASS

  * at least --min-wakes distinct wakes observed, so the sample is the size that was agreed;
  * ZERO ERR_WD on a probe known to be healthy -- any occurrence is disqualifying and is to be
    treated as a real transient needing review, not written off as a spurious bit;
  * ZERO wake-counter regressions (a lower wake number than the previous line means the probe
    reset, and an unexplained reset invalidates the window rather than merely interrupting it);
  * ZERO failed reads.

ERR_AHW and ERR_ALW are reported but do NOT fail the run. Those two flags are not gated by
ERROR_CONFIG and were live before this change, so they say nothing about it.

Run:  python scripts/msv3_soak_score.py --log soak.log --min-wakes 500
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

WAKE_RE = re.compile(
    r"\[DIAG\] wake=(\d+).*?status=0x([0-9A-Fa-f]+).*?"
    r"bareStatus=0x([0-9A-Fa-f]+).*?vbat_mV=(-?\d+)")
SKIP_RE = re.compile(r"\[DIAG\] checked.*?--\s*(.+?), skipping advertise")
READFAIL_RE = re.compile(r"\[WARN\] FDC read failed; flags seen while polling: 0x([0-9A-Fa-f]+)")
BOOT_RE = re.compile(r"\[DIAG\] MSV3 production probe booting")
BUILD_RE = re.compile(r"\[DIAG\] build: (\S+)")

STATUS_ERR_WD = 1 << 11
STATUS_ERR_AHW = 1 << 10
STATUS_ERR_ALW = 1 << 9


def score(lines):
    wakes, err_wd, err_ahw, err_alw = [], [], 0, 0
    bare_err_wd = 0
    regressions, skips, read_failures, boots, builds = [], [], [], 0, set()
    previous = None

    for line in lines:
        if BOOT_RE.search(line):
            boots += 1
            previous = None  # a reboot legitimately restarts the counter
            continue
        match = BUILD_RE.search(line)
        if match:
            builds.add(match.group(1))
            continue
        match = READFAIL_RE.search(line)
        if match:
            read_failures.append(int(match.group(1), 16))
            continue
        match = SKIP_RE.search(line)
        if match:
            skips.append(match.group(1).strip())
            continue
        match = WAKE_RE.search(line)
        if not match:
            continue
        wake = int(match.group(1))
        status = int(match.group(2), 16)
        bare = int(match.group(3), 16)
        wakes.append(wake)
        if status & STATUS_ERR_WD:
            err_wd.append(wake)
        if status & STATUS_ERR_AHW:
            err_ahw += 1
        if status & STATUS_ERR_ALW:
            err_alw += 1
        if bare & STATUS_ERR_WD:
            bare_err_wd += 1
        if previous is not None and wake < previous:
            regressions.append((previous, wake))
        previous = wake

    return {
        "wakes_seen": len(wakes),
        "distinct_wakes": len(set(wakes)),
        "wake_range": [min(wakes), max(wakes)] if wakes else None,
        "err_wd_wakes": err_wd,
        "bare_tank_err_wd": bare_err_wd,
        "err_ahw": err_ahw,
        "err_alw": err_alw,
        "wake_counter_regressions": regressions,
        "skipped_advertises": skips,
        "read_failures": [f"0x{f:04X}" for f in read_failures],
        "boot_banners": boots,
        "build_ids": sorted(builds),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", type=Path, required=True, help="serial_capture.py output")
    parser.add_argument("--min-wakes", type=int, default=500,
                        help="sample size agreed before the run (default 500, about 10h at 70s)")
    parser.add_argument("--expect-build", help="fail if any other build id appears in the capture")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    if not args.log.is_file():
        print(f"error: no such capture: {args.log}")
        return 2

    result = score(args.log.read_text(encoding="utf-8", errors="replace").splitlines())
    failures = []

    if result["distinct_wakes"] < args.min_wakes:
        failures.append(f"only {result['distinct_wakes']} distinct wakes, below the agreed "
                        f"{args.min_wakes} -- the window is too short to conclude anything")
    if result["err_wd_wakes"]:
        failures.append(f"ERR_WD on a healthy probe at wakes {result['err_wd_wakes'][:10]} "
                        f"({len(result['err_wd_wakes'])} total). Disqualifying: investigate as a "
                        f"real transient, do not write it off as a spurious bit")
    if result["read_failures"]:
        failures.append(f"{len(result['read_failures'])} failed reads "
                        f"(flags {result['read_failures'][:5]})")
    if result["wake_counter_regressions"]:
        failures.append(f"wake counter went backwards without a boot banner: "
                        f"{result['wake_counter_regressions'][:5]} -- unexplained reset")
    if args.expect_build and [b for b in result["build_ids"] if b != args.expect_build]:
        failures.append(f"capture contains build ids {result['build_ids']}, expected only "
                        f"{args.expect_build} -- the image changed mid-window")

    result["min_wakes"] = args.min_wakes
    result["failures"] = failures
    result["verdict"] = "PASS" if not failures else "FAIL"

    print(f"capture      {args.log}")
    print(f"builds seen  {', '.join(result['build_ids']) or 'none (no boot banner captured)'}")
    print(f"wakes        {result['distinct_wakes']} distinct, range {result['wake_range']}, "
          f"{result['boot_banners']} boot banner(s)")
    print(f"ERR_WD       {len(result['err_wd_wakes'])}  (must be 0)")
    print(f"read fails   {len(result['read_failures'])}  (must be 0)")
    print(f"skipped adv  {len(result['skipped_advertises'])}")
    print(f"ERR_AHW/ALW  {result['err_ahw']}/{result['err_alw']}  (informational -- not gated by "
          f"ERROR_CONFIG, live before this change)")
    print(f"bare-tank ERR_WD {result['bare_tank_err_wd']}")
    print()
    print(result["verdict"])
    for failure in failures:
        print(f"  - {failure}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")

    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
