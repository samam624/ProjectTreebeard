#!/usr/bin/env python3
"""What the multi-probe pot test data show on their own, without a mechanism model.

Three measurements, each chosen to need as few assumptions as possible:

1. Sun-arrival response. The sun reaches the shelf at ~11:50-12:10 PDT (probe 4's
   in-sun die jumps 3-5 C above probe 6's desk die within minutes). Every day is
   aligned on that instant, the 3 h pre-arrival trend is removed, and the days are
   averaged. No temperature model is involved.
2. Room vs sun. The de-trended daily variation of each rig is regressed on the desk
   die (room air, probe 6) and on the in-sun excess (probe 4 die minus probe 6 die),
   each through a first-order lag, and the share of variance each explains is reported.
3. Probe 7's watering step with the sun and room terms modelled out, checked against
   placebo steps placed at 10:50 on days with no watering.

Writes MSV3/docs/MSV3_MULTIPROBE_DAILY_SHAPES_2026-09-24.png and
MSV3/docs/MSV3_MULTIPROBE_SUN_RESPONSE_2026-09-24.png. Findings are written up in
MSV3/docs/MSV3_MULTIPROBE_DATA_FINDINGS_2026-09-24.md.

    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor N --raw \\
        --since-hours 300 --csv <dir>/treebeard_alocasia_sN_2026-09-24-late.csv   # N = 2 3 4 5 6 7 9
    python MSV3/scripts/analyze_multiprobe_sun_response.py <dir>

Per-day readout for one probe, split at a physical change (the probe 7 flip test,
MSV3/docs/MSV3_PROBE7_FLIP_TEST_2026-09-24.md):

    python MSV3/scripts/analyze_multiprobe_sun_response.py <dir> --suffix <s> --no-figures \\
        --per-day 7 --split "2026-09-25 14:30" --settle-hours 24
"""

from __future__ import annotations

import argparse
import itertools
import statistics as stats
from datetime import date, datetime, timedelta
from pathlib import Path

import numpy as np

import analyze_alocasia_watering as W

P = W.PACIFIC
SENSORS = (9, 7, 5, 4, 3, 2, 6)
NAMES = {9: "s9 blade + AM", 7: "p7 blade + FDC, Begonia", 5: "p5 blade + FDC", 4: "p4 v1.2 + FDC",
         3: "s3 v1.2 + AM", 2: "s2 MSV2 RC", 6: "p6 desk, no soil"}
UNITS = {9: "mV", 3: "mV", 2: "LSB", 4: "logC%", 5: "logC%", 7: "logC%", 6: "logC%"}
FIRST_DAY = {4: date(2026, 9, 19), 5: date(2026, 9, 21)}   # first full day in the pot (fleet.json)
WATERING_DAY = W.WATERING.date()
TAUS = (0, 1, 2, 3, 4, 6)
DOCS = Path(__file__).resolve().parents[1] / "docs"


def at(day, hour=0, minute=0):
    """A September 2026 day number, or a date, at a PDT clock time."""
    if isinstance(day, date):
        return datetime(day.year, day.month, day.day, hour, minute, tzinfo=P)
    return datetime(2026, 9, day, hour, minute, tzinfo=P)


def load(directory, suffix):
    rows = {s: W.median_bins(directory / f"treebeard_alocasia_s{s}_{suffix}.csv", s) for s in SENSORS}
    sig = {}
    for s, data in rows.items():
        pre = [v.get("raw_fdc_counts") for t, v in sorted(data.items())
               if W.WATERING - timedelta(hours=3) <= t < W.WATERING and v.get("raw_fdc_counts")]
        sig[s] = W.series(data, s, stats.median(pre) if pre else 1.0)
    return rows, sig


def die(rows, s):
    return {t: v["board_temp_c"] for t, v in rows[s].items() if "board_temp_c" in v}


def lowpass(tsec, x, tau_h):
    if tau_h == 0:
        return x.copy()
    y = np.empty_like(x)
    y[0] = x[0]
    for i in range(1, len(x)):
        y[i] = y[i - 1] + (1 - np.exp(-(tsec[i] - tsec[i - 1]) / 3600 / tau_h)) * (x[i] - y[i - 1])
    return y


class Drivers:
    """Room air (desk die) and in-sun excess (probe 4 die minus desk die), pre-filtered per lag."""

    def __init__(self, rows):
        t4, t6 = die(rows, 4), die(rows, 6)
        self.times = sorted(set(t4) & set(t6))
        self.tsec = np.array([t.timestamp() for t in self.times])
        room = np.array([t6[t] for t in self.times])
        sun = np.array([t4[t] - t6[t] for t in self.times])
        self.room = {tau: lowpass(self.tsec, room, tau) for tau in TAUS}
        self.sun = {tau: lowpass(self.tsec, sun, tau) for tau in TAUS}
        self.t4, self.t6 = t4, t6

    def sample(self, points, start, end):
        pts = [(t, v) for t, v in points if start <= t < end and self.times[0] <= t <= self.times[-1]]
        ts = np.array([t.timestamp() for t, _ in pts])
        return ts, np.array([v for _, v in pts]), np.clip(np.searchsorted(self.tsec, ts), 0, len(self.tsec) - 1)

    def arrivals(self, until=None):
        """Sun arrival per date: first bin 10:00-16:00 where the in-sun die leads the desk die by 1.5 C."""
        out = {}
        for day in sorted({t.date() for t in self.times}):
            if until and day >= until:
                continue
            for t in self.times:
                if at(day, 10) <= t < at(day, 16) and self.t4[t] - self.t6[t] > 1.5:
                    out[day] = t
                    break
        return out


def watering_step(points):
    return W.median_in(points, W.PREDAWN_AFTER) - W.median_in(points, W.PREDAWN_BEFORE)


GRID = np.arange(-120, 481, 10)


def aligned(lookup, t0):
    """One day's response on GRID minutes around sun arrival t0, minus its 3 h pre-arrival linear trend."""
    pre = [(t, v) for t, v in lookup.items() if t0 - timedelta(hours=3) <= t < t0]
    if len(pre) < 10:
        return None
    slope, icpt = np.polyfit([(t - t0).total_seconds() / 60 for t, _ in pre], [v for _, v in pre], 1)
    row = []
    for minutes in GRID:
        t = t0 + timedelta(minutes=int(minutes))
        row.append(lookup.get(t.replace(minute=t.minute // 10 * 10, second=0), np.nan) - (icpt + slope * minutes))
    return np.array(row)


def sun_response(sig, drivers, until=None):
    arrivals = {d: t for d, t in drivers.arrivals(until).items() if d != WATERING_DAY}
    curves = {}
    for s in SENSORS:
        lookup = dict(sig[s])
        rows = [aligned(lookup, t0) for day, t0 in arrivals.items() if day >= FIRST_DAY.get(s, date.min)]
        curves[s] = np.array([r for r in rows if r is not None])
    return arrivals, GRID, curves


def per_day(sig, rows, drivers, s, split, settle_hours):
    """One probe, one line per sunny day: sun response normalised by that day's sun drive."""
    lookup = dict(sig[s])
    own = die(rows, s)
    window = (GRID >= 90) & (GRID <= 150)
    print(f"PER-DAY SUN RESPONSE, {NAMES.get(s, s)}  (response = mean of +1.5..+2.5 h after arrival, pre-arrival trend removed)")
    print(f"   sun drive = mean (p4 die - desk die) over the same window.  split: {split or 'none'}")
    print(f"   {'date':10} {'arrival':>7} {'response':>9} {'sun drive':>9} {'per C sun':>9} {'own die range':>13} {'daily span':>10}  period")
    for day, t0 in drivers.arrivals().items():
        if day == WATERING_DAY or day < FIRST_DAY.get(s, date.min):
            continue
        if split and split <= t0 < split + timedelta(hours=settle_hours):
            print(f"   {day}  {t0:%H:%M}   -- settling after the change, excluded --")
            continue
        row = aligned(lookup, t0)
        if row is None:
            continue
        response = float(np.nanmean(row[window]))
        drive = float(np.nanmean([drivers.t4[t] - drivers.t6[t] for t in drivers.times
                                  if t0 + timedelta(minutes=90) <= t <= t0 + timedelta(minutes=150)]))
        temps = [v for t, v in own.items() if at(day) <= t < at(day) + timedelta(days=1)]
        pts = W.window(sig[s], at(day), at(day) + timedelta(days=1))
        span = W.diurnal_span([v for _, v in pts], [t.hour + t.minute / 60 for t, _ in pts]) if len(pts) >= 40 else float("nan")
        period = "" if not split else ("AFTER" if t0 >= split else "before")
        print(f"   {day}  {t0:%H:%M} {response:+9.2f} {drive:9.2f} {response / drive:+9.2f} "
              f"{(max(temps) - min(temps)) if temps else float('nan'):11.2f} C {span:10.2f}  {period}")


def room_vs_sun(sig, drivers, s, start, end, post):
    ts, y, idx = drivers.sample(sig[s], start, end)
    h = (ts - ts[0]) / 3600
    best = None
    for decay in ((None,) if not post else (12, 24, 36, 48, 72, 120, 250)):
        base = [np.ones_like(h), h, h ** 2] if decay is None else [np.ones_like(h), np.exp(-h / decay), h]
        b0 = np.column_stack(base)
        resid0 = y - b0 @ np.linalg.lstsq(b0, y, rcond=None)[0]
        ss0 = float(resid0 @ resid0)
        for tr, tx in itertools.product(TAUS, TAUS):
            r, x = drivers.room[tr][idx], drivers.sun[tx][idx]
            m = np.column_stack(base + [r, x])
            coef = np.linalg.lstsq(m, y, rcond=None)[0]
            sse = float(((y - m @ coef) ** 2).sum())
            if best is None or sse < best[0]:
                share = []
                for only in (r, x):
                    mo = np.column_stack(base + [only])
                    share.append(1 - float(((y - mo @ np.linalg.lstsq(mo, y, rcond=None)[0]) ** 2).sum()) / ss0)
                best = (sse, 1 - sse / ss0, share[0], share[1], coef[-2], tr, coef[-1], tx)
    return best[1:]


def step_with_drivers(sig, drivers, s, start, end, when):
    ts, y, idx = drivers.sample(sig[s], start, end)
    w = when.timestamp()
    after = (ts >= w).astype(float)
    since = np.where(after > 0, (ts - w) / 3600, 0)
    best = None
    for tr, tx, decay in itertools.product(TAUS, TAUS, (24, 48, 96, None)):
        relax = after * since if decay is None else after * (1 - np.exp(-since / decay))
        m = np.column_stack([np.ones_like(ts), (ts - ts[0]) / 3600, drivers.room[tr][idx], drivers.sun[tx][idx], after, relax])
        coef = np.linalg.lstsq(m, y, rcond=None)[0]
        sse = float(((y - m @ coef) ** 2).sum())
        if best is None or sse < best[0]:
            best = (sse, coef[4], np.sqrt(sse / len(y)))
    return best[1], best[2]


def figures(sig, drivers, grid, curves):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(2, 4, figsize=(18, 8))
    ax = ax.ravel()
    colours = {17: "#999", 18: "#bbb", 19: "#777", 20: "#c9a", 21: "#a58", 23: "#1f77b4", 24: "#2ca02c"}
    for i, s in enumerate(SENSORS):
        for day, colour in colours.items():
            if at(day).date() < FIRST_DAY.get(s, date.min):
                continue
            pts = W.window(sig[s], at(day), at(day + 1))
            if len(pts) < 100:
                continue
            ax[i].plot([(t - at(day)).total_seconds() / 3600 for t, _ in pts], [v - pts[0][1] for _, v in pts],
                       color=colour, lw=1.4 if day > 22 else 0.9, label=f"09-{day}")
        ax[i].set_title(f"{NAMES[s]} ({UNITS[s]}, wetter up)", fontsize=9)
        ax[i].axhline(0, color="k", lw=0.5)
        ax[i].set_xticks(range(0, 25, 3))
    ax[0].legend(fontsize=7)
    for day, colour in colours.items():
        for temps, style in ((drivers.t4, "-"), (drivers.t6, ":")):
            pts = sorted((t, v) for t, v in temps.items() if at(day) <= t < at(day + 1))
            ax[7].plot([(t - at(day)).total_seconds() / 3600 for t, _ in pts], [v for _, v in pts], color=colour, ls=style, lw=0.9)
    ax[7].set_title("p4 die, in sun (solid); p6 die, desk (dotted), C", fontsize=9)
    ax[7].set_xticks(range(0, 25, 3))
    fig.suptitle("Each day, relative to its own midnight value, by hour of day (PDT). Grey = before watering, colour = after.")
    fig.tight_layout()
    fig.savefig(DOCS / "MSV3_MULTIPROBE_DAILY_SHAPES_2026-09-24.png", dpi=70)

    fig, ax = plt.subplots(2, 4, figsize=(18, 7))
    ax = ax.ravel()
    for i, s in enumerate(SENSORS):
        for row in curves[s]:
            ax[i].plot(grid / 60, row, color="0.65", lw=0.7)
        ax[i].plot(grid / 60, np.nanmean(curves[s], axis=0), "k", lw=2)
        ax[i].axvline(0, color="r")
        ax[i].set_title(f"{NAMES[s]} ({UNITS[s]}), n={len(curves[s])} days", fontsize=9)
    for temps, label in ((drivers.t4, "p4 die, in sun"), (drivers.t6, "p6 die, desk")):
        rows = []
        for day, t0 in drivers.arrivals(date(2026, 9, 25)).items():
            if day == WATERING_DAY:
                continue
            base = np.nanmean([temps.get(t0 - timedelta(minutes=k), np.nan) for k in (10, 20, 30)])
            rows.append([temps.get((t0 + timedelta(minutes=int(m))).replace(second=0), np.nan) - base for m in grid])
        ax[7].plot(grid / 60, np.nanmean(np.array(rows), axis=0), label=label)
    ax[7].legend()
    ax[7].axvline(0, color="r")
    ax[7].set_title("temperature change, C", fontsize=9)
    fig.suptitle("Response to the sun reaching the shelf (red line), pre-arrival trend removed; hours after arrival")
    fig.tight_layout()
    fig.savefig(DOCS / "MSV3_MULTIPROBE_SUN_RESPONSE_2026-09-24.png", dpi=70)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--suffix", default="2026-09-24-late")
    parser.add_argument("--no-figures", action="store_true")
    parser.add_argument("--until", default="2026-09-25",
                        help="last day (exclusive) for sections 1-3; default is the 2026-09-24 findings window")
    parser.add_argument("--per-day", type=int, metavar="SENSOR", help="only print the per-day readout for this sensor")
    parser.add_argument("--split", help='physical change time, PDT, e.g. "2026-09-25 14:30"')
    parser.add_argument("--settle-hours", type=float, default=24, help="exclude sun arrivals this soon after --split")
    args = parser.parse_args()
    rows, sig = load(args.directory, args.suffix)
    drivers = Drivers(rows)

    if args.per_day is not None:
        split = datetime.fromisoformat(args.split).replace(tzinfo=P) if args.split else None
        per_day(sig, rows, drivers, args.per_day, split, args.settle_hours)
        return 0

    arrivals, grid, curves = sun_response(sig, drivers, date.fromisoformat(args.until))
    print("1. RESPONSE TO THE SUN REACHING THE SHELF")
    print("   arrival (p4 die > desk die + 1.5 C): " + ", ".join(f"{d:%m-%d} {t:%H:%M}" for d, t in arrivals.items()))
    print(f"   {'rig':26} {'+30 min':>9} {'+2 h':>9} {'+4 h':>9} {'+6 h':>9}   {'watering step':>13}  {'+2 h as % of step':>17}")
    for s in SENSORS:
        mean = np.nanmean(curves[s], axis=0)
        pick = [mean[np.searchsorted(grid, m)] for m in (30, 120, 240, 360)]
        step = watering_step(sig[s])
        share = "" if s in (6, 7) else f"{100 * pick[1] / step:+16.1f}%"
        print(f"   {NAMES[s]:26} {pick[0]:+9.2f} {pick[1]:+9.2f} {pick[2]:+9.2f} {pick[3]:+9.2f}   {step:+13.2f}  {share}")

    print("\n2. WHAT DRIVES THE DAILY VARIATION: share of de-trended variance explained")
    print("   room = desk die (p6); sun = in-sun excess (p4 die - p6 die); coefficients per C, wetter-positive")
    for label, start, end, post in (("before watering, 09-17 .. 09-22 10:00", at(17), at(22, 10), False),
                                    ("after watering, 09-23 .. 09-25", at(23), at(25), True)):
        print(f"   {label}")
        for s in SENSORS:
            first = at(FIRST_DAY[s]) if s in FIRST_DAY else start
            both, room_only, sun_only, k_room, t_room, k_sun, t_sun = room_vs_sun(sig, drivers, s, max(start, first), end, post)
            print(f"     {NAMES[s]:26} both {both:4.2f}  room {room_only:4.2f}  sun {sun_only:4.2f}   "
                  f"k_room {k_room:+8.3f} (lag {t_room} h)  k_sun {k_sun:+8.3f} (lag {t_sun} h) {UNITS[s]}/C")

    print("\n3. PROBE 7'S WATERING STEP WITH SUN AND ROOM TERMS MODELLED OUT")
    step, rms = step_with_drivers(sig, drivers, 7, at(17), at(25), W.WATERING)
    print(f"   real watering 09-22 10:50: step {step:+6.2f} logC%, fit rms {rms:.3f}")
    for day in (18, 19, 20, 21, 23, 24):
        step, rms = step_with_drivers(sig, drivers, 7, at(17), at(25), at(day, 10, 50))
        print(f"   placebo 09-{day} 10:50:      step {step:+6.2f} logC%, fit rms {rms:.3f}")

    if not args.no_figures:
        figures(sig, drivers, grid, curves)
        print(f"\nfigures written to {DOCS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
