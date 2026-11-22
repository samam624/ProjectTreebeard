#!/usr/bin/env python3
"""Daily swing, MSV3-era rigs (Sept, Alocasia pot) against MSV4 (Oct-Nov), on ONE scale.

The scale and the estimator are those of MSV3/scripts/analyze_alocasia_electrode_vs_circuit.py,
imported unchanged for the MSV3 side: each day (04:30 -> 04:30 local) minus the straight line
between its two predawn (03:00-06:00) medians; the peak-to-peak of what is left, divided by the
rig's own watering step (predawn before -> predawn after the watering), is that day's swing in % of
a watering. For MSV4 the same estimator runs on 10-minute medians of the stored moisture number,
each day divided by the step of the watering that started its cycle; days that hold a watering are
left out, as on the MSV3 side.

MSV3 input: the six raw CSVs that script reads (fetch_supabase_history.py --raw, 1660 h back on
2026-11-18). MSV4 input: data/sensor_logs/cache/sensor{10,11}_raw.csv via msv4_fit_snr_figure.load().

    python MSV4/scripts/msv4_vs_msv3_daily_swing.py <msv3 csv dir> --out MSV4/docs/figures/MSV4_VS_MSV3_DAILY_SWING_2026-11-18.png
"""
from __future__ import annotations

import argparse
import importlib.util
import statistics as st
import sys
from collections import defaultdict
from datetime import timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import msv4_fit_snr_figure as fs

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "msv3_swing", ROOT / "MSV3" / "scripts" / "analyze_alocasia_electrode_vs_circuit.py")
m3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m3)

INK, MUTED, SURFACE, GRID = fs.INK, fs.MUTED, fs.SURFACE, fs.GRID
MSV3_COLOUR = "#E8692D"
MSV3_BUILT = "#F2A27A"
MSV4_COLOUR = "#2A77D6"
MSV4_ALOC = "#8DB8E6"
MSV3_TRACE_DAYS = range(23, 29)          # 09-23 .. 09-28, the settled days after the 09-22 watering


def msv3(directory: Path) -> dict:
    bins = {s: m3.median_bins(directory / f"treebeard_alocasia_s{s}_2026-09-29.csv", s) for s in (2, 3, 4, 5, 9)}
    out = {}
    for s in m3.RIGS:
        sig = m3.wetness(s, bins[s])
        step = m3.watering_step(sig)
        daily, trace = {}, {}
        for day in m3.DAYS:
            if s in m3.INSERTED and m3.at(day, 3) < m3.INSERTED[s] + m3.SETTLE:
                continue
            got = m3.chord_residual(sig, day)
            if got is None:
                continue
            keys, r = got
            daily[day] = float(np.ptp(r)) / abs(step) * 100
            if day in MSV3_TRACE_DAYS:
                trace.update({k: v / abs(step) * 100 for k, v in zip(keys, r)})
        out[s] = {"label": m3.RIGS[s][2], "daily": daily, "trace": trace}
    return out


def msv4(r: dict) -> dict:
    b = defaultdict(list)
    for x in r["rows"]:
        t = x["time"].astimezone()
        b[t.replace(minute=t.minute // 10 * 10, second=0, microsecond=0)].append(x["stored"])
    s = {k: st.median(v) for k, v in sorted(b.items())}

    def med(a, c):
        v = [y for k, y in s.items() if a <= k < c]
        return st.median(v) if len(v) >= 3 else None

    pre = lambda d: med(d.replace(hour=3, minute=0), d.replace(hour=6, minute=0))
    waterings = sorted(w.astimezone() for w in r["waterings"])
    steps = {}
    for w in waterings:
        d0 = w.replace(hour=0, minute=0, second=0, microsecond=0) - (timedelta(days=1) if w.hour < 6 else timedelta(0))
        a, c = pre(d0), pre(d0 + timedelta(days=1))
        if a is not None and c is not None:
            steps[w] = c - a
    daily, residuals = {}, {}
    d = min(s).replace(hour=0, minute=0, second=0, microsecond=0) + timedelta(days=1)
    while d + timedelta(days=2) <= max(s):
        t0 = d.replace(hour=4, minute=30)
        t1 = t0 + timedelta(days=1)
        before = [w for w in waterings if w < t0]
        holds = any(d.replace(hour=3) <= w < d.replace(hour=6) + timedelta(days=1) for w in waterings)
        if before and not holds and before[-1] in steps:
            m0, m1 = pre(d), pre(d + timedelta(days=1))
            keys = [k for k in s if t0 <= k < t1]
            if m0 is not None and m1 is not None and len(keys) >= 100:
                frac = np.array([(k - t0) / (t1 - t0) for k in keys])
                res = (np.array([s[k] for k in keys]) - (m0 + frac * (m1 - m0))) / abs(steps[before[-1]]) * 100
                daily[d.date()] = float(np.ptp(res))
                residuals[d.date()] = list(zip(keys, res))
        d += timedelta(days=1)
    return {"daily": daily, "residuals": residuals, "steps": steps}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("msv3_dir", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    old = msv3(args.msv3_dir)
    new = {r["name"]: msv4(r) for r in fs.load()}
    beg = new["Begonia"]

    # Six settled consecutive Begonia days to set beside MSV3's 09-23..09-28.
    days = sorted(beg["residuals"])
    run = None
    for i in range(len(days) - 5):
        if (days[i + 5] - days[i]).days == 5:
            run = days[i:i + 6]
            break

    fig = plt.figure(figsize=(18, 7.2), dpi=120, facecolor=SURFACE)
    grid = fig.add_gridspec(1, 2, width_ratios=[1.45, 1], wspace=0.42, left=0.05, right=0.985, top=0.83, bottom=0.11)
    ax = fig.add_subplot(grid[0, 0])
    axb = fig.add_subplot(grid[0, 1])
    fs.style(ax)
    fs.style(axb)
    axb.set_axisbelow(True)

    def put(trace_pts, t0, colour, label, lw):
        xs = [(k - t0).total_seconds() / 86400 for k, _ in trace_pts]
        ys = [v for _, v in trace_pts]
        ax.plot(xs, ys, color=colour, lw=lw, label=label)

    t0 = m3.at(MSV3_TRACE_DAYS[0], 4, 30)
    for s, colour, lw in ((9, MSV3_COLOUR, 2.0), (5, MSV3_BUILT, 2.0)):
        pts = sorted(old[s]["trace"].items())
        # break the line between days so the chord jumps are not drawn
        for day in MSV3_TRACE_DAYS:
            seg = [(k, v) for k, v in pts if m3.at(day, 4, 30) <= k < m3.at(day, 4, 30) + timedelta(days=1)]
            put(seg, t0, colour, (f"MSV3 blade + AM detector (sensor 9), Sept" if s == 9
                                  else "MSV3 as built: blade + FDC (probe 5), Sept") if day == MSV3_TRACE_DAYS[0] else None, lw)
    b0 = beg["residuals"][run[0]][0][0].replace(hour=4, minute=30)
    for j, d in enumerate(run):
        put(beg["residuals"][d], b0, MSV4_COLOUR, f"MSV4 Begonia (sensor 11), {run[0]:%m-%d} to {run[-1]:%m-%d}" if j == 0 else None, 2.6)
    ax.axhline(0, color=MUTED, lw=1)
    ax.set_xlim(0, 6)
    ax.set_xticks(range(0, 7))
    ax.set_xlabel("days (each day starts 04:30)", color=MUTED, fontsize=12)
    ax.set_ylabel("swing within the day, % of own watering step", color=MUTED, fontsize=12)
    ax.legend(loc="lower left", fontsize=12, frameon=False)
    ax.set_title("1.  Six days of each, on one scale", loc="left", fontsize=15, color=INK, pad=28)
    ax.text(0, 1.02, "each day minus the line through its two predawn readings", transform=ax.transAxes,
            fontsize=12, color=MUTED)

    rows = [
        ("MSV3 blade + AM detector (s9)", st.median(old[9]["daily"].values()), MSV3_COLOUR),
        ("MSV2 electrode + RC (s2)", st.median(old[2]["daily"].values()), "#C9CCD1"),
        ("v1.2 electrode + FDC (p4)", st.median(old[4]["daily"].values()), "#C9CCD1"),
        ("MSV3 as built: blade + FDC (p5)", st.median(old[5]["daily"].values()), MSV3_BUILT),
        ("v1.2 electrode + AM (s3)", st.median(old[3]["daily"].values()), "#C9CCD1"),
        ("MSV4 Alocasia (s10)", st.median(new["Alocasia"]["daily"].values()), MSV4_ALOC),
        ("MSV4 Begonia (s11)", st.median(beg["daily"].values()), MSV4_COLOUR),
    ]
    y = np.arange(len(rows))
    axb.barh(y, [v for _, v, _ in rows], color=[c for _, _, c in rows], height=0.62)
    for yy, (_, v, _) in zip(y, rows):
        axb.text(v + 0.6, yy, f"{v:.1f} %", va="center", fontsize=12, color=INK)
    axb.set_yticks(y)
    axb.set_yticklabels([n for n, _, _ in rows], fontsize=12, color=INK)
    axb.invert_yaxis()
    axb.grid(False, axis="y")
    axb.set_xlim(0, 36)
    axb.set_xlabel("median daily swing, % of own watering step", color=MUTED, fontsize=12)
    axb.set_title("2.  Every rig, median over its days", loc="left", fontsize=15, color=INK, pad=28)
    axb.text(0, 1.02, "grey: earlier electrode / readout combinations", transform=axb.transAxes,
             fontsize=12, color=MUTED)

    fig.text(0.05, 0.955, "Board temperature swung about the same each day: MSV3 boards 5.6-7.9 °C, "
             "MSV4 Begonia 6.0 °C (median daily range).", fontsize=12, color=MUTED)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=SURFACE)
    print(args.out)
    for name, v, _ in rows:
        print(f"  {name:36s} {v:5.2f} %")
    print(f"  Begonia trace days {run[0]} .. {run[-1]}; steps {[round(x) for x in beg['steps'].values()]} units")
    print(f"  Begonia days: " + " ".join(f"{k:%m-%d}:{v:.1f}" for k, v in sorted(beg['daily'].items())))


if __name__ == "__main__":
    main()
