#!/usr/bin/env python3
"""Install an application on an MSV3 probe already advertising its BLE DFU service.

Unlike nrf_dfu_py's normal CLI, this does not try to contact a running application
or request a buttonless jump first. It is intentionally application-only; never use
the BLE recovery path to replace the bootloader itself.
"""

import argparse
import asyncio
import ctypes
import functools
import logging
import sys
from pathlib import Path


# Lives at the repository root alongside the other flashing tools. The pinned DFU client and its
# dependencies still live under MSV3/.arduino-build/ (gitignored); see the handoff doc to recreate.
REPO_ROOT = Path(__file__).resolve().parents[1]
MSV3_ROOT = REPO_ROOT / "MSV3"
DFU_CLIENT = MSV3_ROOT / ".arduino-build" / "nrf_dfu_py"
MODERN_BLEAK = MSV3_ROOT / ".arduino-build" / "bleak-modern"


def progress(percent: int) -> None:
    print(f"\rApplication transfer: {percent:3d}%", end="", flush=True)
    if percent == 100:
        print()


async def pump_windows_messages() -> None:
    """Keep WinRT callbacks moving while the recovery runs in a Windows STA."""
    from ctypes import wintypes

    message = wintypes.MSG()
    user32 = ctypes.windll.user32
    while True:
        while user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(message))
            user32.DispatchMessageW(ctypes.byref(message))
        await asyncio.sleep(0.005)


async def recover(args: argparse.Namespace) -> None:
    if args.modern_bleak:
        if not MODERN_BLEAK.is_dir():
            raise SystemExit(f"Modern Bleak environment not found: {MODERN_BLEAK}")
        sys.path.insert(0, str(MODERN_BLEAK))
    sys.path.insert(0, str(DFU_CLIENT))
    try:
        import dfu_lib  # type: ignore[import-not-found]
    except ModuleNotFoundError as error:
        raise SystemExit(
            f"Missing BLE DFU client at {DFU_CLIENT}. Recreate the pinned nrf_dfu_py "
            "checkout documented in MSV3/docs/MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md."
        ) from error

    # On current Windows 11 builds, BluetoothLEDevice creation can fail with generic
    # E_FAIL from a console MTA even though the advertisement watcher sees the probe.
    # Windows' consent-capable Bluetooth APIs work on an STA thread with an active UI
    # message pump. Establish that environment before the first WinRT BLE operation.
    if sys.platform == "win32":
        coinit_apartment_threaded = 0x2
        ctypes.windll.ole32.CoInitializeEx.restype = ctypes.c_long
        hresult = ctypes.windll.ole32.CoInitializeEx(None, coinit_apartment_threaded)
        if hresult not in (0, 1):  # S_OK or S_FALSE
            raise ctypes.WinError(hresult)
        from bleak.backends.winrt.util import allow_sta

        allow_sta()
        windows_message_pump = asyncio.create_task(pump_windows_messages())

    # The MSV3 address is static-random (its most-significant address bits are 11). On Windows,
    # BluetoothLEDevice.from_bluetooth_address_async() can reject it with generic E_FAIL unless
    # Bleak uses the overload that includes BluetoothAddressType.RANDOM. nrf_dfu_py does not expose
    # Bleak's WinRT option, so bind it here without modifying the ignored third-party checkout.
    if sys.platform == "win32" and args.address_type != "auto":
        from bleak.backends.winrt.client import BleakClientWinRT
        from winrt.windows.devices.bluetooth import BluetoothAddressType, BluetoothLEDevice
        from winrt.windows.devices.enumeration import DeviceInformation, DeviceInformationKind

        original_create_requester = BleakClientWinRT._create_requester

        async def create_requester_with_device_id(client, bluetooth_address):
            try:
                return await original_create_requester(client, bluetooth_address)
            except OSError as address_error:
                address_type = (
                    BluetoothAddressType.RANDOM
                    if args.address_type == "random"
                    else BluetoothAddressType.PUBLIC
                )
                selector = BluetoothLEDevice.get_device_selector_from_bluetooth_address_with_bluetooth_address_type(
                    bluetooth_address,
                    address_type,
                )
                devices = await DeviceInformation.find_all_async_with_kind_aqs_filter_and_additional_properties(
                    selector,
                    (),
                    DeviceInformationKind.ASSOCIATION_ENDPOINT,
                )
                for device_info in devices:
                    try:
                        requester = await BluetoothLEDevice.from_id_async(device_info.id)
                    except OSError:
                        continue
                    if requester is not None:
                        print("WinRT raw-address open failed; connected through discovered device ID.", flush=True)
                        return requester
                raise address_error

        BleakClientWinRT._create_requester = create_requester_with_device_id
        # use_cached_services=False for the same reason ble_ota_update.py needs it: a bootloader
        # reached after a buttonless jump occupies the address its application just had, with an
        # entirely different GATT table, and Windows caches services per address. Without this the
        # first connect dies with E_CANCELED and only the retry succeeds. Harmless when the
        # bootloader is on its own address+1, so it is unconditional rather than conditional.
        dfu_lib.BleakClient = functools.partial(
            dfu_lib.BleakClient,
            winrt={"address_type": args.address_type, "use_cached_services": False},
        )

    package = args.package.resolve()
    updater = dfu_lib.NordicLegacyDFU(
        str(package),
        prn=args.prn,
        packet_delay=args.delay,
        high_mtu=args.high_mtu,
        progress_callback=progress,
    )
    updater.parse_zip()
    if updater.upload_mode != 0x04:
        raise SystemExit("Recovery package must contain an application only; refusing bootloader DFU over BLE.")

    last_error: Exception | None = None
    for attempt in range(1, args.retry + 1):
        device = await dfu_lib.find_device_by_name_or_address(
            args.device,
            force_scan=True,
            service_uuid=dfu_lib.DFU_SERVICE_UUID,
        )
        print(
            f"Found recovery bootloader {device.name or 'unnamed'} at {device.address} "
            f"(attempt {attempt}/{args.retry})",
            flush=True,
        )
        await asyncio.sleep(args.connect_delay)
        try:
            await updater.perform_update(device, max_retries=1)
            return
        except Exception as error:  # The third-party client exposes backend-specific failures.
            last_error = error
            if attempt < args.retry:
                print("Connection failed; rescanning before retry...", flush=True)

    assert last_error is not None
    raise last_error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="Signed application-only DFU ZIP")
    parser.add_argument("--device", default="AdaDFU", help="BLE name or address (default: AdaDFU)")
    parser.add_argument("--prn", type=int, default=8, help="Packet receipt interval (default: 8)")
    parser.add_argument("--delay", type=float, default=0.4, help="Start/size delay in seconds (default: 0.4)")
    parser.add_argument("--retry", type=int, default=3, help="Connection attempts (default: 3)")
    parser.add_argument(
        "--connect-delay",
        type=float,
        default=2.0,
        help="Seconds between ending the scan and opening GATT (default: 2.0)",
    )
    parser.add_argument("--high-mtu", action="store_true", help="Negotiate a larger BLE MTU")
    parser.add_argument(
        "--modern-bleak",
        action="store_true",
        help="Use the isolated .arduino-build/bleak-modern backend instead of the pinned default",
    )
    parser.add_argument(
        "--address-type",
        choices=("random", "public", "auto"),
        default="random",
        help="Windows BLE address type (default: random, as used by MSV3)",
    )
    args = parser.parse_args()

    if not args.package.is_file():
        parser.error(f"package not found: {args.package}")
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    asyncio.run(recover(args))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
