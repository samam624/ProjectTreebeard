"""Every raw-sweep electrode model against the Begonia pot's weighings (sensor 11), like for like.

msv4_begonia_competition.py scores the stored-number candidates. This adds the parameters of the
models fitted to each raw sweep (lumped, CPE, two-branch, RC line: msv4_cloud_sweeps.py) and scores
them the same way, so one table ranks all of them: hourly medians around each weighing, grams of
water = a + b * x (x logged for R and C, as msv4_sweep_record.py does; CPE n is linear), then the
miss in grams in-sample and leave-one-out. The stored candidates are re-scored here with the same
regression so nothing is ranked on a different yardstick.

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 400 --rs 3300 --cp 8.75 --cd0 13.71 \
        --out data/sensor_logs/cache/sensor11_sweep_fits.csv
    python MSV4/scripts/msv4_begonia_model_competition.py
"""
from __future__ import annotations

import csv
import math
import statistics
import sys
from datetime import timedelta
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import msv4_begonia_competition as bc
import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_sweep_record as record

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / "data" / "sensor_logs" / "cache"
WINDOW = timedelta(minutes=30)
MIN_READINGS = 3


def fit_miss(xs: list[float], ys: list[float]) -> tuple[float, float, float]:
    """(in-sample rms g, leave-one-out rms g, slope g per x unit) of ys on xs."""
    x, y = np.array(xs), np.array(ys)
    slope, icpt = np.polyfit(x, y, 1)
    insample = float(np.sqrt(np.mean((y - (slope * x + icpt)) ** 2)))
    loo = []
    for i in range(len(x)):
        keep = np.arange(len(x)) != i
        s, c = np.polyfit(x[keep], y[keep], 1)
        loo.append(y[i] - (s * x[i] + c))
    return insample, float(np.sqrt(np.mean(np.square(loo)))), float(slope)


def main() -> None:
    weighings, _ = bc.load_begonia()
    water0 = min(w["grams"] for w in weighings)
    history = [r for r in reading.load(CACHE / "sensor11_raw.csv", None) if r["coupled"] is not None]
    fits = record.join(CACHE / "sensor11_sweep_fits.csv", CACHE / "sensor11_raw.csv")

    candidates: list[tuple[str, str, list[dict], object]] = []
    for name, _unit, fn in comp.CANDIDATES:
        candidates.append((name, "stored", history, fn))
    for key, name in record.PARAMS:
        linear = key in record.LINEAR
        candidates.append((f"sweep fit: {name}", "sweep", fits,
                           (lambda k, lin: lambda r: r.get(k) if lin else (math.log(r[k]) if r.get(k, 0) > 0 else None))(key, linear)))

    print(f"{len(history)} stored readings, {len(fits)} sweep fits joined, {len(weighings)} weighings "
          f"(water = grams above the driest, {water0:.0f} g)\n")
    print(f"{'candidate':46} {'source':7} {'paired':>6} {'in-sample g':>11} {'LOO g':>7} {'sign':>5}")
    table = []
    for name, source, rows, fn in candidates:
        xs, ys = [], []
        for w in weighings:
            vals = [v for r in rows if abs(r["time"] - w["time"]) <= WINDOW and (v := fn(r)) is not None]
            if len(vals) >= MIN_READINGS:
                xs.append(statistics.median(vals))
                ys.append(w["grams"] - water0)
        if len(xs) < 4:
            table.append((99, 99, name, source, len(xs), 0.0))
            continue
        ins, loo, slope = fit_miss(xs, ys)
        table.append((loo, ins, name, source, len(xs), slope))
    for loo, ins, name, source, n, slope in sorted(table):
        if loo == 99:
            print(f"{name:46} {source:7} {n:6d}   too few weighings with readings")
        else:
            print(f"{name:46} {source:7} {n:6d} {ins:11.1f} {loo:7.1f} {'wet+' if slope > 0 else 'wet-':>5}")
    print("\nsign: wet+ means the logged value rises with water (expected for C), wet- means it falls (expected for R).")


if __name__ == "__main__":
    main()
