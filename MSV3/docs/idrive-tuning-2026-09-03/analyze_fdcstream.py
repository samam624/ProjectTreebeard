#!/usr/bin/env python
"""Reproduce the 2026-09-03 IDRIVE noise analysis from the captured raw streams.

Usage:  python analyze_fdcstream.py [fdcstream_raw.csv]

Why the level-splitting matters, and why a plain standard deviation is the wrong
tool here: DATA_CH0 switches between two discrete levels roughly 5,500 counts
apart (see MSV3_BENCH_TEST_PLAN_2026-08-21.md, Test 4, "DATA_CH0 switches between
two discrete levels"). That artifact is 5-6x larger than the real measurement
noise, so it dominates any whole-stream statistic:

  - raw SD reports NO difference between IDRIVE codes (1.03x)
  - SD-of-successive-differences reports the difference BACKWARDS (0.80x)
  - only a within-level comparison recovers the true 1.40x

Any future noise comparison on this sensor must split levels first, or it will
draw the wrong conclusion.
"""
import csv
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path


def robust_sigma(xs):
    """SD of successive differences / sqrt(2) -- rejects slow drift, not level jumps."""
    d = [xs[i + 1] - xs[i] for i in range(len(xs) - 1)]
    return st.pstdev(d) / (2 ** 0.5)


def split_levels(xs):
    """Bimodal split at the midpoint of the range.

    Valid here only because the separation (~5,500 counts) is ~18x the
    within-level SD (~300), so the two clusters do not overlap. Re-check that
    assumption before reusing this on a different dataset.
    """
    mid = (max(xs) + min(xs)) / 2
    return [x for x in xs if x >= mid], [x for x in xs if x < mid]


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else
                Path(__file__).parent / "fdcstream_raw.csv")
    streams = defaultdict(list)
    with path.open() as f:
        for row in csv.DictReader(f):
            streams[int(row["idrive_code"])].append(int(row["data_ch0"]))

    levels = {}
    for code in sorted(streams):
        xs = streams[code]
        hi, lo = split_levels(xs)
        levels[code] = (hi, lo)
        print(f"\n=== IDRIVE {code}  (n={len(xs)}) ===")
        print(f"  mean           {st.mean(xs):,.0f}")
        print(f"  raw SD         {st.stdev(xs):8.1f}   <- dominated by level switching")
        print(f"  robust sigma   {robust_sigma(xs):8.1f}   <- also contaminated")
        print(f"  HIGH  n={len(hi):3d}  mean {st.mean(hi):,.0f}  SD {st.stdev(hi):6.1f}")
        print(f"  LOW   n={len(lo):3d}  mean {st.mean(lo):,.0f}  SD {st.stdev(lo):6.1f}")
        gap = st.mean(hi) - st.mean(lo)
        print(f"  level gap      {gap:,.0f} counts ({gap / st.mean(xs) * 100:.4f}%)"
              f"   time in LOW {len(lo) / len(xs) * 100:.1f}%")

    if 16 in levels and 25 in levels:
        hi16, hi25 = levels[16][0], levels[25][0]
        f_stat = st.stdev(hi16) ** 2 / st.stdev(hi25) ** 2
        print("\n=== code 16 vs 25 ===")
        print(f"  within-level SD ratio  {st.stdev(hi16) / st.stdev(hi25):.2f}x  "
              f"(F={f_stat:.2f}, df=({len(hi16) - 1},{len(hi25) - 1}), crit~1.22 at 95%)")
        shift = st.mean(streams[16]) - st.mean(streams[25])
        print(f"  mean shift             {shift:,.0f} counts "
              f"({shift / st.mean(streams[16]) * 100:.3f}%)")
        print(f"  HIGH-level-only shift  {st.mean(hi16) - st.mean(hi25):,.0f} counts "
              f"(matches, so not a level-occupancy artifact)")


if __name__ == "__main__":
    main()
