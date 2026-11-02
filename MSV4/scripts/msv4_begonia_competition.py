"""Score every moisture candidate against the Begonia pot's weighings (sensor 11).

The Alocasia scorer (msv4_moisture_competition.py) reads weighings from `pots`; the Begonia's live
in `msv4_boards[].physical_state.mass`, so this adapts them (pot with its dish, no water standing
in it, per the maintainer's weighing_convention) and reuses that scorer unchanged.

    python MSV4/scripts/msv4_begonia_competition.py [data/sensor_logs/cache/sensor11_raw.csv]
"""
from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

ROOT = Path(__file__).resolve().parents[2]


def load_begonia() -> tuple[list[dict], list[datetime]]:
    fleet = json.loads(comp.FLEET.read_text(encoding="utf-8"))
    board = next(b for b in fleet["msv4_boards"]["boards"] if b.get("sensor_id") == 11)
    mass = board["physical_state"]["mass"]
    weighings = [{"time": datetime.fromisoformat(w["at"]), "grams": float(w["grams"]),
                  "saucer": False, "excluded": False} for w in mass["weighings"]]
    # The 10-27 watering predates the structured `at`; its measured time is in fleet (12:29 PDT).
    return sorted(weighings, key=lambda w: w["time"]), [datetime.fromisoformat("2026-10-27T12:29:00-07:00")]


def main() -> None:
    csv = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "sensor_logs" / "cache" / "sensor11_raw.csv"
    rows = [r for r in reading.load(csv, None) if r["coupled"] is not None]
    if not rows:
        raise SystemExit("no sensor 11 measurements with an air value in that file")
    weighings, waterings = load_begonia()
    print(f"{len(rows)} readings, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}, sensor 11; "
          f"{len(weighings)} weighings, {len(waterings)} watering on record")
    comp.report(rows, weighings, waterings)


if __name__ == "__main__":
    main()
