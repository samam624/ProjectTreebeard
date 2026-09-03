#!/usr/bin/env python3
"""Fail-closed structural, hash/CRC, target, version, and signature checks for MSV3 DFU ZIPs."""

import argparse
import binascii
import hashlib
import json
import zipfile
from pathlib import Path

from ecdsa import SigningKey
from ecdsa.keys import sigdecode_string


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--device-type", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--application-version", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--key", type=Path, help="Required for a signed 0.8 package")
    args = parser.parse_args()

    with zipfile.ZipFile(args.package, "r") as archive:
        manifest = json.loads(archive.read("manifest.json"))["manifest"]
        dfu_version = manifest["dfu_version"]
        if "application" not in manifest:
            raise SystemExit("package has no application")

        for name, entry in manifest.items():
            if name == "dfu_version":
                continue
            init = entry["init_packet_data"]
            if init["device_type"] != args.device_type:
                raise SystemExit(f"{name}: wrong device type {init['device_type']:#06x}")
            if init["application_version"] != args.application_version:
                raise SystemExit(f"{name}: wrong application version")
            image = archive.read(entry["bin_file"])

            if dfu_version == 0.5:
                actual_crc = binascii.crc_hqx(image, 0xFFFF)
                if actual_crc != init["firmware_crc16"]:
                    raise SystemExit(f"{name}: CRC16 mismatch")
            elif dfu_version == 0.8:
                if args.key is None:
                    raise SystemExit("signed package verification requires --key")
                if len(image) != init["firmware_length"]:
                    raise SystemExit(f"{name}: image-length mismatch")
                actual_hash = hashlib.sha256(image).hexdigest()
                if actual_hash != init["firmware_hash"]:
                    raise SystemExit(f"{name}: SHA-256 mismatch")
                dat = archive.read(entry["dat_file"])
                signature = bytes.fromhex(init["init_packet_ecds"])
                if dat[-64:] != signature:
                    raise SystemExit(f"{name}: signature is not the final init-packet field")
                signing_key = SigningKey.from_pem(args.key.read_bytes())
                if not signing_key.get_verifying_key().verify(
                    signature, dat[:-64], hashfunc=hashlib.sha256, sigdecode=sigdecode_string
                ):
                    raise SystemExit(f"{name}: ECDSA signature verification failed")
            else:
                raise SystemExit(f"unsupported DFU version {dfu_version}")

    print(f"Verified {args.package}: DFU {dfu_version}, target {args.device_type:#06x}, "
          f"application version {args.application_version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
