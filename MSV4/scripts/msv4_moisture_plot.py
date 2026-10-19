"""Pot weight over time with every moisture candidate laid over it, in grams of water.

Each candidate is a different unit, so each is converted the way msv4_moisture_competition.py
scores it: a straight line fitted through the scored weighings (candidate vs grams above the
driest weighing), then inverted, so a curve reads "the grams of water this candidate says are in
the pot". A candidate that tracked the scale perfectly would lie on the black line. Candidates are
hourly medians; the black line joins the weighings (repeats merged to their median, bars show the
scale's spread). Hollow marker = water standing in the saucer (not scored); x = saucer unknown.

By default this writes ``msv4_moisture_vs_mass.png`` beside the input CSV and replaces the
previous plot. The temperature-corrected view is the normal view; use ``--raw`` only when the
uncorrected diagnostic comparison is wanted.

    python MSV4/scripts/msv4_moisture_plot.py data/sensor_logs/<sensor10 history>.csv [-o out.png] [--raw]
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

COLORS = ("#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#17becf")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("-o", "--out", type=Path)
    parser.add_argument("--with-saucer", action="store_true", help="fit through weighings with standing saucer water too")
    correction = parser.add_mutually_exclusive_group()
    correction.add_argument("--corrected", dest="corrected", action="store_true", default=True,
                            help="temperature-correct every raw candidate first (default)")
    correction.add_argument("--raw", dest="corrected", action="store_false",
                            help="plot the uncorrected diagnostic candidates instead")
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    if not rows:
        raise SystemExit("no MSV4 measurements with an air value in that file")
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    if args.corrected:
        import msv4_moisture_tempcorr as tempcorr
        days = comp.dry_days(rows, waterings)
        if not days:
            raise SystemExit("no settled dry day to fit the temperature coefficients on")
        comp.CANDIDATES = tempcorr.corrected_candidates(days[0])[0]
    merged = comp.merge_repeats(weighings, waterings)
    listed = comp.pair(rows, merged, waterings)
    pairs = [p for p in listed if not p["excluded"] and (args.with_saucer or p["saucer"] is False)]
    dry = min(p["grams"] for p in pairs)
    t0 = rows[0]["time"]

    fig, ax = plt.subplots(figsize=(14, 9))
    for i, w in enumerate(waterings):
        ax.axvline(w, color="#4a90d9", lw=1.2, ls="--", alpha=0.7, label="watering" if i == 0 else None)
    for (name, unit, value), color in zip(comp.CANDIDATES, COLORS):
        points = []
        for p in pairs:
            data = [v for v in (value(r) for r in p["rows"]) if v is not None]
            if len(data) >= comp.MIN_PAIR_READINGS:
                points.append((p["grams"] - dry, statistics.median(data)))
        if len({round(g / comp.LEVEL_GRAMS) for g, _ in points}) < 2:
            continue
        slope, intercept = statistics.linear_regression([g for g, _ in points], [v for _, v in points])
        if slope <= 0:
            continue
        hours = comp.hourly(rows, value)
        ax.plot([t0 + timedelta(hours=h + 0.5) for h, _, _ in hours],
                [(m - intercept) / slope for _, m, _ in hours], color=color, lw=1.1, alpha=0.85,
                label=name + (" [inverted: raw ohms rise as it dries]" if "resistance" in name else ""))
    xs = [w["time"] for w in merged]
    ys = [w["grams"] - dry for w in merged]
    ax.plot(xs, ys, color="black", lw=2.6, zorder=5, label="pot weight (scale)")
    for w in merged:
        y = w["grams"] - dry
        if w["saucer"] is False:
            ax.errorbar(w["time"], y, yerr=max(w["spread"], 1) / 2, fmt="o", color="black", ms=6, zorder=6)
        else:
            ax.plot(w["time"], y, "o" if w["saucer"] else "x", mfc="white", color="black", ms=8, zorder=6)
    ax.set_ylabel(f"water in pot, g above the driest weighing ({dry:.0f} g)")
    sec = ax.secondary_yaxis("right", functions=(lambda g: g + dry, lambda g: g - dry))
    sec.set_ylabel("scale reading, g (pot + saucer)")
    ax.set_title(f"Sensor {rows[0]['sensor']}: pot weight vs every candidate{', temperature-corrected' if args.corrected else ''}, each converted to grams of water"
                 f"\n{t0:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}   (dashed blue = watering)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d %H:%M", tz=rows[0]["time"].tzinfo))
    fig.autofmt_xdate()
    ax.grid(alpha=0.25)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.12), fontsize=8, ncol=2, frameon=False)
    fig.tight_layout()
    out = args.out or args.csv.with_name("msv4_moisture_vs_mass.png")
    fig.savefig(out, dpi=130)
    print(out)


if __name__ == "__main__":
    main()
