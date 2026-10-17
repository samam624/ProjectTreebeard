"""Why do the moisture candidates miss the scale: a systematic shift, or temperature?

msv4_moisture_competition.py fits one straight line per candidate through every weighing and
reports the miss. This takes the same weighings and readings apart three ways:

  1. Same water, same temperature, different week. The driest weighing before the 10-10 watering
     and the driest one since sit a few grams apart at nearly the same tip temperature. Whatever
     separates a candidate between them is neither water nor temperature.
  2. Leave-one-out miss, in grams, for four calibrations of each candidate against the scale:
     a line through every weighing (as the competition does), the same with a temperature term
     fitted to the weighings, and both again using only the weighings since the watering.
     A term that only fits noise makes the left-out miss worse, not better.
  3. Between consecutive weighings since the watering: change in candidate = s * grams lost +
     k * change in tip temperature, fitted with no intercept. k is the temperature coefficient
     the scale asks for, set beside the one msv4_moisture_tempcorr.py fits on a dry day.

    python MSV4/scripts/msv4_moisture_slope_diagnosis.py data/sensor_logs/<sensor10 history>.csv
    python MSV4/scripts/msv4_moisture_slope_diagnosis.py --self-test
"""
from __future__ import annotations

import argparse
import statistics
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_tempcorr as tempcorr

REF_C = tempcorr.REF_C


def weighed_points(rows: list[dict], value) -> tuple[list[dict], list]:
    """One point per scored weighing: grams of water, median candidate, median tip, after watering?"""
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    dry = min(p["grams"] for p in pairs)
    out = []
    for p in pairs:
        vals = [v for v in (value(r) for r in p["rows"]) if v is not None]
        tips = [r["tip"] for r in p["rows"] if r["tip"] is not None]
        if len(vals) < comp.MIN_PAIR_READINGS or not tips:
            continue
        out.append({"time": p["time"], "g": p["grams"] - dry, "c": statistics.median(vals),
                    "tip": statistics.median(tips), "after": any(w < p["time"] for w in waterings)})
    return out, waterings


def design(points: list[dict], temp: bool) -> np.ndarray:
    cols = [np.ones(len(points)), np.array([p["g"] for p in points])]
    if temp:
        cols.append(np.array([p["tip"] - REF_C for p in points]))
    return np.column_stack(cols)


def loo_miss(points: list[dict], temp: bool) -> tuple[float, np.ndarray]:
    """rms of the left-out misses in grams, and the all-point coefficients."""
    x, y = design(points, temp), np.array([p["c"] for p in points])
    coef, *_ = np.linalg.lstsq(x, y, rcond=None)
    misses = []
    for i in range(len(points)):
        keep = np.arange(len(points)) != i
        c_i, *_ = np.linalg.lstsq(x[keep], y[keep], rcond=None)
        misses.append((y[i] - x[i] @ c_i) / c_i[1])
    return float(np.sqrt(np.mean(np.square(misses)))), coef


def interval_fit(points: list[dict]) -> tuple[float, float, float, float, int]:
    """(s, k, se of k, rms miss in grams) from consecutive differences, no intercept."""
    after = [p for p in points if p["after"]]
    dc = np.array([b["c"] - a["c"] for a, b in zip(after, after[1:])])
    dg = np.array([b["g"] - a["g"] for a, b in zip(after, after[1:])])
    dt = np.array([b["tip"] - a["tip"] for a, b in zip(after, after[1:])])
    x = np.column_stack([dg, dt])
    coef, *_ = np.linalg.lstsq(x, dc, rcond=None)
    resid = dc - x @ coef
    cov = np.linalg.inv(x.T @ x) * (resid @ resid) / max(len(dc) - 2, 1)
    return float(coef[0]), float(coef[1]), float(np.sqrt(cov[1, 1])), float(np.sqrt(np.mean((resid / coef[0]) ** 2))), len(dc)


def report(rows: list[dict]) -> None:
    days = comp.dry_days(rows, comp.load_pot(rows[0]["sensor"])[1])
    _, dayfits = tempcorr.corrected_candidates(days[0]) if days else ((), [])
    day_k = {name: k for name, _, k, _ in dayfits}

    print("1. Same water, same temperature, a week apart")
    print(f"   {'candidate':42} {'before':>24} {'since':>24} {'shift, g':>9}")
    shifts = {}
    for name, unit, value in comp.CANDIDATES:
        points, _ = weighed_points(rows, value)
        before = min((p for p in points if not p["after"]), key=lambda p: p["g"], default=None)
        since = min((p for p in points if p["after"]), key=lambda p: p["g"], default=None)
        if before is None or since is None:
            continue
        s = loo_miss([p for p in points if p["after"]], False)[1][1]
        shift = (since["c"] - before["c"]) / s - (since["g"] - before["g"])
        shifts[name] = shift
        print(f"   {name:42} {before['time']:%m-%d %H:%M} {before['g']:4.0f} g {before['tip']:5.2f} C"
              f"   {since['time']:%m-%d %H:%M} {since['g']:4.0f} g {since['tip']:5.2f} C {shift:+9.0f}")
    print("   shift = how much wetter the candidate reads now than before the watering at the same water,"
          " in grams on the post-watering slope")

    print("\n2. Leave-one-out miss against the scale, rms grams (lower is better)")
    print(f"   {'candidate':42} {'all, line':>10} {'all, +temp':>11} {'since, line':>12} {'since, +temp':>13}"
          f" {'k from weighings':>17}")
    for name, unit, value in comp.CANDIDATES:
        points, _ = weighed_points(rows, value)
        after = [p for p in points if p["after"]]
        cells = [loo_miss(points, False)[0], loo_miss(points, True)[0], loo_miss(after, False)[0]]
        miss_t, coef_t = loo_miss(after, True)
        cells.append(miss_t)
        print(f"   {name:42} {cells[0]:10.1f} {cells[1]:11.1f} {cells[2]:12.1f} {cells[3]:13.1f}"
              f" {coef_t[2] / coef_t[1]:+9.1f} g/C")
    print("   'since' = weighings after the 10-10 watering only; k is the fitted temperature term in grams of"
          " water per C")

    print("\n3. Between consecutive weighings since the watering: change = s*grams + k*change in tip")
    print(f"   {'candidate':42} {'k, g/C (scale)':>16} {'k, g/C (dry day)':>17} {'rms miss, g':>12} {'without k':>10}")
    for name, unit, value in comp.CANDIDATES:
        points, _ = weighed_points(rows, value)
        s, k, se, miss, n = interval_fit(points)
        after = [p for p in points if p["after"]]
        dc = np.array([b["c"] - a["c"] for a, b in zip(after, after[1:])])
        dg = np.array([b["g"] - a["g"] for a, b in zip(after, after[1:])])
        s0 = float(dc @ dg / (dg @ dg))
        plain = float(np.sqrt(np.mean(((dc - s0 * dg) / s0) ** 2)))
        dk = next((v for key, v in day_k.items() if key == name), None)
        dk_text = f"{dk / s:+8.1f}" if dk is not None else "  (fixed 2 %/C)"
        print(f"   {name:42} {k / s:+8.1f}+/-{se / abs(s):4.1f} {dk_text:>17} {miss:12.1f} {plain:10.1f}")
    print(f"   {n} intervals. Dry-day k is msv4_moisture_tempcorr.py's fit on the first settled dry day,"
          " put in grams on the scale's slope s.")


def self_test() -> int:
    """A synthetic candidate with a known temperature term and a known shift at the watering."""
    from datetime import datetime, timedelta
    rng = np.random.default_rng(3)
    t0 = datetime(2026, 10, 10, 12, 0).astimezone()
    pts = []
    for i in range(18):
        g = 30 + 400 * np.exp(-i / 6) if i > 1 else 20 - 10 * i
        tip = 24 + 3 * np.sin(i * 1.7)
        after = i > 1
        c = 100 + 2.0 * g + 2.0 * 6.0 * (tip - REF_C) + (60.0 if after else 0.0) + rng.normal(0, 2)
        pts.append({"time": t0 + timedelta(hours=12 * i), "g": g, "c": c, "tip": tip, "after": after})
    s, k, _, _, _ = interval_fit(pts)
    failures = []
    if abs(k / s - 6.0) > 0.6:
        failures.append(f"interval k {k / s:.2f} g/C, want 6")
    after = [p for p in pts if p["after"]]
    if loo_miss(after, True)[0] > loo_miss(pts, False)[0]:
        failures.append("the shift-free, temperature-aware fit did not beat the plain line")
    for line in failures:
        print("FAIL", line)
    print(f"self-test: {'FAILED' if failures else 'passed'} ({len(failures)} failure(s))")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.csv is None:
        parser.error("need the sensor history CSV")
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    report(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
