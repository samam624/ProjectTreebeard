#!/usr/bin/env python3
"""How one sweep frequency becomes one point on the Bode plot: square drive -> SENSE -> chopper -> sine fit.

Computed, not a capture: the probe never sends its raw waveforms. The SENSE waveform is the steady
state of a 0-3.3 V square wave through the fitted front end, H = 1 / (1 + Rs*Y) (the same formula
msv4_bode_figure.py draws), summed over odd harmonics. The chopper readings follow
MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md: at phase k the U4 filter listens during ticks
[k+N/2, k+N) and is parked on GND for the other half, so each reading is 1/2 x the mean of the
signal over that half period. The fit is the firmware's fitPhases (P + c cos(th) + s sin(th) over M
equally spaced phases), and H = X1(SENSE) / X1(DRV_OUT).

Element values are board 1's median fit, "Moist, after two cups" (2026-10-10 21:14 to 2026-10-11
01:20, 239 readings from data/sensor_logs/cache/sensor10_raw.csv): Rsoil 2987 ohm, Cm 120.2 pF,
Cd 34.75 pF, with that boot's Cp 8.69 pF.

    python MSV4/scripts/msv4_lockin_fit_figure.py --out MSV4/docs/figures/MSV4_LOCKIN_FIT_STEPS_2026-11-18.png
"""
from __future__ import annotations

import argparse
import cmath
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from msv4_bode_figure import transfer

CLOCK_HZ = 16_000_000
N = 16            # timer period in ticks: 16 MHz / 16 = 1 MHz, the routine frequency
M = 16            # chopper phases
VDD = 3.3
RSOIL, CM_PF, CD_PF, CP_PF = 2987.1, 120.2, 34.75, 8.69
HARMONICS = 199   # odd harmonics summed for the square wave

INK = "#3A3F47"
BLUE = "#2A77D6"
SHADE = "#F0EEE8"
SURFACE = "#FCFCFA"
GRID = "#E6E4DE"
MUTED = "#6B7280"


def waveforms(t: np.ndarray, f: float) -> tuple[np.ndarray, np.ndarray]:
    """DRV and SENSE at times t (s): a 50 % square, high for the first half of each period."""
    w = 2.0 * math.pi * f
    # DRV is the ideal square itself (a truncated Fourier sum would ring at the edges). SENSE is the
    # harmonic sum through H, which rolls off fast enough that the truncation does not show.
    drv = np.where(np.mod(t * f, 1.0) < 0.5, VDD, 0.0)
    sense = np.full_like(t, VDD / 2.0)   # H(0) = 1: the electrode passes no DC
    for n in range(1, HARMONICS + 1, 2):
        amp = 2.0 * VDD / (n * math.pi)
        h = transfer(n * f, RSOIL, CM_PF, CD_PF, CP_PF)
        sense += amp * abs(h) * np.sin(n * w * t + cmath.phase(h))
    return drv, sense


def chopper_reading(signal, f: float, k: int) -> float:
    """1/2 x the mean of `signal(t)` over ticks [k+N/2, k+N) of one period."""
    period = 1.0 / f
    t0 = (k + N / 2) / N * period
    t = np.linspace(t0, t0 + period / 2, 4001)
    return 0.5 * float(np.mean(signal(t)))


def fit_phases(v: np.ndarray, ks: np.ndarray) -> tuple[float, float, float]:
    th = 2.0 * math.pi * ks / N
    return float(v.mean()), float(2.0 / len(v) * np.sum(v * np.cos(th))), float(2.0 / len(v) * np.sum(v * np.sin(th)))


def style(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right", "left", "bottom"):
        ax.spines[side].set_visible(False)
    ax.grid(True, color=GRID, lw=1)
    ax.tick_params(colors=MUTED, labelsize=12, length=0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    f = CLOCK_HZ / N
    period = 1.0 / f
    drv_of = lambda t: waveforms(t, f)[0]
    sense_of = lambda t: waveforms(t, f)[1]

    ks = np.array([i * N // M for i in range(M)])
    v_drv = np.array([chopper_reading(drv_of, f, k) for k in ks]) * 1000.0
    v_sense = np.array([chopper_reading(sense_of, f, k) for k in ks]) * 1000.0
    pd, cd, sd = fit_phases(v_drv, ks)
    ps, cs, ss = fit_phases(v_sense, ks)
    h_meas = complex(cs, -ss) / complex(cd, -sd)
    h_true = transfer(f, RSOIL, CM_PF, CD_PF, CP_PF)
    print(f"f = {f/1e6:.3f} MHz")
    print(f"DRV_OUT  P={pd:.1f} I={cd:.1f} Q={sd:.1f} A={math.hypot(cd, sd):.1f} mV")
    print(f"SENSE_OUT P={ps:.1f} I={cs:.1f} Q={ss:.1f} A={math.hypot(cs, ss):.1f} mV")
    print(f"H from the fit  {abs(h_meas):.4f} at {math.degrees(cmath.phase(h_meas)):+.2f} deg")
    print(f"H of the circuit {abs(h_true):.4f} at {math.degrees(cmath.phase(h_true)):+.2f} deg")

    fig = plt.figure(figsize=(16, 7.2), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(2, 2, width_ratios=[1.15, 1], hspace=0.55, wspace=0.16,
                            left=0.045, right=0.985, top=0.9, bottom=0.1)
    ax_d = fig.add_subplot(grid[0, 0])
    ax_s = fig.add_subplot(grid[1, 0], sharex=ax_d)
    ax_r = fig.add_subplot(grid[:, 1])
    for ax in (ax_d, ax_s, ax_r):
        style(ax)

    t = np.linspace(0.0, 2.0 * period, 4000)
    drv, sense = waveforms(t, f)
    us = t * 1e6
    w = 2.0 * math.pi * f
    drv_fund = VDD / 2.0 + 2.0 * VDD / math.pi * np.sin(w * t)
    h1 = transfer(f, RSOIL, CM_PF, CD_PF, CP_PF)
    sense_fund = VDD / 2.0 + 2.0 * VDD / math.pi * abs(h1) * np.sin(w * t + cmath.phase(h1))

    # The half period the chopper listens to at one phase (k = 4), in both periods drawn.
    k_show = 4
    for p in range(2):
        start = (p + (k_show + N / 2) / N) * period * 1e6
        for ax in (ax_d, ax_s):
            ax.axvspan(start, min(start + period / 2 * 1e6, 2.0 * period * 1e6), color=SHADE, zorder=0)

    ax_d.plot(us, drv, color=INK, lw=2.4)
    ax_d.plot(us, drv_fund, color=INK, lw=1.4, ls=(0, (5, 4)), alpha=0.7)
    ax_d.set_title("1.  DRV: the drive is a 0–3.3 V square wave (dashed: its 1 MHz sine part)",
                   loc="left", fontsize=14, color=INK, pad=10)
    ax_d.set_ylabel("volts", color=MUTED, fontsize=12)
    ax_d.set_ylim(-0.9, 4.2)

    ax_s.plot(us, sense, color=BLUE, lw=2.4)
    ax_s.plot(us, sense_fund, color=BLUE, lw=1.4, ls=(0, (5, 4)), alpha=0.8)
    ax_s.set_title("2.  SENSE: after R15 and the electrode, smaller and later",
                   loc="left", fontsize=14, color=INK, pad=10)
    ax_s.set_ylabel("volts", color=MUTED, fontsize=12)
    ax_s.set_xlabel("time, µs (two cycles at 1 MHz)", color=MUTED, fontsize=12)
    ax_s.set_ylim(-0.9, 4.2)

    th_deg = 360.0 * ks / N
    th_fine = np.linspace(0.0, 360.0, 721)
    rad = np.radians(th_fine)
    ax_r.plot(th_fine, pd + cd * np.cos(rad) + sd * np.sin(rad), color=INK, lw=2.0)
    ax_r.plot(th_fine, ps + cs * np.cos(rad) + ss * np.sin(rad), color=BLUE, lw=2.0)
    ax_r.scatter(th_deg, v_drv, s=70, color=INK, edgecolor=SURFACE, linewidth=2, zorder=3)
    ax_r.scatter(th_deg, v_sense, s=70, color=BLUE, edgecolor=SURFACE, linewidth=2, zorder=3)
    ax_r.set_title("3.  Step the chopper phase 16 times and fit a sine to each",
                   loc="left", fontsize=14, color=INK, pad=10)
    ax_r.set_xlabel("chopper phase, degrees of the drive cycle", color=MUTED, fontsize=12)
    ax_r.set_ylabel("filtered reading, mV", color=MUTED, fontsize=12)
    ax_r.set_xticks(range(0, 361, 90))
    ax_r.set_xlim(-10, 370)
    ax_r.set_ylim(-150, 2050)

    ax_r.text(120, 1620, "DRV_OUT", ha="right", fontsize=13, color=INK, fontweight="bold")
    ax_r.annotate("SENSE_OUT", (292.5, v_sense[13]), xytext=(330, 1300), ha="center", fontsize=13, color=BLUE,
                  fontweight="bold", arrowprops=dict(arrowstyle="-", color=BLUE, lw=1.2, shrinkB=8))

    deg = math.degrees(cmath.phase(h_meas))
    ax_r.text(0.5, 0.03,
              f"SENSE ÷ DRV = {abs(h_meas):.2f} at {deg:+.0f}°\n"
              f"= one point on the Bode plot at 1 MHz\n"
              f"dots: readings    lines: fitted P + I cos θ + Q sin θ",
              transform=ax_r.transAxes, ha="center", va="bottom", fontsize=12, color=INK, linespacing=1.5,
              bbox=dict(boxstyle="round,pad=0.6", facecolor="white", edgecolor=GRID))

    fig.text(0.045, 0.965, "Board 1, moist pot (fitted Rsoil 2,987 Ω, Cm 120 pF), computed from the fitted circuit, "
             "not a scope capture.  Shaded: the half cycle the chopper averages at one phase setting.",
             fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
