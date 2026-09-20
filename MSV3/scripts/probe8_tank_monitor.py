#!/usr/bin/env python3
"""Passive Probe 8 tank monitor for the reversible C17 experiment."""

import argparse
import asyncio
import math
from datetime import datetime

from bleak import BleakScanner


COMPANY_ID = 0xFFFF
MAGIC = b"MSV2"
SENSOR_ID = 8
F_INT_HZ = 44.27e6
FDC_SCALE = 1 << 28
L_H = 6.8e-6


def tank_values(count: int):
    if count <= 0 or count >= 0x0FFFFFFF:
        return None, None
    frequency_hz = count * F_INT_HZ / FDC_SCALE
    capacitance_pf = 1e12 / (((2.0 * math.pi * frequency_hz) ** 2) * L_H)
    return frequency_hz / 1e6, capacitance_pf


async def main(seconds: float):
    seen_wakes = set()

    def callback(device, advertisement):
        payload = (advertisement.manufacturer_data or {}).get(COMPANY_ID)
        if not payload or len(payload) < 21 or payload[0:4] != MAGIC or payload[4] != SENSOR_ID:
            return

        wake = int.from_bytes(payload[5:8], "little")
        if wake in seen_wakes:
            return
        seen_wakes.add(wake)

        connected = int.from_bytes(payload[8:12], "little")
        battery_mv = int.from_bytes(payload[12:14], "little")
        flags = payload[14]
        temperature_c = int.from_bytes(payload[15:17], "little", signed=True) / 4.0
        bare = int.from_bytes(payload[17:21], "little") if flags & 0x08 else None
        connected_mhz, connected_pf = tank_values(connected)
        bare_mhz, bare_pf = tank_values(bare) if bare is not None else (None, None)

        print(
            f"[{datetime.now():%H:%M:%S}] wake={wake} phase={(flags >> 1) & 1} "
            f"temp={temperature_c:.2f}C batt={battery_mv}mV flags=0x{flags:02x}"
        )
        print(
            f"  connected count={connected:,}  f={connected_mhz:.3f}MHz  "
            f"C={connected_pf:.1f}pF"
            if connected_mhz is not None
            else f"  connected count={connected:,}  INVALID/SATURATED"
        )
        print(
            f"  bare      count={bare:,}  f={bare_mhz:.3f}MHz  C={bare_pf:.1f}pF"
            if bare_mhz is not None
            else "  bare      invalid/not reported"
        )
        if connected_mhz is not None and connected_mhz > 10.0:
            print("  WARNING: connected tank exceeds the FDC2214 10MHz limit")
        if bare_mhz is not None and bare_mhz > 10.0:
            print("  WARNING: bare tank exceeds the FDC2214 10MHz limit")

    print(f"Listening passively for Probe 8 for {seconds:.0f}s...")
    async with BleakScanner(detection_callback=callback):
        await asyncio.sleep(seconds)
    if not seen_wakes:
        raise SystemExit("Probe 8 was not seen; repeat for a longer window before changing C17.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=90.0)
    args = parser.parse_args()
    asyncio.run(main(args.seconds))
