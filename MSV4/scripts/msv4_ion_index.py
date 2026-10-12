#!/usr/bin/env python
"""MSV4: soil conductance against soil capacitance, as a check on what a step in the reading is.

Water raises the soil's conductance and its capacitance together; ions raise the conductance
alone. Both are measured through the same electrode, so their ratio does not depend on how much
of the blade the soil touches. With Cs and Rs the soil's own capacitance and resistance (the
arithmetic of MSV4_EXPERIMENTAL_MOISTURE_2026-10-10.md) and G25 = 1/Rs corrected to 25 C:

    ion index  k = G25 / (Cs - C0)        [uS/pF]

C0 is where the line through the dry and the watered readings meets zero conductance (Hilhorst
2000 has the same offset for bulk conductivity against permittivity). A watering should move a
reading along that line and leave k alone; a fertilizing should raise k. On the stored number,
10000 * log10(conductance), a change of k by a factor f is a shift of 10000 * log10(f).

  python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 10 --raw --since-hours 120 --csv data/sensor_logs/msv4_sensor10.csv
  python MSV4/scripts/msv4_ion_index.py data/sensor_logs/msv4_sensor10.csv --from "2026-10-09 19:00" --watered "2026-10-10 19:14"

One watering fixes the line; it cannot test it. The scatter of k inside each of the two groups is
the measured part.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as m

EC_TEMPCO_PER_C = 0.02      # the receiver's MSV4_EC_TEMPCO_PER_C
EPS0 = 8.854e-12
EPS_WATER = 80.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", type=Path)
    parser.add_argument("--from", dest="start", required=True, help="local time; leave the settling after planting out")
    parser.add_argument("--watered", required=True, help="local time of the watering")
    parser.add_argument("--block-hours", type=float, default=3)
    args = parser.parse_args()

    start = datetime.fromisoformat(args.start).astimezone()
    watered = datetime.fromisoformat(args.watered).astimezone()
    rows = [r for r in m.load(args.csv, None)
            if not r["open"] and r["cd_soil"] and r["cm"] > 0 and r["tip"] is not None and r["time"] >= start]

    when, data = [], []
    for r in rows:
        cx, cm = r["cd_soil"], r["cm"]
        cs = cx * (1 + cx / cm)
        rs = r["rsoil"] * (cm / (cm + cx)) ** 2
        g25 = 1e6 / rs / (1 + EC_TEMPCO_PER_C * (r["tip"] - 25.0))
        when.append(r["time"])
        data.append((cs, g25, rs * cs * 1e-6, r["stored"], r["tip"]))
    when, data = np.array(when), np.array(data)

    print("%-14s %5s %8s %9s %10s %8s %6s" % ("block from", "n", "Cs pF", "G25 uS", "Rs*Cs us", "stored", "tip C"))
    blocks, t = [], start
    while t < when[-1]:
        chosen = data[(when >= t) & (when < t + timedelta(hours=args.block_hours))]
        if len(chosen) >= 20:
            median = np.median(chosen, axis=0)
            blocks.append((t, median))
            print("%-14s %5d %8.2f %9.1f %10.4f %8.0f %6.1f" % (t.strftime("%m-%d %H:%M"), len(chosen), *median))
        t += timedelta(hours=args.block_hours)

    at = np.array([b[0] for b in blocks])
    values = np.array([b[1] for b in blocks])
    dry, wet = at < watered - timedelta(hours=args.block_hours), at > watered + timedelta(hours=2)
    x, y = np.concatenate([values[dry, 0], values[wet, 0]]), np.concatenate([values[dry, 1], values[wet, 1]])
    slope, intercept = np.polyfit(x, y, 1)
    c0 = -intercept / slope
    k = values[:, 1] / (values[:, 0] - c0)
    print(f"\nline: G25 = {slope:.2f} uS/pF * (Cs - {c0:.2f} pF), rms off it {np.sqrt(((y - intercept - slope * x) ** 2).mean()):.1f} uS over {len(x)} blocks")
    for label, chosen in (("before the watering", dry), ("after it", wet)):
        print(f"ion index k {label}: {k[chosen].mean():.2f} uS/pF, sd {k[chosen].std():.2f} ({100 * k[chosen].std() / k[chosen].mean():.1f} %), {chosen.sum()} blocks")
    print("  the two means agree by construction (C0 is fitted through both groups); the sd is what was measured")
    print("after, in order: " + " ".join("%.2f" % v for v in k[wet]))
    print(f"as a soil-water conductivity, k * eps0 * {EPS_WATER:.0f}: {slope * 1e6 * EPS0 * EPS_WATER:.3f} S/m (Cs is known to come out low, so this is high)")
    step = np.median(values[wet, 3]) - np.median(values[dry, 3])
    print(f"stored number: watering step {step:.0f}; a doubling of k would be +3010, {100 * 3010 / step:.0f} % of that step")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
