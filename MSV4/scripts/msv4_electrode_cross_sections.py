#!/usr/bin/env python3
"""Annotated electrode cross-sections for v1.2, MSV3 and MSV4, with lambda and lambda/2pi.

Draws MSV4/docs/ELECTRODE_CROSS_SECTIONS_2026-09-30.{png,pdf}. All three sections share one mm scale
in x, and the 1/e field depth lambda/2pi is drawn to that scale on each.

Geometry sources:
  v1.2  G3.5 / 2 / H9.5 / 2 / G3.5, single sided -- msv4_electrode_compare.py (maintainer 2026-09-23)
  MSV3  0.5 mm fingers, 1.0 mm gaps, 50 fingers -- MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md;
        the section is taken across the fingers, and only a 13-finger window is drawn
  MSV4  as drawn, IPC-2581 export 2026-09-18 -- msv4_electrode_fieldsolve.py conductor table
Only MSV4's board thickness (1.6 mm) is recorded; the others are drawn at 1.6 mm too.

    python MSV4/scripts/msv4_electrode_cross_sections.py
"""

from __future__ import annotations

import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Patch, Rectangle  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "docs" / "ELECTRODE_CROSS_SECTIONS_2026-09-30"

INK, MUTED, RULE = "#1b1b1b", "#5a5a5a", "#c9c9c4"
SOIL, BOARD, HOT, GND = "#f5efe4", "#dfe5d6", "#1f4e8c", "#9aa3ad"
T_BOARD, T_CU = 1.6, 0.14            # mm; copper thickness exaggerated for visibility
XLIM, YLIM = (-12.5, 12.5), (-3.45, 3.75)
FS_DIM = 7.5
SLIDE = False            # set by main_slide(): 9 x 6 in figure, 1:1 on a 10 x 7.5 in slide
SLIDE_DESC = {"v1.2": "ground | driven | ground, one face",
              "MSV3": "interdigitated comb, across the fingers",
              "MSV4 as drawn": "A | B plates + guard ring, both faces"}

# (x0, x1, role) with role 'hot' or 'gnd'
V12 = [(-10.25, -6.75, "gnd"), (-4.75, 4.75, "hot"), (6.75, 10.25, "gnd")]
MSV4 = [(-8.745, -8.123, "gnd"), (-7.087, -2.3625, "hot"), (2.3625, 7.087, "gnd"),
        (8.123, 8.745, "gnd")]
MSV3 = [(1.5 * i - 0.25, 1.5 * i + 0.25, "hot" if i % 2 == 0 else "gnd") for i in range(-6, 7)]

ROWS = [
    # name, description, s (mm), s arithmetic, pattern text
    ("v1.2", "ground | driven | ground, single-sided", 8.5, "4.75 + 2.0 + 1.75",
     "G3.5 / 2.0 / H9.5 / 2.0 / G3.5"),
    ("MSV3", "interdigitated comb, section across the fingers", 1.5, "0.25 + 1.0 + 0.25",
     "0.5 finger / 1.0 gap, 50 fingers"),
    ("MSV4 as drawn", "A | B plates inside a guard ring, mirrored front and back", 9.45,
     "2.3625 + 4.725 + 2.3625", "A4.725 / 4.725 / B4.725"),
]


def hdim(ax, x0, x1, y, label, ext_from=None, above=True):
    ax.annotate("", xy=(x0, y), xytext=(x1, y),
                arrowprops=dict(arrowstyle="<->", lw=0.7, color=INK, shrinkA=0, shrinkB=0,
                                mutation_scale=6))
    if ext_from is not None:
        top, bot = (y + 0.12, ext_from) if above else (ext_from, y - 0.12)
        for x in (x0, x1):
            ax.plot([x, x], [bot, top], color=MUTED, lw=0.45)
    ax.text((x0 + x1) / 2, y + (0.1 if above else -0.12), label, ha="center",
            va="bottom" if above else "top", fontsize=FS_DIM, color=INK)


def depth_marker(ax, depth):
    ax.plot(XLIM, [depth, depth], color=HOT, lw=1.0, ls=(0, (5, 3)), zorder=2)
    ax.annotate("", xy=(12.0, 0), xytext=(12.0, depth),
                arrowprops=dict(arrowstyle="<->", lw=0.8, color=HOT, shrinkA=0, shrinkB=0,
                                mutation_scale=6))
    label = "λ/2π" if SLIDE else f"λ/2π = {depth:.2f} mm"     # slide: the number sits beside the panel
    ax.text(11.75, depth + 0.1, label, ha="right", va="bottom",
            fontsize=FS_DIM + 1, color=INK, fontweight="bold")


def draw_board(ax, x0, x1, conductors, both_faces=False):
    ax.add_patch(Rectangle((x0, -T_BOARD), x1 - x0, T_BOARD, fc=BOARD, ec="none", zorder=1))
    for a, b, role in conductors:
        c = HOT if role == "hot" else GND
        ax.add_patch(Rectangle((a, 0), b - a, T_CU, fc=c, ec="none", zorder=3))
        if both_faces:
            ax.add_patch(Rectangle((a, -T_BOARD - T_CU), b - a, T_CU, fc=c, ec="none", zorder=3))


def setup(ax, name, desc):
    ax.set_xlim(*XLIM)
    ax.set_ylim(*YLIM)
    ax.set_aspect("equal")
    ax.set_facecolor(SOIL)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_xticks([])
    ax.set_yticks([])
    if SLIDE:
        desc = SLIDE_DESC[name]
    ax.set_title(f"$\\bf{{{name.replace(' ', chr(92) + ' ')}}}$  —  {desc}", loc="left",
                 fontsize=12 if SLIDE else 10.5, color=INK, pad=5)
    if SLIDE:      # top-left would sit on MSV4's depth line at this panel height
        ax.text(XLIM[0] + 0.3, 1.2, "soil", ha="left", va="center", fontsize=8, color=MUTED)
    else:
        ax.text(XLIM[0] + 0.3, YLIM[1] - 0.25, "soil", ha="left", va="top", fontsize=8, color=MUTED)


def panel_v12(ax):
    setup(ax, *ROWS[0][:2])
    draw_board(ax, -10.25, 10.25, V12)
    ax.text(10.0, -T_BOARD / 2, "board", ha="right", va="center", fontsize=8, color=MUTED)
    for (a, b), lab in zip([(-10.25, -6.75), (-6.75, -4.75), (-4.75, 4.75), (4.75, 6.75),
                            (6.75, 10.25)], ["3.5", "2.0", "9.5", "2.0", "3.5"]):
        hdim(ax, a, b, 0.75, lab, ext_from=T_CU)
    hdim(ax, 0, 8.5, -2.3, "s = 8.5  (centre to centre)", ext_from=-T_BOARD, above=False)
    if not SLIDE:  # slide version states the width beside the panel
        hdim(ax, -10.25, 10.25, -2.95 + 0.0, "", ext_from=None, above=False)
        ax.text(0, -3.07, "20.5 total", ha="center", va="top", fontsize=FS_DIM, color=INK)
    depth_marker(ax, 17.0 / (2 * math.pi))


def panel_msv3(ax):
    setup(ax, *ROWS[1][:2])
    draw_board(ax, XLIM[0], XLIM[1], MSV3)
    ax.text(12.2, -T_BOARD / 2, "board", ha="right", va="center", fontsize=8, color=MUTED)
    for x in (-11.3, 11.3):
        ax.text(x, 0.07, "···", ha="center", va="center", fontsize=11, color=MUTED)
    if SLIDE:      # at slide font size the two centred labels overlap: put them either side
        hdim(ax, -0.25, 0.25, 1.25, "", ext_from=T_CU)
        hdim(ax, 0.25, 1.25, 1.25, "", ext_from=T_CU)
        ax.text(-0.45, 1.25, "0.5", ha="right", va="center", fontsize=FS_DIM, color=INK)
        ax.text(1.45, 1.25, "1.0", ha="left", va="center", fontsize=FS_DIM, color=INK)
    else:
        hdim(ax, -0.25, 0.25, 1.25, "0.5", ext_from=T_CU)
        hdim(ax, 0.25, 1.25, 1.25, "1.0", ext_from=T_CU)
    hdim(ax, 0, 1.5, -2.3, "s = 1.5", ext_from=-T_BOARD, above=False)
    if not SLIDE:
        ax.text(0, -3.0, "50 fingers in all; 13 shown", ha="center", va="top", fontsize=FS_DIM,
                color=MUTED)
    depth_marker(ax, 3.0 / (2 * math.pi))


def panel_msv4(ax):
    setup(ax, *ROWS[2][:2])
    draw_board(ax, -9.0, 9.0, MSV4, both_faces=True)
    ax.text(-0.0, -T_BOARD / 2, "board 1.6", ha="center", va="center", fontsize=8, color=MUTED)
    main = [((-8.123, -7.087), "1.04"), ((-7.087, -2.3625), "4.725"), ((-2.3625, 2.3625), "4.725"),
            ((2.3625, 7.087), "4.725"), ((7.087, 8.123), "1.04")]
    for (a, b), lab in main:
        hdim(ax, a, b, 0.75, lab, ext_from=T_CU)
    for a, b in ((-8.745, -8.123), (8.123, 8.745)):
        hdim(ax, a, b, 1.6, "0.62", ext_from=T_CU)
    gy = 2.3 if SLIDE else 2.1
    ax.text(-8.434, gy, "guard", ha="center", va="bottom", fontsize=FS_DIM, color=MUTED)
    ax.text(8.434, gy, "guard", ha="center", va="bottom", fontsize=FS_DIM, color=MUTED)
    hdim(ax, -4.725, 4.725, -2.35, "s = 9.45  (centre to centre)", ext_from=-T_BOARD - T_CU,
         above=False)
    if not SLIDE:
        hdim(ax, -9.0, 9.0, -3.0, "", above=False)
        ax.text(0, -3.12, "18.0 board", ha="center", va="top", fontsize=FS_DIM, color=INK)
    depth_marker(ax, 18.9 / (2 * math.pi))


def main():
    fig = plt.figure(figsize=(8.5, 11), facecolor="white")
    left, width = 0.06, 0.88
    h = (YLIM[1] - YLIM[0]) / (XLIM[1] - XLIM[0]) * width * 8.5 / 11
    fig.text(left, 0.965, "Electrode cross-sections: v1.2, MSV3, MSV4", fontsize=15,
             fontweight="bold", color=INK)
    fig.text(left, 0.947, "Section across the electrodes, all three at the same scale. "
             "Dimensions in mm. Dashed line: 1/e field depth λ/2π.", fontsize=9, color=MUTED)
    fig.legend(handles=[Patch(fc=HOT, label="driven"), Patch(fc=GND, label="ground / guard"),
                        Patch(fc=BOARD, label="board"), Patch(fc=SOIL, label="soil")],
               loc="upper left", bbox_to_anchor=(left - 0.008, 0.94), ncol=4, frameon=False,
               fontsize=8.5, handlelength=1.4, columnspacing=1.6)
    top = 0.885
    for i, fn in enumerate((panel_v12, panel_msv3, panel_msv4)):
        ax = fig.add_axes([left, top - h - i * (h + 0.04), width, h])
        fn(ax)
    table_top = top - 3 * h - 2 * 0.04 - 0.03

    tab = fig.add_axes([left, table_top - 0.11, width, 0.11])
    tab.axis("off")
    cells = []
    for name, _, s, arith, pattern in ROWS:
        lam = 2 * s
        cells.append([name, pattern, f"{arith} = {s:g}", f"{lam:.1f}", f"{lam / (2 * math.pi):.2f}"])
    t = tab.table(cellText=cells, bbox=[0, 0, 1, 1], cellLoc="center",
                  colLabels=["electrode", "pattern (mm)", "s, centre to centre (mm)", "λ = 2s (mm)",
                             "λ/2π (mm)"],
                  colWidths=[0.14, 0.28, 0.32, 0.12, 0.14])
    t.auto_set_font_size(False)
    t.set_fontsize(9)
    for (r, c), cell in t.get_celld().items():
        cell.set_edgecolor(RULE)
        cell.set_facecolor("white")
        txt = cell.get_text()
        txt.set_color(INK if r else MUTED)
        if r and c == 4:
            txt.set_fontweight("bold")
        if r and c == 0:
            txt.set_ha("left")
            cell.PAD = 0.05

    notes = [
        "λ is the spatial period of the pattern: twice the driven-to-ground centre spacing s. "
        "λ/2π is the 1/e depth of the field for an ideal periodic",
        "array. The 2D field solve puts the median signal depth at 1.39 mm (v1.2), 0.23 mm (MSV3) "
        "and 1.01 mm (MSV4); MSV4's guard ring makes it",
        "shallower than λ/2π suggests. Board thickness is drawn at 1.6 mm for all three (only MSV4's is "
        "recorded); copper thickness is exaggerated.",
    ]
    for k, line in enumerate(notes):
        fig.text(left, table_top - 0.135 - k * 0.014, line, fontsize=7.5, color=MUTED)

    for ext in ("png", "pdf"):
        fig.savefig(f"{OUT}.{ext}", dpi=200, facecolor="white")
        print(f"wrote {OUT}.{ext}")


def main_slide():
    """Projection version: panels stacked on the left, each electrode's s, lambda and lambda/2pi
    in large type beside its own panel. 9.0 x 6.05 in, pasted 1:1 under a slide title."""
    global SLIDE, FS_DIM, YLIM
    SLIDE, FS_DIM, YLIM = True, 9.0, (-2.9, 3.5)
    W, H = 9.0, 5.9
    extra = {"v1.2": "20.5 mm wide", "MSV3": "50 fingers (13 shown)", "MSV4 as drawn": "18.0 mm wide"}
    fig = plt.figure(figsize=(W, H), facecolor="white")
    pw = 5.6 / W
    ph = pw * W * (YLIM[1] - YLIM[0]) / (XLIM[1] - XLIM[0]) / H
    title_h, gap = 0.3 / H, 0.06 / H
    fig.legend(handles=[Patch(fc=HOT, label="driven"), Patch(fc=GND, label="ground / guard"),
                        Patch(fc=BOARD, label="board"), Patch(fc=SOIL, label="soil")],
               loc="upper left", bbox_to_anchor=(-0.008, 1.0), ncol=4, frameon=False,
               fontsize=10.5, handlelength=1.4, columnspacing=1.4)
    top = 1 - 0.32 / H
    for i, (fn, (name, _, s, _, _)) in enumerate(zip((panel_v12, panel_msv3, panel_msv4), ROWS)):
        y0 = top - title_h - ph - i * (title_h + ph + gap)
        fn(fig.add_axes([0.0, y0, pw, ph]))
        lam = 2 * s
        x = pw + 0.035
        mid = y0 + ph / 2
        fig.text(x, mid + 0.105, extra[name], fontsize=11, color=MUTED, va="center")
        fig.text(x, mid + 0.055, f"s = {s:g} mm", fontsize=12, color=MUTED, va="center")
        fig.text(x, mid - 0.005, f"λ = 2s = {lam:.1f} mm", fontsize=14, color=INK, va="center")
        fig.text(x, mid - 0.08, f"λ/2π = {lam / (2 * math.pi):.2f} mm", fontsize=19, color=HOT,
                 fontweight="bold", va="center")
    fig.text(0.0, 0.012, "To scale in x (mm). Board drawn 1.6 mm for all three; copper thickness "
             "exaggerated. λ/2π: 1/e field depth of an ideal periodic array.", fontsize=9.5, color=MUTED)
    out = f"{OUT}_SLIDE.png"
    fig.savefig(out, dpi=200, facecolor="white")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
    main_slide()
