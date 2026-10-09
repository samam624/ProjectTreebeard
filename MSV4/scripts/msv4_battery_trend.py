#!/usr/bin/env python3
"""Battery trend of an MSV4 probe from its stored cloud history.

Reads the CSV that cloud/scripts/fetch_supabase_history.py --raw writes and prints, in local time:
an hourly table, the slope over each block of hours, and every reading that stands apart from its
neighbours. Single readings scatter by a few mV and now and then one sits 12-25 mV off, so a trend
of a few mV per hour only shows in the fitted slopes, not by eye over an hour.

Battery values are the probe's own ADC reading. They are comparable with each other from
2026-10-06 22:16 PDT (application 262153, offset recalibrated every wake); the absolute value
carries the SAADC's gain tolerance and is not a meter reading.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 10 --raw --since-hours 48 --csv data/sensor_logs/msv4_s10_cloud.csv
  python MSV4/scripts/msv4_battery_trend.py data/sensor_logs/msv4_s10_cloud.csv --since "2026-10-06 23:10"
"""

from __future__ import annotations

import argparse
import csv
import statistics
from datetime import datetime, timedelta
from pathlib import Path

OUTLIER_MV = 10      # distance from the running median that marks a reading as standing apart
FIT_REJECT_MV = 8    # readings further than this from the running median are left out of a slope
WINDOW = 15          # readings in the running median


def load(path: Path) -> list[dict]:
    rows = []
    with path.open(encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            if not record.get("raw_battery_mv"):
                continue
            stamp = record["timestamp_iso"].replace(" ", "T")
            if stamp.endswith("+00"):
                stamp += ":00"
            rows.append({
                "time": datetime.fromisoformat(stamp).astimezone(),
                "wake": int(record["wake"]),
                "mv": int(record["raw_battery_mv"]),
                "tip_c": float(record["board_temp_c"]) if record.get("board_temp_c") else None,
                "measured": record.get("tank_phase") == "1",
            })
    rows.sort(key=lambda row: row["time"])
    for i, row in enumerate(rows):
        near = sorted(r["mv"] for r in rows[max(0, i - WINDOW // 2): i + WINDOW // 2 + 1])
        row["median"] = near[len(near) // 2]
    return rows


def slope(rows: list[dict]) -> tuple[float, float, float, float] | None:
    """Least squares over the readings near their running median: (mV/h, its 1-sigma, start, end)."""
    kept = [r for r in rows if abs(r["mv"] - r["median"]) <= FIT_REJECT_MV]
    if len(kept) < 10:
        return None
    t0 = kept[0]["time"]
    x = [(r["time"] - t0).total_seconds() / 3600.0 for r in kept]
    y = [float(r["mv"]) for r in kept]
    mx, my = statistics.fmean(x), statistics.fmean(y)
    sxx = sum((a - mx) ** 2 for a in x)
    if sxx == 0:
        return None
    k = sum((a - mx) * (b - my) for a, b in zip(x, y)) / sxx
    resid = [b - (my + k * (a - mx)) for a, b in zip(x, y)]
    sigma = (sum(e * e for e in resid) / (len(kept) - 2) / sxx) ** 0.5
    return k, sigma, my + k * (x[0] - mx), my + k * (x[-1] - mx)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="CSV from fetch_supabase_history.py --raw")
    parser.add_argument("--since", help='Local time to start from, "YYYY-MM-DD HH:MM"')
    parser.add_argument("--block-hours", type=float, default=3.0,
                        help="Length of each block a slope is fitted over (default 3)")
    parser.add_argument("--edges", nargs="*", default=[],
                        help='Block boundaries as local "HH:MM" on the last day in the file, in '
                             "place of fixed-length blocks")
    args = parser.parse_args()

    rows = load(args.csv)
    if args.since:
        start = datetime.strptime(args.since, "%Y-%m-%d %H:%M").astimezone()
        rows = [r for r in rows if r["time"] >= start]
    if not rows:
        print("No battery readings in range.")
        return 1
    print(f"{len(rows)} readings, {rows[0]['time']:%m-%d %H:%M} to {rows[-1]['time']:%m-%d %H:%M} local\n")

    print("hour            n  median   mean   min   max  tip C")
    hours: dict[datetime, list[dict]] = {}
    for row in rows:
        hours.setdefault(row["time"].replace(minute=0, second=0, microsecond=0), []).append(row)
    for hour in sorted(hours):
        group = hours[hour]
        values = [r["mv"] for r in group]
        tips = [r["tip_c"] for r in group if r["tip_c"] is not None]
        tip = f"{statistics.fmean(tips):5.2f}" if tips else "    ?"
        print(f"{hour:%m-%d %H}:00  {len(values):3}  {statistics.median(values):6.0f} "
              f"{statistics.fmean(values):6.1f}  {min(values)}  {max(values)}  {tip}")

    if args.edges:
        day = rows[-1]["time"]
        bounds = [day.replace(hour=int(e[:2]), minute=int(e[3:]), second=0, microsecond=0)
                  for e in args.edges]
        bounds = [rows[0]["time"]] + bounds + [rows[-1]["time"] + timedelta(seconds=1)]
    else:
        bounds = [rows[0]["time"]]
        while bounds[-1] <= rows[-1]["time"]:
            bounds.append(bounds[-1] + timedelta(hours=args.block_hours))
    print("\nblock                      n   mV/h  +-     from      to    tip C")
    for a, b in zip(bounds, bounds[1:]):
        block = [r for r in rows if a <= r["time"] < b]
        fitted = slope(block)
        if fitted is None:
            continue
        tips = [r["tip_c"] for r in block if r["tip_c"] is not None]
        tip = f"{min(tips):.2f}-{max(tips):.2f}" if tips else "?"
        print(f"{a:%m-%d %H:%M} to {block[-1]['time']:%H:%M}  {len(block):4}  {fitted[0]:+5.2f}  {fitted[1]:4.2f}  "
              f"{fitted[2]:7.1f} {fitted[3]:7.1f}  {tip}")

    apart = [r for r in rows if abs(r["mv"] - r["median"]) > OUTLIER_MV]
    print(f"\n{len(apart)} readings more than {OUTLIER_MV} mV from the median of the {WINDOW} around them:")
    for r in apart:
        print(f"  {r['time']:%m-%d %H:%M:%S}  counter {r['wake']:4}  {r['mv']} mV ({r['mv'] - r['median']:+d})"
              f"{'  measurement wake' if r['measured'] else ''}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
