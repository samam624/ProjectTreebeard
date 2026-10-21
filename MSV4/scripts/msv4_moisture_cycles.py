"""Does a candidate read the same grams of water the same way from one watering cycle to the next?

msv4_moisture_competition.py fits one line per candidate through every weighing, so a reading that
shifts between cycles shows up only as a gap to the scale on the plot, and the plot cannot tell a
step at a watering from a drift that keeps growing. This splits the weighings by watering cycle:

  A  each weighing's gap to the scale (candidate grams minus scale grams, the plot's single fit),
     plus how far contact capacitance Cm sits from the median of the five candidates that are not
     built on Cm;
  B  each later cycle against the cycle before it at the SAME scale grams: the later reading minus
     the earlier one interpolated between its weighings, in grams at the single-fit slope. A step
     shows as a constant column, a drift as one that keeps growing;
  C  sensitivity (units per gram) in each cycle over the water range both cycles cover, so a gain
     change is not mistaken for an offset. Fitting each whole cycle instead compares a dry-end
     slope with a wet-end one and overstates the change (Cm x1.95 against x1.24 like for like on
     2026-10-21).

Temperature-corrected candidates by default, as on the plot; --raw for the uncorrected view.

    python MSV4/scripts/msv4_moisture_cycles.py data/sensor_logs/<sensor10 history>.csv [--raw]
"""
from __future__ import annotations

import argparse
import statistics
from pathlib import Path

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

SHORT = (("number", "number"), ("behind the mask", "R mask"), ("resistance", "R plain"), ("direct", "Cd-air"),
         ("soil capacitance", "Csoil"), ("coupled", "Cm+Cd-air"), ("contact", "Cm"))


def short(name: str) -> str:
    return next(label for key, label in SHORT if key in name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--raw", action="store_true", help="no temperature correction")
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    if not args.raw:
        import msv4_moisture_tempcorr as tempcorr
        comp.CANDIDATES = tempcorr.corrected_candidates(comp.dry_days(rows, waterings)[0])[0]
    merged = comp.merge_repeats(weighings, waterings)
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    dry = min(p["grams"] for p in pairs)

    def cycle(when) -> int:
        return sum(1 for w in waterings if w <= when)

    names = [name for name, _, _ in comp.CANDIDATES]
    fits = {}
    for name, _, value in comp.CANDIDATES:
        points = []
        for p in pairs:
            data = [v for v in (value(r) for r in p["rows"]) if v is not None]
            if len(data) >= comp.MIN_PAIR_READINGS:
                points.append((p["time"], p["grams"] - dry, statistics.median(data)))
        slope, intercept = statistics.linear_regression([g for _, g, _ in points], [v for _, _, v in points])
        fits[name] = (slope, intercept, points)
    cycles = sorted({cycle(p["time"]) for p in pairs} - {0})
    print(f"{len(pairs)} weighings with a dry saucer, {len(waterings)} watering(s); water = grams above {dry:.0f} g"
          f"{'' if args.raw else ', temperature-corrected'}")

    print("\nA  candidate minus scale, grams of water (one fit through every weighing, as on the plot)")
    print(f"{'weighing':13}{'cycle':>6}{'tip C':>7}{'water':>7}" + "".join(f"{short(n):>10}" for n in names)
          + f"{'Cm-others':>11}")
    for p in pairs:
        if cycle(p["time"]) == 0:
            continue
        gap = {}
        for n in names:
            slope, intercept, points = fits[n]
            match = [v for t, _, v in points if t == p["time"]]
            gap[n] = (match[0] - intercept) / slope - (p["grams"] - dry) if match else None
        tips = [r["tip"] for r in p["rows"] if r["tip"] is not None]
        cm = next(gap[n] for n in names if short(n) == "Cm")
        others = [gap[n] for n in names if short(n) not in ("Cm", "Cm+Cd-air") and gap[n] is not None]
        print(f"{p['time']:%m-%d %H:%M}  {cycle(p['time']):>6}{statistics.median(tips) if tips else float('nan'):7.2f}"
              f"{p['grams'] - dry:7.0f}" + "".join(f"{gap[n]:+10.0f}" if gap[n] is not None else f"{'-':>10}" for n in names)
              + (f"{cm - statistics.median(others):+11.0f}" if cm is not None and others else ""))

    for later in cycles[1:]:
        earlier = later - 1
        print(f"\nB  cycle {later} minus cycle {earlier} at the same scale grams, in grams (single-fit slope)")
        heads = [(t, g) for t, g, _ in fits[names[0]][2] if cycle(t) == later]
        print(f"{'':12}" + "".join(f"{f'{t:%m-%d %H:%M}':>13}" for t, _ in heads))
        print(f"{'water':12}" + "".join(f"{g:13.0f}" for _, g in heads))
        for n in names:
            slope, _, points = fits[n]
            before = sorted((g, v) for t, g, v in points if cycle(t) == earlier)
            cells = []
            for t, g, v in ((t, g, v) for t, g, v in points if cycle(t) == later):
                lo = [x for x in before if x[0] <= g]
                hi = [x for x in before if x[0] >= g]
                if not lo or not hi:
                    cells.append(f"{'-':>13}")
                    continue
                (g0, v0), (g1, v1) = lo[-1], hi[0]
                expected = v0 if g1 == g0 else v0 + (v1 - v0) * (g - g0) / (g1 - g0)
                cells.append(f"{(v - expected) / slope:+13.0f}")
            print(f"{short(n):12}" + "".join(cells))

        settled = [(t, g) for t, g, _ in fits[names[0]][2]
                   if cycle(t) == later and comp.settling(t, waterings) is None]
        low = max(min(g for _, g in settled), min(g for t, g, _ in fits[names[0]][2] if cycle(t) == earlier))
        high = min(max(g for _, g in settled), max(g for t, g, _ in fits[names[0]][2] if cycle(t) == earlier))
        print(f"\nC  units per gram over {low:.0f}-{high:.0f} g, the range both cycles cover "
              f"(cycle {later} without weighings inside the settle window after its watering)")
        for n in names:
            _, _, points = fits[n]
            sens = []
            for c in (earlier, later):
                sel = [(g, v) for t, g, v in points if cycle(t) == c and low <= g <= high
                       and (c == earlier or comp.settling(t, waterings) is None)]
                sens.append((statistics.linear_regression(*zip(*sel))[0], len(sel)) if len(sel) >= 3 else (float("nan"), len(sel)))
            (s0, n0), (s1, n1) = sens
            print(f"{short(n):12} cycle {earlier} {s0:10.4f} ({n0})   cycle {later} {s1:10.4f} ({n1})   ratio {s1 / s0:5.2f}")


if __name__ == "__main__":
    main()
