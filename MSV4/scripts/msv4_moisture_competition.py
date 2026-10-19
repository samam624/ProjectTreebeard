#!/usr/bin/env python3
"""Which of an MSV4 probe's numbers follows the water in the pot? Scored against the pot's weight.

The truth is grams: the pot is weighed, and its weight above the driest weighing on record is the
water in it. Every candidate reading is paired with each weighing and judged in those grams:

  slope            how far the candidate moves per gram of water (from the weighings);
  miss             how far the weighings sit from that straight line, in grams;
  rank agreement   whether more water always reads as more (Spearman), once there are enough
                   weighings for the question to mean anything;
  daily wobble     how far the candidate strays from a straight line through a day with no
                   watering, in grams (a pot dries through a day; falling with it is not an error);
  scatter          one hour's reading-to-reading scatter, in grams.
  watering trigger how early or late the candidate reaches the weighed dry point if its recent
                   trend continues. This is the end-use metric: when would the probe actually
                   tell the owner to water, and how many grams early or late would that be?

Lower is better for every column in grams. A second table takes each two neighbouring weighings
with no watering between them and sets what the scale lost beside what each candidate lost. The candidates are all made from the three numbers
the probe fits (Rsoil, Cm, Cd) and the probe's Cd in air; msv4_experimental_moisture.py says
what each is.

Weighings and waterings come from MSV3/fleet.json (pots.<pot>.mass.log and .watering_events), the
readings from the CSV that cloud/scripts/fetch_supabase_history.py --raw writes.

- A weighing is paired with the median of the readings within PAIR_MINUTES of it.
- For WATER_SETTLE_HOURS after a watering the probe reads water running past the blade, not the
  pot. A weighing taken in those hours is paired with the first hour after them instead; the pot
  loses little in that time and the table says so.
- A weighing with water standing in the saucer counts water that is not in the soil. It is
  listed and NOT scored; --with-saucer scores it anyway. A weighing whose saucer condition was
  not recorded is listed but never scored.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 10 --raw --since-hours 110 --csv data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_moisture_competition.py data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_moisture_competition.py --self-test
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from datetime import datetime, timedelta
from pathlib import Path

import msv4_experimental_moisture as reading

FLEET = Path(__file__).resolve().parents[2] / "MSV3" / "fleet.json"
PAIR_MINUTES = 30
WATER_SETTLE_HOURS = 2
MIN_PAIR_READINGS = 5
REPEAT_MINUTES = 10       # weighings this close together are repeats of one weighing
LEVEL_GRAMS = 10          # weighings closer than this are the same amount of water for ranking
MIN_LEVELS_TO_RANK = 4
# Board 1's contact capacitance was still climbing 21 hours after it was planted
# (MSV4_SOIL_THERMAL_FIRST_DAY_2026-10-09.md), so no day that starts sooner is a settled one.
INSERTION_SETTLE_HOURS = 21
FORECAST_HOURS = 36      # Long enough not to mistake one day/night swing for the drying rate.


def _log_ohm(value: float) -> float:
    return -10000.0 * math.log10(value)


def _soil(row: dict) -> tuple[float, float] | None:
    """(soil capacitance, soil resistance) behind the mask, with Cd in air as the board's share."""
    if row["open"] or row["cd_soil"] is None or row["cm"] <= 0.0:
        return None
    cx, cm = row["cd_soil"], row["cm"]
    return cx * (1.0 + cx / cm), row["rsoil"] * (cm / (cm + cx)) ** 2


# (name, unit, how a reading row becomes the candidate; None when that row has no value).
# Every one is arranged so that HIGHER MEANS WETTER, which is why the resistances are logged
# and negated: the units are those of the moisture number in use, 10000 per decade of ohms.
CANDIDATES = (
    ("moisture number in use", "units", lambda r: r["stored"] if not r["open"] else None),
    ("soil resistance, no temp. correction", "units", lambda r: _log_ohm(r["rsoil"]) if r["rsoil"] else None),
    ("direct capacitance above air", "pF", lambda r: r["cd_soil"]),
    ("contact capacitance Cm", "pF", lambda r: r["cm"] if not r["open"] else None),
    ("soil-coupled capacitance (Cm + Cd - air)", "pF", lambda r: r["coupled"]),
    ("soil capacitance (derived)", "pF", lambda r: _soil(r)[0] if _soil(r) else None),
    ("soil resistance behind the mask (derived)", "units", lambda r: _log_ohm(_soil(r)[1]) if _soil(r) else None),
)


def saucer_state(entry: dict) -> tuple[bool | None, bool]:
    """Return (standing water, excluded) without interpreting an unknown as dry."""
    condition = str(entry.get("saucer_water", "")).lower()
    saucer = True if condition.startswith("yes") else False if condition.startswith("no") else None
    return saucer, bool(entry.get("exclude_from_gravimetric_fit", False)) or saucer is None


def load_pot(sensor: int, fleet: Path = FLEET) -> tuple[list[dict], list[datetime]]:
    """(weighings, watering times) for the pot that sensor is in."""
    pots = json.loads(fleet.read_text(encoding="utf-8"))["pots"]
    for pot in pots.values():
        if isinstance(pot, dict) and sensor in pot.get("sensors", []) and "mass" in pot:
            weighings = []
            for e in pot["mass"].get("log", []):
                if "at" not in e:
                    continue
                saucer, excluded = saucer_state(e)
                weighings.append({"time": datetime.fromisoformat(e["at"]), "grams": float(e["grams"]),
                                  "saucer": saucer,
                                  "excluded": excluded})
            waterings = [datetime.fromisoformat(e["at"]) for e in pot.get("watering_events", {}).get("events", []) if "at" in e]
            return sorted(weighings, key=lambda w: w["time"]), sorted(waterings)
    raise SystemExit(f"no pot in {fleet} holds sensor {sensor} and has weighings")


def merge_repeats(weighings: list[dict], waterings: list[datetime]) -> list[dict]:
    """Weighings repeated within REPEAT_MINUTES become one, at their median.

    The scale does not give the same number twice (board 1's pot, lifted off and put back:
    5098, 5106, 5105 g), so a repeat is a second look at the same water and their spread is what
    the scale can be trusted to. Never merged across a watering or a change in the saucer.
    """
    merged: list[dict] = []
    for weighing in weighings:
        last = merged[-1] if merged else None
        if (last is not None and weighing["time"] - last["times"][-1] <= timedelta(minutes=REPEAT_MINUTES)
                and weighing["saucer"] == last["saucer"]
                and not any(last["times"][-1] < w <= weighing["time"] for w in waterings)):
            last["all"].append(weighing["grams"])
            last["times"].append(weighing["time"])
        else:
            merged.append({**weighing, "all": [weighing["grams"]], "times": [weighing["time"]]})
    for m in merged:
        m["grams"] = statistics.median(m["all"])
        m["time"] = m["times"][len(m["times"]) // 2]
        m["spread"] = max(m["all"]) - min(m["all"])
    return merged


def settling(when: datetime, waterings: list[datetime]) -> datetime | None:
    """The watering this moment is still inside the settling hours of, if any."""
    for watered in waterings:
        if watered <= when < watered + timedelta(hours=WATER_SETTLE_HOURS):
            return watered
    return None


def pair(rows: list[dict], weighings: list[dict], waterings: list[datetime]) -> list[dict]:
    """Each weighing with the readings it is judged against."""
    out = []
    for weighing in weighings:
        watered = settling(weighing["time"], waterings)
        if watered is None:
            start, end = weighing["time"] - timedelta(minutes=PAIR_MINUTES), weighing["time"] + timedelta(minutes=PAIR_MINUTES)
        else:
            start = watered + timedelta(hours=WATER_SETTLE_HOURS)
            end = start + timedelta(hours=1)
        chunk = [row for row in rows if start <= row["time"] < end and settling(row["time"], waterings) is None]
        out.append({**weighing, "rows": chunk, "late": watered is not None, "from": start, "to": end})
    return out


def dry_days(rows: list[dict], waterings: list[datetime]) -> list[list[dict]]:
    """Whole 24-hour stretches with no watering in them or in the day before.

    They are counted back from each watering (the day before it is the driest and longest
    settled) and forward from a day after it. Nothing within INSERTION_SETTLE_HOURS of the first
    reading counts: the soil is still closing on the blade.
    """
    first = rows[0]["time"] + timedelta(hours=INSERTION_SETTLE_HOURS)
    last = rows[-1]["time"]
    day = timedelta(hours=24)
    spans = []
    for i, watered in enumerate(waterings):
        earliest = max(first, waterings[i - 1] + day) if i else first
        end = watered
        while end - day >= earliest:
            spans.append((end - day, end))
            end -= day
    start = (waterings[-1] + day) if waterings else first
    while start + day <= last:
        spans.append((start, start + day))
        start += day
    return [[row for row in rows if a <= row["time"] < b] for a, b in sorted(spans)]


def hourly(rows: list[dict], value) -> list[tuple[float, float, float]]:
    """(hour, median, scatter) of a candidate for each hour with enough readings."""
    bins: dict[int, list[float]] = {}
    for row in rows:
        v = value(row)
        if v is not None:
            bins.setdefault(int((row["time"] - rows[0]["time"]).total_seconds() // 3600), []).append(v)
    return [(hour, statistics.median(v), reading.mad(v)) for hour, v in sorted(bins.items()) if len(v) >= MIN_PAIR_READINGS]


def wobble(hours: list[tuple[float, float, float]]) -> float:
    """How far the hourly medians stray from a straight line through the day.

    A pot dries through a day, and a reading that follows the water falls with it; that is not
    an error. What is left after the line is taken out is.
    """
    xs, ys = [h[0] for h in hours], [h[1] for h in hours]
    slope, intercept = statistics.linear_regression(xs, ys)
    left = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
    return max(left) - min(left)


def drying(pairs: list[dict], waterings: list[datetime]) -> list[tuple[int, int]]:
    """Neighbouring weighings with no watering between them, as indexes into pairs.

    The watering's step sets nearly all of a candidate's per gram, so the few tens of grams a
    pot loses between two such weighings are a change the line was not fitted to.
    """
    return [(i, i + 1) for i in range(len(pairs) - 1)
            if not any(pairs[i]["time"] < w <= pairs[i + 1]["time"] for w in waterings)]


def spearman(xs: list[float], ys: list[float]) -> float:
    def ranks(data: list[float]) -> list[float]:
        order = sorted(range(len(data)), key=lambda i: data[i])
        out = [0.0] * len(data)
        i = 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and data[order[j + 1]] == data[order[i]]:
                j += 1
            for k in range(i, j + 1):
                out[order[k]] = (i + j) / 2.0
            i = j + 1
        return out
    return statistics.correlation(ranks(xs), ranks(ys))


def score(rows: list[dict], pairs: list[dict], waterings: list[datetime]) -> list[dict]:
    dry = min(p["grams"] for p in pairs)
    levels = sorted({round(p["grams"] / LEVEL_GRAMS) for p in pairs})
    results = []
    for name, unit, value in CANDIDATES:
        medians = []
        for p in pairs:
            data = [v for v in (value(row) for row in p["rows"]) if v is not None]
            medians.append(statistics.median(data) if len(data) >= MIN_PAIR_READINGS else None)
        points = [(p["grams"] - dry, m) for p, m in zip(pairs, medians) if m is not None]
        result = {"name": name, "unit": unit, "points": points, "medians": medians, "slope": None}
        if len({round(g / LEVEL_GRAMS) for g, _ in points}) >= 2:
            grams, vals = [g for g, _ in points], [v for _, v in points]
            slope, intercept = statistics.linear_regression(grams, vals)
            result["slope"] = slope
            if slope > 0.0:
                miss = [(v - intercept) / slope - g for g, v in points]
                result["miss"] = math.sqrt(statistics.fmean(m * m for m in miss))
                if len(levels) >= MIN_LEVELS_TO_RANK:
                    result["rank"] = spearman(grams, vals)
                swings, scatters = [], []
                for day in dry_days(rows, waterings):
                    hours = hourly(day, value)
                    if len(hours) >= 12:
                        swings.append(wobble(hours))
                        scatters.append(statistics.median(h[2] for h in hours))
                if swings:
                    result["swing"] = statistics.median(swings) / slope
                    result["scatter"] = statistics.median(scatters) / slope
                    result["dry_days"] = len(swings)
        results.append(result)
    return results


def watering_forecast(rows: list[dict], pairs: list[dict]) -> dict | None:
    """Project each candidate to the weighed dry point from its latest 36-hour trend.

    The candidate's reading is first converted to grams with the same all-weighing linear fit
    used by the competition. ``trigger_error`` is the scale's projected grams above the dry
    point when that candidate reaches zero: positive is an early watering call, negative is a
    late call. This is deliberately a forecast, not another fit score; it should be trusted only
    while the recent drying trend remains representative.
    """
    if len(pairs) < 3:
        return None
    dry = min(p["grams"] for p in pairs)
    latest = pairs[-1]
    remaining = latest["grams"] - dry
    if remaining <= 0.0:
        return None
    window_start = latest["time"] - timedelta(hours=FORECAST_HOURS)
    scale_points = [p for p in pairs if window_start <= p["time"] <= latest["time"]]
    if len(scale_points) < 3:
        return None
    scale_x = [(p["time"] - latest["time"]).total_seconds() / 86400.0 for p in scale_points]
    scale_slope, _ = statistics.linear_regression(scale_x, [p["grams"] for p in scale_points])
    scale_burn = -scale_slope
    if scale_burn <= 0.0:
        return None

    recent = [r for r in rows if window_start <= r["time"] <= latest["time"]]
    candidates = []
    for name, unit, value in CANDIDATES:
        medians = []
        points = []
        for p in pairs:
            data = [v for v in (value(r) for r in p["rows"]) if v is not None]
            median = statistics.median(data) if len(data) >= MIN_PAIR_READINGS else None
            medians.append(median)
            if median is not None:
                points.append((p["grams"] - dry, median))
        if medians[-1] is None or len({round(g / LEVEL_GRAMS) for g, _ in points}) < 2:
            continue
        slope, intercept = statistics.linear_regression([g for g, _ in points], [v for _, v in points])
        hours = hourly(recent, value)
        if slope <= 0.0 or len(hours) < 12:
            continue
        trend_per_hour, _ = statistics.linear_regression([h for h, _, _ in hours], [m for _, m, _ in hours])
        burn = -trend_per_hour * 24.0 / slope
        if burn <= 0.0:
            continue
        now = (medians[-1] - intercept) / slope
        days = now / burn
        candidates.append({
            "name": name,
            "now": now,
            "bias": now - remaining,
            "burn": burn,
            "days": days,
            "delta_days": days - remaining / scale_burn,
            "trigger_error": latest["grams"] - scale_burn * days - dry,
        })
    return {"dry": dry, "remaining": remaining, "scale_burn": scale_burn,
            "scale_days": remaining / scale_burn, "candidates": candidates}


def report(rows: list[dict], weighings: list[dict], waterings: list[datetime], with_saucer: bool = False) -> None:
    listed = pair(rows, merge_repeats(weighings, waterings), waterings)
    pairs = [p for p in listed if not p.get("excluded", False) and (with_saucer or p["saucer"] is False)]
    dry = min(p["grams"] for p in pairs)
    print(f"\nWeighings (water = grams above the driest, {dry:.0f} g; repeats within {REPEAT_MINUTES} min are one weighing, at their median)")
    print(f"{'weighed':17}  grams  water  saucer  readings paired")
    for p in listed:
        note = f"{p['from']:%m-%d %H:%M} to {p['to']:%H:%M}, {len(p['rows'])} readings"
        if p["late"]:
            note += f" (weighed inside the {WATER_SETTLE_HOURS} h after a watering: paired with the hour after them)"
        if p.get("excluded", False):
            note = "NOT SCORED: saucer condition was not recorded"
        elif p["saucer"] and not with_saucer:
            note = "NOT SCORED: water standing in the saucer is in this weight and not in the soil"
        if len(p["all"]) > 1:
            note += f"; {len(p['all'])} weighings {', '.join(f'{g:.0f}' for g in p['all'])}"
        condition = "unknown" if p["saucer"] is None else "WATER" if p["saucer"] else "dry"
        print(f"{p['time']:%Y-%m-%d %H:%M}  {p['grams']:5.0f}  {p['grams'] - dry:5.0f}  {condition:7}  {note}")
    spreads = [p["spread"] for p in listed if len(p["all"]) > 1]
    if spreads:
        print(f"The scale: repeated weighings differ by up to {max(spreads):.0f} g ({len(spreads)} set(s)). "
              f"A miss or a difference in water smaller than that is not one.")
    levels = len({round(p["grams"] / LEVEL_GRAMS) for p in pairs})
    results = score(rows, pairs, waterings)
    ranked = sorted((r for r in results if r.get("slope") and r["slope"] > 0.0),
                    key=lambda r: (r.get("miss", 1e9) ** 2 + r.get("swing", 1e9) ** 2))
    print(f"\n{'candidate':42}  per gram        miss g  rank  daily wobble g  scatter g")
    for r in ranked:
        rank = f"{r['rank']:+.2f}" if "rank" in r else "    -"
        swing = f"{r['swing']:13.1f}  {r['scatter']:9.1f}" if "swing" in r else f"{'-':>13}  {'-':>9}"
        print(f"{r['name']:42}  {r['slope']:8.4f} {r['unit']:5}  {r['miss']:6.1f}  {rank}  {swing}")
    for r in results:
        if not r.get("slope") or r["slope"] <= 0.0:
            why = "moves the wrong way with water" if r.get("slope") else "too few weighings with readings"
            print(f"{r['name']:42}  not scored: {why}")
    between = drying(pairs, waterings)
    if between and ranked:
        print("\nBetween one weighing and the next with no watering between them: grams of water gained or lost, "
              "by the scale and by each candidate at its own per gram")
        print(f"{'':42}" + "".join(f"  {pairs[a]['time']:%m-%d %H:%M} to {pairs[b]['time']:%m-%d %H:%M}" for a, b in between))
        tips = [[row["tip"] for row in p["rows"] if row.get("tip") is not None] for p in pairs]
        if all(tips[i] for leg in between for i in leg):
            print(f"{'tip, C':42}" + "".join(f"  {f'{statistics.median(tips[a]):.2f} to {statistics.median(tips[b]):.2f}':>26}" for a, b in between))
        print(f"{'the scale':42}" + "".join(f"  {pairs[b]['grams'] - pairs[a]['grams']:+26.0f}" for a, b in between))
        for r in ranked:
            cells = [f"{(r['medians'][b] - r['medians'][a]) / r['slope']:+26.1f}"
                     if r["medians"][a] is not None and r["medians"][b] is not None else f"{'-':>26}" for a, b in between]
            print(f"{r['name']:42}" + "".join(f"  {cell}" for cell in cells))
    forecast = watering_forecast(rows, pairs)
    if forecast and forecast["candidates"]:
        print(f"\nWatering trigger forecast from the latest {FORECAST_HOURS} h trend "
              f"(weighed dry point {forecast['dry']:.0f} g)")
        print(f"Scale: {forecast['remaining']:.1f} g remain, losing {forecast['scale_burn']:.1f} g/day -> "
              f"dry in {forecast['scale_days']:.2f} days")
        print(f"{'candidate':42}  {'water now':>9}  {'loss/day':>8}  {'days':>5}  {'vs scale':>8}  {'trigger error':>13}")
        for f in sorted(forecast["candidates"], key=lambda item: item["days"]):
            timing = f"{f['delta_days']:+.2f} d"
            trigger = f"{abs(f['trigger_error']):.1f} g {'early' if f['trigger_error'] >= 0 else 'late'}"
            print(f"{f['name']:42}  {f['now']:7.1f} g  {f['burn']:6.1f} g  {f['days']:5.2f}  "
                  f"{timing:>8}  {trigger:>13}")
        print("This is the end-use score: positive/early means water remains at the call; late means the "
              "scale would already be below the weighed dry point. It is an extrapolation, so re-run it "
              "after every weighing and confirm it across another watering cycle.")
    days = max((r.get("dry_days", 0) for r in results), default=0)
    print(f"\nOrder: smallest first of miss and daily wobble taken together. {levels} amounts of water weighed, "
          f"{days} settled day(s) with no watering.")
    if levels < MIN_LEVELS_TO_RANK:
        print(f"THE ORDER IS NOT A RESULT YET. With {levels} amounts of water a straight line fits anything: 'per gram' is "
              f"one watering's step, 'miss' is only how the weighings at each amount disagree, and nothing tests whether "
              f"more water always reads as more. That needs {MIN_LEVELS_TO_RANK} or more amounts, which a dry-down weighed daily gives.")
    if any(p["saucer"] for p in pairs):
        print("A scored weighing marked WATER includes water standing in the saucer, which is not in the soil.")


def self_test() -> None:
    """Made-up pot: one candidate follows the water exactly, another follows the time of day."""
    assert saucer_state({"saucer_water": "no: dry"}) == (False, False)
    assert saucer_state({"saucer_water": "yes: standing"}) == (True, False)
    assert saucer_state({"saucer_water": "unknown: not stated"}) == (None, True)
    assert saucer_state({"saucer_water": "no", "exclude_from_gravimetric_fit": True}) == (False, True)
    start = datetime.fromisoformat("2026-11-01T00:00:00+00:00")
    watered = start + timedelta(hours=30)

    def water_at(hour: float) -> float:
        return 100.0 - hour if hour < 30 else 500.0 - 2.0 * (hour - 30)   # grams above empty

    rows = []
    for minute in range(0, 96 * 60, 10):
        hour = minute / 60.0
        water = water_at(hour)
        rows.append({"time": start + timedelta(minutes=minute), "open": False, "true": water,
                     "clock": water + 60.0 * math.sin(2 * math.pi * hour / 24.0), "backwards": -water})
    weighings = [{"time": start + timedelta(hours=h), "grams": 4000.0 + water_at(h), "saucer": False}
                 for h in (6, 28, 30.05, 54, 78, 94)]
    global CANDIDATES
    kept = CANDIDATES
    CANDIDATES = (("true", "u", lambda r: r["true"]), ("clock", "u", lambda r: r["clock"]),
                  ("backwards", "u", lambda r: r["backwards"]))
    try:
        pairs = pair(rows, weighings, [watered])
        # The weighing three minutes after the watering is judged on the hour after the settling.
        assert pairs[2]["late"] and pairs[2]["from"] == watered + timedelta(hours=WATER_SETTLE_HOURS)
        assert not pairs[0]["late"] and len(pairs[0]["rows"]) == 6
        # No reading from the settling hours is paired with anything.
        assert all(settling(row["time"], [watered]) is None for p in pairs for row in p["rows"])
        results = {r["name"]: r for r in score(rows, pairs, [watered])}
        assert abs(results["true"]["slope"] - 1.0) < 0.02 and results["true"]["miss"] < 5.0
        assert results["true"]["rank"] > 0.99
        # A reading that falls with a drying pot has no wobble; one that follows the clock has.
        assert results["clock"]["swing"] > 100.0 and results["true"]["swing"] < 1.0
        assert results["clock"]["miss"] > results["true"]["miss"]
        assert results["backwards"]["slope"] < 0.0 and "miss" not in results["backwards"]
        # The day before the watering starts too soon after planting; the one a day after it counts.
        days = dry_days(rows, [watered])
        assert len(days) == 1 and days[0][0]["time"] == watered + timedelta(hours=24)
        assert results["true"]["dry_days"] == 1
        # Between weighings: every neighbouring two but the two the watering falls between, and
        # a candidate that follows the water loses what the scale loses.
        assert drying(pairs, [watered]) == [(0, 1), (2, 3), (3, 4), (4, 5)]
        lost = (results["true"]["medians"][4] - results["true"]["medians"][3]) / results["true"]["slope"]
        assert abs(lost - (pairs[4]["grams"] - pairs[3]["grams"])) < 2.0
        forecast_weighings = sorted(
            weighings + [{"time": start + timedelta(hours=70),
                           "grams": 4000.0 + water_at(70), "saucer": False}],
            key=lambda w: w["time"],
        )
        forecast = watering_forecast(rows, pair(rows, forecast_weighings, [watered]))
        assert forecast is not None
        true_forecast = next(r for r in forecast["candidates"] if r["name"] == "true")
        assert abs(true_forecast["trigger_error"]) < 2.0 and abs(true_forecast["delta_days"]) < 0.05
        # With the planting further back it is the day BEFORE the watering that counts, and the
        # record ends before a whole day after it has passed.
        later = dry_days(rows, [start + timedelta(hours=50)])
        assert len(later) == 1 and later[0][0]["time"] == start + timedelta(hours=26)
        assert abs(spearman([1, 2, 3, 4], [10, 20, 20, 40]) - statistics.correlation([0, 1, 2, 3], [0, 1.5, 1.5, 3])) < 1e-12
    finally:
        CANDIDATES = kept
    # Repeats become one weighing at their median, but never across a watering or a saucer change.
    at = lambda minute, grams, saucer=False: {"time": start + timedelta(minutes=minute), "grams": grams, "saucer": saucer}
    merged = merge_repeats([at(0, 4673.0), at(2, 4674.0), at(4, 5135.0, True), at(280, 5098.0), at(281, 5106.0), at(282, 5105.0)],
                           [start + timedelta(minutes=3)])
    assert [m["grams"] for m in merged] == [4673.5, 5135.0, 5105.0] and [m["spread"] for m in merged] == [1.0, 0.0, 8.0]
    assert len(merge_repeats([at(0, 4674.0), at(2, 5135.0)], [start + timedelta(minutes=1)])) == 2
    # The real candidates on one of board 1's readings: all defined, all higher after the watering.
    def row(rsoil, cm, cd):
        return {"open": False, "rsoil": rsoil, "cm": cm, "cd": cd, "cd_soil": cd - 13.72, "coupled": cm + cd - 13.72,
                "stored": round(10000 * math.log10(400000 / rsoil))}
    before, after = row(15350.0, 30.0, 21.50), row(2753.0, 124.3, 35.96)
    for name, _, value in CANDIDATES:
        assert value(after) > value(before), name
    assert abs(_soil(before)[0] - 9.80) < 0.01 and abs(_soil(after)[0] - 26.22) < 0.01
    print("PASS moisture competition: pairing, settling hours, scoring, ranking and watering-trigger forecast")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--with-saucer", action="store_true", help="also score weighings with water standing in the saucer")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.csv:
        parser.error("a CSV is needed unless --self-test is given")
    rows = [row for row in reading.load(args.csv, None) if row["coupled"] is not None]
    if not rows:
        raise SystemExit("no MSV4 measurements with an air value in that file")
    weighings, waterings = load_pot(rows[0]["sensor"])
    print(f"{len(rows)} readings, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}, sensor {rows[0]['sensor']}; "
          f"{len(weighings)} weighings, {len(waterings)} watering(s) on record")
    report(rows, weighings, waterings, args.with_saucer)


if __name__ == "__main__":
    main()
