#!/usr/bin/env python3
"""Preflight refusals of msv4_ble_ota.py, against synthetic packages. No radio, no signing key.

The signature and hash check is msv3_fleet.verify_package_integrity(), which has its own tests;
here it is replaced with a stub so these cases isolate the MSV4-specific decisions: which image
may go on which board.

  python MSV4/scripts/test_msv4_ble_ota.py
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import msv4_ble_ota as ota  # noqa: E402
from msv3_fleet import FleetError  # noqa: E402

BOARD = {
    "board": 1,
    "sensor_id": 10,
    "application_address": "F6:26:7D:76:AD:95",
    "recovery_address": "F6:26:7D:76:AD:96",
    "bootloader": "msv3-1.0.3",
    "bootloader_key_sha256": "00" * 32,
    "device_type": "0x3356",
    "installed_application_version": 262150,
}

PRODUCTION = b"\xde\xad" + b"MSV4-MOISTURE-010\x00" + b"MSV4-BUILD:prod-test\x00" + b"\xbe\xef"
CONSOLE = b"\xde\xad" + b"MSV4-BUILD:bringup-test\x00" + b"\xbe\xef"


class PreflightTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        for name in ("load_release_public_key", "verify_package_integrity"):
            patcher = mock.patch.object(ota, name, return_value=b"key")
            patcher.start()
            self.addCleanup(patcher.stop)

    def package(self, image: bytes, version: int = 262151, extra_manifest: dict | None = None) -> Path:
        path = Path(self.dir.name) / "package.zip"
        manifest = {
            "dfu_version": 0.8,
            "application": {
                "bin_file": "app.bin",
                "dat_file": "app.dat",
                "init_packet_data": {"application_version": version, "device_type": 0x3356},
            },
        }
        manifest.update(extra_manifest or {})
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("manifest.json", json.dumps({"manifest": manifest}))
            archive.writestr("app.bin", image)
            archive.writestr("app.dat", b"")
        return path

    def preflight(self, path: Path, board: dict | None = None) -> dict:
        with contextlib.redirect_stdout(io.StringIO()):
            return ota.preflight(path, board or BOARD)

    def assert_refused(self, path: Path, fragment: str, board: dict | None = None):
        with self.assertRaises(ota.Refused) as caught:
            self.preflight(path, board)
        self.assertIn(fragment, str(caught.exception))

    def test_production_image_for_this_sensor_is_accepted(self):
        info = self.preflight(self.package(PRODUCTION))
        self.assertEqual(info["build"], "prod-test")
        self.assertEqual(info["sensor"], 10)
        self.assertEqual(info["version"], 262151)

    def test_console_image_needs_no_sensor_id(self):
        info = self.preflight(self.package(CONSOLE))
        self.assertEqual(info["build"], "bringup-test")
        self.assertIsNone(info["sensor"])

    def test_msv3_image_is_refused(self):
        # The bootloader shares MSV3's device type and would take it; this check is the only gate.
        for image in (b"MSV3-MOISTURE-004\x00MSV3-BUILD:x\x00", PRODUCTION + b"MSV3-BUILD:x\x00"):
            self.assert_refused(self.package(image), "MSV3 identity")

    def test_wrong_sensor_is_refused(self):
        image = PRODUCTION.replace(b"MOISTURE-010", b"MOISTURE-011")
        self.assert_refused(self.package(image), "built for sensor 11")

    def test_sensor_image_needs_a_recorded_sensor_id(self):
        board = {k: v for k, v in BOARD.items() if k != "sensor_id"}
        self.assert_refused(self.package(PRODUCTION), "has no sensor_id", board)

    def test_version_must_be_above_installed(self):
        for version in (262149, 262150):
            self.assert_refused(self.package(PRODUCTION, version=version), "not above")

    def test_image_must_identify_itself_exactly_once(self):
        self.assert_refused(self.package(b"\x00" * 64), "exactly one MSV4-BUILD")
        self.assert_refused(self.package(PRODUCTION + b"MSV4-BUILD:other\x00"), "exactly one MSV4-BUILD")
        self.assert_refused(self.package(PRODUCTION + b"MSV4-MOISTURE-011\x00"), "more than one")

    def test_bootloader_packages_are_refused(self):
        path = self.package(PRODUCTION, extra_manifest={"softdevice_bootloader": {}})
        self.assert_refused(path, "not an application-only package")

    def test_integrity_failure_is_a_refusal(self):
        ota.verify_package_integrity.side_effect = FleetError("SHA-256 mismatch")
        self.assert_refused(self.package(PRODUCTION), "SHA-256 mismatch")


class LoadBoardTest(unittest.TestCase):
    def test_board_must_carry_every_ota_field(self):
        with tempfile.TemporaryDirectory() as directory:
            fleet = Path(directory) / "fleet.json"
            incomplete = {k: v for k, v in BOARD.items() if k != "bootloader_key_sha256"}
            fleet.write_text(json.dumps({"msv4_boards": {"boards": [incomplete]}}), encoding="utf-8")
            with mock.patch.object(ota, "FLEET_PATH", fleet):
                with self.assertRaises(ota.Refused) as caught:
                    ota.load_board(1)
                self.assertIn("bootloader_key_sha256", str(caught.exception))
                with self.assertRaises(ota.Refused):
                    ota.load_board(2)

    def test_tracked_registry_has_a_usable_board_1(self):
        board = ota.load_board(1)
        self.assertEqual(board["sensor_id"], 10)
        self.assertEqual(int(board["device_type"], 0), 0x3356)


if __name__ == "__main__":
    unittest.main()
