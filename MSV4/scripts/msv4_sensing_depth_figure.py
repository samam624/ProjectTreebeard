#!/usr/bin/env python3
"""Sensing depth into soil: the pitch-only equation against the 2D field solve.

Draws MSV4/docs/MSV4_SENSING_DEPTH_EQUATION_VS_SIM_2026-09-30.png and prints the table.

Equation (MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md), lambda = 2 x electrode
centre-to-centre spacing:

    penetration depth      = lambda / 2*pi                 (the 63 % depth)
    share within depth d   = 1 - exp(-2*pi*d / lambda)     (same rule as the coating share)

Sim: finite-volume solve of the blade cross-section (msv4_electrode_fieldsolve.py), lossless soil
er 20, 1.6 mm FR4, 20 um mask; soil beyond d replaced by air, share = C(d) / C(all soil).
MSV4 and v1.2 are read from MSV4/generated/electrode_compare_2026-09-23.json. The MSV3 IDE row is
transcribed from MSV4/docs/MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md (the code for that run is not
tracked). All numbers are modelled; none is measured.

    python MSV4/scripts/msv4_sensing_depth_figure.py [--no-figure]
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
FIGURE = ROOT / "docs" / "MSV4_SENSING_DEPTH_EQUATION_VS_SIM_2026-09-30.png"
SLIDE = FIGURE.with_name(FIGURE.stem + "_SLIDE.png")     # 9 x 6 in, 1:1 on a 10 x 7.5 in slide
COMPARE = ROOT / "generated" / "electrode_compare_2026-09-23.json"

# name, pattern, lambda (mm), sim key or inline data, colour (categorical slots 1-3)
ELECTRODES = [
    ("MSV3 IDE", "0.5 / 1.0 interdigitated", 3.0, "msv3", "#2a78d6"),
    ("v1.2", "G3.5 / 2 / H9.5 / 2 / G3.5", 17.0, "v12", "#eb6834"),
    ("MSV4 as drawn", "A4.725 / 4.725 / B4.725 + guard", 18.9, "msv4", "#1baf7a"),
]
MSV3_SIM = [(0.25, 55.0), (0.5, 81.0), (1.0, 95.0), (2.0, 99.0)]   # 09-19 doc, er 20


def sim_curves():
    rows = json.loads(COMPARE.read_text())["penetration_er20"]
    out = {"msv3": MSV3_SIM}
    for key in ("msv4", "v12"):
        out[key] = [(r["depth_mm"], r[f"{key}_share_pct"]) for r in rows]
    return out


def eq_share(d, lam):
    return 100 * (1 - np.exp(-2 * np.pi * np.asarray(d) / lam))


def eq_depth(pct, lam):
    return -np.log(1 - pct / 100) * lam / (2 * np.pi)


def sim_depth(points, pct):
    """Linear interpolation on the sim grid, anchored at (0, 0)."""
    d, s = zip(*([(0.0, 0.0)] + points))
    return float(np.interp(pct, s, d))


def table(curves):
    rows = []
    for name, pattern, lam, key, _ in ELECTRODES:
        rows.append(dict(name=name, pattern=pattern, lam=lam,
                         eq63=lam / (2 * np.pi), eq50=eq_depth(50, lam),
                         sim63=sim_depth(curves[key], 100 * (1 - np.exp(-1))),
                         sim50=sim_depth(curves[key], 50)))
    return rows


def draw(curves, rows, slide=False):
    """slide=True: 9 x 6 in at 1:1 for a 10 x 7.5 in slide (fonts >= 11 pt), no in-figure title
    (the slide has one), and the pattern column dropped from the table."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    ink, ink2, grid = "#0b0b0b", "#52514e", "#e4e3df"
    k = 1.3 if slide else 1.0                      # font scale
    bg = "#ffffff" if slide else "#fcfcfb"
    fig = plt.figure(figsize=(9.0, 6.0) if slide else (8.6, 7.4), facecolor=bg)
    ax = fig.add_axes([0.09, 0.4, 0.83, 0.5] if slide else [0.09, 0.36, 0.86, 0.56], facecolor=bg)
    d = np.logspace(np.log10(0.08), 1, 400)

    for (name, _, lam, key, col), (lx, ly, li) in zip(ELECTRODES, ((0.2, 55, 0), (0.2, 9, 0),
                                                                  (0.42, 42, 1))):
        ax.plot(d, eq_share(d, lam), color=col, lw=1.6, ls=(0, (5, 3)))
        x, y = zip(*curves[key])
        ax.plot(x, y, color=col, lw=2.0 * k, marker="o", ms=6.5 * k, mec=bg, mew=1.5, zorder=3)
        ax.annotate(name, (x[li], y[li]), xytext=(lx, ly), textcoords="data",
                    ha="right", va="center", fontsize=9.5 * k, color=ink,
                    arrowprops=dict(arrowstyle="-", color=col, lw=1.2, shrinkA=2, shrinkB=5))

    for pct, txt in ((50, "50 %: median depth"), (100 * (1 - np.exp(-1)), "63 %: λ/2π by the equation")):
        ax.axhline(pct, color=ink2, lw=0.8, ls=":", zorder=1)
        if slide:      # curves cross the in-plot label position at this font size: label in the margin
            ax.text(10.4, pct, f"{pct:.0f} %", ha="left", va="center", fontsize=8.5 * k, color=ink2,
                    clip_on=False)
        else:
            ax.text(9.6, pct + 1.2, txt, ha="right", va="bottom", fontsize=8.5 * k, color=ink2)

    ax.set_xscale("log")
    ax.set_xlim(0.08, 10)
    ax.set_ylim(0, 102)
    ax.set_xticks([0.1, 0.25, 0.5, 1, 2, 3, 5, 8])
    ax.set_xticklabels(["0.1", "0.25", "0.5", "1", "2", "3", "5", "8"])
    ax.set_xlabel("depth into soil from the blade surface, d (mm)", color=ink2, fontsize=10 * k)
    ax.set_ylabel("share of signal from within d (%)" if slide else
                  "share of the full soil response from within d (%)", color=ink2, fontsize=10 * k)
    ax.grid(True, which="major", color=grid, lw=0.8)
    ax.tick_params(colors=ink2, labelsize=9 * k)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(grid)
    ax.legend(handles=[Line2D([], [], color=ink2, lw=2, marker="o", ms=6, label="2D field solve"),
                       Line2D([], [], color=ink2, lw=1.6, ls=(0, (5, 3)),
                              label="equation  1 − exp(−2πd/λ)")],
              loc="lower right", bbox_to_anchor=(1.0, 0.06), frameon=False, fontsize=9 * k)
    if slide:
        fig.text(0.09, 0.955, "Lossless soil εr 20, 1.6 mm FR4, 20 µm mask. Modelled, not measured.",
                 fontsize=11, color=ink2)
        fig.text(0.09, 0.917, "Dotted lines: 50 % = median depth; 63 % = λ/2π by the equation. "
                 "Depths in mm.", fontsize=11, color=ink2)
    else:
        fig.text(0.09, 0.965, "Sensing depth into soil: pitch-only equation vs field solve",
                 fontsize=12.5, weight="bold", color=ink)
        fig.text(0.09, 0.935, "Lossless soil er 20, 1.6 mm FR4, 20 µm mask. Modelled, not measured.",
                 fontsize=9, color=ink2)

    tab = fig.add_axes([0.09, 0.03, 0.89, 0.22] if slide else [0.09, 0.07, 0.86, 0.18])
    tab.axis("off")
    if slide:
        cells = [[r["name"], f"{r['lam']:.1f}", f"{r['eq63']:.2f}", f"{r['sim63']:.2f}",
                  f"{r['eq50']:.2f}", f"{r['sim50']:.2f}"] for r in rows]
        t = tab.table(cellText=cells, bbox=[0, 0, 1, 1], cellLoc="center",
                      colLabels=["electrode", "λ", "λ/2π, equation", "63 %, sim", "50 %, equation",
                                 "50 %, sim"],
                      colWidths=[0.2, 0.1, 0.18, 0.16, 0.19, 0.17])
        t.auto_set_font_size(False)
        t.set_fontsize(11.5)
    else:
        cells = [[r["name"], r["pattern"], f"{r['lam']:.1f}", f"{r['eq63']:.2f}", f"{r['sim63']:.2f}",
                  f"{r['eq50']:.2f}", f"{r['sim50']:.2f}"] for r in rows]
        t = tab.table(cellText=cells, loc="upper center", cellLoc="center",
                      colLabels=["electrode", "pattern (mm)", "λ (mm)", "λ/2π\nequation",
                                 "63 % depth\nsim", "50 % depth\nequation", "50 % depth\nsim"],
                      colWidths=[0.15, 0.29, 0.08, 0.12, 0.12, 0.12, 0.12])
        t.auto_set_font_size(False)
        t.set_fontsize(9)
        t.scale(1, 1.9)
    for (r, c), cell in t.get_celld().items():
        cell.set_edgecolor(grid)
        cell.set_facecolor(bg)
        cell.get_text().set_color(ink if r else ink2)
        if r and c == 0:
            cell.get_text().set_ha("left")
            cell.PAD = 0.04
    if not slide:
        fig.text(0.09, 0.03, "Depths in mm. Sim depths interpolated linearly on the solve grid "
                 "(0.25, 0.5, 1, 2, 3, 5, 8 mm). λ = 2 × electrode centre-to-centre spacing.",
                 fontsize=8, color=ink2)
    out = SLIDE if slide else FIGURE
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=200 if slide else 160, facecolor=fig.get_facecolor())
    print(f"wrote {out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-figure", action="store_true")
    args = ap.parse_args()
    curves = sim_curves()
    rows = table(curves)
    print(f"{'electrode':<15}{'lambda':>8}{'eq63':>8}{'sim63':>8}{'eq50':>8}{'sim50':>8}")
    for r in rows:
        print(f"{r['name']:<15}{r['lam']:>8.1f}{r['eq63']:>8.2f}{r['sim63']:>8.2f}"
              f"{r['eq50']:>8.2f}{r['sim50']:>8.2f}")
    if not args.no_figure:
        draw(curves, rows)
        draw(curves, rows, slide=True)


if __name__ == "__main__":
    main()
