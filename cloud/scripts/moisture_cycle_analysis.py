#!/usr/bin/env python
"""The Alocasia pot's waterings, 2026-08-09 to 2026-09-30, read from stored five-minute medians.

Prints four things and writes one figure:
  1. each watering as each sensor saw it: level before, peak, drained level, level before the next
  2. sensor 3: where its daily loss peaks and halves in each cycle, and the level about which the
     cycles scale (the base)
  3. sensor 3, leave one cycle out: its loss rate predicted from the other cycles at the same raw
     level, against the same after dividing by each cycle's own drained level
  4. the percent the current calibration showed around each watering, against a scale made from
     that cycle's drained level and a fraction fixed from the first two waterings

  python cloud/scripts/fetch_moisture_buckets.py --receiver MSV2088A3104A7AC --sensors 2 3 4 5 9
  python cloud/scripts/moisture_cycle_analysis.py --figure cloud/docs/MOISTURE_PERCENT_DRYDOWNS_2026-10-11.png

The watering times are the steps every in-pot sensor shows within one five-minute bucket. Only
2026-09-22 is in MSV3/fleet.json as a stated watering; the rest are read from the record.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

from autocal_replay import BUCKETS, PT, derive, load, pct, sql_rises

EVENTS = ["2026-08-09 01:15", "2026-08-16 18:05", "2026-08-24 12:35", "2026-08-29 20:20", "2026-09-07 13:20",
          "2026-09-14 18:40", "2026-09-22 10:50", "2026-09-30 11:20"]
END = "2026-09-30 18:00"
# clip drops reflash and open-circuit rows; start is when the sensor went into this pot (fleet.json)
SENSORS = {3: dict(clip=(700, 1600), start="2026-08-08 12:00"), 2: dict(clip=(140, 200), start="2026-07-27"),
           4: dict(clip=(40000, 60000), start="2026-09-18 21:00"), 5: dict(clip=(40000, 66000), start="2026-09-19 21:00"),
           9: dict(clip=(1000, 1800), start="2026-09-14 19:00")}
EV = [pd.Timestamp(e, tz=PT).tz_convert("UTC") for e in EVENTS]


def series(sensor: int, buckets: Path) -> pd.Series:
    cfg = SENSORS[sensor]
    b = load(buckets / f"moisture_5min_s{sensor}.csv")["moisture"].astype(float)
    b = b[(b.index >= pd.Timestamp(cfg["start"], tz=PT)) & (b.index <= pd.Timestamp(END, tz=PT))]
    return b[(b >= cfg["clip"][0]) & (b <= cfg["clip"][1])]


def med(b: pd.Series, t0, a: float, c: float) -> float:
    w = b[(b.index >= t0 + pd.Timedelta(hours=a)) & (b.index < t0 + pd.Timedelta(hours=c))]
    return float(np.median(w.values)) if len(w) >= 6 else np.nan


def anatomy(buckets: Path) -> None:
    rows = []
    for sensor in SENSORS:
        b = series(sensor, buckets)
        for i, t0 in enumerate(EV):
            if t0 < b.index[0] + pd.Timedelta(hours=2):
                continue
            nxt = EV[i + 1] if i + 1 < len(EV) else b.index[-1]
            pre, post = med(b, t0, -2, 0), b[(b.index >= t0) & (b.index <= t0 + pd.Timedelta(hours=3))]
            if not len(post) or np.isnan(pre):
                continue
            seg = b[(b.index > t0 + pd.Timedelta(hours=12)) & (b.index < nxt - pd.Timedelta(minutes=10))]
            rows.append(dict(sensor=sensor, watered=t0.tz_convert(PT).strftime("%m-%d %H:%M"), before=pre, max_3h=post.max(),
                             h3_6=med(b, t0, 3, 6), h6_12=med(b, t0, 6, 12), h12_24=med(b, t0, 12, 24),
                             h24_48=med(b, t0, 24, 48), before_next=float(np.median(seg.values[-12:])) if len(seg) > 12 else np.nan,
                             days=(nxt - t0).total_seconds() / 86400))
    d = pd.DataFrame(rows)
    d["peak_over_drained_%"] = 100 * (d["max_3h"] - d["h12_24"]) / (d["h12_24"] - d["before"])
    print("1. EACH WATERING (medians of five-minute medians; h12_24 is the drained level)")
    print(d.to_string(index=False, float_format=lambda x: "%.1f" % x))


def cycles_s3(buckets: Path) -> dict:
    """Sensor 3's dry-downs on an hourly grid: level, and loss per 24 h taken centred so the daily cycle cancels."""
    hourly = series(3, buckets).resample("1h").median()
    cyc = {}
    for i in range(len(EV) - 1):
        seg = hourly[(hourly.index >= EV[i] + pd.Timedelta(hours=12)) & (hourly.index <= EV[i + 1] - pd.Timedelta(hours=1))].interpolate(limit=4)
        plateau = float(seg[seg.index <= EV[i] + pd.Timedelta(hours=24)].median())
        if np.isnan(plateau):
            plateau = float(seg.dropna().iloc[:6].median())
        level = seg.rolling(24, center=True, min_periods=20).mean()
        rate = -(seg.shift(-12) - seg.shift(12))
        ok = level.notna() & rate.notna()
        cyc[EV[i].tz_convert(PT).strftime("%m-%d")] = dict(
            P=plateau, lvl=level[ok], rate=rate[ok].rolling(6, center=True, min_periods=1).median(), seg=seg,
            end=float(seg.dropna().iloc[-3:].median()), days=(EV[i + 1] - EV[i]).total_seconds() / 86400, t0=EV[i])
    return cyc


def landmarks(cyc: dict) -> None:
    rows = []
    for name, c in cyc.items():
        rate, level = c["rate"], c["lvl"]
        if len(rate) < 48:
            continue
        at_peak = rate.idxmax()
        after, before = rate[rate.index > at_peak], rate[rate.index < at_peak]
        low, high = after[after <= 0.5 * rate[at_peak]], before[before <= 0.5 * rate[at_peak]]
        rows.append(dict(cycle=name, drained=c["P"], peak_loss=rate[at_peak], level_at_peak=level[at_peak],
                         level_at_half=level[low.index[0]] if len(low) else np.nan,
                         level_half_rising=level[high.index[-1]] if len(high) else np.nan, before_next=c["end"], days=c["days"]))
    t = pd.DataFrame(rows)
    print("\n2. SENSOR 3: LOSS-RATE LANDMARKS (the 08-24 cycle has a -120 step on 08-28 with no watering in it; its peak is that step)")
    print(t.to_string(index=False, float_format=lambda x: "%.1f" % x))
    print("   a landmark that scales with the drained level about a base b lies on a line  level = b + u*(drained - b):")
    for column in ("level_at_peak", "level_at_half", "before_next"):
        d = t.dropna(subset=[column])
        for label, dd in (("all cycles", d), ("without 09-22", d[d.cycle != "09-22"])):
            slope, intercept = np.polyfit(dd.drained, dd[column], 1)
            resid = dd[column] - (intercept + slope * dd.drained)
            print(f"   {column:14s} {label:14s} n={len(dd)}  u={slope:.3f}  base b={intercept / (1 - slope):4.0f}  rms {np.sqrt((resid ** 2).mean()):.1f}")
    for b in (730, 830):
        print(f"   with b={b}: level at half loss u = " + " ".join("%.2f" % ((r.level_at_half - b) / (r.drained - b)) for r in t.itertuples())
              + "   watered at u = " + " ".join("%.2f" % ((r.before_next - b) / (r.drained - b)) for r in t.itertuples()))


def leave_one_out(cyc: dict) -> None:
    cyc = {k: dict(v) for k, v in cyc.items()}
    c = cyc["08-24"]                                    # keep only what precedes the 08-28 step
    cut = c["rate"].idxmax() - pd.Timedelta(hours=14)
    c["lvl"], c["rate"] = c["lvl"][c["lvl"].index < cut], c["rate"][c["rate"].index < cut]
    keys = list(cyc)

    def coords(b):
        return {k: ((cyc[k]["lvl"].values, cyc[k]["rate"].values) if b is None else
                    ((cyc[k]["lvl"].values - b) / (cyc[k]["P"] - b), cyc[k]["rate"].values / (cyc[k]["P"] - b))) for k in keys}

    def errors(b, width):
        table, out = coords(b), {}
        for k in keys:
            xk, vk = table[k]
            xs = np.concatenate([table[j][0] for j in keys if j != k])
            vs = np.concatenate([table[j][1] for j in keys if j != k])
            who = np.concatenate([[j] * len(table[j][0]) for j in keys if j != k])
            errs = []
            for x0, v0 in zip(xk[::6], vk[::6]):
                near = np.abs(xs - x0) <= width
                if near.sum() < 12 or len(set(who[near])) < 2 or v0 <= 0:
                    continue
                predicted = np.median([np.median(vs[near & (who == j)]) for j in set(who[near])])
                if predicted > 0:
                    errs.append(np.log(v0 / predicted))
            out[k] = (np.sqrt(np.mean(np.square(errs))) if errs else np.nan, len(errs), len(xk[::6]))
        return out

    print("\n3. SENSOR 3, ONE CYCLE HELD OUT: rms error of ln(loss per day), predicted from the other cycles (compared/points)")
    print("%-14s" % "" + "".join("%15s" % k for k in keys))
    for label, result in [("same raw level", errors(None, 15))] + [(f"same u, b={b}", errors(b, 0.04)) for b in (650, 700, 730, 750, 800, 830)]:
        print("%-14s" % label + "".join("%9.2f %2d/%2d" % result[k] for k in keys))
    print("   peak loss per day, raw and over (drained - 730): " + "  ".join(
        f"{k} {cyc[k]['rate'].max():.0f}/{cyc[k]['rate'].max() / (cyc[k]['P'] - 730):.3f}" for k in keys))


def backtest(buckets: Path) -> None:
    print("\n4. PERCENT SHOWN JUST BEFORE EACH WATERING AND A DAY AFTER IT")
    print("   'current' is the replayed live calibration. 'b=' is 100 % at that cycle's drained level (+6..12 h) and 0 % at")
    print("   b + u*(drained - b), u being the median of where the 08-24 and 08-29 waterings fell, never changed after.")
    for sensor, bases in ((3, (650, 730, 830)), (2, (100, 140, 155))):
        b = series(sensor, buckets)
        ev = EV[1:]
        pre, plat, day1 = [med(b, t, -2, 0) for t in ev], [med(b, t, 6, 12) for t in ev], [med(b, t, 22, 26) for t in ev]
        rows = []
        for i, t in enumerate(ev):
            row = dict(watered=t.tz_convert(PT).strftime("%m-%d"), level_before=pre[i], drained=plat[i])
            for tag, when, value in (("before", t - pd.Timedelta(minutes=10), pre[i]), ("day after", t + pd.Timedelta(hours=24), day1[i])):
                d = derive(sql_rises(b[b.index <= when], when), when)
                row[f"current {tag}"] = pct(value, d["dry"], d["wet"]) if not np.isnan(value) else np.nan
                if tag == "day after":
                    row["current range"] = f"{d['dry']}-{d['wet']}"
            for base in bases:
                if i == 0:
                    row[f"b={base} before"], row[f"b={base} day after"] = np.nan, 100.0 * (day1[i] - base) / (plat[i] - base)
                    continue
                u = float(np.median([(pre[j] - base) / (plat[j - 1] - base) for j in (1, 2)]))
                dry = base + u * (plat[i - 1] - base)
                row[f"b={base} before"] = min(100, 100 * (pre[i] - dry) / (plat[i - 1] - dry))
                if not np.isnan(plat[i]) and not np.isnan(day1[i]):
                    dry = base + u * (plat[i] - base)
                    row[f"b={base} day after"] = min(100, 100 * (day1[i] - dry) / (plat[i] - dry))
            rows.append(row)
        print(f"   sensor {sensor}")
        print(pd.DataFrame(rows).to_string(index=False, float_format=lambda x: "%.0f" % x))


def figure(cyc: dict, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(1, 3, figsize=(17, 5.2))
    for name, c in cyc.items():
        days = (c["seg"].index - c["t0"]).total_seconds() / 86400
        style = dict(lw=2.2, color="crimson") if name == "09-22" else dict(lw=1.1)
        ax[0].plot(days, c["seg"].values, label=name, **style)
        ax[1].plot(c["lvl"], c["rate"], label=name, **style)
        ax[2].plot(days, ((c["seg"] - 730) / (c["P"] - 730)).values, label=name, **style)
    ax[0].set(xlabel="days since watering", ylabel="stored reading", title="Sensor 3: each dry-down as stored")
    ax[1].set(xlabel="stored reading", ylabel="loss per 24 h", title="Loss rate against level")
    ax[2].set(xlabel="days since watering", ylabel="(reading - 730) / (drained level - 730)",
              title="The same dry-downs over their own drained level")
    for a in ax:
        a.grid(alpha=0.3)
        a.legend(title="watered", fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    print(f"\nfigure -> {path}")


def review_figure(buckets: Path, path: Path, base: float = 730.0) -> None:
    """Sensor 3's whole record with every watering numbered, for the maintainer to say which
    waterings were on time: the stored reading, and the same as a share of where the pot settled
    after each watering. Prints the table the figure is drawn from."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    surface, ink, ink2, muted, grid, axis, blue, wash = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7", "#2a78d6", "#cde2fb"
    plt.rcParams.update({"font.family": ["Segoe UI", "DejaVu Sans"], "font.size": 10})

    b = series(3, buckets)
    b = b[b.index >= EV[0]]
    local = b.tz_convert(PT)
    half_hour = local.resample("30min").median().dropna()
    pre = [med(b, t, -2, 0) for t in EV]
    settled = [med(b, t, 12, 24) for t in EV]
    hourly = cycles_s3(buckets)
    for i, t in enumerate(EV):                       # a gap (09-07) or the end of the record (09-30) in the 12-24 h window
        if np.isnan(settled[i]):
            name = t.tz_convert(PT).strftime("%m-%d")
            settled[i] = hourly[name]["P"] if name in hourly else med(b, t, 6, 12)
    share = pd.Series(np.nan, index=half_hour.index)
    for i, t in enumerate(EV):
        nxt = EV[i + 1] if i + 1 < len(EV) else b.index[-1] + pd.Timedelta(minutes=1)
        inside = (half_hour.index >= t) & (half_hour.index < nxt)
        share[inside] = (half_hour[inside] - base) / (settled[i] - base)
        share[np.abs(half_hour.index - t) <= pd.Timedelta(minutes=30)] = np.nan      # the half hour that straddles the step
    watered_at = [(pre[i] - base) / (settled[i - 1] - base) for i in range(1, len(EV))]

    fig, (top, bottom) = plt.subplots(2, 1, figsize=(15, 9.2), sharex=True, facecolor=surface,
                                      gridspec_kw=dict(height_ratios=[1.25, 1], hspace=0.16))
    for ax in (top, bottom):
        ax.set_facecolor(surface)
        ax.grid(True, axis="y", color=grid, lw=0.8)
        ax.set_axisbelow(True)
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.spines["bottom"].set_color(axis)
        ax.tick_params(colors=muted, length=0)
    top.plot(half_hour.index, half_hour.values, color=blue, lw=1.6)
    bottom.axhspan(min(watered_at[:-1]), max(watered_at[:-1]), color=wash, lw=0)
    bottom.plot(share.index, share.values, color=blue, lw=1.6)

    for i, t in enumerate(EV):
        when = t.tz_convert(PT)
        for ax in (top, bottom):
            ax.axvline(when, color=axis, lw=0.8, zorder=1)
        note = {0: "\nrecord starts", 6: "\nfertilizer", 7: "\nwatering?"}.get(i, "")
        top.annotate(f"{i}", (when, 1.0), xycoords=("data", "axes fraction"), xytext=(0, 30), textcoords="offset points",
                     ha="center", va="bottom", color=ink, fontsize=13, fontweight="bold")
        top.annotate(f"{when:%a %m-%d}{note}", (when, 1.0), xycoords=("data", "axes fraction"), xytext=(0, 28),
                     textcoords="offset points", ha="center", va="top", color=ink2, fontsize=9)
        if i == 0:
            continue
        top.plot([when], [pre[i]], "o", ms=8, mfc=blue, mec=surface, mew=2, zorder=5)
        top.annotate(f"{pre[i]:.0f}", (when, pre[i]), xytext=(-7, -4), textcoords="offset points", ha="right", va="top", color=ink, fontsize=10)
        bottom.plot([when], [watered_at[i - 1]], "o", ms=8, mfc=blue, mec=surface, mew=2, zorder=5)
        bottom.annotate(f"{watered_at[i - 1]:.2f}", (when, watered_at[i - 1]), xytext=(-7, -4), textcoords="offset points",
                        ha="right", va="top", color=ink, fontsize=10)

    step = pd.Timestamp("2026-08-28 12:00", tz=PT)
    top.annotate("A   fell 120 in an hour,\n     no watering", (step + pd.Timedelta(hours=1), 1012), xytext=(pd.Timestamp("2026-08-29 08:00", tz=PT), 1180),
                 ha="right", va="bottom", color=ink2, fontsize=9, arrowprops=dict(arrowstyle="-", color=muted, lw=0.8, shrinkA=2, shrinkB=3))
    bottom.annotate(f"where waterings 1 to 6 happened: {min(watered_at[:-1]):.2f} to {max(watered_at[:-1]):.2f}",
                    (half_hour.index[0], min(watered_at[:-1])), xytext=(6, -6), textcoords="offset points", va="top", color=ink2, fontsize=9)

    top.set_ylabel("stored reading", color=ink2)
    top.set_title("Reading as stored. Dots: the level just before each watering", loc="left", color=ink2, fontsize=10, pad=58)
    bottom.set_ylabel("share of settled level", color=ink2)
    bottom.set_title(f"The same, as a share of where the pot settled 12 to 24 h after each watering (1.0 = settled, measured above {base:.0f})",
                     loc="left", color=ink2, fontsize=10)
    bottom.set_ylim(0.15, 1.12)
    bottom.xaxis.set_major_locator(mdates.WeekdayLocator(byweekday=mdates.MO, tz=local.index.tz))
    bottom.xaxis.set_major_formatter(mdates.DateFormatter("Mon %m-%d", tz=local.index.tz))
    bottom.xaxis.set_minor_locator(mdates.DayLocator(tz=local.index.tz))
    bottom.tick_params(axis="x", which="minor", length=3, color=axis)
    fig.suptitle("Alocasia GP (sensor 3), 9 August to 30 September 2026: every watering on record", x=0.062, ha="left", color=ink,
                 fontsize=14, fontweight="bold", y=0.985)
    fig.subplots_adjust(left=0.062, right=0.985, top=0.86, bottom=0.05)
    fig.savefig(path, dpi=120, facecolor=surface)

    print("\nWATERINGS ON SENSOR 3 (the table the review figure is drawn from)")
    rows = []
    for i in range(1, len(EV)):
        when, seen = EV[i].tz_convert(PT), b[b.index <= EV[i] - pd.Timedelta(minutes=10)]
        d = derive(sql_rises(seen, seen.index[-1]), seen.index[-1])
        rows.append(dict(n=i, watered=when.strftime("%a %m-%d %H:%M"), days_since_last=(EV[i] - EV[i - 1]).total_seconds() / 86400,
                         reading_before=pre[i], share_of_settled=watered_at[i - 1], percent_shown_then=pct(pre[i], d["dry"], d["wet"]),
                         settled_after=settled[i]))
    print(pd.DataFrame(rows).to_string(index=False, float_format=lambda x: "%.2f" % x if abs(x) < 2 else "%.0f" % x))
    print(f"review figure -> {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--buckets", type=Path, default=BUCKETS)
    parser.add_argument("--figure", type=Path)
    parser.add_argument("--review-figure", type=Path, help="only draw sensor 3's record with the waterings numbered")
    args = parser.parse_args()
    pd.set_option("display.width", 260)
    if args.review_figure:
        review_figure(args.buckets, args.review_figure)
        return 0
    anatomy(args.buckets)
    cyc = cycles_s3(args.buckets)
    landmarks(cyc)
    leave_one_out(cyc)
    backtest(args.buckets)
    if args.figure:
        figure(cyc, args.figure)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
