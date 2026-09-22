#!/usr/bin/env python3
"""Summarize a same-pot Alocasia thermal run from raw history CSV exports.

Input files are named ``treebeard_alocasia_s<ID>_YYYY-MM-DD.csv`` and come from
``cloud/scripts/fetch_supabase_history.py --raw``. This is descriptive analysis:
daily autocorrelation makes per-bin regression uncertainty misleading.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics as stats
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

# The September 2026 analysis window is entirely in Pacific Daylight Time.
PACIFIC = timezone(timedelta(hours=-7), "PDT")
IDS = (2, 3, 4, 5, 6, 9)


def number(value: str) -> float | None:
    try:
        return float(value) if value else None
    except ValueError:
        return None


def median_bins(path: Path, sensor: int) -> dict[datetime, dict[str, float]]:
    buckets: dict[datetime, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            time = datetime.fromisoformat(row["timestamp_iso"].replace("Z", "+00:00")).astimezone(PACIFIC)
            # Probe 4 joined the pot at 20:53 on Sep 18; probe 5 at 20:24 on Sep 19.
            if sensor == 4 and time < datetime(2026, 9, 18, 20, 53, tzinfo=PACIFIC):
                continue
            if sensor == 5 and (time < datetime(2026, 9, 19, 20, 24, tzinfo=PACIFIC)
                                or row["moisture_scale"] != "3"):
                continue
            if sensor in (4, 5, 6) and row["tank_phase"] != "0":
                continue
            time = time.replace(minute=time.minute // 10 * 10, second=0, microsecond=0)
            values = buckets[time]
            for name in ("moisture", "raw_moisture_khz", "raw_fdc_counts", "board_temp_c", "raw_battery_mv"):
                value = number(row[name])
                if value is not None:
                    values[name].append(value)
    return {time: {name: stats.median(values) for name, values in fields.items()}
            for time, fields in buckets.items()}


def residual(values: list[float], x: list[float], degree: int = 1) -> list[float]:
    xm, ym = stats.mean(x), stats.mean(values)
    centered = [v - xm for v in x]
    if degree == 1:
        denom = sum(v*v for v in centered)
        slope = sum(a * (b - ym) for a, b in zip(centered, values)) / denom if denom else 0
        return [value - ym - slope * at for at, value in zip(centered, values)]
    squared = [v*v for v in centered]
    sq_mean = stats.mean(squared)
    squared = [v - sq_mean for v in squared]
    aa = sum(v*v for v in centered)
    ab = sum(a*b for a, b in zip(centered, squared))
    bb = sum(v*v for v in squared)
    ay = sum(a*(b-ym) for a, b in zip(centered, values))
    by = sum(a*(b-ym) for a, b in zip(squared, values))
    det = aa*bb-ab*ab
    linear = (ay*bb-by*ab)/det
    quadratic = (by*aa-ay*ab)/det
    return [value-ym-linear*a-quadratic*b for a, b, value in zip(centered, squared, values)]


def adjusted_slope(y: list[float], temp: list[float], hours: list[float], degree: int = 1) -> tuple[float, float]:
    yd, td = residual(y, hours, degree), residual(temp, hours, degree)
    denom = sum(value * value for value in td)
    if not denom:
        return float("nan"), float("nan")
    slope = sum(a * b for a, b in zip(yd, td)) / denom
    corr = sum(a * b for a, b in zip(yd, td)) / math.sqrt(sum(v*v for v in yd) * denom)
    return slope, corr


def describe(rows: dict[int, dict[datetime, dict[str, float]]], day: str, ref: int) -> None:
    print(f"DAY {day}, temperature reference = probe {ref} die")
    for sensor in (3, 9, 4, 5, 2):
        joint = [(time, value, rows[ref][time]["board_temp_c"])
                 for time, value in sorted(rows[sensor].items())
                 if time.date().isoformat() == day and time in rows[ref]
                 and "board_temp_c" in rows[ref][time]]
        if sensor in (3, 9):
            joint = [(t, v, temp) for t, v, temp in joint if "raw_moisture_khz" in v]
            signal = [v["raw_moisture_khz"] for _, v, _ in joint]
            unit = "detector mV/C; positive = drier warm"
        elif sensor in (4, 5):
            joint = [(t, v, temp) for t, v, temp in joint if v.get("raw_fdc_counts", 0) > 0]
            counts = [v["raw_fdc_counts"] for _, v, _ in joint]
            center = stats.median(counts) if counts else 1
            signal = [-200 * math.log(count / center) for count in counts]
            unit = "tank C %/C; positive = wetter warm"
        else:
            signal = [v["moisture"] for _, v, _ in joint]
            unit = "moisture LSB/C; quantized"
        if len(joint) < 20:
            continue
        temps = [temp for _, _, temp in joint]
        hours = [t.hour + t.minute / 60 for t, _, _ in joint]
        slope, r = adjusted_slope(signal, temps, hours)
        quadratic, _ = adjusted_slope(signal, temps, hours, 2)
        print(f"  s{sensor}: n={len(joint):3}, temp={min(temps):.2f}..{max(temps):.2f} C, "
              f"slope={slope:+.4f} (quadratic trend: {quadratic:+.4f}) {unit}, r={r:+.3f}, "
              f"signal span={max(signal)-min(signal):.2f}")


def compare_probe7(directory: Path) -> None:
    rows = {sensor: median_bins(directory / f"treebeard_compare_s{sensor}_2026-09-21.csv", sensor)
            for sensor in (4, 5, 7)}
    long7 = median_bins(directory / "treebeard_compare_s7_6d_2026-09-21.csv", 7)
    print("PROBE 7: independent Begonia pot, own die temperature")
    for day in ("2026-09-17", "2026-09-18", "2026-09-19", "2026-09-20", "2026-09-21"):
        pairs = [(time, v) for time, v in sorted(long7.items()) if time.date().isoformat() == day]
        counts = [v["raw_fdc_counts"] for _, v in pairs]
        temp = [v["board_temp_c"] for _, v in pairs]
        hours = [time.hour + time.minute / 60 for time, _ in pairs]
        signal = [-200 * math.log(c / stats.median(counts)) for c in counts]
        linear, r = adjusted_slope(signal, temp, hours)
        quadratic, _ = adjusted_slope(signal, temp, hours, 2)
        print(f"  {day}: n={len(pairs)}, die={min(temp):.2f}..{max(temp):.2f} C, "
              f"tank C span={max(signal)-min(signal):.2f}%, "
              f"slope={linear:+.3f}%/C (quadratic {quadratic:+.3f}), r={r:+.3f}")
    print("SAME TIME AND TEMPERATURE REFERENCE: probe 4 die")
    for day in ("2026-09-20", "2026-09-21"):
        for sensor in (4, 5, 7):
            pairs = [(time, v, rows[4][time]["board_temp_c"])
                     for time, v in sorted(rows[sensor].items())
                     if time.date().isoformat() == day and time in rows[4]]
            counts = [v["raw_fdc_counts"] for _, v, _ in pairs]
            temp = [t for _, _, t in pairs]
            hours = [time.hour + time.minute / 60 for time, _, _ in pairs]
            signal = [-200 * math.log(c / stats.median(counts)) for c in counts]
            linear, _ = adjusted_slope(signal, temp, hours)
            quadratic, _ = adjusted_slope(signal, temp, hours, 2)
            print(f"  {day} s{sensor}: n={len(pairs)}, tank C span={max(signal)-min(signal):.2f}%, "
                  f"slope={linear:+.3f}%/C (quadratic {quadratic:+.3f})")
    print("SEP 21 COOLING, 15:00 to 18:00 PDT")
    for sensor in (4, 5, 7):
        first = rows[sensor][datetime(2026, 9, 21, 15, tzinfo=PACIFIC)]
        last = rows[sensor][datetime(2026, 9, 21, 18, tzinfo=PACIFIC)]
        dc = 100 * ((first["raw_fdc_counts"] / last["raw_fdc_counts"]) ** 2 - 1)
        dt = last["board_temp_c"] - first["board_temp_c"]
        print(f"  s{sensor}: die {dt:+.2f} C, tank C {dc:+.2f}%")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--suffix", default="2026-09-21")
    parser.add_argument("--probe7-comparison", action="store_true")
    args = parser.parse_args()
    rows = {sensor: median_bins(args.directory / f"treebeard_alocasia_s{sensor}_{args.suffix}.csv", sensor)
            for sensor in IDS}
    for sensor in IDS:
        data = rows[sensor]
        if data:
            dates = sorted(data)
            print(f"s{sensor}: {len(dates)} bins, {dates[0].isoformat()} to {dates[-1].isoformat()}")
    for day in ("2026-09-20", "2026-09-21"):
        for ref in (4, 5, 6):
            describe(rows, day, ref)
    for sensor in (4, 5):
        print(f"HOURLY s{sensor}, 10-minute median count, die temperature, and supply voltage")
        for time, value in sorted(rows[sensor].items()):
            if time.date().isoformat() in ("2026-09-20", "2026-09-21") and time.minute == 0 and time.hour % 2 == 0:
                print(f"  {time:%m-%d %H:%M} {value.get('raw_fdc_counts', float('nan')):.0f} "
                      f"{value.get('board_temp_c', float('nan')):.2f} C "
                      f"{value.get('raw_battery_mv', float('nan')):.0f} mV")
    print("MATCHED 17:00 PDT VALUES (10-minute medians)")
    for sensor in (2, 3, 9, 4, 5):
        print(f"  s{sensor}", end="")
        for day in (20, 21):
            time = datetime(2026, 9, day, 17, tzinfo=PACIFIC)
            value = rows[sensor].get(time, {})
            key = "raw_moisture_khz" if sensor in (3, 9) else "raw_fdc_counts" if sensor in (4, 5) else "moisture"
            print(f"  {day}: {value.get(key, float('nan')):.1f}", end="")
        print()
    if args.probe7_comparison:
        compare_probe7(args.directory)


if __name__ == "__main__":
    main()
