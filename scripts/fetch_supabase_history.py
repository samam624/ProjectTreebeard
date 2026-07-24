#!/usr/bin/env python
"""Fetch MSV2 sensor history from Supabase into the same CSV schema
log_sensor_readings.py produces, so plot_sensor_readings.py can render either
source unchanged. Uses `supabase db query --linked` (the already-authenticated,
linked CLI) rather than the receiver's per-device API token, since this runs
from a dev machine that already has project access - no need to extract the
receiver's private token off the device.

Example:
  python scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 1 --since-hours 24 --csv data/sensor_logs/alocasia_overnight.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
from pathlib import Path

RECEIVER_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
SUPABASE_CLI = shutil.which("supabase.cmd") or shutil.which("supabase") or "supabase"

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


def run_query(sql: str) -> list[dict]:
    result = subprocess.run(
        [SUPABASE_CLI, "db", "query", "--linked", sql],
        capture_output=True,
        text=True,
        check=True,
    )
    payload = json.loads(result.stdout)
    return payload.get("rows", [])


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch MSV2 sensor history from Supabase into a CSV.")
    parser.add_argument("--receiver", required=True, help="Receiver ID, e.g. MSV2088A3104A7AC.")
    parser.add_argument("--sensor", type=int, required=True, help="Sensor ID to fetch.")
    parser.add_argument("--since-hours", type=float, default=24, help="How far back to fetch.")
    parser.add_argument("--csv", type=Path, required=True, help="CSV output path (overwritten each run).")
    args = parser.parse_args()

    if not RECEIVER_ID_RE.match(args.receiver):
        parser.error("receiver ID contains unexpected characters")

    sql = (
        "select sensor_id, wake, moisture, battery_percent, rssi, age_ms, received_at "
        "from public.msv2_sensor_history "
        f"where receiver_id = '{args.receiver}' "
        f"and sensor_id = {int(args.sensor)} "
        f"and received_at >= now() - interval '{float(args.since_hours)} hours' "
        "order by received_at asc;"
    )

    try:
        rows = run_query(sql)
    except subprocess.CalledProcessError as exc:
        print(f"supabase db query failed: {exc.stderr}")
        return 1
    except json.JSONDecodeError as exc:
        print(f"Could not parse supabase db query output: {exc}")
        return 1

    if not rows:
        print("No rows returned. Is cloud sync enabled on the receiver, and has it uploaded yet?")

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()

        first_ts = None
        for row in rows:
            received_at = row["received_at"]
            if first_ts is None:
                from datetime import datetime

                first_ts = datetime.fromisoformat(received_at.replace(" ", "T"))

            from datetime import datetime

            this_ts = datetime.fromisoformat(received_at.replace(" ", "T"))
            elapsed_s = (this_ts - first_ts).total_seconds()

            writer.writerow(
                {
                    "timestamp_iso": received_at,
                    "elapsed_s": f"{elapsed_s:.1f}",
                    "sensor": row["sensor_id"],
                    "wake": row.get("wake", ""),
                    "moisture": row.get("moisture", ""),
                    "battery_percent": row.get("battery_percent", ""),
                    "rssi": row.get("rssi", ""),
                    "age_ms": row.get("age_ms", ""),
                }
            )

    print(f"Wrote {len(rows)} rows to {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
