#!/usr/bin/env python
"""Replay a "water now" region over a sensor's stored record, seeing only the past at each moment.

The rule (a reference for a build; nothing here is in the receiver or the cloud):

  1. A watering is a step up of 15 % of the scale between the last hour and the hours before it.
  2. The level the pot SETTLES at, the median 12-24 h after the step, is that cycle's scale:
         share = (reading - base) / (settled - base)
  3. "Water now" is on while the share is at or below `--top`, and it is "overdue" below
     `--bottom`. The first 24 h after a watering are "just watered".
  4. A settled level more than 10 % of the scale ABOVE the one in force is either a change of
     scale (ions: a fertilizing) or a shifted probe. Until the pot has dried for a day the region
     is moved up by the whole step, which cannot hide dryness. Then the first day's loss is put
     over both scales and the one nearer the pot's own earlier first days is kept.
  5. A fall of 20 % of the scale inside a few hours with no watering is a disturbed probe; the
     reading is not judged again until the next watering.

Also prints, for each watering, how long the PRESENT calibration (autocal_replay.py) had shown 0 %
or less.

  python cloud/scripts/water_now_replay.py --tops 0.36 0.38 0.41
  python cloud/scripts/water_now_replay.py --top 0.38 --figure cloud/docs/ALOCASIA_GP_WATER_NOW_2026-10-11.png
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

from autocal_replay import BUCKETS, PT, derive, pct, sql_rises
from moisture_cycle_analysis import EV, series

H = pd.Timedelta(hours=1)
STEP = pd.Timedelta(minutes=30)
TOL = 0.10            # a settled level within this share of the scale of the one in force is the same scale
STEP_UP = 0.15        # of the scale: a watering
STEP_DOWN = 0.20      # of the scale, with no watering: a disturbed probe
HYSTERESIS = 0.02     # "water now" does not switch off for a wobble this size


def median_of(vis: pd.Series, t0, t1, least: int = 6) -> float:
    w = vis[(vis.index >= t0) & (vis.index < t1)]
    return float(np.median(w.values)) if len(w) >= least else np.nan


def replay(b: pd.Series, base: float, top: float, bottom: float) -> tuple[pd.DataFrame, list[dict]]:
    """b: five-minute medians (UTC index) starting just after a watering."""
    settled = None                 # the scale in force
    shift = 0.0                    # the region is moved up by this while a higher settled level is unresolved or judged a shift
    last_w = b.index[0]
    waiting, first_day_due, pending_up, from_gap = True, True, None, False
    disturbed, on = False, False
    first_days: list[float] = []
    events = [dict(watered=last_w, level_before=np.nan, settled=np.nan, verdict="record starts", first_day=np.nan)]
    rows = []
    t = b.index[0] + 3 * H
    while t <= b.index[-1] + STEP:
        vis = b[b.index <= t]
        cur = median_of(vis, t - H, t + pd.Timedelta(seconds=1), least=1)
        before = vis[vis.index < t - H].iloc[-36:]
        before = before[before.index >= t - 9 * H]
        pre = float(np.median(before.values)) if len(before) >= 6 else np.nan
        scale = (settled - base) if settled else None

        if not np.isnan(cur) and not np.isnan(pre) and t - last_w > 6 * H:
            if cur - pre >= (STEP_UP * scale if scale else 0.05 * pre):
                rose = vis[(vis.index > before.index[-1]) & (vis >= pre + 0.5 * (cur - pre))]
                last_w = rose.index[0] if len(rose) else t
                events.append(dict(watered=last_w, level_before=pre, settled=np.nan, verdict="", first_day=np.nan))
                waiting, first_day_due, pending_up, disturbed, on, shift, from_gap = True, True, None, False, False, 0.0, False
            elif scale and pre - cur >= STEP_DOWN * scale and not disturbed:
                disturbed = True
                events[-1]["disturbed_at"] = t

        if waiting and t >= last_w + 24 * H:
            new = median_of(vis, last_w + 12 * H, last_w + 24 * H)
            if np.isnan(new) and t >= last_w + 30 * H:           # a gap in the record: take what there is after 12 h
                new, from_gap = median_of(vis, last_w + 12 * H, t), True
            if not np.isnan(new):
                waiting = False
                events[-1]["settled"] = new
                if settled is None:
                    settled, events[-1]["verdict"] = new, "first scale"
                else:
                    change = (new - settled) / (settled - base)
                    if change > TOL:
                        pending_up, shift = (settled, new), new - settled
                        events[-1]["verdict"] = f"{100 * change:+.0f} % of scale: held as a shift until the first day is in"
                    else:
                        settled = new
                        events[-1]["verdict"] = "same scale" if change >= -TOL else f"{100 * change:+.0f} % of scale: taken as the new scale"

        if first_day_due and not waiting and t >= last_w + 48 * H:
            later = median_of(vis, last_w + 36 * H, last_w + 48 * H)
            if not np.isnan(later) or t >= last_w + 60 * H:
                first_day_due = False
                if not np.isnan(later) and not disturbed and not from_gap:
                    new = events[-1]["settled"]
                    loss = new - later
                    if pending_up:
                        old, _ = pending_up
                        as_scale, as_shift = loss / (new - base), loss / (old - base)
                        usual = float(np.median(first_days)) if len(first_days) >= 2 else None
                        if usual and as_scale > 0 and abs(np.log(as_scale / usual)) < abs(np.log(as_shift / usual)):
                            settled, shift = new, 0.0
                            first_days.append(as_scale)
                            events[-1]["verdict"] += f" -> SCALE (first day {as_scale:.3f} of scale; as a shift {as_shift:.3f}; usual {usual:.3f})"
                        else:
                            events[-1]["verdict"] += f" -> SHIFT (first day as scale {as_scale:.3f}, as shift {as_shift:.3f})"
                        events[-1]["first_day"] = as_scale
                        pending_up = None
                    else:
                        first_days.append(loss / (settled - base))
                        events[-1]["first_day"] = first_days[-1]

        if settled is None or waiting or t < last_w + 24 * H:
            state, share = "just watered", np.nan
        elif np.isnan(cur):                                   # the probe is off the air: nothing new to judge
            state, share = "no data", np.nan
        else:
            share = (cur - shift - base) / (settled - base)
            if disturbed:
                state = "disturbed"
            else:
                on = share <= top if not on else share <= top + HYSTERESIS
                state = "overdue" if on and share < bottom else "water now" if on else "not yet"
        edge = (base + top * (settled - base) + shift, base + bottom * (settled - base) + shift) if settled else (np.nan, np.nan)
        rows.append(dict(t=t, reading=cur, settled=settled, share=share, top_raw=edge[0], bottom_raw=edge[1], state=state))
        t += STEP
    return pd.DataFrame(rows).set_index("t"), events


def present_display(b: pd.Series, grid: pd.DatetimeIndex) -> pd.Series:
    """The percent the present calibration would have shown, bounds refreshed every six hours."""
    out, bounds, refreshed = pd.Series(np.nan, index=grid), None, None
    for t in grid:
        vis = b[b.index <= t]
        if refreshed is None or t - refreshed >= 6 * H:
            d = derive(sql_rises(vis, t), t)
            # until it has a watering to go on it runs on percentiles of a falling record and reads 0 or less
            # throughout; that start-up is not what is being compared, so it is left blank
            bounds, refreshed = ((d["dry"], d["wet"]) if d["source"] == "adaptive" else None), t
        cur = median_of(vis, t - H, t + pd.Timedelta(seconds=1), least=1)
        out[t] = pct(cur, *bounds) if bounds and not np.isnan(cur) else np.nan
    return out


def summarize(states: pd.DataFrame, events: list[dict], shown: pd.Series | None) -> pd.DataFrame:
    rows = []
    for i in range(1, len(events)):
        start, end = events[i - 1]["watered"], events[i]["watered"]
        cyc = states[(states.index >= start) & (states.index < end)]
        is_on = cyc.state.isin(["water now", "overdue"])
        off = cyc.index[cyc.state.isin(["not yet", "just watered", "disturbed"])]   # the run of "water now" that the watering ended
        on = cyc[is_on & (cyc.index > off[-1])] if len(off) else cyc[is_on]
        row = dict(n=i, watered=end.tz_convert(PT).strftime("%a %m-%d %H:%M"), level_before=events[i]["level_before"],
                   share_then=cyc.share.dropna().iloc[-1] if cyc.share.notna().any() else np.nan,
                   said_then=cyc.state[cyc.state != "no data"].iloc[-1], water_now_for_h=(end - on.index[0]) / H if len(on) else 0.0)
        if shown is not None:
            pc = shown[(shown.index >= start + 24 * H) & (shown.index < end)].dropna()
            above = pc.index[pc > 0]
            low = pc[(pc <= 0) & (pc.index > above[-1])] if len(above) else pc[pc <= 0]
            row["present_percent_then"] = pc.iloc[-1] if len(pc) else np.nan          # blank: still starting up
            row["present_at_or_below_0_for_h"] = ((end - low.index[0]) / H if len(low) else 0.0) if len(pc) else np.nan
        rows.append(row)
    return pd.DataFrame(rows)


def figure(b: pd.Series, states: pd.DataFrame, events: list[dict], shown: pd.Series, top: float, bottom: float, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    surface, ink, ink2, muted, grid, axis, blue, wash = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7", "#2a78d6", "#cde2fb"
    warning, critical = "#fab219", "#d03b3b"
    plt.rcParams.update({"font.family": ["Segoe UI", "DejaVu Sans"], "font.size": 10})
    local = states.tz_convert(PT)
    line = b.tz_convert(PT).resample("30min").median().dropna()

    fig, (ax, strip) = plt.subplots(2, 1, figsize=(15, 7.6), sharex=True, facecolor=surface,
                                    gridspec_kw=dict(height_ratios=[6, 1], hspace=0.05))
    ax.set_facecolor(surface)
    ax.grid(True, axis="y", color=grid, lw=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(axis)
    ax.tick_params(colors=muted, length=0)
    judged = (local.state != "just watered") & local.top_raw.notna()
    ax.fill_between(local.index, local.bottom_raw.where(judged), local.top_raw.where(judged), color=wash, lw=0, step="post")
    ax.plot(line.index, line.values, color=blue, lw=1.6)
    ax.set_ylabel("stored reading", color=ink2)

    for i, e in enumerate(events):
        when = e["watered"].tz_convert(PT)
        ax.axvline(when, color=axis, lw=0.8, zorder=1)
        note = {0: "\nrecord starts", 6: "\nfertilizer", 7: "\nwatering?"}.get(i, "")
        ax.annotate(f"{i}", (when, 1.0), xycoords=("data", "axes fraction"), xytext=(0, 30), textcoords="offset points", ha="center",
                    va="bottom", color=ink, fontsize=13, fontweight="bold")
        ax.annotate(f"{when:%a %m-%d}{note}", (when, 1.0), xycoords=("data", "axes fraction"), xytext=(0, 28), textcoords="offset points",
                    ha="center", va="top", color=ink2, fontsize=9)
    first = local[judged].iloc[0]
    ax.annotate(f"water now region: {bottom:.2f} to {top:.2f} of the settled level", (local[judged].index[0], first.bottom_raw), xytext=(0, -5),
                textcoords="offset points", va="top", color=ink2, fontsize=9)
    held = local[judged & (local.top_raw > local.top_raw.shift(-96).fillna(local.top_raw) + 50)]      # a region held up as a shift
    if len(held):
        ax.annotate("held up one\nday, then\nrescaled", (held.index[0] - 16 * H, held.bottom_raw.iloc[0]), xytext=(0, -5),
                    textcoords="offset points", va="top", color=ink2, fontsize=9)

    strip.set_facecolor(surface)
    for side in ("top", "right", "left"):
        strip.spines[side].set_visible(False)
    strip.spines["bottom"].set_color(axis)
    strip.set_yticks([1.0, 0.0])
    strip.set_yticklabels(["region says", "present display"], color=ink2, fontsize=9)
    strip.tick_params(axis="y", length=0)
    strip.set_ylim(-0.6, 1.6)
    colours = {"water now": warning, "overdue": critical, "disturbed": muted}
    for state, colour in colours.items():
        strip.fill_between(local.index, 0.72, 1.28, where=(local.state == state).values, color=colour, lw=0, step="post")
    pc = shown.tz_convert(PT)
    watered_recently = pd.Series(False, index=pc.index)
    for e in events:
        watered_recently |= (pc.index >= e["watered"]) & (pc.index < e["watered"] + 24 * H)
    strip.fill_between(pc.index, -0.28, 0.28, where=((pc <= 0) & (pc > -10) & ~watered_recently).values, color=warning, lw=0, step="post")
    strip.fill_between(pc.index, -0.28, 0.28, where=((pc <= -10) & ~watered_recently).values, color=critical, lw=0, step="post")
    strip.legend(handles=[Patch(color=warning, label="water now  /  present display 0 % or less"),
                          Patch(color=critical, label="overdue  /  present display -10 % or less"),
                          Patch(color=muted, label="probe disturbed, not judged")],
                 loc="upper center", bbox_to_anchor=(0.5, -0.42), ncol=3, frameon=False, fontsize=9, labelcolor=ink2)
    strip.xaxis.set_major_locator(mdates.WeekdayLocator(byweekday=mdates.MO, tz=local.index.tz))
    strip.xaxis.set_major_formatter(mdates.DateFormatter("Mon %m-%d", tz=local.index.tz))
    strip.xaxis.set_minor_locator(mdates.DayLocator(tz=local.index.tz))
    strip.tick_params(axis="x", colors=muted, length=0)
    strip.tick_params(axis="x", which="minor", length=3, color=axis)
    ax.set_title("Reading as stored, with the region it would have been judged against", loc="left", color=ink2, fontsize=10, pad=58)
    fig.suptitle("Alocasia GP (sensor 3): when the display would have said \"water now\"", x=0.075, ha="left", color=ink, fontsize=14,
                 fontweight="bold", y=0.985)
    fig.subplots_adjust(left=0.075, right=0.985, top=0.83, bottom=0.13)
    fig.savefig(path, dpi=120, facecolor=surface)
    print(f"figure -> {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sensor", type=int, default=3)
    parser.add_argument("--base", type=float, default=730.0, help="the reading the water-dependent part scales about")
    parser.add_argument("--top", type=float, default=0.38)
    parser.add_argument("--bottom", type=float, default=0.25)
    parser.add_argument("--tops", type=float, nargs="+", help="only compare these top edges")
    parser.add_argument("--figure", type=Path)
    parser.add_argument("--buckets", type=Path, default=BUCKETS)
    args = parser.parse_args()
    pd.set_option("display.width", 250)
    fmt = lambda x: "%.2f" % x if abs(x) < 2 else "%.0f" % x

    b = series(args.sensor, args.buckets)
    b = b[b.index >= EV[0]]
    if args.tops:
        table = {}
        for top in args.tops:
            states, events = replay(b, args.base, top, args.bottom)
            s = summarize(states, events, None)
            table[f"top {top:.2f}: said"] = s.said_then.values
            table[f"top {top:.2f}: on for, h"] = s.water_now_for_h.round(0).values
            index = s.watered.values
        print(f"sensor {args.sensor}, base {args.base:.0f}: what the region said at each watering, and for how long it had said it")
        print(pd.DataFrame(table, index=index).to_string())
        return 0

    states, events = replay(b, args.base, args.top, args.bottom)
    shown = present_display(b, states.index)
    print(f"sensor {args.sensor}, base {args.base:.0f}, water now at or below {args.top:.2f} of the settled level, overdue below {args.bottom:.2f}")
    for i, e in enumerate(events):
        extra = f"   disturbed {e['disturbed_at'].tz_convert(PT):%m-%d %H:%M}" if "disturbed_at" in e else ""
        print(f"  {i}  {e['watered'].tz_convert(PT):%a %m-%d %H:%M}  settled {e['settled']:6.0f}  first day {e['first_day']:.3f}  {e['verdict']}{extra}")
    print(summarize(states, events, shown).to_string(index=False, float_format=fmt))
    counts = states.state.value_counts()
    print("hours in each state: " + ", ".join(f"{k} {v / 2:.0f}" for k, v in counts.items()))
    if args.figure:
        figure(b, states, events, shown, args.top, args.bottom, args.figure)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
