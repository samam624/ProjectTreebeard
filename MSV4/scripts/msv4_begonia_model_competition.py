"""Every raw-sweep electrode model against the Begonia pot's weighings (sensor 11), like for like.

msv4_begonia_competition.py scores the stored-number candidates. This adds the parameters of the
models fitted to each raw sweep (lumped, CPE, two-branch, RC line: msv4_cloud_sweeps.py) and scores
them the same way, so one table ranks all of them: hourly medians around each weighing, grams of
water = a + b * x (x logged for R and C, as msv4_sweep_record.py does; CPE n is linear), then the
miss in grams in-sample and leave-one-out. The stored candidates are re-scored here with the same
regression so nothing is ranked on a different yardstick.

A reading pairs with a weighing only inside the same drying cycle. For readings a cycle starts at
the probe's first sweep after the water went in (the sweep runs every 10th wake, so readings between
the watering and that sweep still describe the soil before it); for weighings it starts at the
watering itself. The second table fits each candidate on the first cycle alone and predicts the
later weighings with that line: whether a conversion survives a watering.

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 400 --rs 3300 --cp 8.75 --cd0 13.71 \
        --out data/sensor_logs/cache/sensor11_sweep_fits.csv
    python MSV4/scripts/msv4_begonia_model_competition.py
"""
from __future__ import annotations

import json
import math
import statistics
import sys
from datetime import datetime, timedelta
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


def load_weighings() -> tuple[list[dict], list[datetime]]:
    """Weighings (with their post-watering flag) and every watering time, oldest first."""
    fleet = json.loads(comp.FLEET.read_text(encoding="utf-8"))
    state = next(b for b in fleet["msv4_boards"]["boards"] if b.get("sensor_id") == 11)["physical_state"]
    weighings = [{"time": datetime.fromisoformat(w["at"]), "grams": float(w["grams"]),
                  "post_watering": bool(w.get("post_watering"))} for w in state["mass"]["weighings"]]
    # The first event predates the structured `at`; its measured time is in bc.load_begonia (12:29 PDT).
    waterings = bc.load_begonia()[1] + [datetime.fromisoformat(e["at"])
                                        for e in state["watering_events"]["events"] if "at" in e]
    return sorted(weighings, key=lambda w: w["time"]), sorted(waterings)


def paired(weighings: list[dict], rows: list[dict], fn, cycle_of_reading, cycle_of_weighing) -> list[tuple[dict, float]]:
    """(weighing, median candidate value within WINDOW and in the same cycle) where there are enough readings."""
    out = []
    for w in weighings:
        c = cycle_of_weighing(w["time"])
        vals = [v for r in rows if abs(r["time"] - w["time"]) <= WINDOW and cycle_of_reading(r["time"]) == c
                and (v := fn(r)) is not None]
        if len(vals) >= MIN_READINGS:
            out.append((w, statistics.median(vals)))
    return out


def main() -> None:
    weighings, waterings = load_weighings()
    water0 = min(w["grams"] for w in weighings)
    history = [r for r in reading.load(CACHE / "sensor11_raw.csv", None) if r["coupled"] is not None]
    fits = record.join(CACHE / "sensor11_sweep_fits.csv", CACHE / "sensor11_raw.csv")
    # For readings, a cycle begins at the first sweep after the water went in.
    starts = [next((f["time"] for f in fits if f["time"] >= w), w) for w in waterings]

    def cycle_of_reading(t: datetime) -> int:
        return sum(1 for s in starts if s <= t) - 1

    def cycle_of_weighing(t: datetime) -> int:
        return sum(1 for w in waterings if w <= t) - 1

    candidates: list[tuple[str, str, list[dict], object]] = []
    for name, _unit, fn in comp.CANDIDATES:
        candidates.append((name, "stored", history, fn))
    for key, name in record.PARAMS:
        linear = key in record.LINEAR
        candidates.append((f"sweep fit: {name}", "sweep", fits,
                           (lambda k, lin: lambda r: r.get(k) if lin else (math.log(r[k]) if r.get(k, 0) > 0 else None))(key, linear)))

    print(f"{len(history)} stored readings, {len(fits)} sweep fits joined, {len(weighings)} weighings "
          f"(water = grams above the driest, {water0:.0f} g)")
    print("readings' cycles start at the first sweep after each watering: "
          + ", ".join(f"{s:%m-%d %H:%M}" for s in starts) + "\n")
    # A weighing taken right after a watering is not settled soil; like the mass plot, it is left out of fits.
    print("ONE LINE THROUGH EVERY SETTLED WEIGHING (both cycles)")
    print(f"{'candidate':46} {'source':7} {'paired':>6} {'in-sample g':>11} {'LOO g':>7} {'sign':>5}")
    table = []
    for name, source, rows, fn in candidates:
        pairs = [(w, x) for w, x in paired(weighings, rows, fn, cycle_of_reading, cycle_of_weighing)
                 if not w["post_watering"]]
        xs, ys = [x for _, x in pairs], [w["grams"] - water0 for w, _ in pairs]
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

    # Fit on cycle 1 only, then predict every later weighing with that line.
    later = [w for w in weighings if cycle_of_weighing(w["time"]) > 0]
    settled = [w for w in later if not w["post_watering"]]
    if not later:
        return
    print("\nFITTED ON CYCLE 1 ONLY, PREDICTING THE LATER WEIGHINGS (predicted minus scale, g)")
    heads = " ".join(f"{w['time'].strftime('%m-%d %H:%M') + ('*' if w['post_watering'] else ''):>13}" for w in later)
    print(f"{'candidate':46} {'c1 n':>4} {'c1 LOO':>6} {heads} {'settled mean':>12} {'change':>7}")
    scale_change = settled[-1]["grams"] - settled[0]["grams"] if len(settled) >= 2 else None
    rows_out = []
    for name, _source, rows, fn in candidates:
        pairs = paired(weighings, rows, fn, cycle_of_reading, cycle_of_weighing)
        first = [(w, x) for w, x in pairs if cycle_of_weighing(w["time"]) == 0]
        if len(first) < 4:
            continue
        xs1, ys1 = [x for _, x in first], [w["grams"] for w, _ in first]
        _, loo, _ = fit_miss(xs1, ys1)
        slope, icpt = np.polyfit(xs1, ys1, 1)
        predicted = {id(w): slope * x + icpt for w, x in pairs}
        errors = [predicted[id(w)] - w["grams"] if id(w) in predicted else None for w in later]
        settled_err = [predicted[id(w)] - w["grams"] for w in settled if id(w) in predicted]
        mean = statistics.fmean(settled_err) if settled_err else float("nan")
        change = None
        if scale_change is not None and id(settled[0]) in predicted and id(settled[-1]) in predicted:
            change = predicted[id(settled[-1])] - predicted[id(settled[0])]
        rows_out.append((abs(mean), name, len(first), loo, errors, mean, change))
    for _, name, n, loo, errors, mean, change in sorted(rows_out):
        cells = " ".join(f"{e:+13.0f}" if e is not None else f"{'-':>13}" for e in errors)
        ch = f"{change:+7.0f}" if change is not None else f"{'-':>7}"
        print(f"{name:46} {n:4d} {loo:6.1f} {cells} {mean:+12.1f} {ch}")
    if scale_change is not None:
        print(f"\nchange: predicted change from {settled[0]['time']:%m-%d %H:%M} to {settled[-1]['time']:%m-%d %H:%M}; "
              f"the scale moved {scale_change:+.0f} g.")
    print("* weighed right after a watering (soil not settled, dish may still have been filling): not in the mean.")
    print("c1 LOO: leave-one-out miss inside cycle 1. settled mean: mean error over the later weighings without *;"
          " rows sorted by its size.")


if __name__ == "__main__":
    main()
