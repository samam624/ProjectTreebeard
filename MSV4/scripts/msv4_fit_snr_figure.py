#!/usr/bin/env python3
"""The moisture-number fit for each pot, and its signal-to-noise in grams.

Uses msv4_stored_vs_mass_both.analyse() unchanged, so the line, the weighings it is fitted on and
the noise figures are exactly the ones in data/sensor_logs/latest/stored_vs_mass_s10_s11.png.

Left two panels: every weighing the fit uses, moisture number against grams on the scale, marked by
watering cycle, with the least-squares line.

Right panel: signal / noise. The SIGNAL is the water range the fit covers in that pot, heaviest
minus lightest fitted weighing. Three noises, all in grams through that pot's own slope:
  per reading       scatter between successive readings (robust sigma / sqrt 2)
  hourly, in a day  hourly medians about a 24 h running median (the within-day wobble)
  against the scale rms distance of the fitted weighings from the line
The first two say how finely the probe resolves a change; the last says how well the number
tracks the scale across days and waterings.

    python MSV4/scripts/msv4_fit_snr_figure.py [-o out.png]
"""
from __future__ import annotations

import argparse
import sys
from datetime import timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import msv4_experimental_moisture as reading
import msv4_stored_vs_mass_both as both

INK = "#2B3038"
MUTED = "#6B7280"
SURFACE = "#FCFCFA"
GRID = "#E6E4DE"
# Cycles are ordered in time, so one hue light -> dark (kept off the two pot colours), plus a marker each.
CYCLE_COLOURS = ["#B49FDB", "#9579CC", "#7656B8", "#583D96", "#3B2770"]
CYCLE_MARKERS = ["o", "s", "D", "^", "v"]
POT_COLOURS = {10: "#2A77D6", 11: "#E8692D"}


def style(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right", "left", "bottom"):
        ax.spines[side].set_visible(False)
    ax.grid(True, color=GRID, lw=1)
    ax.tick_params(colors=MUTED, labelsize=12, length=0)


def load() -> list[dict]:
    out = []
    for sensor, (weighings, waterings), name in (
            (10, both.sensor10_pot(), "Alocasia"),
            (11, both.sensor11_pot(), "Begonia")):
        rows = [r for r in reading.load(both.CACHE / f"sensor{sensor}_raw.csv", None)
                if r["stored"] is not None and r["time"] >= waterings[0] - timedelta(hours=48)]
        res = both.analyse(sensor, rows, weighings, waterings)
        res["name"] = name
        grams = [g for g, _, _ in res["paired"]]
        res["signal_g"] = max(grams) - min(grams)
        res["noises"] = [("per reading", res["per_reading_g"]),
                         ("hourly median, within a day", res["wobble_g"]),
                         ("against the scale (fit error)", res["resid_rms"])]
        out.append(res)
    return out


def scatter(ax, r: dict) -> None:
    style(ax)
    cycles = {}
    for g, u, t in r["paired"]:
        c = sum(1 for w in r["waterings"] if w <= t)
        cycles.setdefault(c, []).append((g, u, t))
    waterings = sorted(r["waterings"])
    for i, c in enumerate(sorted(cycles)):
        pts = cycles[c]
        label = "before the first watering" if c == 0 else f"after the {waterings[c - 1]:%m-%d} watering"
        ax.scatter([g for g, _, _ in pts], [u for _, u, _ in pts], s=80, color=CYCLE_COLOURS[i],
                   marker=CYCLE_MARKERS[i], edgecolor=SURFACE, linewidth=1.2, zorder=3, label=label)
    gs = np.array([g for g, _, _ in r["paired"]])
    x = np.linspace(gs.min() - 20, gs.max() + 20, 50)
    ax.plot(x, r["intercept"] + r["slope"] * x, color=INK, lw=2, zorder=2, label="least-squares line")
    ax.set_title(f"{r['name']} (sensor {r['sensor']})", loc="left", fontsize=15, color=INK, pad=28)
    ax.text(0, 1.02, f"{r['slope']:.1f} units per gram, {len(r['paired'])} weighings, "
            f"{r['resid_rms']:.0f} g rms off the line", transform=ax.transAxes, fontsize=12, color=MUTED)
    ax.set_xlabel("grams on the scale", color=MUTED, fontsize=12)
    ax.set_ylabel("moisture number, units", color=MUTED, fontsize=12)
    ax.legend(loc="upper left", fontsize=11, frameon=False)


def snr_panel(ax, results: list[dict]) -> None:
    style(ax)
    ax.grid(True, axis="x", color=GRID, lw=1)
    ax.grid(False, axis="y")
    labels = [name for name, _ in results[0]["noises"]]
    h = 0.36
    for j, r in enumerate(results):
        ys = np.arange(len(labels)) + (j - 0.5) * (h + 0.04)
        snr = [r["signal_g"] / n for _, n in r["noises"]]
        ax.barh(ys, snr, height=h, color=POT_COLOURS[r["sensor"]], label=f"{r['name']}: signal {r['signal_g']:.0f} g")
        for y, s, (_, n) in zip(ys, snr, r["noises"]):
            ax.text(s * 1.12, y, f"{s:,.0f}  ({n:.1f} g)", va="center", fontsize=12, color=INK)
    ax.set_xscale("log")
    ax.set_xlim(1, 4000)
    ax.set_yticks(np.arange(len(labels)))
    ax.set_yticklabels(labels, fontsize=12, color=INK)
    ax.invert_yaxis()
    ax.set_xlabel("signal ÷ noise (log scale); noise in grams in brackets", color=MUTED, fontsize=12)
    ax.set_title("Signal-to-noise in grams of water", loc="left", fontsize=15, color=INK, pad=28)
    ax.text(0, 1.02, "signal = water range the fit spans in that pot", transform=ax.transAxes,
            fontsize=12, color=MUTED)
    ax.legend(loc="lower right", fontsize=11, frameon=False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", type=Path,
                        default=both.ROOT / "data" / "sensor_logs" / "latest" / "fit_snr_s10_s11.png")
    args = parser.parse_args()
    results = load()

    fig = plt.figure(figsize=(18, 7.2), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(1, 3, width_ratios=[1, 1, 1.25], wspace=0.42,
                            left=0.05, right=0.985, top=0.86, bottom=0.11)
    for k, r in enumerate(results):
        scatter(fig.add_subplot(grid[0, k]), r)
    snr_panel(fig.add_subplot(grid[0, 2]), results)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)
    for r in results:
        last = max(row["time"] for row in r["rows"])
        print(f"{r['name']} (sensor {r['sensor']}): data to {last:%Y-%m-%d %H:%M}; slope {r['slope']:.2f} units/g; "
              f"{len(r['paired'])} weighings; signal {r['signal_g']:.0f} g")
        for name, n in r["noises"]:
            print(f"    {name:32} {n:7.2f} g   SNR {r['signal_g'] / n:8.0f}")


if __name__ == "__main__":
    main()
