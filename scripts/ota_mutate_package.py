#!/usr/bin/env python3
"""Build deliberately-invalid MSV3 DFU packages, for proving the bootloader rejects them.

This exists for one open item in MSV3_BLE_OTA_PROOF_2026-09-02.md: until a mutated image has
actually been offered to a real board and refused, the integrity story is "the host-side window is
narrow", not "the device will not run a tampered image". `ReceiptGuard` and `verify_package_
integrity()` are both HOST-side; a host is exactly what an attacker or a corrupted download
replaces. The only claim worth making is about the bootloader's own SHA-256 and P-256 check, and
the only way to establish it is to send it something bad.

WHAT A SIGNED MSV3 PACKAGE ACTUALLY CONTAINS (DFU 0.8, adafruit-nrfutil `dfu genpkg`)

    manifest.json               bookkeeping, read by the HOST only -- the device never sees it
    <name>.bin                  the application image
    <name>.dat                  52-byte init packet + 64-byte P-256 signature

The 52-byte init packet is what ties the two together:

    offset  0   2   device_type      0x3356 for MSV3's own bootloader
            2   2   device_revision  52840
            4   4   application_version
            8   2   softdevice count
           10   2   softdevice req   0xB6 (S140 6.1.1)
           12   4   ext_packet_id    2 = ECDSA_P256_SHA256
           16   4   firmware_length
           20  32   firmware_hash    SHA-256 of the .bin
           52  64   signature        P-256 over init_packet[0:52]

So the image is covered TRANSITIVELY: the signature covers the hash and the length, and those
cover the image. Every case below breaks exactly one link in that chain, and names which.

THE CASES, AND WHY EACH IS A DIFFERENT ASSERTION

  payload-corrupt    one byte of the .bin flipped, .dat untouched.
                     The signature is still perfectly valid -- it signs an init packet nobody
                     touched. What is broken is the hash's description of the image. This is the
                     "corrupt a byte, keep the original CRC" case, and it is the one a bad radio
                     link or a lying host produces. It asserts the bootloader HASHES WHAT IT
                     STORED rather than trusting a correctly signed header.

  payload-rehashed   one byte of the .bin flipped, AND firmware_hash recomputed over the mutated
                     image in both the .dat and manifest.json. Signature NOT regenerated.
                     Now image and hash agree; the signature is the only thing that disagrees.
                     This is "corrupt it and recompute the CRC without re-signing", and it is what
                     an attacker who understands the format produces. It asserts the bootloader
                     VERIFIES THE SIGNATURE rather than merely checking self-consistency.

  init-tampered      the .bin is untouched; a signed init-packet field (application_version) is
                     edited. Isolates signature coverage from image content: the image is
                     genuine and its hash is correct, so anything that rejects this rejected it
                     on the signature alone. Cheap, and it distinguishes "the signature is
                     checked" from "the hash is checked" when a device rejects both other cases
                     with the same opaque status byte.

  sig-corrupt        one byte of the 64-byte SIGNATURE flipped; the image, the hash and all 52
                     signed bytes are genuine. Cryptographically redundant with payload-rehashed,
                     but it is the only case that attacks the signature without editing a field
                     the bootloader parses -- so a device that rejected payload-rehashed by
                     tripping over the changed hash rather than by verifying the signature would
                     be caught here and nowhere else.

  image-truncated    the image is 32 bytes shorter than the signed firmware_length says.
  image-extended     the image is 32 bytes longer. Neither touches the .dat.
                     These are about FRAMING rather than content: firmware_length is a signed
                     field, and dfu_init_postvalidate() checks it BEFORE it hashes, so they
                     exercise a different code path from the digest comparison.

  payload-corrupt-code
                     payload-corrupt with the byte in executable code instead of an inert string.
                     Supplemental only: it shows the digest covers all payload bytes, but its
                     "never ran" assertion is weaker, because a wrongly-accepted image with a
                     corrupted instruction may hardfault rather than announce itself.

WHERE THE BYTE IS FLIPPED, AND WHY IT IS NOT ARBITRARY

By default the corrupted byte lands inside the image's FIRMWARE_BUILD_ID string. That is chosen so
a WRONG outcome is loud rather than silent:

  * the string is inert -- no instruction decodes it, so a wrongly-accepted image still boots and
    still advertises. A mutation in code might crash instead, and "the board did not come back"
    cannot distinguish "correctly rejected" from "accepted and then hardfaulted".
  * it is echoed by the boot banner and by the Device Information Service, so if the bootloader
    ever DID accept a tampered image, the proof is a probe reporting a build id with a corrupted
    character in it -- unmistakable, and impossible to confuse with a rejection.

A test whose failure mode is silence is a test that passes for the wrong reason. This one's
failure mode is a probe telling you its own name is wrong.
"""

import argparse
import hashlib
import json
import zipfile
from pathlib import Path

INIT_HASH_OFFSET = 20
INIT_HASH_LENGTH = 32
INIT_APP_VERSION_OFFSET = 4
SIGNATURE_LENGTH = 64
DEFAULT_MARKER = b"MSV3-BUILD:"


class MutationError(Exception):
    pass


def _read_package(path: Path):
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        entry = manifest["manifest"]["application"]
        image = archive.read(entry["bin_file"])
        dat = archive.read(entry["dat_file"])
        names = archive.namelist()
    if float(manifest["manifest"]["dfu_version"]) != 0.8:
        raise MutationError(
            f"{path.name} is DFU {manifest['manifest']['dfu_version']}, not a signed 0.8 package. "
            f"There is no signature to attack, so none of these cases mean anything.")
    if len(dat) != 52 + SIGNATURE_LENGTH:
        raise MutationError(
            f"{path.name}: init packet is {len(dat)} bytes, expected {52 + SIGNATURE_LENGTH}. "
            f"The field offsets in this file would be wrong; refusing to guess.")
    if hashlib.sha256(image).hexdigest() != entry["init_packet_data"]["firmware_hash"]:
        raise MutationError(
            f"{path.name} is ALREADY inconsistent -- its declared hash does not match its image. "
            f"Mutating an already-broken package proves nothing. Rebuild the baseline first.")
    return manifest, entry, image, dat, names


def _find_corruption_offset(image: bytes, marker: bytes, explicit):
    """Pick the byte to flip, preferring somewhere inert and observable. See the module docstring."""
    if explicit is not None:
        if not 0 <= explicit < len(image):
            raise MutationError(f"--offset {explicit} is outside the {len(image)}-byte image")
        return explicit, "explicit --offset"

    start = image.find(marker)
    if start < 0:
        raise MutationError(
            f"could not find {marker!r} in the image, so there is no inert, observable place to "
            f"corrupt. Pass --offset to choose one deliberately -- but read the module docstring "
            f"first: a byte flipped in code can crash instead of booting, and a board that does "
            f"not come back cannot tell you whether it was rejected or accepted-then-died.")
    # Land on a LETTER inside the id's text, past the "MSV3-BUILD:" literal. Letters specifically,
    # because the corrupted byte has to survive a serial banner and a GATT string read as something
    # a human sees. The first version of this took a fixed offset, which landed on the '-' of a
    # date and turned it into a carriage return -- a corruption that a terminal renders as nothing
    # at all, in a test whose entire purpose is making a wrong outcome visible.
    for index in range(start + len(marker), min(start + len(marker) + 64, len(image))):
        if chr(image[index]).isalpha():
            return index, f"first letter of FIRMWARE_BUILD_ID's text, at image+{start}"
    raise MutationError(
        f"{marker!r} at image+{start} is not followed by any letter within 64 bytes, so there is "
        f"no site that stays printable when corrupted. Pass --offset deliberately.")


def _write_package(destination: Path, source: Path, names, manifest, entry, image, dat):
    """Rewrite the archive, preserving member names -- the device is told them by manifest.json."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in names:
            if name == "manifest.json":
                archive.writestr(name, json.dumps(manifest, indent=4))
            elif name == entry["bin_file"]:
                archive.writestr(name, image)
            elif name == entry["dat_file"]:
                archive.writestr(name, dat)
            else:
                with zipfile.ZipFile(source) as original:
                    archive.writestr(name, original.read(name))


def mutate(source: Path, case: str, destination: Path, marker: bytes = DEFAULT_MARKER,
           offset=None):
    manifest, entry, image, dat, names = _read_package(source)
    init = entry["init_packet_data"]
    notes = []

    if case in ("payload-corrupt", "payload-rehashed"):
        index, why = _find_corruption_offset(image, marker, offset)
        original_byte = image[index]
        # A printable substitution chosen so the damage READS as damage. 'X' against a lowercase
        # build id is unmistakable at a glance; the fallback only matters if the site already is
        # an 'X'. Never an XOR: that is what produced a control character last time.
        mutated_byte = ord("X") if original_byte != ord("X") else ord("Q")
        image = image[:index] + bytes([mutated_byte]) + image[index + 1:]
        window = image[max(0, index - 24):index + 24]
        notes.append(f"image[{index}] {original_byte:#04x} -> {mutated_byte:#04x} ({why})")
        notes.append(f"context now reads: {window.decode('ascii', 'replace')!r}")

        if case == "payload-rehashed":
            new_hash = hashlib.sha256(image).digest()
            dat = (dat[:INIT_HASH_OFFSET] + new_hash
                   + dat[INIT_HASH_OFFSET + INIT_HASH_LENGTH:])
            init["firmware_hash"] = new_hash.hex()
            notes.append(f"firmware_hash updated to {new_hash.hex()} in BOTH the init packet and "
                         f"manifest.json -- image and hash now agree")
            notes.append("signature deliberately NOT regenerated: it still signs the OLD hash")
        else:
            notes.append(f"firmware_hash left at {init['firmware_hash']} -- it now describes an "
                         f"image that no longer exists")
            notes.append("signature untouched and still mathematically valid over the init packet")

    elif case == "payload-corrupt-code":
        # Same defect as payload-corrupt, but the byte lands in executable code rather than in an
        # inert string. Supplemental, and deliberately secondary: it shows the digest covers ALL
        # payload bytes and not merely the tail, but its "never ran" assertion is WEAKER, because
        # a wrongly-accepted image with a corrupted instruction may hardfault instead of booting,
        # and a board that does not come back cannot distinguish "refused" from "ran and died".
        # Read the module docstring before treating this as the primary evidence.
        index = offset if offset is not None else len(image) // 4
        if not 0 <= index < len(image):
            raise MutationError(f"offset {index} is outside the {len(image)}-byte image")
        original_byte = image[index]
        image = image[:index] + bytes([original_byte ^ 0xFF]) + image[index + 1:]
        notes.append(f"image[{index}] {original_byte:#04x} -> {original_byte ^ 0xFF:#04x}, "
                     f"inside the code region (25% into the image)")
        notes.append(f"firmware_hash left at {init['firmware_hash']}; signature untouched")
        notes.append("weaker 'never ran' assertion than payload-corrupt -- see the case comment")

    elif case == "sig-corrupt":
        # The image, the hash and every signed field are genuine; only the 64-byte signature is
        # damaged. Cryptographically this is redundant with payload-rehashed, but it exercises
        # signature rejection WITHOUT altering any field the bootloader parses -- so a device that
        # rejected payload-rehashed by tripping over the changed hash rather than by verifying the
        # signature would be caught here, and nowhere else.
        signature = bytearray(dat[-SIGNATURE_LENGTH:])
        signature[0] ^= 0xFF
        dat = dat[:-SIGNATURE_LENGTH] + bytes(signature)
        init["init_packet_ecds"] = bytes(signature).hex()
        notes.append(f"signature byte 0 flipped; the other 63 bytes and the whole 52-byte init "
                     f"packet are untouched")
        notes.append("image and firmware_hash are genuine and still agree with each other")

    elif case in ("image-truncated", "image-extended"):
        # Framing rather than content: firmware_length is a SIGNED field, so an image whose real
        # size disagrees with it must be refused even though nothing about the signature or the
        # declared hash was edited. dfu_init_postvalidate() checks that length before it hashes,
        # so this is a distinct code path from the digest comparison the payload cases exercise.
        delta = 32
        if case == "image-truncated":
            image = image[:-delta]
            notes.append(f"image truncated by {delta} bytes to {len(image)}")
        else:
            image = image + bytes(delta)
            notes.append(f"image extended by {delta} zero bytes to {len(image)}")
        notes.append(f"the signed firmware_length still says {init['firmware_length']}, and the "
                     f"signed hash still describes the original image")
        notes.append("signature untouched and still valid over an init packet describing an "
                     "image of a different size")

    elif case == "init-tampered":
        old_version = init["application_version"]
        new_version = old_version + 1
        dat = (dat[:INIT_APP_VERSION_OFFSET]
               + new_version.to_bytes(4, "little")
               + dat[INIT_APP_VERSION_OFFSET + 4:])
        init["application_version"] = new_version
        notes.append(f"application_version {old_version} -> {new_version} in the signed init "
                     f"packet and manifest.json")
        notes.append("image and firmware_hash are both genuine and still agree with each other")
        notes.append("signature deliberately NOT regenerated: it signs the OLD init packet")

    else:
        raise MutationError(f"unknown case {case!r}")

    _write_package(destination, source, names, manifest, entry, image, dat)

    # Restate the resulting package's own consistency, because that is the thing under test and a
    # reader should not have to take the case name's word for it.
    consistent = (hashlib.sha256(image).hexdigest() == init["firmware_hash"]
                  and len(image) == init["firmware_length"])
    return {
        "case": case,
        "source": str(source),
        "package": str(destination),
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
        "image_matches_declared_hash_and_length": consistent,
        # True only where the 52-byte init packet AND its signature were both left alone.
        "signature_covers_this_init_packet": case in ("payload-corrupt", "payload-corrupt-code",
                                                      "image-truncated", "image-extended"),
        "notes": notes,
    }


CASE_ASSERTIONS = {
    "payload-corrupt": "the bootloader must hash the image it actually stored, not trust a "
                       "correctly signed header",
    "payload-corrupt-code": "the digest must cover executable bytes too, not only the tail of "
                            "the image (supplemental to payload-corrupt; weaker 'never ran' "
                            "assertion, see the case comment)",
    "sig-corrupt": "the bootloader must verify the signature itself, not merely notice that a "
                   "signed field was edited",
    "image-truncated": "the bootloader must enforce the SIGNED firmware_length against what it "
                       "actually stored",
    "image-extended": "the bootloader must reject an image longer than its signed "
                      "firmware_length, not just a shorter one",
    "payload-rehashed": "the bootloader must verify the P-256 signature, not merely check that "
                        "the image matches its own declared hash",
    "init-tampered": "the bootloader must reject a signed field that was edited after signing, "
                     "even when the image itself is genuine",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="A VALID signed application package to mutate")
    parser.add_argument("--case", choices=sorted(CASE_ASSERTIONS), required=True)
    parser.add_argument("--out", type=Path, required=True, help="Where to write the bad package")
    parser.add_argument("--offset", type=int,
                        help="Corrupt this image offset instead of the build-id string. Read the "
                             "module docstring before using it.")
    parser.add_argument("--marker", default="MSV3-BUILD:",
                        help="ASCII marker locating the inert corruption site")
    parser.add_argument("--json", type=Path, help="Also write the mutation record here")
    args = parser.parse_args()

    if args.out.resolve() == args.package.resolve():
        raise SystemExit("refusing to overwrite the baseline package with a mutated one")

    try:
        record = mutate(args.package.resolve(), args.case, args.out.resolve(),
                        args.marker.encode(), args.offset)
    except (MutationError, KeyError, zipfile.BadZipFile) as error:
        print(f"error: {error}")
        return 2

    record["asserts"] = CASE_ASSERTIONS[args.case]
    print(f"case      {record['case']}")
    print(f"asserts   {record['asserts']}")
    print(f"baseline  {Path(record['source']).name}")
    print(f"written   {record['package']}")
    print(f"sha256    {record['sha256']}")
    for note in record["notes"]:
        print(f"  - {note}")
    print(f"image matches its declared hash and length: "
          f"{record['image_matches_declared_hash_and_length']}")
    print(f"signature covers this init packet: {record['signature_covers_this_init_packet']}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(record, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
