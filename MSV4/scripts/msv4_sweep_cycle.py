"""A watering-to-watering cycle of cloud raw sweeps: which model fits where, temperature, drying.

Why: msv4_sweep_record.py scores the models over one day at one moisture. A full dry-down covers the
whole working range, so this splits the record at the waterings and reports, per phase and per
band of the probe's own Rsoil, every model's residual and parameters; then, over the cycle, each
reading's tip coefficient with the drying taken out by a 12 h piecewise-linear trend, its
sweep-to-sweep scatter and its wet-to-dry span; the receiver's stored number per degree; and the
drying rate day by day and by day and night (MSV4/docs/MSV4_SENSOR11_FIRST_CYCLE_2026-10-27.md).

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 290 --rs 3300 --cp 8.75 --cd0 13.71 \\
        --out data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-27.csv
    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 11 --raw \\
        --since-hours 290 --csv data/sensor_logs/msv4_sensor11_history_2026-10-27.csv
    python MSV4/scripts/msv4_sweep_cycle.py --fits data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-27.csv \\
        --history data/sensor_logs/msv4_sensor11_history_2026-10-27.csv \\
        --water "2026-10-18 13:31" "2026-10-27 12:31" --plot MSV4/docs/msv4_sensor11_cycle_2026-10-27.png
    python MSV4/scripts/msv4_sweep_cycle.py --self-test

Times are local. A watering time is any moment between the last dry sweep and the first wet one.
"""
from __future__ import annotations

import argparse
import sys
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as history
import msv4_sweep_record as record

BANDS = (0, 1800, 2500, 3500, 5000, 7000, 10000, 14000, 16000, 18000, 25000, 400000)
SHOWN = (("lumped_mask_pf", "lumpC"), ("lumped_cd_pf", "Cd"), ("cpe_mask_pf", "cpeC"), ("cpe_n", "n"),
         ("tline_r_ohm", "tlR"), ("tline_mask_pf", "tlC"))
READINGS = (("fw_r", "probe's Rsoil"),) + record.PARAMS
KNOT_H = 12.0          # the drying trend's knot spacing: long against a sweep, short against a day
SETTLE = timedelta(hours=1.5)  # skip the pour itself when fitting a cycle


def hat_basis(hours: np.ndarray, step: float) -> np.ndarray:
    knots = np.arange(0.0, hours.max() + step, step)
    return np.maximum(0.0, 1.0 - np.abs(hours[:, None] - knots[None, :]) / step)


def tip_coefficient(hours: np.ndarray, tip: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    """(per-degree coefficient, its standard error, residual rms) with a piecewise-linear trend."""
    x = np.column_stack([hat_basis(hours, KNOT_H), tip])
    x = x[:, x.any(axis=0)]
    coef, *_ = np.linalg.lstsq(x, y, rcond=None)
    resid = y - x @ coef
    cov = np.linalg.pinv(x.T @ x) * (resid @ resid) / (len(y) - x.shape[1])
    return float(coef[-1]), float(np.sqrt(cov[-1, -1])), float(resid.std())


def scatter(y: np.ndarray) -> float:
    d = np.diff(y)
    return float(1.4826 * np.median(np.abs(d - np.median(d))) / np.sqrt(2))


def phases(rows: list[dict], waters: list[datetime]) -> list[tuple[str, list[dict]]]:
    edges = [rows[0]["time"]] + waters + [rows[-1]["time"] + timedelta(seconds=1)]
    names = ["before " + f"{waters[0]:%m-%d}"] + [f"after {w:%m-%d}" for w in waters]
    return [(n, [r for r in rows if a <= r["time"] < b]) for n, a, b in zip(names, edges, edges[1:])]


def report(rows: list[dict], hist: list[dict], waters: list[datetime]) -> None:
    exp = (rows[-1]["wake"] - rows[0]["wake"]) // 10 + 1
    measured = [h for h in hist if rows[0]["time"] <= h["time"] <= rows[-1]["time"]]
    have = {r["wake"] for r in rows}
    lost = [h for h in measured if h["wake"] not in have]
    print(f"{len(rows)} sweeps {rows[0]['time']:%m-%d %H:%M} to {rows[-1]['time']:%m-%d %H:%M}; "
          f"{len(measured)} measured wakes in the history, {len(lost)} of them with no sweep stored "
          f"({100 * len(rows) / len(measured):.1f} % stored; {exp} expected from the wake span)")
    run, best = [], []
    for h in lost:
        run = run + [h] if run and h["wake"] - run[-1]["wake"] == 10 else [h]
        best = run if len(run) > len(best) else best
    if best:
        print(f"longest run of lost sweeps: {len(best)}, {best[0]['time']:%m-%d %H:%M}-{best[-1]['time']:%H:%M}")
    agree = [r["lumped_r_ohm"] / r["fw_r"] - 1 for r in rows if r.get("fw_r")]
    print(f"cloud refit lumped R vs the probe's Rsoil: median {100 * np.median(agree):+.3f} %, "
          f"worst {100 * max(map(abs, agree)):.2f} %")

    print("\nmedian residual (1e-4 of drive) and parameters by phase and by the probe's Rsoil band")
    for name, chunk in phases(rows, waters):
        print(f"=== {name}: {len(chunk)} sweeps")
        for lo, hi in zip(BANDS, BANDS[1:]):
            sel = [r for r in chunk if lo <= r["fw_r"] < hi]
            if len(sel) < 3:
                continue
            med = {m: 1e4 * np.median([r[f"{m}_rms"] for r in sel]) for m in record.MODELS}
            win = min(med, key=med.get)
            print(f"  {lo:>6}-{hi:<6} {len(sel):4} " + " ".join(f"{m[:6]}={med[m]:4.0f}" for m in record.MODELS)
                  + f"  best={win:10} " + " ".join(f"{s}={np.median([r[k] for r in sel]):.4g}" for k, s in SHOWN))

    start, end = waters[0] + SETTLE, waters[1] if len(waters) > 1 else rows[-1]["time"]
    cyc = [r for r in rows if start <= r["time"] < end]
    hours = np.array([(r["time"] - start).total_seconds() / 3600 for r in cyc])
    tip = np.array([r["tip"] for r in cyc])
    wet = [r for r in cyc if r["time"] < start + timedelta(hours=6)]
    dry = [r for r in cyc if r["time"] > end - timedelta(hours=12)]
    print(f"\ncycle {start:%m-%d %H:%M} to {end:%m-%d %H:%M}: {len(cyc)} sweeps, tip {tip.min():.2f}-{tip.max():.2f} C, "
          f"trend knots every {KNOT_H:g} h; wet = first 6 h, dry = last 12 h")
    print(f"  {'reading':24} {'wet':>10} {'dry':>10} {'span':>9} {'tip /C':>16} {'scatter':>8} "
          f"{'span/scatter':>12} {'span per C':>10}")
    for key, name in READINGS:
        linear = key in record.LINEAR
        y = np.array([r[key] for r in cyc])
        y = y if linear else 100 * np.log(y)
        c, se, _ = tip_coefficient(hours, tip, y)
        a, b = np.median([r[key] for r in wet]), np.median([r[key] for r in dry])
        span = b - a if linear else 100 * np.log(b / a)
        unit = "" if linear else " %"
        print(f"  {name:24} {a:10.4g} {b:10.4g} {span:+8.3g}{unit[:1] or ' '} {c:+8.3f}+/-{se:.3f}{unit[:1] or ' '} "
              f"{scatter(y):8.3f} {abs(span) / scatter(y):12.0f} {abs(span / c):10.0f}")

    hc = [h for h in hist if start <= h["time"] < end and h["rsoil"]]
    hh = np.array([(h["time"] - start).total_seconds() / 3600 for h in hc])
    ht = np.array([h["tip"] for h in hc])
    stored = np.array([h["stored"] for h in hc], float)
    lnr = 100 * np.log(np.array([h["rsoil"] for h in hc]))
    print("\nreceiver's stored number (10,000 per decade of Rsoil, corrected to 25 C at 2 %/C):")
    for label, mask in (("whole cycle", stored > 0), ("wet half (> 19,000)", stored > 19000),
                        ("dry half (<= 19,000)", stored <= 19000)):
        cs, ses, _ = tip_coefficient(hh[mask], ht[mask], stored[mask])
        cr, ser, _ = tip_coefficient(hh[mask], ht[mask], lnr[mask])
        print(f"  {label:22} stored {cs:+6.1f} +/- {ses:.1f} counts/C; Rsoil {cr:+.2f} +/- {ser:.2f} %/C")
    swing = {}
    for h in hc:
        swing.setdefault(h["time"].date(), []).append(h["tip"])
    print("  tip range per day: " + ", ".join(f"{d:%m-%d} {min(v):.2f}-{max(v):.2f}" for d, v in swing.items()))

    coef, _, _ = tip_coefficient(hh, ht, lnr)
    lnr22 = lnr - coef * (ht - 22.0)
    def near(when: datetime) -> float | None:
        sel = [v for h, v in zip(hc, lnr22) if abs((h["time"] - when).total_seconds()) <= 3600]
        return float(np.median(sel)) if sel else None
    print(f"\nRsoil at 22 C ({coef:+.2f} %/C), change per day 06:00-06:00 and its halves (06-18 day, 18-06 night)")
    day = start.replace(hour=6, minute=0, second=0, microsecond=0) + timedelta(days=1)
    while day + timedelta(days=1) <= end:
        a, m, b = near(day), near(day + timedelta(hours=12)), near(day + timedelta(days=1))
        if None not in (a, m, b):
            pct = lambda x, y: 100 * (np.exp((y - x) / 100) - 1)
            print(f"  {day:%m-%d}  day+night {pct(a, b):+6.1f} %   day {pct(a, m):+6.1f} %   night {pct(m, b):+6.1f} %")
        day += timedelta(days=1)


def plot(rows: list[dict], hist: list[dict], waters: list[datetime], out: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.ticker
    import matplotlib.pyplot as plt

    blue, orange, aqua = "#2a78d6", "#eb6834", "#1baf7a"
    ink, muted, grid = "#0b0b0b", "#52514e", "#e4e3df"
    plt.rcParams.update({"font.size": 9, "axes.edgecolor": muted, "axes.labelcolor": ink,
                         "xtick.color": muted, "ytick.color": muted, "axes.titlesize": 10})
    fig = plt.figure(figsize=(11, 8.2), facecolor="#fcfcfb")
    gs = fig.add_gridspec(2, 2, height_ratios=(1, 1.15), hspace=0.45, wspace=0.32)
    plain = matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}")
    ax1, ax2, ax3 = fig.add_subplot(gs[0, :]), fig.add_subplot(gs[1, 0]), fig.add_subplot(gs[1, 1])
    for ax in (ax1, ax2, ax3):
        ax.set_facecolor("#fcfcfb")
        ax.grid(True, color=grid, linewidth=0.6)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)

    t = [h["time"] for h in hist if h["rsoil"]]
    ax1.plot(t, [h["rsoil"] / 1000 for h in hist if h["rsoil"]], color=blue, linewidth=1.2)
    ax1.set_yscale("log")
    ax1.set_ylabel("probe's Rsoil, kohm (log)")
    ax1.set_title("Soil resistance, every measured wake", loc="left", color=ink)
    ax1.set_yticks([1, 2, 5, 10, 20])
    ax1.yaxis.set_major_formatter(plain)
    ax1.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    for w in waters:
        ax1.axvline(w, color=muted, linewidth=0.8, linestyle=":")
        ax1.annotate(f"watered {w:%m-%d %H:%M}", (w, 0.04), xycoords=("data", "axes fraction"),
                     xytext=(-4, 0), textcoords="offset points", color=muted, fontsize=8, ha="right")
    ax1.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d"))

    hours: dict[datetime, list[dict]] = {}
    for r in rows:
        hours.setdefault(r["time"].replace(minute=0, second=0, microsecond=0), []).append(r)
    ht = sorted(hours)
    for model, colour, label in (("lumped", aqua, "lumped (firmware)"), ("tline", orange, "RC line"),
                                 ("cpe", blue, "CPE")):
        v = [1e4 * np.median([r[f"{model}_rms"] for r in hours[h]]) for h in ht]
        ax2.plot(ht, v, color=colour, linewidth=1.4, label=label)
        ax2.annotate(label.split(" (")[0], (ht[-1], v[-1]), xytext=(4, 0), textcoords="offset points",
                     color=ink, fontsize=8, va="center")
    ax2.set_ylabel("fit residual, 1e-4 of drive (hourly median)")
    ax2.set_title("Which model fits best changes with moisture", loc="left", color=ink, pad=24)
    ax2.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d"))
    ax2.legend(loc="lower left", bbox_to_anchor=(0, 1.01), ncol=3, frameon=False, fontsize=8,
               borderaxespad=0, handlelength=1.5)
    ax2.set_xlim(ht[0], ht[-1] + timedelta(hours=36))
    for w in waters:
        ax2.axvline(w, color=muted, linewidth=0.8, linestyle=":")

    for (name, chunk), colour in zip(phases(rows, waters), (orange, blue, aqua)):
        if len(chunk) < 3:
            continue
        ax3.scatter([r["fw_r"] / 1000 for r in chunk], [r["lumped_mask_pf"] for r in chunk], s=9,
                    color=colour, alpha=0.55, linewidths=0, label=f"{name} ({len(chunk)} sweeps)")
    ax3.set_xscale("log")
    ax3.set_yscale("log")
    ax3.set_xlabel("probe's Rsoil, kohm (log)")
    ax3.set_ylabel("lumped mask C, pF (log)")
    ax3.set_title("Same resistance, different mask capacitance", loc="left", color=ink, pad=24)
    ax3.set_xticks([1, 2, 5, 10, 20])
    ax3.set_yticks([10, 20, 50, 100, 200, 300])
    for axis in (ax3.xaxis, ax3.yaxis):
        axis.set_major_formatter(plain)
        axis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    ax3.legend(loc="lower left", frameon=False, fontsize=8, markerscale=2, handletextpad=0.2)
    fig.savefig(out, dpi=140, bbox_inches="tight", facecolor=fig.get_facecolor())
    print(f"\nwrote {out}")


def self_test() -> int:
    """A known tip coefficient on a non-linear dry-down with a daily temperature swing must come back."""
    rng = np.random.default_rng(3)
    hours = np.arange(0, 9 * 24, 1 / 6)
    tip = 21 + 3 * np.sin(2 * np.pi * (hours - 9) / 24) + rng.normal(0, 0.1, hours.size)
    trend = 100 * np.log(1600) + 100 * np.log(12) * (1 - np.exp(-hours / 100)) / (1 - np.exp(-216 / 100))
    y = trend - 1.5 * (tip - 21) + rng.normal(0, 0.6, hours.size)
    c, se, _ = tip_coefficient(hours, tip, y)
    ok = abs(c + 1.5) < 0.1
    print(f"self-test: tip coefficient {c:+.3f} +/- {se:.3f} %/C, want -1.500: {'passed' if ok else 'FAILED'}")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--fits", type=Path, help="msv4_cloud_sweeps.py output")
    parser.add_argument("--history", type=Path, help="fetch_supabase_history.py --raw output, same sensor")
    parser.add_argument("--water", nargs="+", help="local watering times, 'YYYY-MM-DD HH:MM', oldest first")
    parser.add_argument("--plot", type=Path, help="write the cycle figure here")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not (args.fits and args.history and args.water):
        parser.error("need --fits, --history and --water")
    rows = record.join(args.fits, args.history)
    hist = history.load(args.history, None)
    waters = [datetime.fromisoformat(w).astimezone() for w in args.water]
    report(rows, hist, waters)
    if args.plot:
        plot(rows, [h for h in hist if h["time"] >= rows[0]["time"]], waters, args.plot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
