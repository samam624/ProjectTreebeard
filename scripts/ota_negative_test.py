#!/usr/bin/env python3
"""Offer a real MSV3 board a tampered image and prove it refuses to run it.

This closes the last open item from MSV3_BLE_OTA_PROOF_2026-09-02.md. Everything the OTA work has
built so far -- `ReceiptGuard`, `verify_package_integrity()`, the fleet registry -- runs on the
HOST. A host is precisely what a corrupted download, a buggy script or an attacker substitutes, so
none of it can be the integrity boundary. The boundary is the bootloader's own SHA-256 and P-256
check over what it actually stored, and until a board has been handed something bad and refused
it, that is an assumption about somebody else's code, not a result.

TWO ASSERTIONS, DELIBERATELY SEPARATE

  Phase 1, the host gate. The production flasher must refuse each mutated package outright, before
  it touches the radio. Cheap, and it is what protects a FIELD probe: a refusal here costs nothing,
  while every device-side rejection below costs the running application.

  Phase 2, the device gate. The mutated package is then pushed anyway, through
  `ble_dfu_recover.py` -- the recovery flasher, which by design carries no fleet preflight. This is
  not a contrivance to defeat our own check: it is the realistic path, because recovery is exactly
  when someone reaches for the tool that asks fewer questions, and it is the tool a bad actor or a
  panicking operator would use. What must hold is that the DEVICE says no.

WHY THIS RUNS ON THE BENCH BOARD ONLY

  A single-bank bootloader erases the application when Start DFU is accepted -- before it can
  possibly know the incoming image is bad. So a correctly-working device gate still costs the old
  firmware: the expected good outcome of the payload cases is a board sitting in BLE recovery with
  no application, which then has to be reflashed. That is fine on a USB-recoverable bench board and
  is not fine on a coated probe in a pot. This harness reflashes the good image at the end of every
  case for exactly that reason, and reports whether the board came back.

WHAT COUNTS AS A PASS

Not "the transfer failed" -- a transfer can fail for a flat battery or a dropped link, which
proves nothing about integrity checking. The assertions are:

  1. the device answered a DFU stage with a REFUSING STATUS BYTE -- not merely that the transfer
     failed. A failed scan, a dropped link and a rebooting bootloader all fail to complete a
     transfer too, and every one of them has happened on this bench, and
  2. the corrupted build id NEVER appears -- not in a boot banner, not over the Device Information
     Service, not anywhere. This is the real assertion. The mutation is placed in the build-id
     string precisely so that a wrongly-ACCEPTED image announces itself instead of failing
     silently, and
  3. the board is still reachable afterwards, and takes a good image again.

Assertion 2 is the one that would catch the failure that matters. A bootloader that skipped its
hash check would sail through 1 and 3. It applies only to the cases that alter the image, though:
init-tampered leaves the image genuine on purpose, so its package carries the SAME build id and
the check cannot discriminate. The harness says so out loud rather than counting it as a pass.
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

from msv3_fleet import FleetError, load_fleet, read_package  # noqa: E402
from ota_interrupt_test import BannerWatcher, observe  # noqa: E402
from ota_mutate_package import CASE_ASSERTIONS, MutationError, mutate  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
FLASHER = REPO_ROOT / "scripts" / "ble_ota_update.py"
RECOVER = REPO_ROOT / "scripts" / "ble_dfu_recover.py"

# The flasher prints this immediately after the bootloader has been confirmed on the air and
# immediately before the first byte of image data. Killing here leaves the board in recovery with
# nothing written, which is the state the mutated push needs to start from.
JUMP_DONE_MARKER = "Transferring "


def log(message):
    print(f"[{datetime.now():%H:%M:%S}] {message}", flush=True)


def run(command, timeout_s, kill_on=None):
    """Run a flasher as a subprocess, streaming its output, optionally killing it at a marker."""
    log(f"$ {' '.join(str(part) for part in command)}")
    process = subprocess.Popen([str(part) for part in command], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, bufsize=1,
                               creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
    transcript, killed = [], False
    deadline = time.time() + timeout_s
    try:
        for line in process.stdout:
            line = line.rstrip("\n")
            if line:
                transcript.append(line)
                print(f"    | {line}", flush=True)
            if kill_on and not killed and kill_on in line:
                killed = True
                log(f"reached {kill_on!r} -- stopping the flasher here, nothing written")
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               capture_output=True)
                break
            if time.time() > deadline:
                log("timeout; killing")
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               capture_output=True)
                break
    finally:
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
    return process.returncode, transcript, killed


def live_bootloader_address(seen):
    """The address the bootloader is ACTUALLY on, taken from the scan rather than assumed.

    Both of a probe's addresses are legitimate bootloader locations -- after a buttonless jump it
    keeps the application's, and only a bootloader starting with no valid application uses +1 --
    so which one is live depends on how the board got here. `observe()` only ever reports the two
    this probe owns, so anything returned here is provably still this board.
    """
    for address, entry in seen.items():
        if entry.get("dfu"):
            return address
    return None


# What a SUCCESSFUL install looks like, taken from the code that prints it rather than assumed.
#
# `dfu_lib.perform_update()` returns after "DFU Complete."; if the link drops while the activate
# reset is in flight it catches that, prints "Device disconnected during reset. Update Successful."
# and returns successfully too -- a normal outcome, since the board really is resetting. The third
# is `ble_ota_update.py`'s own wrapper line, which only that tool prints.
#
# The first version of this list had ONLY the wrapper line, so nothing the recovery flasher printed
# on success could ever match. That is not a cosmetic bug: a device that ACCEPTED a tampered image
# would have produced no match either, and `device_verdict()` would have called it "rejected" --
# a false pass on the one assertion this whole harness exists to make. It was caught because a
# recovery reflash that had demonstrably worked (the board's own boot banner reported the new
# build one second later) was scored as a transport failure.
SUCCESS_MARKERS = ("DFU Complete.", "Update Successful.", "Transfer completed")


def transfer_completed(transcript):
    """Whether the DEVICE acknowledged and activated the image. See SUCCESS_MARKERS."""
    return any(marker in line for line in transcript for marker in SUCCESS_MARKERS)


def reached_the_device(transcript):
    """Whether the push actually got as far as talking to the bootloader.

    This distinction is the whole difference between a result and a false pass. A run that never
    found the device also never completed a transfer, so a naive "did not complete => rejected"
    reading scores a failed BLE scan as proof that the bootloader refused a tampered image. It
    happened on the first run of this harness: the recovery flasher's 5-second `discover()` came
    up empty while the bootloader was, on a scan seconds later, advertising 244 packets in 25 s.

    `ble_dfu_recover.py` prints this line only after discovery has succeeded and it holds a real
    device handle, so it is the honest marker for "the device was given the chance to say no".
    """
    return any("Found recovery bootloader" in line for line in transcript)


# A REAL device refusal, distinguished from a timeout wearing the same words.
#
# dfu_lib's `_wait_for_response()` returns -1 both when it times out and when the notification
# carries the wrong op code, and its callers only test `status != 1`. So "Validation failed.
# Status: -1" means the device said NOTHING, while "Validation failed. Status: 5" means it
# answered CRC_ERROR. Matching the sentence and ignoring the number would file a silent device as
# a refusal -- the same false-pass shape as the two bugs already fixed here.
#
# "Start DFU sequence failed" is deliberately NOT in this list even though it is a rejection path,
# because dfu_lib formats it without the status byte: a Start DFU that was refused and one that
# timed out produce the identical string, so it cannot support a claim either way.
DEVICE_STATUS_RE = re.compile(r"(Init Packet|Upload|Validation) failed\. Status: (-?\d+)")

# ble_dfu.h: 1 SUCCESS, 2 INVALID_STATE, 3 NOT_SUPPORTED, 4 DATA_SIZE, 5 CRC_ERROR, 6 OPER_FAILED.
VALID_REFUSAL_STATUSES = {2, 3, 4, 5, 6}


# What each case must be refused BY, not merely that it was refused.
#
# An explicit refusal proves the device answered; it does not prove WHICH check answered. A
# mutation of the init packet that came back INVALID_STATE (2) would be a real refusal and still
# tell you nothing about signature verification -- the board might simply have been holding a
# stale session. So each case declares the pair it must produce, derived from the source rather
# than from the observed runs:
#
#   nrf_err_code_translate() (dfu_transport_ble.c) maps NRF_ERROR_INVALID_DATA to CRC_ERROR (5)
#   when the procedure is VALIDATE, and to OPER_FAILED (6) otherwise. dfu_init_prevalidate()
#   returns INVALID_DATA on a failed P-256 verify during the INIT procedure -> Init Packet / 6.
#   dfu_init_postvalidate() returns INVALID_DATA on a signed-length mismatch or a digest mismatch
#   during VALIDATE -> Validation / 5.
#
# A refusal that does not match is reported as INCONCLUSIVE FOR THE MECHANISM rather than a pass:
# the image was still refused, but this run did not demonstrate the check it was built to exercise.
CASE_EXPECTATIONS = {
    "payload-corrupt": ("Validation", 5, "dfu_init_postvalidate() digest comparison"),
    "payload-corrupt-code": ("Validation", 5, "dfu_init_postvalidate() digest comparison"),
    "image-truncated": ("Validation", 5, "dfu_init_postvalidate() signed-length check"),
    "image-extended": ("Validation", 5, "dfu_init_postvalidate() signed-length check"),
    "payload-rehashed": ("Init Packet", 6, "dfu_init_prevalidate() P-256 verify"),
    "init-tampered": ("Init Packet", 6, "dfu_init_prevalidate() P-256 verify"),
    "sig-corrupt": ("Init Packet", 6, "dfu_init_prevalidate() P-256 verify"),
}


def device_refusals(transcript):
    """Every (stage, status) the DEVICE actually answered with. Timeouts are excluded."""
    found = []
    for line in transcript:
        match = DEVICE_STATUS_RE.search(line)
        if match and int(match.group(2)) in VALID_REFUSAL_STATUSES:
            found.append((match.group(1), int(match.group(2))))
    return found


def matches_expectation(case, refusals):
    """Did the device refuse by the check this case was built to exercise?

    Returns (matched, expected). `matched` is False both when nothing was refused and when
    something else refused it -- an INVALID_STATE from a stale session is a real refusal that
    demonstrates nothing about signature or digest verification.
    """
    expected = CASE_EXPECTATIONS.get(case)
    if expected is None:
        return False, None
    stage, status, _ = expected
    return any(s == stage and c == status for s, c in refusals), expected


def device_verdict(transcript):
    """Classify one push attempt. The three outcomes are NOT interchangeable.

      completed  the device accepted and activated the image
      rejected   the device answered a DFU stage with a failing status byte -- a real refusal
      transport  the device was reached but the attempt died in the link or the host stack
      not_found  discovery never produced a device

    Collapsing the last two into "rejected" is the classic way this kind of test passes for the
    wrong reason: every one of them also fails to complete a transfer. Both have already happened
    on this bench -- a scan that returned nothing while the bootloader was plainly advertising,
    and a WinRT "operation was canceled" while the board rebooted out of a stale DFU session.
    """
    if transfer_completed(transcript):
        return "completed"
    if device_refusals(transcript):
        return "rejected"
    if reached_the_device(transcript):
        return "transport"
    return "not_found"


def push(package, address, timeout_s, retries=3, settle=4.0):
    """Send a package to a bootloader, retrying everything EXCEPT a real device verdict.

    A rejection and a completion are both answers and are returned immediately -- retrying a
    rejection would be pointless and slow, since each payload case is a 135 kB transfer. Discovery
    and transport failures are not answers, and both are routine here:

      * Windows regularly returns nothing from a `discover()` issued moments after another process
        stopped its own scanner.
      * After a refused image the bootloader holds the dead session's state, answers the next
        Start DFU with INVALID_STATE, and dfu_lib resets it -- so the board is rebooting, not
        broken, and the write that carried the reset dies with a WinRT cancellation.
        `ble_ota_update.py` handles that case itself (--stale-session-retries); the recovery
        flasher does not, so the retry has to live here.
    """
    code, transcript = 1, []
    for attempt in range(1, retries + 1):
        # Let the Windows BLE stack settle after whatever scanner ran before this, and give a
        # resetting bootloader time to come back and re-advertise.
        time.sleep(settle)
        code, transcript, _ = run(
            [sys.executable, "-u", RECOVER, package, "--device", address,
             "--retry", "1", "--high-mtu"], timeout_s=timeout_s)
        verdict = device_verdict(transcript)
        if verdict in ("completed", "rejected"):
            return code, transcript
        log(f"attempt {attempt}/{retries} ended in '{verdict}', which is NOT a device verdict; "
            f"retrying")
        settle = max(settle, 12.0)  # a rebooting bootloader needs longer than a settling scanner
    return code, transcript


def rejection_evidence(transcript):
    """Pull out how the device said no, so the report records a mechanism and not just a failure.

    The legacy DFU control point answers with a status byte, and the two that matter here are
    different findings: an image the bootloader hashed and rejected is not the same event as an
    init packet whose signature did not verify.
    """
    markers = ("status", "Status", "INVALID", "failed", "Failed", "Error", "error",
               "Exception", "Traceback", "signature", "CRC", "Validate", "Activat")
    return [line for line in transcript if any(marker in line for marker in markers)][-12:]


async def run_case(args, probe, case, good_package, bad_package, mutation):
    print("=" * 78, flush=True)
    log(f"CASE {case}")
    log(f"asserts   {CASE_ASSERTIONS[case]}")
    log(f"target    {probe.describe()}")
    for note in mutation["notes"]:
        log(f"mutation  {note}")

    corrupt_build_id = read_package(bad_package).build_id
    good_build_id = read_package(good_package).build_id
    log(f"good build id      {good_build_id}")
    log(f"corrupted build id {corrupt_build_id}  <-- must never be observed")

    banner = BannerWatcher(args.port).start()
    result = {
        "case": case, "asserts": CASE_ASSERTIONS[case], "mutation": mutation,
        "good_build_id": good_build_id, "corrupt_build_id": corrupt_build_id,
        "started": datetime.now().isoformat(timespec="seconds"),
    }
    try:
        # --- before -------------------------------------------------------------------------
        state, detail, seen = await observe(probe.bootloader_addresses, args.observe_before)
        log(f"before: {state} -- {detail}")
        result["before"] = {"state": state, "detail": detail}

        # --- phase 1: the host gate ---------------------------------------------------------
        log("phase 1: the production flasher must refuse this package before the radio")
        code, transcript, _ = run(
            [sys.executable, "-u", FLASHER, bad_package, "--probe", probe.sensor_id,
             "--preflight-only"], timeout_s=120)
        result["host_gate"] = {
            "exit_code": code,
            "refused": code != 0,
            "reason": next((line for line in transcript if "REFUSED" in line), None),
        }
        log(f"phase 1: {'REFUSED (correct)' if code != 0 else 'ACCEPTED -- THIS IS A DEFECT'}")

        # --- get the board into its bootloader ----------------------------------------------
        if state != "recovery":
            log("jumping the board into its bootloader with the GOOD package, then stopping "
                "before any data is written")
            code, transcript, killed = run(
                [sys.executable, "-u", FLASHER, good_package, "--probe", probe.sensor_id,
                 "--reflash-same-build"], timeout_s=args.jump_timeout,
                kill_on=JUMP_DONE_MARKER)
            if not killed:
                result["verdict"] = "INCONCLUSIVE"
                result["why"] = ("could not park the board in its bootloader, so the device gate "
                                 "was never exercised")
                return result
            state, detail, seen = await observe(probe.bootloader_addresses, args.observe_short)
            log(f"parked: {state} -- {detail}")
            if state != "recovery":
                result["verdict"] = "INCONCLUSIVE"
                result["why"] = f"expected the bootloader on the air, found {state}: {detail}"
                return result

        # --- phase 2: the device gate -------------------------------------------------------
        target = live_bootloader_address(seen)
        if target is None:
            result["verdict"] = "INCONCLUSIVE"
            result["why"] = "no DFU bootloader on either of this probe's addresses"
            return result
        log(f"phase 2: pushing the MUTATED package to {target} through the recovery flasher, "
            f"which has no package preflight of its own")
        code, transcript = push(bad_package, target, args.transfer_timeout)
        verdict = device_verdict(transcript)
        completed = verdict == "completed"
        refusals = device_refusals(transcript)
        result["device_gate"] = {
            "exit_code": code,
            "verdict": verdict,
            "transfer_completed": completed,
            "rejected": verdict == "rejected",
            # The stage and status byte, so the record says HOW the device refused rather than
            # only that it did. payload-corrupt reaching Validate/5 and payload-rehashed reaching
            # Init Packet/6 are different findings about different checks.
            "refusals": [{"stage": stage, "status": status} for stage, status in refusals],
            "evidence": rejection_evidence(transcript),
            "transcript": transcript,
        }
        matched, expected = matches_expectation(case, refusals)
        result["device_gate"]["expected"] = (
            None if expected is None
            else {"stage": expected[0], "status": expected[1], "mechanism": expected[2]})
        result["device_gate"]["matched_expectation"] = matched
        if refusals:
            for stage, status in refusals:
                log(f"    device refused at {stage} with status {status}")
        if expected and refusals and not matched:
            log(f"    but the expected refusal was {expected[0]}/{expected[1]} "
                f"({expected[2]}) -- this run did not exercise that check")
        if verdict in ("not_found", "transport"):
            result["verdict"] = "INCONCLUSIVE"
            result["why"] = (
                f"the push ended in '{verdict}' rather than a device verdict, so the bootloader "
                f"was never given a clean chance to refuse the tampered image")
            return result
        log(f"phase 2: transfer {'COMPLETED -- THIS IS A DEFECT' if completed else 'rejected'}")

        # --- after ---------------------------------------------------------------------------
        state, detail, seen = await observe(probe.bootloader_addresses, args.observe_after)
        log(f"after: {state} -- {detail}")
        result["after"] = {"state": state, "detail": detail}

        # --- recover -------------------------------------------------------------------------
        log("reflashing the GOOD package to put the board back")
        recovery_target = live_bootloader_address(seen) or target
        code, transcript = push(good_package, recovery_target, args.transfer_timeout)
        recovered = transfer_completed(transcript)
        state, detail, _ = await observe(probe.bootloader_addresses, args.observe_after)
        log(f"recovered: {state} -- {detail}")
        result["recovery"] = {"exit_code": code, "transfer_completed": recovered,
                              "state": state, "detail": detail}

        time.sleep(args.serial_seconds)
    finally:
        banner.stop()

    seen = list(banner.build_ids)
    result["serial_build_ids"] = seen
    result["serial_lines"] = banner.lines[-60:]

    # The build-id assertion only discriminates when the mutation actually CHANGED the build id.
    # init-tampered deliberately leaves the image genuine -- it attacks the init packet alone -- so
    # its "corrupted" package carries the same build id as the good one. Scoring that case on
    # build id would flag the healthy post-recovery boot as proof a tampered image ran, which is
    # backwards. Where it does not discriminate, say so rather than quietly scoring a pass.
    build_id_discriminates = (corrupt_build_id is not None
                              and corrupt_build_id != good_build_id)
    result["build_id_discriminates"] = build_id_discriminates
    corrupt_seen = build_id_discriminates and corrupt_build_id in seen

    failures, inconclusive = [], []
    if not result["host_gate"]["refused"]:
        failures.append("the production flasher ACCEPTED a tampered package at preflight")
    if result["device_gate"]["transfer_completed"]:
        # Protocol acceptance is already a defect whether or not the image then ran; the build id
        # only says which of the two happened. Reporting them differently matters because
        # "accepted and executed" and "accepted, execution unconfirmed" send you to different
        # places in the bootloader.
        if corrupt_seen:
            failures.append("the device ACCEPTED the tampered image AND EXECUTED it -- the "
                            "corrupted build id was observed running")
        else:
            failures.append("the device ACCEPTED the tampered image (execution unconfirmed: the "
                            "corrupted build id was not observed, but acceptance is the defect)")
    if not result["device_gate"]["rejected"]:
        failures.append("the device never returned a refusing DFU status")
    elif not result["device_gate"]["matched_expectation"]:
        expected = result["device_gate"]["expected"]
        got = ", ".join(f"{r['stage']}/{r['status']}"
                        for r in result["device_gate"]["refusals"]) or "nothing"
        inconclusive.append(
            f"the device refused ({got}) but not at the predeclared "
            f"{expected['stage']}/{expected['status']} for {expected['mechanism']}, so this run "
            f"does not demonstrate that check -- only that the image was rejected somehow")
    if corrupt_seen and not result["device_gate"]["transfer_completed"]:
        failures.append(f"the corrupted build id {corrupt_build_id} was OBSERVED RUNNING even "
                        f"though the transfer never completed -- the bootloader executed a "
                        f"tampered image")
    if not build_id_discriminates:
        log("note: this case does not change the build id, so the strongest assertion -- that a "
            "tampered image was never executed -- rests on the rejection stage alone")
    if result["after"]["state"] == "unreachable":
        failures.append("the board became unreachable after the rejection")
    if not result["recovery"]["transfer_completed"]:
        failures.append("the board did not take a good image afterwards")

    result["failures"] = failures
    result["inconclusive"] = inconclusive
    if failures:
        result["verdict"] = "FAIL"
    elif inconclusive:
        result["verdict"] = "INCONCLUSIVE"
        result["why"] = inconclusive[0]
    else:
        result["verdict"] = "PASS"
    return result


def main_async_entry(args):
    return asyncio.run(main_async(args))


async def main_async(args):
    try:
        fleet = load_fleet()
        probe = fleet.by_sensor_id(args.probe)
    except FleetError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    # The gate that keeps this off a plant, and it is deliberately absolute -- no override flag.
    #
    # A correctly-working device gate still destroys the running application: the erase happens in
    # dfu_start_pkt_handle(), before the bootloader has even received the init packet it would
    # reject. There is no cheap device-side refusal on a single-bank bootloader, so there is no
    # version of this test that a field probe survives intact.
    #
    # An escape hatch was drafted here and removed. preflight() in ble_ota_update.py already
    # refuses --allow-identity-mismatch on a deployed probe for the stated reason that fixing the
    # registry "leaves an auditable change behind, where a command-line flag does not". The same
    # reasoning applies with more force to a test whose expected outcome is an erased probe: if a
    # board is genuinely on the bench, say so in MSV3/fleet.json.
    if probe.deployed:
        print(f"error: probe {probe.sensor_id} is deployed=true in {fleet.source}.\n"
              f"  These cases leave the board with NO application -- the erase happens at Start "
              f"DFU, before the bootloader can know the image is bad.\n"
              f"  Run this on the bench board (probe 99). If this board really is on the bench, "
              f"set deployed=false in the registry rather than looking for a flag.",
              file=sys.stderr)
        return 2
    if not probe.ota_capable:
        print(f"error: probe {probe.sensor_id} is ota_capable=false; there is no bootloader here "
              f"to test.", file=sys.stderr)
        return 2

    good = args.good.resolve()
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    cases = sorted(CASE_ASSERTIONS) if args.case == "all" else [args.case]
    results = []
    for case in cases:
        bad = outdir / f"{good.stem}-{case}.zip"
        try:
            mutation = mutate(good, case, bad)
        except MutationError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2
        results.append(await run_case(args, probe, case, good, bad, mutation))

    print("=" * 78, flush=True)
    for result in results:
        print(f"{result['verdict']:12} {result['case']}", flush=True)
        for failure in result.get("failures", []):
            print(f"             - {failure}", flush=True)
        for note in result.get("inconclusive", []):
            print(f"             - {note}", flush=True)
        if result["verdict"] == "INCONCLUSIVE" and not result.get("inconclusive"):
            print(f"             - {result.get('why')}", flush=True)
        gate = result.get("device_gate", {})
        if gate.get("refusals"):
            shown = ", ".join(f"{r['stage']}/{r['status']}" for r in gate["refusals"])
            print(f"             refused at {shown}"
                  f"{' (as predicted)' if gate.get('matched_expectation') else ''}", flush=True)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(results, indent=2), encoding="utf-8")
        log(f"wrote {args.json}")

    return 0 if all(result["verdict"] == "PASS" for result in results) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--probe", type=int, default=99)
    parser.add_argument("--good", type=Path, required=True,
                        help="A VALID signed package for this probe; also used to recover it")
    parser.add_argument("--case", default="all",
                        choices=sorted(CASE_ASSERTIONS) + ["all"])
    parser.add_argument("--outdir", type=Path, required=True,
                        help="Where the mutated packages are written")
    parser.add_argument("--port", help="USB CDC port for the boot banner, e.g. COM9")
    parser.add_argument("--observe-before", type=float, default=20.0)
    parser.add_argument("--observe-short", type=float, default=15.0)
    parser.add_argument("--observe-after", type=float, default=90.0,
                        help="Must exceed the 60s application advertising interval, or a healthy "
                             "board looks dead (default 90)")
    parser.add_argument("--serial-seconds", type=float, default=20.0)
    parser.add_argument("--jump-timeout", type=float, default=240.0)
    parser.add_argument("--transfer-timeout", type=float, default=420.0)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if not args.good.is_file():
        parser.error(f"good package not found: {args.good}")
    return main_async_entry(args)


if __name__ == "__main__":
    raise SystemExit(main())
