#!/usr/bin/env python3
"""Listen for MSV4 production advertisements and decode them.

The packet layout is an ABI with startAdv() in
MSV4/arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino. Change one side, change the
other, and run --self-test before trusting this against hardware.

Examples:
  python MSV4/scripts/msv4_adv_scan.py --self-test
  python MSV4/scripts/msv4_adv_scan.py --seconds 150 --sensor 10 --require-progress
  python MSV4/scripts/msv4_adv_scan.py --seconds 0 --csv data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_adv_scan.py --read-build F6:26:7D:76:AD:95

The probe advertises for 5 s once a minute, so anything under about 70 s can miss it entirely.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import struct
import sys
from datetime import datetime
from pathlib import Path

COMPANY_ID = 0xFFFF
MAGIC = b"MSV2"
# Bleak hands over manufacturer data WITHOUT the two company-ID bytes, so every offset here is the
# firmware's mfg[] index minus 2.
PAYLOAD_LEN = 21
# Application version 262150 added three bytes: die temperature and a second flags byte.
EXTENDED_LEN = 24

FLAG_FRESH = 1 << 1
FLAG_TEMP_OK = 1 << 2
FLAG_OTA_OK = 1 << 4
FLAG_INVALID = 1 << 5
FLAG_MSV4 = 1 << 7
FLAG2_DIE_TEMP_OK = 1 << 0
# Layout B (application version 262152): the fit residual moved into what was Rsoil's always-zero
# top byte, and the two bytes that held Cp and the residual now hold Cd in 0.01 pF. Cp left the
# packet; it is in the DIS hardware revision string (--read-build).
FLAG2_LAYOUT_B = 1 << 1
# Application version 262154: most wakes send a short NON-CONNECTABLE burst. A packet with this
# bit set cannot be connected to; wait for one without it (every tenth wake, and the first three
# after a boot).
FLAG2_SHORT_BURST = 1 << 2

RSOIL_DECIOHM_MAX = 4_000_000
CP_DEFAULT_DECI_PF = 120
# Soil conductivity rises about 2 %/C. The probe sends Rsoil uncorrected; this is the same
# correction the bring-up console's `moisture` command applies.
EC_TEMPCO_PER_C = 0.02

CSV_FIELDS = ["time", "sensor", "counter", "rssi", "battery_mv", "tip_c", "moisture_valid",
              "fresh", "rsoil_ohm", "rsoil_open", "rsoil25_ohm", "cm_pf", "cp_pf", "cp_default",
              "fit_rms", "ota_ok", "die_c", "cd_pf", "connectable"]


def decode(payload: bytes) -> dict | None:
    """Decode one manufacturer payload (company ID already stripped). None if it is not MSV4."""
    if len(payload) < PAYLOAD_LEN or payload[0:4] != MAGIC:
        return None
    flags = payload[14]
    if not flags & FLAG_MSV4:
        return None
    sensor = payload[4]
    counter = payload[5] | (payload[6] << 8) | (payload[7] << 16)
    rsoil_word, battery_mv = struct.unpack_from("<IH", payload, 8)
    rsoil_deci = rsoil_word & 0xFFFFFF
    temp_q4, cm_deci = struct.unpack_from("<hH", payload, 15)
    flags2 = payload[23] if len(payload) >= EXTENDED_LEN else 0
    if flags2 & FLAG2_LAYOUT_B:
        rms_e4, cp_deci = rsoil_word >> 24, None
        cd_centi = struct.unpack_from("<H", payload, 19)[0]
    else:
        cp_deci, rms_e4, cd_centi = payload[19], payload[20], 0

    die_c = None
    if flags2 & FLAG2_DIE_TEMP_OK:
        die_c = struct.unpack_from("<h", payload, 21)[0] / 4.0

    valid = not flags & FLAG_INVALID
    temp_ok = bool(flags & FLAG_TEMP_OK)
    tip_c = temp_q4 / 4.0 if temp_ok else None
    rsoil = rsoil_deci / 10.0 if valid else None
    rsoil25 = None
    if rsoil is not None and tip_c is not None and rsoil_deci < RSOIL_DECIOHM_MAX:
        rsoil25 = rsoil * (1.0 + EC_TEMPCO_PER_C * (tip_c - 25.0))
    return {
        "sensor": sensor,
        "counter": counter,
        "battery_mv": battery_mv or None,
        "tip_c": tip_c,
        "moisture_valid": valid,
        "fresh": bool(flags & FLAG_FRESH),
        "rsoil_ohm": rsoil,
        "rsoil_open": valid and rsoil_deci >= RSOIL_DECIOHM_MAX,
        "rsoil25_ohm": rsoil25,
        "cm_pf": cm_deci / 10.0 if valid else None,
        "cd_pf": cd_centi / 100.0 if valid and cd_centi else None,
        "cp_pf": cp_deci / 10.0 if cp_deci is not None else None,
        "cp_default": cp_deci == CP_DEFAULT_DECI_PF,
        "fit_rms": rms_e4 / 1e4 if valid else None,
        "ota_ok": bool(flags & FLAG_OTA_OK),
        "die_c": die_c,
        "connectable": not flags2 & FLAG2_SHORT_BURST,
    }


def describe(row: dict) -> str:
    battery = f"{row['battery_mv']} mV" if row["battery_mv"] else "battery ?"
    tip = f"tip {row['tip_c']:.2f} C" if row["tip_c"] is not None else "tip ?"
    if row["die_c"] is not None:
        tip += f" die {row['die_c']:.2f} C"
    cd = f"Cd {row['cd_pf']:.2f} pF " if row["cd_pf"] is not None else ""
    if not row["moisture_valid"]:
        moisture = "moisture INVALID"
    elif row["rsoil_open"]:
        moisture = f"OPEN, nothing conducting | {cd}rms {row['fit_rms']:.4f}"
    else:
        r25 = f" ({row['rsoil25_ohm']:.1f} @25C)" if row["rsoil25_ohm"] is not None else ""
        moisture = (f"Rsoil {row['rsoil_ohm']:.1f} ohm{r25} Cm {row['cm_pf']:.1f} pF "
                    f"{cd}rms {row['fit_rms']:.4f}")
    cp = ""
    if row["cp_pf"] is not None:
        cp = f" | Cp {row['cp_pf']:.1f} pF" + (" (DEFAULT)" if row["cp_default"] else "")
    marks = ("fresh" if row["fresh"] else "repeat") + ("" if row["ota_ok"] else ", OTA closed")
    marks += ", connectable" if row["connectable"] else ", short burst"
    return f"sensor {row['sensor']} #{row['counter']} {battery} {tip} | {moisture}{cp} | {marks}"


# --------------------------------------------------------------------------------- self-test --
def _encode(sensor, counter, rsoil_deci, battery_mv, flags, temp_q4, cm_deci, cp_deci, rms_e4,
            die_q4=None, flags2=None, cd_centi=None):
    """Written from the firmware's layout comment, independently of decode().

    Without die_q4/flags2 this is the 23-byte packet of application version 262148. Passing
    cd_centi makes it layout B (262152): cp_deci is then not on the wire at all.
    """
    layout_b = cd_centi is not None
    if layout_b:
        flags2 = (flags2 or 0) | FLAG2_LAYOUT_B
    mfg = bytearray(23 if flags2 is None else 26)
    if flags2 is not None:
        mfg[23:25] = (die_q4 or 0).to_bytes(2, "little", signed=True)
        mfg[25] = flags2
    mfg[0:2] = COMPANY_ID.to_bytes(2, "little")
    mfg[2:6] = MAGIC
    mfg[6] = sensor
    mfg[7:10] = counter.to_bytes(3, "little")
    # One 32-bit word on the wire: an MSV3 packet uses all of it as an FDC count; an MSV4 packet
    # uses the low 24 bits for Rsoil and, in layout B, the top byte for the fit residual.
    mfg[10:14] = (rsoil_deci | ((rms_e4 << 24) if layout_b else 0)).to_bytes(4, "little")
    mfg[14:16] = battery_mv.to_bytes(2, "little")
    mfg[16] = flags
    mfg[17:19] = temp_q4.to_bytes(2, "little", signed=True)
    mfg[19:21] = cm_deci.to_bytes(2, "little")
    if layout_b:
        mfg[21:23] = cd_centi.to_bytes(2, "little")
    else:
        mfg[21] = cp_deci
        mfg[22] = rms_e4
    return bytes(mfg[2:])  # what Bleak delivers


def self_test() -> int:
    failures = []

    def check(name, got, want):
        if got != want:
            failures.append(f"{name}: got {got!r}, want {want!r}")

    # A wet reading: 355.0 ohm at 26.75 C, Cm 671.0 pF, Cp 8.8 pF, rms 0.0015, 3814 mV.
    row = decode(_encode(10, 0x012345, 3550, 3814, FLAG_MSV4 | FLAG_FRESH | FLAG_TEMP_OK | FLAG_OTA_OK,
                         107, 6710, 88, 15))
    check("sensor", row["sensor"], 10)
    check("counter", row["counter"], 0x012345)
    check("battery", row["battery_mv"], 3814)
    check("tip", row["tip_c"], 26.75)
    check("rsoil", row["rsoil_ohm"], 355.0)
    check("rsoil25", round(row["rsoil25_ohm"], 3), round(355.0 * (1 + 0.02 * 1.75), 3))
    check("cm", row["cm_pf"], 671.0)
    check("cp", row["cp_pf"], 8.8)
    check("cp_default", row["cp_default"], False)
    check("rms", row["fit_rms"], 0.0015)
    check("fresh", row["fresh"], True)
    check("ota", row["ota_ok"], True)
    check("valid", row["moisture_valid"], True)
    check("open", row["rsoil_open"], False)
    check("v262148.die", row["die_c"], None)

    # The 26-byte packet: die temperature present, negative, and flagged absent.
    common = (10, 0x012345, 3550, 3814, FLAG_MSV4 | FLAG_TEMP_OK, 107, 6710, 88, 15)
    check("die", decode(_encode(*common, die_q4=126, flags2=FLAG2_DIE_TEMP_OK))["die_c"], 31.5)
    check("die.negative", decode(_encode(*common, die_q4=-22, flags2=FLAG2_DIE_TEMP_OK))["die_c"], -5.5)
    check("die.absent", decode(_encode(*common, die_q4=126, flags2=0))["die_c"], None)
    check("die.keeps.tip", decode(_encode(*common, die_q4=126, flags2=FLAG2_DIE_TEMP_OK))["tip_c"], 26.75)

    # Layout B, board 1 in air as the console measured it: open, Cd 13.01 pF, residual 0.0229.
    air = decode(_encode(10, 1, RSOIL_DECIOHM_MAX, 3816, FLAG_MSV4 | FLAG_TEMP_OK, 93, 0, None, 229,
                         die_q4=94, flags2=FLAG2_DIE_TEMP_OK, cd_centi=1301))
    check("b.cd", air["cd_pf"], 13.01)
    check("b.rms", air["fit_rms"], 0.0229)
    check("b.open", air["rsoil_open"], True)
    check("b.rsoil", air["rsoil_ohm"], 400000.0)
    check("b.cp.gone", air["cp_pf"], None)
    check("b.die", air["die_c"], 23.5)
    check("b.tip", air["tip_c"], 23.25)
    wet = decode(_encode(10, 2, 3550, 3814, FLAG_MSV4 | FLAG_TEMP_OK, 107, 6710, None, 255,
                         die_q4=94, flags2=FLAG2_DIE_TEMP_OK, cd_centi=65535))
    check("b.wet.rsoil", wet["rsoil_ohm"], 355.0)
    check("b.wet.cm", wet["cm_pf"], 671.0)
    check("b.wet.cd", wet["cd_pf"], 655.35)
    check("b.wet.rms", wet["fit_rms"], 0.0255)
    # Short non-connectable bursts are marked; every older packet reads as connectable.
    burst = decode(_encode(10, 3, RSOIL_DECIOHM_MAX, 3820, FLAG_MSV4 | FLAG_TEMP_OK, 93, 0, None, 229,
                           die_q4=94, flags2=FLAG2_DIE_TEMP_OK | FLAG2_SHORT_BURST, cd_centi=1301))
    check("burst", burst["connectable"], False)
    check("burst.keeps.cd", burst["cd_pf"], 13.01)
    check("burst.keeps.die", burst["die_c"], 23.5)
    check("window", air["connectable"], True)
    check("v262148.connectable", row["connectable"], True)

    # Layout A packets never carried Cd.
    check("a.cd", decode(_encode(*common, die_q4=126, flags2=FLAG2_DIE_TEMP_OK))["cd_pf"], None)
    check("a.cp", decode(_encode(*common, die_q4=126, flags2=FLAG2_DIE_TEMP_OK))["cp_pf"], 8.8)

    # Air: saturated Rsoil, default Cp, negative tip temperature, repeated reading, OTA closed.
    row = decode(_encode(10, 7, RSOIL_DECIOHM_MAX, 3400, FLAG_MSV4 | FLAG_TEMP_OK, -21, 180, 120, 255))
    check("open.flag", row["rsoil_open"], True)
    check("open.rsoil25", row["rsoil25_ohm"], None)
    check("open.tip", row["tip_c"], -5.25)
    check("open.cp_default", row["cp_default"], True)
    check("open.fresh", row["fresh"], False)
    check("open.ota", row["ota_ok"], False)
    check("open.rms", row["fit_rms"], 0.0255)

    # A failed sweep: fields zeroed, bit5 set, battery still reported.
    row = decode(_encode(10, 8, 0, 3700, FLAG_MSV4 | FLAG_INVALID, 0, 0, 88, 0))
    check("invalid.valid", row["moisture_valid"], False)
    check("invalid.rsoil", row["rsoil_ohm"], None)
    check("invalid.cm", row["cm_pf"], None)
    check("invalid.tip", row["tip_c"], None)
    check("invalid.battery", row["battery_mv"], 3700)

    # Must not claim an MSV3 packet (bit7 clear, capless bit6 set) or a short or foreign one.
    check("msv3", decode(_encode(4, 9, 39_400_000, 3900, (1 << 6) | (1 << 2), 100, 0, 0, 0)), None)
    check("short", decode(_encode(10, 1, 1, 1, FLAG_MSV4, 0, 0, 0, 0)[:20]), None)
    check("magic", decode(b"MSV3" + bytes(17)), None)

    # The firmware's ceiling must stay under the receiver's 5,000,000-count MSV3 floor.
    check("ceiling", RSOIL_DECIOHM_MAX < 5_000_000, True)

    for line in failures:
        print("FAIL", line)
    print(f"self-test: {'FAILED' if failures else 'passed'} ({len(failures)} failure(s))")
    return 1 if failures else 0


# -------------------------------------------------------------------------------------- scan --
async def scan(seconds: float, sensor: int | None, csv_path: Path | None) -> dict[int, list[int]]:
    from bleak import BleakScanner

    seen: dict[int, list[int]] = {}
    writer = handle = None
    if csv_path:
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        new_file = not csv_path.exists()
        handle = csv_path.open("a", newline="", encoding="utf-8")
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        if new_file:
            writer.writeheader()

    def callback(device, advertisement):
        payload = advertisement.manufacturer_data.get(COMPANY_ID)
        row = decode(bytes(payload)) if payload else None
        if row is None or (sensor is not None and row["sensor"] != sensor):
            return
        counters = seen.setdefault(row["sensor"], [])
        if row["counter"] in counters:
            return  # one advertising window repeats the same frame many times
        counters.append(row["counter"])
        now = datetime.now().astimezone().isoformat(timespec="seconds")
        name = advertisement.local_name or device.name or "?"
        print(f"{now} {device.address} {name} rssi {advertisement.rssi} | {describe(row)}",
              flush=True)
        if writer:
            writer.writerow({"time": now, "rssi": advertisement.rssi, **row})
            handle.flush()

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        if seconds > 0:
            await asyncio.sleep(seconds)
        else:
            while True:
                await asyncio.sleep(3600)
    finally:
        await scanner.stop()
        if handle:
            handle.close()
    return seen


DIS_FIRMWARE_REVISION_UUID = "00002a26-0000-1000-8000-00805f9b34fb"
DIS_SOFTWARE_REVISION_UUID = "00002a28-0000-1000-8000-00805f9b34fb"
DIS_HARDWARE_REVISION_UUID = "00002a27-0000-1000-8000-00805f9b34fb"


async def read_build(address: str, timeout: float) -> int:
    """Connect during the probe's next advertising window and read what it says it is running.

    The probe drops an idle central after 30 s, so this connects, reads two strings and leaves.
    """
    from bleak import BleakClient, BleakScanner

    print(f"Waiting up to {timeout:.0f}s for a connectable advertisement from {address}...", flush=True)
    found = asyncio.get_running_loop().create_future()
    wanted = address.upper()
    candidate = None

    def callback(device, advertisement):
        nonlocal candidate
        if device.address.upper() != wanted or found.done():
            return
        payload = bytes(advertisement.manufacturer_data.get(COMPANY_ID, b""))
        row = decode(payload) if payload else None
        if row is not None and not row["connectable"]:
            candidate = None
            return  # a short burst: nothing can connect on this wake
        # The first event of a window can still carry the previous wake's payload (measured on
        # Windows, 2026-10-06), so a production packet must repeat before it is believed.
        if row is not None and candidate != row["counter"]:
            candidate = row["counter"]
            return
        found.set_result(device)

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        device = await asyncio.wait_for(found, timeout=timeout)
    except asyncio.TimeoutError:
        device = None
    finally:
        await scanner.stop()
    if device is None:
        print("No connectable advertisement seen.")
        return 1
    await asyncio.sleep(1.0)  # let the scanner wind down before opening GATT
    async with BleakClient(device, timeout=20.0) as client:
        # The hardware revision string is where application 262152 onward reports its boot-time
        # Cp calibration ("Cp 8.76 pF", with " default" if the calibration fell back to 12 pF).
        for label, uuid in (("firmware revision", DIS_FIRMWARE_REVISION_UUID),
                            ("software revision", DIS_SOFTWARE_REVISION_UUID),
                            ("hardware revision", DIS_HARDWARE_REVISION_UUID)):
            try:
                value = bytes(await client.read_gatt_char(uuid)).split(b"\x00")[0].decode("utf-8", "replace")
            except Exception as error:  # noqa: BLE001
                value = f"unreadable ({type(error).__name__})"
            print(f"{label}: {value}", flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true",
                        help="Decode synthetic packets and exit; no radio")
    parser.add_argument("--seconds", type=float, default=150.0,
                        help="How long to listen (default 150; 0 = until Ctrl+C)")
    parser.add_argument("--sensor", type=int, help="Only report this sensor ID")
    parser.add_argument("--require-progress", action="store_true",
                        help="Exit 1 unless at least two advancing counters were seen")
    parser.add_argument("--csv", type=Path, help="Append one row per new advertisement")
    parser.add_argument("--read-build", metavar="ADDRESS",
                        help="Connect to this address and read its DIS build strings, then exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.read_build:
        # Connectable windows can be ten minutes apart, so the default wait covers one full cycle.
        return asyncio.run(read_build(args.read_build, max(args.seconds, 700.0)))
    try:
        seen = asyncio.run(scan(args.seconds, args.sensor, args.csv))
    except KeyboardInterrupt:
        return 0
    if not seen:
        print("No MSV4 production advertisement seen.")
        return 1
    if args.require_progress:
        progressed = [s for s, c in seen.items() if len(c) >= 2 and c == sorted(c)]
        if not progressed:
            print("No sensor showed two advancing counters.")
            return 1
        print(f"Progress confirmed for sensor(s) {progressed}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
