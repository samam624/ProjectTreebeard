#!/usr/bin/env python
"""Continuously log MSV2 probe readings from the ESP32 display hub /data endpoint.

Meant for long unattended runs (e.g. an overnight moisture-drydown test) - polls
on an interval, appends one row per poll to a CSV, and keeps going through
transient network/HTTP errors instead of dying partway through the run.

Examples:
  python scripts/log_sensor_readings.py --url http://10.0.0.236 --sensor 1
  python scripts/log_sensor_readings.py --url http://10.0.0.236 --sensor 1 --interval 5 --csv data/sensor_logs/alocasia_2026-07-23.csv
  python scripts/log_sensor_readings.py --url http://10.0.0.236 --sensor 1 --duration 43200

Run detached so it survives past this terminal/session (PowerShell):
  Start-Process -FilePath python -ArgumentList "scripts/log_sensor_readings.py --url http://10.0.0.236 --sensor 1 --csv data/sensor_logs/alocasia_overnight.csv" -RedirectStandardOutput data/sensor_logs/alocasia_overnight.log -RedirectStandardError data/sensor_logs/alocasia_overnight.err -WindowStyle Hidden
"""

from __future__ import annotations

import argparse
import csv
import json
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


CSV_FIELDS = [
    "timestamp_iso",
    "elapsed_s",
    "sensor",
    "wake",
    "moisture",
    "battery_percent",
    "rssi",
    "age_ms",
]


def endpoint(base_or_endpoint: str, route: str) -> str:
    url = base_or_endpoint.rstrip("/")
    if url.endswith(route):
        return url
    return f"{url}{route}"


def fetch_data(url: str) -> dict:
    request = urllib.request.Request(
        endpoint(url, "/data"),
        headers={"Cache-Control": "no-cache"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def sensor_by_id(payload: dict) -> dict[int, dict]:
    return {int(item["sensor"]): item for item in payload.get("sensors", []) if "sensor" in item}


def main() -> int:
    parser = argparse.ArgumentParser(description="Log MSV2 probe readings to CSV over time.")
    parser.add_argument("--url", default="http://192.168.4.1", help="ESP32 display hub base URL.")
    parser.add_argument("--sensor", type=int, required=True, help="Sensor ID to log.")
    parser.add_argument("--interval", type=float, default=5.0, help="Seconds between polls.")
    parser.add_argument("--duration", type=float, default=0, help="Stop after this many seconds. 0 = run until stopped.")
    parser.add_argument("--csv", type=Path, required=True, help="CSV output path. Appends if the file already exists.")
    args = parser.parse_args()

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    file_is_new = not args.csv.exists() or args.csv.stat().st_size == 0

    csv_file = args.csv.open("a", newline="", encoding="utf-8")
    writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
    if file_is_new:
        writer.writeheader()
        csv_file.flush()

    start = time.monotonic()
    rows_written = 0
    consecutive_errors = 0

    print(f"Logging sensor {args.sensor} from {endpoint(args.url, '/data')} every {args.interval:g}s to {args.csv}")
    print("Press Ctrl+C to stop.\n")

    try:
        while True:
            elapsed = time.monotonic() - start
            if args.duration and elapsed > args.duration:
                break

            loop_start = time.monotonic()
            timestamp_iso = datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")

            try:
                payload = fetch_data(args.url)
                item = sensor_by_id(payload).get(args.sensor)
            except (OSError, urllib.error.URLError, json.JSONDecodeError, ValueError) as exc:
                consecutive_errors += 1
                print(f"[{elapsed:8.1f}s] fetch failed ({consecutive_errors} in a row): {exc}")
                time.sleep(args.interval)
                continue

            if item is None:
                consecutive_errors += 1
                print(f"[{elapsed:8.1f}s] sensor {args.sensor} not in /data yet ({consecutive_errors} in a row)")
                time.sleep(args.interval)
                continue

            consecutive_errors = 0
            row = {
                "timestamp_iso": timestamp_iso,
                "elapsed_s": f"{elapsed:.1f}",
                "sensor": args.sensor,
                "wake": item.get("wake", ""),
                "moisture": item.get("moisture", ""),
                "battery_percent": item.get("batteryPercent", ""),
                "rssi": item.get("rssi", ""),
                "age_ms": item.get("ageMs", ""),
            }
            writer.writerow(row)
            csv_file.flush()
            rows_written += 1

            print(
                f"[{elapsed:8.1f}s] rows={rows_written} wake={row['wake']} "
                f"moisture={row['moisture']} battery={row['battery_percent']}% rssi={row['rssi']}dBm"
            )

            sleep_for = args.interval - (time.monotonic() - loop_start)
            if sleep_for > 0:
                time.sleep(sleep_for)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        csv_file.close()

    print(f"\nWrote {rows_written} rows to {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
