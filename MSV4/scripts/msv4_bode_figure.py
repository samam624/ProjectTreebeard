#!/usr/bin/env python3
"""Bode diagram of an MSV4 probe's front end, drawn from values the probe itself fitted.

The front end is a divider: the series resistor Rs on top, the electrode underneath, and
H = SENSE / DRV = 1 / (1 + Rs * Y) with
    Y = jw(Cp + Cd) + 1 / (Rsoil + 1/(jw Cm)).
Each curve is that formula with the medians of Rsoil, Cm and Cd the probe reported over a stretch
of its stored history, and that boot's Cp. The probe only measures at its eight sweep frequencies
(the dots, inside the shaded band); everywhere else the curve is the fitted circuit carried on,
not a measurement.

Reads the CSV that cloud/scripts/fetch_supabase_history.py --raw writes.

  python MSV4/scripts/msv4_bode_figure.py data/sensor_logs/msv4_s10.csv --out MSV4/docs/MSV4_BODE_BOARD1_2026-10-11.png \
      --regime "Dry pot" "2026-10-10 18:00" "2026-10-10 19:14" \
      --regime "Moist, after two cups" "2026-10-10 21:14" "2026-10-11 01:20" \
      --moment "First reading with water" "2026-10-10 19:14:59"
"""

from __future__ import annotations

import argparse
import cmath
import math
import statistics
from datetime import datetime, timedelta
from pathlib import Path

import msv4_experimental_moisture as reading

RS_OHM = reading.SWEEP_RS_OHM
SWEEP_HZ = [reading.SWEEP_CLOCK_HZ / n for n in reading.SWEEP_DIVISORS]


def transfer(f: float, rsoil: float | None, cm_pf: float, cd_pf: float, cp_pf: float) -> complex:
    w = 2.0 * math.pi * f
    y = 1j * w * (cd_pf + cp_pf) * 1e-12
    if rsoil is not None:
        y += 1.0 / (rsoil + 1.0 / (1j * w * cm_pf * 1e-12))
    return 1.0 / (1.0 + RS_OHM * y)


def poles_and_zero(rsoil: float | None, cm_pf: float, cd_pf: float, cp_pf: float) -> tuple[list[float], float | None]:
    """The frequencies, in Hz, of H's poles and of its zero.

    With C1 = Cp + Cd and s = jw, clearing the fraction in H = 1 / (1 + Rs*Y) gives
        H(s) = (1 + s*Rsoil*Cm) / (1 + s*b + s^2*a)
        a = Rsoil*Cm * Rs*C1          b = Rsoil*Cm + Rs*C1 + Rs*Cm
    so the zero is at 1/(2 pi Rsoil Cm) and the poles are the roots of the quadratic, both real.
    With nothing conducting it is the plain filter: one pole at 1/(2 pi Rs C1) and no zero.
    """
    c1 = (cp_pf + cd_pf) * 1e-12
    if rsoil is None:
        return [1.0 / (2.0 * math.pi * RS_OHM * c1)], None
    tau = rsoil * cm_pf * 1e-12
    a = tau * RS_OHM * c1
    b = tau + RS_OHM * c1 + RS_OHM * cm_pf * 1e-12
    root = math.sqrt(b * b - 4.0 * a)
    return [(b - root) / (2.0 * a) / (2.0 * math.pi), (b + root) / (2.0 * a) / (2.0 * math.pi)], 1.0 / (2.0 * math.pi * tau)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--poles", action="store_true", help="mark each curve's poles (x) and zero (ring)")
    parser.add_argument("--soil-path", type=Path, metavar="PNG",
                        help="also draw the soil path on its own: the mask's opposition against the soil's, "
                             "and how far the path has opened, against frequency")
    parser.add_argument("--regime", nargs=3, action="append", default=[], metavar=("NAME", "START", "END"),
                        help="a stretch of local time whose median fit is one curve; up to three")
    parser.add_argument("--moment", nargs=2, metavar=("NAME", "TIME"), help="one reading, drawn dashed")
    parser.add_argument("--no-air", action="store_true", help="leave out the curve for the blade in air")
    args = parser.parse_args()

    rows = [row for row in reading.load(args.csv, None) if row["coupled"] is not None]
    if not rows:
        raise SystemExit("no MSV4 measurements with an air value in that file")
    cp = next(cp for sensor, _, _, cp, _ in reading.AIR_BASELINES if sensor == rows[0]["sensor"])
    local = lambda text: datetime.fromisoformat(text).astimezone()

    curves = []   # (name, rsoil or None, cm, cd, detail)
    if not args.no_air:
        air = rows[-1]["cd_air"]
        curves.append(("Air", None, 0.0, air, f"nothing conducts, Cd {air:.2f} pF"))
    for name, start, end in args.regime[:3 - len(curves)]:
        chunk = [row for row in rows if local(start) <= row["time"] < local(end) and not row["open"]]
        if len(chunk) < 5:
            raise SystemExit(f"fewer than five conducting readings for {name!r}")
        r, cm, cd = (statistics.median(row[key] for row in chunk) for key in ("rsoil", "cm", "cd"))
        curves.append((name, r, cm, cd, f"Rsoil {r:,.0f} ohm, Cm {cm:.1f} pF, Cd {cd:.2f} pF ({len(chunk)} readings)"))
    moment = None
    if args.moment:
        at = local(args.moment[1])
        row = min((row for row in rows if not row["open"]), key=lambda row: abs((row["time"] - at).total_seconds()))
        if abs((row["time"] - at).total_seconds()) > 90:
            raise SystemExit(f"no reading within 90 s of {args.moment[1]}")
        moment = (args.moment[0], row["rsoil"], row["cm"], row["cd"],
                  f"Rsoil {row['rsoil']:,.0f} ohm, Cm {row['cm']:.1f} pF, Cd {row['cd']:.2f} pF (one reading, {row['time']:%H:%M:%S})")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    surface, ink, muted, grid, shade = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#efeeea"
    palette = ("#2a78d6", "#eb6834", "#1baf7a")   # fixed order; validated all-pairs on this surface
    freqs = [10 ** (4.3 + 3.4 * i / 400) for i in range(401)]   # 20 kHz .. 50 MHz
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(11, 8.4), sharex=True, facecolor=surface)

    def draw(axis, value, name, colour, params, dashed=False):
        r, cm, cd = params
        ys = [value(transfer(f, r, cm, cd, cp)) for f in freqs]
        style = dict(color=colour, linestyle=(0, (5, 3)) if dashed else "-")
        axis.plot(freqs, ys, linewidth=1.3, alpha=0.45, **style)
        inside = [(f, y) for f, y in zip(freqs, ys) if SWEEP_HZ[0] <= f <= SWEEP_HZ[-1]]
        axis.plot([f for f, _ in inside], [y for _, y in inside], linewidth=2.2, label=name, **style)
        if not dashed and not args.poles:
            axis.plot(SWEEP_HZ, [value(transfer(f, r, cm, cd, cp)) for f in SWEEP_HZ], "o", color=colour,
                      markersize=6, markeredgecolor=surface, markeredgewidth=1.5)
        if args.poles:
            poles, zero = poles_and_zero(r, cm, cd, cp)
            at = lambda f: value(transfer(f, r, cm, cd, cp))
            axis.plot(poles, [at(f) for f in poles], "x", color=colour, markersize=10, markeredgewidth=2.4, zorder=5)
            if zero is not None:
                axis.plot([zero], [at(zero)], "o", color=colour, markersize=10, markerfacecolor=surface,
                          markeredgewidth=2.2, zorder=5)
        return ys[-1]

    for axis, value, title in (
            (top, abs, "How much of the drive reaches SENSE (SENSE ÷ DRV, log scale)"),
            (bottom, lambda h: math.degrees(cmath.phase(h)), "How late SENSE is, in degrees of a cycle (negative = later)")):
        axis.axvspan(SWEEP_HZ[0], SWEEP_HZ[-1], color=shade, linewidth=0, zorder=0)
        ends = []
        for (name, r, cm, cd, _), colour in zip(curves, palette):
            ends.append((draw(axis, value, name, colour, (r, cm, cd)), name))
        if moment:
            ends.append((draw(axis, value, moment[0], muted, moment[1:4], dashed=True), moment[0]))
        # Direct labels at the right-hand end, nudged apart so none overlap.
        log_axis = axis is top
        place = sorted(ends, key=lambda e: e[0])
        gap = 0.085 if log_axis else 4.5
        position = []
        for y, name in place:
            target = math.log10(y) if log_axis else y
            if position and target - position[-1][0] < gap:
                target = position[-1][0] + gap
            position.append((target, name))
        for target, name in position:
            axis.annotate(name, (freqs[-1], 10 ** target if log_axis else target), xytext=(8, 0), textcoords="offset points",
                          va="center", color=ink, fontsize=9)
        axis.set_title(title, loc="left", color=ink, fontsize=11)
        axis.set_xscale("log")
        axis.set_facecolor(surface)
        axis.grid(True, which="major", color=grid, linewidth=0.8)
        axis.tick_params(colors=muted, labelsize=9, length=0)
        axis.margins(x=0)
        for side in axis.spines.values():
            side.set_visible(False)

    top.set_yscale("log")
    top.set_ylim(0.02, 1.25)
    top.set_yticks([1, 0.71, 0.5, 0.3, 0.2, 0.1, 0.05, 0.02])
    top.set_yticklabels(["1", "0.71 (−3 dB)", "0.5", "0.3", "0.2", "0.1", "0.05", "0.02"])
    top.minorticks_off()
    top.annotate("the probe's sweep\n250 kHz – 2.67 MHz", (math.sqrt(SWEEP_HZ[0] * SWEEP_HZ[-1]), 0.026), ha="center",
                 va="bottom", color=muted, fontsize=9)
    if args.poles:
        top.plot([], [], "x", color=muted, markersize=9, markeredgewidth=2.2, label="pole")
        top.plot([], [], "o", color=muted, markersize=9, markerfacecolor=surface, markeredgewidth=2.0, label="zero")
    top.legend(loc="lower left", frameon=False, fontsize=9, labelcolor=ink)
    bottom.set_ylim(-95, 3)
    bottom.set_yticks([0, -15, -30, -45, -60, -75, -90])
    bottom.set_yticklabels(["0°", "−15°", "−30°", "−45°", "−60°", "−75°", "−90°"])
    bottom.set_xticks([1e5, 1e6, 1e7])
    bottom.set_xticklabels(["100 kHz", "1 MHz", "10 MHz"])
    bottom.set_xlim(freqs[0], freqs[-1])
    fig.subplots_adjust(left=0.1, right=0.82, top=0.95, bottom=0.07, hspace=0.2)
    fig.savefig(args.out, dpi=140, facecolor=surface)

    if args.soil_path:
        # The soil path alone: Cm in series with Rsoil. The mask opposes with 1/(w*Cm), which falls
        # as frequency rises; the soil opposes with Rsoil at every frequency. Where the two are
        # equal is the turn-over, and the path's current as a share of what the soil alone would
        # pass is Rsoil / sqrt(Rsoil^2 + (1/(w*Cm))^2).
        span = [10 ** (4.3 + 3.0 * i / 300) for i in range(301)]   # 20 kHz .. 20 MHz
        fig2, (upper, lower) = plt.subplots(2, 1, figsize=(11, 8.4), sharex=True, facecolor=surface)
        mask_ohm = lambda f, cm: 1.0 / (2.0 * math.pi * f * cm * 1e-12)
        open_share = lambda f, r, cm: 100.0 * r / math.hypot(r, mask_ohm(f, cm))
        soils = [(name, r, cm, colour) for (name, r, cm, _, _), colour in zip(curves, palette) if r is not None]
        for i, (name, r, cm, colour) in enumerate(soils):
            turn = 1.0 / (2.0 * math.pi * r * cm * 1e-12)
            upper.plot(span, [mask_ohm(f, cm) for f in span], color=colour, linewidth=2.2, label=name)
            upper.plot(span, [r] * len(span), color=colour, linewidth=2.2, linestyle=(0, (5, 3)))
            upper.plot([turn], [r], "o", color=colour, markersize=10, markerfacecolor=surface, markeredgewidth=2.2, zorder=5)
            upper.annotate(f"equal at {turn / 1e3:.0f} kHz", (turn, r), xytext=(10, 12), textcoords="offset points",
                           color=ink, fontsize=9)
            upper.annotate(f"soil, {r:,.0f} Ω", (span[-1], r), xytext=(8, 0), textcoords="offset points", va="center",
                           color=ink, fontsize=9)
            upper.annotate(f"mask, {cm:.0f} pF", (span[-1], mask_ohm(span[-1], cm)), xytext=(8, 0),
                           textcoords="offset points", va="center", color=ink, fontsize=9)
            lower.plot(span, [open_share(f, r, cm) for f in span], color=colour, linewidth=1.3, alpha=0.45)
            inside = [f for f in span if SWEEP_HZ[0] <= f <= SWEEP_HZ[-1]]
            lower.plot(inside, [open_share(f, r, cm) for f in inside], color=colour, linewidth=2.2, label=name)
            lower.plot(SWEEP_HZ, [open_share(f, r, cm) for f in SWEEP_HZ], "o", color=colour, markersize=6,
                       markeredgecolor=surface, markeredgewidth=1.5)
            lower.annotate(f"{name}: {open_share(SWEEP_HZ[0], r, cm):.0f} % at 250 kHz", (SWEEP_HZ[0], open_share(SWEEP_HZ[0], r, cm)),
                           xytext=(-10, 16 - 34 * i), textcoords="offset points", ha="right", va="center", color=ink, fontsize=9)
        upper.plot([], [], color=muted, linewidth=2.2, label="the mask: opposes less as frequency rises")
        upper.plot([], [], color=muted, linewidth=2.2, linestyle=(0, (5, 3)), label="the soil: the same at every frequency")
        upper.set_yscale("log")
        upper.set_ylim(40, 600000)
        upper.set_yticks([100, 1000, 10000, 100000])
        upper.set_yticklabels(["100 Ω", "1 kΩ", "10 kΩ", "100 kΩ"])
        upper.minorticks_off()
        upper.set_title("What stands in the current's way along the soil path (ohms, log scale)", loc="left", color=ink, fontsize=11)
        upper.legend(loc="lower left", frameon=False, fontsize=9, labelcolor=ink)
        lower.axhline(100.0 / math.sqrt(2.0), color=muted, linewidth=1, linestyle=(0, (2, 3)))
        lower.annotate("71 %: mask and soil oppose equally", (span[-1], 100.0 / math.sqrt(2.0)), xytext=(-6, -12),
                       textcoords="offset points", ha="right", color=muted, fontsize=9)
        lower.set_ylim(0, 105)
        lower.set_yticks([0, 25, 50, 75, 100])
        lower.set_yticklabels(["0 %", "25 %", "50 %", "75 %", "100 %"])
        lower.set_title("How far the soil path has opened: its current as a share of what the soil alone would pass",
                        loc="left", color=ink, fontsize=11)
        lower.legend(loc="lower right", frameon=False, fontsize=9, labelcolor=ink)
        for axis in (upper, lower):
            axis.axvspan(SWEEP_HZ[0], SWEEP_HZ[-1], color=shade, linewidth=0, zorder=0)
            axis.set_xscale("log")
            axis.set_facecolor(surface)
            axis.grid(True, which="major", color=grid, linewidth=0.8)
            axis.tick_params(colors=muted, labelsize=9, length=0)
            axis.margins(x=0)
            for side in axis.spines.values():
                side.set_visible(False)
        upper.annotate("the probe's sweep", (math.sqrt(SWEEP_HZ[0] * SWEEP_HZ[-1]), 300000), ha="center", va="center",
                       color=muted, fontsize=9)
        lower.set_xticks([1e5, 1e6, 1e7])
        lower.set_xticklabels(["100 kHz", "1 MHz", "10 MHz"])
        lower.set_xlim(span[0], span[-1])
        fig2.subplots_adjust(left=0.08, right=0.86, top=0.95, bottom=0.07, hspace=0.2)
        fig2.savefig(args.soil_path, dpi=140, facecolor=surface)
        print(f"Soil path figure: {args.soil_path}")
        print(f"{'  path open, % of soil alone':28}" + "".join(f"{f / 1e3:>9.0f}k" for f in SWEEP_HZ))
        for name, r, cm, _ in soils:
            print(f"{'  ' + name:28}" + "".join(f"{open_share(f, r, cm):>10.0f}" for f in SWEEP_HZ)
                  + f"   turn-over {1e-3 / (2.0 * math.pi * r * cm * 1e-12):.0f} kHz")

    print(f"Figure: {args.out}   (Rs {RS_OHM:.0f} ohm, Cp {cp:.2f} pF)")
    print(f"{'':28}" + "".join(f"{f / 1e3:>9.0f}k" for f in SWEEP_HZ))
    for name, r, cm, cd, detail in curves + ([moment] if moment else []):
        hs = [transfer(f, r, cm, cd, cp) for f in SWEEP_HZ]
        print(f"{name}: {detail}")
        print(f"{'  SENSE / DRV':28}" + "".join(f"{abs(h):>10.3f}" for h in hs))
        print(f"{'  delay, degrees':28}" + "".join(f"{-math.degrees(cmath.phase(h)):>10.1f}" for h in hs))
        poles, zero = poles_and_zero(r, cm, cd, cp)
        print("  poles " + ", ".join(f"{f / 1e3:,.0f} kHz" for f in poles) + (f"; zero {zero / 1e3:,.0f} kHz" if zero else "; no zero"))


if __name__ == "__main__":
    main()
