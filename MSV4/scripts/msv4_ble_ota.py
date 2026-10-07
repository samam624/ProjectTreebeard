#!/usr/bin/env python3
"""Install a signed application on an MSV4 board over BLE.

MSV4 boards are deliberately not in the MSV3 OTA registry (fleet.json `probes[]`), so
scripts/ble_ota_update.py cannot and must not target them. This is the MSV4 counterpart. It reads
the board from fleet.json `msv4_boards`, refuses a package that is not an MSV4 image for that
board, asks the running application to reboot into the bootloader through the buttonless DFU
service (present in both the bring-up console and the production image), then transfers with the
same hardened client settings as scripts/ble_ota_update.py.

The Adafruit bootloader's application DFU is single-bank: the old application is gone as soon as a
transfer starts. A failed transfer therefore leaves the board in BLE DFU with no application, which
is stable and retryable. Re-run with --skip-jump.

Run it with the portable Python:
  MSV3\\.arduino-build\\python-portable\\runtime\\python.exe MSV4\\scripts\\msv4_ble_ota.py PACKAGE --board 1 --preflight-only
  MSV3\\.arduino-build\\python-portable\\runtime\\python.exe MSV4\\scripts\\msv4_ble_ota.py PACKAGE --board 1

Afterwards prove the result with msv4_adv_scan.py --require-progress, then update
`installed_application_version` and `installed_build` for the board in fleet.json.
"""

from __future__ import annotations

import argparse
import asyncio
import functools
import json
import re
import sys
import time
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import ble_ota_update as ota  # noqa: E402
from msv3_fleet import FleetError, load_release_public_key, verify_package_integrity  # noqa: E402

FLEET_PATH = REPO_ROOT / "MSV3" / "fleet.json"

MSV4_NAME_RE = re.compile(rb"MSV4-MOISTURE-(\d{3})\x00")
MSV4_BUILD_RE = re.compile(rb"MSV4-BUILD:([0-9A-Za-z._-]{1,48})\x00")
MSV3_IDENTITY_RE = re.compile(rb"MSV3-(?:MOISTURE-\d{3}|BUILD:)")
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
# flags2 bit2 in the production packet (Bleak payload index 23): a short non-connectable burst.
SHORT_BURST_FLAG2 = 0x04


class Refused(Exception):
    """A check failed. Nothing has been written and nothing has been jumped."""


class _DfuAsyncioProxy:
    """Raises dfu_lib's packet-receipt deadline, exactly as ble_ota_update.run() does.

    dfu_lib waits max(0.8, prn * 0.08) s for each receipt, then logs "PRN Timeout, continuing
    anyway" and keeps streaming. On a weak link the receipts are merely late, the client outruns
    the bootloader's flash writes, and Receive Firmware Image fails with status 6 once the whole
    image has been sent. Measured on board 1 at -78 dBm on 2026-10-06: 64 timeouts, then status 6
    at 100 %. Only sub-2 s timeouts are substituted, which picks out the receipt wait and nothing
    else.
    """

    def __init__(self, real, floor):
        self._real = real
        self._floor = floor

    def __getattr__(self, name):
        return getattr(self._real, name)

    async def wait_for(self, aw, timeout=None):
        if timeout is not None and timeout <= 2.0 and timeout < self._floor:
            timeout = self._floor
        return await self._real.wait_for(aw, timeout)


def load_board(number: int) -> dict:
    boards = json.loads(FLEET_PATH.read_text(encoding="utf-8")).get("msv4_boards", {}).get("boards", [])
    for board in boards:
        if int(board.get("board", -1)) == number:
            for key in ("application_address", "recovery_address", "bootloader",
                        "bootloader_key_sha256", "device_type", "installed_application_version"):
                if key not in board:
                    raise Refused(f"fleet.json msv4_boards board {number} has no '{key}'")
            return board
    raise Refused(f"fleet.json msv4_boards has no board {number}")


def preflight(package: Path, board: dict) -> dict:
    """Every check that needs no radio."""
    with zipfile.ZipFile(package) as archive:
        manifest = json.loads(archive.read("manifest.json"))["manifest"]
        extra = set(manifest) - {"dfu_version", "application"}
        if extra or "application" not in manifest:
            raise Refused(f"{package.name}: not an application-only package ({sorted(extra)})")
        entry = manifest["application"]
        init = entry["init_packet_data"]
        image = archive.read(entry["bin_file"])

    device_type = int(board["device_type"], 0)
    release = board["bootloader"].replace("msv3-", "")
    try:
        key = load_release_public_key(release, board["bootloader_key_sha256"])
        verify_package_integrity(package, device_type, key)
    except FleetError as error:
        raise Refused(str(error)) from None

    if MSV3_IDENTITY_RE.search(image):
        raise Refused(f"{package.name}: the image carries an MSV3 identity string. An MSV3 image "
                      f"drives MSV3 pins and must never go on an MSV4 board.")
    builds = sorted({m.decode() for m in MSV4_BUILD_RE.findall(image)})
    if len(builds) != 1:
        raise Refused(f"{package.name}: expected exactly one MSV4-BUILD: string in the image, "
                      f"found {builds}")
    names = sorted({int(m) for m in MSV4_NAME_RE.findall(image)})
    if len(names) > 1:
        raise Refused(f"{package.name}: more than one MSV4-MOISTURE-NNN identity in the image")
    if names:
        expected = board.get("sensor_id")
        if expected is None:
            raise Refused(f"{package.name}: built for sensor {names[0]}, but board "
                          f"{board['board']} has no sensor_id in fleet.json. Record it first.")
        if names[0] != int(expected):
            raise Refused(f"{package.name}: built for sensor {names[0]}, but board "
                          f"{board['board']} is sensor {expected}")

    version = int(init["application_version"])
    installed = int(board["installed_application_version"])
    if version <= installed:
        raise Refused(f"{package.name}: application version {version} is not above the "
                      f"{installed} recorded as installed. The bootloader keeps a monotonic "
                      f"floor; rebuild with a larger -ApplicationVersion.")

    info = {"build": builds[0], "sensor": names[0] if names else None, "version": version,
            "bytes": len(image)}
    print(f"Package   {package.name}")
    print(f"  build MSV4-BUILD:{info['build']}, application version {version} "
          f"(installed {installed}), {len(image)} bytes")
    print("  identity " + (f"MSV4-MOISTURE-{names[0]:03d}" if names else "bring-up console (no sensor ID)"))
    print(f"  signature and hash verify against bootloader {board['bootloader']}")
    print(f"Target    board {board['board']} at {board['application_address']} "
          f"(recovery {board['recovery_address']})")
    return info


async def transfer(package: Path, addresses: list[str], device, args) -> int:
    """Send the image to a board already in its bootloader. Whole-transfer retries, bounded."""
    import dfu_lib

    if sys.platform == "win32":
        # A bootloader reached after a jump sits on the application's address with a different GATT
        # table; Windows would otherwise serve the cached one.
        dfu_lib.BleakClient = functools.partial(
            dfu_lib.BleakClient, winrt={"use_cached_services": False, "address_type": "random"})
    if args.prn_timeout > 0:
        dfu_lib.asyncio = _DfuAsyncioProxy(asyncio, args.prn_timeout)

    updater = dfu_lib.NordicLegacyDFU(
        str(package), prn=args.prn, packet_delay=args.delay,
        high_mtu=not args.no_high_mtu, progress_callback=ota.progress)
    updater.parse_zip()
    if updater.upload_mode != ota.UPLOAD_MODE_APPLICATION:
        raise Refused(f"{package.name}: upload mode {updater.upload_mode:#04x} is not application-only")
    guard = ota.ReceiptGuard(image_size=len(getattr(updater, "bin_data", b"") or b""))
    ota.install_transfer_hooks(updater, guard, None)
    status = ota.ControlStatus()
    ota.install_status_hook(updater, status)

    attempts_left = max(1, args.session_retries)
    started = time.time()
    while True:
        await asyncio.sleep(args.connect_delay)
        print(f"Transferring {package.name} to {device.address}...", flush=True)
        try:
            await updater.perform_update(device, max_retries=args.retry)
            break
        except (KeyboardInterrupt, asyncio.CancelledError):
            raise
        except Exception as error:  # noqa: BLE001 -- the client raises backend-specific failures
            attempts_left -= 1
            print()
            print(f"Transfer attempt failed ({type(error).__name__}: {error}); last control "
                  f"response op={status.last_op} status={status.last_status}.", flush=True)
            if attempts_left <= 0:
                print("FAILED: out of attempts. The board is in BLE DFU with no application; "
                      "re-run with --skip-jump.")
                return 1
            # After a failed or interrupted session the bootloader resets and can return on either
            # of this board's two addresses. Only ever those two.
            device, _ = await ota.wait_for_advertisement(
                addresses, args.bootloader_timeout, "the DFU bootloader again",
                require_dfu_service=True)
            if device is None:
                print("FAILED: the bootloader did not come back. Re-run with --skip-jump.")
                return 1

    elapsed = time.time() - started
    print(f"Transfer completed in {int(elapsed // 60)}m {elapsed % 60:.0f}s.", flush=True)
    return 0


async def wait_for_connectable(address: str, timeout_s: float):
    """Scan until `address` advertises something a client can connect to.

    From application version 262154 the production image opens its connectable window only on
    measurement wakes (every tenth minute) and on the first three wakes after a boot. In between
    it sends short non-connectable bursts marked with flags2 bit2; connecting on one of those is
    impossible, and trying would only use up the jump's retries. Older images, the bring-up
    console and the bootloader never set that bit.

    A production packet has to be seen TWICE with the same counter before it is believed. Measured
    2026-10-06: the first event Windows delivers for a new window sometimes still carries the
    previous wake's payload (3 of 11 windows), so one event alone can show the last connectable
    packet at the start of a burst that is not connectable at all.
    """
    from bleak import BleakScanner

    print(f"Waiting up to {timeout_s:.0f}s for a connectable advertisement from {address}...", flush=True)
    found = asyncio.get_running_loop().create_future()
    bursts = 0
    candidate = None

    def callback(device, advertisement):
        nonlocal bursts, candidate
        if device.address.upper() != address or found.done():
            return
        payload = bytes(advertisement.manufacturer_data.get(0xFFFF, b""))
        is_production = len(payload) >= 21 and payload[0:4] == b"MSV2" and payload[14] & 0x80
        if is_production and len(payload) >= 24 and payload[23] & SHORT_BURST_FLAG2:
            candidate = None
            if bursts == 0:
                print("  heard a short non-connectable burst; waiting for the next connectable "
                      "window (up to ten minutes)", flush=True)
            bursts += 1
            return
        if is_production:
            counter = bytes(payload[5:8])
            if candidate != counter:
                candidate = counter  # believe it only when the same packet arrives again
                return
        found.set_result((device, advertisement))

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        device, advertisement = await asyncio.wait_for(found, timeout=timeout_s)
        print(f"  found {device.address} (rssi {advertisement.rssi})", flush=True)
        return device, advertisement
    except asyncio.TimeoutError:
        return None, None
    finally:
        await scanner.stop()


async def live(package: Path, board: dict, args) -> int:
    sys.path.insert(0, str(ota.DFU_CLIENT))
    pump = ota.install_windows_ble_workarounds("random")  # keep the reference; see ble_ota_update

    app_address = board["application_address"].upper()
    both = [app_address, board["recovery_address"].upper()]

    if not args.skip_jump:
        async def find_application():
            device, advertisement = await wait_for_connectable(app_address, args.app_timeout)
            if device is None:
                return None
            if ota.looks_like_bootloader(device, advertisement):
                raise Refused(f"{app_address} is advertising a DFU service: the board is already "
                              f"in its bootloader. Re-run with --skip-jump.")
            # The name travels in the scan response, which a passive sighting can miss. With no
            # name, accept only the two things an MSV4 application puts in the advertisement
            # itself: the production packet's MSV4 flag, or the bring-up console's UART service.
            name = advertisement.local_name or device.name or ""
            payload = bytes(advertisement.manufacturer_data.get(0xFFFF, b""))
            is_production = len(payload) >= 21 and payload[0:4] == b"MSV2" and payload[14] & 0x80
            is_console = NUS_SERVICE_UUID in [u.lower() for u in (advertisement.service_uuids or [])]
            if not (name.startswith("MSV4-") or (not name and (is_production or is_console))):
                raise Refused(f"{app_address} advertises as '{name}', not an MSV4 application")
            print(f"  application is '{name or ('production packet' if is_production else 'console')}'",
                  flush=True)
            return device

        device = await find_application()
        if device is None:
            print(f"FAILED: no connectable advertisement from {app_address} in "
                  f"{args.app_timeout:.0f}s. Nothing was requested.")
            return 1
        try:
            status = await ota.jump_to_bootloader(
                device, "random", retries=args.retry, reconnect=find_application,
                connect_delay=args.connect_delay)
        except ota.JumpError as error:
            print(f"FAILED: {error}")
            return 1
        print(f"  jump: {status}", flush=True)
        if status.startswith("REJECTED"):
            return 1

    device, _ = await ota.wait_for_advertisement(
        both, args.bootloader_timeout, "the DFU bootloader", require_dfu_service=True)
    if device is None:
        print("FAILED: no DFU bootloader appeared. If the application is advertising again the "
              "jump did not take; otherwise re-run with --skip-jump.")
        return 1
    try:
        return await transfer(package, both, device, args)
    finally:
        if pump is not None:
            pump.cancel()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="Signed application-only DFU ZIP")
    parser.add_argument("--board", type=int, required=True,
                        help="Board number from fleet.json msv4_boards")
    parser.add_argument("--preflight-only", action="store_true",
                        help="Run every package and target check and stop before the radio")
    parser.add_argument("--skip-jump", action="store_true",
                        help="Board is already sitting in its bootloader; go straight to the transfer")
    parser.add_argument("--app-timeout", type=float, default=700.0,
                        help="Seconds to wait for a connectable advertisement (default 700: the "
                             "production image opens one every ten minutes)")
    parser.add_argument("--bootloader-timeout", type=float, default=60.0,
                        help="Seconds to wait for the bootloader after a jump (default 60)")
    parser.add_argument("--connect-delay", type=float, default=2.0,
                        help="Seconds between ending a scan and opening GATT (default 2.0)")
    parser.add_argument("--retry", type=int, default=3,
                        help="Jump and DFU connection attempts (default 3)")
    parser.add_argument("--session-retries", type=int, default=3,
                        help="Whole-transfer attempts before giving up (default 3)")
    parser.add_argument("--prn", type=int, default=16, help="Packet receipt interval (default 16)")
    parser.add_argument("--prn-timeout", type=float, default=6.0,
                        help="Seconds to wait for a packet receipt (default 6.0; 0 = dfu_lib's own)")
    parser.add_argument("--delay", type=float, default=0.6, help="Start/size delay seconds (default 0.6)")
    parser.add_argument("--no-high-mtu", action="store_true",
                        help="Force the 20-byte chunk path instead of a negotiated MTU")
    args = parser.parse_args()

    if not args.package.is_file():
        parser.error(f"package not found: {args.package}")
    try:
        board = load_board(args.board)
        preflight(args.package.resolve(), board)
        if args.preflight_only:
            print("\nPreflight passed. Stopping before the radio (--preflight-only).")
            return 0
        return asyncio.run(live(args.package.resolve(), board, args))
    except Refused as error:
        print(f"\nREFUSED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
