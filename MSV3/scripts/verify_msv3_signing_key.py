#!/usr/bin/env python3
"""Prove a RESTORED MSV3 signing key is the real one, and that it can still sign.

Why this exists
---------------
`MSV3/.secrets/msv3-dfu-signing-private.pem` is 227 bytes, gitignored, and the bootloader embeds
only its public half. Once the SWD pads are under conformal coating, losing every usable copy of
the private key means no probe can ever receive another application update -- not over BLE, not
over USB, not at all. The only escape would be reflashing a different bootloader over SWD, and
that is exactly what the coating removes.

So a backup has to exist. But "a file exists on a USB stick" is not the claim that matters. The
claim that matters is "a copy I can actually reach today still signs packages this fleet's
bootloaders will accept." That is what this script checks, and it is deliberately checkable rather
than a line in a checklist.

It never prints private key material.

What it proves, in order
------------------------
1. The candidate PEM parses as a NIST P-256 private key.
2. Its public point (Qx||Qy) hashes to the fingerprint `MSV3/fleet.json` pins per probe as
   `bootloader_key_sha256` -- i.e. it is the key the INSTALLED bootloaders were built to trust,
   not merely a valid key.
3. It agrees with the public key recorded in a release manifest.
4. It can produce a signature that verifies. Step 2 only compares public halves; a truncated or
   subtly corrupted private key can still carry a correct-looking public point in its encoding.
   Signing is what separates "the right key" from "a working copy of the right key".

Usage
-----
    python MSV3/scripts/verify_msv3_signing_key.py --key D:/backup/msv3-dfu-signing-private.pem
    python MSV3/scripts/verify_msv3_signing_key.py            # checks the working copy
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import msv3_fleet  # noqa: E402

DEFAULT_KEY = REPO_ROOT / "MSV3" / ".secrets" / "msv3-dfu-signing-private.pem"


class DrillError(Exception):
    pass


def load_private_key(path: Path):
    ecdsa = msv3_fleet.require_ecdsa()
    if not path.exists():
        raise DrillError(f"no key at {path}")
    try:
        key = ecdsa.SigningKey.from_pem(path.read_text(encoding="utf-8"))
    except Exception as error:  # noqa: BLE001 - any parse failure is the same verdict here
        raise DrillError(f"{path.name} did not parse as a PEM private key: {error}") from error
    if key.curve.name not in ("NIST256p", "prime256v1", "secp256r1"):
        raise DrillError(
            f"{path.name} is on curve {key.curve.name}, not NIST P-256. The bootloader verifies "
            f"P-256 only, so this key cannot be the fleet's signing key.")
    return key


def public_point(key) -> bytes:
    """The 64-byte Qx||Qy the bootloader embeds and fleet.json fingerprints."""
    point = key.get_verifying_key().pubkey.point
    return int(point.x()).to_bytes(32, "big") + int(point.y()).to_bytes(32, "big")


def registry_fingerprints() -> dict[str, list[int]]:
    """Every distinct bootloader_key_sha256 in the registry, and which probes pin it."""
    raw = json.loads((REPO_ROOT / "MSV3" / "fleet.json").read_text(encoding="utf-8"))
    pins: dict[str, list[int]] = {}
    for probe in raw["probes"]:
        fingerprint = (probe.get("bootloader_key_sha256") or "").lower()
        if fingerprint:
            pins.setdefault(fingerprint, []).append(probe["sensor_id"])
    return pins


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", type=Path, default=DEFAULT_KEY,
                        help="candidate private key PEM (default: the working copy)")
    parser.add_argument("--release", default="1.0.2",
                        help="release manifest to cross-check the public key against")
    args = parser.parse_args()

    print(f"MSV3 signing-key restore drill")
    print(f"  candidate: {args.key}")
    if args.key.resolve() == DEFAULT_KEY.resolve():
        print("  NOTE: this is the working copy, not a restored backup. The drill only means "
              "something\n        when it is pointed at a copy recovered from offline storage.")
    print()

    try:
        key = load_private_key(args.key)
    except DrillError as error:
        print(f"FAIL  {error}")
        return 1
    print(f"PASS  parses as a NIST P-256 private key ({args.key.stat().st_size} bytes)")

    point = public_point(key)
    fingerprint = hashlib.sha256(point).hexdigest()
    print(f"      public point fingerprint: {fingerprint}")

    pins = registry_fingerprints()
    if not pins:
        print("FAIL  MSV3/fleet.json pins no bootloader_key_sha256 for any probe, so there is "
              "nothing to check this key against.")
        return 1
    if fingerprint not in pins:
        print(f"FAIL  no probe in the registry pins this key. Registry pins:")
        for pinned, probes in pins.items():
            print(f"        {pinned}  (probes {', '.join(str(p) for p in probes)})")
        print("      This key cannot sign for any probe in the fleet. Either it is the wrong "
              "key,\n      or it is a NEW trust root that has not been installed over SWD.")
        return 1
    covered = ", ".join(str(p) for p in sorted(pins[fingerprint]))
    print(f"PASS  matches the fingerprint pinned by probes {covered}")

    try:
        manifest_key = msv3_fleet.load_release_public_key(args.release, fingerprint)
    except msv3_fleet.FleetError as error:
        print(f"FAIL  release manifest cross-check: {error}")
        return 1
    if manifest_key != point:
        print(f"FAIL  the {args.release} manifest records a different public point than this key "
              f"derives.")
        return 1
    print(f"PASS  agrees with the public key in the {args.release} release manifest")

    # The load-bearing step. A public-point match proves identity; only a signature proves the
    # private half survived the round trip through whatever storage this copy came from.
    message = b"msv3 signing-key restore drill " + os.urandom(16)
    try:
        signature = key.sign(message, hashfunc=hashlib.sha256)
        key.get_verifying_key().verify(signature, message, hashfunc=hashlib.sha256)
    except Exception as error:  # noqa: BLE001
        print(f"FAIL  the key parsed and matched, but could not produce a verifiable signature: "
              f"{error}")
        return 1
    print(f"PASS  signed a challenge and verified it ({len(signature)} byte signature)")

    print()
    print("DRILL PASSED. This copy can sign packages the installed bootloaders will accept.")
    print("Record where it came from, and keep at least two independent copies.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except DrillError as error:
        print(f"FAIL  {error}")
        sys.exit(1)
