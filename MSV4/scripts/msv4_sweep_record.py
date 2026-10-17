"""A day of cloud raw sweeps, model by model: fit quality, temperature, drift, steps and scatter.

Why: msv4_cloud_sweeps.py fits every electrode model to every stored sweep; this joins those fits to
the probe's own history (tip temperature, board-end temperature) and says how each model's numbers
behaved over the record, so a model can be judged on more than one averaged capture
(MSV4/docs/MSV4_SENSOR11_SWEEP_RECORD_2026-10-16.md).

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 24 --rs 3300 --cp 8.75 --cd0 13.71 \\
        --out data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-16.csv
    python cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 11 --raw \\
        --since-hours 24 --csv data/sensor_logs/msv4_sensor11_history_2026-10-16.csv
    python MSV4/scripts/msv4_sweep_record.py --fits data/sensor_logs/msv4_sensor11_sweep_fits_2026-10-16.csv \\
        --history data/sensor_logs/msv4_sensor11_history_2026-10-16.csv \\
        --step "2026-10-16 11:20" --flat "2026-10-16 04:00" "2026-10-16 11:20"
    python MSV4/scripts/msv4_sweep_record.py --self-test

Times are local. The joint fit is ln(p) = a + step + b*tip + c*hours over the whole record; with tip
and time correlated inside each leg it is provisional until a record has a long leg at each sign.
"""
from __future__ import annotations

import argparse
import csv
import sys
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as history

MODELS = ("lumped", "cpe", "two-branch", "tline", "lumped-fixed")
PARAMS = (("lumped_r_ohm", "lumped R (firmware's)"), ("lumped_mask_pf", "lumped mask C"),
          ("lumped_cd_pf", "lumped Cd"), ("cpe_mask_pf", "CPE mask C"), ("cpe_n", "CPE n"),
          ("tline_r_ohm", "RC-line R"), ("tline_mask_pf", "RC-line mask C"),
          ("two-branch_r_ohm", "two-branch R"), ("two-branch_mask_pf", "two-branch mask C"))
LINEAR = {"cpe_n"}  # fitted on the value itself; everything else on its log, in %


def local(text: str) -> datetime:
    text = text.replace(" ", "T")
    if text.endswith("+00"):
        text += ":00"
    when = datetime.fromisoformat(text)
    return when.astimezone() if when.tzinfo else when.astimezone()


def join(fits: Path, hist: Path) -> list[dict]:
    measured = history.load(hist, None)
    rows = []
    with fits.open(encoding="utf-8") as handle:
        for record in csv.DictReader(handle):
            when, wake = local(record["observed_at"]), int(record["wake"])
            match = [h for h in measured if h["wake"] == wake and abs((h["time"] - when).total_seconds()) < 600]
            if not match:
                continue
            row = {k: float(v) for k, v in record.items() if k not in ("observed_at", "sensor", "wake") and v}
            row.update(time=when, wake=wake, tip=match[0]["tip"], die=match[0]["die"], fw_r=match[0]["rsoil"])
            rows.append(row)
    return rows


def regress(rows: list[dict], key: str, step: datetime | None) -> tuple[np.ndarray, np.ndarray, float]:
    """Coefficients (const, [step], tip, hours), their standard errors and the residual rms."""
    t0 = rows[0]["time"]
    y = np.array([r[key] if key in LINEAR else np.log(r[key]) for r in rows])
    cols = [np.ones(len(rows))]
    if step is not None and len({r["time"] > step for r in rows}) == 2:
        cols.append(np.array([1.0 if r["time"] > step else 0.0 for r in rows]))
    cols += [np.array([r["tip"] for r in rows]), np.array([(r["time"] - t0).total_seconds() / 3600 for r in rows])]
    x = np.column_stack(cols)
    coef, *_ = np.linalg.lstsq(x, y, rcond=None)
    resid = y - x @ coef
    cov = np.linalg.inv(x.T @ x) * (resid @ resid) / (len(y) - x.shape[1])
    return coef, np.sqrt(np.diag(cov)), float(np.std(resid))


def scale(key: str) -> float:
    return 1.0 if key in LINEAR else 100.0


def report(rows: list[dict], step: datetime | None, flat: tuple[datetime, datetime] | None) -> None:
    tips = [r["tip"] for r in rows]
    print(f"{len(rows)} sweeps {rows[0]['time']:%m-%d %H:%M} to {rows[-1]['time']:%m-%d %H:%M}, "
          f"tip {min(tips):.2f}-{max(tips):.2f} C")
    gaps = [(a["wake"], b["wake"]) for a, b in zip(rows, rows[1:]) if b["wake"] - a["wake"] != 10]
    print(f"wake gaps (expect 10 apart): {gaps or 'none'}")
    agree = [r["lumped_r_ohm"] / r["fw_r"] - 1 for r in rows if r.get("fw_r")]
    print(f"cloud refit lumped R vs the probe's stored Rsoil: median {100 * np.median(agree):+.3f} %, "
          f"worst {100 * max(map(abs, agree)):.2f} %")

    print("\nresidual, 1e-4 of drive     median    p10    p90")
    for name in MODELS:
        v = np.array([r[f"{name}_rms"] for r in rows]) * 1e4
        print(f"  {name:24} {np.median(v):7.0f} {np.percentile(v, 10):6.0f} {np.percentile(v, 90):6.0f}")
    wins = sum(r["cpe_rms"] < r["lumped_rms"] for r in rows)
    print(f"CPE below the firmware's lumped fit on {wins} of {len(rows)} sweeps")
    cpe_r = np.array([r["cpe_r_ohm"] for r in rows])
    print(f"CPE series R: median {np.median(cpe_r):.0f} ohm, p10 {np.percentile(cpe_r, 10):.1f}, "
          f"p90 {np.percentile(cpe_r, 90):.0f}, at the 0.1 ohm bound on {int((cpe_r < 1).sum())} of {len(rows)}")

    print("\nhourly medians")
    keys = ("tip", "die") + tuple(k for k, _ in PARAMS)
    print("  hour          " + " ".join(f"{k.replace('_ohm', '').replace('_pf', '')[:10]:>10}" for k in keys))
    hour = rows[0]["time"].replace(minute=0, second=0, microsecond=0)
    while hour <= rows[-1]["time"]:
        chunk = [r for r in rows if hour <= r["time"] < hour + timedelta(hours=1)]
        if chunk:
            print(f"  {hour:%m-%d %H:%M}   " + " ".join(f"{np.median([r[k] for r in chunk]):10.4g}" for k in keys))
        hour += timedelta(hours=1)

    if step is not None:
        before = [r for r in rows if r["time"] <= step][-3:]
        after = [r for r in rows if r["time"] > step][:3]
        print(f"\nstep at {step:%m-%d %H:%M}: last 3 sweeps before vs first 3 after (median)")
        for key, name in PARAMS:
            b, a = np.median([r[key] for r in before]), np.median([r[key] for r in after])
            change = f"{a - b:+.4f}" if key in LINEAR else f"{100 * (a / b - 1):+.1f} %"
            print(f"  {name:24} {b:10.4g} -> {a:10.4g}  {change}")

    print("\njoint fit ln(p) = a + step + b*tip + c*hours (CPE n on its value); PROVISIONAL")
    print(f"  {'':24} {'step %':>13} {'tip %/C':>15} {'drift %/h':>15} {'resid %':>8} {'sweep-to-sweep %':>17}")
    for key, name in PARAMS:
        coef, se, rms = regress(rows, key, step)
        s = scale(key)
        v = np.array([r[key] for r in rows])
        d = np.diff(v) if key in LINEAR else np.diff(np.log(v))
        noise = 1.4826 * np.median(np.abs(d - np.median(d))) / np.sqrt(2) * s
        step_text = f"{s * coef[1]:+7.2f}+/-{s * se[1]:.2f}" if len(coef) == 4 else f"{'-':>13}"
        print(f"  {name:24} {step_text:>13} {s * coef[-2]:+8.3f}+/-{s * se[-2]:.3f} "
              f"{s * coef[-1]:+8.3f}+/-{s * se[-1]:.3f} {s * rms:8.2f} {noise:17.3f}")

    if flat is not None:
        chunk = [r for r in rows if flat[0] <= r["time"] < flat[1]]
        tips = [r["tip"] for r in chunk]
        print(f"\nnear-constant tip {flat[0]:%m-%d %H:%M}-{flat[1]:%H:%M} ({len(chunk)} sweeps, "
              f"tip {min(tips):.2f}-{max(tips):.2f} C): straight-line drift per hour")
        hours = np.array([(r["time"] - chunk[0]["time"]).total_seconds() / 3600 for r in chunk])
        for key, name in PARAMS:
            y = np.array([r[key] if key in LINEAR else np.log(r[key]) for r in chunk])
            unit = "" if key in LINEAR else " %"
            print(f"  {name:24} {scale(key) * np.polyfit(hours, y, 1)[0]:+8.3f}{unit}")


def self_test() -> int:
    """Synthetic rows with a known step, temperature coefficient and drift must come back out."""
    rng = np.random.default_rng(1)
    t0 = datetime(2026, 10, 16, 0, 0).astimezone()
    step = t0 + timedelta(hours=14)
    rows = []
    for i in range(120):
        when = t0 + timedelta(minutes=10 * i)
        h = i / 6
        tip = 21 + 2.5 * np.sin(2 * np.pi * h / 24)
        ln = np.log(15000) - 0.017 * (tip - 21) + 0.006 * h + (0.12 if when > step else 0) + rng.normal(0, 0.005)
        rows.append({"time": when, "tip": tip, "x": float(np.exp(ln))})
    coef, _, _ = regress(rows, "x", step)
    failures = []
    for got, want, what in ((coef[1], 0.12, "step"), (coef[2], -0.017, "tip"), (coef[3], 0.006, "drift")):
        if abs(got - want) > 0.1 * abs(want):
            failures.append(f"{what}: got {got:.4f}, want {want}")
    for line in failures:
        print("FAIL", line)
    print(f"self-test: {'FAILED' if failures else 'passed'} ({len(failures)} failure(s))")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--fits", type=Path, help="msv4_cloud_sweeps.py output")
    parser.add_argument("--history", type=Path, help="fetch_supabase_history.py --raw output, same sensor")
    parser.add_argument("--step", help="local time of a step to fit as an offset, 'YYYY-MM-DD HH:MM'")
    parser.add_argument("--flat", nargs=2, metavar=("START", "END"), help="a near-constant-temperature window")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not (args.fits and args.history):
        parser.error("need --fits and --history")
    rows = join(args.fits, args.history)
    if not rows:
        print("no sweep matched a history row")
        return 1
    step = datetime.fromisoformat(args.step).astimezone() if args.step else None
    flat = tuple(datetime.fromisoformat(t).astimezone() for t in args.flat) if args.flat else None
    report(rows, step, flat)
    return 0


if __name__ == "__main__":
    sys.exit(main())
