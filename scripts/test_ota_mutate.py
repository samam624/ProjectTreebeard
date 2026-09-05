#!/usr/bin/env python3
"""Deterministic tests for ota_mutate_package, the tampered-image builder.

The hardware test that uses it (`ota_negative_test.py`) is slow, destroys the bench board's
application on every payload case, and cannot run in CI. This file is what keeps it honest between
runs: it asserts that each mutation really produces the defect its name claims, because a "bad"
package that is not actually bad would make the bench test pass while proving nothing.

The load-bearing test is `test_every_case_is_refused_by_the_production_gate`. Structural assertions
about bytes are only self-consistent -- they check the mutator against its own idea of the format.
Feeding each mutated package to the REAL `verify_package_integrity()` checks it against the code
that actually guards the fleet, and pins the two together: if the package format or the gate
changes, this fails rather than silently generating packages that are wrong in some new way.

Run:  python scripts/test_ota_mutate.py
"""

from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from msv3_fleet import FleetError, require_ecdsa, verify_package_integrity  # noqa: E402
from ota_mutate_package import CASE_ASSERTIONS, MutationError, mutate  # noqa: E402

FAILURES = []
TMP = Path(tempfile.mkdtemp(prefix="msv3-mutate-"))
BUILD_ID = "MSV3-BUILD:2026-09-05-negtest-base"


def check(name, condition, detail=""):
    if condition:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


def raises(name, exception_type, call, must_contain=None):
    try:
        call()
    except exception_type as error:
        if must_contain and must_contain.lower() not in str(error).lower():
            check(name, False, f"message lacked {must_contain!r}: {error}")
        else:
            check(name, True)
        return
    except Exception as error:  # noqa: BLE001
        check(name, False, f"raised {type(error).__name__} instead: {error}")
        return
    check(name, False, "did not raise")


# ------------------------------------------------------------------ fixtures ----

def make_image(size=4096, build_id=BUILD_ID):
    """A stand-in application image carrying the NUL-terminated build id the firmware stores."""
    body = bytearray(b"\x00" * size)
    blob = build_id.encode() + b"\x00"
    body[1024:1024 + len(blob)] = blob
    return bytes(body)


def make_signed_package(path, image, signing_key, application_version=65538,
                        device_type=0x3356, dat_override=None, dfu_version=0.8):
    """Build a package with the REAL 52-byte legacy init packet, not a simplified stand-in.

    The layout is asserted below rather than trusted: `ota_mutate_package` edits this structure by
    byte offset, so a fixture that got it wrong would test the mutator against a fiction.
    """
    firmware_hash = hashlib.sha256(image).digest()
    body = (struct.pack("<HHIHH", device_type, 52840, application_version, 1, 0xB6)
            + struct.pack("<II", 2, len(image))
            + firmware_hash)
    assert len(body) == 52, len(body)

    ecdsa = require_ecdsa()
    from ecdsa.keys import sigencode_string
    signature = signing_key.sign(body, hashfunc=hashlib.sha256, sigencode=sigencode_string)
    dat = dat_override if dat_override is not None else body + signature

    manifest = {"manifest": {
        "dfu_version": dfu_version,
        "application": {
            "bin_file": "app.bin", "dat_file": "app.dat",
            "init_packet_data": {
                "device_type": device_type, "device_revision": 52840,
                "application_version": application_version, "softdevice_req": [0xB6],
                "ext_packet_id": 2, "firmware_length": len(image),
                "firmware_hash": firmware_hash.hex(), "init_packet_ecds": signature.hex(),
            }}}}
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest, indent=4))
        archive.writestr("app.bin", image)
        archive.writestr("app.dat", dat)
    return Path(path)


def read_parts(path):
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read("manifest.json"))["manifest"]["application"]
        return manifest, archive.read("app.bin"), archive.read("app.dat")


def new_key():
    ecdsa = require_ecdsa()
    return ecdsa.SigningKey.generate(curve=ecdsa.NIST256p)


def public_point(signing_key):
    return signing_key.get_verifying_key().to_string()


# --------------------------------------------------------------------- tests ----

def test_baseline_is_valid():
    """If the baseline were not accepted, every rejection below would prove nothing."""
    key = new_key()
    package = make_signed_package(TMP / "base.zip", make_image(), key)
    try:
        verify_package_integrity(package, 0x3356, public_point(key))
        check("the unmutated baseline passes the production integrity gate", True)
    except FleetError as error:
        check("the unmutated baseline passes the production integrity gate", False, str(error))


def test_payload_corrupt_keeps_a_valid_signature():
    key = new_key()
    base = make_signed_package(TMP / "pc-base.zip", make_image(), key)
    _, original_image, original_dat = read_parts(base)

    record = mutate(base, "payload-corrupt", TMP / "pc.zip")
    manifest, image, dat = read_parts(TMP / "pc.zip")

    check("payload-corrupt changes exactly one image byte",
          len(image) == len(original_image)
          and sum(a != b for a, b in zip(image, original_image)) == 1)
    check("payload-corrupt leaves the init packet byte-identical", dat == original_dat)
    check("payload-corrupt leaves the declared hash describing the OLD image",
          manifest["init_packet_data"]["firmware_hash"] == hashlib.sha256(original_image).hexdigest())
    check("payload-corrupt reports the image no longer matching its declared hash",
          record["image_matches_declared_hash_and_length"] is False)
    check("payload-corrupt reports the signature still covering the init packet",
          record["signature_covers_this_init_packet"] is True)


def test_payload_rehashed_breaks_only_the_signature():
    key = new_key()
    base = make_signed_package(TMP / "pr-base.zip", make_image(), key)
    _, original_image, original_dat = read_parts(base)

    record = mutate(base, "payload-rehashed", TMP / "pr.zip")
    manifest, image, dat = read_parts(TMP / "pr.zip")

    check("payload-rehashed changes exactly one image byte",
          sum(a != b for a, b in zip(image, original_image)) == 1)
    check("payload-rehashed rewrites the hash inside the signed init packet",
          dat[20:52] == hashlib.sha256(image).digest())
    check("payload-rehashed rewrites the hash in manifest.json too",
          manifest["init_packet_data"]["firmware_hash"] == hashlib.sha256(image).hexdigest())
    check("payload-rehashed leaves the 64-byte signature untouched",
          dat[-64:] == original_dat[-64:])
    check("payload-rehashed leaves image and declared hash agreeing",
          record["image_matches_declared_hash_and_length"] is True)
    check("payload-rehashed reports the signature no longer covering the init packet",
          record["signature_covers_this_init_packet"] is False)


def test_init_tampered_leaves_the_image_genuine():
    key = new_key()
    base = make_signed_package(TMP / "it-base.zip", make_image(), key)
    _, original_image, original_dat = read_parts(base)

    mutate(base, "init-tampered", TMP / "it.zip")
    manifest, image, dat = read_parts(TMP / "it.zip")

    check("init-tampered does not touch the image at all", image == original_image)
    check("init-tampered bumps application_version in the signed init packet",
          struct.unpack("<I", dat[4:8])[0] == struct.unpack("<I", original_dat[4:8])[0] + 1)
    check("init-tampered bumps application_version in manifest.json too",
          manifest["init_packet_data"]["application_version"] == 65539)
    check("init-tampered leaves the signature untouched", dat[-64:] == original_dat[-64:])
    check("init-tampered leaves the hash still correct for the image",
          manifest["init_packet_data"]["firmware_hash"] == hashlib.sha256(image).hexdigest())


def test_every_case_is_refused_by_the_production_gate():
    """The one that matters: each mutation must be rejected by the real fleet-side check.

    This is what stops the bench test from passing on a package that was never actually invalid.
    """
    key = new_key()
    base = make_signed_package(TMP / "gate-base.zip", make_image(), key)
    point = public_point(key)
    for case in sorted(CASE_ASSERTIONS):
        mutate(base, case, TMP / f"gate-{case}.zip")
        raises(f"{case} is refused by verify_package_integrity",
               FleetError,
               lambda case=case: verify_package_integrity(TMP / f"gate-{case}.zip", 0x3356, point))


def test_corruption_site_stays_printable_and_inside_the_build_id():
    """A control character here would be invisible in the very banner the test reads."""
    key = new_key()
    base = make_signed_package(TMP / "site-base.zip", make_image(), key)
    _, original_image, _ = read_parts(base)
    mutate(base, "payload-corrupt", TMP / "site.zip")
    _, image, _ = read_parts(TMP / "site.zip")

    index = next(i for i, (a, b) in enumerate(zip(image, original_image)) if a != b)
    start = original_image.find(b"MSV3-BUILD:")
    check("the corrupted byte lands inside the build-id string",
          start < index < start + len(BUILD_ID))
    check("the corrupted byte is printable ASCII", 0x20 <= image[index] < 0x7F,
          f"got {image[index]:#04x}")
    check("the corrupted byte differs from the original",
          image[index] != original_image[index])
    check("the corrupted build id is still readable as text",
          "Xegtest" in image[start:start + 64].decode("ascii", "replace"))


def test_refuses_packages_it_cannot_reason_about():
    key = new_key()

    unsigned = make_signed_package(TMP / "unsigned.zip", make_image(), key, dfu_version=0.5)
    raises("an unsigned (DFU 0.5) package is refused", MutationError,
           lambda: mutate(unsigned, "payload-corrupt", TMP / "unsigned-out.zip"),
           must_contain="not a signed")

    short = make_signed_package(TMP / "short.zip", make_image(), key,
                                dat_override=b"\x00" * 40)
    raises("an init packet of unexpected length is refused", MutationError,
           lambda: mutate(short, "payload-corrupt", TMP / "short-out.zip"),
           must_contain="init packet")

    # A baseline whose hash already disagrees with its image: mutating it would produce a package
    # that fails for a reason the test did not introduce, and the run would look like a pass.
    inconsistent = TMP / "inconsistent.zip"
    base = make_signed_package(TMP / "incon-base.zip", make_image(), key)
    manifest_raw, image, dat = read_parts(base)
    with zipfile.ZipFile(base) as src, zipfile.ZipFile(inconsistent, "w") as dst:
        for name in src.namelist():
            dst.writestr(name, image[:-1] + b"\xff" if name == "app.bin" else src.read(name))
    raises("an already-inconsistent baseline is refused", MutationError,
           lambda: mutate(inconsistent, "payload-corrupt", TMP / "incon-out.zip"),
           must_contain="already")

    no_marker = make_signed_package(TMP / "nomarker.zip", make_image(build_id="NOTHING-HERE"), key)
    raises("an image with no build-id marker is refused rather than corrupted blindly",
           MutationError,
           lambda: mutate(no_marker, "payload-corrupt", TMP / "nomarker-out.zip"),
           must_contain="could not find")

    raises("an unknown case name is refused", MutationError,
           lambda: mutate(make_signed_package(TMP / "case.zip", make_image(), key),
                          "not-a-case", TMP / "case-out.zip"))


def test_explicit_offset_is_honoured_and_bounds_checked():
    key = new_key()
    base = make_signed_package(TMP / "off-base.zip", make_image(), key)
    _, original_image, _ = read_parts(base)

    mutate(base, "payload-corrupt", TMP / "off.zip", offset=7)
    _, image, _ = read_parts(TMP / "off.zip")
    check("an explicit --offset corrupts that exact byte",
          image[7] != original_image[7]
          and sum(a != b for a, b in zip(image, original_image)) == 1)

    raises("an out-of-range --offset is refused", MutationError,
           lambda: mutate(base, "payload-corrupt", TMP / "off2.zip", offset=len(original_image)))


def main() -> int:
    print("ota_mutate_package tests")
    test_baseline_is_valid()
    test_payload_corrupt_keeps_a_valid_signature()
    test_payload_rehashed_breaks_only_the_signature()
    test_init_tampered_leaves_the_image_genuine()
    test_every_case_is_refused_by_the_production_gate()
    test_corruption_site_stays_printable_and_inside_the_build_id()
    test_refuses_packages_it_cannot_reason_about()
    test_explicit_offset_is_honoured_and_bounds_checked()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): " + ", ".join(FAILURES))
        return 1
    print("all mutation tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
