#!/usr/bin/env python3
"""How the chopper readings are summed in the complex plane, 4 phases (plain I/Q) against 16.

Computed, not a capture, on the same board-1 moist circuit and chopper model as
msv4_lockin_fit_figure.py (1 MHz, N = 16). Each SENSE_OUT reading V_k, taken at chopper phase
theta_k = 2 pi k / M, becomes the vector (2/M) V_k e^(-j theta_k). Laid tip to tail they end at
X = I - jQ, the firmware's fitPhases result. The offset P cancels because the M unit vectors
e^(-j theta_k) sum to zero (the dashed loop closes on the origin).

Panel 3: the same readings with independent noise of SIGMA_MV on every reading, repeated TRIALS
times, for M = 4 and M = 16. The spread falls as sqrt(2/M) x sigma per axis: 16 phases have half
the noise of 4 (the same as four repeats of 4 phases would). What only more phases give is less
harmonic folding: with M phases the n-th harmonic lands on the fundamental when n = +-1 (mod M), so
M = 4 adds the 3rd, 5th, 7th ... and M = 16 only the 15th, 17th ...; the M = 4 cloud is centred
off the true 1 MHz sine part. SIGMA_MV is illustrative, not a measured ADC figure.

    python MSV4/scripts/msv4_phase_sum_figure.py --out MSV4/docs/figures/MSV4_PHASE_SUM_4_VS_16_2026-11-18.png
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

import msv4_lockin_fit_figure as lk

SIGMA_MV = 5.0
TRIALS = 3000
SEED = 7

INK = lk.INK
BLUE = lk.BLUE
ORANGE = "#E8692D"
MUTED = lk.MUTED
SURFACE = lk.SURFACE
GRID = lk.GRID


def readings(signal, f: float, m: int) -> np.ndarray:
    """SENSE_OUT (or DRV_OUT) readings in mV at M equally spaced chopper phases."""
    return np.array([lk.chopper_reading(signal, f, i * lk.N / m) for i in range(m)]) * 1000.0


def phasor(v: np.ndarray) -> complex:
    m = len(v)
    th = 2.0 * math.pi * np.arange(m) / m
    return complex(2.0 / m * np.sum(v * np.exp(-1j * th)))


def chain(ax, v: np.ndarray, colour: str, title: str, note: str) -> complex:
    lk.style(ax)
    m = len(v)
    th = 2.0 * math.pi * np.arange(m) / m
    steps = 2.0 / m * v * np.exp(-1j * th)
    pts = np.concatenate([[0], np.cumsum(steps)])
    p = float(v.mean())
    ghost = np.concatenate([[0], np.cumsum(2.0 / m * p * np.exp(-1j * th))])
    ax.plot(ghost.real, ghost.imag, color=MUTED, lw=1.2, ls=(0, (4, 4)), zorder=1)
    for a, b in zip(pts[:-1], pts[1:]):
        ax.annotate("", (b.real, b.imag), (a.real, a.imag),
                    arrowprops=dict(arrowstyle="-|>", color=colour, lw=2.0, mutation_scale=14,
                                    shrinkA=0, shrinkB=0), zorder=3)
    end = pts[-1]
    ax.annotate("", (end.real, end.imag), (0, 0),
                arrowprops=dict(arrowstyle="-|>", color=INK, lw=3.0, mutation_scale=20, shrinkA=0, shrinkB=0),
                zorder=4)
    ax.scatter([0], [0], s=40, color=INK, zorder=5)
    ax.text(end.real, end.imag + 45, f"I − jQ = {abs(end):.0f} mV at {math.degrees(cmath.phase(end)):+.0f}°",
            fontsize=12, color=INK, ha="left", va="bottom", fontweight="bold")
    ax.set_title(title, loc="left", fontsize=15, color=INK, pad=28)
    ax.text(0, 1.02, note, transform=ax.transAxes, fontsize=12, color=MUTED)
    ax.set_aspect("equal")
    ax.set_xlim(-750, 550)
    ax.set_ylim(-650, 350)
    ax.set_xlabel("real, mV", color=MUTED, fontsize=12)
    ax.set_ylabel("imaginary, mV", color=MUTED, fontsize=12)
    return end


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    f = lk.CLOCK_HZ / lk.N
    sense_of = lambda t: lk.waveforms(t, f)[1]
    drv_of = lambda t: lk.waveforms(t, f)[0]
    v4, v16 = readings(sense_of, f, 4), readings(sense_of, f, 16)
    true_x = phasor(readings(sense_of, f, 720))      # the pure 1 MHz sine part of the readings

    fig = plt.figure(figsize=(18, 7.0), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(1, 3, width_ratios=[1, 1, 1.05], wspace=0.3,
                            left=0.05, right=0.985, top=0.84, bottom=0.11)
    ax4, ax16, axn = (fig.add_subplot(grid[0, i]) for i in range(3))
    x4 = chain(ax4, v4, ORANGE, "1.  4 phases: plain I/Q",
               "the 4 readings at 0°, 90°, 180°, 270°, tip to tail")
    x16 = chain(ax16, v16, BLUE, "2.  16 phases: the sweep",
                "16 readings, 22.5° apart, tip to tail")
    for ax in (ax4, ax16):
        ax.text(0.02, 0.03, "dashed: the offset P alone\nloops back to zero", transform=ax.transAxes,
                ha="left", va="bottom", fontsize=11, color=MUTED)

    rng = np.random.default_rng(SEED)
    lk.style(axn)
    spreads = {}
    for m, v, colour in ((4, v4, ORANGE), (16, v16, BLUE)):
        th = 2.0 * math.pi * np.arange(m) / m
        noisy = v[None, :] + rng.normal(0.0, SIGMA_MV, size=(TRIALS, m))
        xs = 2.0 / m * noisy @ np.exp(-1j * th)
        spreads[m] = float(np.sqrt(np.mean(np.abs(xs - xs.mean()) ** 2) / 2.0))
        axn.scatter(xs.real, xs.imag, s=6, color=colour, alpha=0.35, linewidths=0, zorder=2)
        centre = xs.mean()
        axn.scatter([centre.real], [centre.imag], s=90, color=colour, edgecolor=SURFACE, linewidth=2, zorder=4)
    axn.scatter([true_x.real], [true_x.imag], s=220, marker="+", color=INK, linewidths=2.5, zorder=5)
    axn.annotate("true 1 MHz\nsine part", (true_x.real, true_x.imag), xytext=(-18, 14), textcoords="offset points",
                 ha="right", fontsize=12, color=INK)
    off4 = abs(x4 - true_x)
    off16 = abs(x16 - true_x)
    axn.text(x4.real, x4.imag - 13, f"4 phases\nnoise ±{spreads[4]:.1f} mV\noff by {off4:.0f} mV",
             ha="center", va="top", fontsize=12, color=ORANGE, fontweight="bold")
    axn.text(x16.real, x16.imag - 10, f"16 phases\nnoise ±{spreads[16]:.1f} mV\noff by {off16:.1f} mV",
             ha="center", va="top", fontsize=12, color=BLUE, fontweight="bold")
    axn.set_aspect("equal")
    cx = (x4.real + x16.real) / 2
    cy = (x4.imag + x16.imag) / 2
    half = max(abs(x4.real - x16.real), abs(x4.imag - x16.imag)) / 2 + 45
    axn.set_xlim(cx - half, cx + half)
    axn.set_ylim(cy - half, cy + half)
    axn.set_title("3.  Same noise on every reading, 3,000 repeats", loc="left", fontsize=15, color=INK, pad=28)
    axn.text(0, 1.02, f"±{SIGMA_MV:.0f} mV per reading (illustrative); dot = cloud average",
             transform=axn.transAxes, fontsize=12, color=MUTED)
    axn.set_xlabel("real, mV", color=MUTED, fontsize=12)
    axn.set_ylabel("imaginary, mV", color=MUTED, fontsize=12)

    fig.text(0.05, 0.955, "SENSE_OUT readings at 1 MHz, board 1 in the moist pot, computed from the fitted circuit. "
             "Each reading becomes an arrow (2/M)·V·e^(−jθ); the arrows add up to I − jQ.", fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)

    h_true = lk.transfer(f, lk.RSOIL, lk.CM_PF, lk.CD_PF, lk.CP_PF)
    print(f"true sine part of SENSE_OUT: {abs(true_x):.1f} mV at {math.degrees(cmath.phase(true_x)):+.2f} deg")
    for m in (4, 8, 16):
        xs_, xd_ = phasor(readings(sense_of, f, m)), phasor(readings(drv_of, f, m))
        h = xs_ / xd_
        print(f"M={m:2}: SENSE_OUT {abs(xs_):6.1f} mV at {math.degrees(cmath.phase(xs_)):+7.2f} deg, "
              f"off the sine part by {abs(xs_ - true_x):5.1f} mV; H {abs(h):.4f} at {math.degrees(cmath.phase(h)):+.2f} deg "
              f"(circuit {abs(h_true):.4f} at {math.degrees(cmath.phase(h_true)):+.2f}); "
              f"noise per axis sqrt(2/M)*sigma = {math.sqrt(2 / m) * SIGMA_MV:.2f} mV")
    print(f"Monte Carlo spread per axis: M=4 {spreads[4]:.2f} mV, M=16 {spreads[16]:.2f} mV")


if __name__ == "__main__":
    main()
