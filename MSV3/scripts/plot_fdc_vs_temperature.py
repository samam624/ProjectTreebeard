#!/usr/bin/env python
"""Plot an MSV3 probe's raw FDC counts against board (die) temperature, on one
shared time axis with two y-scales, to show the thermal-cycle coupling
directly. Reads the CSV produced by cloud/scripts/fetch_supabase_history.py
--raw (needs raw_fdc_counts and board_temp_c).

Note on sign: raw_fdc_counts is inversely related to moisture -- lower
capacitance (drier) gives a HIGHER count. This plots the raw count as-is, not
inverted, so "up" on the moisture axis means "drier."

Example:
  python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC \\
      --sensor 4 --raw --since-hours 168 --csv data/sensor_logs/msv3_sensor4_raw_7d.csv
  python MSV3/scripts/plot_fdc_vs_temperature.py \\
      --csv data/sensor_logs/msv3_sensor4_raw_7d.csv \\
      --out data/sensor_logs/msv3_sensor4_fdc_vs_temp_7d.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np

SURFACE = "#1a1a19"
GRID = "#2c2c2a"
PRIMARY_INK = "#ffffff"
SECONDARY_INK = "#c3c2b7"
MUTED_INK = "#898781"
FDC_COLOR = "#3987e5"
TEMP_COLOR = "#d95926"


def load_series(csv_path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    timestamps, fdc, temp = [], [], []
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            fdc_value = row.get("raw_fdc_counts", "")
            temp_value = row.get("board_temp_c", "")
            if fdc_value in ("", None) or temp_value in ("", None):
                continue
            timestamps.append(np.datetime64(row["timestamp_iso"].replace(" ", "T").split("+")[0]))
            fdc.append(float(fdc_value))
            temp.append(float(temp_value))

    t = np.array(timestamps)
    f = np.array(fdc, dtype=float)
    c = np.array(temp, dtype=float)
    order = np.argsort(t)
    return t[order], f[order], c[order]


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot raw FDC counts vs. board temperature on a shared time axis.")
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--probe-label", default="Probe 4", help='Title prefix, e.g. "Probe 4" (default "Probe 4").')
    parser.add_argument("--days", type=float, default=None, help="Trim to the most recent N days before plotting.")
    parser.add_argument("--start", default=None, help="Drop rows before this ISO timestamp (e.g. a boot/reflash transient at the front of the window).")
    args = parser.parse_args()

    t, fdc, temp = load_series(args.csv)
    if len(t) == 0:
        print("No rows with both raw_fdc_counts and board_temp_c found.")
        return 1

    if args.start is not None:
        mask = t >= np.datetime64(args.start)
        t, fdc, temp = t[mask], fdc[mask], temp[mask]

    if args.days is not None:
        cutoff = t[-1] - np.timedelta64(int(args.days * 24 * 3600), "s")
        mask = t >= cutoff
        t, fdc, temp = t[mask], fdc[mask], temp[mask]

    plt.rcParams.update({
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "text.color": PRIMARY_INK,
        "font.family": "sans-serif",
    })

    fig, ax_fdc = plt.subplots(figsize=(13, 6.5))
    ax_temp = ax_fdc.twinx()

    (fdc_line,) = ax_fdc.plot(t, fdc, linewidth=1.2, color=FDC_COLOR, label="Raw FDC counts (connected)")
    (temp_line,) = ax_temp.plot(t, temp, linewidth=1.2, color=TEMP_COLOR, label="Board (die) temperature", alpha=0.9)

    ax_fdc.set_ylabel("Raw FDC counts  (higher = drier)", color=FDC_COLOR)
    ax_temp.set_ylabel("Board temperature (°C)", color=TEMP_COLOR)

    for spine in ("top",):
        ax_fdc.spines[spine].set_visible(False)
        ax_temp.spines[spine].set_visible(False)
    ax_fdc.spines["left"].set_color(FDC_COLOR)
    ax_fdc.spines["right"].set_visible(False)
    ax_temp.spines["right"].set_color(TEMP_COLOR)
    ax_temp.spines["bottom"].set_color(MUTED_INK)
    ax_fdc.spines["bottom"].set_color(MUTED_INK)

    ax_fdc.tick_params(axis="y", colors=FDC_COLOR)
    ax_temp.tick_params(axis="y", colors=TEMP_COLOR)
    ax_fdc.tick_params(axis="x", colors=SECONDARY_INK)

    ax_fdc.grid(True, axis="y", color=GRID, linewidth=0.8, alpha=0.6)
    ax_fdc.set_axisbelow(True)

    ax_fdc.xaxis.set_major_locator(mdates.AutoDateLocator())
    ax_fdc.xaxis.set_major_formatter(mdates.ConciseDateFormatter(ax_fdc.xaxis.get_major_locator()))

    span_days = (t[-1] - t[0]) / np.timedelta64(1, "D")
    fig.suptitle(
        f"{args.probe_label} — raw FDC counts vs. board temperature ({span_days:.1f} days)",
        color=PRIMARY_INK, fontsize=15, x=0.01, ha="left", y=0.985,
    )

    legend = ax_fdc.legend(
        handles=[fdc_line, temp_line],
        loc="lower center",
        bbox_to_anchor=(0.5, 1.005),
        ncol=2,
        frameon=False,
        labelcolor=[FDC_COLOR, TEMP_COLOR],
    )

    fig.tight_layout(rect=(0, 0, 1, 0.93))
    fig.savefig(args.out, dpi=150, facecolor=SURFACE)
    print(f"Wrote {args.out} ({len(t)} points, {span_days:.1f} days)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
