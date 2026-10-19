"""Figures for the 2026-10-15 raw-sweep test (MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md).

    python MSV4/scripts/msv4_rawsweep_plot.py data/sensor_logs/msv4_rawsweep_2026-10-15 --out-dir data/sensor_logs

Writes three PNGs dated 2026-10-15:
  msv4_rawsweep_models_*    each model's residual by medium, and its mask C change with one pinch
  msv4_rawsweep_timeline_*  the logged sweeps from the dunk to the salt, fitted per sweep
  msv4_rawsweep_loop_*      R and mask C against the tip through the hot-bowl swing
The per-sweep fits come from msv4_rawsweep_swing.py's cache (swing_fits.csv in the data folder),
which this script extends if new sweeps were logged.
"""
from __future__ import annotations

import argparse
import contextlib
import io
from datetime import datetime
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

import msv4_raw_sweep_models as models
import msv4_rawsweep_swing as swing

DAY = "2026-10-15"
RS, CD0 = 3300.0, 13.71e-12
MEDIA = (("air", "air.txt"), ("tap", "tap_presalt_monitor.txt"), ("1 pinch", "salt1.txt"), ("soil", "soil.txt"))
SURFACE, INK, INK_2, GRID, BAND = "#fcfcfb", "#0b0b0b", "#52514e", "#e6e5e1", "#f0efec"
# Colour follows the model in every figure (dataviz reference palette, slots 1-5 in order);
# markers repeat the identity for readers who cannot separate the hues.
MODEL_STYLE = {
    "lumped": ("lumped, Cd free (firmware)", "#2a78d6", "o"),
    "lumped-fixed": ("lumped, Cd fixed", "#eb6834", "s"),
    "cpe": ("CPE", "#1baf7a", "^"),
    "tline": ("RC line", "#eda100", "D"),
    "two-branch": ("two-branch", "#e87ba4", "v"),
}
PHASES = (("tap, settling", "14:12:20", "14:54:00"), ("hot bowl", "14:54:00", "15:16:00"),
          ("cooling in room air", "15:16:00", "16:48:00"), ("1 pinch of salt", "17:03:00", "17:26:00"))
# The plot starts at the dunk, so it needs no marker of its own.
EVENTS = (("glass moved", "15:16:00"), ("salt in", "17:03:00"), ("mixed, bubbles knocked off", "17:08:00"))
SHORT = {"lumped": "lumped (firmware)", "tline": "RC line", "two-branch": "two-branch"}
TIME_SEQ = LinearSegmentedColormap.from_list("time", ["#b9d4f3", "#2a78d6", "#0d2d57"])


def at(clock: str) -> datetime:
    return datetime.fromisoformat(f"{DAY}T{clock}")


def style(ax, grid_axis: str = "y") -> None:
    ax.set_facecolor(SURFACE)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(INK_2)
        ax.spines[s].set_linewidth(0.8)
    ax.grid(axis=grid_axis, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.tick_params(colors=INK_2, labelsize=9.5, length=0)


def title(fig, text: str, sub: str) -> None:
    fig.text(0.012, 0.975, text, fontsize=14, color=INK, weight="semibold", va="top")
    fig.text(0.012, 0.935, sub, fontsize=10, color=INK_2, va="top")


def media_summary(folder: Path) -> dict:
    sweeps = {name: models.parse_sweeps((folder / f).read_text(encoding="utf-8", errors="replace")) for name, f in MEDIA}
    cp = models.fit_cp(models.parse_sweeps((folder / "open.txt").read_text(encoding="utf-8")), RS)
    with contextlib.redirect_stdout(io.StringIO()):
        return models.report(sweeps, RS, cp, CD0)


def figure_models(summary: dict, out: Path) -> None:
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 5.6), facecolor=SURFACE,
                                   gridspec_kw={"width_ratios": [1.45, 1], "wspace": 0.42})
    title(fig, "Water and soil rank the electrode models in opposite orders",
          "Probe 11, 15 averaged sweeps per medium, 2026-10-15. Left: lower fits better. "
          "Right: a model that separates the mask should not move with salt.")
    names = [m for m, _ in MEDIA]
    x = np.arange(len(names))
    for model, (label, colour, marker) in MODEL_STYLE.items():
        y = [summary[m][model]["rms"] * 1e4 for m in names]
        ax1.plot(x, y, color=colour, linewidth=2, marker=marker, markersize=8,
                 markeredgecolor=SURFACE, markeredgewidth=1.5, label=label, zorder=3)
        ax1.annotate(f"{y[-1]:.0f}", (x[-1], y[-1]), xytext=(9, 0), textcoords="offset points",
                     va="center", fontsize=9.5, color=INK_2)
    noise = summary["air"]["lumped"]["noise"] * 1e4
    ax1.axhline(noise, color=INK_2, linewidth=1, linestyle=(0, (2, 3)))
    ax1.text(x[0] - 0.15, noise + 6, f"averaged repeat noise, {noise:.1f}", fontsize=9, color=INK_2)
    style(ax1)
    ax1.set_xticks(x, names, fontsize=10.5, color=INK)
    ax1.set_xlim(-0.3, len(names) - 0.55)
    ax1.set_ylim(0, None)
    ax1.set_ylabel("fit residual, 1e-4 of drive", color=INK_2, fontsize=10)
    ax1.legend(frameon=False, fontsize=9.5, loc="upper left", labelcolor=INK)

    order = list(MODEL_STYLE)
    change = [100 * (summary["1 pinch"][m]["mask C"] / summary["tap"][m]["mask C"] - 1) for m in order]
    ypos = np.arange(len(order))[::-1]
    for yy, model, c in zip(ypos, order, change):
        ax2.barh(yy, c, height=0.6, color=MODEL_STYLE[model][1], zorder=3)
        ax2.text(c + 1.2, yy, f"+{c:.1f} %", va="center", fontsize=10, color=INK)
    style(ax2, "x")
    ax2.set_yticks(ypos, [MODEL_STYLE[m][0] for m in order], fontsize=10, color=INK)
    ax2.set_xlim(0, max(change) * 1.18)
    ax2.set_xlabel("mask C change, tap to one pinch", color=INK_2, fontsize=10)
    ax2.axvline(0, color=INK_2, linewidth=0.8)
    fig.subplots_adjust(left=0.07, right=0.97, top=0.84, bottom=0.11)
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def series(rows: list[dict], model: str, key: str, start: str, end: str):
    sel = [r for r in rows if r["model"] == model and start <= r["time"][11:19] < end and r[key]]
    sel.sort(key=lambda r: r["time"])
    return [datetime.fromisoformat(r["time"]) for r in sel], np.array([float(r[key]) for r in sel])


def figure_timeline(rows: list[dict], out: Path) -> None:
    start, end = "14:12:20", "17:26:00"
    fig, axes = plt.subplots(4, 1, figsize=(12.5, 10), sharex=True, facecolor=SURFACE,
                             gridspec_kw={"height_ratios": [0.8, 1, 1, 0.9], "hspace": 0.12})
    title(fig, "One cup of tap water: settling, a hot-bowl swing, then one pinch of salt",
          "Every model fitted to every logged sweep (one each 30 s). Gaps are the 15-sweep captures. "
          "The maintainer attributes the one-way changes to bubbles on the blade.")
    panels = (
        ("tip, °C", (("lumped", "tip_c"),)),
        ("water R, Ω", (("lumped", "r_ohm"), ("tline", "r_ohm"))),
        ("mask C, pF", (("lumped", "mask_c_pf"), ("tline", "mask_c_pf"), ("two-branch", "mask_c_pf"))),
        ("Cd (lumped), pF", (("lumped", "cd_pf"),)),
    )
    legend_at = {"water R, Ω": "lower left", "mask C, pF": "lower center"}
    for ax, (ylabel, items) in zip(axes, panels):
        style(ax)
        for i, (label, a, b) in enumerate(PHASES):
            if i % 2 == 0:
                ax.axvspan(at(a), at(b), color=BAND, zorder=0, linewidth=0)
        for _, clock in EVENTS:
            ax.axvline(at(clock), color=INK_2, linewidth=0.8, linestyle=(0, (2, 3)), zorder=1)
        for model, key in items:
            t, v = series(rows, model, key, start, end)
            colour = INK if key == "tip_c" else MODEL_STYLE[model][1]
            # Break the line across logger pauses so a capture gap is not drawn as data.
            gaps = np.where(np.diff(mdates.date2num(t)) * 1440 > 1.5)[0] + 1
            for i, (seg_t, seg_v) in enumerate(zip(np.split(np.array(t), gaps), np.split(v, gaps))):
                ax.plot(seg_t, seg_v, color=colour, linewidth=2, zorder=3,
                        label=SHORT[model] if i == 0 and len(items) > 1 else None)
        if len(items) > 1:
            ax.legend(frameon=False, fontsize=9.5, loc=legend_at[ylabel], labelcolor=INK)
        ax.set_ylabel(ylabel, color=INK_2, fontsize=10)
    for label, a, b in PHASES:
        mid = at(a) + (at(b) - at(a)) / 2
        axes[0].text(mid, 1.02, label, transform=axes[0].get_xaxis_transform(), ha="center",
                     fontsize=9.5, color=INK)
    # Event names sit low in the mask-C panel, the one region every event leaves empty.
    for label, clock in EVENTS:
        axes[2].text(at(clock), 0.04, label + " ", transform=axes[2].get_xaxis_transform(), rotation=90,
                     fontsize=8.5, color=INK_2, va="bottom", ha="right")
    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    axes[-1].set_xlim(at(start), at("17:28:00"))
    fig.subplots_adjust(left=0.08, right=0.97, top=0.88, bottom=0.05)
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def figure_loop(rows: list[dict], out: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(12.5, 5.6), facecolor=SURFACE, gridspec_kw={"wspace": 0.3})
    title(fig, "The water's R retraces the swing; the mask C comes back about 5 % higher",
          "Lumped model, every logged sweep 14:44-16:48, coloured by time. Dashed: the cooling leg's "
          "fit, carried back to the pre-swing temperature.")
    t0 = at(swing.PRE[0])
    for ax, (key, ylabel) in zip(axes, (("r_ohm", "water R, Ω"), ("mask_c_pf", "mask C, pF"))):
        style(ax, "both")
        t, v = series(rows, "lumped", key, swing.PRE[0], swing.COOL[1])
        _, tip = series(rows, "lumped", "tip_c", swing.PRE[0], swing.COOL[1])
        mins = np.array([(x - t0).total_seconds() / 60 for x in t])
        sc = ax.scatter(tip, v, c=mins, cmap=TIME_SEQ, s=26, edgecolors=SURFACE, linewidths=0.8, zorder=3)
        cool = np.array([swing.COOL[0] <= x.strftime("%H:%M:%S") < swing.COOL[1] for x in t])
        pre = np.array([swing.PRE[0] <= x.strftime("%H:%M:%S") < swing.PRE[1] for x in t])
        p = np.polyfit(tip[cool], np.log(v[cool]), 1)
        xs = np.linspace(tip[pre].mean() - 0.3, tip[cool].max(), 50)
        ax.plot(xs, np.exp(np.polyval(p, xs)), color=INK_2, linewidth=1.2, linestyle=(0, (4, 3)), zorder=2)
        gap = 100 * (np.exp(np.polyval(p, tip[pre].mean())) / np.median(v[pre]) - 1)
        ax.annotate(f"pre-swing: {gap:+.1f} % from\nthe cooling leg's line", (tip[pre].mean(), np.median(v[pre])),
                    xytext=(55, -4) if key == "mask_c_pf" else (30, 30), textcoords="offset points", fontsize=9.5,
                    color=INK, va="center", arrowprops={"arrowstyle": "-", "color": INK_2, "linewidth": 0.8})
        heat = ~cool & ~pre & np.array([x.strftime("%H:%M:%S") < "15:04:00" for x in t])
        hx, hy = tip[heat][len(tip[heat]) // 2], v[heat][len(v[heat]) // 2]
        right = key == "mask_c_pf"
        ax.annotate("heating leg\n(the tip lags the water)", (hx, hy), xytext=(8, -30) if right else (-150, -30),
                    textcoords="offset points", fontsize=9, color=INK_2, va="center",
                    arrowprops={"arrowstyle": "-", "color": INK_2, "linewidth": 0.8})
        ax.text(0.97 if right else 0.03, 0.04, f"cooling leg {100 * p[0]:+.2f} %/°C", transform=ax.transAxes,
                ha="right" if right else "left", va="bottom", fontsize=10, color=INK)
        ax.set_xlabel("tip, °C", color=INK_2, fontsize=10)
        ax.set_ylabel(ylabel, color=INK_2, fontsize=10)
    cbar = fig.colorbar(sc, cax=fig.add_axes([0.925, 0.14, 0.012, 0.66]))
    cbar.set_label("minutes from 14:44", color=INK_2, fontsize=9.5)
    cbar.outline.set_visible(False)
    cbar.ax.tick_params(colors=INK_2, labelsize=9, length=0)
    fig.subplots_adjust(left=0.07, right=0.895, top=0.84, bottom=0.11)
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path, help="data/sensor_logs/msv4_rawsweep_2026-10-15")
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    plt.rcParams.update({"font.family": "Segoe UI", "font.size": 10.5})
    rows = swing.fit_log(args.folder / "monitor.txt", RS, 8.75e-12, CD0, args.folder / "swing_fits.csv")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    outs = [args.out_dir / f"msv4_rawsweep_{k}_{DAY}.png" for k in ("models", "timeline", "loop")]
    figure_models(media_summary(args.folder), outs[0])
    figure_timeline(rows, outs[1])
    figure_loop(rows, outs[2])
    for o in outs:
        print(o)


if __name__ == "__main__":
    main()
