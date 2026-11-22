#!/usr/bin/env python3
"""From a measured Bode plot to the circuit: how the fit finds Rsoil, Cm and Cd.

MEASURED input: one raw sweep that probe 11 (board 2, R15 3.3 kohm) sent to the cloud, from
data/sensor_logs/cache/sensor11_sweep_fits.csv. The model is the firmware's (specNodeH +
specModel): H = 1 / (1 + Rs (jw (Cd + Cp) + 1/(Rsoil + 1/(jw Cm)))), with each point folded the
way M phases fold the square wave's harmonics. The fit is Levenberg-Marquardt on ln(Cm, Rsoil, Cd)
like the firmware's specFit (lambda 1e-3, x0.3 on a step that helps, x10 on one that does not),
started COLD from a generic guess so the steps show; the firmware normally warm-starts from the
previous wake's answer. The final values are checked against the cloud fit of the same sweep.

Panel 1: the measured points and the model at the start, after each step, and at the end.
Panel 2: what each element does to the curve (each one halved and doubled about the answer).
Panel 3: how far the model is from the points at each step.

    python MSV4/scripts/msv4_circuit_fit_figure.py --out MSV4/docs/figures/MSV4_BODE_TO_CIRCUIT_FIT_2026-11-18.png
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import msv4_lockin_fit_figure as lk

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / "data" / "sensor_logs" / "cache" / "sensor11_sweep_fits.csv"
CLOCK = 16_000_000
PROBE_N = (64, 48, 32, 24, 16, 12, 8, 6)
RS = 3300.0
CP = 8.75e-12
TARGET = datetime(2026, 10, 28, 21, 27, tzinfo=timezone.utc)   # 10-28 14:27 PDT, drying
START = (30e-12, 20000.0, 15e-12)                               # cold start: Cm, Rsoil, Cd
INK, BLUE, MUTED, SURFACE, GRID = lk.INK, lk.BLUE, lk.MUTED, lk.SURFACE, lk.GRID
STEP_COLOURS = ["#C9CCD1", "#B49FDB", "#9579CC", "#7656B8", "#583D96", "#3B2770"]


def spec_phases(n: int) -> int:
    for m in (16, 12, 8, 6, 4):
        if m <= n and n % m == 0:
            return m
    return 0


def node_h(w: float, p) -> complex:
    cm, r, cd = p
    return 1.0 / (1.0 + RS * (1j * w * (cd + CP) + 1.0 / (r + 1.0 / (1j * w * cm))))


def model(p) -> np.ndarray:
    """specModel: each sweep point with the harmonics its M phases fold onto it."""
    out = []
    for n in PROBE_N:
        m = spec_phases(n)
        w1 = 2.0 * math.pi * CLOCK / n
        num, den = 0j, 0.0
        for k in range(-8, 9):
            h = 1 + k * m
            if h % 2 == 0 or h == 0 or abs(h) > 63:
                continue
            hv = node_h(w1 * abs(h), p)
            num += (hv.conjugate() if h < 0 else hv) / (h * h)
            den += 1.0 / (h * h)
        out.append(num / den)
    return np.array(out)


def residual(p, meas) -> np.ndarray:
    d = model(p) - meas
    return np.concatenate([d.real, d.imag])


def lm(meas, start) -> list[tuple]:
    """Levenberg-Marquardt on ln(p). Returns the accepted parameter sets, start first."""
    x = np.log(np.array(start))
    lam = 1e-3
    path = [tuple(np.exp(x))]
    r = residual(np.exp(x), meas)
    ss = float(r @ r)
    for _ in range(60):
        jac = np.empty((len(r), 3))
        for j in range(3):
            dx = np.zeros(3)
            dx[j] = 1e-6
            jac[:, j] = (residual(np.exp(x + dx), meas) - r) / 1e-6
        a = jac.T @ jac
        g = jac.T @ r
        improved = False
        for _ in range(12):
            try:
                step = np.linalg.solve(a + lam * np.diag(np.diag(a)) + 1e-12 * np.eye(3), -g)
            except np.linalg.LinAlgError:
                lam *= 10
                continue
            step = np.clip(step, -1.0, 1.0)          # at most a factor e per parameter per step
            r2 = residual(np.exp(x + step), meas)
            ss2 = float(r2 @ r2)
            if np.isfinite(ss2) and ss2 < ss:
                gain = (ss - ss2) / ss
                x, r, ss = x + step, r2, ss2
                lam *= 0.3
                improved = True
                break
            lam *= 10
        if not improved:
            break
        path.append(tuple(np.exp(x)))
        if gain < 1e-4:                       # under 0.01 % better: done
            break
    return path


def rms_pct(p, meas) -> float:
    r = residual(p, meas)
    return 100.0 * math.sqrt(float(np.mean(r * r)))


def load_sweep() -> dict:
    best = None
    with CSV.open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            t = datetime.fromisoformat(row["observed_at"].replace("+00", "+00:00"))
            if best is None or abs(t - TARGET) < abs(best[0] - TARGET):
                best = (t, row)
    t, row = best
    meas = np.array([complex(float(row[f"re{i}"]), float(row[f"im{i}"])) for i in range(8)])
    return {"time": t, "h": meas, "cloud_r": float(row["lumped_r_ohm"]),
            "cloud_cm": float(row["lumped_mask_pf"]), "cloud_cd": float(row["lumped_cd_pf"])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    sw = load_sweep()
    meas = sw["h"]
    path = lm(meas, START)
    final = path[-1]
    freqs = np.array([CLOCK / n for n in PROBE_N])

    print(f"sweep {sw['time'].astimezone():%Y-%m-%d %H:%M %Z}")
    for i, p in enumerate(path):
        print(f"  step {i:2d}: Cm {p[0] * 1e12:7.1f} pF  Rsoil {p[1]:8.0f} ohm  Cd {p[2] * 1e12:6.2f} pF  "
              f"residual {rms_pct(p, meas):.3f} % of drive")
    print(f"  cloud fit of the same sweep: Cm {sw['cloud_cm']:.1f} pF  Rsoil {sw['cloud_r']:.0f} ohm  "
          f"Cd {sw['cloud_cd']:.2f} pF")

    fig = plt.figure(figsize=(18, 7.8), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(6, 3, width_ratios=[1.15, 0.9, 0.9], hspace=1.3, wspace=0.3,
                            left=0.05, right=0.985, top=0.84, bottom=0.09)
    axg = fig.add_subplot(grid[0:3, 0])
    axp = fig.add_subplot(grid[3:6, 0], sharex=axg)
    axe = [fig.add_subplot(grid[2 * i:2 * i + 2, 1]) for i in range(3)]
    axr = fig.add_subplot(grid[0:4, 2])
    for ax in [axg, axp, axr] + axe:
        lk.style(ax)

    # 1: the model converging on the measured points.
    shown = list(range(len(path)))
    if len(shown) > len(STEP_COLOURS):
        shown = [0] + list(np.linspace(1, len(path) - 2, len(STEP_COLOURS) - 2).round().astype(int)) + [len(path) - 1]
    for j, i in enumerate(shown):
        h = model(path[i])
        last = i == len(path) - 1
        colour = BLUE if last else STEP_COLOURS[min(j, len(STEP_COLOURS) - 2)]
        label = "start (a guess)" if i == 0 else ("fitted" if last else (f"step {i}" if j in (1, len(shown) - 2) else None))
        lw = 2.6 if last else 1.6
        axg.plot(freqs, np.abs(h), color=colour, lw=lw, label=label, zorder=2 + j)
        axp.plot(freqs, np.degrees(np.angle(h)), color=colour, lw=lw, zorder=2 + j)
    axg.scatter(freqs, np.abs(meas), s=80, color=INK, zorder=20, label="measured")
    axp.scatter(freqs, np.degrees(np.angle(meas)), s=80, color=INK, zorder=20)
    axg.set_xscale("log")
    axg.set_ylabel("gain", color=MUTED, fontsize=12)
    axp.set_ylabel("phase, °", color=MUTED, fontsize=12)
    axp.set_xlabel("frequency", color=MUTED, fontsize=12)
    axp.set_xticks([3e5, 1e6, 2e6])
    axp.set_xticklabels(["300 kHz", "1 MHz", "2 MHz"])
    plt.setp(axg.get_xticklabels(), visible=False)
    axg.legend(loc="upper right", fontsize=11, frameon=False)
    from matplotlib.ticker import NullFormatter
    for ax in [axg, axp] + axe:
        ax.xaxis.set_minor_formatter(NullFormatter())
    axg.set_title("1.  Adjust the circuit until it lands on the points", loc="left", fontsize=15, color=INK, pad=28)
    axg.text(0, 1.04, f"probe 11, one measured sweep, {sw['time'].astimezone():%m-%d %H:%M}",
             transform=axg.transAxes, fontsize=12, color=MUTED)

    # 2: what each element does.
    ff = np.logspace(math.log10(1e5), math.log10(8e6), 300)
    curve = lambda p: np.array([abs(node_h(2 * math.pi * f, p)) for f in ff])
    names = (("Rsoil", 1, "sets how deep the dip is"), ("Cm (mask)", 0, "sets where it starts"),
             ("Cd", 2, "moves the top roll-off"))
    for ax, (name, idx, what) in zip(axe, names):
        for factor, colour, ls in ((0.5, "#B49FDB", (0, (4, 3))), (2.0, "#583D96", (0, (1, 2)))):
            p = list(final)
            p[idx] *= factor
            ax.plot(ff, curve(p), color=colour, lw=1.8, ls=ls, label=f"{name} ×{factor:g}")
        ax.plot(ff, curve(final), color=BLUE, lw=2.4)
        ax.axvspan(freqs[0], freqs[-1], color="#F0EEE8", zorder=0)
        ax.set_xscale("log")
        ax.set_ylim(0, 1.05)
        ax.set_xticks([1e5, 1e6])
        ax.set_xticklabels(["100 kHz", "1 MHz"])
        ax.text(0.98, 0.92, f"{name}: {what}", transform=ax.transAxes, ha="right", va="top", fontsize=12,
                color=INK, fontweight="bold")
        ax.legend(loc="lower left", fontsize=10, frameon=False)
    axe[0].set_title("2.  What each part does (gain)", loc="left", fontsize=15, color=INK, pad=28)
    axe[0].text(0, 1.06, "blue: the fit; each part halved and doubled; shaded: the sweep",
                transform=axe[0].transAxes, fontsize=12, color=MUTED)

    # 3: convergence.
    res = [rms_pct(p, meas) for p in path]
    axr.plot(range(len(res)), res, color=INK, lw=2, marker="o", ms=7)
    axr.set_yscale("log")
    axr.set_yticks([2, 5, 10, 20])
    axr.set_yticklabels(["2 %", "5 %", "10 %", "20 %"])
    axr.yaxis.set_minor_formatter(NullFormatter())
    axr.set_xlabel("step", color=MUTED, fontsize=12)
    axr.set_ylabel("model minus measured, rms", color=MUTED, fontsize=12)
    axr.set_title("3.  Each step gets closer", loc="left", fontsize=15, color=INK, pad=28)
    axr.text(0, 1.04, "Levenberg-Marquardt, as in the firmware", transform=axr.transAxes, fontsize=12, color=MUTED)
    fig.text(0.70, 0.24,
             f"fitted:  Rsoil {final[1]:,.0f} Ω   Cm {final[0] * 1e12:.0f} pF   Cd {final[2] * 1e12:.1f} pF\n"
             f"cloud fit of the same sweep:  Rsoil {sw['cloud_r']:,.0f} Ω   Cd {sw['cloud_cd']:.1f} pF\n"
             f"left over: {res[-1]:.2f} % of drive\n"
             "Rsoil goes on to the moisture number",
             fontsize=12.5, color=INK, linespacing=1.7, va="top")

    fig.text(0.05, 0.955, "Model: H = 1 / (1 + Rs (jω(Cd + Cp) + 1 / (Rsoil + 1/(jωCm)))), Rs = R15 = 3.3 kΩ, "
             "Cp = 8.75 pF, each point folded as its phases fold the square wave.", fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)


if __name__ == "__main__":
    main()
