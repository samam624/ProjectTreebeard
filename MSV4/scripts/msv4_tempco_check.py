#!/usr/bin/env python3
"""Is the receiver's 2 %/C soil-resistance correction right for sensor 10, and is the daily wobble temperature?

Two ways to fit a temperature coefficient, which fail in opposite directions:

  within the day   reading = slow drying trend + k * tip. Cheap and uses every reading, but the
                   sun warms the tip and dries the pot in the SAME hours, so a pot that loses most
                   of its water around noon pulls k low (the noon drop is read as temperature).
  against the scale  between two weighings with no watering, change in reading = s * grams lost +
                   k * change in tip. The scale measures the water whatever hour it left, so the
                   noon drying cannot leak into k. Fewer points (one per weighing interval).

The script prints, per watering cycle:
  1. where the day's water goes, from the scale alone (grams/hour inside 11:00-15:00 and outside);
  2. the coefficient each method gives for the soil resistance;
  3. for coefficients 0-2.5 %/C, how far the moisture number misses the scale over each weighing
     interval (the end use) and how far it strays from a straight line through each day (what
     looks like wobble on a plot);
  4. for every candidate on the moisture-vs-mass plot, the coefficient the plot uses (fitted on
     the first settled day) against one fitted on the scale, scored on the OTHER cycle.

The --self-test builds a pot whose true coefficient is 2 %/C and which loses about half its water
between 11:00 and 15:00, and checks that the scale method recovers 2 % while the within-day fit
comes out low, which is the bias this script exists to expose.

    python MSV4/scripts/msv4_tempco_check.py data/sensor_logs/<sensor10 history>.csv
    python MSV4/scripts/msv4_tempco_check.py --self-test
"""
from __future__ import annotations

import argparse
import math
import statistics
from datetime import datetime, timedelta, timezone
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
from msv4_sweep_cycle import hat_basis

RSOIL_MAX_OHM = 400000.0      # the receiver's ceiling: moisture = 10000*log10(400 kohm / R@25C)
REF_C = 25.0
UNITS_PER_FRACTION = 10000.0 / math.log(10.0)   # d(units)/d(a*dT) at small a: 1 %/C = 43.4 units/C
SKIP_AFTER_WATERING_H = 30.0  # every reading decays for ~30 h after a watering (cycle-2 record, 10-21)
TREND_KNOT_H = 6.0            # within-day fit: drying trend knots; 2-48 h all gave the same answer on 10-27
NOON = (11, 15)               # the sun reaches the shelf about 12:00 (multi-probe test record)
GRID = (0.0, 0.005, 0.01, 0.015, 0.02, 0.025)


def number_at(row: dict, a: float) -> float:
    """The receiver's moisture number recomputed with coefficient a (fraction per C)."""
    return 10000.0 * math.log10(RSOIL_MAX_OHM / (row["rsoil"] * (1.0 + a * (row["tip"] - REF_C))))


def cycle_of(t: datetime, waterings: list[datetime]) -> int:
    return sum(1 for w in waterings if w <= t)


def hours_since_watering(t: datetime, waterings: list[datetime]) -> float:
    return min(((t - w).total_seconds() / 3600 for w in waterings if w <= t), default=math.inf)


def stretches(rows: list[dict], waterings: list[datetime]) -> dict[int, list[dict]]:
    """Each cycle's settled dry-down: from SKIP_AFTER_WATERING_H after a watering to the next one."""
    out = {}
    for c, watered in enumerate(waterings, start=1):
        end = waterings[c] if c < len(waterings) else rows[-1]["time"] + timedelta(minutes=1)
        start = watered + timedelta(hours=SKIP_AFTER_WATERING_H)
        out[c] = [r for r in rows if start <= r["time"] < end]
    return out


def intervals(pairs: list[dict], waterings: list[datetime], value) -> dict[int, np.ndarray]:
    """Per cycle: rows of (grams change, tip change, value change, start) over neighbouring weighings."""
    out: dict[int, list] = {}
    for p, q in zip(pairs, pairs[1:]):
        c = cycle_of(p["time"], waterings)
        if c == 0 or c != cycle_of(q["time"], waterings) or hours_since_watering(p["time"], waterings) < SKIP_AFTER_WATERING_H:
            continue
        a = [value(r) for r in p["rows"]]
        b = [value(r) for r in q["rows"]]
        a, b = [v for v in a if v is not None], [v for v in b if v is not None]
        if len(a) < comp.MIN_PAIR_READINGS or len(b) < comp.MIN_PAIR_READINGS:
            continue
        tip = statistics.mean(r["tip"] for r in q["rows"]) - statistics.mean(r["tip"] for r in p["rows"])
        out.setdefault(c, []).append((q["grams"] - p["grams"], tip, statistics.median(b) - statistics.median(a),
                                      p["time"].timestamp(), q["time"].timestamp()))
    return {c: np.array(v) for c, v in out.items()}


def ols(x: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    coef, *_ = np.linalg.lstsq(x, y, rcond=None)
    resid = y - x @ coef
    cov = np.linalg.pinv(x.T @ x) * (resid @ resid) / max(1, len(y) - x.shape[1])
    return coef, cov


def scale_fit(iv: np.ndarray) -> tuple[float, float, float]:
    """(per gram, k per C, se of k), with the scale's grams regressed on the probe's change and the tip's.

    The scale is the noisy side (repeats to 8 g; the probe's change is a median of ~60 readings),
    so it goes on the left. With intervals losing tens of grams the other direction gives the same
    answer on average (20 synthetic pots: 2.00 vs 2.03 for a true 2.00, scatter 0.23 either way).
    grams = (change - k * tip) / s is fitted as grams = b1 * change + b2 * tip, so s = 1 / b1 and
    k = -b2 / b1.
    """
    coef, cov = ols(iv[:, [2, 1]], iv[:, 0])
    b1, b2 = float(coef[0]), float(coef[1])
    grad = np.array([b2 / b1 ** 2, -1.0 / b1])
    return 1.0 / b1, -b2 / b1, float(np.sqrt(grad @ cov @ grad))


def interval_miss(iv: np.ndarray, k: float = 0.0) -> float:
    """rms grams by which the (corrected) change misses the scale; grams per unit fitted through the origin."""
    dv = iv[:, 2] - k * iv[:, 1]
    b = float(dv @ iv[:, 0] / (dv @ dv))
    return float(np.sqrt(np.mean((b * dv - iv[:, 0]) ** 2)))


def noon_rates(iv: np.ndarray, tz) -> tuple[float, float]:
    """(g/h outside NOON, g/h inside NOON) from the scale alone."""
    def noon_hours(t0: float, t1: float) -> float:
        n, t = 0.0, t0
        while t < t1:
            hour = datetime.fromtimestamp(t, tz).hour
            n += (min(t1, t + 600) - t) / 3600 if NOON[0] <= hour < NOON[1] else 0.0
            t += 600
        return n
    span = (iv[:, 4] - iv[:, 3]) / 3600
    noon = np.array([noon_hours(a, b) for a, b in zip(iv[:, 3], iv[:, 4])])
    coef, _ = ols(np.column_stack([span - noon, noon]), -iv[:, 0])
    return float(coef[0]), float(coef[1])


def within_day(stretch: list[dict], value) -> float:
    """k per C from 10-minute medians = piecewise-linear trend + k * tip."""
    t0, bins = stretch[0]["time"], {}
    for r in stretch:
        v = value(r)
        if v is not None:
            bins.setdefault(int((r["time"] - t0).total_seconds() // 600), []).append((v, r["tip"]))
    keys = sorted(k for k in bins if len(bins[k]) >= 3)
    hours = np.array([k / 6 for k in keys])
    y = np.array([statistics.median(v for v, _ in bins[k]) for k in keys])
    tip = np.array([statistics.mean(t for _, t in bins[k]) for k in keys]) - REF_C
    x = np.column_stack([hat_basis(hours, TREND_KNOT_H), tip])
    x = x[:, x.any(axis=0)]
    coef, *_ = np.linalg.lstsq(x, y, rcond=None)
    return float(coef[-1])


def daily_wobble(stretch: list[dict], value, per_gram: float) -> float:
    """Median over whole days of the hourly medians' range about a straight line, in grams."""
    out, start = [], stretch[0]["time"]
    while start + timedelta(hours=24) <= stretch[-1]["time"] + timedelta(minutes=1):
        day = [r for r in stretch if start <= r["time"] < start + timedelta(hours=24)]
        hours = comp.hourly(day, value)
        if len(hours) >= 20:
            out.append(comp.wobble(hours) / per_gram)
        start += timedelta(hours=24)
    return statistics.median(out) if out else math.nan


def analyse(rows: list[dict], weighings: list[dict], waterings: list[datetime], plot_ks: dict[str, float]) -> dict:
    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings)
             if not p["excluded"] and p["saucer"] is False and len(p["rows"]) >= comp.MIN_PAIR_READINGS]
    raw = lambda r: comp._log_ohm(r["rsoil"]) if r["rsoil"] else None
    raw_iv = intervals(pairs, waterings, raw)
    parts = stretches(rows, waterings)
    cycles = sorted(raw_iv)
    result = {"cycles": cycles, "noon": {}, "scale": {}, "within": {}, "grid": {}, "candidates": []}
    for c in cycles:
        result["noon"][c] = noon_rates(raw_iv[c], rows[0]["time"].tzinfo)
        s, k, se = scale_fit(raw_iv[c])
        result["scale"][c] = (k / UNITS_PER_FRACTION, se / UNITS_PER_FRACTION, len(raw_iv[c]))
        result["within"][c] = within_day(parts[c], raw) / UNITS_PER_FRACTION
    both = np.vstack([raw_iv[c] for c in cycles])
    _, k, se = scale_fit(both)
    result["scale"]["all"] = (k / UNITS_PER_FRACTION, se / UNITS_PER_FRACTION, len(both))
    for a in GRID:
        value = lambda r, a=a: number_at(r, a) if r["rsoil"] else None
        iv = intervals(pairs, waterings, value)
        row = {}
        for c in cycles:
            s = scale_fit(iv[c])[0]
            row[c] = (interval_miss(iv[c]), daily_wobble(parts[c], value, s))
        result["grid"][a] = row
    if len(cycles) >= 2:
        first, second = cycles[0], cycles[1]
        for name, unit, value in comp.CANDIDATES:
            if name == "moisture number in use":
                continue
            iv = intervals(pairs, waterings, value)
            if first not in iv or second not in iv:
                continue
            s_all, k_all, se_all = scale_fit(np.vstack([iv[first], iv[second]]))
            k1, k2 = scale_fit(iv[first])[1], scale_fit(iv[second])[1]
            pk = plot_ks.get(name)
            result["candidates"].append({
                "name": name, "unit": unit, "k_scale": k_all, "se": se_all, "g_per_c_scale": k_all / s_all,
                "k_plot": pk, "g_per_c_plot": None if pk is None else pk / s_all,
                # out of sample: each cycle corrected with the other cycle's scale fit
                "miss_none": [interval_miss(iv[first]), interval_miss(iv[second])],
                "miss_plot": None if pk is None else [interval_miss(iv[first], pk), interval_miss(iv[second], pk)],
                "miss_other": [interval_miss(iv[first], k2), interval_miss(iv[second], k1)],
            })
    return result


def report(result: dict, waterings: list[datetime]) -> None:
    cycles = result["cycles"]
    label = {c: f"cycle {c} (after {waterings[c - 1]:%m-%d %H:%M})" for c in cycles}
    print(f"Readings from {SKIP_AFTER_WATERING_H:.0f} h after each watering; weighing intervals with a dry saucer only.\n")
    print(f"1. Where the day's water goes, from the scale alone ({NOON[0]:02d}:00-{NOON[1]:02d}:00 against the rest)")
    for c in cycles:
        other, noon = result["noon"][c]
        span = NOON[1] - NOON[0]
        share = span * noon / (span * noon + (24 - span) * other)
        print(f"   {label[c]}: {noon:.2f} g/h inside, {other:.2f} g/h outside; {share:.0%} of a day's loss in those {span} h")
    print("\n2. Soil-resistance coefficient, %/C (the receiver uses 2.00)")
    print(f"   {'':34} {'against the scale':>22} {'within the day':>15}")
    for c in cycles:
        k, se, n = result["scale"][c]
        print(f"   {label[c]:34} {k * 100:12.2f} +/- {se * 100:.2f} ({n:2d}) {result['within'][c] * 100:12.2f}")
    k, se, n = result["scale"]["all"]
    print(f"   {'both cycles':34} {k * 100:12.2f} +/- {se * 100:.2f} ({n:2d})")
    print("\n3. The moisture number at other coefficients, in grams")
    print("   interval miss = rms of (number's change - scale's change) over weighing intervals (the scale repeats to 8 g)")
    print("   daily wobble  = median over days of the hourly range about a straight line through the day")
    head = "".join(f"  {'c' + str(c) + ' miss':>9} {'c' + str(c) + ' wobble':>10}" for c in cycles)
    print(f"   {'coef':>6}{head}")
    for a, row in result["grid"].items():
        mark = "  <- in use" if abs(a - 0.02) < 1e-9 else ""
        print(f"   {a * 100:5.1f}%" + "".join(f"  {row[c][0]:9.1f} {row[c][1]:10.1f}" for c in cycles) + mark)
    if result["candidates"]:
        c1, c2 = cycles[0], cycles[1]
        print(f"\n4. Every candidate on the plot: its temperature coefficient, and the interval miss (g) it leaves")
        print(f"   scored out of sample: cycle {c1} corrected with cycle {c2}'s scale fit and the reverse; 'plot' is the first-settled-day fit the plot uses")
        print(f"   {'candidate':44} {'scale fit, g/C':>15} {'plot, g/C':>10}   {'miss: none':>11} {'plot':>11} {'other cycle':>12}")
        for d in result["candidates"]:
            pl = "-" if d["g_per_c_plot"] is None else f"{d['g_per_c_plot']:+.1f}"
            mp = "-" if d["miss_plot"] is None else f"{d['miss_plot'][0]:.1f}/{d['miss_plot'][1]:.1f}"
            print(f"   {d['name']:44} {d['g_per_c_scale']:+15.1f} {pl:>10}   "
                  f"{d['miss_none'][0]:5.1f}/{d['miss_none'][1]:<5.1f} {mp:>11} {d['miss_other'][0]:5.1f}/{d['miss_other'][1]:<5.1f}")


def synthetic(true_a: float = 0.02, days: int = 8, seed: int = 4) -> tuple[list[dict], list[dict], list[datetime]]:
    """A pot that loses ~54 % of each day's water in 11:00-15:00 while the sun warms the tip."""
    rng = np.random.default_rng(seed)
    tz = timezone(timedelta(hours=-7))
    watered = datetime(2026, 1, 1, 0, 30, tzinfo=tz)
    rows, weighings = [], []
    water, temp = 300.0, 22.5
    t = watered + timedelta(minutes=1)
    end = watered + timedelta(days=days)
    next_weigh = watered + timedelta(hours=8)
    while t < end:
        hour = t.hour + t.minute / 60
        in_noon = NOON[0] <= hour < NOON[1]
        water -= (5.0 if in_noon else 0.9) / 60
        temp += ((1.5 if in_noon else 0.0) - (temp - 22.0) / 6.0) / 60      # sun heats, then relaxes over ~6 h
        tip = round((temp + rng.normal(0, 0.05)) * 4) / 4
        units = 13.5 * water + 9000.0 + UNITS_PER_FRACTION * math.log(1.0 + true_a * (tip - REF_C))
        rsoil = RSOIL_MAX_OHM / 10 ** ((units + rng.normal(0, 3.0)) / 10000.0)
        rows.append({"time": t, "rsoil": rsoil, "tip": tip, "open": False, "stored": 0})
        if t >= next_weigh:
            weighings.append({"time": t, "grams": 4500 + water + rng.normal(0, 3.0), "saucer": False, "excluded": False})
            next_weigh = t + timedelta(hours=float(rng.uniform(7, 16)))
        t += timedelta(minutes=1)
    return rows, weighings, [watered]


def self_test(seeds: int = 6) -> None:
    """Several synthetic pots (one pot's 8 days gives only ~13 intervals, scatter ~0.23 %/C)."""
    saved = comp.CANDIDATES
    comp.CANDIDATES = ()
    scale, within, best, noon, other = [], [], [], [], []
    wobble = {a: [] for a in GRID}
    try:
        for seed in range(seeds):
            result = analyse(*synthetic(seed=seed), {})
            for a in GRID:
                wobble[a].append(result["grid"][a][1][1])
            scale.append(result["scale"][1][0])
            within.append(result["within"][1])
            best.append(min(result["grid"], key=lambda a: result["grid"][a][1][0]))
            other.append(result["noon"][1][0])
            noon.append(result["noon"][1][1])
    finally:
        comp.CANDIDATES = saved
    print(f"{seeds} synthetic pots, true 2.00 %/C, 5.0 g/h in 11-15 and 0.9 g/h otherwise:")
    print(f"  scale fit {statistics.mean(scale) * 100:.2f} (each {', '.join(f'{k * 100:.2f}' for k in scale)})")
    print(f"  within the day {statistics.mean(within) * 100:.2f}; best interval miss at {', '.join(f'{b * 100:.1f}' for b in best)} %")
    print(f"  scale-only split {statistics.mean(noon):.2f} g/h inside, {statistics.mean(other):.2f} outside")
    print("  daily wobble with NO error but the coefficient: "
          + ", ".join(f"{a * 100:.1f} % {statistics.mean(v):.1f} g" for a, v in wobble.items()))
    assert abs(statistics.mean(scale) - 0.02) < 0.0015, "the scale method must recover the true coefficient on average"
    assert statistics.mean(within) < 0.016, "noon-concentrated drying must pull the within-day fit low (the bias shown)"
    assert statistics.median(best) in (0.015, 0.02, 0.025), "the interval miss must be smallest near the true coefficient"
    assert 4.0 < statistics.mean(noon) < 6.0 and 0.6 < statistics.mean(other) < 1.2, "the noon split must recover 5 and 0.9 g/h"
    print("PASS tempco check: scale fit recovers 2 %/C; the within-day fit is pulled low by noon drying")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.csv is None:
        parser.error("give a sensor history CSV or --self-test")
    rows = [r for r in reading.load(args.csv, None)
            if r["coupled"] is not None and not r["open"] and r["tip"] is not None]
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    import msv4_moisture_tempcorr as tempcorr
    days = comp.dry_days(rows, waterings)
    plot_ks = {name: k for name, _, k, _ in tempcorr.corrected_candidates(days[0])[1]} if days else {}
    print(f"{len(rows)} readings, sensor {rows[0]['sensor']}, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}; "
          f"{len(weighings)} weighings, {len(waterings)} waterings\n")
    report(analyse(rows, weighings, waterings, plot_ks), waterings)


if __name__ == "__main__":
    main()
