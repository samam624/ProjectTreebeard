"""Plot sensor 11's Begonia mass in the same format as the Alocasia mass plot."""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from datetime import datetime, timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
import msv4_experimental_moisture as reading

ROOT = Path(__file__).resolve().parents[2]
FLEET = ROOT / "MSV3" / "fleet.json"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, nargs="?",
                        default=ROOT / "data" / "sensor_logs" / "cache" / "sensor11_raw.csv",
                        help="history CSV (default: the sensor 11 cache)")
    parser.add_argument(
        "-o", "--out", type=Path,
        default=ROOT / "data" / "sensor_logs" / "latest" / "sensor11_mass_vs_moisture.png",
        help="output PNG (default: overwritten each run)",
    )
    args = parser.parse_args()

    fleet = json.loads(FLEET.read_text(encoding="utf-8"))
    board = next(b for b in fleet["msv4_boards"]["boards"] if b.get("sensor_id") == 11)
    state = board["physical_state"]
    # The first event predates the structured `at` convention; its measured time is explicit in fleet.
    watered = datetime.fromisoformat("2026-10-27T12:29:00-07:00")
    waterings = [watered] + [datetime.fromisoformat(e["at"])
                             for e in state["watering_events"]["events"] if "at" in e]
    rows = [r for r in reading.load(args.csv, None) if r["time"] >= watered and r["stored"] is not None]
    if not rows:
        raise SystemExit("no sensor 11 readings at or after the recorded watering")

    hourly = []
    start = watered.replace(minute=0, second=0, microsecond=0)
    while start <= rows[-1]["time"]:
        chunk = [r for r in rows if start <= r["time"] < start + timedelta(hours=1)]
        if chunk:
            values = [r["stored"] for r in chunk]
            hourly.append((start + timedelta(minutes=30), statistics.median(values), min(values), max(values)))
        start += timedelta(hours=1)

    weighings = state["mass"]["weighings"]
    weight_times = [datetime.fromisoformat(w["at"]) for w in weighings]
    weights = [w["grams"] for w in weighings]
    hours = (weight_times[-1] - weight_times[0]).total_seconds() / 3600
    change = weights[-1] - weights[0]

    def cycle(t: datetime) -> int:
        """Index of the drying cycle t falls in (0 = after the first watering)."""
        return sum(1 for w in waterings if w <= t) - 1

    # Fit on the first drying cycle only: a fresh watering is not settled, and later
    # cycles are drawn through this conversion to show whether they read the same.
    # A reading pairs with a weighing only when no watering falls between them.
    paired = []
    for weighing, when, grams in zip(weighings, weight_times, weights):
        if cycle(when) != 0 or weighing.get("post_watering"):
            continue
        nearby = [r["stored"] for r in rows
                  if abs((r["time"] - when).total_seconds()) <= 30 * 60
                  and cycle(r["time"]) == cycle(when)]
        if nearby:
            paired.append((grams, statistics.median(nearby)))
    if len(paired) < 2:
        raise SystemExit("the provisional Begonia conversion requires at least two weighings with nearby readings")
    # Least-squares line through every weighing (exactly the two-point line when there are two).
    mean_g = statistics.fmean(g for g, _ in paired)
    mean_u = statistics.fmean(u for _, u in paired)
    slope = (sum((g - mean_g) * (u - mean_u) for g, u in paired)
             / sum((g - mean_g) ** 2 for g, _ in paired))
    intercept = mean_u - slope * mean_g
    moisture_as_grams = [(when, (median - intercept) / slope) for when, median, _, _ in hourly]

    # Match msv4_mass_vs_value_plot.py, the existing Alocasia figure.
    fig, ax = plt.subplots(figsize=(14, 7))
    for i, w in enumerate(waterings):
        ax.axvline(w, color="#4a90d9", lw=1.2, ls="--", alpha=0.7, label="watering" if i == 0 else None)
    ax.plot([t for t, _ in moisture_as_grams], [g for _, g in moisture_as_grams],
            color="#1f77b4", lw=1.3, alpha=0.9,
            label="moisture number in use (as built), as grams")
    ax.plot(weight_times, weights, color="black", lw=2.4, marker="o", ms=5, zorder=5,
            label="pot weight (scale, pot + dish)")
    ax.set_ylabel("grams on the scale (pot + plastic dish)")
    ax.set_title("Sensor 11: pot weight and the moisture number, provisional conversion\n"
                 f"{rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}")
    # Outside the axes: after a watering the newest data climbs back to the top right.
    note = (f"provisional conversion fitted to the {len(paired)}\nweighings of the first drying cycle:\n"
            + "\n".join(f"{g:.0f} g = {u:,.0f} units" for g, u in paired)
            + f"\nslope {slope:.2f} units/g")
    ax.text(1.01, 1.0, note, transform=ax.transAxes, fontsize=8, va="top", ha="left",
            bbox=dict(boxstyle="round", fc="white", ec="0.7", alpha=0.9))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=rows[0]["time"].tzinfo))
    ax.grid(alpha=0.25)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.1), ncol=3, fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(args.out, dpi=130, bbox_inches="tight")
    print(args.out)


if __name__ == "__main__":
    main()
