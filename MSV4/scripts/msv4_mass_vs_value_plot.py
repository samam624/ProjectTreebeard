"""Pot mass over time against one sensor reading converted to grams by a watering's jump.

One watering calibrates the reading, nothing else does: the scale weighed the pot just before and
just after, so grams added = after - before, and the reading moved by (reading after - reading
before) over the same minutes. gain = reading change / grams added; the reading is then drawn as

    grams(t) = scale_before + (reading(t) - reading_before) / gain

on the scale's own axis, anchored at the pre-watering weighing. No line is fitted through the later
weighings, so any gap to the black curve is the calibration failing to hold, not a fit hiding it.

Every watering is calibrated on ITS OWN jump (gains differ between waterings: the first cycle sits
lower at the same weight, see moisture_cycle2_offset), and each cycle's curve is anchored at that
watering's own pre-watering weighing and drawn until the next watering. The scale's "after" weight
is the first weighing with NO water standing in the dish (a weight with standing dish water holds
water the sensor cannot see), so it can come hours after the watering; that is printed per watering.
Reading "after" is the median over the 30 min starting at that weighing.

Two figures are written beside the input CSV:
  msv4_mass_vs_moisture_number.png   moisture number in use (as built, 2 %/C corrected)
  msv4_mass_vs_cm.png                Cm raw and corrected to 25 C by the fitted tip coefficient

    python MSV4/scripts/msv4_mass_vs_value_plot.py data/sensor_logs/<sensor10 history>.csv [--watering N]
"""
from __future__ import annotations

import argparse
import statistics
from datetime import timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_tempcorr as tempcorr

CLEAN_MIN = 30
NL = chr(10)
AFTER_MAX_MIN = 360   # first dry-dish weighing may come hours after the water went in


def median_in(rows, value, start, end):
    v = [x for x in (value(r) for r in rows if start <= r["time"] < end) if x is not None]
    return (statistics.median(v), len(v)) if v else (None, 0)


def calibrate(rows, weighings, watering, value, after_max_min=CLEAN_MIN):
    """(gain in reading units per gram, scale_before, reading_before, scale_after, reading_after) or None."""
    dry = [w for w in weighings if w["saucer"] is False]
    before = [w for w in dry if watering - timedelta(minutes=CLEAN_MIN) <= w["time"] < watering]
    after = [w for w in dry if watering <= w["time"] <= watering + timedelta(minutes=after_max_min)]
    if not before or not after:
        return None
    b, a = before[-1], after[0]
    vb, nb = median_in(rows, value, b["time"] - timedelta(minutes=55), b["time"] + timedelta(minutes=5))
    va, na = median_in(rows, value, a["time"], a["time"] + timedelta(minutes=30))
    if vb is None or va is None or a["grams"] <= b["grams"]:
        return None
    return (va - vb) / (a["grams"] - b["grams"]), b["grams"], vb, a["grams"], va, a["time"]


def figure(rows, weighings, waterings, series, title, out, note):
    fig, ax = plt.subplots(figsize=(14, 7))
    for i, w in enumerate(waterings):
        ax.axvline(w, color="#4a90d9", lw=1.2, ls="--", alpha=0.7, label="watering" if i == 0 else None)
    for label, line, color in series:
        ax.plot([t for t, _ in line], [g for _, g in line], color=color, lw=1.3, alpha=0.9, label=label)
    ax.plot([w["time"] for w in weighings], [w["grams"] for w in weighings], color="black", lw=2.4, zorder=5,
            label="pot weight (scale, pot + dish)")
    for w in weighings:
        if w["saucer"] is False:
            ax.errorbar(w["time"], w["grams"], yerr=max(w["spread"], 1) / 2, fmt="o", color="black", ms=5, zorder=6)
        else:
            ax.plot(w["time"], w["grams"], "o" if w["saucer"] else "x", mfc="white", color="black", ms=8, zorder=6)
    ax.set_ylabel("grams on the scale (hollow = water standing in the dish)")
    ax.set_title(title + f"\n{rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}")
    ax.text(0.01, 0.01, note, transform=ax.transAxes, fontsize=8, va="bottom",
            bbox=dict(boxstyle="round", fc="white", ec="0.7", alpha=0.9))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=rows[0]["time"].tzinfo))
    ax.grid(alpha=0.25)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.1), ncol=4, fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    if not rows:
        raise SystemExit("no MSV4 measurements with an air value in that file")
    sensor = rows[0]["sensor"]
    weighings, waterings = comp.load_pot(sensor)
    merged = comp.merge_repeats(weighings, waterings)
    out_dir = args.out_dir or args.csv.parent

    number = lambda r: r["stored"] if not r["open"] else None
    cm = lambda r: r["cm"] if not r["open"] else None
    days = comp.dry_days(rows, waterings)
    k_cm = tempcorr.fit_k(days[0], cm)[0] if days else None
    cm_corrected = lambda r: (r["cm"] - k_cm * (r["tip"] - tempcorr.REF_C)) if (not r["open"] and r["tip"] is not None) else None

    t0 = rows[0]["time"]
    hours = {}

    def segments(value, unit):
        """One (start, end, line) per watering, each with its own gain; also the notes."""
        if id(value) not in hours:
            hours[id(value)] = [(t0 + timedelta(hours=h + 0.5), m) for h, m, _ in comp.hourly(rows, value)]
        series_h = hours[id(value)]
        lines, notes = [], []
        for n, w in enumerate(waterings):
            cal = calibrate(rows, merged, w, value, AFTER_MAX_MIN)
            if cal is None:
                continue
            gain, sb, vb, sa, va, ta = cal
            end = waterings[n + 1] if n + 1 < len(waterings) else rows[-1]["time"] + timedelta(hours=1)
            start = rows[0]["time"] if n == 0 else w
            lines.append([(t, sb + (m - vb) / gain) for t, m in series_h if start <= t < end])
            notes.append(f"{w:%m-%d %H:%M} watering: scale {sb:.0f} -> {sa:.0f} g (+{sa - sb:.0f}, weighed {ta:%H:%M}), "
                         f"reading {vb:,.1f} -> {va:,.1f} {unit} = {gain:.3g} {unit}/g")
        return lines, notes

    def draw(name, specs, title, file, unit):
        series, all_notes = [], []
        for label, value, color in specs:
            lines, notes = segments(value, unit)
            for j, line in enumerate(lines):
                series.append((label if j == 0 else None, line, color))
            all_notes += [f"[{label.split(',')[0]}] {x}" for x in notes] if len(specs) > 1 else notes
        for x in all_notes:
            print(name, x)
        figure(rows, merged, waterings, series, title, out_dir / file,
                "each watering calibrated on its own jump" + NL + NL.join(all_notes))

    draw("number", [("moisture number in use, as grams", number, "#1f77b4")],
         f"Sensor {sensor}: pot weight and the moisture number, each watering converted to grams by its own jump",
         "msv4_mass_vs_moisture_number.png", "units")
    specs = [("Cm raw, as grams", cm, "#1f77b4")]
    if k_cm is not None:
        specs.append((f"Cm corrected to {tempcorr.REF_C:g} C ({k_cm:+.3f} pF/C), as grams", cm_corrected, "#d95f02"))
    draw("Cm", specs[:1], f"Sensor {sensor}: pot weight and Cm, each watering converted to grams by its own jump",
         "msv4_mass_vs_cm.png", "pF")


if __name__ == "__main__":
    main()
