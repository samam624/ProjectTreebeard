#!/usr/bin/env python3
"""Tests for ota_negative_test's outcome classifier.

`device_verdict()` decides whether a hardware run counts as the bootloader REFUSING a tampered
image. Everything the negative test concludes rests on it, and it is the part most likely to be
quietly wrong, because every wrong answer looks like the right one: a scan that found nothing, a
link that dropped, and a device that said no all fail to install an image.

Both mistakes it can make have already happened on the bench, and both are pinned here:

  * a failed BLE scan scored as a rejection (the bootloader was advertising 244 packets in 25 s
    seconds later), and
  * a SUCCESSFUL install scored as a transport failure, because the success markers were taken
    from a different tool than the one being run. That is the dangerous direction: had the
    bootloader accepted a tampered image, the same gap would have reported "rejected" and the
    test would have passed while proving the opposite of what it claims.

The transcripts below are real lines from `ble_dfu_recover.py` / `dfu_lib`, not invented ones.

Run:  python scripts/test_ota_negative.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ota_negative_test import (  # noqa: E402
    CASE_EXPECTATIONS,
    device_refusals,
    device_verdict,
    matches_expectation,
    reached_the_device,
    transfer_completed,
)
from ota_mutate_package import CASE_ASSERTIONS  # noqa: E402

FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


FOUND = "Found recovery bootloader AdaDFU at DB:AD:4D:9D:21:BB (attempt 1/1)"

SUCCESS = [
    FOUND, "Connected to Bootloader. MTU: 247", "Sending Init Packet...",
    "Application transfer: 100%", "Validating...", "Activating & Resetting...", "DFU Complete.",
]
SUCCESS_VIA_DISCONNECT = [
    FOUND, "Application transfer: 100%", "Validating...", "Activating & Resetting...",
    "Device disconnected during reset. Update Successful.",
]
REJECTED_AT_VALIDATE = [
    FOUND, "Application transfer: 100%", "Validating...",
    "Attempt 1 failed: Validation failed. Status: 5",
    "dfu_lib.DfuException: Validation failed. Status: 5",
]
REJECTED_AT_INIT = [
    FOUND, "Sending Init Packet...", "Attempt 1 failed: Init Packet failed. Status: 6",
    "dfu_lib.DfuException: Init Packet failed. Status: 6",
]
NOT_FOUND = [
    "Traceback (most recent call last):", 'raise DfuException("Device not found.")',
    "dfu_lib.DfuException: Device not found.",
]
TRANSPORT = [
    FOUND, "Connected to Bootloader. MTU: 247",
    "await client.write_gatt_char(DFU_CONTROL_POINT_UUID, bytearray([OP_CODE_RESET]))",
    "OSError: [WinError -2147023673] The operation was canceled by the user.",
]


def test_success_is_never_mistaken_for_a_rejection():
    """The dangerous direction. A device that accepted the image must never read as a refusal."""
    for name, transcript in (("DFU Complete.", SUCCESS),
                             ("disconnect-during-reset", SUCCESS_VIA_DISCONNECT)):
        check(f"a successful install via {name} is classified 'completed'",
              device_verdict(transcript) == "completed",
              f"got {device_verdict(transcript)!r}")
        check(f"a successful install via {name} is not read as a rejection",
              device_verdict(transcript) != "rejected")
        check(f"transfer_completed() recognises {name}", transfer_completed(transcript))


def test_real_device_refusals_are_rejections():
    check("a Validate-stage refusal (status 5) is 'rejected'",
          device_verdict(REJECTED_AT_VALIDATE) == "rejected")
    check("an Init-Packet-stage refusal (status 6) is 'rejected'",
          device_verdict(REJECTED_AT_INIT) == "rejected")


def test_failures_that_are_not_answers():
    check("a scan that found nothing is 'not_found', not a rejection",
          device_verdict(NOT_FOUND) == "not_found",
          f"got {device_verdict(NOT_FOUND)!r}")
    check("a dropped link is 'transport', not a rejection",
          device_verdict(TRANSPORT) == "transport",
          f"got {device_verdict(TRANSPORT)!r}")
    check("an empty transcript is 'not_found'", device_verdict([]) == "not_found")


def test_reached_the_device_tracks_discovery_only():
    check("discovery success is detected", reached_the_device([FOUND]))
    check("a transcript with no discovery line is not counted as reaching the device",
          not reached_the_device(NOT_FOUND))


def test_completion_wins_over_a_later_error_line():
    """A stack trace printed after activation must not downgrade a completed install.

    dfu_lib returns as soon as it prints DFU Complete., but the surrounding process can still emit
    disconnect noise on its way out; ordering must not decide the verdict.
    """
    noisy = SUCCESS + ["OSError: [WinError -2147023673] The operation was canceled by the user."]
    check("a completed install stays 'completed' despite trailing error noise",
          device_verdict(noisy) == "completed", f"got {device_verdict(noisy)!r}")


def test_timeouts_are_not_refusals():
    """dfu_lib prints the same sentence for a refusal and a timeout; only the number differs.

    `_wait_for_response()` returns -1 on asyncio.TimeoutError AND on a mismatched op code, and its
    callers only test `status != 1`. So "Validation failed. Status: -1" means the device said
    NOTHING. Matching the sentence and ignoring the number would file a silent device as a refusal.
    """
    for stage in ("Init Packet", "Upload", "Validation"):
        silent = [FOUND, f"Attempt 1 failed: {stage} failed. Status: -1"]
        check(f"a {stage} timeout (Status: -1) is not a refusal",
              device_refusals(silent) == [], f"got {device_refusals(silent)!r}")
        check(f"a {stage} timeout classifies as transport, not rejected",
              device_verdict(silent) == "transport", f"got {device_verdict(silent)!r}")

    # SUCCESS (1) is not a refusal either, and must never be scraped as one.
    check("Status: 1 is not collected as a refusal",
          device_refusals([FOUND, "Validation failed. Status: 1"]) == [])

    # "Start DFU sequence failed" carries no status byte at all, so it cannot support a claim.
    start = [FOUND, "dfu_lib.DfuException: Start DFU sequence failed"]
    check("a Start DFU failure is not counted as a refusal (it carries no status)",
          device_refusals(start) == [] and device_verdict(start) == "transport")


def test_every_case_declares_the_check_it_exercises():
    check("every mutation case has a predeclared (stage, status) expectation",
          set(CASE_EXPECTATIONS) == set(CASE_ASSERTIONS),
          f"missing: {set(CASE_ASSERTIONS) - set(CASE_EXPECTATIONS)}")


def test_a_refusal_by_the_wrong_mechanism_does_not_count():
    """An explicit refusal proves the device answered, not that the intended check answered."""
    matched, expected = matches_expectation("payload-corrupt", [("Validation", 5)])
    check("payload-corrupt refused at Validation/5 matches its expectation", matched)
    check("the expectation names the mechanism", expected[2].startswith("dfu_init_postvalidate"))

    # A stale session answers INVALID_STATE (2). Real refusal, wrong reason.
    matched, _ = matches_expectation("payload-corrupt", [("Init Packet", 2)])
    check("payload-corrupt refused by INVALID_STATE does NOT satisfy the digest check",
          not matched)

    # Right status, wrong stage: a signature case cannot be satisfied by the digest check.
    matched, _ = matches_expectation("sig-corrupt", [("Validation", 5)])
    check("sig-corrupt refused at Validation/5 does NOT satisfy the signature check", not matched)

    matched, _ = matches_expectation("sig-corrupt", [("Init Packet", 6)])
    check("sig-corrupt refused at Init Packet/6 does satisfy the signature check", matched)

    matched, _ = matches_expectation("payload-corrupt", [])
    check("no refusal at all never satisfies an expectation", not matched)


def main() -> int:
    print("ota_negative_test classifier tests")
    test_success_is_never_mistaken_for_a_rejection()
    test_real_device_refusals_are_rejections()
    test_failures_that_are_not_answers()
    test_reached_the_device_tracks_discovery_only()
    test_completion_wins_over_a_later_error_line()
    test_timeouts_are_not_refusals()
    test_every_case_declares_the_check_it_exercises()
    test_a_refusal_by_the_wrong_mechanism_does_not_count()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): " + ", ".join(FAILURES))
        return 1
    print("all classifier tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
