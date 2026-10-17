"""The capacitance candidates with their temperature term fitted to the weighings, not a dry day.

msv4_moisture_tempcorr.py fits each candidate's temperature coefficient on one settled dry day,
with a straight line in time standing for the drying. Day/night drying follows the sun as the
temperature does, so that fit cannot tell them apart, and for the capacitances it comes out about
2.5 times too weak against the scale (msv4_moisture_slope_diagnosis.py, section 3). This refits k
from the changes between consecutive weighings since the watering (change in candidate =
s * grams + k * change in tip, no intercept) and draws both corrections against the scale.

Both curves in each panel are converted to grams the same way, by a straight line through the
weighings since the watering, so the only difference between them is the temperature term.
The 'left out' miss refits k and the line without the weighing it predicts.

    python MSV4/scripts/msv4_thermal_refit_plot.py data/sensor_logs/<sensor10 history>.csv [-o out.png]
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
import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_slope_diagnosis as diag
import msv4_moisture_tempcorr as tempcorr

CAPACITANCES = ("direct capacitance above air", "contact capacitance Cm",
                "soil-coupled capacitance (Cm + Cd - air)", "soil capacitance (derived)")
INK, MUTED, GRID = "#0b0b0b", "#8a8984", "#e4e3df"
NEW = "#2a78d6"


def corrected(points: list[dict], k: float) -> list[dict]:
    return [{**p, "c": p["c"] - k * (p["tip"] - tempcorr.REF_C)} for p in points]


def line(points: list[dict]) -> tuple[float, float]:
    """(intercept, per gram) of candidate against grams."""
    s, a = np.polyfit([p["g"] for p in points], [p["c"] for p in points], 1)
    return a, s


def miss(raw: list[dict], k_of) -> float:
    """rms grams at the weighings; k_of(points) gives k, the line is fitted on the same points."""
    k = k_of(raw)
    pts = corrected(raw, k)
    a, s = line(pts)
    return float(np.sqrt(np.mean([((p["c"] - a) / s - p["g"]) ** 2 for p in pts])))


def left_out_miss(raw: list[dict], k_of) -> float:
    errs = []
    for i in range(len(raw)):
        rest = raw[:i] + raw[i + 1:]
        k = k_of(rest)
        a, s = line(corrected(rest, k))
        p = corrected([raw[i]], k)[0]
        errs.append((p["c"] - a) / s - p["g"])
    return float(np.sqrt(np.mean(np.square(errs))))


def interval_k(points: list[dict]) -> float:
    return diag.interval_fit(points)[1]


def interval_miss(points: list[dict], k: float) -> float:
    """rms grams between consecutive weighings, with k fixed and only the slope refitted."""
    dg = np.array([b["g"] - a["g"] for a, b in zip(points, points[1:])])
    dt = np.array([b["tip"] - a["tip"] for a, b in zip(points, points[1:])])
    dc = np.array([b["c"] - a["c"] for a, b in zip(points, points[1:])]) - k * dt
    s = float(dc @ dg / (dg @ dg))
    return float(np.sqrt(np.mean((dc / s - dg) ** 2)))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("-o", "--out", type=Path)
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    _, waterings = comp.load_pot(rows[0]["sensor"])
    day_k = {name: k for name, _, k, _ in tempcorr.corrected_candidates(comp.dry_days(rows, waterings)[0])[1]}
    start = waterings[-1] + timedelta(hours=comp.WATER_SETTLE_HOURS)
    tz = rows[0]["time"].tzinfo
    window = [r for r in rows if r["time"] >= start and r["tip"] is not None]

    fig = plt.figure(figsize=(14, 10.5))
    grid = fig.add_gridspec(3, 2, height_ratios=(1, 1, 0.45), hspace=0.42, wspace=0.12)
    axes = [fig.add_subplot(grid[i // 2, i % 2]) for i in range(4)]
    strip = fig.add_subplot(grid[2, :], sharex=axes[0])
    print(f"{'candidate':42} {'k dry day':>10} {'k weighings':>12} {'g/C':>12} "
          f"{'miss old':>9} {'miss new':>9} {'left out old':>13} {'left out new':>13} {'between, old->new':>18}")
    for ax, name in zip(axes, CAPACITANCES):
        value = next(v for n, _, v in comp.CANDIDATES if n == name)
        points, _ = diag.weighed_points(rows, value)
        after = [p for p in points if p["after"]]
        k_old, k_new = day_k[name], interval_k(after)
        s = line(corrected(after, k_new))[1]
        scores = (miss(after, lambda _: k_old), miss(after, interval_k),
                  left_out_miss(after, lambda _: k_old), left_out_miss(after, interval_k))
        print(f"{name:42} {k_old:+10.3f} {k_new:+12.3f} {k_old / s:+5.1f}->{k_new / s:+5.1f} "
              f"{scores[0]:9.1f} {scores[1]:9.1f} {scores[2]:13.1f} {scores[3]:13.1f} "
              f"{interval_miss(after, k_old):8.1f} -> {interval_miss(after, k_new):5.1f}")

        for k, colour, width, label in ((k_old, MUTED, 1.2, f"dry-day fit, {k_old / s:+.1f} g/°C"),
                                        (k_new, NEW, 2.0, f"fitted to weighings, {k_new / s:+.1f} g/°C")):
            a, sk = line(corrected(after, k))
            hours: dict = {}
            for r in window:
                v = value(r)
                if v is not None:
                    hours.setdefault(r["time"].replace(minute=0, second=0, microsecond=0), []).append(
                        (v - k * (r["tip"] - tempcorr.REF_C) - a) / sk)
            xs = sorted(h for h, vs in hours.items() if len(vs) >= comp.MIN_PAIR_READINGS)
            ax.plot(xs, [statistics.median(hours[h]) for h in xs], color=colour, lw=width, label=label)
        ax.plot([p["time"] for p in after], [p["g"] for p in after], color=INK, lw=2.6, marker="o", ms=5,
                label="scale", zorder=5)
        ax.set_title(f"{name}\nmiss at weighings, left out: {scores[2]:.0f} g -> {scores[3]:.0f} g",
                     fontsize=10.5, color=INK, loc="left")
        ax.legend(loc="upper right", fontsize=8.5, frameon=False)
        ax.set_ylim(-60, 470)

    tip_hours: dict = {}
    for r in window:
        tip_hours.setdefault(r["time"].replace(minute=0, second=0, microsecond=0), []).append(r["tip"])
    xs = sorted(tip_hours)
    strip.plot(xs, [statistics.median(tip_hours[h]) for h in xs], color=INK, lw=1.4)
    strip.set_ylabel("tip, °C", color=INK)
    strip.set_title("blade tip temperature (hourly median)", fontsize=10.5, color=INK, loc="left")

    for ax in axes + [strip]:
        ax.grid(color=GRID, lw=0.8)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(MUTED)
        ax.tick_params(colors="#52514e", labelsize=9)
        ax.xaxis.set_major_locator(mdates.DayLocator(tz=tz))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=tz))
    for ax in axes[0::2]:
        ax.set_ylabel("water in pot, g above driest weighing", color=INK, fontsize=9.5)
    fig.suptitle(f"Sensor 10: capacitance candidates, temperature term from a dry day vs from the weighings\n"
                 f"since the {waterings[-1]:%m-%d %H:%M} watering (+{comp.WATER_SETTLE_HOURS} h), "
                 f"each converted to grams by a line through the weighings since then",
                 fontsize=12, color=INK)
    out = args.out or args.csv.parent / "msv4_thermal_refit.png"
    fig.savefig(out, dpi=110, bbox_inches="tight", facecolor="#fcfcfb")
    print(out)


if __name__ == "__main__":
    main()
