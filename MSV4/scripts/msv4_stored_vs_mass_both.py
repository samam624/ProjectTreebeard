"""The receiver's moisture number against the scale, Alocasia (sensor 10) and Begonia (sensor 11).

Both panels are drawn the SAME way, so the two pots can be compared by eye:

* the value is the stored moisture number exactly as the receiver makes it for both probes
  (calibrateMsv4Moisture(): log10 of Rsoil, corrected 2 %/C on the tip thermistor), nothing else;
* hourly medians of the wakes that measured;
* converted to grams by a least-squares line through that pot's own weighings, leaving out every
  weighing within FIT_SKIP_HOURS of a watering (the soil is still settling) and every weighing
  taken right after water went in;
* both panels span the same number of grams, so a gram is the same height in each.

Prints, per sensor: units per gram, how far the weighings sit from the line, the per-reading
noise (scatter between successive measured readings, robust sigma / sqrt 2) and the within-day
wobble of the hourly medians about a 24 h running median, all in grams.

    python MSV4/scripts/msv4_stored_vs_mass_both.py [-o out.png]
"""
from __future__ import annotations

import argparse
import json
import statistics
from datetime import datetime, timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

ROOT = Path(__file__).resolve().parents[2]
FLEET = ROOT / "MSV3" / "fleet.json"
FIT_SKIP_HOURS = 24
PAIR_MINUTES = 30


def sensor11_pot() -> tuple[list[dict], list[datetime]]:
    fleet = json.loads(FLEET.read_text(encoding="utf-8"))
    board = next(b for b in fleet["msv4_boards"]["boards"] if b.get("sensor_id") == 11)
    state = board["physical_state"]
    # The first watering predates the structured `at` convention; its time is explicit in fleet.
    waterings = [datetime.fromisoformat("2026-10-27T12:29:00-07:00")] + [
        datetime.fromisoformat(e["at"]) for e in state["watering_events"]["events"] if "at" in e]
    weighings = [{"time": datetime.fromisoformat(w["at"]), "grams": float(w["grams"]),
                  "post": bool(w.get("post_watering"))} for w in state["mass"]["weighings"]]
    return weighings, sorted(waterings)


def sensor10_pot() -> tuple[list[dict], list[datetime]]:
    weighings, waterings = comp.load_pot(10)
    merged = comp.merge_repeats(weighings, waterings)
    out = []
    for w in merged:
        # A weighing within minutes after a watering is the "right after" reading.
        post = any(timedelta(0) <= w["time"] - t <= timedelta(minutes=30) for t in waterings)
        out.append({"time": w["time"], "grams": w["grams"], "post": post or w["excluded"]})
    return out, waterings


def robust_sigma(values: list[float]) -> float:
    med = statistics.median(values)
    return 1.4826 * statistics.median(abs(v - med) for v in values)


def analyse(sensor: int, rows: list[dict], weighings: list[dict], waterings: list[datetime]) -> dict:
    def since_watering(t: datetime) -> float:
        past = [w for w in waterings if w <= t]
        return (t - past[-1]).total_seconds() / 3600 if past else 1e9

    def cycle(t: datetime) -> int:
        return sum(1 for w in waterings if w <= t)

    paired = []
    for w in weighings:
        if w["post"] or since_watering(w["time"]) < FIT_SKIP_HOURS:
            continue
        near = [r["stored"] for r in rows
                if abs((r["time"] - w["time"]).total_seconds()) <= PAIR_MINUTES * 60
                and cycle(r["time"]) == cycle(w["time"])]
        if near:
            paired.append((w["grams"], statistics.median(near), w["time"]))
    mg = statistics.fmean(g for g, _, _ in paired)
    mu = statistics.fmean(u for _, u, _ in paired)
    slope = sum((g - mg) * (u - mu) for g, u, _ in paired) / sum((g - mg) ** 2 for g, _, _ in paired)
    intercept = mu - slope * mg
    to_g = lambda u: (u - intercept) / slope

    hourly = []
    start = rows[0]["time"].replace(minute=0, second=0, microsecond=0)
    i = 0
    while start <= rows[-1]["time"]:
        end = start + timedelta(hours=1)
        chunk = []
        while i < len(rows) and rows[i]["time"] < end:
            chunk.append(rows[i]["stored"])
            i += 1
        if chunk:
            hourly.append((start + timedelta(minutes=30), statistics.median(chunk), len(chunk)))
        start = end

    residuals = [to_g(u) - g for g, u, _ in paired]
    diffs = [b["stored"] - a["stored"] for a, b in zip(rows, rows[1:])
             if (b["time"] - a["time"]) <= timedelta(minutes=15)]
    per_reading_units = robust_sigma(diffs) / 2 ** 0.5

    # Within-day wobble: hourly medians about a centred 24 h running median, skipping the day
    # after each watering (the overshoot is not wobble).
    wob = []
    for k, (t, u, _) in enumerate(hourly):
        if since_watering(t) < FIT_SKIP_HOURS:
            continue
        window = [v for s, v, _ in hourly if abs((s - t).total_seconds()) <= 12 * 3600
                  and cycle(s) == cycle(t)]
        if len(window) >= 20:
            wob.append(u - statistics.median(window))
    return {
        "sensor": sensor, "rows": rows, "weighings": weighings, "waterings": waterings,
        "paired": paired, "slope": slope, "intercept": intercept, "to_g": to_g, "hourly": hourly,
        "resid_rms": (statistics.fmean(r * r for r in residuals)) ** 0.5,
        "resid": residuals,
        "per_reading_units": per_reading_units, "per_reading_g": per_reading_units / slope,
        "per_hour": statistics.median(n for _, _, n in hourly),
        "wobble_g": (statistics.fmean(w * w for w in wob)) ** 0.5 / slope if wob else float("nan"),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", type=Path,
                        default=ROOT / "data" / "sensor_logs" / "latest" / "stored_vs_mass_s10_s11.png")
    args = parser.parse_args()

    results = []
    for sensor, (weighings, waterings), name in (
            (10, sensor10_pot(), "Alocasia (sensor 10, board 1, R15 1k)"),
            (11, sensor11_pot(), "Begonia (sensor 11, board 2, R15 3.3k)")):
        rows = [r for r in reading.load(reading.history_csv(sensor), None)
                if r["stored"] is not None and r["time"] >= waterings[0] - timedelta(hours=48)]
        res = analyse(sensor, rows, weighings, waterings)
        res["name"] = name
        results.append(res)

    span = max(max(w["grams"] for w in r["weighings"]) - min(w["grams"] for w in r["weighings"])
               for r in results) + 120
    fig, axes = plt.subplots(len(results), 1, figsize=(14, 10))
    for ax, r in zip(axes, results):
        for k, w in enumerate(r["waterings"]):
            ax.axvline(w, color="#4a90d9", lw=1.1, ls="--", alpha=0.7, label="watering" if k == 0 else None)
        ax.plot([t for t, _, _ in r["hourly"]], [r["to_g"](u) for _, u, _ in r["hourly"]],
                color="#1f77b4", lw=1.2, label="moisture number (hourly median) as grams")
        ws = sorted(r["weighings"], key=lambda w: w["time"])
        ax.plot([w["time"] for w in ws], [w["grams"] for w in ws], color="black", lw=2.2,
                marker="o", ms=4.5, zorder=5, label="pot weight (scale)")
        fitted = {t for _, _, t in r["paired"]}
        ax.plot([w["time"] for w in ws if w["time"] not in fitted],
                [w["grams"] for w in ws if w["time"] not in fitted], ls="none", marker="o", ms=4.5,
                mfc="white", mec="black", zorder=6, label="weighing not used in the fit")
        lo = min(w["grams"] for w in ws)
        hi = max(w["grams"] for w in ws)
        mid = (lo + hi) / 2
        ax.set_ylim(mid - span / 2, mid + span / 2)
        ax.set_ylabel("grams on the scale")
        ax.set_title(f"{r['name']}: {r['slope']:.1f} units/g, fitted on {len(r['paired'])} weighings, "
                     f"weighings sit {r['resid_rms']:.0f} g rms off the line", loc="left", fontsize=10)
        ax.text(0.995, 0.97,
                f"per-reading noise {r['per_reading_units']:.0f} units = {r['per_reading_g']:.1f} g\n"
                f"readings per hour {r['per_hour']:.0f}\n"
                f"within-day wobble of hourly medians {r['wobble_g']:.1f} g rms",
                transform=ax.transAxes, ha="right", va="top", fontsize=8.5,
                bbox=dict(boxstyle="round", fc="white", ec="0.7", alpha=0.9))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=r["rows"][0]["time"].tzinfo))
        ax.grid(alpha=0.25)
    axes[0].legend(loc="lower left", fontsize=8, ncol=2)
    fig.suptitle("Stored moisture number vs pot weight, same processing for both probes "
                 f"(both panels span {span:.0f} g)", fontsize=12)
    fig.tight_layout()
    fig.savefig(args.out, dpi=120)
    print(args.out)
    for r in results:
        print(f"sensor {r['sensor']}: {r['slope']:.2f} units/g over {len(r['paired'])} weighings; "
              f"residual rms {r['resid_rms']:.1f} g (max {max(abs(x) for x in r['resid']):.0f}); "
              f"per-reading {r['per_reading_units']:.1f} units = {r['per_reading_g']:.2f} g; "
              f"{r['per_hour']:.0f}/h; wobble {r['wobble_g']:.2f} g")


if __name__ == "__main__":
    main()
