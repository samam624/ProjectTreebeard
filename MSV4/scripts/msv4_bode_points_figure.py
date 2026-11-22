#!/usr/bin/env python3
"""From the chopper readings to the points on the Bode plot.

Computed, not a capture, on the same board-1 moist circuit and chopper model as
msv4_chopper_figure.py / msv4_lockin_fit_figure.py. At each sweep frequency (16 MHz / N for
N in 64..6) the firmware's specMeasure() steps the chopper through M = specPhases(N) phases, reads
DRV_OUT and SENSE_OUT at each, sums each channel as X = sum V_i e^(-j 2 pi i / M) (the sine part)
and stores H = X(SENSE) / X(DRV_OUT): one gain and one phase, one point on the Bode plot.

Panel 1: the 16 readings of each channel at 1 MHz with their fitted sines.
Panel 2: the two sine parts as arrows; their length ratio is the gain, the angle between them the phase.
Panel 3: H at all eight frequencies, against the circuit's own curve.

    python MSV4/scripts/msv4_bode_points_figure.py --out MSV4/docs/figures/MSV4_READINGS_TO_BODE_2026-11-18.png
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


def spec_phases(n: int) -> int:
    """Firmware specPhases(): the largest of 16, 12, 8, 6, 4 that divides N."""
    for m in (16, 12, 8, 6, 4):
        if m <= n and n % m == 0:
            return m
    return 0


SWEEP_N = (64, 48, 32, 24, 16, 12, 8, 6)
INK, BLUE, MUTED, SURFACE, GRID = lk.INK, lk.BLUE, lk.MUTED, lk.SURFACE, lk.GRID
HILITE = "#E8692D"


def reading(signal, f: float, frac: float) -> float:
    """Chopper reading in mV with the window at phase fraction `frac` of the cycle."""
    period = 1.0 / f
    t0 = (frac + 0.5) * period
    t = np.linspace(t0, t0 + period / 2, 3001)
    return 0.5 * float(np.mean(signal(t))) * 1000.0


def measure(f: float, m: int) -> tuple[np.ndarray, np.ndarray, complex, complex]:
    drv = lambda t: lk.waveforms(t, f)[0]
    sense = lambda t: lk.waveforms(t, f)[1]
    vd = np.array([reading(drv, f, i / m) for i in range(m)])
    vs = np.array([reading(sense, f, i / m) for i in range(m)])
    e = np.exp(-2j * np.pi * np.arange(m) / m)
    return vd, vs, complex(2.0 / m * np.sum(vd * e)), complex(2.0 / m * np.sum(vs * e))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    points = []
    for n in SWEEP_N:
        f = lk.CLOCK_HZ / n
        m = spec_phases(n)
        vd, vs, xd, xs = measure(f, m)
        points.append({"f": f, "n": n, "m": m, "vd": vd, "vs": vs, "xd": xd, "xs": xs, "h": xs / xd})
    one = next(p for p in points if p["n"] == 16)

    fig = plt.figure(figsize=(18, 7.6), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(2, 3, width_ratios=[1.15, 0.85, 1.05], hspace=0.42, wspace=0.3,
                            left=0.05, right=0.985, top=0.83, bottom=0.1)
    ax1 = fig.add_subplot(grid[:, 0])
    ax2 = fig.add_subplot(grid[:, 1])
    axg = fig.add_subplot(grid[0, 2])
    axp = fig.add_subplot(grid[1, 2], sharex=axg)
    for ax in (ax1, ax2, axg, axp):
        lk.style(ax)

    # 1: readings and fitted sines at 1 MHz.
    m = one["m"]
    th = np.arange(m) * 360.0 / m
    fine = np.linspace(0, 360, 721)
    for v, x, colour, name, lab_xy in ((one["vd"], one["xd"], INK, "DRV_OUT", (62, 1330)),
                                        (one["vs"], one["xs"], BLUE, "SENSE_OUT", (318, 1300))):
        p0 = float(np.mean(v))
        ax1.plot(fine, p0 + x.real * np.cos(np.radians(fine)) - x.imag * np.sin(np.radians(fine)),
                 color=colour, lw=2.0)
        ax1.scatter(th, v, s=60, color=colour, edgecolor=SURFACE, linewidth=1.5, zorder=3)
        ax1.text(*lab_xy, name, color=colour, fontsize=13, fontweight="bold", ha="center")
    pk_d = (math.degrees(-cmath.phase(one["xd"]))) % 360
    pk_s = (math.degrees(-cmath.phase(one["xs"]))) % 360
    ax1.annotate("", (pk_s, 1720), (pk_d, 1720), arrowprops=dict(arrowstyle="->", color=HILITE, lw=2.5,
                 mutation_scale=18))
    ax1.text((pk_d + pk_s) / 2, 1760, f"SENSE peaks {pk_s - pk_d:.0f}° later", ha="center", fontsize=12,
             color=HILITE, fontweight="bold")
    ax1.set_xticks(range(0, 361, 90))
    ax1.set_xlim(-10, 370)
    ax1.set_ylim(-150, 1900)
    ax1.set_xlabel("chopper phase, degrees", color=MUTED, fontsize=12)
    ax1.set_ylabel("filtered reading, mV", color=MUTED, fontsize=12)
    ax1.set_title("1.  Fit a sine to each channel's 16 readings", loc="left", fontsize=15, color=INK, pad=28)
    ax1.text(0, 1.02, "1 MHz; dots: readings, lines: P + I cos θ + Q sin θ", transform=ax1.transAxes,
             fontsize=12, color=MUTED)

    # 2: the two sine parts as arrows.
    zd, zs = one["xd"], one["xs"]
    rot = cmath.exp(-1j * cmath.phase(zd))          # turn DRV to point along +x
    zd, zs = zd * rot, zs * rot
    for z, colour, name in ((zd, INK, "DRV_OUT"), (zs, BLUE, "SENSE_OUT")):
        ax2.annotate("", (z.real, z.imag), (0, 0),
                     arrowprops=dict(arrowstyle="-|>", color=colour, lw=3, mutation_scale=22, shrinkA=0, shrinkB=0))
        ax2.text(z.real, z.imag - 45 if z.imag < 0 else z.imag + 20, f"{name}\n{abs(z):.0f} mV", color=colour,
                 fontsize=13, fontweight="bold", ha="right", va="top" if z.imag < 0 else "bottom")
    arc = np.linspace(0, cmath.phase(zs), 40)
    ax2.plot(260 * np.cos(arc), 260 * np.sin(arc), color=HILITE, lw=2)
    ax2.text(290, -60, f"{math.degrees(cmath.phase(zs)):.0f}°", color=HILITE, fontsize=13, fontweight="bold")
    h = one["h"]
    ax2.text(0.04, 0.96, f"gain = {abs(zs):.0f} ÷ {abs(zd):.0f} = {abs(h):.2f}\nphase = {math.degrees(cmath.phase(h)):+.0f}°",
             transform=ax2.transAxes, va="top", fontsize=14, color=INK, linespacing=1.6,
             bbox=dict(boxstyle="round,pad=0.5", facecolor="white", edgecolor=GRID))
    ax2.set_aspect("equal")
    ax2.set_xlim(-60, 760)
    ax2.set_ylim(-420, 400)
    ax2.set_xlabel("in phase with DRV, mV", color=MUTED, fontsize=12)
    ax2.set_ylabel("90° to DRV, mV", color=MUTED, fontsize=12)
    ax2.set_title("2.  Divide SENSE by DRV", loc="left", fontsize=15, color=INK, pad=28)
    ax2.text(0, 1.02, "each sine part drawn as an arrow", transform=ax2.transAxes, fontsize=12, color=MUTED)

    # 3: all eight points against the circuit.
    ff = np.logspace(math.log10(1e5), math.log10(6e6), 300)
    hc = np.array([lk.transfer(x, lk.RSOIL, lk.CM_PF, lk.CD_PF, lk.CP_PF) for x in ff])
    axg.plot(ff, np.abs(hc), color=MUTED, lw=1.4, ls=(0, (4, 3)))
    axp.plot(ff, np.degrees(np.angle(hc)), color=MUTED, lw=1.4, ls=(0, (4, 3)))
    fs = np.array([p["f"] for p in points])
    hs = np.array([p["h"] for p in points])
    axg.scatter(fs, np.abs(hs), s=70, color=BLUE, edgecolor=SURFACE, linewidth=1.5, zorder=3)
    axp.scatter(fs, np.degrees(np.angle(hs)), s=70, color=BLUE, edgecolor=SURFACE, linewidth=1.5, zorder=3)
    axg.scatter([one["f"]], [abs(h)], s=230, facecolor="none", edgecolor=HILITE, linewidth=2.5, zorder=4)
    axp.scatter([one["f"]], [math.degrees(cmath.phase(h))], s=230, facecolor="none", edgecolor=HILITE,
                linewidth=2.5, zorder=4)
    axg.annotate("1 MHz, from panel 2", (one["f"], abs(h)), xytext=(1.6e5, 0.55), fontsize=12, color=HILITE,
                 arrowprops=dict(arrowstyle="-", color=HILITE, lw=1.2, shrinkB=10))
    axg.set_xscale("log")
    axg.set_ylim(0.45, 1.02)
    axg.set_ylabel("gain", color=MUTED, fontsize=12)
    axp.set_ylabel("phase, °", color=MUTED, fontsize=12)
    axp.set_xlabel("frequency", color=MUTED, fontsize=12)
    axp.set_xticks([1e5, 3e5, 1e6, 3e6])
    axp.set_xticklabels(["100 kHz", "300 kHz", "1 MHz", "3 MHz"])
    plt.setp(axg.get_xticklabels(), visible=False)
    axg.set_title("3.  Repeat at 8 frequencies: the Bode plot", loc="left", fontsize=15, color=INK, pad=28)
    axg.set_title("3.  Repeat at 8 frequencies: the Bode plot", loc="left", fontsize=15, color=INK, pad=44)
    axg.text(0, 1.04, "dots: measured;  dashed: the circuit.  The top two sit low:\n"
             "8 or 6 phases fold in harmonics, which the fit models", transform=axg.transAxes,
             fontsize=12, color=MUTED)

    fig.text(0.05, 0.955, "Board 1, moist pot, computed from the fitted circuit the way the firmware measures it "
             "(16, 12, 8 or 6 phases per frequency).", fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)
    for p in points:
        hc1 = lk.transfer(p["f"], lk.RSOIL, lk.CM_PF, lk.CD_PF, lk.CP_PF)
        print(f"{p['f'] / 1e3:7.0f} kHz  M={p['m']:2d}  H {abs(p['h']):.4f} {math.degrees(cmath.phase(p['h'])):+7.2f} deg"
              f"   circuit {abs(hc1):.4f} {math.degrees(cmath.phase(hc1)):+7.2f} deg")


if __name__ == "__main__":
    main()
