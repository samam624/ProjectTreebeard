#!/usr/bin/env python3
"""Line console for the MSV4 bring-up firmware over the Nordic UART Service.

MSV4 has no USB, so this replaces the serial monitor used for MSV3 bring-up. It connects to an
MSV4_Probe_Bringup_BLE board, prints everything the board sends, and sends each line you type.

  # interactive; picks the only MSV4-BU-* board in range, or pass --device
  MSV3/.arduino-build/python-portable/runtime/python.exe MSV4/scripts/msv4_ble_console.py

  # scripted: run commands in order, wait for each prompt, then exit
  ... msv4_ble_console.py --device MSV4-BU-1A2B --cmd info --cmd vbat --cmd "iq 16"

Any NUS app (nRF Toolbox, Bluefruit Connect) works too; this script only adds scripting and a
Windows BLE setup that is known to work with this project's static-random addresses.
"""

import argparse
import asyncio
import ctypes
import sys

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # central -> board (write)
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # board -> central (notify)
NAME_PREFIX = "MSV4-BU-"
PROMPT = "> "


async def pump_windows_messages() -> None:
    """Keep WinRT callbacks moving on a Windows STA (same workaround as scripts/ble_dfu_recover.py)."""
    from ctypes import wintypes

    message = wintypes.MSG()
    user32 = ctypes.windll.user32
    while True:
        while user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(message))
            user32.DispatchMessageW(ctypes.byref(message))
        await asyncio.sleep(0.005)


def windows_sta_setup() -> None:
    # Windows 11 can fail BluetoothLEDevice creation with E_FAIL from a console MTA even though the
    # advertisement is visible; an STA with a message pump avoids it.
    coinit_apartment_threaded = 0x2
    ctypes.windll.ole32.CoInitializeEx.restype = ctypes.c_long
    hresult = ctypes.windll.ole32.CoInitializeEx(None, coinit_apartment_threaded)
    if hresult not in (0, 1):
        raise ctypes.WinError(hresult)
    from bleak.backends.winrt.util import allow_sta

    allow_sta()


async def find_board(device: str | None, timeout: float):
    from bleak import BleakScanner

    if device and ":" in device:
        found = await BleakScanner.find_device_by_address(device, timeout=timeout)
        if found is None:
            raise SystemExit(f"No advertisement from {device} within {timeout:.0f}s.")
        return found

    seen = await BleakScanner.discover(timeout=timeout, service_uuids=[NUS_SERVICE])
    boards = [d for d in seen if d.name and d.name.startswith(NAME_PREFIX)]
    if device:
        boards = [d for d in boards if d.name == device]
    if not boards:
        want = device or f"{NAME_PREFIX}*"
        raise SystemExit(f"No {want} board advertising NUS within {timeout:.0f}s.")
    if len(boards) > 1:
        names = ", ".join(f"{d.name} ({d.address})" for d in boards)
        raise SystemExit(f"Several MSV4 boards in range: {names}. Pass --device.")
    return boards[0]


async def run(args: argparse.Namespace) -> int:
    if sys.platform == "win32":
        windows_sta_setup()
        pump = asyncio.create_task(pump_windows_messages())  # noqa: F841 (kept alive for the session)

    from bleak import BleakClient

    board = await find_board(args.device, args.scan_timeout)
    print(f"Connecting to {board.name} at {board.address} ...", flush=True)

    buffer = ""
    prompt_seen = asyncio.Event()

    def on_notify(_handle, data: bytearray) -> None:
        nonlocal buffer
        buffer += data.decode("utf-8", errors="replace")
        while "\n" in buffer:
            line, buffer = buffer.split("\n", 1)
            if line.strip() == PROMPT.strip():
                prompt_seen.set()
                if not args.cmd:
                    print(PROMPT, end="", flush=True)
            else:
                print(line, flush=True)

    kwargs = {}
    if sys.platform == "win32":
        kwargs["winrt"] = {"address_type": "random", "use_cached_services": False}
    async with BleakClient(board, timeout=args.connect_timeout, **kwargs) as client:
        await client.start_notify(NUS_TX, on_notify)
        rx = client.services.get_characteristic(NUS_RX)
        chunk = max(20, (rx.max_write_without_response_size if rx else 20))

        async def send(line: str) -> None:
            data = (line + "\n").encode("utf-8")
            for i in range(0, len(data), chunk):
                await client.write_gatt_char(NUS_RX, data[i:i + chunk], response=False)

        print(f"Connected. MTU payload {chunk} bytes. Type 'help'; Ctrl+C to quit.", flush=True)

        if args.cmd:
            for line in args.cmd:
                prompt_seen.clear()
                print(f"{PROMPT}{line}", flush=True)
                await send(line)
                try:
                    await asyncio.wait_for(prompt_seen.wait(), timeout=args.cmd_timeout)
                except asyncio.TimeoutError:
                    print(f"(no prompt within {args.cmd_timeout:.0f}s for '{line}')", flush=True)
                    return 1
            return 0

        loop = asyncio.get_running_loop()
        while client.is_connected:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            line = line.rstrip("\r\n")
            if line:
                await send(line)
        if not client.is_connected:
            print("Disconnected by the board.", flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", help=f"Board name ({NAME_PREFIX}XXXX) or BLE address")
    parser.add_argument("--cmd", action="append", help="Command to run non-interactively (repeatable)")
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--cmd-timeout", type=float, default=30.0,
                        help="Seconds to wait for each scripted command's prompt (sweeps take a while)")
    args = parser.parse_args()
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
