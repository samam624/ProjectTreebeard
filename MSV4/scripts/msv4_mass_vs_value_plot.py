"""Pot mass over time against ONE reading, converted to grams the way msv4_moisture_plot.py does.

Method (same family as msv4_moisture_plot.py, but one line PER WATERING CYCLE):
  1. Split the dry-dish weighings by watering cycle (from one watering to the next). Pair each with
     the hour of readings around it (the hour after, if weighed within 2 h of a watering).
  2. In each cycle fit its own straight line, reading = intercept + slope * grams. A least-squares
     line passes through the average of its points, so that cycle's reading is synced to the scale at
     the cycle's average weight (printed), with its own slope.
  3. Invert each line over its own cycle only: hourly median reading -> grams. The segments are drawn
     on the scale's axis against the weight curve. The span before the first watering is not drawn
     (two weighings, no cycle to fit).

Temperature correction is the repo's normal view: the number is as built (2 %/C), Cm is corrected
to 25 C by the tip coefficient fitted on the first settled dry day. --raw uses uncorrected Cm.

Two figures are written beside the input CSV:
  msv4_mass_vs_moisture_number.png    msv4_mass_vs_cm.png

    python MSV4/scripts/msv4_mass_vs_value_plot.py data/sensor_logs/<sensor10 history>.csv [--raw]
"""
from __future__ import annotations

import argparse
import statistics
from datetime import timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_tempcorr as tempcorr

NL = chr(10)


def _unused_fit_line(pairs, value, dry):
    points = []
    for p in pairs:
        data = [v for v in (value(r) for r in p["rows"]) if v is not None]
        if len(data) >= comp.MIN_PAIR_READINGS:
            points.append((p["grams"] - dry, statistics.median(data)))
    slope, intercept = statistics.linear_regression([g for g, _ in points], [v for _, v in points])
    return slope, intercept, len(points)


def figure(rows, merged, waterings, curves, title, out, note):
    fig, ax = plt.subplots(figsize=(14, 7))
    for i, w in enumerate(waterings):
        ax.axvline(w, color="#4a90d9", lw=1.2, ls="--", alpha=0.7, label="watering" if i == 0 else None)
    for label, line, color in curves:
        ax.plot([t for t, _ in line], [g for _, g in line], color=color, lw=1.3, alpha=0.9, label=label)
    ax.plot([w["time"] for w in merged], [w["grams"] for w in merged], color="black", lw=2.4, zorder=5,
            label="pot weight (scale, pot + dish)")
    for w in merged:
        if w["saucer"] is False:
            ax.errorbar(w["time"], w["grams"], yerr=max(w["spread"], 1) / 2, fmt="o", color="black", ms=5, zorder=6)
        else:
            ax.plot(w["time"], w["grams"], "o" if w["saucer"] else "x", mfc="white", color="black", ms=8, zorder=6)
    ax.set_ylabel("grams on the scale (hollow = water standing in the dish)")
    ax.set_title(title + NL + f"{rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}")
    ax.text(0.01, 0.01, note, transform=ax.transAxes, fontsize=8, va="bottom",
            bbox=dict(boxstyle="round", fc="white", ec="0.7", alpha=0.9))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=rows[0]["time"].tzinfo))
    ax.grid(alpha=0.25)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.1), ncol=4, fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--raw", action="store_true", help="uncorrected Cm instead of temperature-corrected")
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    if not rows:
        raise SystemExit("no MSV4 measurements with an air value in that file")
    sensor = rows[0]["sensor"]
    weighings, waterings = comp.load_pot(sensor)
    merged = comp.merge_repeats(weighings, waterings)
    days = comp.dry_days(rows, waterings)
    if not days:
        raise SystemExit("no settled dry day to fit the temperature coefficients on")
    corrected = {name: value for name, _, value in tempcorr.corrected_candidates(days[0])[0]}
    raw = {name: value for name, _, value in comp.CANDIDATES}
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    t0 = rows[0]["time"]
    bounds = list(waterings) + [rows[-1]["time"] + timedelta(hours=1)]
    out_dir = args.out_dir or args.csv.parent

    def cycle_curves(label, value, color, unit):
        """One segment per watering cycle, each from its own line; returns curves and the notes."""
        curves, notes = [], []
        for k in range(len(waterings)):
            lo, hi = bounds[k], bounds[k + 1]
            cyc = [p for p in pairs if lo <= p["time"] < hi]
            pts = []
            for p in cyc:
                data = [v for v in (value(r) for r in p["rows"]) if v is not None]
                if len(data) >= comp.MIN_PAIR_READINGS:
                    pts.append((p["grams"], statistics.median(data)))
            if len(pts) < 3:
                continue
            gs, vs = [g for g, _ in pts], [v for _, v in pts]
            slope, intercept = statistics.linear_regression(gs, vs)
            line = [(t0 + timedelta(hours=h + 0.5), (m - intercept) / slope)
                    for h, m, _ in comp.hourly(rows, value) if lo <= t0 + timedelta(hours=h + 0.5) < hi]
            curves.append((label if not curves else None, line, color))
            notes.append(f"cycle from {lo:%m-%d %H:%M}: {len(pts)} weighings, synced at {statistics.mean(gs):.0f} g = "
                         f"{statistics.mean(vs):,.1f} {unit}, slope {slope:.4g} {unit}/g")
        return curves, notes

    for name, value, label, color, unit, title, file in (
        ("number", corrected["moisture number in use (as built, 2%/C)"], "moisture number in use (as built), as grams",
         "#1f77b4", "units", "the moisture number", "msv4_mass_vs_moisture_number.png"),
        ("Cm", raw["contact capacitance Cm"] if args.raw else corrected["contact capacitance Cm + fitted temp"],
         "Cm " + ("raw" if args.raw else "corrected to 25 C") + ", as grams", "#d95f02", "pF", "Cm", "msv4_mass_vs_cm.png"),
    ):
        curves, notes = cycle_curves(label, value, color, unit)
        for line in notes:
            print(name, line)
        figure(rows, merged, waterings, curves, f"Sensor {sensor}: pot weight and {title}, one line per watering cycle",
               out_dir / file, "each cycle fitted on its own weighings (synced at its own average):" + NL + NL.join(notes))


if __name__ == "__main__":
    main()
