#!/usr/bin/env python3
"""Deterministic tests for the ble_ota_update preflight and the MSV3 fleet registry.

The property that matters is not "the checks exist" but "the checks fire before the radio is
touched". A jump takes a live sensor off the air for the length of a transfer, and on a coated
probe in soil there is no cable to undo it with -- so a refusal is only useful if it costs nothing.
`test_refusal_happens_before_any_radio_call` is therefore the load-bearing test here: it drives the
real `run()` with the BLE entry points replaced by tripwires and asserts none of them is reached.

The rest is a branch matrix over the ways a package and a target can disagree.

Run:  python scripts/test_ota_preflight.py
"""

from __future__ import annotations

import asyncio
import binascii
import hashlib
import json
import struct
import sys
import tempfile
import types
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import msv3_fleet  # noqa: E402
from msv3_fleet import (  # noqa: E402
    IDENTITY_CONTRACT_MIN_APP_VERSION,
    FleetError,
    decode_manufacturer_data,
    load_fleet,
    next_address,
    read_package,
    verify_package_integrity,
)

FAILURES = []
TMP = Path(tempfile.mkdtemp(prefix="msv3-preflight-"))


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

def make_image(sensor_ids, size=4096):
    """A fake application image carrying the NUL-terminated names the firmware would store."""
    body = bytearray(b"\x00" * size)
    offset = 64
    for sensor_id in sensor_ids:
        blob = f"MSV3-MOISTURE-{sensor_id:03d}".encode() + b"\x00"
        body[offset:offset + len(blob)] = blob
        offset += 256
    return bytes(body)


def make_package(path, image, device_type=0x3356, application_version=65538,
                 signed=False, signing_key=None, extra_sections=None, corrupt_signature=False):
    init = {"device_type": device_type, "application_version": application_version}
    if signed:
        init["firmware_length"] = len(image)
        init["firmware_hash"] = hashlib.sha256(image).hexdigest()
        # The Adafruit legacy init packet the bootloader signs over, followed by the signature.
        body = struct.pack("<HHI", device_type, 52840, application_version) + bytes.fromhex(
            init["firmware_hash"])
        from ecdsa.keys import sigencode_string
        signature = signing_key.sign(body, hashfunc=hashlib.sha256, sigencode=sigencode_string)
        if corrupt_signature:
            signature = bytes([signature[0] ^ 0xFF]) + signature[1:]
        init["init_packet_ecds"] = signature.hex()
        dat = body + signature
    else:
        init["firmware_crc16"] = binascii.crc_hqx(image, 0xFFFF)
        dat = b"\x00" * 14

    manifest = {"manifest": {"dfu_version": 0.8 if signed else 0.5,
                             "application": {"bin_file": "app.bin", "dat_file": "app.dat",
                                             "init_packet_data": init}}}
    for name in extra_sections or []:
        manifest["manifest"][name] = {"bin_file": f"{name}.bin", "dat_file": f"{name}.dat",
                                      "init_packet_data": dict(init)}

    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("manifest.json", json.dumps(manifest))
        archive.writestr("app.bin", image)
        archive.writestr("app.dat", dat)
        for name in extra_sections or []:
            archive.writestr(f"{name}.bin", image)
            archive.writestr(f"{name}.dat", dat)
    return Path(path)


def make_registry(path, entries):
    path.write_text(json.dumps({"probes": entries}), encoding="utf-8")
    return path


REAL_KEY_SHA256 = "0fc6341903a1391ec264d98e9ae7d3fcead2cebee9f6adeb944c7038d3976daa"


def probe_entry(sensor_id, address, bootloader="msv3-1.0.1", device_type="0x3356",
                ota_capable=True, deployed=True, key_sha256=REAL_KEY_SHA256, legacy=()):
    return {"sensor_id": sensor_id, "name": f"MSV3-MOISTURE-{sensor_id:03d}",
            "application_address": address, "recovery_address": next_address(address),
            "bootloader": bootloader, "device_type": device_type, "ota_capable": ota_capable,
            "deployed": deployed, "bootloader_key_sha256": key_sha256,
            "legacy_package_sha256": list(legacy), "notes": ""}


class Args(types.SimpleNamespace):
    """Just enough of the argparse namespace for the preflight."""

    def __init__(self, **overrides):
        defaults = dict(package=None, probe=None, app_address=None, bootloader_address=None,
                        fleet=None, allow_identity_mismatch=False, allow_legacy_package=False,
                        preflight_only=False, skip_jump=False, reflash_same_build=False, prn=16,
                        stale_session_retries=3,
                        delay=0.6, retry=3, app_timeout=1.0, bootloader_timeout=1.0,
                        connect_delay=0.0, no_high_mtu=False, profile=False,
                        address_type="random")
        defaults.update(overrides)
        super().__init__(**defaults)


class FakeAdvertisement:
    def __init__(self, local_name=None, manufacturer_data=None, rssi=-60):
        self.local_name = local_name
        self.manufacturer_data = manufacturer_data or {}
        self.rssi = rssi
        self.service_uuids = []


def msv3_payload(sensor_id, wake=100, raw=19_000_000, battery_mv=3950, flags=0x1e,
                 temp_q4=88, bare=22_000_000):
    return (b"MSV2" + bytes([sensor_id]) + wake.to_bytes(3, "little")
            + struct.pack("<IHB", raw, battery_mv, flags)
            + struct.pack("<h", temp_q4) + struct.pack("<I", bare))


# -------------------------------------------------------------------- tests ----

def test_registry():
    print("\nregistry")
    good = make_registry(TMP / "good.json", [probe_entry(4, "F2:16:32:1B:2B:47"),
                                             probe_entry(5, "F9:18:9B:CA:10:B2")])
    fleet = load_fleet(good)
    check("loads both probes", len(fleet) == 2)
    check("resolves by sensor id", fleet.by_sensor_id(4).application_address == "F2:16:32:1B:2B:47")
    check("resolves by application address",
          fleet.by_address("f2:16:32:1b:2b:47").sensor_id == 4)
    check("resolves by recovery address (the no-valid-app +1 case)",
          fleet.by_address("F2:16:32:1B:2B:48").sensor_id == 4)
    check("unknown address is not resolved", fleet.by_address("AA:BB:CC:DD:EE:FF") is None)
    raises("unknown sensor id refuses", FleetError, lambda: fleet.by_sensor_id(7), "not in")

    dup_id = make_registry(TMP / "dupid.json", [probe_entry(4, "F2:16:32:1B:2B:47"),
                                                probe_entry(4, "AA:BB:CC:DD:EE:01")])
    raises("duplicate sensor id refuses", FleetError, lambda: load_fleet(dup_id), "twice")

    # An overlapping address range is the subtle one: probe A's recovery address colliding with
    # probe B's application address would silently make one probe answer for the other.
    overlap = make_registry(TMP / "overlap.json", [probe_entry(4, "F2:16:32:1B:2B:47"),
                                                   probe_entry(6, "F2:16:32:1B:2B:48")])
    raises("overlapping recovery/application address refuses", FleetError,
           lambda: load_fleet(overlap), "claimed by both")

    check("real registry loads", len(load_fleet()) >= 2)


def test_advertisement_decode():
    print("\nadvertisement decoding")
    # The genuine bytes captured from probe 4 on 2026-09-04.
    probe4 = decode_manufacturer_data(
        bytes.fromhex("4d53563204" "3d0200" "6b172701" "730f" "1e" "5900" "64155701"))
    check("sensor id", probe4.sensor_id == 4)
    check("wake counter", probe4.wake == 573)
    check("battery", probe4.battery_mv == 3955)
    check("flags decode", (probe4.charging, probe4.phase, probe4.temp_valid,
                           probe4.bare_valid, probe4.ota_gate_open) == (False, 1, True, True, True))
    check("die temperature", probe4.die_temp_c == 22.25)

    # Probe 5 on 2026-09-04: bit4 clear at 4419 mV, far above the gate -- pre-OTA firmware.
    probe5 = decode_manufacturer_data(
        bytes.fromhex("4d53563205" "b30300" "6d7a2b01" "4311" "0d" "5b00" "93805401"))
    check("pre-OTA firmware reads as gate closed", probe5.ota_gate_open is False)
    check("...while the battery is well above the gate", probe5.battery_mv == 4419)

    narrow = decode_manufacturer_data(bytes.fromhex("4d53563203" "523300" "4b09681000"))
    check("legacy narrow payload decodes only the common prefix",
          narrow.wide is False and narrow.sensor_id == 3 and narrow.battery_mv is None)
    check("foreign manufacturer data is not an MSV probe",
          decode_manufacturer_data(b"\x01\x02\x03\x04\x05\x06\x07\x08") is None)
    check("truncated payload is rejected", decode_manufacturer_data(b"MSV2") is None)


def test_package_identity():
    print("\npackage identity")
    one = make_package(TMP / "one.zip", make_image([4]))
    check("single identity is read out of the image", read_package(one).sensor_id == 4)
    check("...and the name is derived from it", read_package(one).name == "MSV3-MOISTURE-004")

    none = make_package(TMP / "none.zip", b"\x00" * 4096)
    raises("no identity refuses", FleetError, lambda: read_package(none), "no MSV3-MOISTURE")

    many = make_package(TMP / "many.zip", make_image([4, 5]))
    raises("several identities refuse", FleetError, lambda: read_package(many), "several")

    # A name without its NUL is a fragment of some longer string, not a stored identity.
    partial = make_package(TMP / "partial.zip", b"\x00" * 64 + b"MSV3-MOISTURE-0041234")
    raises("unterminated identity does not count", FleetError,
           lambda: read_package(partial), "no MSV3-MOISTURE")

    multi = make_package(TMP / "multi.zip", make_image([4]), extra_sections=["bootloader"])
    raises("a package carrying a bootloader is refused outright", FleetError,
           lambda: read_package(multi), "never be delivered over BLE")


def test_package_integrity():
    print("\npackage integrity")
    ecdsa = msv3_fleet.require_ecdsa()
    key = ecdsa.SigningKey.generate(curve=ecdsa.NIST256p)
    public = key.get_verifying_key().to_string()
    other = ecdsa.SigningKey.generate(curve=ecdsa.NIST256p).get_verifying_key().to_string()

    signed = make_package(TMP / "signed.zip", make_image([4]), signed=True, signing_key=key)
    verify_package_integrity(signed, 0x3356, public)
    check("a correctly signed package verifies", True)

    raises("a package signed by another key is refused", FleetError,
           lambda: verify_package_integrity(signed, 0x3356, other), "does not verify")

    bad = make_package(TMP / "badsig.zip", make_image([4]), signed=True, signing_key=key,
                       corrupt_signature=True)
    raises("a mutated signature is refused", FleetError,
           lambda: verify_package_integrity(bad, 0x3356, public), "does not verify")

    raises("no verification key for a signed package is refused", FleetError,
           lambda: verify_package_integrity(signed, 0x3356, None), "no verification key")

    raises("wrong device type is refused", FleetError,
           lambda: verify_package_integrity(signed, 0x0052, public), "device type")

    unset = make_package(TMP / "unset.zip", make_image([4]), application_version=0xFFFFFFFF)
    raises("the raw Arduino wrapper's unset application version is refused", FleetError,
           lambda: verify_package_integrity(unset, 0x3356, None), "unset value")

    # A mutated image must fail the hash even though its signature is untouched -- this is the
    # negative test the flasher docstring used to list as outstanding.
    tampered = TMP / "tampered.zip"
    with zipfile.ZipFile(signed) as src, zipfile.ZipFile(tampered, "w") as dst:
        for item in src.infolist():
            data = src.read(item.filename)
            if item.filename == "app.bin":
                data = bytearray(data)
                data[100] ^= 0xFF
                data = bytes(data)
            dst.writestr(item, data)
    raises("a mutated image is refused by the hash", FleetError,
           lambda: verify_package_integrity(tampered, 0x3356, public), "sha-256")

    unsigned = make_package(TMP / "unsigned.zip", make_image([4]))
    verify_package_integrity(unsigned, 0x3356, None)
    check("an unsigned package still gets its CRC16 checked", True)

    crc_broken = TMP / "crcbad.zip"
    with zipfile.ZipFile(unsigned) as src, zipfile.ZipFile(crc_broken, "w") as dst:
        for item in src.infolist():
            data = src.read(item.filename)
            if item.filename == "app.bin":
                data = bytes(bytearray(data)[:-1]) + b"\xAA"
            dst.writestr(item, data)
    raises("a broken CRC16 is refused", FleetError,
           lambda: verify_package_integrity(crc_broken, 0x3356, None), "crc16")


def test_target_resolution():
    print("\ntarget resolution")
    from ble_ota_update import PreflightError, resolve_target

    registry = make_registry(TMP / "fleet.json", [
        probe_entry(4, "F2:16:32:1B:2B:47"),
        probe_entry(5, "F9:18:9B:CA:10:B2", bootloader="adafruit-stock-0.11.0",
                    device_type="0x0052", ota_capable=False),
    ])

    _, probe = resolve_target(Args(probe=4, fleet=registry))
    check("--probe resolves the address", probe.application_address == "F2:16:32:1B:2B:47")
    check("...and both bootloader addresses",
          probe.bootloader_addresses == ["F2:16:32:1B:2B:47", "F2:16:32:1B:2B:48"])

    _, probe = resolve_target(Args(app_address="f9:18:9b:ca:10:b2", fleet=registry))
    check("--app-address resolves through the registry", probe.sensor_id == 5)

    raises("an unregistered address is refused", PreflightError,
           lambda: resolve_target(Args(app_address="AA:BB:CC:DD:EE:FF", fleet=registry)),
           "not in")
    raises("--probe and a contradicting --app-address is refused", PreflightError,
           lambda: resolve_target(Args(probe=4, app_address="F9:18:9B:CA:10:B2", fleet=registry)),
           "fix whichever is wrong")
    raises("neither target given is refused", PreflightError,
           lambda: resolve_target(Args(fleet=registry)), "--probe")
    raises("a malformed address is refused", PreflightError,
           lambda: resolve_target(Args(app_address="not-an-address", fleet=registry)))


def test_preflight_identity():
    print("\npreflight: package against target")
    from ble_ota_update import PreflightError, preflight, resolve_target

    registry = make_registry(TMP / "fleet.json", [
        probe_entry(4, "F2:16:32:1B:2B:47"),
        probe_entry(5, "F9:18:9B:CA:10:B2", bootloader="adafruit-stock-0.11.0",
                    device_type="0x0052", ota_capable=False),
    ])
    for_four = make_package(TMP / "for4.zip", make_image([4]))
    for_five = make_package(TMP / "for5.zip", make_image([5]), device_type=0x0052)

    _, probe = resolve_target(Args(probe=4, fleet=registry))
    package = preflight(Args(package=for_four, probe=4, fleet=registry), probe)
    check("a matching package passes", package.sensor_id == 4)

    # Probe 5 is on the stock bootloader, so the target-suitability check refuses this before the
    # package is examined at all. Several refusals would be correct here; assert on the one that
    # actually fires, so the test documents the real order rather than an imagined one.
    raises("the real 2026-09-04 hazard: a stale probe-4 package aimed at probe 5 is refused",
           PreflightError,
           lambda: preflight(Args(package=for_four, probe=5, fleet=registry),
                             load_fleet(registry).by_sensor_id(5)),
           "ota_capable=false")

    # Make probe 5 OTA-capable so the identity check, not the bootloader check, is what refuses.
    registry2 = make_registry(TMP / "fleet2.json", [probe_entry(4, "F2:16:32:1B:2B:47"),
                                                    probe_entry(5, "F9:18:9B:CA:10:B2")])
    five = load_fleet(registry2).by_sensor_id(5)
    raises("a probe-4 package aimed at an OTA-capable probe 5 is refused on identity",
           PreflightError,
           lambda: preflight(Args(package=for_four, probe=5, fleet=registry2), five),
           "identity mismatch")

    # The override is bench-only; probe 5 is deployed, so it must NOT open here. See
    # test_identity_override_is_bench_only for the full scoping.
    raises("--allow-identity-mismatch does not open it for a deployed probe", PreflightError,
           lambda: preflight(Args(package=for_four, probe=5, fleet=registry2,
                                  allow_identity_mismatch=True), five),
           "deployed=true")

    registry3 = make_registry(TMP / "fleet3.json",
                              [probe_entry(5, "F9:18:9B:CA:10:B2", deployed=False)])
    bench_five = load_fleet(registry3).by_sensor_id(5)
    package = preflight(Args(package=for_four, probe=5, fleet=registry3,
                             allow_identity_mismatch=True), bench_five)
    check("--allow-identity-mismatch permits it on a non-deployed board", package.sensor_id == 4)

    # ...but the override must not reach anything else. for_five is built for device type
    # 0x0052 while probe 5 is registered here on the 0x3356 bootloader.
    raises("--allow-identity-mismatch does NOT relax the device type", PreflightError,
           lambda: preflight(Args(package=for_five, probe=5, fleet=registry3,
                                  allow_identity_mismatch=True), bench_five),
           "device type")


def test_live_identity():
    print("\npreflight: the board on air")
    from ble_ota_update import PreflightError, check_live_identity

    probe = load_fleet(make_registry(TMP / "fleet.json",
                                     [probe_entry(4, "F2:16:32:1B:2B:47")])).by_sensor_id(4)

    healthy = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(4)})
    check_live_identity(Args(), probe, healthy)
    check("a matching live board passes", True)

    wrong_sensor = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(5)})
    raises("a board advertising another sensor ID is refused", PreflightError,
           lambda: check_live_identity(Args(), probe, wrong_sensor), "advertises sensor 5")

    wrong_name = FakeAdvertisement("MSV3-MOISTURE-007", {0xFFFF: msv3_payload(4)})
    raises("a board advertising another name is refused", PreflightError,
           lambda: check_live_identity(Args(), probe, wrong_name), "not 'MSV3-MOISTURE-004'")

    shut = FakeAdvertisement("MSV3-MOISTURE-004",
                             {0xFFFF: msv3_payload(4, battery_mv=3700, flags=0x0e)})
    raises("a shut power gate is refused", PreflightError,
           lambda: check_live_identity(Args(), probe, shut), "power gate is shut")

    pre_ota = FakeAdvertisement("MSV3-MOISTURE-004",
                                {0xFFFF: msv3_payload(4, battery_mv=4400, flags=0x0e)})
    raises("pre-OTA firmware is refused and named as such", PreflightError,
           lambda: check_live_identity(Args(), probe, pre_ota), "pre-OTA firmware")

    silent = FakeAdvertisement("MSV3-MOISTURE-004", {})
    raises("a board with no MSV payload is refused", PreflightError,
           lambda: check_live_identity(Args(), probe, silent), "no MSV manufacturer payload")

    # The override is scoped to non-deployed boards, so exercise it on one. Its refusal against a
    # deployed probe is covered by test_identity_override_is_bench_only.
    bench = load_fleet(make_registry(
        TMP / "bench.json",
        [probe_entry(4, "F2:16:32:1B:2B:47", deployed=False)])).by_sensor_id(4)
    override = Args(allow_identity_mismatch=True)
    check_live_identity(override, bench, wrong_sensor)
    check("--allow-identity-mismatch permits a sensor-ID disagreement on a bench board", True)
    raises("--allow-identity-mismatch does NOT permit a shut power gate", PreflightError,
           lambda: check_live_identity(override, bench, shut), "power gate is shut")
    raises("--allow-identity-mismatch does NOT permit pre-OTA firmware", PreflightError,
           lambda: check_live_identity(override, bench, pre_ota), "pre-OTA firmware")


def test_refusal_happens_before_any_radio_call():
    """The load-bearing test: a refused run must not scan, connect, jump, or transfer."""
    print("\nordering: nothing touches the radio before the checks pass")
    import ble_ota_update
    from ble_ota_update import PreflightError

    touched = []

    async def tripwire_scan(*args, **kwargs):
        touched.append("wait_for_advertisement")
        return None, None

    async def tripwire_jump(*args, **kwargs):
        touched.append("jump_to_bootloader")
        return "never"

    def tripwire_workarounds(*args, **kwargs):
        touched.append("install_windows_ble_workarounds")
        return None

    originals = (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
                 ble_ota_update.install_windows_ble_workarounds)
    ble_ota_update.wait_for_advertisement = tripwire_scan
    ble_ota_update.jump_to_bootloader = tripwire_jump
    ble_ota_update.install_windows_ble_workarounds = tripwire_workarounds

    registry = make_registry(TMP / "fleet.json", [
        probe_entry(4, "F2:16:32:1B:2B:47"),
        probe_entry(5, "F9:18:9B:CA:10:B2", bootloader="adafruit-stock-0.11.0",
                    device_type="0x0052", ota_capable=False),
    ])
    for_four = make_package(TMP / "for4.zip", make_image([4]))
    no_identity = make_package(TMP / "noid.zip", b"\x00" * 4096)
    wrong_type = make_package(TMP / "wrongtype.zip", make_image([4]), device_type=0x0052)

    cases = [
        ("wrong probe", Args(package=for_four, probe=5, fleet=registry)),
        ("unregistered address",
         Args(package=for_four, app_address="AA:BB:CC:DD:EE:FF", fleet=registry)),
        ("unknown probe", Args(package=for_four, probe=42, fleet=registry)),
        ("package with no identity", Args(package=no_identity, probe=4, fleet=registry)),
        ("wrong device type", Args(package=wrong_type, probe=4, fleet=registry)),
    ]
    try:
        for label, args in cases:
            touched.clear()
            try:
                asyncio.run(ble_ota_update.run(args))
                check(f"{label} is refused", False, "run() returned instead of raising")
            except (PreflightError, FleetError):
                check(f"{label} is refused with the radio untouched", not touched,
                      f"reached {touched}")
            except Exception as error:  # noqa: BLE001
                check(f"{label} is refused", False,
                      f"raised {type(error).__name__}: {error}")
    finally:
        (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
         ble_ota_update.install_windows_ble_workarounds) = originals


def test_bootloader_vs_application_advertisement():
    """An orphaned bootloader must not be confused with a running application at the same address."""
    print("\nbootloader / application disambiguation")
    from ble_ota_update import DFU_SERVICE_UUID, looks_like_bootloader

    class Device:
        def __init__(self, name=None):
            self.name = name
            self.address = "F2:16:32:1B:2B:47"

    application = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(4)})
    check("a running application is NOT taken for the bootloader",
          not looks_like_bootloader(Device("MSV3-MOISTURE-004"), application))

    # The production sketch registers the DFU service in GATT but keeps it out of the advertising
    # payload -- the 31-byte legacy PDU is already full -- so this is the real on-air shape.
    with_gatt_dfu = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(4)})
    check("...even though it does have a DFU service in its GATT table",
          not looks_like_bootloader(Device(), with_gatt_dfu))

    by_uuid = FakeAdvertisement(None, {})
    by_uuid.service_uuids = [DFU_SERVICE_UUID]
    check("the bootloader is recognised by its DFU service UUID",
          looks_like_bootloader(Device(), by_uuid))

    by_name = FakeAdvertisement("AdaDFU", {})
    check("...and by the AdaDFU name", looks_like_bootloader(Device("AdaDFU"), by_name))
    check("a probe name containing no 'dfu' does not match",
          "dfu" not in "MSV3-MOISTURE-004".lower())


def test_legacy_package_gate():
    """A package predating the derived-name invariant cannot have its identity trusted."""
    print("\nlegacy packages (name not provably equal to SENSOR_ID)")
    from ble_ota_update import PreflightError, preflight

    registry = make_registry(TMP / "fleet.json", [probe_entry(4, "F2:16:32:1B:2B:47")])
    four = load_fleet(registry).by_sensor_id(4)

    legacy = make_package(TMP / "legacy.zip", make_image([4]),
                          application_version=IDENTITY_CONTRACT_MIN_APP_VERSION - 1)
    raises("a pre-contract package is refused even when its name matches", PreflightError,
           lambda: preflight(Args(package=legacy, probe=4, fleet=registry), four),
           "predates the compile-time guarantee")

    # On a DEPLOYED probe the flag alone must not be enough. An old image whose name says 004
    # while its compiled SENSOR_ID is 5 would sail through the name check, and the live check
    # describes the application currently running, not the one about to be sent.
    raises("--allow-legacy-package alone is refused for a deployed probe", PreflightError,
           lambda: preflight(Args(package=legacy, probe=4, fleet=registry,
                                  allow_legacy_package=True), four),
           "deployed=true")

    bench_registry = make_registry(TMP / "benchlegacy.json",
                                   [probe_entry(4, "F2:16:32:1B:2B:47", deployed=False)])
    bench = load_fleet(bench_registry).by_sensor_id(4)
    package = preflight(Args(package=legacy, probe=4, fleet=bench_registry,
                             allow_legacy_package=True), bench)
    check("--allow-legacy-package accepts it on a non-deployed board", package.sensor_id == 4)

    # The escape for a deployed probe is an allowlisted image hash -- a reviewable registry edit,
    # not a keystroke.
    digest = read_package(legacy).sha256
    allow_registry = make_registry(TMP / "allowlisted.json",
                                   [probe_entry(4, "F2:16:32:1B:2B:47", legacy=(digest,))])
    allowed = load_fleet(allow_registry).by_sensor_id(4)
    package = preflight(Args(package=legacy, probe=4, fleet=allow_registry), allowed)
    check("an allowlisted legacy image passes on a deployed probe, with no flag",
          package.sensor_id == 4)

    # A genuinely different image -- the allowlist keys on the application IMAGE hash, not the ZIP,
    # because the image is what gets written to flash and ZIP metadata is not stable. Two packages
    # wrapping byte-identical firmware are the same firmware and both should pass.
    other = make_package(TMP / "legacy2.zip", make_image([4], size=8192),
                         application_version=IDENTITY_CONTRACT_MIN_APP_VERSION - 2)
    check("the fixture really is a different image",
          read_package(other).sha256 != digest)
    raises("a DIFFERENT legacy image is still refused under that allowlist", PreflightError,
           lambda: preflight(Args(package=other, probe=4, fleet=allow_registry,
                                  allow_legacy_package=True), allowed),
           "deployed=true")

    current = make_package(TMP / "current.zip", make_image([4]),
                           application_version=IDENTITY_CONTRACT_MIN_APP_VERSION)
    package = preflight(Args(package=current, probe=4, fleet=registry), four)
    check("a package at the threshold needs no override", package.sensor_id == 4)

    # The real 1.0.1 signed release predates the invariant, so it must be refused by default.
    release = (Path(__file__).resolve().parents[1] / "MSV3" / "bootloader" / "generated" /
               "1.0.1" / "msv3-1.0.1-signed-ota.zip")
    if release.exists():
        five = load_fleet(make_registry(TMP / "f5.json",
                                        [probe_entry(5, "F9:18:9B:CA:10:B2")])).by_sensor_id(5)
        raises("the shipped 1.0.1 signed release is treated as legacy", PreflightError,
               lambda: preflight(Args(package=release, probe=5, fleet=TMP / "f5.json"), five),
               "predates the compile-time guarantee")


def test_identity_override_is_bench_only():
    """--allow-identity-mismatch must not be usable against a deployed probe."""
    print("\nidentity override is scoped to non-deployed boards")
    from ble_ota_update import PreflightError, check_live_identity, preflight

    registry = make_registry(TMP / "fleet.json", [
        probe_entry(4, "F2:16:32:1B:2B:47", deployed=True),
        probe_entry(99, "DB:AD:4D:9D:21:BA", deployed=False),
    ])
    fleet = load_fleet(registry)
    for_four = make_package(TMP / "for4.zip", make_image([4]))

    # A probe-4 image aimed at deployed probe 4 matches, so use a foreign image to force the
    # mismatch path against a deployed target.
    for_seven = make_package(TMP / "for7.zip", make_image([7]))
    raises("the override is refused for a deployed probe", PreflightError,
           lambda: preflight(Args(package=for_seven, probe=4, fleet=registry,
                                  allow_identity_mismatch=True), fleet.by_sensor_id(4)),
           "deployed=true")

    bench = fleet.by_sensor_id(99)
    package = preflight(Args(package=for_four, probe=99, fleet=registry,
                             allow_identity_mismatch=True), bench)
    check("the override still works for the bench board", package.sensor_id == 4)

    wrong = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(4)})
    check_live_identity(Args(allow_identity_mismatch=True), bench, wrong)
    check("the live override works for the bench board", True)

    deployed = fleet.by_sensor_id(4)
    live_wrong = FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(9)})
    raises("the live override is refused for a deployed probe", PreflightError,
           lambda: check_live_identity(Args(allow_identity_mismatch=True), deployed, live_wrong),
           "does not apply")


def test_bootloader_key_is_pinned():
    """The registry, not just the release manifest, decides which key the client will trust."""
    print("\nbootloader public key pinned by the registry")
    from ble_ota_update import PreflightError, preflight
    from msv3_fleet import load_release_public_key

    key = load_release_public_key("1.0.1", REAL_KEY_SHA256)
    check("the pinned fingerprint matches the shipped 1.0.1 manifest", len(key) == 64)

    raises("a manifest disagreeing with the registry is refused", FleetError,
           lambda: load_release_public_key("1.0.1", "00" * 32), "registry pins")
    raises("an unpinned probe is refused rather than trusted blindly", FleetError,
           lambda: load_release_public_key("1.0.1"), "records no bootloader_key_sha256")

    ecdsa = msv3_fleet.require_ecdsa()
    signer = ecdsa.SigningKey.generate(curve=ecdsa.NIST256p)
    signed = make_package(TMP / "signed4.zip", make_image([4]), signed=True, signing_key=signer)
    registry = make_registry(TMP / "fleet.json",
                             [probe_entry(4, "F2:16:32:1B:2B:47", key_sha256="")])
    four = load_fleet(registry).by_sensor_id(4)
    raises("a signed package against an unpinned probe is refused", PreflightError,
           lambda: preflight(Args(package=signed, probe=4, fleet=registry), four),
           "bootloader_key_sha256")


def test_live_failures_trip_before_the_jump():
    """Scanning may happen; jumping must not, when the board on air disagrees."""
    print("\nordering: a live mismatch reaches the scan but never the jump")
    import ble_ota_update
    from ble_ota_update import PreflightError

    registry = make_registry(TMP / "fleet.json", [probe_entry(4, "F2:16:32:1B:2B:47")])
    for_four = make_package(TMP / "for4.zip", make_image([4]))

    class Device:
        address = "F2:16:32:1B:2B:47"
        name = "MSV3-MOISTURE-004"

    scenarios = {
        "wrong sensor ID on air":
            FakeAdvertisement("MSV3-MOISTURE-004", {0xFFFF: msv3_payload(9)}),
        "shut OTA power gate":
            FakeAdvertisement("MSV3-MOISTURE-004",
                              {0xFFFF: msv3_payload(4, battery_mv=3700, flags=0x0e)}),
        "pre-OTA firmware":
            FakeAdvertisement("MSV3-MOISTURE-004",
                              {0xFFFF: msv3_payload(4, battery_mv=4400, flags=0x0e)}),
        "wrong name on air":
            FakeAdvertisement("MSV3-MOISTURE-007", {0xFFFF: msv3_payload(4)}),
    }

    originals = (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
                 ble_ota_update.install_windows_ble_workarounds)
    try:
        for label, advertisement in scenarios.items():
            reached = []

            async def scan(addresses, timeout, kind, require_dfu_service=False,
                           _adv=advertisement, _reached=reached):
                _reached.append(f"scan:{kind}")
                # No bootloader; the application is present and answers with _adv.
                if require_dfu_service:
                    return None, None
                return Device(), _adv

            async def jump(*a, _reached=reached, **k):
                _reached.append("jump")
                return "should never happen"

            ble_ota_update.wait_for_advertisement = scan
            ble_ota_update.jump_to_bootloader = jump
            ble_ota_update.install_windows_ble_workarounds = lambda *a, **k: None

            try:
                asyncio.run(ble_ota_update.run(Args(package=for_four, probe=4, fleet=registry)))
                check(f"{label} is refused", False, "run() returned instead of raising")
            except PreflightError:
                check(f"{label}: scanned but never jumped",
                      "jump" not in reached and any(r.startswith("scan") for r in reached),
                      f"reached {reached}")
            except Exception as error:  # noqa: BLE001
                check(f"{label} is refused", False,
                      f"raised {type(error).__name__}: {error}")
    finally:
        (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
         ble_ota_update.install_windows_ble_workarounds) = originals


def test_stale_session_recovery():
    """The branch added after the 2026-09-04 interrupted-transfer campaign.

    Real behaviour it encodes, measured twice on the bench: kill a transfer partway and the
    bootloader keeps the dead session's state, answering the next Start DFU with INVALID_STATE.
    dfu_lib resets the board and raises; the board reboots, often onto its OTHER address, and a
    retry then works. Before this branch the client spent its retries during the reboot and exited
    with a raw WinRT traceback -- in the field, indistinguishable from a brick.
    """
    print("\nstale bootloader session recovery")
    import ble_ota_update
    from ble_ota_update import (
        DFU_STATUS_INVALID_STATE,
        OP_CODE_START_DFU,
        OP_CODE_VALIDATE,
        ControlStatus,
        PreflightError,
    )

    status = ControlStatus()
    status.record(OP_CODE_START_DFU, DFU_STATUS_INVALID_STATE)
    check("Start DFU + INVALID_STATE arms the recovery branch", status.start_dfu_invalid_state)

    for op, code, label in [
        (OP_CODE_VALIDATE, DFU_STATUS_INVALID_STATE, "INVALID_STATE from Validate"),
        (ble_ota_update.OP_CODE_RECEIVE_FIRMWARE_IMAGE, DFU_STATUS_INVALID_STATE,
         "INVALID_STATE from Receive Firmware"),
        (OP_CODE_START_DFU, 6, "Start DFU with an operation-failed status"),
        (OP_CODE_START_DFU, 1, "a successful Start DFU"),
    ]:
        other = ControlStatus()
        other.record(op, code)
        check(f"{label} does NOT arm it", not other.start_dfu_invalid_state)

    registry = make_registry(TMP / "fleet.json", [probe_entry(99, "DB:AD:4D:9D:21:BA",
                                                              deployed=False)])
    good = make_package(TMP / "bench.zip", make_image([99]),
                        application_version=IDENTITY_CONTRACT_MIN_APP_VERSION)

    class Device:
        def __init__(self, address):
            self.address = address
            self.name = "AdaDFU"

    def drive(*, transfer_results, advertise_addresses, args_overrides=None):
        """Run the real run() with the transfer and the scanner replaced. Returns (code, events)."""
        events = []
        results = list(transfer_results)
        addresses = list(advertise_addresses)

        class FakeUpdater:
            upload_mode = ble_ota_update.UPLOAD_MODE_APPLICATION
            bin_data = b"x" * 1024

            def __init__(self, *a, **k):
                pass

            def parse_zip(self):
                pass

            async def perform_update(self, device, max_retries=3):
                events.append(f"transfer:{device.address}")
                outcome = results.pop(0)
                # BaseException: the cancellation fixture injects KeyboardInterrupt, which is not
                # an Exception. Testing for Exception here silently turned that case into a
                # successful transfer and made the assertion vacuous.
                if isinstance(outcome, BaseException):
                    raise outcome

        async def fake_scan(addrs, timeout, label, require_dfu_service=False):
            events.append(f"scan:{label}")
            if not addresses:
                return None, None
            address = addresses.pop(0)
            if address is None:
                return None, None
            return Device(address), None

        originals = (ble_ota_update.wait_for_advertisement,
                     ble_ota_update.install_windows_ble_workarounds,
                     ble_ota_update.install_transfer_hooks,
                     ble_ota_update.install_status_hook)
        ble_ota_update.wait_for_advertisement = fake_scan
        ble_ota_update.install_windows_ble_workarounds = lambda *a, **k: None
        ble_ota_update.install_transfer_hooks = lambda *a, **k: None

        captured = {}

        def fake_status_hook(updater, control):
            captured["control"] = control
            # Replay the device's answer to Start DFU so the branch sees what the bench saw.
            control.record(OP_CODE_START_DFU, DFU_STATUS_INVALID_STATE)

        ble_ota_update.install_status_hook = fake_status_hook

        import types as _types
        fake_dfu_lib = _types.ModuleType("dfu_lib")
        fake_dfu_lib.NordicLegacyDFU = FakeUpdater
        fake_dfu_lib.BleakClient = object
        fake_dfu_lib.DFU_CONTROL_POINT_UUID = "00001531-1212-efde-1523-785feabcd123"
        sys.modules["dfu_lib"] = fake_dfu_lib

        overrides = dict(package=good, probe=99, fleet=registry, skip_jump=True,
                         connect_delay=0.0, bootloader_timeout=0.1)
        overrides.update(args_overrides or {})
        try:
            code = asyncio.run(ble_ota_update.run(Args(**overrides)))
            return code, events, None
        except BaseException as error:  # noqa: BLE001
            # BaseException, not Exception: the cancellation case deliberately raises
            # KeyboardInterrupt, and catching only Exception would let it escape the harness.
            return None, events, error
        finally:
            (ble_ota_update.wait_for_advertisement,
             ble_ota_update.install_windows_ble_workarounds,
             ble_ota_update.install_transfer_hooks,
             ble_ota_update.install_status_hook) = originals
            sys.modules.pop("dfu_lib", None)

    # The bench sequence: found at BA, INVALID_STATE, board resets onto BB, retry succeeds.
    code, events, error = drive(
        transfer_results=[RuntimeError("The operation was canceled by the user."), None],
        advertise_addresses=["DB:AD:4D:9D:21:BA", "DB:AD:4D:9D:21:BB"])
    check("a stale session is cleared and the transfer restarts", code == 0, f"{error} {events}")
    check("...restarting against the address it actually came back on",
          events == ["scan:bootloader", "transfer:DB:AD:4D:9D:21:BA",
                     "scan:bootloader after its reset", "transfer:DB:AD:4D:9D:21:BB"],
          str(events))

    # Retries are bounded.
    code, events, error = drive(
        transfer_results=[RuntimeError("boom")] * 6,
        advertise_addresses=["DB:AD:4D:9D:21:BA"] * 6,
        args_overrides={"stale_session_retries": 3})
    check("retries are bounded, and the last failure propagates",
          code is None and error is not None,
          f"code={code} error={error}")
    check("...having tried exactly the configured number of times",
          len([e for e in events if e.startswith("transfer:")]) == 3, str(events))

    # Opting out.
    code, events, error = drive(
        transfer_results=[RuntimeError("boom"), None],
        advertise_addresses=["DB:AD:4D:9D:21:BA", "DB:AD:4D:9D:21:BB"],
        args_overrides={"stale_session_retries": 0})
    check("--stale-session-retries 0 does not retry at all",
          error is not None and len([e for e in events if e.startswith("transfer:")]) == 1,
          str(events))

    # The bootloader never comes back.
    code, events, error = drive(
        transfer_results=[RuntimeError("boom"), None],
        advertise_addresses=["DB:AD:4D:9D:21:BA", None])
    check("a bootloader that never returns exits non-zero without transferring again",
          code == 1 and len([e for e in events if e.startswith("transfer:")]) == 1,
          f"code={code} events={events} error={error}")

    # An unrelated AdaDFU must never be adopted -- 0x3356 identifies no individual probe.
    code, events, error = drive(
        transfer_results=[RuntimeError("boom"), None],
        advertise_addresses=["DB:AD:4D:9D:21:BA", "AA:BB:CC:DD:EE:FF"])
    check("a bootloader at an unpinned address is refused, not flashed",
          isinstance(error, PreflightError) and "not one of probe" in str(error),
          f"{type(error).__name__}: {error}")

    # Operator cancellation must never be retried through.
    code, events, error = drive(
        transfer_results=[KeyboardInterrupt(), None],
        advertise_addresses=["DB:AD:4D:9D:21:BA", "DB:AD:4D:9D:21:BB"])
    check("Ctrl-C is not retried through",
          isinstance(error, KeyboardInterrupt)
          and len([e for e in events if e.startswith("transfer:")]) == 1,
          f"{type(error).__name__} events={events}")


def test_happy_path_reaches_the_radio_boundary():
    """A valid package/target pair must actually get through -- rejections alone prove nothing.

    Without this, a preflight that refused everything would pass the whole rest of this file.
    """
    print("\nhappy path: a valid release passes every check")
    import ble_ota_update

    registry = make_registry(TMP / "fleet.json", [probe_entry(4, "F2:16:32:1B:2B:47")])
    ecdsa = msv3_fleet.require_ecdsa()

    # Sign with the real fleet key so the registry-pinned fingerprint is genuinely exercised.
    key_path = (Path(__file__).resolve().parents[1] / "MSV3" / ".secrets" /
                "msv3-dfu-signing-private.pem")
    if not key_path.exists():
        check("signing key present for the happy-path test", False, f"missing {key_path}")
        return
    signer = ecdsa.SigningKey.from_pem(key_path.read_bytes())
    good = make_package(TMP / "good4.zip", make_image([4]), signed=True, signing_key=signer,
                        application_version=IDENTITY_CONTRACT_MIN_APP_VERSION)

    touched = []

    async def tripwire(*a, _t=touched, **k):
        _t.append("radio")
        return None, None

    originals = (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
                 ble_ota_update.install_windows_ble_workarounds)
    ble_ota_update.wait_for_advertisement = tripwire
    ble_ota_update.jump_to_bootloader = tripwire
    ble_ota_update.install_windows_ble_workarounds = lambda *a, **k: touched.append("radio")
    try:
        code = asyncio.run(ble_ota_update.run(
            Args(package=good, probe=4, fleet=registry, preflight_only=True)))
        check("a valid signed package for the right probe passes every check", code == 0)
        check("...and --preflight-only stops before the radio", not touched, f"reached {touched}")
    except Exception as error:  # noqa: BLE001
        check("a valid signed package for the right probe passes every check", False,
              f"raised {type(error).__name__}: {error}")
    finally:
        (ble_ota_update.wait_for_advertisement, ble_ota_update.jump_to_bootloader,
         ble_ota_update.install_windows_ble_workarounds) = originals


def test_real_registry_matches_hardware():
    """Guard the facts this session actually measured, so a careless edit to fleet.json shows up."""
    print("\nregistry against 2026-09-04 observations")
    fleet = load_fleet()
    four = fleet.by_sensor_id(4)
    check("probe 4 address", four.application_address == "F2:16:32:1B:2B:47")
    check("probe 4 is on the MSV3 bootloader and OTA-capable",
          four.device_type == 0x3356 and four.ota_capable)
    five = fleet.by_sensor_id(5)
    check("probe 5 address", five.application_address == "F9:18:9B:CA:10:B2")
    check("probe 5 is recorded as still on the stock bootloader",
          five.device_type == 0x0052 and not five.ota_capable)
    check("every recovery address is application address + 1",
          all(p.recovery_address == next_address(p.application_address) for p in fleet))
    check("every OTA-capable probe pins its bootloader's public key",
          all(p.bootloader_key_sha256 == REAL_KEY_SHA256 for p in fleet if p.ota_capable))
    check("the bench board is the only non-deployed entry",
          [p.sensor_id for p in fleet if not p.deployed] == [99])
    # An allowlisted legacy image bypasses the identity guarantee for that probe, so an entry here
    # should be a deliberate, recorded decision -- never a leftover.
    check("no probe allowlists a legacy image without a recorded reason",
          all(not p.legacy_package_sha256 for p in fleet))


def main():
    print("MSV3 OTA preflight")
    test_registry()
    test_advertisement_decode()
    test_package_identity()
    test_package_integrity()
    test_target_resolution()
    test_preflight_identity()
    test_live_identity()
    test_bootloader_vs_application_advertisement()
    test_legacy_package_gate()
    test_identity_override_is_bench_only()
    test_bootloader_key_is_pinned()
    test_refusal_happens_before_any_radio_call()
    test_live_failures_trip_before_the_jump()
    test_happy_path_reaches_the_radio_boundary()
    test_stale_session_recovery()
    test_real_registry_matches_hardware()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): " + ", ".join(FAILURES))
        return 1
    print("all preflight tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
