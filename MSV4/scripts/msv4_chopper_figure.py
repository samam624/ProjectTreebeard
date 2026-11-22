#!/usr/bin/env python3
"""How the chopper turns a 1 MHz square wave into one DC reading per phase (DRV channel).

Computed, not a capture. U4 (READ_CNTL, the chopper) connects the 100 kohm / 1 nF filter (R19 /
C16 on DRV, R18 / C9 on SENSE) to the signal for half of every drive cycle and to GND for the
other half (MSV4_Moisture_BLE_Sleep.ino: "0 = filters on signal, 1 = on GND"). The chopper runs
off the drive's own timer, so the window sits at a fixed phase. At timer phase k the filter
listens during ticks [k + N/2, k + N) (MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md). The filter's
time constant, 100 us, is 100 drive cycles, so the capacitor settles to the AVERAGE of what the
switch passes: 1/2 x the mean of the signal over the window. The firmware waits FE_SETTLE_MS = 2 ms
(20 time constants) before the ADC reads it.

Slide the window by stepping k and the reading traces the DRV_OUT curve: zero when the window sits
on the low half, 1650 mV when it sits on the high half (phase 180 degrees).

    python MSV4/scripts/msv4_chopper_figure.py --out MSV4/docs/figures/MSV4_CHOPPER_WINDOW_2026-11-18.png
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import msv4_lockin_fit_figure as lk

TAU_S = 100e3 * 1e-9          # R19 x C16
SETTLE_S = 2e-3               # FE_SETTLE_MS
SHOW_DEG = (0, 90, 180)
PHASE_COLOURS = {0: "#B49FDB", 90: "#7656B8", 180: "#3B2770"}
INK, MUTED, SURFACE, GRID = lk.INK, lk.MUTED, lk.SURFACE, lk.GRID


def listening(t: np.ndarray, period: float, k_ticks: float) -> np.ndarray:
    """True where the chopper connects the filter to the signal (ticks [k+N/2, k+N) of each cycle)."""
    pos = np.mod(t / period * lk.N - (k_ticks + lk.N / 2), lk.N)
    return pos < lk.N / 2


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    f = lk.CLOCK_HZ / lk.N
    period = 1.0 / f
    drv = lambda t: np.where(np.mod(t * f, 1.0) < 0.5, lk.VDD, 0.0)

    fig = plt.figure(figsize=(18, 8.4), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(3, 2, width_ratios=[1.25, 1], hspace=0.62, wspace=0.2,
                            left=0.05, right=0.985, top=0.83, bottom=0.08)

    # Left: three phases, two drive cycles each.
    t = np.linspace(0.0, 2.0 * period, 4001)
    us = t * 1e6
    for row, deg in enumerate(SHOW_DEG):
        ax = fig.add_subplot(grid[row, 0])
        lk.style(ax)
        ax.set_axisbelow(True)
        c = PHASE_COLOURS[deg]
        k = deg / 360.0 * lk.N
        on = listening(t, period, k)
        passed = np.where(on, drv(t), 0.0)
        ax.fill_between(us, 0, 4.0, where=on, color=c, alpha=0.13, lw=0, step="mid")
        ax.plot(us, drv(t), color=INK, lw=1.6, alpha=0.55)
        ax.fill_between(us, 0, passed, color=c, alpha=0.75, lw=0, step="mid")
        level = lk.chopper_reading(drv, f, k) * 2.0 * 0.5   # exact half-window average, volts
        ax.axhline(level, color=c, lw=2.6, ls=(0, (6, 3)))
        ax.text(1.97, level + 0.12, f"averages to {level * 1000:,.0f} mV", va="bottom", ha="right", fontsize=13,
                color=c, fontweight="bold", bbox=dict(boxstyle="round,pad=0.25", facecolor=SURFACE, edgecolor="none"))
        ax.set_xlim(0, 2.0)
        ax.set_ylim(-0.3, 4.0)
        ax.set_yticks([0, 3.3])
        ax.set_yticklabels(["0", "3.3 V"])
        ax.set_title(f"chopper phase {deg}°", loc="left", fontsize=14, color=INK, pad=6)
        if row == 2:
            ax.set_xlabel("time, µs (two cycles at 1 MHz)", color=MUTED, fontsize=12)
        else:
            ax.set_xticklabels([])

    # Right top: the reading at every phase.
    axr = fig.add_subplot(grid[0:2, 1])
    lk.style(axr)
    fine = np.linspace(0, lk.N, 361)
    curve = [lk.chopper_reading(drv, f, k) * 1000 for k in fine]
    axr.plot(fine / lk.N * 360, curve, color=MUTED, lw=1.4, ls=(0, (4, 3)))
    ks = np.arange(lk.M) * lk.N / lk.M
    vals = [lk.chopper_reading(drv, f, k) * 1000 for k in ks]
    axr.scatter(ks / lk.N * 360, vals, s=60, color=INK, edgecolor=SURFACE, linewidth=1.5, zorder=3)
    for deg in SHOW_DEG:
        v = lk.chopper_reading(drv, f, deg / 360.0 * lk.N) * 1000
        axr.scatter([deg], [v], s=260, color=PHASE_COLOURS[deg], edgecolor=SURFACE, linewidth=2.5, zorder=4)
    axr.annotate("window on the high half:\nthe biggest DRV_OUT", (180, 1650), xytext=(235, 1740),
                 fontsize=12, color=INK, arrowprops=dict(arrowstyle="-", color=INK, lw=1.2, shrinkB=10))
    axr.annotate("window on the low half: 0", (0, 0), xytext=(40, 120), fontsize=12, color=INK,
                 arrowprops=dict(arrowstyle="-", color=INK, lw=1.2, shrinkB=10))
    axr.set_xticks(range(0, 361, 90))
    axr.set_xlim(-12, 372)
    axr.set_ylim(-120, 1950)
    axr.set_xlabel("chopper phase, degrees of the drive cycle", color=MUTED, fontsize=12)
    axr.set_ylabel("DRV_OUT reading, mV", color=MUTED, fontsize=12)
    axr.set_title("2.  One reading per phase: 16 phases trace this curve", loc="left", fontsize=15,
                  color=INK, pad=10)

    # Right bottom: the RC filter settling at 180 degrees.
    axs = fig.add_subplot(grid[2, 1])
    lk.style(axs)
    k180 = lk.N / 2
    dt = period / 64
    steps = int(SETTLE_S * 0.3 / dt)       # 0.6 ms is enough to show the settle
    tt = np.arange(steps) * dt
    vin = np.where(listening(tt + dt / 2, period, k180), drv(tt + dt / 2), 0.0)
    v = np.empty(steps)
    acc = 0.0
    a = math.exp(-dt / TAU_S)
    for i in range(steps):
        acc = vin[i] + (acc - vin[i]) * a
        v[i] = acc
    axs.plot(tt * 1e3, v * 1000, color=PHASE_COLOURS[180], lw=2.0)
    axs.axhline(1650, color=PHASE_COLOURS[180], lw=1.2, ls=(0, (6, 3)))
    axs.set_xlim(0, tt[-1] * 1e3)
    axs.set_ylim(0, 1900)
    axs.set_xlabel("time after the phase is set, ms", color=MUTED, fontsize=12)
    axs.set_ylabel("mV", color=MUTED, fontsize=12)
    axs.set_title("3.  The RC filter settles in ~0.5 ms (read at 2 ms)",
                  loc="left", fontsize=15, color=INK, pad=10)

    fig.text(0.05, 0.955, "1.  The switch passes DRV to the 100 kΩ / 1 nF filter for half of each cycle, at a fixed phase",
             fontsize=15, color=INK)
    fig.text(0.05, 0.915, "grey line: DRV.  tint: when the filter is connected.  filled: what it receives.  "
             "dashed: the average it settles to.  SENSE has an identical switch and filter on the same window.",
             fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)
    for deg in SHOW_DEG:
        print(f"phase {deg:3d} deg: DRV_OUT {lk.chopper_reading(drv, f, deg / 360.0 * lk.N) * 1000:7.1f} mV")
    print(f"settled to {v[-1] * 1000:.1f} mV after {tt[-1] * 1e3:.2f} ms; ripple {np.ptp(v[-128:]) * 1000:.1f} mV p-p")


if __name__ == "__main__":
    main()
