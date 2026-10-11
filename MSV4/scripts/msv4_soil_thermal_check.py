#!/usr/bin/env python3
"""How an MSV4 probe's soil reading moves with temperature, from its stored cloud history.

Reads the CSV that cloud/scripts/fetch_supabase_history.py --raw writes and prints, in local time:

  1. a check that the stored moisture number is the receiver's formula applied to the stored
     Rsoil and tip temperature (calibrateMsv4Moisture(): 10000 * log10(400 kohm / Rsoil@25C),
     Rsoil@25C = Rsoil * (1 + 0.02 * (tip - 25)));
  2. a binned table of tip and die temperature, Rsoil as sent and at 25 C, the moisture number
     with and without the correction, Cm, Cd and Rsoil * Cm;
  3. the change across each stretch between --edges, and what Rsoil did per degree of tip.
     Rsoil * Cm is the part of the fitted branch that does not depend on how much of the blade
     the soil touches: more contact lowers Rsoil and raises Cm in proportion and leaves it alone;
  4. over --window, a fit of ln Rsoil against the tip (and die) temperature passed through a
     first-order lag, for a range of lag time constants; then how far the moisture number swings
     over that window with no correction, as stored, and with each of those fits as the
     correction. Those fits are judged on the hours they were fitted to, so --apply START END
     runs each one unchanged over a different span and prints how far it misses there;
  5. the reading-to-reading scatter, and whether the fit trades Rsoil against Cm;
  6. with --match, Rsoil and Cm at the same tip temperature on the way up to a peak and on the
     way back down. A temperature effect returns to where it was; a one-way change does not.

Only rows the probe measured on that wake (tank_phase 1) with something conducting are used.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 10 --raw --since-hours 30 --csv data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_soil_thermal_check.py data/sensor_logs/msv4_s10.csv --since "2026-10-08 22:08" --edges "2026-10-08 22:18" 01:30 09:00 11:30 12:30 15:00 --window "2026-10-09 11:00" "2026-10-09 15:20"
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from datetime import datetime, timedelta
from pathlib import Path

RSOIL_OPEN_OHM = 400000.0   # the probe's saturation value: nothing conducting
EC_TEMPCO_PER_C = 0.02      # MSV4_EC_TEMPCO_PER_C in the receiver
EC_REFERENCE_C = 25.0
EDGE_HALF_WIDTH_MIN = 10    # an edge's value is the median of the readings this close to it
LAGS_MIN = (0, 10, 20, 30, 45, 60, 90, 120, 150, 180, 240)


def rsoil_at_25(rsoil: float, tip_c: float) -> float:
    return rsoil * max(0.25, 1.0 + EC_TEMPCO_PER_C * (tip_c - EC_REFERENCE_C))


def moisture_number(rsoil: float) -> float:
    return 10000.0 * math.log10(RSOIL_OPEN_OHM / rsoil) if rsoil < RSOIL_OPEN_OHM else 0.0


def load(path: Path, since: datetime | None) -> list[dict]:
    rows = []
    with path.open(encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            if record.get("moisture_scale") != "4" or record.get("tank_phase") != "1":
                continue
            if not record.get("raw_fdc_counts") or not record.get("board_temp_quarter_c"):
                continue
            stamp = record["timestamp_iso"].replace(" ", "T")
            if stamp.endswith("+00"):
                stamp += ":00"
            when = datetime.fromisoformat(stamp).astimezone()
            rsoil = int(record["raw_fdc_counts"]) / 10.0
            if (since and when < since) or not 1.0 <= rsoil < RSOIL_OPEN_OHM:
                continue
            word = int(record["bare_tank_fdc_counts"] or 0)
            tip = int(record["board_temp_quarter_c"]) / 4.0
            rows.append({
                "time": when,
                "rsoil": rsoil,
                "rsoil25": rsoil_at_25(rsoil, tip),
                "tip": tip,
                "die": int(record["raw_moisture_khz"]) / 4.0 if record.get("raw_moisture_khz") else None,
                "cm": (word & 0xFFFF) / 10.0,
                "cd": ((word >> 32) & 0xFFFF) / 100.0,
                "stored": int(record["moisture"]),
            })
    rows.sort(key=lambda row: row["time"])
    return rows


def median(rows: list[dict], key: str) -> float:
    return statistics.median(r[key] for r in rows if r[key] is not None)


def solve(columns: list[list[float]], y: list[float]) -> tuple[list[float], float]:
    """Least squares by the normal equations: (coefficients, residual standard deviation)."""
    n = len(columns)
    a = [[sum(p * q for p, q in zip(columns[i], columns[j])) for j in range(n)] +
         [sum(p * q for p, q in zip(columns[i], y))] for i in range(n)]
    for i in range(n):
        pivot = max(range(i, n), key=lambda r: abs(a[r][i]))
        a[i], a[pivot] = a[pivot], a[i]
        for r in range(n):
            if r != i:
                f = a[r][i] / a[i][i]
                a[r] = [u - f * v for u, v in zip(a[r], a[i])]
    beta = [a[i][n] / a[i][i] for i in range(n)]
    resid = [v - sum(b * c[k] for b, c in zip(beta, columns)) for k, v in enumerate(y)]
    return beta, statistics.pstdev(resid)


def lagged(rows: list[dict], key: str, tau_min: float) -> list[float]:
    """The series passed through a first-order lag of that time constant."""
    out, state = [], rows[0][key]
    for i, row in enumerate(rows):
        if i and tau_min > 0:
            dt = (row["time"] - rows[i - 1]["time"]).total_seconds() / 60.0
            state += (1.0 - math.exp(-dt / tau_min)) * (row[key] - state)
        else:
            state = row[key]
        out.append(state)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--since", help='local time, "YYYY-MM-DD HH:MM"')
    parser.add_argument("--bin", type=int, default=30, help="table bin in minutes")
    parser.add_argument("--edges", nargs="*", default=[], help='HH:MM on the last day in the file, or "YYYY-MM-DD HH:MM"')
    parser.add_argument("--window", nargs=2, metavar=("START", "END"), help="local times for the lag fit")
    parser.add_argument("--apply", nargs=2, metavar=("START", "END"),
                        help="local times: run each --window fit unchanged over this other span")
    parser.add_argument("--match", nargs=2, metavar=("START", "PEAK"),
                        help="local times: compare equal tip temperatures between START and PEAK with those after PEAK")
    args = parser.parse_args()

    since = datetime.fromisoformat(args.since).astimezone() if args.since else None
    rows = load(args.csv, since)
    if len(rows) < 20:
        raise SystemExit("not enough conducting MSV4 measurements in that file")
    print(f"{len(rows)} measurements, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}")

    # 1. The instrument: is the stored number the formula?
    worst = max(abs(r["stored"] - round(moisture_number(r["rsoil25"]))) for r in rows)
    print(f"\nStored moisture minus the formula recomputed from Rsoil and tip: at most {worst} unit(s)")

    # 2. Binned table.
    print(f"\n{'':16}  tip C  die C   Rsoil  Rsoil@25  moist(raw)  moist  Cm pF  Cd pF  R*Cm us   n")
    start = rows[0]["time"].replace(minute=rows[0]["time"].minute // args.bin * args.bin, second=0, microsecond=0)
    while start <= rows[-1]["time"]:
        chunk = [r for r in rows if start <= r["time"] < start + timedelta(minutes=args.bin)]
        if chunk:
            rs = median(chunk, "rsoil")
            print(f"{start:%Y-%m-%d %H:%M}  {statistics.fmean(r['tip'] for r in chunk):5.2f}  "
                  f"{statistics.fmean(r['die'] for r in chunk if r['die'] is not None):5.2f}  {rs:6.0f}  "
                  f"{median(chunk, 'rsoil25'):8.0f}  {moisture_number(rs):10.0f}  {median(chunk, 'stored'):5.0f}  "
                  f"{median(chunk, 'cm'):5.1f}  {median(chunk, 'cd'):5.2f}  {rs * median(chunk, 'cm') * 1e-6:7.3f}  {len(chunk):2d}")
        start += timedelta(minutes=args.bin)

    # 3. Change across each stretch between edges.
    if len(args.edges) >= 2:
        day = rows[-1]["time"].date()
        points = []
        for edge in args.edges:
            if len(edge) > 5:
                at, edge = datetime.fromisoformat(edge).astimezone(), edge[5:]
            else:
                at = datetime.combine(day, datetime.strptime(edge, "%H:%M").time()).astimezone()
            near = [r for r in rows if abs((r["time"] - at).total_seconds()) <= EDGE_HALF_WIDTH_MIN * 60]
            if near:
                points.append((edge, {k: median(near, k) for k in ("tip", "die", "rsoil", "rsoil25", "cm", "cd", "stored")}))
        print(f"\n{'':26}  d tip  d die  d Rsoil  per C tip  d Rsoil@25  d moist(raw)  d moist    d Cm  d R*Cm  d Cd pF")
        for (e0, p0), (e1, p1) in zip(points, points[1:]):
            d_tip = p1["tip"] - p0["tip"]
            d_r = 100.0 * (p1["rsoil"] / p0["rsoil"] - 1.0)
            per_c = f"{d_r / d_tip:+6.1f} %" if abs(d_tip) >= 1.0 else "      -"
            print(f"{e0 + ' -> ' + e1:26}  {d_tip:+5.2f}  {p1['die'] - p0['die']:+5.2f}  {d_r:+6.1f}%  {per_c:>9}  "
                  f"{100.0 * (p1['rsoil25'] / p0['rsoil25'] - 1.0):+9.1f}%  "
                  f"{moisture_number(p1['rsoil']) - moisture_number(p0['rsoil']):+12.0f}  {p1['stored'] - p0['stored']:+7.0f}  "
                  f"{100.0 * (p1['cm'] / p0['cm'] - 1.0):+6.1f}%  "
                  f"{100.0 * (p1['rsoil'] * p1['cm'] / (p0['rsoil'] * p0['cm']) - 1.0):+5.1f}%  {p1['cd'] - p0['cd']:+7.2f}")

    # 4. Lag fit over the window.
    if args.window:
        w0, w1 = (datetime.fromisoformat(t).astimezone() for t in args.window)
        index = [i for i, r in enumerate(rows) if w0 <= r["time"] <= w1]
        y = [math.log(rows[i]["rsoil"]) for i in index]
        hours = [(rows[i]["time"] - w0).total_seconds() / 3600.0 for i in index]
        ones = [1.0] * len(index)
        # What each no-drift fit says Rsoil should read over the last 20 minutes of the file.
        last = [i for i, r in enumerate(rows) if (rows[-1]["time"] - r["time"]).total_seconds() <= 1200]
        print(f"\nln Rsoil against lagged temperature, {w0:%m-%d %H:%M} to {w1:%H:%M}, {len(index)} readings")
        print("                    no drift term                         with a linear drift term")
        print(f"driver  lag min   %/C   resid sd %  Rsoil at {rows[-1]['time']:%H:%M}      %/C   drift %/h   resid sd %")
        tip_fits = []
        for key in ("tip", "die"):
            for tau in LAGS_MIN:
                series = lagged(rows, key, tau)
                temp = [series[i] for i in index]
                plain, sd_plain = solve([ones, temp], y)
                if key == "tip":
                    tip_fits.append((tau, plain, series))
                drift, sd_drift = solve([ones, temp, hours], y)
                predicted = math.exp(plain[0] + plain[1] * statistics.median(series[i] for i in last))
                print(f"{key:6}  {tau:7d}  {-100 * plain[1]:5.2f}  {100 * sd_plain:9.2f}  {predicted:14.0f}    "
                      f"{-100 * drift[1]:6.2f}  {100 * drift[2]:+9.2f}  {100 * sd_drift:10.2f}")
        print(f"Rsoil over those last 20 minutes, measured: {statistics.median(rows[i]['rsoil'] for i in last):.0f}")
        _, sd_fixed = solve([ones], [math.log(rows[i]["rsoil25"]) for i in index])
        _, sd_none = solve([ones], y)
        print(f"For scale: sd of ln Rsoil as sent {100 * sd_none:.2f} %, of ln Rsoil@25 as the receiver computes it {100 * sd_fixed:.2f} %")

        # How far the moisture number swings over the window under each correction.
        def swing(values: list[float]) -> str:
            bins: dict[datetime, list[float]] = {}
            for i, value in zip(index, values):
                at = rows[i]["time"]
                bins.setdefault(at.replace(minute=at.minute // args.bin * args.bin, second=0, microsecond=0), []).append(value)
            meds = sorted((statistics.median(v), at) for at, v in bins.items() if len(v) >= args.bin / 3)
            return (f"{meds[-1][0] - meds[0][0]:5.0f}   low {meds[0][0]:5.0f} at {meds[0][1]:%m-%d %H:%M}, "
                    f"high {meds[-1][0]:5.0f} at {meds[-1][1]:%m-%d %H:%M}")

        applied = []
        if args.apply:
            a0, a1 = (datetime.fromisoformat(t).astimezone() for t in args.apply)
            applied = [i for i, r in enumerate(rows) if a0 <= r["time"] <= a1]
        print(f"\nMoisture number over the window, peak to peak of the {args.bin}-minute medians")
        print(f"no correction                 {swing([moisture_number(rows[i]['rsoil']) for i in index])}")
        print(f"as stored (2 %/C, no lag)     {swing([rows[i]['stored'] for i in index])}")
        for tau, plain, series in tip_fits:
            line = swing([moisture_number(rows[i]["rsoil"] * math.exp(plain[1] * (EC_REFERENCE_C - series[i]))) for i in index])
            if applied:
                miss = [math.log(rows[i]["rsoil"]) - plain[0] - plain[1] * series[i] for i in applied]
                line += f"   applied: mean {100 * statistics.fmean(miss):+5.2f} %, sd {100 * statistics.pstdev(miss):4.2f} %"
            print(f"tip lag {tau:3d} min, {-100 * plain[1]:4.2f} %/C   {line}")
        print("The lagged rows are each fit judged on the hours it was fitted to."
              + (f" 'applied' is the same fit over {a0:%m-%d %H:%M} to {a1:%H:%M} ({len(applied)} readings): ln Rsoil measured minus predicted."
                 if applied else ""))

    # 5. Scatter and parameter trade-off.
    steps = [(math.log(b["rsoil"] / a["rsoil"]), math.log(b["cm"] / a["cm"]))
             for a, b in zip(rows, rows[1:])
             if (b["time"] - a["time"]).total_seconds() < 150 and a["cm"] > 0 and b["cm"] > 0]
    dr, dc = [s[0] for s in steps], [s[1] for s in steps]
    print(f"\nReading-to-reading step in ln Rsoil: sd {100 * statistics.pstdev(dr):.2f} % "
          f"(one reading {100 * statistics.pstdev(dr) / math.sqrt(2):.2f} %); in ln Cm: sd {100 * statistics.pstdev(dc):.2f} %")
    print(f"Correlation of those steps, ln Cm against ln Rsoil: {statistics.correlation(dr, dc):+.2f}")

    # 6. The same tip temperature on the way up and on the way back down.
    if args.match:
        m0, peak = (datetime.fromisoformat(t).astimezone() for t in args.match)
        up = [r for r in rows if m0 <= r["time"] < peak]
        down = [r for r in rows if r["time"] >= peak]
        print(f"\nSame tip temperature before and after {peak:%m-%d %H:%M} (readings from {m0:%H:%M})")
        print("tip C    warming: at    Rsoil  Cm pF    cooling: at    Rsoil  Cm pF   Rsoil change   Cm change")
        for level in sorted({r["tip"] for r in up} & {r["tip"] for r in down}):
            a = [r for r in up if r["tip"] == level]
            b = [r for r in down if r["tip"] == level]
            if len(a) < 5 or len(b) < 5 or level * 2 != int(level * 2):
                continue
            ra, rb, ca, cb = median(a, "rsoil"), median(b, "rsoil"), median(a, "cm"), median(b, "cm")
            print(f"{level:5.2f}  {a[len(a) // 2]['time']:%H:%M} ({len(a):3d})  {ra:7.0f}  {ca:5.1f}    "
                  f"{b[len(b) // 2]['time']:%H:%M} ({len(b):3d})  {rb:7.0f}  {cb:5.1f}   {100 * (rb / ra - 1):+10.1f} %  {100 * (cb / ca - 1):+8.1f} %")
        for name, part in (("warming", up), ("cooling", down), ("both", up + down)):
            if len(part) > 10:
                r_tip = statistics.correlation([p["tip"] for p in part], [math.log(p["rsoil"]) for p in part])
                hrs = [(p["time"] - m0).total_seconds() for p in part]
                r_time = statistics.correlation(hrs, [math.log(p["rsoil"]) for p in part])
                print(f"Correlation of ln Rsoil, {name:8}: with tip {r_tip:+.2f}, with time {r_time:+.2f}  ({len(part)} readings)")


if __name__ == "__main__":
    main()
