#!/usr/bin/env python
"""Replay the cloud's moisture auto-calibration over stored history.

A port of `msv2_read_sensor_auto_calibration` (migration 027) and `deriveAutoCal`
(`supabase/functions/receiver-snapshot/autocal.ts`), evaluated at any past moment over the
five-minute medians that `fetch_moisture_buckets.py` writes. It answers: what dry/wet range would
the display have been using then, and what percent did that make the reading.

  python cloud/scripts/autocal_replay.py --sensor 3 --start "2026-08-08 12:00" --end "2026-09-30 18:00" --clip 700,1600

The live function only knows a sensor's LATEST manual reset, so a replay runs as if there had
never been one; `--start` is where to begin instead. `moisture_percent_closedloop.py --self-test`
holds a second, array-based port equal to this one.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

PT = "America/Los_Angeles"
BUCKETS = Path(__file__).resolve().parents[2] / "data" / "sensor_logs"


def load(path) -> pd.DataFrame:
    frame = pd.read_csv(path)
    frame["t"] = pd.to_datetime(frame["bucket_utc"], utc=True, format="mixed")
    return frame.set_index("t").sort_index()


def sql_rises(b: pd.Series, now, lookback_days: float = 21, reset_at=None):
    """Migration 027 on a Series of five-minute medians (UTC index), seeing nothing after `now`."""
    lo = now - pd.Timedelta(days=lookback_days)
    if reset_at is not None:
        lo = max(lo, reset_at)
    w = b[(b.index >= lo) & (b.index <= now)]
    if len(w) == 0:
        return None
    p5, p95 = np.percentile(w.values, [5, 95])
    out = dict(sampleCount=len(w), oldest=w.index[0], dryBound=round(p5), wetBound=round(p95), rises=[], p5=p5, p95=p95)
    rng = p95 - p5
    if rng <= 0:
        return out
    t, v, ts = w.index.values, w.values, w.index
    dv, gap = np.diff(v), np.diff(t)
    for i in np.where((gap <= np.timedelta64(30, "m")) & (dv >= 0.20 * rng))[0] + 1:
        at, prev = ts[i], v[i - 1]
        pre = w[(ts >= at - pd.Timedelta(hours=2)) & (ts < at)]
        post = w[(ts >= at) & (ts <= at + pd.Timedelta(hours=3))]
        later = w[(ts >= at + pd.Timedelta(hours=6)) & (ts <= at + pd.Timedelta(hours=12))]
        if len(pre) < 6 or len(post) < 6:
            continue
        pre_med, pre_p20 = np.median(pre.values), np.percentile(pre.values, 20)
        post_p80, post_max = np.percentile(post.values, 80), post.values.max()
        later_med = np.median(later.values) if len(later) else np.nan
        if post_p80 - pre_med < 0.50 * rng or abs(prev - pre_med) > 0.50 * rng or not post_max > prev:
            continue
        mature = len(later) >= 12 and (later_med - pre_med) >= 0.20 * rng
        young = len(later) < 12 and at >= now - pd.Timedelta(hours=12)
        if not (mature or young):
            continue
        cycle_dry, cycle_wet = round(0.75 * prev + 0.25 * pre_p20), round(post_max)
        if cycle_wet <= cycle_dry:
            continue
        out["rises"].append(dict(at=at, dryBound=cycle_dry, wetBound=cycle_wet, pre=len(pre), post=len(post),
                                 later=len(later), persistenceValidated=bool(mature)))
    return out


def clamp(x, a, b):
    return min(max(x, a), b)


def support(rise) -> float:
    pre, post, later = clamp(rise["pre"] / 12, 0.25, 1), clamp(rise["post"] / 18, 0.25, 1), clamp(rise["later"] / 24, 0.25, 1)
    return pre * post * (later if rise["persistenceValidated"] else 0.25)


def cycles_of(rises) -> list[dict]:
    cycles, active, last = [], None, None
    for rise in sorted(rises, key=lambda r: r["at"]):
        weight = support(rise)
        if last is None or (rise["at"] - last) > pd.Timedelta(hours=12):
            if active:
                cycles.append(active)
            active = dict(at=rise["at"], dry=rise["dryBound"], wet=rise["wetBound"], w=weight, val=rise["persistenceValidated"])
        else:
            active["wet"] = max(active["wet"], rise["wetBound"])
            active["w"] = max(active["w"], weight)
            active["val"] = active["val"] or rise["persistenceValidated"]
        last = rise["at"]
    if active:
        cycles.append(active)
    return cycles


def wmean(vals, ws) -> float:
    ws, vals = np.asarray(ws, float), np.asarray(vals, float)
    return float((vals * ws).sum() / ws.sum()) if ws.sum() > 0 else float("nan")


def robust(vals, ws, floor) -> float:
    if len(vals) < 3:
        return wmean(vals, ws)
    centre = float(np.median(vals))
    limit = max(floor, 2.5 * 1.4826 * float(np.median(np.abs(np.asarray(vals) - centre))))
    return wmean([clamp(x, centre - limit, centre + limit) for x in vals], ws)


def derive(raw, now):
    """autocal.ts deriveAutoCal: the dry/wet bounds the receiver would be handed at `now`."""
    if raw is None:
        return None
    cyc = cycles_of(raw["rises"])
    ready = raw["sampleCount"] >= 50 and (now - raw["oldest"]) >= pd.Timedelta(days=3)
    fd, fw = raw["dryBound"], raw["wetBound"]
    res = dict(ready=ready, dry=fd, wet=fw, source="percentiles", n_cycles=len(cyc), prior_dry=fd, prior_wet=fw,
               event_dry=np.nan, event_span=np.nan, cycles=cyc)
    if ready and cyc and fw > fd:
        newest, fs = cyc[-1]["at"], max(1, fw - fd)
        ws = [c["w"] * 0.5 ** (max(0, (newest - c["at"]).total_seconds()) / 86400 / 7) for c in cyc]
        ed = robust([c["dry"] for c in cyc], ws, max(1, fs * 0.05))
        es = robust([c["wet"] - c["dry"] for c in cyc], ws, max(1, fs * 0.10))
        ew, pw = sum(ws), (0.20 if len(cyc) == 1 else 0.10)
        ld, ls = (ed * ew + fd * pw) / (ew + pw), (es * ew + fs * pw) / (ew + pw)
        res.update(dry=round(ld), wet=round(ld + max(1, ls)), source="adaptive", event_dry=ed, event_span=es)
    return res


def pct(value, dry, wet) -> float:
    if wet <= dry:
        wet = dry + 1
    return min(100.0, 100.0 * (value - dry) / (wet - dry))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sensor", type=int, required=True)
    parser.add_argument("--start", required=True, help="local time, e.g. '2026-08-08 12:00'")
    parser.add_argument("--end", required=True)
    parser.add_argument("--step-hours", type=float, default=6)
    parser.add_argument("--clip", help="lo,hi: drop buckets outside this (reflash and open-circuit rows)")
    parser.add_argument("--buckets", type=Path, default=BUCKETS)
    args = parser.parse_args()

    b = load(args.buckets / f"moisture_5min_s{args.sensor}.csv")["moisture"].astype(float)
    b = b[(b.index >= pd.Timestamp(args.start, tz=PT)) & (b.index <= pd.Timestamp(args.end, tz=PT))]
    if args.clip:
        lo, hi = map(float, args.clip.split(","))
        b = b[(b >= lo) & (b <= hi)]

    rows, now = [], b.index[0] + pd.Timedelta(days=1)
    while now <= b.index[-1]:
        seen = b[b.index <= now]
        r = derive(sql_rises(seen, now), now)
        if r:
            rows.append(dict(now=now.tz_convert(PT).strftime("%m-%d %H:%M"), raw=seen.iloc[-1], ready=r["ready"], dry=r["dry"],
                             wet=r["wet"], source=r["source"], cycles=r["n_cycles"], prior_dry=r["prior_dry"],
                             prior_wet=r["prior_wet"], event_dry=r["event_dry"], event_span=r["event_span"],
                             percent=pct(seen.iloc[-1], r["dry"], r["wet"])))
        now += pd.Timedelta(hours=args.step_hours)

    whole = sql_rises(b, b.index[-1], lookback_days=10000)
    print(f"waterings the detector finds with the whole record in view (p5/p95 {whole['p5']:.0f}/{whole['p95']:.0f}):")
    for c in cycles_of(whole["rises"]):
        print(f"  {c['at'].tz_convert(PT):%m-%d %H:%M}  cycle dry {c['dry']}  cycle wet (max in 3 h) {c['wet']}  weight {c['w']:.2f}")
    pd.set_option("display.width", 250)
    pd.set_option("display.max_rows", 2000)
    print(pd.DataFrame(rows).iloc[::max(1, int(24 / args.step_hours))].to_string(index=False, float_format=lambda x: "%.1f" % x))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
