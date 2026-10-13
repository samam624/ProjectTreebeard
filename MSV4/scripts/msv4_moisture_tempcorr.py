"""Re-run the moisture competition with every candidate temperature-corrected.

The moisture number in use is corrected with the tip thermistor; the other candidates are not,
so the plain competition compares one corrected candidate with six raw ones. Here each raw
candidate gets its own coefficient, fitted on the settled dry day(s) only (candidate = a + b*hours
+ k*(tip - 25), the linear trend in hours standing for the pot drying), and corrected = value -
k*(tip - 25). The moisture number in use is left as built (its 2 %/C is fixed, not fitted).

The weighings, which sit mostly outside that dry day, score the corrected candidates out of
sample; the daily-wobble column is scored on the very day the coefficient was fitted on, so it
is flattering and is printed as such.

    python MSV4/scripts/msv4_moisture_tempcorr.py data/sensor_logs/<sensor10 history>.csv
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

REF_C = 25.0


def fit_k(day: list[dict], value) -> tuple[float, float, int] | None:
    xs = [(r, value(r)) for r in day if r["tip"] is not None and value(r) is not None]
    if len(xs) < 200:
        return None
    t0 = xs[0][0]["time"]
    hours = np.array([(r["time"] - t0).total_seconds() / 3600 for r, _ in xs])
    tip = np.array([r["tip"] for r, _ in xs]) - REF_C
    y = np.array([v for _, v in xs])
    a = np.column_stack([np.ones_like(hours), hours, tip])
    coef, *_ = np.linalg.lstsq(a, y, rcond=None)
    resid = y - a @ coef
    cov = np.linalg.inv(a.T @ a) * resid.var(ddof=3)
    return float(coef[2]), float(np.sqrt(cov[2, 2])), len(xs)


def corrected_candidates(day: list[dict]):
    """(candidates with each raw one temperature-corrected, [(name, unit, k, se)] for the fitted ones)."""
    corrected, fits = [], []
    for name, unit, value in comp.CANDIDATES:
        if name == "moisture number in use":
            corrected.append((name + " (as built, 2%/C)", unit, value))
            continue
        fit = fit_k(day, value)
        if fit is None:
            continue
        k, se, _ = fit
        fits.append((name, unit, k, se))
        corrected.append((name + " + fitted temp", unit,
                          lambda r, v=value, k=k: (v(r) - k * (r["tip"] - REF_C)) if v(r) is not None and r["tip"] is not None else None))
    return tuple(corrected), fits


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    days = comp.dry_days(rows, waterings)
    if not days:
        raise SystemExit("no settled dry day to fit the coefficients on")
    day = days[0]
    tips = [r["tip"] for r in day if r["tip"] is not None]
    print(f"fit day {day[0]['time']:%m-%d %H:%M} to {day[-1]['time']:%m-%d %H:%M}, tip {min(tips):.2f} to {max(tips):.2f} C "
          f"({len(day)} readings)\n")
    original = comp.CANDIDATES
    corrected, fits = corrected_candidates(day)
    print(f"{'candidate':44} {'k per C':>12} {'(se)':>8}   per C in grams of water")
    for name, unit, k, se in fits:
        print(f"{name:44} {k:12.4g} {se:8.2g}   ({unit}/C)")
    print("\n===== BEFORE: as in the plain competition =====")
    comp.report(rows, weighings, waterings)
    comp.CANDIDATES = tuple(corrected)
    print("\n===== AFTER: each raw candidate corrected with its own fitted coefficient =====")
    print("(daily wobble is in-sample for the corrected candidates: the coefficient was fitted on that day)")
    comp.report(rows, weighings, waterings)


if __name__ == "__main__":
    main()
