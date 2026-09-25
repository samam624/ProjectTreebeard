#!/usr/bin/env python3
"""Robustness audit of MSV3_ALOCASIA_WATERING_REVIEW_2026-09-24.md.

Re-derives the review's load-bearing claims with the alternative estimators and
calibrations the review did not try, using the same input files and the same
binning/rejection code (imported from analyze_alocasia_watering.py). Findings
are written up in MSV3/docs/MSV3_ALOCASIA_WATERING_REVIEW_AUDIT_2026-09-24.md.

    python MSV3/scripts/audit_alocasia_watering.py <dir with treebeard_alocasia_s<ID>_2026-09-24.csv>
"""

from __future__ import annotations

import argparse
import statistics as stats
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import analyze_alocasia_watering as W

P = W.PACIFIC
WET = ("2026-09-23", "2026-09-24")
DRY = ("2026-09-21",)


def at(day, hour=0, minute=0):
    return datetime(2026, 9, day, hour, minute, tzinfo=P)


def load(directory):
    rows = {s: W.median_bins(directory / f"treebeard_alocasia_s{s}_2026-09-24.csv", s) for s in W.RIGS}
    signals = {}
    for sensor, data in rows.items():
        pre = [v.get("raw_fdc_counts") for t, v in sorted(data.items())
               if W.WATERING - timedelta(hours=3) <= t < W.WATERING and v.get("raw_fdc_counts")]
        signals[sensor] = W.series(data, sensor, stats.median(pre) if pre else 1.0)
    return rows, signals


def temps_of(rows, sensor):
    return {t: v["board_temp_c"] for t, v in rows[sensor].items() if "board_temp_c" in v}


def predawn(points, day):
    return W.median_in(points, (at(day, 3), at(day, 6)))


def day_span(points, day):
    start = datetime.fromisoformat(day).replace(tzinfo=P)
    pts = W.window(points, start, start + timedelta(days=1))
    return W.diurnal_span([v for _, v in pts], [t.hour + t.minute / 60 for t, _ in pts])


def joint_k(points, temps, start, end, trend):
    """Temperature coefficient fitted jointly with a multi-day trend.

    trend 'lin' for the slow pre-watering dry-down; 'exp' (grid over tau) for the
    curved post-watering dry-down. Avoids both per-day detrending (which leaks
    dry-down curvature into the slope) and a +/-3 h high-pass (which keeps only
    ~10 % of a 24 h cycle).
    """
    pts = [(t, v, temps[t]) for t, v in W.window(points, start, end) if t in temps]
    hours = np.array([(t - pts[0][0]).total_seconds() / 3600 for t, _, _ in pts])
    y = np.array([v for _, v, _ in pts])
    x = np.array([c for _, _, c in pts])
    best = None
    for tau in ([None] if trend == "lin" else [2, 3, 4, 6, 8, 12, 16, 24, 36, 48, 72, 100, 150, 250]):
        design = np.column_stack([np.ones_like(hours), x, hours if tau is None else np.exp(-hours / tau)])
        beta, *_ = np.linalg.lstsq(design, y, rcond=None)
        sse = float(((design @ beta - y) ** 2).sum())
        if best is None or sse < best[0]:
            best = (sse, float(beta[1]))
    return best[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    rows, sig = load(parser.parse_args().directory)

    print("A. Temperature coefficient by estimator (FDC: own die, log-cap %/C; AM/RC: probe 4 die)")
    print("   review = per-day linear detrend, the estimator the review's soil-water table uses")
    print(f"   {'rig':6} {'review 09-21':>12} {'09-23':>7} {'09-24':>7} | {'hipass 09-21':>12} {'09-23':>7} {'09-24':>7}"
          f" | {'joint PRE':>9} {'joint POST':>10}")
    for s in (4, 5, 7, 6, 3, 9, 2):
        temps = temps_of(rows, s if s in (4, 5, 6, 7) else 4)
        det, hp = [], []
        for day in ("2026-09-21", "2026-09-23", "2026-09-24"):
            start = datetime.fromisoformat(day).replace(tzinfo=P)
            end = start + timedelta(days=1)
            pairs = [(t, v, temps[t]) for t, v in W.window(sig[s], start, end) if t in temps]
            det.append(W.adjusted_slope([v for _, v, _ in pairs], [c for *_, c in pairs],
                                        [t.hour + t.minute / 60 for t, _, _ in pairs])[0])
            hp.append(W.high_pass_slope(sig[s], temps, start, end)[0])
        pre = joint_k(sig[s], temps, at(21) if s == 5 else at(20), at(22, 10), "lin")
        post = joint_k(sig[s], temps, at(22, 22), at(24, 16, 30), "exp")
        print(f"   s{s:<5} {det[0]:+12.3f} {det[1]:+7.3f} {det[2]:+7.3f} | {hp[0]:+12.3f} {hp[1]:+7.3f} {hp[2]:+7.3f}"
              f" | {pre:+9.3f} {post:+10.3f}")

    print("\nB. Partial-day effect: data end 09-24 16:30. Probe 7 per-day detrend, full vs 00:00-16:30")
    temps = temps_of(rows, 7)
    for day in ("2026-09-20", "2026-09-21", "2026-09-23"):
        start = datetime.fromisoformat(day).replace(tzinfo=P)
        out = []
        for hours in (24, 16.5):
            pairs = [(t, v, temps[t]) for t, v in W.window(sig[7], start, start + timedelta(hours=hours)) if t in temps]
            out.append(W.adjusted_slope([v for _, v, _ in pairs], [c for *_, c in pairs],
                                        [t.hour + t.minute / 60 for t, _, _ in pairs])[0])
        print(f"   {day}: full day {out[0]:+.3f}   truncated to 16:30 {out[1]:+.3f}")

    print("\nC. Probe 7: is its +2.99 'step' distinguishable from its night-to-night scatter?")
    values = [predawn(sig[7], d) for d in range(17, 25)]
    diffs = [b - a for a, b in zip(values, values[1:])]
    print("   predawn medians 09-17..09-24: " + "  ".join(f"{v:+.2f}" for v in values))
    others = [d for i, d in enumerate(diffs) if i != 5]
    print(f"   night-to-night change, no watering: sd {stats.pstdev(others):.2f}, range {min(others):+.2f}..{max(others):+.2f}")
    print(f"   night-to-night change across the watering: {diffs[5]:+.2f}")

    # The maintainer states (fleet.json pots.shared-alocasia.watering_events.uniform_saturation)
    # that the small Alocasia pot saturates uniformly, so the watering step IS the common
    # calibration. Dry-down calibrations are not used: drying rates differ between rigs, but
    # the watering input does not.
    print("\nD. Additivity, tested correctly. With every rig on the watering-step scale, an ADDITIVE")
    print("   model (artifact = readout term + electrode term) predicts the blade-minus-v1.2")
    print("   DIFFERENCE is the same through both readouts. A MULTIPLICATIVE model predicts the")
    print("   RATIO is the same, which is the test the review ran. Matched wet days 09-23/24.")
    step = {s: predawn(sig[s], 23) - predawn(sig[s], 22) for s in (3, 4, 5, 9)}
    share = {s: 100 * stats.median([day_span(sig[s], d) for d in WET]) / abs(step[s]) for s in step}
    for name, v12, blade in (("FDC", 4, 5), ("AM ", 3, 9)):
        print(f"   {name}: v1.2 {share[v12]:6.2f}%  blade {share[blade]:6.2f}%   "
              f"difference {share[blade] - share[v12]:+7.2f} pp   ratio x{share[blade] / share[v12]:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
