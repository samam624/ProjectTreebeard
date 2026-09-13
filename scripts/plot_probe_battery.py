#!/usr/bin/env python
"""Plot a probe's raw battery signal over time, with optional spike labels.

Generalizes plot_probe4_battery_mv.py to any probe/column: MSV3 probes upload
true raw_battery_mv (ADC millivolts); older MSV2-generation probes never send
raw millivolts at all and battery_percent is the most granular value they
report, so that's the column to use for them. Spike labeling only makes sense
where genuine isolated transients exist -- pass --spike-threshold to enable it;
left off by default because forcing it onto a uniformly noisy signal produces
meaningless clutter, not real events.

Example (MSV3 probe, true mV, spike labels on):
  python scripts/plot_probe_battery.py --csv data/sensor_logs/msv3_sensor4_raw.csv \\
      --column raw_battery_mv --unit mV --probe-label "Probe 4" --spike-threshold 30 \\
      --out data/sensor_logs/probe4_battery_mv.png

Example (MSV2 probe, no raw mV, no spike detection):
  python scripts/plot_probe_battery.py --csv data/sensor_logs/msv2_sensor2_30d.csv \\
      --column battery_percent --unit % --probe-label "Probe 2" \\
      --out data/sensor_logs/probe2_battery_pct.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np


def load_series(csv_path: Path, column: str) -> tuple[np.ndarray, np.ndarray]:
    timestamps = []
    values = []
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            value = row.get(column, "")
            if value in ("", None):
                continue
            timestamps.append(np.datetime64(row["timestamp_iso"].replace(" ", "T").split("+")[0]))
            values.append(float(value))

    t = np.array(timestamps)
    v = np.array(values, dtype=float)
    order = np.argsort(t)
    return t[order], v[order]


def find_spikes(t: np.ndarray, v: np.ndarray, window: int, threshold: float, min_gap: int) -> list[int]:
    """Flag samples that jump well above their recent trailing baseline, and
    return one peak index per contiguous cluster of flagged samples."""
    kernel = np.ones(window) / window
    padded = np.concatenate([np.full(window, v[0]), v[:-1]])
    baseline = np.convolve(padded, kernel, mode="valid")[: len(v)]
    deviation = v - baseline

    flagged = np.where(deviation > threshold)[0]
    clusters: list[list[int]] = []
    for idx in flagged:
        if clusters and idx - clusters[-1][-1] <= min_gap:
            clusters[-1].append(idx)
        else:
            clusters.append([idx])

    return [c[int(np.argmax(v[c]))] for c in clusters]


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot a probe's raw battery signal over time.")
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--column", default="raw_battery_mv", help="CSV column to plot (default raw_battery_mv).")
    parser.add_argument("--unit", default="mV", help="Unit label for the y-axis and value string (default mV).")
    parser.add_argument("--probe-label", default="Probe", help='Title prefix, e.g. "Probe 4" (default "Probe").')
    parser.add_argument("--since-trough", action="store_true", help="Trim to start at the global minimum value.")
    parser.add_argument("--spike-threshold", type=float, default=None, help="Enable USB spike labeling at this deviation threshold.")
    parser.add_argument("--spike-window", type=int, default=15, help="Rolling-baseline window in samples.")
    parser.add_argument("--spike-min-gap", type=int, default=5, help="Max sample gap to merge into one spike cluster.")
    parser.add_argument("--spike-label", default="USB", help="Text for each spike annotation.")
    parser.add_argument("--trendline", action="store_true", help="Overlay a linear least-squares trend line.")
    args = parser.parse_args()

    t, v = load_series(args.csv, args.column)
    if len(v) == 0:
        print(f"No rows with {args.column} found.")
        return 1

    if args.since_trough:
        i = int(np.argmin(v))
        t, v = t[i:], v[i:]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(t, v, linewidth=1, label="raw")

    if args.trendline:
        days = (t - t[0]) / np.timedelta64(1, "D")
        slope, intercept = np.polyfit(days, v, 1)
        ax.plot(t, slope * days + intercept, linewidth=1.5, linestyle="--", label=f"trend ({slope:+.2f} {args.unit}/day)")
        ax.legend()

    if args.spike_threshold is not None:
        for idx in find_spikes(t, v, args.spike_window, args.spike_threshold, args.spike_min_gap):
            ax.annotate(
                args.spike_label,
                xy=(t[idx], v[idx]),
                xytext=(0, 12),
                textcoords="offset points",
                ha="center",
                fontsize=9,
                arrowprops=dict(arrowstyle="-", linewidth=0.75),
            )

    ax.set_title(f"{args.probe_label} Battery {'Voltage' if args.unit == 'mV' else ''} (Raw)".replace("  ", " ").strip())
    ax.set_xlabel("Date")
    ax.set_ylabel(f"Battery{' Voltage' if args.unit == 'mV' else ''} ({args.unit})")
    ax.grid(True, alpha=0.3)

    ax.xaxis.set_major_locator(mdates.AutoDateLocator())
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(ax.xaxis.get_major_locator()))

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"Wrote {args.out} ({len(v)} points, {v[0]:.0f} -> {v[-1]:.0f} {args.unit})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
