"""Figures and phase table for MSV4 board 2's cup-of-water and salt test of 2026-10-14.

Phases are the maintainer's stated moves (MSV3/fleet.json msv4_boards board 2 active_test), as
PDT clock times. Each phase's value is the median over its settled window, the minutes before the
next move. The warm bath that followed (from ~15:33) is left out: it changed the coupling one way and
did not come back as the cup cooled.

    python MSV4/scripts/msv4_cup_test_plot.py data/sensor_logs/msv4_sensor11_cuptest_2026-10-14.csv --out-dir data/sensor_logs
"""
from __future__ import annotations

import argparse
import math
import statistics
from datetime import datetime, time
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading

DAY = "2026-10-14"
# (name, start, end, settled-window start): times PDT; a phase runs [start, end).
PHASES = (
    ("Brita", "12:40", "14:16", "14:04"),
    ("Tap", "14:16", "14:39", "14:26"),
    ("1 pinch", "14:39", "14:57", "14:45"),
    ("2 pinches", "14:57", "15:08", "15:00"),
    ("3 pinches", "15:08", "15:33:30", "15:20"),
)
AIR = ("12:09", "12:40")
REFERENCE = "Tap"

SURFACE, INK, INK_2, GRID, BAND = "#fcfcfb", "#0b0b0b", "#52514e", "#e6e5e1", "#f0efec"
# Colour follows the metric in every figure (dataviz reference palette, slots 1-6 in order).
METRICS = (
    # key, label, unit, colour, marker
    ("coupled", "soil-coupled  Cm + Cd − air", "pF", "#2a78d6", "o"),
    ("rsoil", "Rsoil", "Ω", "#eb6834", "s"),
    ("cm", "Cm", "pF", "#1baf7a", "^"),
    ("cd", "Cd", "pF", "#eda100", "D"),
    ("csoil", "soil capacitance (derived)", "pF", "#e87ba4", "v"),
    ("rmask", "Rsoil behind the mask", "Ω", "#008300", "P"),
)
PHASE_COLORS = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4")


def at(clock: str, tz) -> datetime:
    return datetime.fromisoformat(f"{DAY}T{clock if clock.count(':') == 2 else clock + ':00'}").replace(tzinfo=tz)


def derived(r: dict) -> dict:
    cx = r["cd"] - r["cd_air"]
    total = r["cm"] + cx
    return {"coupled": total, "rsoil": r["rsoil"], "cm": r["cm"], "cd": r["cd"],
            "csoil": cx * (1.0 + cx / r["cm"]), "rmask": r["rsoil"] * (r["cm"] / total) ** 2, "tip": r["tip"]}


def style(ax) -> None:
    ax.set_facecolor(SURFACE)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(INK_2)
        ax.spines[s].set_linewidth(0.8)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.tick_params(colors=INK_2, labelsize=9.5, length=0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["cd"] is not None and r["cm"] and r["cd_air"] is not None]
    tz = rows[0]["time"].tzinfo
    phases = []
    for name, a, b, settle in PHASES:
        inside = [(r["time"], derived(r)) for r in rows if at(a, tz) <= r["time"] < at(b, tz)]
        settled = [d for t, d in inside if t >= at(settle, tz)]
        phases.append({"name": name, "start": at(a, tz), "end": at(b, tz), "rows": inside, "n": len(settled),
                       "med": {k: statistics.median(d[k] for d in settled) for k in settled[0]}})
    air = [derived(r) for r in rows if at(AIR[0], tz) <= r["time"] < at(AIR[1], tz)]
    air_med = {k: statistics.median(d[k] for d in air) for k in air[0]}
    ref = next(p for p in phases if p["name"] == REFERENCE)["med"]

    # Table.
    print(f"{'phase':10} {'n':>3} {'tip C':>6} " + " ".join(f"{m[1].split()[0][:12]:>12}" for m in METRICS))
    print(f"{'air':10} {len(air):3d} {air_med['tip']:6.2f} " + " ".join(f"{air_med[m[0]]:12.1f}" for m in METRICS))
    for p in phases:
        print(f"{p['name']:10} {p['n']:3d} {p['med']['tip']:6.2f} " + " ".join(f"{p['med'][m[0]]:12.1f}" for m in METRICS))
    print("relative to tap, %:")
    for p in phases:
        print(f"{p['name']:10} " + " ".join(f"{100 * p['med'][m[0]] / ref[m[0]]:12.0f}" for m in METRICS))

    plt.rcParams.update({"font.family": "Segoe UI", "font.size": 10.5})
    args.out_dir.mkdir(parents=True, exist_ok=True)

    # 1. Timeline, small multiples on a shared time axis.
    fig, axes = plt.subplots(3, 1, figsize=(12, 8.6), sharex=True, facecolor=SURFACE,
                             gridspec_kw={"height_ratios": [1, 1.5, 0.7]})
    series = [("Rsoil, Ω", (METRICS[1],)), ("Capacitance, pF", (METRICS[0], METRICS[2], METRICS[3])),
              ("Tip, °C", ())]
    for ax, (ylabel, mets) in zip(axes, series):
        style(ax)
        for i, p in enumerate(phases):
            if i % 2 == 0:
                ax.axvspan(p["start"], p["end"], color=BAND, zorder=0, lw=0)
        times = [t for p in phases for t, _ in p["rows"]]
        if not mets:
            ax.plot(times, [d["tip"] for p in phases for _, d in p["rows"]], color=INK_2, lw=1.8)
        for key, label, _, color, _ in mets:
            ys = [d[key] for p in phases for _, d in p["rows"]]
            ax.plot(times, ys, color=color, lw=2.0 if key != "coupled" else 2.6, label=label)
            if len(mets) > 1:
                ax.text(times[-1], ys[-1], "  " + label.split("  ")[0], color=INK, fontsize=9.5, va="center")
        ax.set_ylabel(ylabel, color=INK_2, fontsize=10)
    for p in phases:
        mid = p["start"] + (p["end"] - p["start"]) / 2
        axes[0].text(mid, 1.02, p["name"], transform=axes[0].get_xaxis_transform(), ha="center", va="bottom",
                     color=INK, fontsize=10)
    axes[1].set_ylim(0, max(d["coupled"] for p in phases for _, d in p["rows"]) * 1.12)
    axes[2].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=tz))
    axes[2].set_xlim(phases[0]["start"], phases[-1]["end"])
    fig.text(0.07, 0.965, "Probe 11 in a cup of water: Brita, tap, then salt a pinch at a time", color=INK,
             fontsize=14, fontweight="semibold", ha="left")
    fig.subplots_adjust(left=0.07, right=0.88, top=0.90, bottom=0.06, hspace=0.18)
    out1 = args.out_dir / f"msv4_cuptest_timeline_{DAY}.png"
    fig.savefig(out1, dpi=200, facecolor=SURFACE)

    # 2. The salt ladder: every metric relative to plain tap water.
    fig, ax = plt.subplots(figsize=(11, 6.6), facecolor=SURFACE)
    style(ax)
    xs = list(range(len(phases)))
    ax.plot([-0.3, xs[-1] + 0.05], [100, 100], color=INK_2, lw=1, ls=(0, (3, 3)), zorder=1)
    ends = []
    for key, label, _, color, marker in METRICS:
        ys = [100 * p["med"][key] / ref[key] for p in phases]
        ax.plot(xs, ys, color=color, lw=2.6 if key == "coupled" else 2.0, marker=marker, ms=8, mec=SURFACE, mew=1.5,
                zorder=4 if key == "coupled" else 3)
        ends.append([ys[-1], label, color])
    ax.set_yscale("log")
    ticks = [20, 30, 50, 70, 100, 150, 200, 300, 400]
    ax.set_yticks(ticks)
    ax.set_yticklabels([f"{t} %" for t in ticks])
    ax.minorticks_off()
    ax.set_ylim(15, 450)
    ends.sort()
    placed = []
    for y, label, color in ends:
        y = max(y, placed[-1][0] * 1.09) if placed else y
        placed.append((y, label, color))
    for y, label, color in placed:
        ax.text(xs[-1] + 0.12, y, label, color=INK, fontsize=10, va="center")
    ax.set_xticks(xs)
    ax.set_xticklabels([p["name"] for p in phases], color=INK, fontsize=10.5)
    ax.set_xlim(-0.3, xs[-1] + 1.9)
    ax.set_ylabel("value relative to plain tap water", color=INK_2, fontsize=10)
    fig.text(0.08, 0.95, "What salt does to each reading (median of each settled phase)", color=INK, fontsize=14,
             fontweight="semibold", ha="left")
    fig.subplots_adjust(left=0.08, right=0.97, top=0.89, bottom=0.09)
    out2 = args.out_dir / f"msv4_cuptest_salt_ladder_{DAY}.png"
    fig.savefig(out2, dpi=200, facecolor=SURFACE)

    # 3. Cm against Cd: salt slides the reading along a line of constant total.
    fig, ax = plt.subplots(figsize=(9.5, 7.2), facecolor=SURFACE)
    style(ax)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    cd_air = rows[0]["cd_air"]
    lo_cd, hi_cd = 90, 310
    # Lines of constant total through the phases' own medians: the three salt levels share one.
    salt = [p["med"]["coupled"] for p in phases if "pinch" in p["name"]]
    guides = ((round(statistics.median(salt)), "all three salt levels"),
              (round(next(p["med"]["coupled"] for p in phases if p["name"] == "Tap")), "tap"),
              (round(next(p["med"]["coupled"] for p in phases if p["name"] == "Brita")), "Brita, end"))
    for total, what in guides:
        ax.plot([lo_cd, hi_cd], [total + cd_air - lo_cd, total + cd_air - hi_cd], color=INK_2, lw=0.9,
                ls=(0, (4, 3)), zorder=1)
        ax.text(hi_cd, total + cd_air - hi_cd, f" {total} pF  ({what})", color=INK_2, fontsize=9,
                va="center", ha="left")
    ax.text(hi_cd, max(g[0] for g in guides) + cd_air - hi_cd + 22, " Cm + Cd − air", color=INK_2,
            fontsize=9, va="center", ha="left")
    for p, color in zip(phases, PHASE_COLORS):
        ax.scatter([d["cd"] for _, d in p["rows"]], [d["cm"] for _, d in p["rows"]], s=26, color=color,
                   edgecolor=SURFACE, linewidth=0.8, zorder=3, label=p["name"])
        m = p["med"]
        ax.annotate(p["name"], (m["cd"], m["cm"]), xytext=(8, 8), textcoords="offset points", color=INK,
                    fontsize=10, zorder=5)
    ax.set_xlim(lo_cd, hi_cd)
    ax.set_xlabel("Cd, pF", color=INK_2)
    ax.set_ylabel("Cm, pF", color=INK_2)
    ax.legend(loc="lower left", frameon=False, fontsize=9.5, labelcolor=INK)
    fig.text(0.1, 0.95, "Salt moves capacitance from Cm into Cd; their total stays put", color=INK, fontsize=14,
             fontweight="semibold", ha="left")
    fig.subplots_adjust(left=0.1, right=0.78, top=0.9, bottom=0.09)
    out3 = args.out_dir / f"msv4_cuptest_cm_vs_cd_{DAY}.png"
    fig.savefig(out3, dpi=200, facecolor=SURFACE)
    print(out1, out2, out3, sep="\n")


if __name__ == "__main__":
    main()
