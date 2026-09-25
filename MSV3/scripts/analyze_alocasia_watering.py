#!/usr/bin/env python3
"""Analyse the multi-probe pot test across its first watering event.

Input files are ``treebeard_alocasia_s<ID>_<suffix>.csv`` from
``cloud/scripts/fetch_supabase_history.py --raw``.

The watering of 2026-09-22 10:50 PDT is the first common physical input every rig
saw at once. It supplies the transfer calibration the earlier thermal work
lacked: AM detector millivolts, FDC capacitance and MSV2 RC counts can each be
divided by that rig's OWN step response, after which their temperature
coefficients and noise floors become directly comparable.

The Begonia pot (probe 7) was watered at the same time. Its step is not visible
by inspection because it is buried under that rig's own diurnal thermal swing --
which is the single most important result here, so probe 7 is a member of the
comparison, not a control. Only probe 6, on the desk in no soil, is a control.

Sign convention: every rig's signal is oriented so that POSITIVE = WETTER.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics as stats
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

PACIFIC = timezone(timedelta(hours=-7), "PDT")

# Watering, located to a 5-minute bin from the simultaneous step in all four
# in-pot Alocasia rigs; probe 6 on the desk does not move at the same instant.
WATERING = datetime(2026, 9, 22, 10, 50, tzinfo=PACIFIC)

# Sensor -> (label, readout family). The AM and RC rigs report no die temperature.
RIGS = {
    2: ("sensor 2  MSV2 interdigitated + RC", "rc"),
    3: ("sensor 3  v1.2 electrode + AM", "am"),
    9: ("sensor 9  MSV3 blade + AM", "am"),
    4: ("probe 4   v1.2 electrode + FDC", "fdc"),
    5: ("probe 5   MSV3 blade + FDC", "fdc"),
    7: ("probe 7   MSV3 blade + FDC, Begonia", "fdc"),
    6: ("probe 6   CONTROL, desk, no soil", "fdc"),
}
CONTROLS = (6,)

# Each sensor's first valid moment in its pot (fleet.json pots.*.insertion_dates).
INSERTED = {
    4: datetime(2026, 9, 18, 20, 53, tzinfo=PACIFIC),
    5: datetime(2026, 9, 19, 20, 24, tzinfo=PACIFIC),
}

UNITS = {"fdc": "log-cap %", "am": "mV", "rc": "LSB"}
RATES = {"fdc": "%/C", "am": "mV/C", "rc": "LSB/C"}

# Probe 5 entered the pot 09-19 20:24 and was still equilibrating through 09-20;
# fleet.json's settling caveat says not to treat that day as an electrode property.
SKIP_DAYS = {(5, "2026-09-20")}

# Temperature-matched step windows. Both sit at the diurnal temperature minimum,
# one before the watering and one about 16 h after it, so the diurnal term very
# largely cancels. This is the only step estimator that works for a rig whose
# thermal swing is bigger than its watering response.
PREDAWN_BEFORE = (datetime(2026, 9, 22, 3, tzinfo=PACIFIC), datetime(2026, 9, 22, 6, tzinfo=PACIFIC))
PREDAWN_AFTER = (datetime(2026, 9, 23, 3, tzinfo=PACIFIC), datetime(2026, 9, 23, 6, tzinfo=PACIFIC))


def number(value):
    try:
        return float(value) if value else None
    except ValueError:
        return None


def median_bins(path, sensor, minutes=10):
    """Ten-minute medians of every raw field, with the same rejections as the
    2026-09-21 checkpoint: post-insertion only, FDC phase 0 only, probe 5's
    pre-fix scale-2 rows dropped."""
    buckets = defaultdict(lambda: defaultdict(list))
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            time = datetime.fromisoformat(row["timestamp_iso"].replace("Z", "+00:00")).astimezone(PACIFIC)
            if sensor in INSERTED and time < INSERTED[sensor]:
                continue
            if sensor == 5 and row["moisture_scale"] != "3":
                continue
            if RIGS[sensor][1] == "fdc":
                if row["tank_phase"] != "0":
                    continue
                counts = number(row["raw_fdc_counts"])
                if counts is None or not 1e6 < counts < 5e7:
                    continue
            key = time.replace(minute=time.minute // minutes * minutes, second=0, microsecond=0)
            values = buckets[key]
            for name in ("moisture", "raw_moisture_khz", "raw_fdc_counts", "board_temp_c", "raw_battery_mv"):
                value = number(row[name]) if name in row else None
                if value is not None:
                    values[name].append(value)
    return {time: {name: stats.median(v) for name, v in fields.items()}
            for time, fields in buckets.items()}


def wetness(sensor, bin_values, reference):
    """Signal in each rig's native unit, oriented so positive means wetter.

    FDC: 100*ln(C/Cref), where C is proportional to 1/f^2 and the count is
    proportional to f, so the value is -200*ln(count/ref). That is a LOG ratio.
    It coincides with percent only for small changes, and the watering steps
    here are not small -- probe 5's is a factor of several in capacitance.
    A log ratio is the right scale anyway: a slope measured in it means the same
    thing at any operating point, which a linear percent does not.

    AM: negated detector millivolts. RC: the MSV2 count as reported.
    """
    kind = RIGS[sensor][1]
    if kind == "fdc":
        counts = bin_values.get("raw_fdc_counts")
        return None if not counts else -200.0 * math.log(counts / reference)
    if kind == "am":
        millivolts = bin_values.get("raw_moisture_khz")
        return None if millivolts is None else -millivolts
    value = bin_values.get("moisture")
    return None if value is None else value


def series(rows, sensor, reference):
    out = []
    for time, values in sorted(rows.items()):
        signal = wetness(sensor, values, reference)
        if signal is not None:
            out.append((time, signal))
    return out


def window(points, start, end):
    return [(t, v) for t, v in points if start <= t < end]


def median_in(points, span):
    values = [v for _, v in window(points, *span)]
    return stats.median(values) if values else float("nan")


def residual(values, x):
    xm, ym = stats.mean(x), stats.mean(values)
    centered = [v - xm for v in x]
    denom = sum(v * v for v in centered)
    slope = sum(a * (b - ym) for a, b in zip(centered, values)) / denom if denom else 0.0
    return [value - ym - slope * at for at, value in zip(centered, values)]


def adjusted_slope(y, temp, hours):
    """Slope of y against temperature after removing a linear time trend from both.

    Daily autocorrelation makes the regression's own uncertainty misleading, so
    only the point estimate and the correlation are reported.
    """
    yd, td = residual(y, hours), residual(temp, hours)
    denom = sum(v * v for v in td)
    if not denom:
        return float("nan"), float("nan")
    covariance = sum(a * b for a, b in zip(yd, td))
    spread = math.sqrt(sum(v * v for v in yd) * denom)
    return covariance / denom, (covariance / spread if spread else float("nan"))


def diurnal_span(values, hours):
    """Peak-to-trough of the signal after a linear time trend is removed.

    slope x temperature-range assumes the response is instantaneous and linear
    in temperature. Probe 7's is neither -- its capacitance lags its die by
    hours, so a regression slope understates the excursion badly. And a +/- 3 h
    high-pass, being a high-pass, attenuates a smooth 24 h swing on purpose.
    This measures the excursion directly: what is left of the day once the
    dry-down is taken out is what a reader of the chart actually sees.
    """
    if len(values) < 40:
        return float("nan")
    detrended = residual(values, hours)
    return max(detrended) - min(detrended)


def short_term_noise(points, start, end):
    """Allan-style noise from successive 10-minute bins: sd(diff)/sqrt(2).

    A rolling-median residual understates this badly for a quantized channel
    such as the MSV2 RC count, because the median tracks the quantization.
    First differences do not.
    """
    values = [v for _, v in window(points, start, end)]
    if len(values) < 8:
        return float("nan")
    diffs = [b - a for a, b in zip(values, values[1:])]
    return stats.pstdev(diffs) / math.sqrt(2)


def high_pass(points, hours=3.0, minutes=10):
    """Remove a centred rolling median of width +/- `hours`.

    The post-watering dry-down is fast and curved, and it correlates with
    temperature because warm afternoons evaporate faster. Neither a linear nor a
    quadratic time term removes that, so both inflate the apparent thermal
    slope. High-passing removes any slow trend whatever its shape and leaves the
    diurnal wiggle, which is what a temperature coefficient means.
    """
    span = int(hours * 60 / minutes)
    values = [v for _, v in points]
    out = []
    for index in range(span, len(values) - span):
        baseline = stats.median(values[index - span:index + span + 1])
        out.append((points[index][0], values[index] - baseline))
    return out


def high_pass_slope(points, temps, start, end):
    """Thermal slope from high-passed signal against high-passed temperature."""
    band = [(t, v) for t, v in high_pass(points) if start <= t < end]
    band_temp = dict(p for p in high_pass(sorted(temps.items())) if start <= p[0] < end)
    pairs = [(v, band_temp[t]) for t, v in band if t in band_temp]
    if len(pairs) < 40:
        return float("nan"), float("nan")
    y = [a for a, _ in pairs]
    x = [b for _, b in pairs]
    ym, xm = stats.mean(y), stats.mean(x)
    denom = sum((b - xm) ** 2 for b in x)
    if not denom:
        return float("nan"), float("nan")
    covariance = sum((a - ym) * (b - xm) for a, b in pairs)
    spread = math.sqrt(sum((a - ym) ** 2 for a in y) * denom)
    return covariance / denom, (covariance / spread if spread else float("nan"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--suffix", default="2026-09-24")
    parser.add_argument("--temp-ref", type=int, default=4,
                        help="Sensor whose die temperature is the proxy (4 = in pot, 6 = room)")
    args = parser.parse_args()

    rows = {}
    for sensor in RIGS:
        path = args.directory / f"treebeard_alocasia_s{sensor}_{args.suffix}.csv"
        if path.exists():
            rows[sensor] = median_bins(path, sensor)

    # A fixed per-rig count reference keeps the log-capacitance scale comparable
    # across every window below.
    reference = {}
    for sensor, data in rows.items():
        pre = [v.get("raw_fdc_counts") for t, v in sorted(data.items())
               if WATERING - timedelta(hours=3) <= t < WATERING and v.get("raw_fdc_counts")]
        reference[sensor] = stats.median(pre) if pre else 1.0
    signals = {s: series(rows[s], s, reference[s]) for s in rows}

    print("=" * 78)
    print("MULTI-PROBE POT TEST - watering 2026-09-22 10:50 PDT, Alocasia AND Begonia")
    print("=" * 78)
    for sensor, points in signals.items():
        print(f"  s{sensor}: {len(points):5} bins, {points[0][0]:%m-%d %H:%M} .. {points[-1][0]:%m-%d %H:%M}")

    print()
    print("-- STEP RESPONSE ------------------------------------------------------")
    print("   peak     most extreme bin in the 90 min after watering, against the")
    print("            3 h before it")
    print("   24 h     same clock time next day, so the same point of the diurnal")
    print("            cycle, minus the same baseline")
    print("   predawn  03:00-06:00 on 09-23 minus 03:00-06:00 on 09-22: both at the")
    print("            daily temperature minimum, so the thermal term cancels. This")
    print("            is the one estimator that survives a large thermal swing, and")
    print("            it is what the figure of merit below uses.")
    print()
    step = {}
    for sensor, points in signals.items():
        base = [v for _, v in window(points, WATERING - timedelta(hours=3), WATERING)]
        burst = [v for _, v in window(points, WATERING, WATERING + timedelta(minutes=90))]
        if not base or not burst:
            continue
        baseline = stats.median(base)
        peak = max(burst, key=lambda v: abs(v - baseline)) - baseline
        day_later = median_in(points, (WATERING + timedelta(hours=23, minutes=30),
                                       WATERING + timedelta(hours=24, minutes=30))) - baseline
        predawn = median_in(points, PREDAWN_AFTER) - median_in(points, PREDAWN_BEFORE)
        step[sensor] = predawn
        print(f"  {RIGS[sensor][0]:38} peak {peak:+8.2f}  24 h {day_later:+8.2f}  "
              f"predawn {predawn:+8.2f}  {UNITS[RIGS[sensor][1]]}")
    temp_ref_points = sorted((t, v["board_temp_c"]) for t, v in rows[args.temp_ref].items()
                             if "board_temp_c" in v)
    print(f"  predawn windows, probe {args.temp_ref} die: "
          f"{median_in(temp_ref_points, PREDAWN_BEFORE):.2f} C before, "
          f"{median_in(temp_ref_points, PREDAWN_AFTER):.2f} C after")
    if 7 in rows:
        own = sorted((t, v["board_temp_c"]) for t, v in rows[7].items() if "board_temp_c" in v)
        print(f"  probe 7 own die: {median_in(own, PREDAWN_BEFORE):.2f} C before, "
              f"{median_in(own, PREDAWN_AFTER):.2f} C after")

    print()
    print("-- THERMAL COEFFICIENT, PER DAY ---------------------------------------")
    print(f"   temperature proxy = probe {args.temp_ref} die. Positive = reads wetter")
    print("   when warm. Two estimators:")
    print("     detrend  linear time term removed from signal and temperature")
    print("     hipass   both high-passed at +/- 3 h, so ANY slow trend is gone")
    print("   They disagree where a fast curved trend is present, which after")
    print("   watering is the dry-down itself. The figure of merit uses hipass.")
    days = ("2026-09-20", "2026-09-21", "2026-09-23", "2026-09-24")
    per_day = defaultdict(dict)
    for day in days:
        start = datetime.fromisoformat(day).replace(tzinfo=PACIFIC)
        end = start + timedelta(days=1)
        temps = {t: v["board_temp_c"] for t, v in rows[args.temp_ref].items()
                 if start <= t < end and "board_temp_c" in v}
        if not temps:
            continue
        tag = "pre-water " if day < "2026-09-22" else "post-water"
        print(f"  {day} ({tag}, die {min(temps.values()):.2f}..{max(temps.values()):.2f} C)")
        for sensor, points in signals.items():
            pairs = [(t, v, temps[t]) for t, v in window(points, start, end) if t in temps]
            if len(pairs) < 40:
                continue
            values = [v for _, v, _ in pairs]
            slope, _ = adjusted_slope(values, [c for _, _, c in pairs],
                                      [t.hour + t.minute / 60 for t, _, _ in pairs])
            hp_slope, hp_r = high_pass_slope(points, temps, start, end)
            wobble = diurnal_span(values, [t.hour + t.minute / 60 for t, _, _ in pairs])
            note = "  (settling, excluded)" if (sensor, day) in SKIP_DAYS else ""
            # A quantized channel high-passes to identically zero, which is not
            # a measurement of zero thermal response. nan r flags that.
            if (sensor, day) not in SKIP_DAYS and not math.isnan(hp_slope) and not math.isnan(hp_r):
                per_day[sensor][day] = (hp_slope, max(temps.values()) - min(temps.values()), wobble)
            print(f"    {RIGS[sensor][0]:38} detrend {slope:+8.3f}  hipass {hp_slope:+8.3f} "
                  f"{RATES[RIGS[sensor][1]]:6} r={hp_r:+.3f}  "
                  f"diurnal swing {wobble:7.2f}{note}")

    print()
    print("-- CROSS-ARCHITECTURE FIGURE OF MERIT ---------------------------------")
    print("   Each rig's daily thermal artifact and short-term noise divided by its")
    print("   OWN watering step, so every architecture lands on one scale.")
    print("   Two artifact measures, because they bracket the truth:")
    print("     hipass   |slope| x that day's die range -- trend-immune, but a")
    print("              high-pass attenuates a smooth 24 h swing, so it is a FLOOR")
    print("     diurnal  detrended peak-to-trough of the day -- what the chart")
    print("              actually shows, but any curved dry-down leaks in, so it")
    print("              is a CEILING")
    print("   Both are medians over the usable days. Noise: 00:00-05:00, 09-23/24.")
    print()
    print(f"  {'rig':40} {'step':>8} {'hipass':>8} {'diurnal':>8} {'noise':>7} {'SNR':>7}")
    quiet = [(datetime(2026, 9, 23, 0, tzinfo=PACIFIC), datetime(2026, 9, 23, 5, tzinfo=PACIFIC)),
             (datetime(2026, 9, 24, 0, tzinfo=PACIFIC), datetime(2026, 9, 24, 5, tzinfo=PACIFIC))]
    for sensor, points in signals.items():
        if sensor in CONTROLS or sensor not in step or not per_day[sensor]:
            continue
        artifact = stats.median([abs(s) * rng for s, rng, _ in per_day[sensor].values()])
        wobble = stats.median([w for _, _, w in per_day[sensor].values()])
        noises = [n for n in (short_term_noise(points, a, b) for a, b in quiet) if not math.isnan(n)]
        noise = stats.median(noises) if noises else float("nan")
        size = abs(step[sensor])
        print(f"  {RIGS[sensor][0]:40} {size:8.2f} {100 * artifact / size:7.1f}% "
              f"{100 * wobble / size:7.1f}% {100 * noise / size:6.2f}% "
              f"{size / wobble:7.2f}")
    print()
    print("  step     = temperature-matched predawn watering step, native units")
    print("  hipass   = floor estimate of one day of thermal artifact, % of that step")
    print("  diurnal  = ceiling estimate of the same, % of that step")
    print("  noise    = 10-minute-bin noise, sd(diff)/sqrt(2), % of that step")
    print("  SNR      = step divided by the DIURNAL artifact, the pessimistic one.")
    print("             Below about 1, one day of sun moves the reading as far as a")
    print("             whole watering does, and the watering is not visible.")
    print()
    for sensor in CONTROLS:
        if per_day.get(sensor):
            artifact = stats.median([abs(s) * rng for s, rng, _ in per_day[sensor].values()])
            wobble = stats.median([w for _, _, w in per_day[sensor].values()])
            slopes = [s for s, _, _ in per_day[sensor].values()]
            print(f"  control  {RIGS[sensor][0]:38} {stats.median(slopes):+7.3f} %/C, "
                  f"hipass {artifact:5.2f}, diurnal {wobble:5.2f} log-cap % per day")

    print()
    print("-- HEAD TO HEAD: ONE VARIABLE AT A TIME -------------------------------")
    print("   Each pair shares a pot, days and readout, so the electrode is the only")
    print("   variable. Days are restricted to ones where BOTH members are settled")
    print("   and in the same moisture state.")

    def penalty(sensor, days):
        usable = [per_day[sensor][d] for d in days if d in per_day[sensor]]
        if not usable or sensor not in step or not step[sensor]:
            return float("nan"), float("nan"), float("nan")
        wobble = stats.median([w for _, _, w in usable])
        return abs(step[sensor]), wobble, 100 * wobble / abs(step[sensor])

    for title, v12, blade, days, why in (
        ("FDC readout, shared Alocasia pot", 4, 5, ("2026-09-23", "2026-09-24"),
         "probe 5 settles only after 09-20, and only these two days have both "
         "probes in the same post-watering soil"),
        ("AM readout, shared Alocasia pot", 3, 9,
         ("2026-09-20", "2026-09-21", "2026-09-23", "2026-09-24"),
         "both in this pot throughout, so every day counts"),
    ):
        print()
        print(f"  {title}")
        print(f"    days: {', '.join(days)}")
        print(f"    ({why})")
        rows_out = []
        for sensor in (v12, blade):
            size, wobble, share = penalty(sensor, days)
            rows_out.append((sensor, size, wobble, share))
            print(f"    {RIGS[sensor][0]:38} step {size:8.2f}  diurnal {wobble:7.2f}  "
                  f"= {share:6.2f}% of step")
        (_, size_a, wob_a, share_a), (_, size_b, wob_b, share_b) = rows_out
        print(f"    blade / v1.2:  signal x{size_b / size_a:.2f},  "
              f"thermal artifact x{wob_b / wob_a:.2f},  "
              f"NET x{share_b / share_a:.2f}")
        verdict = "better" if share_b < share_a else "worse"
        print(f"    -> with this readout the blade is {abs(share_b / share_a):.2f}x "
              f"the normalized artifact, i.e. {verdict}")
    print()
    print("  If the electrode contributed an additive term independent of readout,")
    print("  the two NET ratios would match. Compare them.")

    print()
    print("-- SAME ELECTRODE AND READOUT, DIFFERENT POT --------------------------")
    print("   probe 5 and probe 7 are both MSV3 blade + FDC2214. Only the pot and")
    print("   the mounting differ.")
    for sensor in (5, 7):
        size, wobble, share = penalty(sensor, ("2026-09-23", "2026-09-24"))
        print(f"  {RIGS[sensor][0]:38} step {size:8.2f}  diurnal {wobble:7.2f}  "
              f"= {share:7.2f}% of step")

    print()
    print("-- DOES THE THERMAL COEFFICIENT DEPEND ON SOIL WATER? -----------------")
    print("   Own-die slope before and after the watering. The no-soil control is")
    print("   the discriminator: if the board were the dominant thermal path, it")
    print("   would move with the others.")
    for sensor in (4, 5, 7, 6):
        if sensor not in rows:
            continue
        line = f"  {RIGS[sensor][0]:38}"
        for day in ("2026-09-20", "2026-09-21", "2026-09-23", "2026-09-24"):
            start = datetime.fromisoformat(day).replace(tzinfo=PACIFIC)
            end = start + timedelta(days=1)
            own = {t: v["board_temp_c"] for t, v in rows[sensor].items()
                   if start <= t < end and "board_temp_c" in v}
            pairs = [(t, v) for t, v in window(signals[sensor], start, end) if t in own]
            if len(pairs) < 40:
                line += f"  {day[5:]}: {'n/a':>7}"
                continue
            slope, _ = adjusted_slope([v for _, v in pairs], [own[t] for t, _ in pairs],
                                      [t.hour + t.minute / 60 for t, _ in pairs])
            mark = "*" if (sensor, day) in SKIP_DAYS else " "
            line += f"  {day[5:]}:{slope:+7.3f}{mark}"
        print(line)
    print("  (* excluded elsewhere as post-insertion settling)")

    print()
    print("-- DRY-DOWN AFTER WATERING --------------------------------------------")
    print("   Percent of the predawn step already given back, at each predawn since.")
    for sensor, points in signals.items():
        if sensor in CONTROLS or sensor not in step or math.isnan(step[sensor]):
            continue
        base = median_in(points, PREDAWN_BEFORE)
        line = f"  {RIGS[sensor][0]:38}"
        for day in (23, 24):
            span = (datetime(2026, 9, day, 3, tzinfo=PACIFIC), datetime(2026, 9, day, 6, tzinfo=PACIFIC))
            value = median_in(points, span)
            if math.isnan(value):
                line += f"  09-{day}: {'n/a':>6}"
            else:
                line += f"  09-{day}: {100 * (1 - (value - base) / step[sensor]):5.1f}%"
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
