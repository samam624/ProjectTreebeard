#!/usr/bin/env python
"""Fetch a sensor's whole stored moisture history as five-minute medians.

These are the same buckets `msv2_read_sensor_auto_calibration` (migration 027) builds before it
learns a dry/wet range, so the calibration can be replayed from them
(`cloud/scripts/autocal_replay.py`). Read-only: one SELECT per sensor through the linked CLI.

  python cloud/scripts/fetch_moisture_buckets.py --receiver MSV2088A3104A7AC --sensors 1 2 3 4 5 7 9 10
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

from fetch_supabase_history import RECEIVER_ID_RE, run_query

DEFAULT_OUT = Path(__file__).resolve().parents[2] / "data" / "sensor_logs"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--receiver", required=True)
    parser.add_argument("--sensors", type=int, nargs="+", required=True)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="directory for moisture_5min_s<N>.csv")
    args = parser.parse_args()
    if not RECEIVER_ID_RE.match(args.receiver):
        parser.error("receiver ID contains unexpected characters")

    args.out.mkdir(parents=True, exist_ok=True)
    for sensor in args.sensors:
        rows = run_query(
            "select date_bin(interval '5 minutes', received_at, timestamptz '2000-01-01 00:00:00+00') as b, "
            "percentile_cont(0.5) within group (order by moisture) as m, count(*) as n, "
            "max(moisture_scale) as scale, "
            "percentile_cont(0.5) within group (order by board_temp_quarter_c) as tq "
            "from public.msv2_sensor_history "
            f"where receiver_id = '{args.receiver}' and sensor_id = {int(sensor)} and moisture is not null "
            "group by 1 order by 1;"
        )
        path = args.out / f"moisture_5min_s{sensor}.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["bucket_utc", "moisture", "n", "scale", "temp_c"])
            for row in rows:
                quarter = row.get("tq")
                writer.writerow([row["b"], row["m"], row["n"], row["scale"],
                                 "" if quarter is None else float(quarter) / 4.0])
        print(f"sensor {sensor}: {len(rows)} buckets -> {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
