"""How many grams does a reading miss by when its calibration did not see the weighings it is scored on?

Fitting a line through the same weighings it is then compared with always looks good (that is
"in-sample"). The honest test is cross-cycle: calibrate on one watering cycle's dry-dish weighings,
convert the other cycle's readings to grams, and report converted reading minus scale. Bias is the
mean error (a sign means the reading reads wetter than the scale), rms and worst are in grams.
The scale repeats to about 8 g.

    python MSV4/scripts/msv4_moisture_crosscycle.py data/sensor_logs/<sensor10 history>.csv
"""
from __future__ import annotations

import argparse
import math
import statistics as st
from pathlib import Path

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp
import msv4_moisture_tempcorr as tc


def stats(err):
    return f"bias {st.mean(err):+6.1f}  rms {math.sqrt(st.mean(e * e for e in err)):5.1f}  worst {max(err, key=abs):+6.1f}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    rows = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    weighings, waterings = comp.load_pot(rows[0]["sensor"])
    merged = comp.merge_repeats(weighings, waterings)
    days = comp.dry_days(rows, waterings)
    cands = tc.corrected_candidates(days[0])[0]
    pairs = [p for p in comp.pair(rows, merged, waterings) if not p["excluded"] and p["saucer"] is False]
    cycle = lambda p: 1 if waterings[0] <= p["time"] < waterings[1] else (2 if p["time"] >= waterings[1] else 0)

    def points(value, k):
        out = []
        for p in pairs:
            if cycle(p) != k:
                continue
            d = [x for x in map(value, p["rows"]) if x is not None]
            if len(d) >= comp.MIN_PAIR_READINGS:
                out.append((p["grams"], st.median(d)))
        return out

    print("error in grams = converted reading minus scale; the scale repeats to ~8 g")
    for name, _, value in cands:
        c1, c2 = points(value, 1), points(value, 2)
        if len(c1) < 4 or len(c2) < 4:
            continue
        fit = lambda pp: st.linear_regression([g for g, _ in pp], [v for _, v in pp])
        err = lambda pp, line: [(v - line[1]) / line[0] - g for g, v in pp]
        print(f"\n{name}")
        print("  fitted on both, scored on both (in-sample):", stats(err(c1 + c2, fit(c1 + c2))))
        print("  fitted on cycle 1, scored on cycle 2:      ", stats(err(c2, fit(c1))))
        print("  fitted on cycle 2, scored on cycle 1:      ", stats(err(c1, fit(c2))))


if __name__ == "__main__":
    main()
