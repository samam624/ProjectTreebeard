#!/usr/bin/env python
"""Plot probe 4's raw battery millivolts over time.

Reads the CSV produced by cloud/scripts/fetch_supabase_history.py --raw and
plots raw_battery_mv (true ADC millivolts, not the calibrated percent) as-is:
no smoothing, no annotations, just the raw series.

Example:
  python scripts/plot_probe4_battery_mv.py --csv data/sensor_logs/msv3_sensor4_raw.csv --out data/sensor_logs/probe4_battery_mv.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np


def load_series(csv_path: Path) -> tuple[np.ndarray, np.ndarray]:
    timestamps = []
    mv = []
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            value = row.get("raw_battery_mv", "")
            if value in ("", None):
                continue
            timestamps.append(np.datetime64(row["timestamp_iso"].replace(" ", "T").split("+")[0]))
            mv.append(float(value))

    t = np.array(timestamps)
    v = np.array(mv, dtype=float)
    order = np.argsort(t)
    return t[order], v[order]


def find_spikes(t: np.ndarray, v: np.ndarray, window: int = 15, threshold: float = 30.0) -> list[int]:
    """Flag samples that jump well above their recent trailing baseline (USB
    connect/disconnect transients during charging or flashing sessions), and
    return one peak index per contiguous cluster of flagged samples."""
    kernel = np.ones(window) / window
    padded = np.concatenate([np.full(window, v[0]), v[:-1]])
    baseline = np.convolve(padded, kernel, mode="valid")[: len(v)]
    deviation = v - baseline

    flagged = np.where(deviation > threshold)[0]
    clusters: list[list[int]] = []
    for idx in flagged:
        if clusters and idx - clusters[-1][-1] <= 5:
            clusters[-1].append(idx)
        else:
            clusters.append([idx])

    return [c[int(np.argmax(v[c]))] for c in clusters]


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot probe 4 raw battery mV over time.")
    parser.add_argument("--csv", type=Path, default=Path("data/sensor_logs/msv3_sensor4_raw.csv"))
    parser.add_argument("--out", type=Path, default=Path("data/sensor_logs/probe4_battery_mv.png"))
    parser.add_argument(
        "--since-trough",
        action="store_true",
        default=True,
        help="Trim to start at the global minimum mV (default on).",
    )
    parser.add_argument("--full-range", dest="since_trough", action="store_false", help="Plot the whole CSV.")
    args = parser.parse_args()

    t, v = load_series(args.csv)
    if len(v) == 0:
        print("No rows with raw_battery_mv found.")
        return 1

    if args.since_trough:
        i = int(np.argmin(v))
        t, v = t[i:], v[i:]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(t, v, linewidth=1)

    for idx in find_spikes(t, v):
        ax.annotate(
            "USB",
            xy=(t[idx], v[idx]),
            xytext=(0, 12),
            textcoords="offset points",
            ha="center",
            fontsize=9,
            arrowprops=dict(arrowstyle="-", linewidth=0.75),
        )

    ax.set_title("Probe 4 Battery Voltage (Raw)")
    ax.set_xlabel("Date")
    ax.set_ylabel("Battery Voltage (mV)")
    ax.grid(True, alpha=0.3)

    ax.xaxis.set_major_locator(mdates.AutoDateLocator())
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(ax.xaxis.get_major_locator()))

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"Wrote {args.out} ({len(v)} points, {v[0]:.0f} -> {v[-1]:.0f} mV)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
