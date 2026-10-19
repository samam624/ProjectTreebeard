"""One clean figure: the pot's weighed water against the soil capacitance and the soil resistance.

The presentation version of msv4_moisture_plot.py. Each candidate is temperature-corrected by
msv4_moisture_tempcorr.py (k fitted on the first settled dry day) and converted exactly as
msv4_moisture_competition.py scores it: a straight line fitted through the dry-saucer weighings
(corrected candidate vs grams above the driest weighing), inverted, so each curve reads "grams of
water this reading says are in the pot". Curves are hourly medians; the scale is the dry-saucer
weighings joined (repeats merged to their median). The temperature correction is printed on the
figure with the fitted coefficients.

    python MSV4/scripts/msv4_moisture_clean_plot.py data/sensor_logs/<sensor10 history>.csv -o out.png
"""
from __future__ import annotations

import argparse
import math
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

# dataviz reference palette, light mode, categorical slots 1-3 in order (validated; slot 3 is under
# 3:1 on the surface, so every line is also labelled directly and resistance is dashed).
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e6e5e1"
SCALE_COLOR = "#2a78d6"
SERIES = (
    # (candidate name prefix, label, colour, line style)
    ("soil capacitance (derived)", "soil capacitance", "#eb6834", "-"),
    ("soil resistance, no temp. correction", "soil resistance", "#1baf7a", (0, (5, 2.5))),
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("-o", "--out", type=Path, required=True)
    args = parser.parse_args()

    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    sensor = rows[0]["sensor"]
    weighings, waterings = comp.load_pot(sensor)
    fit_day = comp.dry_days(rows, waterings)[0]
    corrected, fits = tempcorr.corrected_candidates(fit_day)
    k_of = {name: k for name, _, k, _ in fits}

    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    dry = min(p["grams"] for p in pairs)
    t0 = rows[0]["time"]
    # From the last watering on: the step itself is a vertical jump that would dominate the figure,
    # and for WATER_SETTLE_HOURS the blade reads water running past it, not the pot.
    watered = waterings[-1] if waterings else None
    begin = t0 if watered is None else watered + timedelta(hours=comp.WATER_SETTLE_HOURS)
    scale = [w for w in merged if w["saucer"] is False and w["time"] >= begin]

    curves = []
    for prefix, label, color, style in SERIES:
        name, _, value = next(c for c in corrected if c[0].startswith(prefix))
        points = []
        for p in pairs:
            data = [v for v in (value(r) for r in p["rows"]) if v is not None]
            if len(data) >= comp.MIN_PAIR_READINGS:
                points.append((p["grams"] - dry, statistics.median(data)))
        a, b = statistics.linear_regression([g for g, _ in points], [v for _, v in points])
        miss = math.sqrt(statistics.fmean(((v - b) / a - g) ** 2 for g, v in points))
        hours = [(t0 + timedelta(hours=h + 0.5), (m - b) / a) for h, m, _ in comp.hourly(rows, value)]
        hours = [(t, g) for t, g in hours if t >= begin]
        curves.append({"label": label, "color": color, "style": style, "hours": hours, "miss": miss,
                       "k": k_of[prefix], "n": len(points)})

    plt.rcParams.update({"font.family": "Segoe UI", "font.size": 11, "mathtext.fontset": "custom",
                         "mathtext.rm": "Segoe UI", "mathtext.it": "Segoe UI:italic",
                         "mathtext.bf": "Segoe UI:bold"})
    fig, ax = plt.subplots(figsize=(12, 6.4), facecolor=SURFACE)
    ax.set_facecolor(SURFACE)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(INK_2)
        ax.spines[spine].set_linewidth(0.8)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.tick_params(colors=INK_2, labelsize=10, length=0)

    for c in curves:
        ax.plot([t for t, _ in c["hours"]], [g for _, g in c["hours"]], color=c["color"], lw=2.2, ls=c["style"],
                zorder=3, label=f"{c['label'].capitalize()}, temperature-corrected  (RMS miss {c['miss']:.0f} g)")
    ax.plot([w["time"] for w in scale], [w["grams"] - dry for w in scale], color=SCALE_COLOR, lw=2.2, zorder=4,
            marker="o", ms=8, mec=SURFACE, mew=2, label="Pot on the scale")

    # Direct labels at the right-hand ends, pushed apart where the lines end close together. The
    # text stays in ink; a short stroke in the series colour carries the identity.
    end = max(max(c["hours"][-1][0] for c in curves), scale[-1]["time"])
    ends = [(c["hours"][-1][1], c["label"], c["color"], c["style"]) for c in curves]
    ends.append((scale[-1]["grams"] - dry, "scale", SCALE_COLOR, "-"))
    ends.sort()
    values = [g for c in curves for _, g in c["hours"]] + [w["grams"] - dry for w in scale]
    lo = math.floor((min(values) - 15) / 25) * 25
    hi = math.ceil((max(values) + 25) / 25) * 25
    gap = (hi - lo) * 0.045
    placed = []
    for y, label, color, style in ends:
        y = max(y, placed[-1][0] + gap) if placed else y
        placed.append((y, label, color, style))
    x_key = end + timedelta(hours=2)
    for y, label, color, style in placed:
        ax.plot([x_key, x_key + timedelta(hours=3)], [y, y], color=color, lw=2.2, ls=style, clip_on=False)
        ax.text(x_key + timedelta(hours=4), y, label, color=INK, fontsize=10, va="center", ha="left")

    ax.set_ylabel(f"Water in the pot, g above the driest weighing ({dry:.0f} g)", color=INK_2, fontsize=10.5)
    ax.xaxis.set_major_locator(mdates.DayLocator(tz=t0.tzinfo))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d", tz=t0.tzinfo))
    ax.xaxis.set_minor_locator(mdates.HourLocator(byhour=(6, 12, 18), tz=t0.tzinfo))
    ax.tick_params(axis="x", which="minor", length=3, color=GRID)
    ax.set_xlim(begin - timedelta(hours=3), end + timedelta(hours=3))
    ax.set_ylim(lo, hi)

    kc = next(c["k"] for c in curves if "capacitance" in c["label"])
    kr = next(c["k"] for c in curves if "resistance" in c["label"])
    per_c = (1.0 - 10.0 ** (-kr / 1e4)) * 100.0
    day = f"{fit_day[0]['time']:%b %d %H:%M} to {fit_day[-1]['time']:%b %d %H:%M}"
    equations = "\n".join((
        r"$\mathbf{Temperature\ correction}$  ($T$ = tip thermistor, °C)",
        rf"$C_{{25}} = C - k_C\,(T - 25)$,   $k_C$ = {kc:.3f} pF/°C",
        rf"$R_{{25}} = R \cdot 10^{{\,k_R\,(T - 25)\,/\,10^4}}$,   $k_R$ = {kr:.1f}   ($R$ falls {per_c:.1f} %/°C)",
        rf"$k$ fitted on the dry day {day}:  reading $= a + b\,t + k\,(T - 25)$",
        r"grams of water $= (\mathrm{reading}_{25} - \beta)\,/\,\alpha$, a straight line through the weighings",
    ))
    ax.text(0.015, 0.03, equations, transform=ax.transAxes, color=INK_2, fontsize=10, va="bottom", ha="left",
            linespacing=1.7)

    fig.text(0.06, 0.95, f"Sensor {sensor}: soil capacitance and soil resistance against the weighed water",
             color=INK, fontsize=15, fontweight="semibold", ha="left")
    ax.legend(loc="upper right", frameon=False, fontsize=10, labelcolor=INK)
    fig.subplots_adjust(left=0.06, right=0.86, top=0.89, bottom=0.08)
    fig.savefig(args.out, dpi=200, facecolor=SURFACE)
    for c in curves:
        print(f"{c['label']}: k {c['k']:.4f}, RMS miss {c['miss']:.1f} g over {c['n']} weighings")
    print(args.out)


if __name__ == "__main__":
    main()
