#!/usr/bin/env python
"""Log RSSI data from the Treebeard_Antenna_Test firmware over serial.

Tees the antenna-test board's serial output to the console (so you still see
the human-readable WiFi scan / BLE table blocks) and writes every machine-
parseable "RPT," line to CSV for later comparison against another board (e.g.
devkit vs. new PCB) or before/after an antenna change.

Examples:
  python scripts/antenna_rssi_logger.py --port COM14
  python scripts/antenna_rssi_logger.py --port COM14 --csv data/antenna_logs/new_pcb_2026-07-29.csv
  python scripts/antenna_rssi_logger.py --port COM14 --duration 300
"""

from __future__ import annotations

import argparse
import csv
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

CSV_FIELDS = [
    "timestamp_iso",
    "elapsed_s",
    "wifi_status",
    "wifi_rssi",
    "wifi_avg",
    "wifi_min",
    "wifi_max",
    "ble_seen",
    "ble_devices",
    "ble_best_rssi",
    "ble_best_addr",
]


def parse_report_line(line: str) -> dict | None:
    if not line.startswith("RPT,"):
        return None
    parts = line.strip().split(",")
    data_fields = CSV_FIELDS[1:]  # drop timestamp_iso - the firmware doesn't send it
    if len(parts) != len(data_fields) + 1:
        return None
    report = dict(zip(data_fields, parts[1:]))  # parts[0] is the "RPT" tag itself
    try:
        float(report["elapsed_s"])
    except ValueError:
        return None  # the firmware's own CSV header line, not a data row
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Log RSSI reports from Treebeard_Antenna_Test over serial.")
    parser.add_argument("--port", required=True, help="Serial port of the antenna-test board (e.g. COM14).")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    parser.add_argument("--duration", type=float, default=0, help="Stop after this many seconds. 0 = run until Ctrl+C.")
    parser.add_argument("--csv", type=Path, help="Optional CSV output path for RPT rows. Appends if it already exists.")
    parser.add_argument("--retry-interval", type=float, default=0.3, help="Seconds between reconnect attempts after a serial drop.")
    args = parser.parse_args()

    csv_file = None
    writer = None
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        file_is_new = not args.csv.exists() or args.csv.stat().st_size == 0
        csv_file = args.csv.open("a", newline="", encoding="utf-8")
        writer = csv.DictWriter(csv_file, fieldnames=["timestamp_iso"] + CSV_FIELDS[1:])
        if file_is_new:
            writer.writeheader()
            csv_file.flush()

    print(f"Opening {args.port} @ {args.baud} baud. Press Ctrl+C to stop.\n")

    rows_written = 0
    best_wifi_rssi = None
    worst_wifi_rssi = None
    best_ble_rssi = None
    start = time.monotonic()

    ser: serial.Serial | None = None
    try:
        while True:
            elapsed = time.monotonic() - start
            if args.duration and elapsed > args.duration:
                break

            if ser is None:
                try:
                    ser = serial.Serial(args.port, args.baud, timeout=1)
                    print(f"--- connected to {args.port} ---")
                except serial.SerialException:
                    time.sleep(args.retry_interval)
                    continue

            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                print(f"--- {args.port} dropped, reconnecting ---")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(args.retry_interval)
                continue

            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="replace").rstrip()
            except UnicodeDecodeError:
                continue
            if not line:
                continue

            report = parse_report_line(line)
            if report is None:
                print(line)
                continue

            print(
                f"[{report['elapsed_s']:>7}s] wifi={report['wifi_status']:<11} "
                f"rssi={report['wifi_rssi']:>4}dBm avg={report['wifi_avg']:>4} "
                f"min={report['wifi_min']:>4} max={report['wifi_max']:>4} | "
                f"ble_seen={report['ble_seen']:>3} devices={report['ble_devices']:>3} "
                f"best={report['ble_best_rssi']:>4}dBm ({report['ble_best_addr']})"
            )

            try:
                wifi_rssi = int(report["wifi_rssi"])
                if report["wifi_status"] == "CONNECTED":
                    best_wifi_rssi = wifi_rssi if best_wifi_rssi is None else max(best_wifi_rssi, wifi_rssi)
                    worst_wifi_rssi = wifi_rssi if worst_wifi_rssi is None else min(worst_wifi_rssi, wifi_rssi)
                ble_rssi = int(report["ble_best_rssi"])
                if ble_rssi != 0:
                    best_ble_rssi = ble_rssi if best_ble_rssi is None else max(best_ble_rssi, ble_rssi)
            except ValueError:
                pass

            if writer:
                row = {"timestamp_iso": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")}
                row.update(report)
                writer.writerow(row)
                csv_file.flush()
                rows_written += 1
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
        if csv_file:
            csv_file.close()

    print("\nSession summary:")
    print(f"  WiFi RSSI while connected: best={best_wifi_rssi} dBm, worst={worst_wifi_rssi} dBm")
    print(f"  Best BLE RSSI seen: {best_ble_rssi} dBm")
    if args.csv:
        print(f"  Wrote {rows_written} rows to {args.csv}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
