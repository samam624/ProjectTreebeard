#!/usr/bin/env python3
"""Day-by-day solar charging of every probe in a set of stored cloud histories.

Reads CSVs that cloud/scripts/fetch_supabase_history.py --raw writes (one sensor each) and prints,
per sensor and per local day:

  morning   battery mV at dawn: median of the readings 06:00-08:00
  peak      highest 15-reading running median between 08:00 and 18:00, and its time
  gain      peak minus morning: what the day put into the cell, net of the load
  next      the following dawn's morning value; night = next minus peak (the overnight loss)
  temp      board temperature: dawn median, daytime peak of the running median, and the rise.
            On a probe in a window the rise tracks direct sun on the case, so it is a light
            proxy independent of the charger.

Battery values are each probe's own ADC reading; compare a probe with itself, day against day,
not one probe's millivolts against another's. A cell already at the charger's ceiling cannot
show a gain however bright the day, so read gain together with peak.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 4 --raw --since-hours 264 --csv data/sensor_logs/s4.csv
  python scripts/solar_charge_by_day.py data/sensor_logs/s4.csv data/sensor_logs/s7.csv
"""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from datetime import date, datetime, time, timedelta
from pathlib import Path

WINDOW = 15  # readings in the running median (about 15 minutes at one reading a minute)


def load(path: Path) -> tuple[str, list[tuple[datetime, float, float | None]]]:
    rows = []
    sensor = "?"
    with path.open(encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            if not record.get("raw_battery_mv"):
                continue
            sensor = record["sensor"]
            stamp = record["timestamp_iso"].replace(" ", "T")
            if stamp.endswith("+00"):
                stamp += ":00"
            temp = record.get("board_temp_c")
            rows.append((datetime.fromisoformat(stamp).astimezone(), float(record["raw_battery_mv"]),
                         float(temp) if temp else None))
    rows.sort(key=lambda row: row[0])
    return sensor, rows


def running_median(values: list[float]) -> list[float]:
    half = WINDOW // 2
    return [statistics.median(values[max(0, i - half):i + half + 1]) for i in range(len(values))]


def between(day: date, start: int, end: int, times: list[datetime]) -> list[int]:
    lo = datetime.combine(day, time(start)).astimezone()
    hi = datetime.combine(day, time(end)).astimezone()
    return [i for i, t in enumerate(times) if lo <= t < hi]


def report(path: Path) -> None:
    sensor, rows = load(path)
    if not rows:
        print(f"{path}: no battery readings")
        return
    times = [r[0] for r in rows]
    mv = running_median([r[1] for r in rows])
    temps = [r[2] for r in rows]
    have_temp = [i for i, t in enumerate(temps) if t is not None]
    temp_smooth = dict(zip(have_temp, running_median([temps[i] for i in have_temp]))) if have_temp else {}

    days = sorted({t.date() for t in times})
    morning: dict[date, float] = {}
    for day in days:
        idx = between(day, 6, 8, times)
        if len(idx) >= 30:
            morning[day] = statistics.median(rows[i][1] for i in idx)

    print(f"sensor {sensor}: {len(rows)} readings, {times[0]:%m-%d %H:%M} to {times[-1]:%m-%d %H:%M}")
    print("day         morning     peak   at     gain   next  night   temp dawn  peak  rise")
    for day in days:
        idx = between(day, 8, 18, times)
        if len(idx) < 60:
            continue
        top = max(idx, key=lambda i: mv[i])
        partial = times[-1] < datetime.combine(day, time(18)).astimezone()
        dawn = morning.get(day)
        nxt = morning.get(day + timedelta(days=1))
        gain = f"{mv[top] - dawn:+6.0f}" if dawn is not None else "     -"
        night = f"{nxt - mv[top]:+6.0f}" if nxt is not None else "     -"
        dawn_t = [temps[i] for i in between(day, 6, 8, times) if temps[i] is not None]
        day_t = [temp_smooth[i] for i in idx if i in temp_smooth]
        if dawn_t and day_t:
            t0 = statistics.median(dawn_t)
            tpk = max(day_t)
            temp_text = f"{t0:9.2f} {tpk:6.2f} {tpk - t0:+5.2f}"
        else:
            temp_text = "        -      -     -"
        print(f"{day:%a %m-%d}  {dawn if dawn is not None else float('nan'):7.0f}  {mv[top]:7.0f}  "
              f"{times[top]:%H:%M}  {gain}  {nxt if nxt is not None else float('nan'):5.0f}  {night}  "
              f"{temp_text}{'   (day not over: last reading ' + format(times[-1], '%H:%M') + ')' if partial else ''}")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="+")
    args = parser.parse_args()
    for path in args.csv:
        report(path)


if __name__ == "__main__":
    main()
