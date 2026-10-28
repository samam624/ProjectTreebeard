"""Pot mass over time against one sensor reading over time, in the reading's OWN units.

msv4_moisture_plot.py converts every candidate to grams of water through a fitted line, which is
the right view for scoring but hides the raw shape. This puts the scale on the left axis (one
black curve through the weighings) and the reading on the right axis, unconverted, so the two
shapes can be compared by eye. Two figures are written beside the input CSV:

  msv4_mass_vs_moisture_number.png   the moisture number in use (what the receiver stores)
  msv4_mass_vs_cm.png                contact capacitance Cm in pF, raw hourly median, with the
                                     temperature-corrected Cm (fitted tip coefficient) as a second line

    python MSV4/scripts/msv4_mass_vs_value_plot.py data/sensor_logs/<sensor10 history>.csv [--out-dir DIR]
"""
from __future__ import annotations

import argparse
from datetime import timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_tempcorr as tempcorr

BLACK, BLUE, ORANGE = "black", "#1f77b4", "#d95f02"


def hourly_line(rows, value):
    t0 = rows[0]["time"]
    return [(t0 + timedelta(hours=h + 0.5), m) for h, m, _ in comp.hourly(rows, value)]


def figure(rows, weighings, waterings, series, ylabel, title, out):
    fig, ax = plt.subplots(figsize=(14, 7))
    for i, w in enumerate(waterings):
        ax.axvline(w, color="#4a90d9", lw=1.2, ls="--", alpha=0.7, label="watering" if i == 0 else None)
    ax.plot([w["time"] for w in weighings], [w["grams"] for w in weighings], color=BLACK, lw=2.4, zorder=5,
            label="pot weight (scale, pot + dish)")
    for w in weighings:
        if w["saucer"] is False:
            ax.errorbar(w["time"], w["grams"], yerr=max(w["spread"], 1) / 2, fmt="o", color=BLACK, ms=5, zorder=6)
        else:
            ax.plot(w["time"], w["grams"], "o" if w["saucer"] else "x", mfc="white", color=BLACK, ms=8, zorder=6)
    ax.set_ylabel("scale reading, g (hollow = water standing in the dish)")
    ax.grid(alpha=0.25)
    right = ax.twinx()
    for (label, line, color, lw) in series:
        right.plot([t for t, _ in line], [v for _, v in line], color=color, lw=lw, alpha=0.9, label=label)
    right.set_ylabel(ylabel)
    ax.set_title(title + f"\n{rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}   (dashed blue = watering)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=rows[0]["time"].tzinfo))
    h1, l1 = ax.get_legend_handles_labels()
    h2, l2 = right.get_legend_handles_labels()
    ax.legend(h1 + h2, l1 + l2, loc="upper center", bbox_to_anchor=(0.5, -0.1), ncol=4, fontsize=8, frameon=False)
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
    figure(rows, merged, waterings, [("moisture number in use (hourly median)", hourly_line(rows, number), BLUE, 1.3)],
           "moisture number (stored, 10,000 per decade of Rsoil; higher = wetter)",
           f"Sensor {sensor}: pot weight and moisture number over time", out_dir / "msv4_mass_vs_moisture_number.png")

    days = comp.dry_days(rows, waterings)
    series = [("Cm raw (hourly median)", hourly_line(rows, cm), BLUE, 1.3)]
    if days:
        k, se, _ = tempcorr.fit_k(days[0], cm)
        corrected = lambda r: (r["cm"] - k * (r["tip"] - tempcorr.REF_C)) if (not r["open"] and r["tip"] is not None) else None
        series.append((f"Cm corrected to {tempcorr.REF_C:g} C ({k:+.3f} pF/C fitted on {days[0][0]['time']:%m-%d})",
                       hourly_line(rows, corrected), ORANGE, 1.3))
    figure(rows, merged, waterings, series, "contact capacitance Cm, pF",
           f"Sensor {sensor}: pot weight and Cm over time", out_dir / "msv4_mass_vs_cm.png")


if __name__ == "__main__":
    main()
