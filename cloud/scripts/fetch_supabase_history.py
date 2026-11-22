#!/usr/bin/env python
"""Fetch sensor history (every probe generation) from Supabase into CSV.

Default output uses the same CSV schema log_sensor_readings.py produces, so
plot_sensor_readings.py can render either source unchanged. `--raw` instead
emits every stored raw column (raw_battery_mv, raw_fdc_counts,
bare_tank_fdc_counts, board_temp_quarter_c, ...) for MSV3 analysis and
presentation graphing.

Uses `supabase db query --linked` (the already-authenticated, linked CLI)
rather than the receiver's per-device API token, since this runs from a dev
machine that already has project access - no need to extract the receiver's
private token off the device, and no service-role key on disk.

Examples:
  python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 1 --since-hours 24 --csv data/sensor_logs/alocasia_overnight.csv
  python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 4 --raw --since-hours 336 --csv data/sensor_logs/msv3_probe4_raw.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import tempfile
import os
import subprocess
from datetime import datetime
from pathlib import Path

RECEIVER_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
SUPABASE_CLI = shutil.which("supabase.cmd") or shutil.which("supabase") or "supabase"

# `supabase db query --linked` resolves the linked project ref from the Supabase
# project directory, not from the process CWD, so it fails with
# LegacyProjectNotLinkedError unless it is pointed at cloud/ (which holds
# supabase/config.toml). Pass --workdir explicitly so this script works from
# any directory.
SUPABASE_WORKDIR = Path(__file__).resolve().parents[1]

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

# Every raw column the receiver uploads, in the order that reads best in a
# spreadsheet: identity, then the battery chain, then the capacitive chain.
# board_temp_quarter_c is stored in quarter-degrees C; board_temp_c is derived
# here so a chart can use it directly.
RAW_DB_COLUMNS = [
    "wake",
    "moisture",
    "display_moisture",
    "moisture_scale",
    "battery_percent",
    "raw_battery_mv",
    "raw_charging",
    "raw_fdc_counts",
    "bare_tank_fdc_counts",
    "raw_moisture_khz",
    "tank_phase",
    "board_temp_quarter_c",
    "temperature_compensated_log_ratio_ppm",
    "temperature_compensation_model_version",
    "rssi",
    "age_ms",
]

RAW_CSV_FIELDS = ["timestamp_iso", "elapsed_s", "sensor"] + RAW_DB_COLUMNS + ["board_temp_c"]


CACHE_DIR = Path(__file__).resolve().parents[2] / "data" / "sensor_logs" / "cache"


def run_query(sql: str) -> list[dict]:
    result = subprocess.run(
        [SUPABASE_CLI, "db", "query", "--linked", "--workdir", str(SUPABASE_WORKDIR), sql],
        capture_output=True,
        text=True,
        check=True,
    )
    # The CLI prints a human preamble ("Initialising login role...") before the
    # JSON body, so json.loads on the whole of stdout fails. Start at the first
    # brace instead of assuming stdout is pure JSON.
    stdout = result.stdout
    brace = stdout.find("{")
    if brace < 0:
        raise json.JSONDecodeError("no JSON object in supabase output", stdout, 0)
    payload = json.loads(stdout[brace:])
    return payload.get("rows", [])


def parse_ts(value: str) -> datetime:
    return datetime.fromisoformat(value.replace(" ", "T"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch sensor history (every probe generation) from Supabase into a CSV.")
    parser.add_argument("--receiver", required=True, help="Receiver ID, e.g. MSV2088A3104A7AC.")
    parser.add_argument("--sensor", type=int, required=True, help="Sensor ID to fetch.")
    parser.add_argument("--since-hours", type=float, default=24, help="How far back to fetch.")
    parser.add_argument("--csv", type=Path, help="CSV output path (overwritten each run).")
    parser.add_argument(
        "--cache",
        action="store_true",
        help="Keep a growing file per receiver and sensor at data/sensor_logs/cache/<receiver>/sensor<N>[_raw].csv instead of a "
        "dated snapshot: each run refreshes the full receiver/sensor history so delayed uploads are included; "
        "--since-hours and --csv are ignored.",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Emit every stored raw column (MSV3 millivolts, FDC counts, board temperature) "
        "instead of the log_sensor_readings.py schema.",
    )
    args = parser.parse_args()
    if args.cache == (args.csv is not None):
        parser.error("give exactly one of --csv (one-off snapshot) or --cache (the per-sensor cache)")

    if not RECEIVER_ID_RE.match(args.receiver):
        parser.error("receiver ID contains unexpected characters")

    columns = RAW_DB_COLUMNS if args.raw else ["wake", "moisture", "battery_percent", "rssi", "age_ms"]
    select_list = ("id, receiver_id, " if args.cache else "") + "sensor_id, " + ", ".join(columns) + ", received_at"

    since_clause = f"received_at >= now() - interval '{float(args.since_hours)} hours' "
    if args.cache:
        args.csv = CACHE_DIR / args.receiver / f"sensor{int(args.sensor)}{'_raw' if args.raw else ''}.csv"
        # The observation time can precede earlier exports when an outage queue drains.
        # Re-read with IDs so delayed inserts and concurrent transactions are included.
        since_clause = "true "

    sql = (
        f"select {select_list} "
        "from public.msv2_sensor_history "
        f"where receiver_id = '{args.receiver}' "
        f"and sensor_id = {int(args.sensor)} "
        f"and {since_clause}"
        "order by received_at asc;"
    )

    try:
        rows = run_query(sql)
    except subprocess.CalledProcessError as exc:
        print(f"supabase db query failed: {exc.stderr or exc.stdout}")
        return 1
    except json.JSONDecodeError as exc:
        print(f"Could not parse supabase db query output: {exc}")
        return 1

    if not rows:
        print("No rows returned. Is cloud sync enabled on the receiver, and has it uploaded yet?")
        return 1

    # Rebuilding from the authoritative query also repairs incomplete exports.
    # Legacy unscoped caches stay available as archives.
    fields = RAW_CSV_FIELDS if args.raw else CSV_FIELDS
    if args.cache:
        fields = fields + ["id", "receiver_id"]
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.NamedTemporaryFile(mode="w", newline="", encoding="utf-8",
                                            dir=args.csv.parent, delete=False)
    temporary_path = Path(temporary.name)
    try:
        with temporary as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()

            first_ts = None
            for row in rows:
                received_at = row["received_at"]
                this_ts = parse_ts(received_at)
                if first_ts is None:
                    first_ts = this_ts
                elapsed_s = (this_ts - first_ts).total_seconds()

                record = {
                    "timestamp_iso": received_at,
                    "elapsed_s": f"{elapsed_s:.1f}",
                    "sensor": row["sensor_id"],
                }
                for column in columns:
                    value = row.get(column)
                    record[column] = "" if value is None else value

                if args.raw:
                    quarter_c = row.get("board_temp_quarter_c")
                    record["board_temp_c"] = "" if quarter_c is None else f"{quarter_c / 4.0:.2f}"

                if args.cache:
                    record["id"] = row["id"]
                    record["receiver_id"] = row["receiver_id"]
                writer.writerow(record)
        os.replace(temporary_path, args.csv)
    finally:
        temporary_path.unlink(missing_ok=True)

    print(f"Wrote {len(rows)} rows to {args.csv} ({len(rows)} fetched)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
