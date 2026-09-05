#!/usr/bin/env python3
"""Deliberately interrupt an MSV3 OTA and record what the board becomes.

This is adoption-sequence item 6 from MSV3_BLE_OTA_PROOF_2026-09-02.md -- the acceptance test that
has gated conformal-coating since the OTA path was first proven. Until it passes there is no
evidence that a probe survives a transfer that goes wrong, and a coated probe has no cable.

WHAT IT ACTUALLY TESTS, AND WHAT IT DOES NOT

It kills the host process mid-transfer. That is a faithful HOST CRASH: the Windows stack tears the
connection down, so the bootloader sees a disconnect. It is NOT the same as:

  * RF disappearance -- the board would instead sit until the BLE supervision timeout, exercising
    a different path. Use --rf-loss guidance in the report; it needs the board carried out of
    range, which cannot be automated from here.
  * Power loss -- the only class that can interrupt a flash erase/program in progress. The MCU
    stays powered through a host crash, so this harness structurally cannot produce it. Those runs
    need a physical unplug and, because they are the ones that could corrupt bootloader metadata,
    they should not be attempted without SWD recovery attached.

Treat a pass here as covering one of three classes.

THE EXPECTED-OUTCOME MATRIX IS NOT "THE OLD APPLICATION SURVIVES"

The adoption text says every interrupted case must "either boot the old application or return to
BLE recovery". For a SINGLE-BANK bootloader the first half of that is unachievable once a transfer
has started: the incoming image overwrites the running application in place, so by 10% there is no
old application left to boot. The honest acceptance rule, and the one asserted here, is:

    after any interruption the board must expose EITHER a bootable application OR authenticated
    BLE DFU recovery, and must never become unreachable without SWD.

Which of the two is legitimate depends on how far the transfer got, so each case declares its own
allowed set rather than sharing one.

WHY A BUILD ID EXISTS

The bootloader stores no application version -- dfu_init.c's version check is an unimplemented
upstream template, so `application_version` in the DFU package is never persisted and cannot be
read back. Two builds for the same probe are otherwise indistinguishable. FIRMWARE_BUILD_ID in the
sketch is a literal in the image and is printed in the boot banner, so this harness can assert
that what came back is what it sent.

Run one case:
    python scripts/ota_interrupt_test.py --probe 99 --package <signed.zip> --case baseline
    python scripts/ota_interrupt_test.py --probe 99 --package <signed.zip> --case transfer-50
"""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from msv3_fleet import (  # noqa: E402
    FleetError,
    advertisement_from_bleak,
    load_fleet,
    read_package,
)

REPO_ROOT = Path(__file__).resolve().parents[1]
FLASHER = REPO_ROOT / "scripts" / "ble_ota_update.py"
DFU_SERVICE_UUID = "00001530-1212-efde-1523-785feabcd123"

PROGRESS_RE = re.compile(r"Transfer:\s+(\d+)%")

# What each case is allowed to leave behind. "application" means a valid app booted and is
# advertising; "recovery" means the bootloader is advertising its DFU service with no valid app.
CASES = {
    "baseline": {
        "trigger": None,
        "allowed": ("application",),
        "why": "no interruption at all -- proves the client and the board still work end to end "
               "before anything is deliberately broken",
    },
    "transfer-10": {
        "trigger": ("percent", 10),
        "allowed": ("recovery",),
        "why": "single-bank has already overwritten the start of the application, so recovery is "
               "the only legitimate outcome",
    },
    "transfer-50": {
        "trigger": ("percent", 50),
        "allowed": ("recovery",),
        "why": "same, halfway through",
    },
    "pre-validate": {
        "trigger": ("stdout", "Verifying Upload"),
        "allowed": ("recovery", "application"),
        "why": "every byte is written and the host has just asked for verification. The "
               "valid-image marker may or may not be committed, so both outcomes are legitimate; "
               "what must not happen is an unreachable board",
    },
    "activate-race": {
        "trigger": ("stdout", "Activating & Resetting"),
        "allowed": ("recovery", "application"),
        "why": "kills the host at the moment the activate command goes out, so the process dies "
               "while the device is doing its own activation. This is the closest a host crash "
               "can get to interrupting activation -- the device operation is synchronous and "
               "cannot be halted at a chosen internal point, so the outcome is a genuine race",
    },
    "post-complete": {
        "trigger": ("stdout", "Transfer completed"),
        "allowed": ("application",),
        "why": "kills after DFU Complete has already been acknowledged. This does NOT test "
               "activation -- by then the device has activated and reset. It is kept only as a "
               "control showing a fully acknowledged transfer always lands",
    },
}


def log(message):
    print(f"[{datetime.now():%H:%M:%S}] {message}", flush=True)


# ------------------------------------------------------------- board state ----

async def observe(addresses, seconds, expect_application_window=True):
    """Watch every address this probe may use and classify what is there.

    Deliberately long by default: a running application advertises 5 s per 60 s, so a short scan
    that finds nothing proves nothing, while the recovery bootloader advertises continuously. That
    asymmetry is exactly how you mistake a healthy board for a dead one.
    """
    from bleak import BleakScanner

    wanted = {a.upper() for a in addresses}
    seen = {}

    def callback(device, advertisement):
        address = device.address.upper()
        if address not in wanted:
            return
        uuids = [u.lower() for u in (advertisement.service_uuids or [])]
        name = advertisement.local_name or device.name or ""
        entry = seen.setdefault(address, {"names": set(), "dfu": False, "advert": None,
                                          "rssi": advertisement.rssi, "count": 0})
        entry["count"] += 1
        if name:
            entry["names"].add(name)
        if DFU_SERVICE_UUID in uuids or "dfu" in name.lower():
            entry["dfu"] = True
        advert = advertisement_from_bleak(advertisement)
        if advert is not None:
            entry["advert"] = advert

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        await asyncio.sleep(seconds)
    finally:
        await scanner.stop()

    state, detail = "unreachable", "nothing advertised on either address"
    for address, entry in seen.items():
        if entry["dfu"]:
            state = "recovery"
            detail = (f"DFU bootloader at {address} "
                      f"({', '.join(sorted(entry['names'])) or 'unnamed'}), "
                      f"{entry['count']} packets, rssi {entry['rssi']}")
            break
    else:
        for address, entry in seen.items():
            advert = entry["advert"]
            if advert is not None:
                state = "application"
                detail = (f"application at {address} "
                          f"({', '.join(sorted(entry['names'])) or 'unnamed'}), sensor "
                          f"{advert.sensor_id}, wake {advert.wake}, "
                          f"{advert.battery_mv} mV, {entry['count']} packets")
                break
            state = "unknown"
            detail = f"{address} advertised but carried no MSV payload"
    return state, detail, seen


class BannerWatcher:
    """Capture the application's boot banner over USB CDC, across the DFU reset.

    The banner prints once, in the first second after boot, and the application boots the instant
    the bootloader activates the new image -- which is roughly when the flasher prints "DFU
    Complete". Opening the port afterwards, as the first version of this harness did, arrives
    minutes late and sees nothing. So this runs from before the flasher starts until after the
    observation window, reopening the port as it disappears and reappears across the reset.

    A build id here is the only positive proof of WHICH image is running: the bootloader stores no
    application version, and every build for this probe advertises the same name and address.
    """

    def __init__(self, port):
        self.port = port
        self.lines = []
        self.build_ids = []
        self._stop = None
        self._thread = None

    def start(self):
        if not self.port:
            return self
        try:
            from serial import Serial, SerialException
        except ModuleNotFoundError:
            log("pyserial not installed; the serial build check is skipped")
            return self

        import threading
        self._stop = threading.Event()

        def pump():
            while not self._stop.is_set():
                try:
                    with Serial(self.port, 115200, timeout=0.5) as handle:
                        while not self._stop.is_set():
                            raw = handle.readline()
                            if not raw:
                                continue
                            line = raw.decode("utf-8", "replace").strip()
                            if not line:
                                continue
                            self.lines.append(line)
                            if "build:" in line:
                                # The banner prints the whole literal ("MSV3-BUILD:<id>") while
                                # msv3_fleet's regex captures only the <id> part. Normalise here so
                                # the two are comparable -- the baseline run compared them raw and
                                # reported "the previous image survived" about an image that had
                                # in fact just been installed.
                                found = line.split("build:", 1)[1].strip()
                                if found.startswith("MSV3-BUILD:"):
                                    found = found[len("MSV3-BUILD:"):]
                                self.build_ids.append(found)
                                log(f"    serial: boot banner reports build {found}")
                except SerialException:
                    # Expected: the port vanishes at the DFU reset and returns when the
                    # application enumerates again. Also expected on a board with no valid
                    # application, where DEFAULT_TO_OTA_DFU removes USB CDC entirely.
                    time.sleep(0.3)
                except Exception:  # noqa: BLE001
                    time.sleep(0.3)

        self._thread = threading.Thread(target=pump, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        if self._stop is not None:
            self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=3)

    @property
    def latest_build(self):
        return self.build_ids[-1] if self.build_ids else None


# --------------------------------------------------------------- the run ----

def run_flasher(package, probe, trigger, extra_args, timeout_s=420):
    """Drive the real, unmodified flasher as a subprocess and kill it at the trigger.

    Killing the process rather than raising inside it is the point: no test hook exists in the
    production tool, so what is exercised here is exactly what a real operator runs.
    """
    command = [sys.executable, "-u", str(FLASHER), str(package), "--probe", str(probe),
               *extra_args]
    log(f"$ {' '.join(command)}")
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1,
                               creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))

    transcript = []
    killed_at = None
    deadline = time.time() + timeout_s
    try:
        for line in process.stdout:
            line = line.rstrip("\n")
            if line:
                transcript.append(line)
                print(f"    | {line}", flush=True)
            if trigger is None or killed_at is not None:
                if time.time() > deadline:
                    break
                continue

            kind, value = trigger
            hit = False
            if kind == "percent":
                match = PROGRESS_RE.search(line)
                # NOTE: the percentage is host-side submission progress, not a device
                # acknowledgement, so the board may be slightly beyond it. That is acceptable for
                # "interrupt roughly here" and is why the report records the state, not the offset.
                if match and int(match.group(1)) >= value:
                    hit = True
            elif kind == "stdout" and value in line:
                hit = True

            if hit:
                killed_at = line
                log(f"TRIGGER HIT -> killing the host process at: {line!r}")
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               capture_output=True)
                break
            if time.time() > deadline:
                log("timeout waiting for the trigger; killing")
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               capture_output=True)
                break
    finally:
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)

    return process.returncode, transcript, killed_at


async def main_async(args):
    try:
        fleet = load_fleet()
        probe = fleet.by_sensor_id(args.probe)
        package = read_package(args.package)
    except FleetError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    case = CASES[args.case]
    print("=" * 78)
    log(f"CASE {args.case}: {case['why']}")
    log(f"target  {probe.describe()}")
    log(f"package {package.describe()}")
    if not package.build_id:
        log("WARNING: this package carries no MSV3-BUILD id, so a booted application cannot be "
            "identified as this image")
    print("=" * 78)

    before_state, before_detail, _ = await observe(probe.bootloader_addresses, args.observe_before)
    log(f"BEFORE: {before_state} -- {before_detail}")

    # Started BEFORE the flasher so it is already listening when the board resets into the new
    # image. This is the fix for the baseline run, where the port was opened after a 90s
    # observation window and the once-only banner was long gone.
    banner = BannerWatcher(args.port).start()

    extra = list(args.flasher_arg or [])
    if before_state == "recovery":
        extra.append("--skip-jump")
        log("board is already in recovery, adding --skip-jump")

    code, transcript, killed_at = run_flasher(args.package, args.probe, case["trigger"], extra)
    log(f"flasher exited {code}" + (f", killed at {killed_at!r}" if killed_at else ""))

    log(f"observing for {args.observe_after:.0f}s (a running application advertises only "
        f"5s per 60s)...")
    after_state, after_detail, _ = await observe(probe.bootloader_addresses, args.observe_after)
    log(f"AFTER: {after_state} -- {after_detail}")

    banner.stop()
    build_id = banner.latest_build
    log(f"serial build id: {build_id or 'not seen'}"
        + (f"  (all seen: {banner.build_ids})" if len(banner.build_ids) > 1 else ""))

    allowed = case["allowed"]
    verdict = "PASS" if after_state in allowed else "FAIL"
    if after_state == "unreachable":
        verdict = "FAIL"
    print()
    print("=" * 78)
    log(f"{verdict}  case={args.case} state={after_state} allowed={'/'.join(allowed)}")
    if build_id and package.build_id:
        if build_id == package.build_id:
            log(f"the running application IS the image just sent ({build_id})")
        else:
            log(f"the running application is {build_id!r}, NOT the {package.build_id!r} that was "
                f"sent -- the previous image survived")
    elif after_state == "application" and args.port and not build_id:
        log("no boot banner was captured, so which image is running is NOT established here")
    print("=" * 78)

    if args.json:
        args.json.write_text(json.dumps({
            "case": args.case, "verdict": verdict, "ran_at": datetime.now().isoformat(),
            "probe": probe.sensor_id, "package": str(args.package),
            "package_build_id": package.build_id, "package_sha256": package.sha256,
            "before": {"state": before_state, "detail": before_detail},
            "after": {"state": after_state, "detail": after_detail},
            "allowed": list(allowed), "killed_at": killed_at, "flasher_exit": code,
            "serial_build_id": build_id, "serial_build_ids": banner.build_ids,
            "serial_lines": banner.lines[-40:],
            "transcript": transcript,
        }, indent=2), encoding="utf-8")
        log(f"wrote {args.json}")

    return 0 if verdict == "PASS" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--probe", type=int, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--case", choices=sorted(CASES), required=True)
    parser.add_argument("--port", help="USB CDC port for the boot banner, e.g. COM9")
    parser.add_argument("--observe-before", type=float, default=20.0)
    parser.add_argument("--observe-after", type=float, default=90.0,
                        help="Must comfortably exceed the 60s application advertising interval, "
                             "or a healthy board looks dead (default 90)")
    parser.add_argument("--serial-seconds", type=float, default=25.0)
    parser.add_argument("--flasher-arg", action="append",
                        help="Extra argument passed through to ble_ota_update.py; repeatable")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if not args.package.is_file():
        parser.error(f"package not found: {args.package}")
    return asyncio.run(main_async(args))


if __name__ == "__main__":
    raise SystemExit(main())
