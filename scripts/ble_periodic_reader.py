#!/usr/bin/env python
"""Scan for a BLE nRF52840 device and read data periodically.

Examples:
  python scripts/ble_periodic_reader.py --scan-only
  python scripts/ble_periodic_reader.py --adv-monitor --exact-name MSV2-MOISTURE-001
  python scripts/ble_periodic_reader.py --name MySensor --dump-services
  python scripts/ble_periodic_reader.py --exact-name MSV2-MOISTURE-001 --uart
  python scripts/ble_periodic_reader.py --exact-name MSV2-MOISTURE-001 --uart --watch
  python scripts/ble_periodic_reader.py --index 5 --uart
  python scripts/ble_periodic_reader.py --index 5 --uart --send "hello from pc"
  python scripts/ble_periodic_reader.py --address AA:BB:CC:DD:EE:FF --read 00002a19-0000-1000-8000-00805f9b34fb --interval 30

Install dependency:
  python -m pip install -r requirements.txt
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import struct
from datetime import datetime
from typing import Iterable

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData


NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # PC writes here, nRF receives.
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # nRF notifies here, PC receives.
MSV2_COMPANY_ID = 0xFFFF
MSV2_MAGIC = b"MSV2"
MSV3_MAGIC = b"MSV3"  # MSV3_Probe_Bringup_Test.ino's blecycle/blestart/bench -- distinct magic
                       # so it can never be mistaken for real MSV2 plant data (see that sketch's
                       # BLE section comment).
# Bench packet ABI. The authoritative layout is the comment above bleStartAdv() in
# MSV3_Probe_Bringup_Test.ino; the other two decoders are that sketch itself and
# Treebeard_ESP32_Display_Runtime.ino's parseBenchAdvertisement(). Change one, change all three.
#
# NOTE ON OFFSETS: bleak strips the 2-byte company ID before handing over manufacturer_data, so
# every index here is the firmware's mfg[] index MINUS 2. mfg[16] (flags) is payload[14].
MSV3_BENCH_FORMAT_VERSION_1 = 1
MSV3_BENCH_FLAG_CHARGING = 1 << 0
MSV3_BENCH_FLAG_BOARD_TEMP_OK = 1 << 2
MSV3_BENCH_FLAG_OTA_POWER_OK = 1 << 4
MSV3_BENCH_FLAG_READING_INVALID = 1 << 5
MSV3_BENCH_FLAG_LIVE_MODE = 1 << 7
BATTERY_LEVEL_UUID = "00002a19-0000-1000-8000-00805f9b34fb"
DIS_MODEL_UUID = "00002a24-0000-1000-8000-00805f9b34fb"
DIS_MANUFACTURER_UUID = "00002a29-0000-1000-8000-00805f9b34fb"


def now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def bytes_preview(value: bytes) -> str:
    hex_value = value.hex(" ")
    with contextlib.suppress(UnicodeDecodeError):
        text = value.decode("utf-8").strip("\x00\r\n ")
        if text and all(ch.isprintable() for ch in text):
            return f"{hex_value}  text={text!r}"
    if len(value) == 1:
        return f"{hex_value}  int={value[0]}"
    return hex_value


def matches_device(
    device: BLEDevice,
    adv: AdvertisementData,
    *,
    name: str | None,
    exact_name: str | None,
    address: str | None,
) -> bool:
    if address and device.address.lower() == address.lower():
        return True

    advertised_names = [device.name or "", adv.local_name or ""]

    if exact_name:
        return exact_name in advertised_names

    if name:
        target = name.lower()
        return any(target in candidate.lower() for candidate in advertised_names)

    return False


def format_adv(device: BLEDevice, adv: AdvertisementData) -> str:
    advertised_name = adv.local_name or device.name or "(no name)"
    service_uuids = ", ".join(adv.service_uuids) if adv.service_uuids else "-"
    mfg = "-"
    if adv.manufacturer_data:
        mfg = ", ".join(
            f"0x{company_id:04x}:{payload.hex(' ')}"
            for company_id, payload in adv.manufacturer_data.items()
        )
    return (
        f"{advertised_name} | {device.address} | RSSI={adv.rssi} | "
        f"services={service_uuids} | manufacturer={mfg}"
    )


def parse_msv2_manufacturer_data(adv: AdvertisementData) -> tuple[int, int, int] | None:
    payload = adv.manufacturer_data.get(MSV2_COMPANY_ID)
    if not payload or len(payload) < 10:
        return None

    if not payload.startswith(MSV2_MAGIC):
        return None

    sensor_id = payload[4]
    wake_low24 = payload[5] | (payload[6] << 8) | (payload[7] << 16)
    moisture = struct.unpack_from("<H", payload, 8)[0]
    return sensor_id, wake_low24, moisture


def parse_msv3_manufacturer_data(adv: AdvertisementData) -> dict | None:
    payload = adv.manufacturer_data.get(MSV2_COMPANY_ID)
    if not payload or len(payload) < 15:
        return None

    if not payload.startswith(MSV3_MAGIC):
        return None

    flags = payload[14]
    parsed = {
        "sensor_id": payload[4],
        "wake": payload[5] | (payload[6] << 8) | (payload[7] << 16),
        "data_ch0": struct.unpack_from("<I", payload, 8)[0],
        "battery_mv": struct.unpack_from("<H", payload, 12)[0],
        # Masked, not truthiness-tested. The flags byte has carried more than charging since the
        # OTA power gate was added, so `payload[14] != 0` reported charging for any probe whose
        # gate was merely open.
        "charging": bool(flags & MSV3_BENCH_FLAG_CHARGING),
        "ota_power_ok": bool(flags & MSV3_BENCH_FLAG_OTA_POWER_OK),
        "reading_valid": not (flags & MSV3_BENCH_FLAG_READING_INVALID),
        "live_mode": bool(flags & MSV3_BENCH_FLAG_LIVE_MODE),
        "format_version": 0,
        "board_temp_c": None,
        "mux_code": None,
        "fdc_status": None,
    }

    # Everything below exists only in the widened (23-byte) bench packet. Gate the v1-only fields
    # on the VERSION byte, not on length alone: length says the bytes are present, version says
    # what they mean. CLAUDE.md records four readings mislabelled by decoding a reused byte
    # against the wrong build -- this is that rule applied here.
    if len(payload) >= 21:
        if flags & MSV3_BENCH_FLAG_BOARD_TEMP_OK:
            temp_q4 = struct.unpack_from("<h", payload, 15)[0]
            if -160 <= temp_q4 <= 340:
                parsed["board_temp_c"] = temp_q4 / 4.0
        if payload[17] >= MSV3_BENCH_FORMAT_VERSION_1:
            parsed["format_version"] = payload[17]
            parsed["mux_code"] = payload[18] & 0x03
            parsed["fdc_status"] = struct.unpack_from("<H", payload, 19)[0]

    return parsed


async def monitor_advertisements(
    *,
    exact_name: str | None,
    name: str | None,
    address: str | None,
) -> None:
    seen: dict[tuple[str, str, int], tuple] = {}
    last_wake: dict[tuple[str, str, int], int] = {}

    def handle_adv(device: BLEDevice, adv: AdvertisementData) -> None:
        if not matches_device(device, adv, name=name, exact_name=exact_name, address=address):
            return

        advertised_name = adv.local_name or device.name or "(no name)"

        parsed = parse_msv2_manufacturer_data(adv)
        if parsed is not None:
            sensor_id, wake_count, moisture = parsed
            key = (device.address, "v2", sensor_id)
            if seen.get(key) == parsed:
                return
            seen[key] = parsed
            previous_wake = last_wake.get(key)
            last_wake[key] = wake_count
            gap = ""
            if previous_wake is not None:
                missed = (wake_count - previous_wake - 1) & 0xFFFFFF
                if missed:
                    gap = f" missed={missed}"
            print(
                f"[{now()}] {advertised_name} {device.address} "
                f"RSSI={adv.rssi} sensor={sensor_id} wake={wake_count} moisture={moisture}{gap}"
            )
            return

        parsed3 = parse_msv3_manufacturer_data(adv)
        if parsed3 is not None:
            sensor_id = parsed3["sensor_id"]
            wake_count = parsed3["wake"]
            key = (device.address, "v3", sensor_id)
            fingerprint = tuple(sorted(parsed3.items(), key=lambda kv: kv[0]))
            if seen.get(key) == fingerprint:
                return
            seen[key] = fingerprint
            previous_wake = last_wake.get(key)
            last_wake[key] = wake_count
            gap = ""
            if previous_wake is not None:
                missed = (wake_count - previous_wake - 1) & 0xFFFFFF
                if missed:
                    gap = f" missed={missed}"
            charge_note = " (charging)" if parsed3["charging"] else ""
            live_note = " LIVE" if parsed3["live_mode"] else ""
            # A placeholder advertisement (an OTA discovery window, or a failed FDC read) carries
            # dataCh0=0. Printing that as a count would be a fabricated reading.
            count_note = (
                f"dataCh0={parsed3['data_ch0']}"
                if parsed3["reading_valid"]
                else "dataCh0=-- (no reading)"
            )
            extras = ""
            if parsed3["board_temp_c"] is not None:
                extras += f" temp={parsed3['board_temp_c']:.1f}C"
            if parsed3["mux_code"] is not None:
                extras += f" mux={parsed3['mux_code']}"
            if parsed3["fdc_status"] is not None:
                extras += f" status=0x{parsed3['fdc_status']:04X}"
            print(
                f"[{now()}] {advertised_name} {device.address} "
                f"RSSI={adv.rssi} sensor={sensor_id} wake={wake_count}{live_note} "
                f"{count_note} battMv={parsed3['battery_mv']}{extras}{charge_note}{gap}"
            )

    print(f"[{now()}] Monitoring BLE advertisements. Press Ctrl+C to stop.")
    async with BleakScanner(detection_callback=handle_adv):
        while True:
            await asyncio.sleep(1)


async def scan(duration: float) -> dict[str, tuple[BLEDevice, AdvertisementData]]:
    print(f"[{now()}] Scanning for {duration:g}s...")
    found = await BleakScanner.discover(
        timeout=duration,
        return_adv=True,
    )

    if not found:
        print("No BLE devices found. Make sure Bluetooth is on and the sensor is advertising.")
        return {}

    print(f"\nFound {len(found)} BLE device(s):")
    for index, (_, (device, adv)) in enumerate(found.items(), start=1):
        print(f"  {index:02d}. {format_adv(device, adv)}")
    print()
    return found


def pick_device(
    found: dict[str, tuple[BLEDevice, AdvertisementData]],
    *,
    name: str | None,
    exact_name: str | None,
    address: str | None,
    index: int | None,
) -> BLEDevice | None:
    if index is not None:
        devices = list(found.values())
        if 1 <= index <= len(devices):
            return devices[index - 1][0]
        return None

    if not name and not exact_name and not address:
        return None

    for device, adv in found.values():
        if matches_device(device, adv, name=name, exact_name=exact_name, address=address):
            return device

    return None


async def dump_services(client: BleakClient) -> None:
    print("GATT services:")
    for service in client.services:
        print(f"  Service {service.uuid} ({service.description})")
        for char in service.characteristics:
            props = ",".join(char.properties)
            print(f"    Char {char.uuid} ({char.description}) props=[{props}]")
            for descriptor in char.descriptors:
                print(f"      Desc {descriptor.uuid} ({descriptor.description})")
    print()


async def read_characteristic(
    client: BleakClient,
    uuid: str,
    *,
    quiet_errors: bool = False,
) -> None:
    try:
        value = await client.read_gatt_char(uuid)
    except Exception as exc:  # BLE stacks return platform-specific exception types.
        if not quiet_errors:
            print(f"[{now()}] {uuid}: read failed: {exc}")
        return

    print(f"[{now()}] {uuid}: {bytes_preview(bytes(value))}")


async def read_common_info(client: BleakClient) -> None:
    print("Common device info reads:")
    for label, uuid in (
        ("battery level", BATTERY_LEVEL_UUID),
        ("model number", DIS_MODEL_UUID),
        ("manufacturer", DIS_MANUFACTURER_UUID),
    ):
        try:
            value = bytes(await client.read_gatt_char(uuid))
        except Exception:
            print(f"  {label}: not available")
        else:
            print(f"  {label}: {bytes_preview(value)}")
    print()


async def poll_reads(
    client: BleakClient,
    uuids: Iterable[str],
    *,
    interval: float,
    count: int | None,
) -> None:
    reads = list(uuids)
    if not reads:
        print("No --read UUIDs supplied, so there is nothing to poll.")
        return

    iteration = 0
    while count is None or iteration < count:
        iteration += 1
        print(f"Poll {iteration}" if count != 1 else "Read")
        for uuid in reads:
            await read_characteristic(client, uuid)
        if count is not None and iteration >= count:
            break
        await asyncio.sleep(interval)


async def uart_mode(
    client: BleakClient,
    *,
    listen_seconds: float,
    send_values: Iterable[str],
    disconnected: asyncio.Event | None = None,
) -> None:
    def handle_rx(_: int, data: bytearray) -> None:
        print(f"[{now()}] nRF -> PC: {bytes_preview(bytes(data))}")

    await client.start_notify(NUS_TX_UUID, handle_rx)
    print(f"[{now()}] BLE UART notifications enabled.")
    print("With your current Arduino sketch, type into Serial Monitor to send nRF -> PC.")

    ready_payload = b"ready\n"
    await client.write_gatt_char(NUS_RX_UUID, ready_payload, response=False)
    print(f"[{now()}] PC -> nRF: 'ready'")

    for value in send_values:
        payload = value.encode("utf-8")
        await client.write_gatt_char(NUS_RX_UUID, payload, response=False)
        print(f"[{now()}] PC -> nRF: {value!r}")

    try:
        if disconnected is not None and listen_seconds <= 0:
            await disconnected.wait()
        elif disconnected is not None:
            with contextlib.suppress(asyncio.TimeoutError):
                await asyncio.wait_for(disconnected.wait(), timeout=listen_seconds)
        elif listen_seconds <= 0:
            while True:
                await asyncio.sleep(1)
        else:
            await asyncio.sleep(listen_seconds)
    finally:
        with contextlib.suppress(Exception):
            await client.stop_notify(NUS_TX_UUID)


async def connect_and_run(device: BLEDevice, args: argparse.Namespace) -> int:
    disconnected = asyncio.Event()
    loop = asyncio.get_running_loop()

    def on_disconnect(_: BleakClient) -> None:
        loop.call_soon_threadsafe(disconnected.set)

    print(f"[{now()}] Connecting to {device.name or '(no name)'} at {device.address}...")
    async with BleakClient(device, disconnected_callback=on_disconnect) as client:
        if not client.is_connected:
            print("Connection failed.")
            return 3

        print(f"[{now()}] Connected.\n")

        if args.dump_services:
            await dump_services(client)

        if args.common_info:
            await read_common_info(client)

        if args.uart:
            await uart_mode(
                client,
                listen_seconds=args.listen_seconds,
                send_values=args.send,
                disconnected=disconnected,
            )
        elif args.read:
            await poll_reads(client, args.read, interval=args.interval, count=args.count)
        elif not args.dump_services and not args.common_info:
            await dump_services(client)
            await read_common_info(client)

    if disconnected.is_set():
        print(f"[{now()}] Disconnected.")
    return 0


async def main() -> int:
    parser = argparse.ArgumentParser(
        description="Scan for a BLE nRF52840 sensor and read GATT data periodically."
    )
    parser.add_argument("--scan-seconds", type=float, default=8.0, help="BLE scan duration.")
    parser.add_argument("--scan-only", action="store_true", help="Only list nearby BLE devices.")
    parser.add_argument(
        "--adv-monitor",
        action="store_true",
        help="Continuously monitor MSV2 sensor advertising packets without connecting.",
    )
    parser.add_argument("--index", type=int, help="Connect to this 1-based index from the scan list.")
    parser.add_argument("--name", help="Connect to the first device whose advertised name contains this text.")
    parser.add_argument("--exact-name", help="Connect only to a device with this exact advertised name.")
    parser.add_argument("--address", help="Connect to this BLE address exactly.")
    parser.add_argument("--dump-services", action="store_true", help="Print GATT services and characteristics.")
    parser.add_argument(
        "--common-info",
        action="store_true",
        help="Try reading standard battery/model/manufacturer characteristics.",
    )
    parser.add_argument(
        "--read",
        action="append",
        default=[],
        metavar="UUID",
        help="Characteristic UUID to read. Repeat for multiple characteristics.",
    )
    parser.add_argument("--interval", type=float, default=30.0, help="Seconds between reads.")
    parser.add_argument(
        "--uart",
        action="store_true",
        help="Use Adafruit/Nordic UART mode: receive nRF notifications and optionally send text.",
    )
    parser.add_argument(
        "--send",
        action="append",
        default=[],
        metavar="TEXT",
        help="Text to send to the nRF over BLE UART. Repeat to send multiple messages.",
    )
    parser.add_argument(
        "--listen-seconds",
        type=float,
        default=60.0,
        help="Seconds to listen in --uart mode. Use 0 to listen until Ctrl+C.",
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Keep scanning/reconnecting after the board sleeps or disconnects.",
    )
    parser.add_argument("--retry-delay", type=float, default=2.0, help="Seconds between watch-mode scans.")
    parser.add_argument(
        "--count",
        type=int,
        default=None,
        help="Number of polling cycles. Omit to run until Ctrl+C.",
    )
    args = parser.parse_args()

    if args.adv_monitor:
        await monitor_advertisements(
            exact_name=args.exact_name,
            name=args.name,
            address=args.address,
        )
        return 0

    while True:
        found = await scan(args.scan_seconds)
        if args.scan_only:
            return 0

        device = pick_device(
            found,
            name=args.name,
            exact_name=args.exact_name,
            address=args.address,
            index=args.index,
        )
        if not device:
            print("No matching device selected.")
            print("Run with --exact-name SENSOR_NAME, --name SENSOR_NAME, or --address BLE_ADDRESS from the scan list above.")
            if not args.watch:
                return 2
            await asyncio.sleep(args.retry_delay)
            continue

        try:
            result = await connect_and_run(device, args)
        except Exception as exc:
            print(f"[{now()}] Connection/session failed: {exc}")
            result = 3

        if not args.watch:
            return result

        print(f"[{now()}] Watch mode: scanning again in {args.retry_delay:g}s.\n")
        await asyncio.sleep(args.retry_delay)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except KeyboardInterrupt:
        print("\nStopped.")
