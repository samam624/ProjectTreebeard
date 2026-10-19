"""Temperature swing on raw sweeps: every electrode model fitted to every logged sweep.

Why: the 2026-10-15 raw-sweep test (MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md) logged one
bring-up-console `spectrum ab` every 30 s while a cup of tap water was heated (glass in a 125 F
bowl) and left to cool in room air. Fitting each model to each sweep, against the tip thermistor,
says which part of the electrode carries the temperature, and whether anything failed to come back.

Input is the logger's file: a line `=== <ISO time>` before each console session, then the
console's own print of the sweep (msv4_raw_sweep_models.py parses the points). Lines the BLE
console lost leave a sweep short; such sweeps are skipped.

    python MSV4/scripts/msv4_rawsweep_swing.py data/sensor_logs/msv4_rawsweep_2026-10-15/monitor.txt \\
        --rs 3300 --cp 8.75 --cd0 13.71 --cache data/sensor_logs/msv4_rawsweep_2026-10-15/swing_fits.csv

Prints, for each model's water R and mask capacitance:
  - a joint fit ln(value) = a + b (T - 25) + c t + d step over the settled pre-swing window and the
    slow cooling leg (the fast heating leg is left out: the tip lagged the water there), with a
    step term at the 15:16 glass move
  - the loop closure: the return point corrected to the pre-swing temperature with b
"""
from __future__ import annotations

import argparse
import csv
import re
from datetime import datetime
from pathlib import Path

import numpy as np

import msv4_raw_sweep_models as models

DAY = "2026-10-15"
PRE = ("14:44:00", "14:54:30")      # settled in tap, before the glass went into the hot bowl
COOL = ("15:16:30", "16:48:30")     # out of the bowl at the 15:16 move, cooling in room air
RETURN = ("16:43:00", "16:48:30")   # the last minutes before the 27 C return target
SERIES = (("lumped", "r_ohm"), ("tline", "r_ohm"), ("lumped", "mask_c_pf"), ("tline", "mask_c_pf"),
          ("two-branch", "mask_c_pf"))


def fit_log(path: Path, rs: float, cp: float, cd0: float, cache: Path) -> list[dict]:
    """One row per (sweep, model); sweeps already in the cache are not refitted."""
    rows = []
    if cache.exists():
        with cache.open(newline="") as f:
            rows = list(csv.DictReader(f))
    done = {r["time"] for r in rows}
    fresh = []
    for block in path.read_text(encoding="utf-8-sig", errors="replace").split("=== ")[1:]:
        stamp = block.split("\n", 1)[0].strip()
        tip = re.search(r"tip ([\d.]+) C", block)
        sweeps = models.parse_sweeps(block)
        if stamp in done or tip is None or len(sweeps) != 1 or len(sweeps[0]["n"]) != len(models.PROBE_N):
            continue
        for name, fit in models.fit_all(sweeps[0], rs, cp, cd0).items():
            d = models.derived(name, fit["p"], cd0)
            fresh.append({"time": stamp, "tip_c": tip.group(1), "model": name, "rms": f"{fit['rms']:.6f}",
                          "mask_c_pf": f"{d['mask C'] * 1e12:.3f}", "r_ohm": f"{d['R']:.2f}",
                          "cd_pf": f"{d['Cd'] * 1e12:.3f}" if "Cd" in d else ""})
    if fresh:
        new = not cache.exists()
        with cache.open("a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(fresh[0]))
            if new:
                w.writeheader()
            w.writerows(fresh)
    return rows + fresh


def inside(row: dict, window: tuple[str, str]) -> bool:
    return window[0] <= row["time"][11:19] < window[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("monitor", type=Path)
    parser.add_argument("--rs", type=float, required=True, help="R15 in ohms")
    parser.add_argument("--cp", type=float, required=True, help="Cp in pF (from the open sweeps)")
    parser.add_argument("--cd0", type=float, required=True, help="the board's Cd in air, pF")
    parser.add_argument("--cache", type=Path, required=True, help="CSV of per-sweep fits, appended to")
    args = parser.parse_args()

    rows = fit_log(args.monitor, args.rs, args.cp * 1e-12, args.cd0 * 1e-12, args.cache)
    t0 = datetime.fromisoformat(f"{DAY}T{PRE[0]}")
    print(f"{len({r['time'] for r in rows})} sweeps fitted; joint fit over {PRE[0]}-{PRE[1]} and {COOL[0]}-{COOL[1]}")
    print(f"{'model':11} {'value':10} {'%/C':>7} {'%/min':>7} {'step %':>7} {'scatter %':>9}   "
          f"{'pre':>14} {'return, at pre T':>18} {'closure':>8}")
    for model, key in SERIES:
        sel = [r for r in rows if r["model"] == model and (inside(r, PRE) or inside(r, COOL))]
        temp = np.array([float(r["tip_c"]) for r in sel])
        mins = np.array([(datetime.fromisoformat(r["time"]) - t0).total_seconds() / 60 for r in sel])
        step = np.array([1.0 if inside(r, COOL) else 0.0 for r in sel])
        y = np.log([float(r[key]) for r in sel])
        a = np.c_[np.ones_like(temp), temp - 25.0, mins, step]
        c = np.linalg.lstsq(a, y, rcond=None)[0]
        scatter = 100 * float(np.std(y - a @ c))
        pre = [r for r in rows if r["model"] == model and inside(r, PRE)]
        ret = [r for r in rows if r["model"] == model and inside(r, RETURN)]
        v_pre, t_pre = (np.median([float(r[k]) for r in pre]) for k in (key, "tip_c"))
        v_ret, t_ret = (np.median([float(r[k]) for r in ret]) for k in (key, "tip_c"))
        back = v_ret * (1 + c[1] * (t_pre - t_ret))
        print(f"{model:11} {key:10} {100 * c[1]:+7.2f} {100 * c[2]:+7.3f} {100 * c[3]:+7.1f} {scatter:9.2f}   "
              f"{v_pre:7.1f} @{t_pre:5.2f}C {back:11.1f} @{t_pre:5.2f}C {100 * (back / v_pre - 1):+7.1f}%")


if __name__ == "__main__":
    main()
