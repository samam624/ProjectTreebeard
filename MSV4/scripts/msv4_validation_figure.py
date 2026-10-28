"""One slide-ready figure: the moisture reading against the weight of the pot, tested out of sample.

Each watering cycle's readings are converted to grams with a line fitted ONLY on the other cycle's
weighings, so no weighing is scored by a calibration that saw it. Left: pot weight and the converted
reading over time. Right: converted reading against the scale for every weighing (on the dashed
line = exact). The dry-dish weighings are the 42 scored ones of msv4_moisture_competition.py.

    python MSV4/scripts/msv4_validation_figure.py data/sensor_logs/<sensor10 history>.csv [-o out.png]
"""
from __future__ import annotations

import argparse
import math
import statistics as st
from datetime import timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

INK, MUTED, GRID, BLUE = "#1a1a1a", "#666666", "#e3e3e3", "#2563a8"
NL = chr(10)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    args = ap.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    sensor = rows[0]["sensor"]
    weighings, waterings = comp.load_pot(sensor)
    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    value = lambda r: r["stored"] if not r["open"] else None
    t0 = rows[0]["time"]
    bounds = list(waterings) + [rows[-1]["time"] + timedelta(hours=1)]
    cycles = [(bounds[k], bounds[k + 1]) for k in range(len(waterings))]

    def points(lo, hi):
        out = []
        for p in pairs:
            if lo <= p["time"] < hi:
                d = [x for x in map(value, p["rows"]) if x is not None]
                if len(d) >= comp.MIN_PAIR_READINGS:
                    out.append((p["time"], p["grams"], st.median(d)))
        return out

    pts = [points(lo, hi) for lo, hi in cycles]
    lines = [st.linear_regression([g for _, g, _ in pp], [v for _, _, v in pp]) for pp in pts]   # slope, intercept
    hourly = [(t0 + timedelta(hours=h + 0.5), m) for h, m, _ in comp.hourly(rows, value)]

    fig, (ax, bx) = plt.subplots(1, 2, figsize=(15, 6.6), gridspec_kw={"width_ratios": [1.6, 1]})
    fig.patch.set_facecolor("white")
    errors, scatter = [], []
    for k, (lo, hi) in enumerate(cycles):
        other = lines[1 - k] if len(cycles) == 2 else lines[k]
        slope, intercept = other
        ax.plot([t for t, m in hourly if lo <= t < hi], [(m - intercept) / slope for t, m in hourly if lo <= t < hi],
                color=BLUE, lw=1.6, zorder=3, label="moisture reading, converted to grams" if k == 0 else None)
        for t, g, v in pts[k]:
            e = (v - intercept) / slope - g
            errors.append(e)
            scatter.append((g, g + e, k))
    for w in waterings:
        ax.axvline(w, color=MUTED, lw=1, ls=(0, (4, 3)), zorder=1)
        ax.text(w, 0.985, " watering", transform=ax.get_xaxis_transform(), color=MUTED, fontsize=10, va="top")
    ax.plot([w["time"] for w in merged if w["saucer"] is False], [w["grams"] for w in merged if w["saucer"] is False],
            color=INK, lw=1.2, marker="o", ms=5, zorder=4, label="weight of the pot (scale)")
    ax.set_ylabel("grams on the scale (pot + dish)", color=INK, fontsize=11)
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d", tz=rows[0]["time"].tzinfo))
    ax.grid(color=GRID, lw=0.8)
    ax.set_axisbelow(True)
    ax.legend(loc="upper right", bbox_to_anchor=(1.0, 0.9), frameon=False, fontsize=11)
    ax.set_title("Over time", loc="left", fontsize=12, color=INK)

    lo_g = min(g for g, _, _ in scatter) - 20
    hi_g = max(g for g, _, _ in scatter) + 20
    bx.fill_between([lo_g, hi_g], [lo_g - 40, hi_g - 40], [lo_g + 40, hi_g + 40], color=BLUE, alpha=0.10, lw=0, label="within 40 g")
    bx.plot([lo_g, hi_g], [lo_g, hi_g], color=MUTED, lw=1, ls=(0, (4, 3)))
    for k, mk in ((0, "o"), (1, "s")):
        sel = [(g, p) for g, p, c in scatter if c == k]
        bx.scatter([g for g, _ in sel], [p for _, p in sel], s=34, marker=mk, color=BLUE, edgecolor="white", lw=0.8,
                   zorder=3, label=f"cycle {k + 1} (calibrated on cycle {2 - k})")
    bx.set_xlim(lo_g, hi_g); bx.set_ylim(lo_g, hi_g); bx.set_aspect("equal")
    bx.set_xlabel("weight of the pot (scale), g", fontsize=11)
    bx.set_ylabel("reading converted to grams, g", fontsize=11)
    bx.grid(color=GRID, lw=0.8); bx.set_axisbelow(True)
    bx.legend(loc="upper left", frameon=False, fontsize=10)
    bx.set_title("Every weighing", loc="left", fontsize=12, color=INK)
    for a in (ax, bx):
        for s in ("top", "right"):
            a.spines[s].set_visible(False)
        a.tick_params(colors=MUTED, labelsize=10)

    rms = math.sqrt(st.mean(e * e for e in errors))
    inside = sum(abs(e) <= 40 for e in errors)
    fig.suptitle("Does the moisture reading follow the water in the pot?  Tested against the scale", x=0.01, ha="left",
                 fontsize=15, color=INK, fontweight="bold")
    fig.text(0.01, 0.905,
             f"Typical miss {rms:.0f} g (rms), {inside} of {len(errors)} weighings within 40 g, on a watering of about 460 g. "
             "Each cycle is converted with a calibration fitted only on the other cycle.",
             fontsize=11.5, color=MUTED, ha="left")
    fig.text(0.01, 0.015,
             f"Sensor {sensor} (MSV4), one Alocasia pot, {rows[0]['time']:%b %d} to {rows[-1]['time']:%b %d %Y}. "
             "Weighings: kitchen scale, pot + dish, no water standing in the dish; the scale repeats to about 8 g." + NL +
             "Reading: the moisture number the sensor reports (temperature-corrected at 2 %/C).",
             fontsize=9, color=MUTED, ha="left", va="bottom")
    fig.tight_layout(rect=(0, 0.06, 1, 0.9))
    out = args.out or args.csv.with_name("msv4_moisture_validation.png")
    fig.savefig(out, dpi=160)
    print(out, f"rms {rms:.1f} g, {inside}/{len(errors)} within 40 g")


if __name__ == "__main__":
    main()
