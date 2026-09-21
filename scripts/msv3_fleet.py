#!/usr/bin/env python3
"""Shared MSV3 fleet facts: the probe registry, the advertisement format, and package identity.

Three things used to live only in people's heads, and each of them could send the wrong firmware
to the wrong probe:

1. **Which BLE address is which probe.** `ble_ota_update.py` takes a raw `--app-address`. Getting
   it wrong jumps a healthy sensor into its bootloader and takes it offline.
2. **Which probe a package was built for.** `SENSOR_ID` is a hand-edited constant in
   MSV3_Moisture_BLE_Sleep.ino. Two stale packages sitting in the build directories on 2026-09-04
   both contained `MSV3-MOISTURE-004` while the committed source said 005 -- an ordinary leftover
   from a reverted local edit, and nothing would have stopped either being flashed.
3. **Which device type the target bootloader accepts.** Probe 4 still runs MSV3 1.0.1; probes 5-8
   run MSV3 1.0.3. All accept MSV3's `0x3356`, while a raw/stock Adafruit bootloader accepts
   `0x0052` only until its one-time SWD provisioning.

`MSV3/fleet.json` records all three, and this module is the single reader for it.

The identity a package claims is read out of the *application image itself* -- the NUL-terminated
`MSV3-MOISTURE-NNN` string the firmware advertises -- not out of the ZIP manifest, which is
unsigned metadata anybody can edit. Since 2026-09-04 that string is derived from `SENSOR_ID` at
compile time (see `makeDeviceName()` in the sketch), so it cannot disagree with the ID the probe
puts on the air.
"""

from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parents[1]
MSV3_ROOT = REPO_ROOT / "MSV3"
FLEET_PATH = MSV3_ROOT / "fleet.json"
BOOTLOADER_RELEASES = MSV3_ROOT / "bootloader" / "generated"

# ecdsa ships in the gitignored DFU dependency set rather than the system environment. Add it
# rather than making the caller remember a PYTHONPATH -- but never silently skip verification if
# it is missing; see require_ecdsa().
DFU_PYDEPS = MSV3_ROOT / ".arduino-build" / "dfu-pydeps"

BLE_COMPANY_ID = 0xFFFF
BLE_MAGIC = b"MSV2"  # Real production magic for every generation -- see the sketch's comment on
                     # why MSV3 kept MSV2's, and MSV3_Probe_Bringup_Test's distinct "MSV3" magic.
MSV3_WIDE_LEN = 21   # 23-byte payload minus the 2-byte company ID the scanner strips.
# MSV3_Probe_Bringup_Test's targeted fleet diagnostic. A different magic on purpose, so a production
# receiver can never ingest its placeholder reading -- see that sketch's bleStartAdv().
DIAGNOSTIC_MAGIC = b"MSV3"
DIAGNOSTIC_LEN = 15  # 17-byte payload minus the company ID.

# The advertised name as it appears in the compiled image. Anchored on the trailing NUL so a
# partial match inside a longer string cannot pass.
IDENTITY_RE = re.compile(rb"MSV3-MOISTURE-(\d{3})\x00")

# The build identity, same NUL-anchored trick. Sensor ID says which BOARD; this says which IMAGE.
# The bootloader stores no application version (dfu_init.c never implements the version check), so
# after an interrupted update this literal is the only way to tell what actually came back.
BUILD_ID_RE = re.compile(rb"MSV3-BUILD:([0-9A-Za-z._-]{1,48})\x00")
# Guard 2's boot counter lives at 0x2003FFF8 and only survives a reset if the BOOTLOADER also
# excludes those bytes from its stack -- true only from MSV3 bootloader 1.0.3. The application
# stamps this marker so a host can check the pairing before a transfer; see the long comment
# beside MSV3_GUARD2_MARKER in MSV3_Moisture_BLE_Sleep.ino. THREE states, not two:
#   b"1"  -> guard enabled, requires bootloader >= 1.0.3
#   b"0"  -> guard disabled, cannot hit the incompatibility
#   None  -> unmarked; every package built before 2026-09-09, including the archived rescue
#            images for probes 5/6/7/8, so absence must never by itself mean refusal.
GUARD2_RE = re.compile(rb"MSV3-GUARD2:([01])\x00")

ADDRESS_RE = re.compile(r"^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$")

# Reading MSV3-MOISTURE-NNN out of an image only proves what the firmware ADVERTISES. Before
# 2026-09-04 the advertised name and SENSOR_ID were separate hand-edited constants, so an older
# signed package could legitimately carry the name 004 while putting sensor ID 5 on the wire --
# exactly the mismatch the identity check exists to catch, and invisible to it.
#
# Every package built from 2026-09-04 source derives the name from SENSOR_ID, so for those the
# name IS the ID. This threshold is what separates the two eras: below it, the identity check is
# not trustworthy and the package needs --allow-legacy-package. 65537 is release 1.0.1, the last
# build predating the invariant, so the first release that carries it is 65538.
#
# A marker string compiled into the image would be less easy to misread than a version number, but
# the Arduino core builds with -fdata-sections -Wl,--gc-sections, so an unreferenced constant is
# not reliably retained -- and one kept alive by a serial print would vanish with ENABLE_SERIAL_LOGS.
# The version is carried in the signed init packet, which cannot be dropped by a linker.
#
# WHAT THIS IS AND IS NOT. It is an authenticated *release-policy* boundary, not cryptographic
# proof that a binary was compiled from invariant-bearing source. It stops a historical package
# being reused by accident, which is the failure that actually happened. It does not stop a
# deliberate relabel by someone holding the signing key, who can build and sign anything.
#
# CORRECTED 2026-09-06. This used to name `-SkipApplicationCompile` as the concrete unsafe
# path, because it packaged whatever hex was sitting in .arduino-build/msv3-probe/ and stamped
# it with whatever --ApplicationVersion it was given. That is no longer true: the switch now
# builds the bootloader ONLY and emits no application or migration package at all, precisely
# because a same-sensor stale image would pass both the identity and the version check while
# being the wrong code. The relabel risk is now human, not a switch you can reach by accident.
IDENTITY_CONTRACT_MIN_APP_VERSION = 65538


class FleetError(Exception):
    """A registry, advertisement, or package fact did not hold. Always fail closed on this."""


def normalise_address(address: str) -> str:
    text = address.strip().upper().replace("-", ":")
    if not ADDRESS_RE.match(text):
        raise FleetError(f"not a BLE address: {address!r}")
    return text


def next_address(address: str) -> str:
    """address + 1 in the last octet -- where a bootloader with no valid application advertises."""
    text = normalise_address(address)
    return f"{text[:-2]}{(int(text[-2:], 16) + 1) & 0xFF:02X}"


# ------------------------------------------------------------------ registry ----

@dataclass(frozen=True)
class Probe:
    sensor_id: int
    name: str
    application_address: str
    recovery_address: str
    bootloader: str
    device_type: int
    ota_capable: bool
    deployed: bool = True
    bootloader_key_sha256: str = ""
    legacy_package_sha256: tuple = ()
    # The application_version of the package last installed on this probe, as bookkeeping -- the
    # bootloader cannot report it. Measured 2026-09-05: the device accepts a LOWER version without
    # complaint, because dfu_init.c's version check is an explicit empty template. So this is the
    # only place a rollback can be noticed at all, and it is host-side and therefore defeatable:
    # it catches the accident, not an attacker. 0 means "not recorded", which disables the check.
    installed_application_version: int = 0
    # MUTABLE PHYSICAL STATE: are this probe's SWD/USB pads reachable right now? Conformal
    # coating makes it false, and it must be set false the moment a probe is coated. Defaults
    # to False so a probe added without the field is treated as unreachable -- the safe
    # direction, since the only thing gated on it is permission to build an image that
    # deliberately never advertises.
    swd_accessible: bool = False
    # DURABLE FACT, deliberately separate from the above: this hardware has demonstrated the
    # Guard-2 watchdog-loop -> BLE DFU escape. Nothing gates on it; having passed the test once
    # says nothing about whether the probe can be rescued today.
    guard2_validated: bool = False
    # DURABLE, PER-BOARD FACT: the date UICR.NRFFW[1] was READ on this specific probe and found to
    # hold the MBR params page address. Empty means never read, which is the safe default.
    #
    # This is the precondition for delivering a BOOTLOADER over the air. The bootloader region is
    # rewritten by the MBR (SD_MBR_COMMAND_COPY_BL), not by the bootloader, and the MBR journals
    # that copy into the params page so an interrupted one resumes on the next boot. It finds that
    # page through UICR.NRFFW[1]. If that word is erased, an interrupted copy has nowhere to resume
    # from and the board is unrecoverable without SWD -- which is exactly the population this
    # feature exists for.
    #
    # bootloader_settings.c writes the word when it reads blank, so it is nearly always set. "Nearly
    # always" is not a basis for bricking a coated probe, so ble_ota_update.py refuses a
    # bootloader-bearing package unless this field is populated for the target. Read it with the
    # `uicr` command in MSV3_Probe_Bringup_Test and record the date here -- per board, never
    # inferred from a sibling.
    mbr_params_page_verified: str = ""
    notes: str = ""

    @property
    def bootloader_release(self) -> tuple[int, ...] | None:
        """The MSV3 bootloader release as a comparable tuple, or None if it is not an MSV3 one.

        Parsed structurally rather than compared as a string: "msv3-1.0.10" sorts BEFORE
        "msv3-1.0.3" lexically, which is exactly the kind of quiet wrong answer a safety gate
        must not produce. A stock Adafruit loader returns None, which callers must treat as
        "does not have any MSV3 guarantee", never as "new enough".
        """
        prefix = "msv3-"
        if not self.bootloader.startswith(prefix):
            return None
        try:
            return tuple(int(part) for part in self.bootloader[len(prefix):].split("."))
        except ValueError:
            return None

    @property
    def reserves_boot_guard_ram(self) -> bool:
        """Does this probe's bootloader keep its stack out of the 0x2003FFF8 retained record?

        Measured from vector[0] of each shipped release hex on 2026-09-09:
        1.0.1 -> MSP 0x20040000, 1.0.2 -> MSP 0x20040000, 1.0.3 -> MSP 0x2003FFF8. So only
        1.0.3 and later leave the eight bytes alone; on anything older the bootloader's own
        stack is the first thing to descend into them, and a guard-enabled application would
        count boots that always read back as zero.
        """
        release = self.bootloader_release
        return release is not None and release >= (1, 0, 3)

    @property
    def bootloader_addresses(self) -> list[str]:
        """Every address the bootloader may legitimately appear on, most likely first.

        After a buttonless jump it reuses the application's address (the peer-data handoff carries
        it across the reset). Only a bootloader starting with no valid application uses +1.
        """
        return [self.application_address, self.recovery_address]

    @property
    def all_addresses(self) -> list[str]:
        return self.bootloader_addresses

    def describe(self) -> str:
        return (f"probe {self.sensor_id} ({self.name}) app={self.application_address} "
                f"recovery={self.recovery_address} bootloader={self.bootloader} "
                f"device_type={self.device_type:#06x}")


class Fleet:
    def __init__(self, probes: list[Probe], source: Path):
        self.source = source
        self._probes = probes
        by_id: dict[int, Probe] = {}
        by_address: dict[str, Probe] = {}
        for probe in probes:
            if probe.sensor_id in by_id:
                raise FleetError(f"{source}: sensor_id {probe.sensor_id} appears twice")
            by_id[probe.sensor_id] = probe
            for address in probe.all_addresses:
                owner = by_address.get(address)
                if owner is not None:
                    raise FleetError(
                        f"{source}: address {address} is claimed by both probe {owner.sensor_id} "
                        f"and probe {probe.sensor_id}")
                by_address[address] = probe
        self._by_id = by_id
        self._by_address = by_address

    def __iter__(self):
        return iter(self._probes)

    def __len__(self):
        return len(self._probes)

    def by_sensor_id(self, sensor_id: int) -> Probe:
        try:
            return self._by_id[sensor_id]
        except KeyError:
            known = ", ".join(str(p.sensor_id) for p in self._probes)
            raise FleetError(
                f"probe {sensor_id} is not in {self.source}. Known probes: {known}. Add it there "
                f"before flashing it, so its addresses and bootloader are recorded.") from None

    def by_address(self, address: str) -> Optional[Probe]:
        return self._by_address.get(normalise_address(address))


def load_fleet(path: Path | None = None) -> Fleet:
    source = Path(path) if path else FLEET_PATH
    if not source.exists():
        raise FleetError(f"fleet registry missing at {source}")
    raw = json.loads(source.read_text(encoding="utf-8"))
    probes = []
    for entry in raw["probes"]:
        application = normalise_address(entry["application_address"])
        recovery = normalise_address(entry["recovery_address"])
        probes.append(Probe(
            sensor_id=int(entry["sensor_id"]),
            name=entry["name"],
            application_address=application,
            recovery_address=recovery,
            bootloader=entry["bootloader"],
            device_type=int(entry["device_type"], 0),
            ota_capable=bool(entry["ota_capable"]),
            deployed=bool(entry.get("deployed", True)),
            bootloader_key_sha256=entry.get("bootloader_key_sha256", "").lower(),
            legacy_package_sha256=tuple(
                h.lower() for h in entry.get("legacy_package_sha256", [])),
            installed_application_version=int(entry.get("installed_application_version", 0)),
            swd_accessible=bool(entry.get("swd_accessible", False)),
            guard2_validated=bool(entry.get("guard2_validated", False)),
            mbr_params_page_verified=str(entry.get("mbr_params_page_verified", "") or ""),
            notes=entry.get("notes", ""),
        ))
    return Fleet(probes, source)


# ----------------------------------------------------------- advertisements ----

@dataclass(frozen=True)
class Advertisement:
    """A decoded MSV-family manufacturer payload, company ID already stripped by the scanner.

    `wide` means battery and flags were decoded. `diagnostic` marks a bring-up heartbeat: its
    DATA_CH0 is not production telemetry, and only its charging and OTA-gate bits are defined, so
    the other flag properties read None for it rather than a misleading False.
    """

    sensor_id: int
    wake: int
    raw_bytes: bytes
    wide: bool
    data_ch0: Optional[int] = None
    battery_mv: Optional[int] = None
    flags: Optional[int] = None
    die_temp_c: Optional[float] = None
    bare_ch0: Optional[int] = None
    diagnostic: bool = False

    def _production_bit(self, mask: int) -> Optional[bool]:
        if self.flags is None or self.diagnostic:
            return None
        return bool(self.flags & mask)

    # flags bits, from the sketch's advertisement layout comment
    @property
    def charging(self) -> Optional[bool]:
        return None if self.flags is None else bool(self.flags & 0x01)

    @property
    def phase(self) -> Optional[int]:
        bit = self._production_bit(0x02)
        return None if bit is None else int(bit)

    @property
    def temp_valid(self) -> Optional[bool]:
        return self._production_bit(0x04)

    @property
    def bare_valid(self) -> Optional[bool]:
        return self._production_bit(0x08)

    @property
    def reading_invalid(self) -> Optional[bool]:
        """bit5: the FDC read failed and DATA_CH0 is a deliberate 0. Always clear before 2026-09-04."""
        return self._production_bit(0x20)

    @property
    def capless(self) -> Optional[bool]:
        """bit6: capless firmware, which never selects the disconnected mux 2/3."""
        return self._production_bit(0x40)

    @property
    def ota_gate_open(self) -> Optional[bool]:
        """bit4. Only ever SET by OTA-aware firmware.

        Clear is therefore ambiguous on its own: it means either a shut power gate or firmware
        with no OTA feature at all. Read it together with battery_mv -- clear while comfortably
        above OTA_MIN_BATTERY_MV (3800) means pre-OTA firmware, which is how probe 5 was
        identified as unmigrated on 2026-09-04.
        """
        return None if self.flags is None else bool(self.flags & 0x10)


def decode_manufacturer_data(data: bytes) -> Optional[Advertisement]:
    """Decode an MSV manufacturer payload, or return None if it is not one.

    Only the first 8 bytes (magic, sensor ID, 24-bit wake) are common to every generation's wire
    format, so anything shorter than the MSV3 wide layout is decoded to that prefix and marked
    `wide=False` rather than being parsed speculatively against a layout it may not use.
    """
    if len(data) < 8 or data[0:4] != BLE_MAGIC:
        return None
    sensor_id = data[4]
    wake = int.from_bytes(data[5:8], "little")
    if len(data) < MSV3_WIDE_LEN:
        return Advertisement(sensor_id=sensor_id, wake=wake, raw_bytes=bytes(data), wide=False)
    data_ch0, battery_mv, flags = struct.unpack_from("<IHB", data, 8)
    (temp_q4,) = struct.unpack_from("<h", data, 15)
    (bare_ch0,) = struct.unpack_from("<I", data, 17)
    return Advertisement(
        sensor_id=sensor_id,
        wake=wake,
        raw_bytes=bytes(data),
        wide=True,
        data_ch0=data_ch0,
        battery_mv=battery_mv,
        flags=flags,
        die_temp_c=(temp_q4 / 4.0) if flags & 0x04 else None,
        bare_ch0=bare_ch0 if flags & 0x08 else None,
    )


def decode_diagnostic_manufacturer_data(data: bytes) -> Optional[Advertisement]:
    """Decode a targeted bring-up heartbeat, or return None if it is not one.

    Kept apart from decode_manufacturer_data so nothing takes it for production telemetry: its
    DATA_CH0 is 0 in the periodic OTA heartbeat and a console-requested reading otherwise.
    """
    if len(data) < DIAGNOSTIC_LEN or data[0:4] != DIAGNOSTIC_MAGIC:
        return None
    data_ch0, battery_mv, flags = struct.unpack_from("<IHB", data, 8)
    return Advertisement(
        sensor_id=data[4],
        wake=int.from_bytes(data[5:8], "little"),
        raw_bytes=bytes(data),
        wide=True,
        data_ch0=data_ch0,
        battery_mv=battery_mv,
        flags=flags,
        diagnostic=True,
    )


def advertisement_from_bleak(advertisement) -> Optional[Advertisement]:
    """Pull the production MSV payload out of a Bleak AdvertisementData, if it carries one."""
    payload = (advertisement.manufacturer_data or {}).get(BLE_COMPANY_ID)
    return decode_manufacturer_data(payload) if payload else None


def any_advertisement_from_bleak(advertisement) -> Optional[Advertisement]:
    """Production telemetry or a bring-up heartbeat. Check `.diagnostic` before using data_ch0."""
    payload = (advertisement.manufacturer_data or {}).get(BLE_COMPANY_ID)
    if not payload:
        return None
    data = bytes(payload)
    return decode_manufacturer_data(data) or decode_diagnostic_manufacturer_data(data)


# ------------------------------------------------------------------ packages ----

@dataclass(frozen=True)
class Package:
    path: Path
    sensor_id: int
    name: str
    device_type: int
    application_version: int
    image_length: int
    sha256: str
    dfu_version: float
    signed: bool
    build_id: str = ""
    # Tri-state, see GUARD2_RE: True = guard 2 compiled in, False = compiled out, None = the
    # image predates the marker and cannot say.
    boot_guard: bool | None = None
    # True when the ZIP carries a SoftDevice and/or bootloader alongside the application. Only
    # reachable through read_package(allow_bootloader=True); see that function.
    contains_bootloader: bool = False

    def describe(self) -> str:
        signature = "signed" if self.signed else "UNSIGNED"
        build = f", build {self.build_id}" if self.build_id else ""
        payload = " SD+BL+APP" if self.contains_bootloader else ""
        return (f"{self.path.name}: {self.name} (sensor {self.sensor_id}), "
                f"device_type={self.device_type:#06x}, app_version={self.application_version}, "
                f"{self.image_length} bytes, {signature}{build}{payload}")


# Manifest keys a bootloader-bearing package may carry beside its application. nrfutil emits the
# combined "softdevice_bootloader" entry when --softdevice and --bootloader are given together;
# the separate spellings are accepted so a future single-component package still parses.
BOOTLOADER_MANIFEST_KEYS = {"softdevice_bootloader", "bootloader", "softdevice"}


def read_package(path: Path, allow_bootloader: bool = False) -> Package:
    """Parse a DFU ZIP and read the identity out of the application image itself.

    Fails closed on zero or multiple identity strings. Zero means the image is not an MSV3
    production application (the LED test is the usual case); multiple means the assumption that
    the firmware carries exactly one advertised name no longer holds, and every check downstream
    of that assumption is unsafe.

    allow_bootloader admits a SoftDevice/bootloader-bearing package. It is OFF by default and no
    ordinary update path turns it on -- delivering a bootloader rewrites the one component that
    cannot be re-flashed over the air if it goes wrong, so it must be asked for explicitly.

    An APPLICATION IS STILL REQUIRED even under allow_bootloader, and that is a safety property,
    not an accident of packaging. Every wrong-target guard in this project binds a package to a
    probe through the MSV3-MOISTURE-NNN string compiled into the application image; a
    bootloader-only package carries no identity at all, so nothing downstream could tell which
    board it was built for. Shipping SD+BL+APP keeps that binding intact.
    """
    path = Path(path)
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read("manifest.json"))["manifest"]
        dfu_version = float(manifest["dfu_version"])
        if "application" not in manifest:
            raise FleetError(
                f"{path.name}: package has no application. Even a bootloader delivery must carry "
                f"one -- it is the only thing in the package that states which probe it is for.")
        extra = set(manifest) - {"dfu_version", "application"}
        contains_bootloader = bool(extra)
        if extra and not allow_bootloader:
            raise FleetError(
                f"{path.name}: package contains more than an application "
                f"({sorted(extra)}). Refusing -- delivering a bootloader over BLE rewrites the one "
                f"component that cannot be re-flashed over the air, so it is never the default. "
                f"Pass --bootloader to ble_ota_update.py if that is genuinely what you intend.")
        if extra - BOOTLOADER_MANIFEST_KEYS:
            raise FleetError(
                f"{path.name}: package contains unrecognised components "
                f"{sorted(extra - BOOTLOADER_MANIFEST_KEYS)}. Refusing rather than transferring a "
                f"payload this tool cannot account for.")
        entry = manifest["application"]
        init = entry["init_packet_data"]
        image = archive.read(entry["bin_file"])

    matches = sorted(set(IDENTITY_RE.findall(image)))
    if not matches:
        raise FleetError(
            f"{path.name}: no MSV3-MOISTURE-NNN identity string in the application image. Either "
            f"this is not an MSV3 production application, or the firmware stopped storing its "
            f"advertised name as a literal (a runtime snprintf() would do that). Use "
            f"--allow-identity-mismatch only if you are certain what this image is.")
    if len(matches) > 1:
        found = ", ".join(m.decode() for m in matches)
        raise FleetError(f"{path.name}: image contains several identity strings ({found})")

    sensor_id = int(matches[0])

    builds = sorted(set(BUILD_ID_RE.findall(image)))
    if len(builds) > 1:
        found = ", ".join(b.decode() for b in builds)
        raise FleetError(f"{path.name}: image contains several build ids ({found})")
    build_id = builds[0].decode() if builds else ""

    # Fail closed on anything but exactly one well-formed marker. Deliberately NOT de-duplicated
    # first: the sketch emits precisely one, so two -- even two identical ones -- means the image
    # is not the shape this gate assumes, and a safety check must not paper over that.
    guards = GUARD2_RE.findall(image)
    if len(guards) > 1:
        found = ", ".join(sorted(g.decode() for g in guards))
        raise FleetError(
            f"{path.name}: image contains {len(guards)} MSV3-GUARD2 markers ({found}); exactly "
            f"one is expected. Refusing -- the boot-guard/bootloader RAM pairing cannot be "
            f"checked against an image that does not state one configuration.")
    # A marker the regex could not parse (a value other than 0/1, or a missing NUL terminator)
    # would otherwise fall through as "unmarked", turning a corrupt or unexpected image into the
    # most permissive answer. Detect the bare prefix and refuse instead.
    if not guards and b"MSV3-GUARD2" in image:
        raise FleetError(
            f"{path.name}: image contains a malformed MSV3-GUARD2 marker. Refusing rather than "
            f"treating an unparseable marker as an absent one.")
    boot_guard = (guards[0] == b"1") if guards else None

    return Package(
        path=path,
        sensor_id=sensor_id,
        name=f"MSV3-MOISTURE-{sensor_id:03d}",
        device_type=int(init["device_type"]),
        application_version=int(init["application_version"]),
        image_length=len(image),
        sha256=hashlib.sha256(image).hexdigest(),
        dfu_version=dfu_version,
        signed=(dfu_version == 0.8),
        build_id=build_id,
        boot_guard=boot_guard,
        contains_bootloader=contains_bootloader,
    )


def require_ecdsa():
    import sys
    if str(DFU_PYDEPS) not in sys.path:
        sys.path.insert(0, str(DFU_PYDEPS))
    try:
        import ecdsa  # noqa: F401
    except ModuleNotFoundError as error:
        raise FleetError(
            f"the 'ecdsa' package is needed to verify a signed package and is not importable "
            f"(looked in the environment and in {DFU_PYDEPS}). Recreate the DFU dependency set "
            f"with MSV3/scripts/prepare_msv3_dfu_tools.ps1. Refusing to transfer a signed package "
            f"whose signature has not been checked.") from error
    return ecdsa


def load_release_public_key(release: str, expected_sha256: str = "") -> bytes:
    """The 64-byte P-256 public point the named bootloader release was built to trust.

    `expected_sha256` is the fingerprint the registry records for the probe being targeted. Without
    it, verifying a package against `release-manifest.json` only proves the package matches
    whatever key that file currently names -- and that file is an ordinary tracked JSON document
    that a regenerated or restored release would overwrite. Pinning it in the registry means
    changing the client's trust root takes a visible edit to two files instead of one.
    """
    manifest_path = BOOTLOADER_RELEASES / release / "release-manifest.json"
    if not manifest_path.exists():
        raise FleetError(f"no bootloader release manifest at {manifest_path}")
    text = json.loads(manifest_path.read_text(encoding="utf-8"))["public_verification_key"]
    qx = re.search(r"Qx:\s*([0-9a-fA-F]{64})", text)
    qy = re.search(r"Qy:\s*([0-9a-fA-F]{64})", text)
    if not qx or not qy:
        raise FleetError(f"{manifest_path}: could not read Qx/Qy from public_verification_key")
    key = bytes.fromhex(qx.group(1)) + bytes.fromhex(qy.group(1))

    actual = hashlib.sha256(key).hexdigest()
    if expected_sha256:
        if actual != expected_sha256.lower():
            raise FleetError(
                f"{manifest_path} names a public key with fingerprint {actual}, but the registry "
                f"pins {expected_sha256.lower()} for this probe. Either the manifest was "
                f"regenerated against a different signing key, or the registry entry is stale. "
                f"Do not override this: it decides which key the board will accept.")
    else:
        raise FleetError(
            f"the registry records no bootloader_key_sha256 for this probe, so the key in "
            f"{manifest_path.name} (fingerprint {actual}) cannot be confirmed as the one its "
            f"bootloader actually trusts. Add \"bootloader_key_sha256\": \"{actual}\" to the "
            f"probe's entry once you have confirmed it.")
    return key


def package_predates_identity_contract(package: "Package") -> bool:
    """True when this package's advertised name cannot be trusted to equal its SENSOR_ID.

    See IDENTITY_CONTRACT_MIN_APP_VERSION. Unsigned packages are already refused elsewhere for
    other reasons, so this only has to reason about real releases.
    """
    return package.application_version < IDENTITY_CONTRACT_MIN_APP_VERSION


def verify_package_integrity(path: Path, expected_device_type: int, public_key: bytes | None):
    """Full fail-closed package check, run BEFORE anything is written or any board is jumped.

    Deliberately independent of `read_package()`: identity is about *which probe*, this is about
    *whether the bootloader will accept the image at all*. A package that fails here would be
    rejected after the jump, leaving the probe sitting in recovery for no reason.

    `public_key` is the verification key compiled into the target's bootloader, taken from the
    release manifest rather than from the private key -- so this proves the package will actually
    verify on the board, and needs no access to the signing secret.
    """
    path = Path(path)
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read("manifest.json"))["manifest"]
        dfu_version = float(manifest["dfu_version"])
        entry = manifest["application"]
        init = entry["init_packet_data"]
        image = archive.read(entry["bin_file"])
        dat = archive.read(entry["dat_file"])

    if init["device_type"] != expected_device_type:
        raise FleetError(
            f"{path.name}: built for device type {init['device_type']:#06x}, but the target's "
            f"bootloader accepts {expected_device_type:#06x}. It would be rejected after the jump.")

    if init["application_version"] in (0, 0xFFFFFFFF):
        raise FleetError(
            f"{path.name}: application_version is {init['application_version']:#x}, which is the "
            f"unset value the raw Arduino DFU wrapper emits. Build the release through "
            f"MSV3/scripts/build_msv3_release.ps1 instead.")

    if dfu_version == 0.8:
        if len(image) != init["firmware_length"]:
            raise FleetError(f"{path.name}: image-length mismatch")
        if hashlib.sha256(image).hexdigest() != init["firmware_hash"]:
            raise FleetError(f"{path.name}: SHA-256 mismatch")
        if public_key is None:
            raise FleetError(f"{path.name}: signed package, but no verification key to check it")
        ecdsa = require_ecdsa()
        from ecdsa.keys import sigdecode_string
        signature = bytes.fromhex(init["init_packet_ecds"])
        if dat[-64:] != signature:
            raise FleetError(f"{path.name}: signature is not the final init-packet field")
        verifying_key = ecdsa.VerifyingKey.from_string(public_key, curve=ecdsa.NIST256p)
        try:
            verifying_key.verify(signature, dat[:-64],
                                 hashfunc=hashlib.sha256, sigdecode=sigdecode_string)
        except Exception as error:
            raise FleetError(
                f"{path.name}: ECDSA signature does not verify against the key the target "
                f"bootloader trusts. The board would reject this image. ({error})") from None
    elif dfu_version == 0.5:
        import binascii
        if binascii.crc_hqx(image, 0xFFFF) != init["firmware_crc16"]:
            raise FleetError(f"{path.name}: CRC16 mismatch")
    else:
        raise FleetError(f"{path.name}: unsupported DFU version {dfu_version}")


# ------------------------------------------------------------- safety gates ----

def check_boot_guard_pairing(probe: Probe, boot_guard: bool | None,
                             image_sha256: str = "") -> str | None:
    """Refusal text if this guard configuration is unsafe on this probe, else None.

    Guard 2's counter is an eight-byte record at 0x2003FFF8 shared between the application and
    the bootloader. Measured from vector[0] of each release hex: bootloader 1.0.1 and 1.0.2 boot
    with MSP = 0x20040000, so that record is the first memory their stacks descend into; 1.0.3
    boots with MSP = 0x2003FFF8 and leaves it alone. A guard-enabled application on an older
    bootloader therefore counts boots that always read back as zero -- a guard that looks armed
    and can never fire, which is exactly the GPREGRET2 failure the retained-RAM scheme replaced.

    THE UNMARKED CASE, and why it is not simply allowed. Every package built before 2026-09-09
    lacks the marker, so absence cannot mean "guard on". But it cannot mean "guard off" either:
    a signature proves the bytes are authorised, not that the guard was compiled out. Guard 2
    existed for two days before the marker did, so unmarked-but-guard-enabled images genuinely
    exist. On a bootloader that already reserves the record the distinction cannot hurt anything,
    so unmarked is allowed there. On an older bootloader it CAN hurt, so it needs the same
    hash-pinned, reviewable exception the identity contract uses -- an edit to
    legacy_package_sha256 in MSV3/fleet.json, not a command-line flag anybody can reach for.

    Verified 2026-09-09 that this costs the coating work nothing: the rescue packages actually
    installed on probes 5/6/7/8 (v65555/65556/65556/65557) are all unmarked AND all target
    bootloader 1.0.3, so every one of them still passes.
    """
    if boot_guard is True:
        if probe.reserves_boot_guard_ram:
            return None
        return (
            f"probe {probe.sensor_id} runs bootloader {probe.bootloader}, which does not reserve "
            f"the boot-guard retained record at 0x2003FFF8 (its stack starts at 0x20040000 and "
            f"descends straight through it). This image has Guard 2 compiled in, so on this probe "
            f"the boot counter would always read back as zero: the guard would look armed and "
            f"could never escape into DFU. Install bootloader 1.0.3 or later over SWD first, or "
            f"build this image without -BootGuard. See MSV3/bootloader/README.md.")

    if boot_guard is False:
        return None

    # Unmarked from here on.
    if probe.reserves_boot_guard_ram:
        return None
    if image_sha256 and image_sha256.lower() in probe.legacy_package_sha256:
        return None
    return (
        f"probe {probe.sensor_id} runs bootloader {probe.bootloader}, which does not reserve the "
        f"boot-guard retained record at 0x2003FFF8, and this image carries no MSV3-GUARD2 marker "
        f"so nothing here can prove Guard 2 is compiled out of it.\n"
        f"  Guard 2 predates the marker by two days, so an unmarked image genuinely may have it "
        f"enabled -- in which case its boot counter on this probe would always read back as zero.\n"
        f"  If this exact image is known to be guard-free, record its image SHA-256 "
        f"{image_sha256 or '<unknown>'} in \"legacy_package_sha256\" for probe {probe.sensor_id}. "
        f"That is a reviewable registry edit rather than a flag, deliberately.")


def check_hang_test_target(probe: Probe) -> str | None:
    """Refusal text if a deliberately-hanging image must not be built for this probe.

    A -HangTest image boots, passes every integrity check, and then never opens a radio window.
    On a probe whose pads are reachable that is a controlled experiment; on a coated probe it is
    a brick with no way back, because the only recovery channel is the radio it never opens.

    Gated on swd_accessible, which is MUTABLE state, not on guard2_validated, which is history.
    Probe 8 passing this test as probe 99 says nothing about whether it can be rescued today.
    """
    if not probe.swd_accessible:
        return (
            f"probe {probe.sensor_id} is not marked swd_accessible in MSV3/fleet.json. A "
            f"-HangTest image deliberately never advertises, so the only way back is the SWD/USB "
            f"pads. If this probe really is on the bench with a debugger attached, set "
            f"\"swd_accessible\": true for it -- and set it false again the moment it is coated.")
    return None


def _cli(argv: list[str]) -> int:
    """Tiny gate CLI so PowerShell build wrappers do not have to parse fleet.json themselves.

    This module is the single reader of MSV3/fleet.json (see the module docstring). A second
    parser in PowerShell would be a second place for the registry's meaning to drift, which is
    the class of bug this whole file exists to prevent -- so the wrapper shells out to here.
    """
    import argparse

    parser = argparse.ArgumentParser(prog="msv3_fleet", description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    gate = sub.add_parser("gate", help="check whether a dangerous build mode is safe for a probe")
    gate.add_argument("--sensor-id", type=int, required=True)
    gate.add_argument("--boot-guard", action="store_true",
                      help="the image will have Guard 2 compiled in")
    gate.add_argument("--hang-test", action="store_true",
                      help="the image will deliberately hang before advertising")
    args = parser.parse_args(argv)

    try:
        probe = load_fleet().by_sensor_id(args.sensor_id)
    except FleetError as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 2

    refusals = []
    if args.hang_test:
        refusals.append(check_hang_test_target(probe))
    if args.boot_guard:
        refusals.append(check_boot_guard_pairing(probe, True))
    refusals = [r for r in refusals if r]

    if refusals:
        for refusal in refusals:
            print(f"REFUSED: {refusal}", file=sys.stderr)
        return 1

    print(f"OK: probe {probe.sensor_id} ({probe.name}) bootloader={probe.bootloader} "
          f"swd_accessible={probe.swd_accessible}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli(sys.argv[1:]))
