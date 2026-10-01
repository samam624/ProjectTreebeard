#!/usr/bin/env python3
"""MSV4 small-signal model of the moisture front end, and what it says about I/Q demodulation.

Draws MSV4/docs/MSV4_SMALL_SIGNAL_MODEL_IQ_2026-09-29.png and prints the numbers quoted in
MSV4/docs/MSV4_SMALL_SIGNAL_MODEL_IQ_2026-09-29.md.

The model is the one the firmware fits (`specNodeH` in MSV4_Probe_Bringup_BLE.ino), imported from
its Python replica in test_msv4_spectrum_fit.py so the two cannot drift apart:

    H(jw) = V_SENSE / V_DRV = 1 / (1 + Rs*(Ye + jw*Cp))
    Ye    = jw*Cd + 1 / ( 1/(jw*Cm) + 1/(Gs + jw*Cs) )

Element values are the repo's modelled ones (spectrum-fit example row A, 2026-09-23): Cm 671 pF,
Rsoil 355 ohm, Cs 14.9 pF, Cd 5 pF, Cp 12 pF, Rs = R15 = 1 kohm, at the 1.000 MHz routine
frequency. Temperature acts on the soil as Gs +2 %/C (ion mobility, the standard EC correction the
firmware applies) and Cs -0.4 %/C (water permittivity). NOTHING HERE IS MEASURED: no MSV4 board has
taken a reading.

    python MSV4/scripts/msv4_small_signal_iq_figure.py [--no-figure]
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_msv4_spectrum_fit import CP_DEFAULT, SPEC_RS, node_h  # noqa: E402

FIGURE = Path(__file__).resolve().parents[1] / "docs" / "MSV4_SMALL_SIGNAL_MODEL_IQ_2026-09-29.png"
W = 2 * np.pi * 1.0e6
BASE = {"Cm": 671e-12, "R": 355.0, "Cs": 14.9e-12, "Cd": 5e-12}
TC_G, TC_EPS = 0.02, -0.004          # per C: soil conductivity, water permittivity


def h(p, w=W):
    return node_h(w, [p["Cm"], 1.0 / p["R"], p["Cs"], p["Cd"]], CP_DEFAULT)


def ye(p, w=W):
    """Electrode admittance alone, backed out of H so it is the same model."""
    return (1.0 / h(p, w) - 1.0) / SPEC_RS - 1j * w * CP_DEFAULT


def warmer(p, dt=1.0):
    return dict(p, R=p["R"] / (1 + TC_G * dt), Cs=p["Cs"] * (1 + TC_EPS * dt))


def tempcos(p):
    """Apparent %/C of each single-number reading, for the same +2 %/C in the soil."""
    h0, h1, y0, y1 = h(p), h(warmer(p)), ye(p), ye(warmer(p))
    return {
        "|H|, magnitude only (AM detector)": 100 * np.log(abs(h1) / abs(h0)),
        "C_eff = Im(Ye)/w, capacitance only": 100 * np.log(y1.imag / y0.imag),
        "G_eff = Re(Ye), conductance only": 100 * np.log(y1.real / y0.real),
        "Gs from the I/Q sweep fit": 100 * np.log((1 / warmer(p)["R"]) / (1 / p["R"])),
    }


def directions(p):
    """Direction each physical change moves H in the I/Q plane, degrees."""
    h0 = h(p)
    out = {}
    for name, q in (("+10 C", warmer(p, 10)), ("drier: Rsoil +25 %", dict(p, R=p["R"] * 1.25)),
                    ("contact: Cm -20 %", dict(p, Cm=p["Cm"] * 0.8)),
                    ("permittivity: Cs +20 %", dict(p, Cs=p["Cs"] * 1.2))):
        d = h(q) - h0
        out[name] = (d, np.degrees(np.angle(d)), abs(h(q)) - abs(h0))
    return out


def report():
    h0, y0 = h(BASE), ye(BASE)
    x = W * BASE["R"] * BASE["Cm"]
    print(f"Operating point, 1.000 MHz, moist: w*Rsoil*Cm = {x:.3f}; H = {h0.real:.4f} {h0.imag:+.4f}j, "
          f"|H| {abs(h0):.4f}, phase {np.degrees(np.angle(h0)):.2f} deg; Ye: G {y0.real * 1e3:.3f} mS, "
          f"C_eff {y0.imag / W * 1e12:.1f} pF")
    print("\nApparent temperature coefficient, %/C, for Gs +2 %/C and Cs -0.4 %/C in the soil:")
    print(f"{'Rsoil':>7s} {'wRCm':>6s} " + " ".join(f"{k.split(',')[0]:>10s}" for k in tempcos(BASE)))
    for r in (30000, 10000, 3000, 1000, 355, 150, 60):
        p = dict(BASE, R=r)
        print(f"{r:7.0f} {W * r * BASE['Cm']:6.2f} " + " ".join(f"{v:+10.3f}" for v in tempcos(p).values()))
    for r in (1000, 355, 150):
        d = directions(dict(BASE, R=r))
        sep = (d["contact: Cm -20 %"][1] - d["drier: Rsoil +25 %"][1] + 180) % 360 - 180
        print(f"\nDirections in the I/Q plane at Rsoil {r} ohm (contact vs drying {abs(sep):.1f} deg apart):")
        for name, (dv, ang, dmag) in d.items():
            print(f"  {name:24s} |dH| {abs(dv) * 1e3:6.2f}e-3 at {ang:+7.1f} deg;  d|H| {dmag * 1e3:+6.2f}e-3")


# ---- drawing -------------------------------------------------------------------------------
SURFACE, INK, INK2, MUTED, GRID, BASELINE = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
BLUE, ORANGE, AQUA, YELLOW = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"


def schematic(ax):
    ax.set_xlim(0, 26)
    ax.set_ylim(0.2, 10.2)
    ax.set_aspect("equal")
    ax.axis("off")
    lw = 1.3

    def wire(*pts):
        xs, ys = zip(*pts)
        ax.plot(xs, ys, color=INK, lw=lw, solid_capstyle="round")

    def dot(x, y):
        ax.plot(x, y, "o", color=INK, ms=4.5)

    def resistor(x0, y0, x1, y1, n=6, amp=0.2):
        v = np.array([x1 - x0, y1 - y0])
        length = np.hypot(*v)
        u, perp = v / length, np.array([-v[1], v[0]]) / length
        pts = [np.array([x0, y0])]
        for i in range(1, 2 * n):
            pts.append(np.array([x0, y0]) + u * length * i / (2 * n) + perp * amp * (1 if i % 2 else -1))
        pts.append(np.array([x1, y1]))
        wire(*[tuple(p) for p in pts])

    def cap(x, y, vertical=True, gap=0.16, plate=0.62):
        if vertical:
            for dy in (-gap / 2, gap / 2):
                ax.plot([x - plate / 2, x + plate / 2], [y + dy] * 2, color=INK, lw=2.0)
        else:
            for dx in (-gap / 2, gap / 2):
                ax.plot([x + dx] * 2, [y - plate / 2, y + plate / 2], color=INK, lw=2.0)

    def vcap(x, ytop, ybot, yc):
        wire((x, ytop), (x, yc + 0.08))
        wire((x, yc - 0.08), (x, ybot))
        cap(x, yc)

    def ground(x, y):
        for i, w in enumerate((0.5, 0.32, 0.14)):
            ax.plot([x - w / 2, x + w / 2], [y - 0.13 * i] * 2, color=INK, lw=lw)

    def label(x, y, s, **kw):
        kw.setdefault("fontsize", 8.6)
        kw.setdefault("color", INK2)
        ax.text(x, y, s, **kw)

    rail = 6.0
    # Drive source.
    ax.add_patch(__import__("matplotlib.patches", fromlist=["Circle"]).Circle((1.5, 3.6), 0.48, fill=False, lw=lw, ec=INK))
    ax.plot([1.22, 1.22, 1.5, 1.5, 1.78, 1.78], [3.45, 3.78, 3.78, 3.45, 3.45, 3.78], color=INK, lw=1.0)
    wire((1.5, 4.08), (1.5, rail), (3.0, rail))
    wire((1.5, 3.12), (1.5, 1.2))
    ground(1.5, 1.2)
    label(2.15, 3.6, "P0.07 drive,\nsquare wave\n0.25–2.67 MHz sweep,\n1.000 MHz routine", va="center")
    dot(3.0, rail)
    label(2.55, rail - 0.55, "DRV", fontsize=8.6, color=INK, weight="semibold")
    wire((3.0, rail), (3.6, rail))
    resistor(3.6, rail, 5.6, rail)
    label(4.6, rail - 0.75, "$R_s$ = R15\n1 kΩ", ha="center", va="top", color=INK)
    wire((5.6, rail), (11.6, rail))
    dot(6.5, rail)
    label(6.5, rail - 0.55, "SENSE", ha="center", va="top", fontsize=8.6, color=INK, weight="semibold")
    # Parasitic node capacitance.
    dot(7.7, rail)
    vcap(7.7, rail, 1.2, 3.6)
    ground(7.7, 1.2)
    label(8.2, 3.6, "$C_p$ ≈ 12 pF\nswitch C + traces,\nmeasured by\n'spectrum open'", va="center")
    # A/B swap mux.
    ax.add_patch(__import__("matplotlib.patches", fromlist=["Rectangle"]).Rectangle((9.3, rail - 0.45), 1.5, 0.9, fill=True, fc=SURFACE, ec=INK, lw=lw, zorder=3))
    label(10.05, rail, "U3\nA/B swap", ha="center", va="center", fontsize=8, color=INK, zorder=4)

    # Electrode + soil.
    ax.add_patch(__import__("matplotlib.patches", fromlist=["FancyBboxPatch"]).FancyBboxPatch(
        (11.4, 0.55), 14.5, 6.75, boxstyle="round,pad=0.02,rounding_size=0.25", fill=True, fc="#fdf1ec",
        ec=ORANGE, lw=1.0, ls=(0, (4, 3))))
    label(11.7, 6.93, "Electrode in soil: plate A → mask → soil → mask → plate B (B + guard at ground)",
          fontsize=9, color=INK, weight="semibold", va="center")
    wire((11.6, rail), (17.0, rail))
    dot(12.4, rail)
    vcap(12.4, rail, 1.2, 3.6)
    ground(12.4, 1.2)
    label(12.9, 3.6, "$C_d$ 5–20 pF\ndirect field in\nFR4 / mask / air\n(nuisance)", va="center")
    # Series branch: mask A, soil, mask B.
    xs = 17.0
    wire((xs, rail), (xs, 5.55))
    cap(xs, 5.47)
    wire((xs, 5.39), (xs, 4.75), (xs - 0.75, 4.75))
    wire((xs, 4.75), (xs + 0.75, 4.75))
    resistor(xs - 0.75, 4.75, xs - 0.75, 2.65, n=5, amp=0.18)
    wire((xs + 0.75, 4.75), (xs + 0.75, 3.78))
    cap(xs + 0.75, 3.7)
    wire((xs + 0.75, 3.62), (xs + 0.75, 2.65))
    wire((xs - 0.75, 2.65), (xs + 0.75, 2.65))
    wire((xs, 2.65), (xs, 2.03))
    cap(xs, 1.95)
    wire((xs, 1.87), (xs, 1.2))
    ground(xs, 1.2)
    label(xs + 0.45, 5.47, "$C_{m,A}$", va="center", color=INK)
    label(xs + 0.45, 1.95, "$C_{m,B}$", va="center", color=INK)
    label(xs - 1.0, 3.7, "$R_{soil}$", ha="right", va="center", color=INK)
    label(xs + 1.1, 3.7, "$C_s$", va="center", color=INK)
    # What each element carries.
    tx = 19.3
    rows = [
        (6.1, "$G_s = 1/R_{soil}$ ≈ 1/355 Ω  (moist)", "the moisture signal. Temperature moves\nit +2 %/°C (ion mobility)"),
        (4.6, "$C_m$ = $C_{m,A}$, $C_{m,B}$ in series ≈ 671 pF", "contact, air gap, interface.\nA gap collapses it"),
        (3.1, "$C_s$ ≈ 15 pF", "soil permittivity. Below ~30 MHz it moves H\n~1/30 as much as $R_{soil}$: not identifiable"),
        (1.6, "R17 NTC at the blade tip", "soil temperature where the field is:\n$G_s$(25 °C) = $G_s$ / (1 + 0.02 (T − 25))"),
    ]
    for y, head, body in rows:
        label(tx, y, head, color=INK, weight="semibold", va="top", fontsize=8.8)
        label(tx, y - 0.42, body, va="top", fontsize=8.3)

    # Demodulator.
    rect = __import__("matplotlib.patches", fromlist=["Rectangle"]).Rectangle
    for x0, name, src in ((1.2, "DRV_OUT", 3.0), (4.9, "SENSE_OUT", 6.5)):
        wire((src, rail), (src, 7.95))
        ax.add_patch(rect((x0, 7.95), 3.5, 1.35, fill=True, fc="#eef4fc", ec=BLUE, lw=1.0))
        label(x0 + 1.75, 8.62, f"U4 chopper at θ\n100 kΩ / 1 nF → {name}\nADC", ha="center", va="center",
              fontsize=8, color=INK)
    label(1.2, 9.55, "Lock-in: the chopper runs off the SAME timer as the drive (TIMER + PPI + GPIOTE), "
          "so θ is exact. One switch, phase swept in firmware:", va="bottom", fontsize=8.6, color=INK)
    label(9.0, 8.95, "V(θ) = P + A cos(θ − φ)    at 4 phases (routine) or 16 (diagnostic)", va="center",
          fontsize=9, color=INK)
    label(9.0, 8.35, "I = A cos φ,   Q = A sin φ    →    H = X₁(SENSE) / X₁(DRV_OUT) = I + jQ", va="center",
          fontsize=9, color=INK)
    label(9.0, 7.75, "$H = 1\\,/\\,(1 + R_s(Y_e + j\\omega C_p))$,    "
          "$Y_e = j\\omega C_d + [\\,1/(j\\omega C_m) + 1/(G_s + j\\omega C_s)\\,]^{-1}$",
          va="center", fontsize=9.5, color=INK)


def figure():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    plt.rcParams.update({"font.family": ["Segoe UI", "DejaVu Sans"], "font.size": 9.5, "mathtext.fontset": "dejavusans",
                         "axes.edgecolor": BASELINE, "axes.labelcolor": INK2, "xtick.color": MUTED,
                         "ytick.color": MUTED, "text.color": INK})
    fig = plt.figure(figsize=(13, 11.4), facecolor=SURFACE)
    gs = fig.add_gridspec(2, 2, height_ratios=[1.08, 1], left=0.06, right=0.975, top=0.925, bottom=0.06,
                          hspace=0.2, wspace=0.22)
    fig.text(0.06, 0.965, "MSV4 moisture front end: small-signal model, and why it is read with I/Q",
             fontsize=15, weight="bold")
    fig.text(0.06, 0.94, "Modelled, not measured: element values are the repo's field-solve / spectrum-fit ones "
             "(moist potting mix, 1.000 MHz). No MSV4 board has taken a reading yet.", fontsize=9.3, color=INK2, va="top")

    ax = fig.add_subplot(gs[0, :])
    schematic(ax)

    # I/Q plane.
    axq = fig.add_subplot(gs[1, 0])
    axq.set_facecolor(SURFACE)
    axq.grid(color=GRID, lw=0.6)
    axq.spines[["top", "right"]].set_visible(False)
    axq.tick_params(length=0)
    h0 = h(BASE)
    r_sweep = np.geomspace(200, 700, 60)
    loc_r = np.array([h(dict(BASE, R=r)) for r in r_sweep])
    cm_sweep = np.linspace(350e-12, 900e-12, 60)
    loc_c = np.array([h(dict(BASE, Cm=c)) for c in cm_sweep])
    axq.plot(loc_r.real, loc_r.imag, color=MUTED, lw=1.0)
    axq.plot(loc_c.real, loc_c.imag, color=MUTED, lw=1.0, ls=(0, (1, 2)))
    th = np.linspace(np.angle(h0) - 0.2, np.angle(h0) + 0.12, 80)
    axq.plot(abs(h0) * np.cos(th), abs(h0) * np.sin(th), color=MUTED, lw=0.9, ls=(0, (5, 3)))
    d = directions(BASE)
    arrows = [("+10 C", ORANGE, "+10 °C"), ("drier: Rsoil +25 %", BLUE, "drier, $R_{soil}$ +25 %"),
              ("contact: Cm -20 %", AQUA, "contact loss, $C_m$ −20 %"), ("permittivity: Cs +20 %", YELLOW, "$C_s$ +20 %")]
    for key, col, text in arrows:
        dv = d[key][0]
        axq.annotate("", xy=(h0.real + dv.real, h0.imag + dv.imag), xytext=(h0.real, h0.imag),
                     arrowprops=dict(arrowstyle="-|>", color=col, lw=2.0, mutation_scale=13), zorder=4)
    axq.plot(h0.real, h0.imag, "o", color=INK, ms=6, zorder=5)
    offs = {"+10 C": (-0.004, 0.009, "right"), "drier: Rsoil +25 %": (0.004, 0.006, "left"),
            "contact: Cm -20 %": (0.004, -0.006, "left"), "permittivity: Cs +20 %": (-0.004, 0.012, "right")}
    for key, col, text in arrows:
        dv = d[key][0]
        dx, dy, ha = offs[key]
        axq.text(h0.real + dv.real + dx, h0.imag + dv.imag + dy, text, color=INK, fontsize=8.8, ha=ha, va="center")
    axq.text(h0.real - 0.006, h0.imag - 0.02, "moist, 1 MHz", fontsize=8.5, color=INK2, ha="right")
    sep = abs((d["contact: Cm -20 %"][1] - d["drier: Rsoil +25 %"][1] + 180) % 360 - 180)
    axq.text(0.02, 0.98, f"Contact loss moves H {sep:.0f}° away from drying.\n"
             f"Temperature moves it along the drying line.\n"
             f"Dashed arc = constant |H|: an AM detector reads only\ndistance from the origin, so contact loss "
             f"(+{d['contact: Cm -20 %'][2] * 1e3:.0f}e-3)\nand drying (+{d['drier: Rsoil +25 %'][2] * 1e3:.0f}e-3) "
             f"both read as 'drier'.", transform=axq.transAxes, va="top", fontsize=8.6, color=INK2)
    axq.set_xlabel("I = Re H  (in phase with the drive)")
    axq.set_ylabel("Q = Im H  (quadrature)")
    axq.set_aspect("equal", adjustable="datalim")
    axq.set_title("A.  Where each physical change moves H, at 1 MHz", loc="left", fontsize=11, weight="bold")
    axq.legend(handles=[Line2D([], [], color=MUTED, lw=1.0, label="$R_{soil}$ 200–700 Ω (moisture / temperature)"),
                        Line2D([], [], color=MUTED, lw=1.0, ls=(0, (1, 2)), label="$C_m$ 350–900 pF (contact)")],
               loc="lower right", frameon=False, fontsize=8.3)

    # Apparent tempco vs soil state.
    axt = fig.add_subplot(gs[1, 1])
    axt.set_facecolor(SURFACE)
    axt.grid(color=GRID, lw=0.6)
    axt.spines[["top", "right"]].set_visible(False)
    axt.tick_params(length=0)
    rs = np.geomspace(40, 30000, 200)
    curves = {k: [] for k in tempcos(BASE)}
    for r in rs:
        for k, v in tempcos(dict(BASE, R=r)).items():
            curves[k].append(v)
    style = {"Gs from the I/Q sweep fit": (BLUE, 2.4), "C_eff = Im(Ye)/w, capacitance only": (ORANGE, 1.6),
             "G_eff = Re(Ye), conductance only": (AQUA, 1.6), "|H|, magnitude only (AM detector)": (YELLOW, 1.6)}
    for k, (col, w) in style.items():
        axt.plot(rs, curves[k], color=col, lw=w)
    axt.axhline(0, color=BASELINE, lw=0.8)
    axt.axvline(BASE["R"], color=MUTED, lw=0.8, ls=(0, (4, 3)))
    axt.text(BASE["R"] * 1.08, -2.05, "moist operating\npoint, 355 Ω", fontsize=8.3, color=INK2, va="bottom")
    axt.set_xscale("log")
    axt.set_xlim(40, 30000)
    axt.set_ylim(-2.2, 3.9)
    axt.set_xlabel("$R_{soil}$, Ω   (wet ← → dry)")
    axt.set_ylabel("apparent temperature coefficient, %/°C")
    axt.set_title("B.  One physical tempco, four readings of it", loc="left", fontsize=11, weight="bold")
    labels = {"Gs from the I/Q sweep fit": (2600, 2.35, "$G_s$ from the I/Q fit: +2.0 %/°C at\n"
                                                         "every moisture, one NTC coefficient"),
              "C_eff = Im(Ye)/w, capacitance only": (950, 2.95, "capacitance only (what a resonance readout tracks)"),
              "G_eff = Re(Ye), conductance only": (48, -1.55, "conductance only"),
              "|H|, magnitude only (AM detector)": (1500, -1.28, "|H| only (AM detector)")}
    for k, (x, y, text) in labels.items():
        axt.text(x, y, text, fontsize=8.4, color=INK, va="center")
    short = {"Gs from the I/Q sweep fit": "$G_s$ from the I/Q sweep fit", "C_eff = Im(Ye)/w, capacitance only":
             "capacitance only, Im($Y_e$)/ω", "G_eff = Re(Ye), conductance only": "conductance only, Re($Y_e$)",
             "|H|, magnitude only (AM detector)": "|H| only (AM detector)"}
    axt.legend(handles=[Line2D([], [], color=c, lw=w, label=short[k]) for k, (c, w) in style.items()],
               loc="upper left", frameon=False, fontsize=7.8)

    fig.savefig(FIGURE, dpi=150, facecolor=SURFACE)
    print(f"\nwrote {FIGURE}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-figure", action="store_true")
    args = ap.parse_args()
    report()
    if not args.no_figure:
        figure()


if __name__ == "__main__":
    main()
