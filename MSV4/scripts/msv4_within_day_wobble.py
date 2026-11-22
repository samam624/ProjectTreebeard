#!/usr/bin/env python3
"""What the "within-day wobble" of the moisture number is made of, Alocasia (sensor 10) vs Begonia (11).

The wobble is the one in msv4_stored_vs_mass_both.analyse(): hourly medians of the stored number
about a centred 24 h running median, in grams through each pot's own fitted line, leaving out the
24 h after every watering and everything before the first watering (the soil settling round a
newly inserted blade). This script splits it three ways:

  daily shape   the average residual at each hour of the day (what repeats every day)
  leftover      the rest: changes from day to day, plus noise
  white floor   per-reading noise / sqrt(readings in the hour): what averaging alone leaves

and tests whether the daily shape is real water by comparing it with the scale: every pair of
consecutive weighings (4 to 16 h apart, no watering between, both 24 h or more after a watering)
gives the grams the scale lost and the grams the moisture number lost over the same interval.
A real daytime drink shows up on both; a reading artefact shows up only on the probe.

    python MSV4/scripts/msv4_within_day_wobble.py [-o out.png]
"""
from __future__ import annotations

import argparse
import math
import statistics
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
import msv4_stored_vs_mass_both as both

SKIP_H = both.FIT_SKIP_HOURS
PAIR_MIN_H, PAIR_MAX_H = 4.0, 16.0
DAY_HOURS = range(9, 19)   # 09:00-19:00 local counts as "daytime" for the interval split


def hours_since(waterings, t) -> float:
    past = [w for w in waterings if w <= t]
    return (t - past[-1]).total_seconds() / 3600 if past else -1.0


def decompose(r: dict) -> dict:
    w = sorted(r["waterings"])
    cyc = lambda t: sum(1 for x in w if x <= t)
    hourly = [(t, u) for t, u, _ in r["hourly"] if hours_since(w, t) >= SKIP_H]
    resid = []
    for t, u in hourly:
        # Only hours whose whole +-12 h window is clean on BOTH sides: a one-sided window near a
        # watering turns the drying slope itself into "wobble".
        if hours_since(w, t - timedelta(hours=12)) < SKIP_H or cyc(t + timedelta(hours=12)) != cyc(t):
            continue
        win = [v for s, v in hourly if abs((s - t).total_seconds()) <= 12 * 3600 and cyc(s) == cyc(t)]
        if len(win) >= 20:
            resid.append((t, (u - statistics.median(win)) / r["slope"]))
    by_hour = defaultdict(list)
    for t, g in resid:
        by_hour[t.astimezone().hour].append(g)
    profile = {h: statistics.fmean(v) for h, v in by_hour.items()}
    shape = [profile[t.astimezone().hour] for t, _ in resid]
    left = [g - s for (_, g), s in zip(resid, shape)]
    rms = lambda xs: math.sqrt(statistics.fmean(x * x for x in xs))

    # hour-of-day drying rate from consecutive hourly medians, g/h (negative = drying)
    rate = defaultdict(list)
    for (t0, u0), (t1, u1) in zip(hourly, hourly[1:]):
        if t1 - t0 == timedelta(hours=1) and cyc(t0) == cyc(t1):
            rate[t1.astimezone().hour].append((u1 - u0) / r["slope"])
    ups = sum(d for v in rate.values() for d in v if d > 0)
    downs = -sum(d for v in rate.values() for d in v if d < 0)

    # scale vs probe over the same intervals
    near = lambda t: [x["stored"] for x in r["rows"] if abs((x["time"] - t).total_seconds()) <= both.PAIR_MINUTES * 60]
    ws = sorted((x for x in r["weighings"] if not x["post"]), key=lambda x: x["time"])
    pairs = []
    for a, b in zip(ws, ws[1:]):
        dt = (b["time"] - a["time"]).total_seconds() / 3600
        if not (PAIR_MIN_H <= dt <= PAIR_MAX_H) or cyc(a["time"]) != cyc(b["time"]):
            continue
        if hours_since(w, a["time"]) < SKIP_H:
            continue
        na, nb = near(a["time"]), near(b["time"])
        if not na or not nb:
            continue
        mid = a["time"] + (b["time"] - a["time"]) / 2
        day_share = sum(1 for k in range(int(dt * 4))
                        if (a["time"] + timedelta(minutes=15 * k)).astimezone().hour in DAY_HOURS) / max(1, int(dt * 4))
        pairs.append({"a": a["time"], "dt": dt, "scale": b["grams"] - a["grams"],
                      "probe": (statistics.median(nb) - statistics.median(na)) / r["slope"],
                      "day": day_share >= 0.5, "mid": mid})
    floor = r["per_reading_g"] / math.sqrt(r["per_hour"])
    return {"resid": resid, "profile": profile, "rate": rate, "pairs": pairs,
            "total": rms([g for _, g in resid]), "shape": rms(shape), "left": rms(left), "floor": floor,
            "ups": ups, "downs": downs}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", type=Path,
                        default=both.ROOT / "data" / "sensor_logs" / "latest" / "within_day_wobble_s10_s11.png")
    args = parser.parse_args()
    results = fs.load()
    for r in results:
        r.update(decompose(r))

    for r in results:
        print(f"{r['name']} (sensor {r['sensor']}), {len(r['resid'])} hours, slope {r['slope']:.2f} units/g")
        print(f"  wobble total {r['total']:.2f} g rms = daily shape {r['shape']:.2f} g + leftover {r['left']:.2f} g "
              f"(white floor {r['floor']:.2f} g)")
        print(f"  hourly steps: {r['downs']:.0f} g down, {r['ups']:.0f} g up "
              f"(up / down = {r['ups'] / r['downs']:.2f})")
        rates = {h: statistics.fmean(v) for h, v in sorted(r["rate"].items())}
        print("  mean drying rate by hour, g/h: " + " ".join(f"{h:02d}:{v:+.1f}" for h, v in rates.items()))
        for kind in (True, False):
            sel = [p for p in r["pairs"] if p["day"] == kind]
            if sel:
                sr = sum(p["scale"] for p in sel) / sum(p["dt"] for p in sel)
                pr = sum(p["probe"] for p in sel) / sum(p["dt"] for p in sel)
                print(f"  {'daytime' if kind else 'night  '} intervals: {len(sel):2d}, scale {sr:+.2f} g/h, "
                      f"probe {pr:+.2f} g/h")
        for p in r["pairs"]:
            print(f"     {p['a'].astimezone():%m-%d %H:%M} {p['dt']:5.1f} h {'day  ' if p['day'] else 'night'} "
                  f"scale {p['scale']:+6.1f} g  probe {p['probe']:+6.1f} g")

    fig = plt.figure(figsize=(18, 7.0), dpi=120, facecolor=fs.SURFACE)
    grid = fig.add_gridspec(1, 3, width_ratios=[1.15, 1, 0.85], wspace=0.32,
                            left=0.05, right=0.985, top=0.84, bottom=0.11)
    ax_r, ax_p, ax_b = (fig.add_subplot(grid[0, i]) for i in range(3))
    for ax in (ax_r, ax_p, ax_b):
        fs.style(ax)

    for r in results:
        c = fs.POT_COLOURS[r["sensor"]]
        hrs = sorted(r["rate"])
        mean = [statistics.fmean(r["rate"][h]) for h in hrs]
        ax_r.plot([h + 0.5 for h in hrs], mean, color=c, lw=2.4, marker="o", ms=6, label=r["name"])
    ax_r.axhline(0, color=fs.MUTED, lw=1)
    ax_r.set_xticks(range(0, 25, 3))
    ax_r.set_xticklabels([f"{h:02d}:00" for h in range(0, 25, 3)])
    ax_r.set_xlim(0, 24)
    ax_r.set_xlabel("hour of the day", color=fs.MUTED, fontsize=12)
    ax_r.set_ylabel("change in the reading, g of water per hour", color=fs.MUTED, fontsize=12)
    ax_r.set_title("1.  When each pot dries, by the probe", loc="left", fontsize=15, color=fs.INK, pad=28)
    ax_r.text(0, 1.02, "average hour-to-hour change of the hourly medians", transform=ax_r.transAxes,
              fontsize=12, color=fs.MUTED)
    ax_r.legend(loc="lower left", fontsize=12, frameon=False)

    lim = 0
    for r in results:
        c = fs.POT_COLOURS[r["sensor"]]
        for kind, marker in ((True, "o"), (False, "s")):
            sel = [p for p in r["pairs"] if p["day"] == kind]
            ax_p.scatter([p["scale"] for p in sel], [p["probe"] for p in sel], s=70, color=c, marker=marker,
                         edgecolor=fs.SURFACE, linewidth=1.2, zorder=3,
                         label=f"{r['name']}, {'daytime' if kind else 'overnight'}")
            lim = max([lim] + [abs(p["scale"]) for p in sel] + [abs(p["probe"]) for p in sel])
    lim = math.ceil((lim + 5) / 10) * 10
    ax_p.plot([-lim, 5], [-lim, 5], color=fs.INK, lw=1.4, ls=(0, (5, 4)))
    ax_p.set_xlim(-lim, 5)
    ax_p.set_ylim(-lim, 5)
    ax_p.set_aspect("equal")
    ax_p.set_xlabel("grams lost on the scale", color=fs.MUTED, fontsize=12)
    ax_p.set_ylabel("grams lost by the moisture number", color=fs.MUTED, fontsize=12)
    ax_p.set_title("2.  The same intervals on the scale", loc="left", fontsize=15, color=fs.INK, pad=28)
    ax_p.text(0, 1.02, "consecutive weighings 4-16 h apart; dashed = equal", transform=ax_p.transAxes,
              fontsize=12, color=fs.MUTED)
    ax_p.legend(loc="upper left", fontsize=11, frameon=False)

    import matplotlib.dates as mdates
    from datetime import datetime as _dt
    for r in results:
        c = fs.POT_COLOURS[r["sensor"]]
        days = defaultdict(list)
        for t, g in r["resid"]:
            days[t.astimezone().date()].append(g)
        pts = sorted((k, math.sqrt(statistics.fmean(x * x for x in v))) for k, v in days.items() if len(v) >= 20)
        ax_b.plot([_dt.combine(k, _dt.min.time()) + timedelta(hours=12) for k, _ in pts], [v for _, v in pts],
                  color=c, lw=2.0, marker="o", ms=6, label=r["name"])
        for wt in r["waterings"]:
            ax_b.axvline(wt.astimezone().replace(tzinfo=None), color=c, lw=1.2, ls=(0, (4, 4)), alpha=0.8)
    ax_b.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d"))
    ax_b.xaxis.set_major_locator(mdates.DayLocator(interval=7))
    ax_b.set_ylabel("wobble that day, g rms", color=fs.MUTED, fontsize=12)
    ax_b.set_title("3.  Day by day", loc="left", fontsize=15, color=fs.INK, pad=28)
    ax_b.text(0, 1.02, "dashed = watering, in each pot's colour", transform=ax_b.transAxes,
              fontsize=12, color=fs.MUTED)
    ax_b.legend(loc="upper left", fontsize=12, frameon=False)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, facecolor=fs.SURFACE)
    print(args.out)


if __name__ == "__main__":
    main()
