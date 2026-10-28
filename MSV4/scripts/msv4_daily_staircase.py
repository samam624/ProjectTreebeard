#!/usr/bin/env python3
"""Where each day's drop in an MSV4 probe's moisture happens: 11:00-15:00 or the rest of the day.

The moisture-vs-mass plot shows the probe's reading falling in steps once the pot has dried for a
few days. This splits every day of each dry-down into the drop over NOON (msv4_tempco_check) and
the drop over the other 20 hours, in grams, from the moisture number recomputed at the receiver's
2 %/C. Grams come from a line fitted through that cycle's weighings (scale regressed on the
number). The scale's own split, from weighing intervals alone, is printed beside it.

--plot draws the last cycle: the reading in grams with the noon hours shaded and the weighings on
top, the tip temperature under it, and the per-day split.

    python MSV4/scripts/msv4_daily_staircase.py data/sensor_logs/<sensor10 history>.csv [--plot out.png]
    python MSV4/scripts/msv4_daily_staircase.py --self-test
"""
from __future__ import annotations

import argparse
import statistics
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_tempco_check as tc

COEF = 0.02           # the receiver's correction
WINDOW_MIN = 20       # each end of a split is the median of the readings within this many minutes
DARK_NOON_RISE_C = 2.0  # a noon tip rise below this is a dark day (10-21 and 10-22 were 1.2 and 0.7 C)
BLUE, ORANGE, BAND = "#2a78d6", "#eb6834", "#f3e9d2"


def number(row: dict) -> float:
    return tc.number_at(row, COEF)


def median_near(rows: list[dict], t: datetime, value) -> float | None:
    v = [value(r) for r in rows if abs((r["time"] - t).total_seconds()) <= WINDOW_MIN * 60]
    return statistics.median(v) if len(v) >= 5 else None


def cycle_fit(pairs: list[dict], waterings: list[datetime], cycle: int) -> tuple[float, float]:
    """(grams per unit, unit offset) of the number in this cycle, scale regressed on the number."""
    pts = [(statistics.median(number(r) for r in p["rows"]), p["grams"]) for p in pairs
           if tc.cycle_of(p["time"], waterings) == cycle
           and tc.hours_since_watering(p["time"], waterings) >= tc.SKIP_AFTER_WATERING_H]
    x, y = np.array(pts).T
    slope, intercept = np.polyfit(x, y, 1)
    return float(slope), float(intercept)


def daily_split(rows: list[dict], start: datetime, end: datetime, grams_per_unit: float) -> list[dict]:
    """Per day from the first NOON start after start: grams lost in NOON and in the 20 h after it."""
    out = []
    day = start.replace(hour=tc.NOON[0], minute=0, second=0, microsecond=0)
    if day < start:
        day += timedelta(days=1)
    span = tc.NOON[1] - tc.NOON[0]
    while day + timedelta(hours=24) <= end:
        a = median_near(rows, day, number)
        b = median_near(rows, day + timedelta(hours=span), number)
        e = median_near(rows, day + timedelta(hours=24), number)
        t0 = median_near(rows, day, lambda r: r["tip"])
        t1 = median_near(rows, day + timedelta(hours=span), lambda r: r["tip"])
        if None not in (a, b, e, t0, t1):
            noon, rest = (a - b) * grams_per_unit, (b - e) * grams_per_unit
            out.append({"day": day, "noon": noon, "rest": rest, "tip_rise": t1 - t0,
                        "share": noon / (noon + rest) if noon + rest > 0 else float("nan")})
        day += timedelta(days=1)
    return out


def analyse(rows: list[dict], weighings: list[dict], waterings: list[datetime]) -> dict:
    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings)
             if not p["excluded"] and p["saucer"] is False and len(p["rows"]) >= comp.MIN_PAIR_READINGS]
    raw_iv = tc.intervals(pairs, waterings, lambda r: comp._log_ohm(r["rsoil"]))
    tz = rows[0]["time"].tzinfo
    out = {}
    for c, watered in enumerate(waterings, start=1):
        if c not in raw_iv:
            continue
        end = waterings[c] if c < len(waterings) else rows[-1]["time"]
        start = watered + timedelta(hours=tc.SKIP_AFTER_WATERING_H)
        gpu, offset = cycle_fit(pairs, waterings, c)
        iv = raw_iv[c][np.argsort(raw_iv[c][:, 3])]
        half = len(iv) // 2
        halves = []
        for part in (iv[:half], iv[half:]):
            other, noon = tc.noon_rates(part, tz)
            halves.append((datetime.fromtimestamp(part[0, 3], tz), datetime.fromtimestamp(part[-1, 4], tz), noon, other))
        out[c] = {"watered": watered, "start": start, "end": end, "gpu": gpu, "offset": offset,
                  "days": daily_split(rows, start, end, gpu), "scale_halves": halves,
                  "weighings": [p for p in pairs if tc.cycle_of(p["time"], waterings) == c]}
    return out


def report(result: dict) -> None:
    span = tc.NOON[1] - tc.NOON[0]
    for c, r in result.items():
        print(f"cycle {c} (watered {r['watered']:%m-%d %H:%M}); number at {COEF * 100:.0f} %/C, "
              f"{1 / r['gpu']:.1f} units per gram from its weighings")
        print(f"   scale only, {tc.NOON[0]:02d}:00-{tc.NOON[1]:02d}:00 against the rest:")
        for a, b, noon, other in r["scale_halves"]:
            share = span * noon / (span * noon + (24 - span) * other)
            print(f"     {a:%m-%d %H:%M} to {b:%m-%d %H:%M}: {noon:.2f} g/h inside, {other:.2f} g/h outside, {share:.0%} in those {span} h")
        print(f"   probe, per day:  day    {tc.NOON[0]:02d}-{tc.NOON[1]:02d} g   other {24 - span} h g   share   tip rise {tc.NOON[0]:02d}-{tc.NOON[1]:02d}")
        for d in r["days"]:
            dark = "   dark day" if d["tip_rise"] < DARK_NOON_RISE_C else ""
            print(f"                   {d['day']:%m-%d}   {d['noon']:7.1f}   {d['rest']:10.1f}   {d['share']:5.0%}   {d['tip_rise']:+6.2f} C{dark}")
        print()


def plot(rows: list[dict], result: dict, out: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    c = max(result)
    r = result[c]
    span = [x for x in rows if r["start"] <= x["time"] <= r["end"]]
    bins: dict[datetime, list[dict]] = {}
    for x in span:
        bins.setdefault(x["time"].replace(minute=(x["time"].minute // 15) * 15, second=0, microsecond=0), []).append(x)
    times = sorted(t for t in bins if len(bins[t]) >= 5)
    dry = min(p["grams"] for p in r["weighings"])
    water = [r["gpu"] * statistics.median(number(x) for x in bins[t]) + r["offset"] - dry for t in times]
    tip = [statistics.mean(x["tip"] for x in bins[t]) for t in times]

    fig = plt.figure(figsize=(11, 9.2))
    grid = fig.add_gridspec(3, 1, height_ratios=(3, 1.3, 1.7), hspace=0.3)
    ax1 = fig.add_subplot(grid[0])
    ax2 = fig.add_subplot(grid[1], sharex=ax1)
    ax3 = fig.add_subplot(grid[2])
    day = r["start"].replace(hour=0, minute=0, second=0, microsecond=0)
    while day <= r["end"]:
        for ax in (ax1, ax2):
            ax.axvspan(day + timedelta(hours=tc.NOON[0]), day + timedelta(hours=tc.NOON[1]), color=BAND, lw=0, zorder=0)
        day += timedelta(days=1)
    w = [p for p in r["weighings"] if p["time"] >= r["start"]]
    ax1.plot([p["time"] for p in w], [p["grams"] - dry for p in w], color="#9a9a9a", lw=1.2, ls="--", zorder=2,
             label="weighings joined by straight lines (what the plot draws)")
    ax1.plot(times, water, color=BLUE, lw=2, zorder=3, label="probe's moisture number, in grams")
    ax1.scatter([p["time"] for p in w], [p["grams"] - dry for p in w], s=46, color="black", zorder=4, label="weighing")
    ax1.set_ylabel(f"water, g above {dry:.0f} g")
    ax1.set_title(f"Sensor 10, cycle {c}: the drop comes in the shaded {tc.NOON[0]:02d}:00-{tc.NOON[1]:02d}:00 hours", loc="left", pad=10)
    ax1.legend(loc="upper right", frameon=False, fontsize=9)
    ax2.plot(times, tip, color=ORANGE, lw=2)
    ax2.set_ylabel("tip, °C")
    ax2.xaxis.set_major_locator(mdates.DayLocator(tz=rows[0]["time"].tzinfo))
    ax2.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=rows[0]["time"].tzinfo))
    plt.setp(ax1.get_xticklabels(), visible=False)
    for ax in (ax1, ax2):
        ax.grid(axis="y", color="#e6e6e6", lw=0.8)
        ax.spines[["top", "right"]].set_visible(False)
    days = r["days"]
    x = np.arange(len(days))
    ax3.bar(x - 0.2, [d["noon"] for d in days], width=0.38, color=BLUE, label=f"{tc.NOON[0]:02d}:00-{tc.NOON[1]:02d}:00 (4 h)")
    ax3.bar(x + 0.2, [d["rest"] for d in days], width=0.38, color=ORANGE, label="the other 20 h")
    for i, d in enumerate(days):
        tag = f"{d['share']:.0%}" + ("\ndark" if d["tip_rise"] < DARK_NOON_RISE_C else "")
        ax3.text(i, max(d["noon"], d["rest"]) + 1.5, tag, ha="center", va="bottom", fontsize=9, color="#444444")
    ax3.set_xticks(x, [f"{d['day']:%m-%d}" for d in days])
    ax3.set_ylabel("grams lost")
    ax3.set_ylim(0, max(max(d["noon"], d["rest"]) for d in days) * 1.35)
    ax3.set_title("Each day's drop by the probe, split at the noon hours (share in the noon hours above)", loc="left", fontsize=10)
    ax3.legend(loc="upper left", frameon=False, fontsize=9, ncol=2)
    ax3.spines[["top", "right"]].set_visible(False)
    fig.savefig(out, dpi=130, bbox_inches="tight")
    print(f"wrote {out}")


def self_test() -> None:
    rows, weighings, waterings = tc.synthetic(days=8, seed=2)
    result = analyse(rows, weighings, waterings)
    days = result[1]["days"]
    share = statistics.median(d["share"] for d in days)
    noon = statistics.median(d["noon"] for d in days)
    print(f"synthetic pot (5.0 g/h in 11-15, 0.9 g/h otherwise: 20 g and 18 g, 53 %): "
          f"median {noon:.1f} g in the noon hours, share {share:.0%} over {len(days)} days")
    assert 0.45 < share < 0.61 and 16 < noon < 24, "the split must recover the synthetic pot's noon drying"
    print("PASS daily staircase")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--plot", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.csv is None:
        parser.error("give a sensor history CSV or --self-test")
    rows = [r for r in reading.load(args.csv, None)
            if r["coupled"] is not None and not r["open"] and r["tip"] is not None]
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    result = analyse(rows, weighings, waterings)
    report(result)
    if args.plot:
        plot(rows, result, args.plot)


if __name__ == "__main__":
    main()
