#!/usr/bin/env python3
"""Alocasia pot test: does the daily swing enter through the electrode or the circuit?

Two measurements, both with as few assumptions as possible.

1. Daily swing on a common scale. Each day (06:00 -> 06:00 PDT) has the straight line
   between its two predawn medians (03:00-06:00) subtracted, which removes that day's
   dry-down without assuming its shape over more than one day. The peak-to-peak of what
   is left is the daily swing. It is divided by the rig's own 2026-09-22 watering step
   (predawn 09-22 -> predawn 09-23), so AM millivolts, FDC counts and MSV2 LSB share one
   scale. The maintainer states the pot saturates uniformly (fleet.json), which is what
   makes the step a common calibration.
2. Circuit alone, same chip, same instants. Until 2026-09-14 the MSV3 firmware also read
   a bare reference tank (no electrode) on the same FDC2214. Its daily swing, on the same
   board at the same instants, is the circuit's own term.

Physical state is from MSV3/fleet.json (Alocasia pot verified 2026-09-29: nothing moved
since 09-24, no watering since 09-22). Findings:
MSV3/docs/MSV3_ALOCASIA_ELECTRODE_VS_CIRCUIT_2026-09-29.md

    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor N --raw \\
        --since-hours 620 --csv <dir>/treebeard_alocasia_sN_2026-09-29.csv    # N = 2 3 4 5 8 9
    python MSV3/scripts/analyze_alocasia_electrode_vs_circuit.py <dir> [--suffix 2026-09-29]
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics as stats
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

import numpy as np

PACIFIC = timezone(timedelta(hours=-7), "PDT")
DOCS = Path(__file__).resolve().parents[1] / "docs"
FIGURE = DOCS / "MSV3_ALOCASIA_ELECTRODE_VS_CIRCUIT_2026-09-29.png"
PANEL_A = DOCS / "MSV3_ALOCASIA_ELECTRODE_VS_CIRCUIT_2026-09-29_PANEL_A.png"     # standalone, for slides
PANEL_C = DOCS / "MSV3_ALOCASIA_ELECTRODE_VS_CIRCUIT_2026-09-29_PANEL_C.png"


def at(day, hour=0, minute=0):
    return datetime(2026, 9, day, hour, minute, tzinfo=PACIFIC)


WATERING = at(22, 10, 50)
# First valid moment of each sensor in the shared Alocasia pot (fleet.json insertion_dates).
# Probe 8 was in this pot 09-08 23:00 -> 09-14 ~11:00 (three-generation pot test).
INSERTED = {4: at(18, 20, 53), 5: at(19, 20, 24), 9: at(14, 18, 30), 8: at(8, 23, 0)}
SETTLE = timedelta(hours=24)            # skip the first day after insertion
FDC = {4, 5, 8}
AM = {3, 9}

# Alocasia rigs in the 2x2 plus sensor 2. Electrode family, readout, label.
RIGS = {
    3: ("v1.2", "AM", "v1.2 + AM detector  (sensor 3)"),
    4: ("v1.2", "FDC", "v1.2 + MSV3 FDC board  (probe 4)"),
    5: ("interdigitated", "FDC", "MSV3 blade + MSV3 FDC board  (probe 5)"),
    2: ("interdigitated", "RC", "MSV2 interdigitated + RC  (sensor 2)"),
    9: ("interdigitated", "AM", "MSV3 blade + AM detector  (sensor 9)"),
}
# Days are 04:30 -> 04:30 PDT. Excluded: 09-18 and 09-19, when probes 4 and 5 were pushed into this
# pot (20:53 and 20:24), which disturbs the neighbours (sensor 3 reads 5.6 and 5.3 % those days
# against ~2 % otherwise), and 09-22, which holds the watering.
EXCLUDED = {18: "probe 4 inserted", 19: "probe 5 inserted", 22: "watered"}
DAYS = [d for d in range(15, 29) if d not in EXCLUDED]
PROBE8_DAYS = range(10, 14)     # probe 8 entered 09-08 23:00; 09-09 is its settling day


def number(value):
    try:
        return float(value) if value not in (None, "") else None
    except ValueError:
        return None


def median_bins(path, sensor, minutes=10):
    """Ten-minute medians. FDC: phase 0 only, counts in range. Before-insertion rows dropped."""
    buckets = defaultdict(lambda: defaultdict(list))
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            t = datetime.fromisoformat(row["timestamp_iso"].replace("+00", "+00:00")).astimezone(PACIFIC)
            if sensor in INSERTED and t < INSERTED[sensor]:
                continue
            if sensor == 8 and t >= at(14, 11):
                continue
            if sensor in FDC:
                counts = number(row["raw_fdc_counts"])
                if row["tank_phase"] != "0" or counts is None or not 1e6 < counts < 5e7:
                    continue
            key = t.replace(minute=t.minute // minutes * minutes, second=0, microsecond=0)
            for name in ("moisture", "raw_moisture_khz", "raw_fdc_counts", "bare_tank_fdc_counts", "board_temp_c"):
                value = number(row.get(name))
                if value is not None:
                    buckets[key][name].append(value)
    return {k: {n: stats.median(v) for n, v in f.items()} for k, f in sorted(buckets.items())}


def wetness(sensor, bins, field="raw_fdc_counts"):
    """Wetter-positive native units. FDC: 100*ln(C/Cref) = -200*ln(counts/ref) (log-cap %)."""
    if sensor in AM:
        return {k: -v["raw_moisture_khz"] for k, v in bins.items() if "raw_moisture_khz" in v}
    if sensor == 2:
        return {k: v["moisture"] for k, v in bins.items() if "moisture" in v}
    raw = {k: v[field] for k, v in bins.items() if field in v}
    ref = stats.median(raw.values())
    return {k: -200.0 * math.log(c / ref) for k, c in raw.items()}


def median_in(series, start, end):
    values = [v for k, v in series.items() if start <= k < end]
    return stats.median(values) if len(values) >= 6 else None


def chord_residual(series, day):
    """06:00-ish day -> next day, minus the line through the two predawn (03-06) medians."""
    m0 = median_in(series, at(day, 3), at(day, 6))
    m1 = median_in(series, at(day, 3) + timedelta(days=1), at(day, 6) + timedelta(days=1))
    if m0 is None or m1 is None:
        return None
    t0 = at(day, 4, 30)
    t1 = t0 + timedelta(days=1)
    keys = [k for k in series if t0 <= k < t1]
    if len(keys) < 100:
        return None
    frac = np.array([(k - t0) / (t1 - t0) for k in keys])
    return keys, np.array([series[k] for k in keys]) - (m0 + frac * (m1 - m0))


def watering_step(series):
    return median_in(series, at(23, 3), at(23, 6)) - median_in(series, at(22, 3), at(22, 6))


def selftest():
    """Synthetic check of the estimator. A known 5.0-unit daily bump on a linear dry-down must
    come back as 5.0. A curved dry-down alone leaves a bow, because a chord is straight: it must
    stay under 5 % of that day's drop. The curve here (tau 60 h) is steeper than any rig's
    dry-down from 09-23 on, so the bow is an upper bound on this bias."""
    lin, curved = {}, {}
    for i in range(0, 3 * 144):
        t = at(10) + timedelta(minutes=10 * i)
        hours = (t - at(10)).total_seconds() / 3600
        bump = 5.0 * math.exp(-((t.hour + t.minute / 60 - 14.0) / 2.5) ** 2)
        lin[t] = -0.5 * hours + bump
        curved[t] = -40.0 * (1 - math.exp(-hours / 60.0))
    swing = np.ptp(chord_residual(lin, 11)[1])
    bow = np.ptp(chord_residual(curved, 11)[1])
    drop = curved[at(11, 4, 30)] - curved[at(12, 4, 30)]
    assert abs(swing - 5.0) < 0.05, swing
    assert bow < 0.05 * drop, (bow, drop)
    return swing, bow / drop


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", type=Path)
    ap.add_argument("--suffix", default="2026-09-29")
    ap.add_argument("--no-figure", action="store_true")
    args = ap.parse_args()

    swing, bow = selftest()
    print(f"selftest: 5.00 bump on a linear dry-down -> {swing:.3f}; "
          f"a curved dry-down alone leaves a bow of {bow * 100:.1f} % of that day's drop")

    bins = {s: median_bins(args.directory / f"treebeard_alocasia_s{s}_{args.suffix}.csv", s)
            for s in (2, 3, 4, 5, 8, 9)}
    sig = {s: wetness(s, bins[s]) for s in RIGS}
    steps = {s: watering_step(sig[s]) for s in RIGS}

    # 1. Daily swing, % of own watering step.
    daily = {s: {} for s in RIGS}
    resid = {s: {} for s in RIGS}
    for s in RIGS:
        for day in DAYS:
            if s in INSERTED and at(day, 3) < INSERTED[s] + SETTLE:
                continue
            got = chord_residual(sig[s], day)
            if got is None:
                continue
            keys, r = got
            daily[s][day] = float(np.ptp(r)) / abs(steps[s]) * 100
            resid[s].update({k: v / abs(steps[s]) * 100 for k, v in zip(keys, r)})

    die = {s: {k: v["board_temp_c"] for k, v in bins[s].items() if "board_temp_c" in v} for s in (4, 5)}
    die_range = {s: {} for s in (4, 5)}
    for s in (4, 5):
        for day in DAYS:
            vals = [v for k, v in die[s].items() if at(day, 4, 30) <= k < at(day, 4, 30) + timedelta(days=1)]
            if len(vals) > 100:
                die_range[s][day] = max(vals) - min(vals)

    print("\nWatering step 09-22 (predawn -> predawn), native units, wetter-positive:")
    for s in RIGS:
        unit = {"AM": "mV", "FDC": "logC%", "RC": "LSB"}[RIGS[s][1]]
        print(f"  {RIGS[s][2]:44s} {steps[s]:+9.2f} {unit}")

    print("\nDaily swing, % of own watering step (chord-detrended peak-to-peak, 06:00 -> 06:00):")
    print(f"  {'rig':44s} {'days':>4s} {'median':>7s} {'min':>6s} {'max':>6s} {'pre-09-22':>10s} {'post':>6s}")
    for s in RIGS:
        v = daily[s]
        pre = [x for d, x in v.items() if d < 22]
        post = [x for d, x in v.items() if d > 22]
        print(f"  {RIGS[s][2]:44s} {len(v):4d} {stats.median(v.values()):7.2f} {min(v.values()):6.2f} "
              f"{max(v.values()):6.2f} {stats.median(pre) if pre else float('nan'):10.2f} {stats.median(post):6.2f}")
    print("  sensor 2: 1 LSB = %.1f %% of its step, so its swing is 1-3 LSB and quantization-limited."
          % (100 / abs(steps[2])))

    print("  Dry-down bias bound (selftest bow x each rig's largest predawn-to-predawn drop on a used day):")
    for s in RIGS:
        drops = []
        for d in daily[s]:
            m0 = median_in(sig[s], at(d, 3), at(d, 6))
            m1 = median_in(sig[s], at(d + 1, 3), at(d + 1, 6))
            drops.append(abs(m1 - m0) / abs(steps[s]) * 100)
        print(f"    {RIGS[s][2]:44s} largest drop {max(drops):5.2f} % of step -> bias < {0.05 * max(drops):.2f} % of step")

    paired = [daily[9][d] / daily[3][d] for d in daily[9] if d in daily[3]]
    print(f"\nSame AM detector design, blade / v1.2, paired by day: median x{stats.median(paired):.1f} "
          f"(range x{min(paired):.1f}-x{max(paired):.1f}, {len(paired)} days)")
    # Upper bound on the detector circuit's share of sensor 9: credit ALL of sensor 3's millivolt
    # swing to the detector design the two rigs share.
    mv = {s: {d: x * abs(steps[s]) / 100 for d, x in daily[s].items()} for s in (3, 9)}
    share = [mv[3][d] / mv[9][d] for d in mv[9] if d in mv[3]]
    print(f"In detector mV per day: sensor 3 median {stats.median(mv[3].values()):.1f}, sensor 9 median "
          f"{stats.median(mv[9].values()):.1f}. All of sensor 3's swing as the circuit term = "
          f"{stats.median(share) * 100:.0f} % of sensor 9's (paired median, max {max(share) * 100:.0f} %)")
    for s in (4, 5):
        print(f"Probe {s} die range per day, median {stats.median(die_range[s].values()):.2f} C "
              f"({min(die_range[s].values()):.2f}-{max(die_range[s].values()):.2f})")
    per_c = {s: stats.median(daily[s][d] / die_range[s][d] for d in daily[s] if d in die_range[s]) for s in (4, 5)}
    print(f"Per C of own die range, % of step: probe 4 {per_c[4]:.2f}, probe 5 {per_c[5]:.2f}")

    # 2. Circuit alone: bare reference tank on the same FDC2214, probe 8 in this pot.
    conn8 = wetness(8, bins[8], "raw_fdc_counts")
    bare8 = wetness(8, bins[8], "bare_tank_fdc_counts")
    die8 = {k: v["board_temp_c"] for k, v in bins[8].items() if "board_temp_c" in v}
    print("\nProbe 8, MSV3 blade + FDC in this pot, connected channel vs bare reference tank (same chip, same instants):")
    rows8 = []
    tank = {"conn": {}, "bare": {}}
    for day in PROBE8_DAYS:
        a, b = chord_residual(conn8, day), chord_residual(bare8, day)
        if not a or not b:
            continue
        tank["conn"].update(dict(zip(*a)))
        tank["bare"].update(dict(zip(*b)))
        t = np.array([die8.get(k, np.nan) for k in a[0]])
        ok = ~np.isnan(t)
        sc, sb = np.polyfit(t[ok], a[1][ok], 1)[0], np.polyfit(t[ok], b[1][ok], 1)[0]
        rows8.append((day, np.ptp(a[1]), np.ptp(b[1]), np.nanmax(t) - np.nanmin(t), sc, sb))
        print(f"  09-{day:02d}  connected {np.ptp(a[1]):.3f}  bare {np.ptp(b[1]):.3f} logC%   "
              f"die range {np.nanmax(t) - np.nanmin(t):.2f} C   slope {sc:+.3f} vs {sb:+.3f} logC%/C")
    mc, mb = stats.median(r[1] for r in rows8), stats.median(r[2] for r in rows8)
    print(f"  median: connected {mc:.3f}, bare {mb:.3f} logC% -> circuit alone is {mb / mc * 100:.0f} % of the swing")

    if not args.no_figure:
        figure(resid, daily, tank, die, rows8, paired, {s: stats.median(die_range[s].values()) for s in (4, 5)})


SURFACE, INK, INK2, MUTED, GRID, BASELINE = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"     # v1.2, interdigitated, circuit only
COLOUR = {"v1.2": BLUE, "interdigitated": ORANGE}
MARKER = {"AM": "o", "FDC": "s", "RC": "^"}
SPAN = (at(16, 4, 30), at(29, 4, 30))


def _setup():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.family": ["Segoe UI", "DejaVu Sans"], "font.size": 9.5,
                         "axes.edgecolor": BASELINE, "axes.labelcolor": INK2, "xtick.color": MUTED,
                         "ytick.color": MUTED, "axes.titlecolor": INK, "text.color": INK})
    return plt


def _style(ax, surface=SURFACE):
    ax.set_facecolor(surface)
    ax.grid(axis="y", color=GRID, lw=0.6)
    ax.spines[["top", "right"]].set_visible(False)
    ax.tick_params(length=0)


def _gapped(series):
    keys = sorted(series)
    x, y = [], []
    for i, t in enumerate(keys):
        if i and (t - keys[i - 1]) > timedelta(minutes=40):
            x.append(t - timedelta(minutes=10))
            y.append(np.nan)
        x.append(t)
        y.append(series[t])
    return x, y


def _panel_a(ax, resid, daily, paired, title, surface=SURFACE, fs=1.0, ncol=2):
    """Sensors 3 and 9: same AM detector design, only the electrode differs."""
    import matplotlib.dates as mdates
    from matplotlib.lines import Line2D
    _style(ax, surface)
    med3, med9 = stats.median(daily[3].values()), stats.median(daily[9].values())
    for s in (3, 9):
        ax.plot(*_gapped(resid[s]), color=COLOUR[RIGS[s][0]], lw=1.1)
    ax.axhline(0, color=BASELINE, lw=0.8)
    for day, why in EXCLUDED.items():
        ax.axvspan(at(day, 4, 30), at(day + 1, 4, 30), color=GRID, alpha=0.55, lw=0)
        stagger = 4 * fs if (day - 1) in EXCLUDED else 0     # adjacent excluded days: lift the second label
        ax.text(at(day, 16, 30), -44 + stagger, why, ha="center", va="bottom", fontsize=8 * fs, color=MUTED)
    ax.set_ylim(-46, 21)
    ax.set_ylabel("daily swing, % of own watering step")
    ax.set_title(title.format(ratio=stats.median(paired)), loc="left", fontsize=11 * fs, weight="bold")
    ax.legend(handles=[Line2D([], [], color=BLUE, lw=2,
                              label=f"v1.2 electrode + AM (sensor 3): median {med3:.1f} % of a watering per day"),
                       Line2D([], [], color=ORANGE, lw=2,
                              label=f"MSV3 interdigitated blade + AM (sensor 9): median {med9:.1f} %")],
              loc="upper left", frameon=False, fontsize=9 * fs, ncol=ncol)
    ax.xaxis.set_major_locator(mdates.DayLocator(tz=PACIFIC))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=PACIFIC))
    ax.set_xlim(*SPAN)


def _panel_c(ax, daily, die_med, title, surface=SURFACE, fs=1.0):
    """Every Alocasia rig on the watering-step scale, one dot per day."""
    from matplotlib.lines import Line2D
    _style(ax, surface)
    ax.grid(axis="y", visible=False)
    ax.grid(axis="x", color=GRID, lw=0.6)
    order = sorted(RIGS, key=lambda s: stats.median(daily[s].values()))
    notes = {4: f"MSV3 board in full sun, die swing ~{die_med[4]:.0f} °C",
             5: f"shade side of the pot, die swing ~{die_med[5]:.0f} °C",
             2: "hollow: 1 LSB = 7 % of its step", 3: "", 9: "sun side"}
    rng = np.random.default_rng(7)
    for row, s in enumerate(order):
        fam, readout, label = RIGS[s]
        vals = np.array(list(daily[s].values()))
        hollow = s == 2
        ax.scatter(vals, row + rng.uniform(-0.13, 0.13, len(vals)), s=30, marker=MARKER[readout],
                   facecolor="none" if hollow else COLOUR[fam], edgecolor=COLOUR[fam] if hollow else surface,
                   linewidth=1.2 if hollow else 0.8, zorder=3)
        m = stats.median(vals)
        ax.plot([m, m], [row - 0.24, row + 0.24], color=INK, lw=1.6, zorder=4)
        ax.text(0.6, row + 0.3, label, fontsize=9 * fs, color=INK, va="bottom", weight="semibold")
        ax.text(m, row - 0.3, f"{m:.1f} %" + (f"   {notes[s]}" if notes[s] else ""), fontsize=8.5 * fs,
                color=INK2, va="top", ha="left" if m < 30 else "center")
    ax.set_yticks([])
    ax.spines["left"].set_visible(False)
    ax.set_ylim(-0.75, len(order) - 0.05)
    ax.set_xlim(0, 55)
    ax.set_xlabel("daily swing, % of own watering step (one dot per day, bar = median)")
    ax.set_title(title, loc="left", fontsize=11 * fs, weight="bold")
    ax.legend(handles=[Line2D([], [], ls="", marker="o", color=BLUE, label="v1.2 electrode"),
                       Line2D([], [], ls="", marker="o", color=ORANGE, label="interdigitated electrode"),
                       Line2D([], [], ls="", marker="o", mfc="none", color=MUTED, label="AM readout"),
                       Line2D([], [], ls="", marker="s", mfc="none", color=MUTED, label="FDC readout"),
                       Line2D([], [], ls="", marker="^", mfc="none", color=MUTED, label="RC readout")],
              loc="lower right", frameon=False, fontsize=8.5 * fs)


def presentation_panels(resid, daily, paired, die_med):
    """Panels A and C on their own, white background, sized 1:1 for a 10 x 7.5 in slide's content
    area (9 in wide) so nothing is scaled down when pasted in. Fonts are 12 pt and up for projection."""
    plt = _setup()
    plt.rcParams.update({"font.size": 12.5})
    white = "#ffffff"
    fig, ax = plt.subplots(figsize=(9.0, 5.0), facecolor=white)
    _panel_a(ax, resid, daily, paired, "Same AM circuit, only the electrode differs: blade swings ×{ratio:.0f} more",
             surface=white, fs=1.3, ncol=1)
    ax.set_ylim(-46, 30)
    fig.subplots_adjust(left=0.1, right=0.98, top=0.91, bottom=0.08)
    fig.savefig(PANEL_A, dpi=200, facecolor=white)
    fig, ax = plt.subplots(figsize=(9.0, 5.9), facecolor=white)
    _panel_c(ax, daily, die_med, "Daily swing of every Alocasia rig, 09-16 → 09-29", surface=white, fs=1.3)
    fig.subplots_adjust(left=0.02, right=0.98, top=0.93, bottom=0.11)
    fig.savefig(PANEL_C, dpi=200, facecolor=white)
    print(f"wrote {PANEL_A}\nwrote {PANEL_C}")


def figure(resid, daily, tank, die, rows8, paired, die_med):
    plt = _setup()
    import matplotlib.dates as mdates
    from matplotlib.lines import Line2D

    surface, ink, ink2, muted, grid, base = SURFACE, INK, INK2, MUTED, GRID, BASELINE
    orange, aqua = ORANGE, AQUA
    style, gapped, span = _style, _gapped, SPAN

    fig = plt.figure(figsize=(13, 11.6), facecolor=surface)
    gs = fig.add_gridspec(3, 2, height_ratios=[0.5, 2.2, 2.5], width_ratios=[1, 1.1],
                          left=0.065, right=0.975, top=0.88, bottom=0.055, hspace=0.42, wspace=0.16)
    fig.text(0.065, 0.965, "Alocasia pot test: the daily swing enters at the electrode, not the circuit",
             fontsize=15, weight="bold", color=ink)
    fig.text(0.065, 0.94,
             "Daily swing = peak-to-peak left after removing each day's dry-down (the line between its 03-06 h medians), as % of "
             "that rig's own 2026-09-22 watering step,\nso AM millivolts, FDC counts and RC LSB share one scale. Wetter is up. "
             "Same pot, same soil, same days. Days excluded: 09-18 and 09-19 (probes 4 and 5 pushed into the pot), 09-22 (watered).",
             fontsize=9.3, color=ink2, va="top")

    # Strip: pot-side air at the probe heads.
    ax0 = fig.add_subplot(gs[0, :])
    style(ax0)
    ax0.plot(*gapped({t: v for t, v in die[4].items() if span[0] <= t < span[1]}), color=muted, lw=0.9)
    ax0.set_xlim(*span)
    ax0.set_ylabel("°C", rotation=0, labelpad=12)
    ax0.set_title("Sun-side head temperature (probe 4 die, from 09-18 when it entered the pot). "
                  "The noon sun reaches the shelf ~12:00 PDT", loc="left", fontsize=9.5, color=ink2)
    ax0.xaxis.set_major_locator(mdates.DayLocator(tz=PACIFIC))
    ax0.xaxis.set_major_formatter(mdates.DateFormatter("", tz=PACIFIC))

    # Panel A: same AM circuit, swap the electrode.
    ax = fig.add_subplot(gs[1, :], sharex=ax0)
    _panel_a(ax, resid, daily, paired, "A.  Same AM detector design, same outlet, same pot, same days. "
             "Only the electrode differs: the blade swings x{ratio:.0f} more")
    ax0.xaxis.set_major_formatter(mdates.DateFormatter("", tz=PACIFIC))

    # Panel B: circuit alone vs circuit + blade on one chip.
    axb = fig.add_subplot(gs[2, 0])
    style(axb)
    axb.plot(*gapped(tank["conn"]), color=orange, lw=1.1)
    axb.plot(*gapped(tank["bare"]), color=aqua, lw=1.1)
    axb.axhline(0, color=base, lw=0.8)
    mc, mb = stats.median(r[1] for r in rows8), stats.median(r[2] for r in rows8)
    axb.set_title("B.  One MSV3 board, one FDC chip, the same instants\n     (probe 8 in this pot, 09-10 → 09-14)",
                  loc="left", fontsize=11, weight="bold")
    axb.set_ylabel("daily swing, log-capacitance %")
    axb.xaxis.set_major_locator(mdates.DayLocator(tz=PACIFIC))
    axb.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=PACIFIC))
    ylo, yhi = axb.get_ylim()
    axb.set_ylim(ylo - 0.45 * (yhi - ylo), yhi + 0.1 * (yhi - ylo))
    axb.legend(handles=[Line2D([], [], color=orange, lw=2, label=f"channel with the blade in soil: median {mc:.2f} per day"),
                        Line2D([], [], color=aqua, lw=2, label=f"bare reference tank, no electrode: median {mb:.2f} per day")],
               loc="lower left", frameon=False, fontsize=9)
    axb.text(0.99, 0.02, f"circuit alone = {mb / mc * 100:.0f} %\nof the swing", transform=axb.transAxes,
             ha="right", va="bottom", fontsize=9.5, color=ink)

    # Panel C: every Alocasia rig on the watering-step scale.
    _panel_c(fig.add_subplot(gs[2, 1]), daily, die_med, "C.  Every Alocasia rig, 09-16 → 09-29")

    fig.savefig(FIGURE, dpi=150, facecolor=surface)
    print(f"\nwrote {FIGURE}")
    presentation_panels(resid, daily, paired, die_med)


if __name__ == "__main__":
    main()
