"""Board 1 (sensor 10, R15 1 kohm) vs board 2 (sensor 11, R15 3.3 kohm): where the noise comes from.

For each sensor, over a window both pots spent drying on the same shelf (24 h after any watering
left out):
  * per-reading noise: scatter of successive readings (robust sigma / sqrt 2), so slow drying and
    daily swings cancel. Board 1 is also checked on every 10th wake, to match board 2's cadence.
  * sensitivity to water: a line through the weighings, so noise can be put in grams of water.
  * white noise left in an hourly median (what the plots draw), and the within-day swing of hourly
    medians around a local 24 h linear trend (rms).
  * how far the divider moves for a 1 % change in Rsoil with R15 = 1k and 3.3k at each pot's own
    median Rsoil / Cm / Cd, rms over the 8 sweep frequencies.

    python MSV4/scripts/msv4_r15_noise_compare.py   (reads the two caches in data/sensor_logs/cache)
"""
import json
import math
import statistics
import sys
from datetime import datetime, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import msv4_experimental_moisture as reading
import msv4_moisture_competition as comp

START = datetime.fromisoformat("2026-10-28T00:00:00-07:00")
END = datetime.fromisoformat("2026-11-04T12:00:00-08:00")
LOCAL = datetime.fromisoformat("2026-11-01T00:00:00-08:00").tzinfo
SKIP_AFTER_WATERING_H = 24


def rows_for(sensor):
    return [r for r in reading.load(ROOT / "data/sensor_logs/cache" / f"sensor{sensor}_raw.csv", None)
            if not r["open"] and r["rsoil"]]


def robust_sigma(d):
    m = statistics.median(d)
    return 1.4826 * statistics.median(abs(x - m) for x in d)


def in_window(rows, waterings):
    return [r for r in rows if START <= r["time"] < END and not any(
        timedelta(0) <= r["time"] - w < timedelta(hours=SKIP_AFTER_WATERING_H) for w in waterings)]


def successive(rows, key, max_gap_min, log=False):
    d = []
    for a, b in zip(rows, rows[1:]):
        if (b["time"] - a["time"]).total_seconds() / 60 > max_gap_min or a[key] is None or b[key] is None:
            continue
        d.append(math.log(b[key]) - math.log(a[key]) if log else b[key] - a[key])
    return d


def per_reading(rows, key, max_gap_min, log=False):
    return robust_sigma(successive(rows, key, max_gap_min, log)) / math.sqrt(2)


def per_gram(rows, weighings, key, log):
    pts = []
    for when, g in weighings:
        near = [r[key] for r in rows if abs((r["time"] - when).total_seconds()) <= 1800 and r[key]]
        if len(near) >= 3:
            v = statistics.median(near)
            pts.append((g, math.log(v) if log else v))
    if len(pts) < 3:
        return None, len(pts)
    return statistics.linear_regression([g for g, _ in pts], [v for _, v in pts])[0], len(pts)


def within_day(rows, key, log):
    """rms of hourly medians about a +/-12 h linear trend, and the median readings per hour."""
    byhour = {}
    for r in rows:
        if r[key] is not None:
            byhour.setdefault(r["time"].replace(minute=0, second=0, microsecond=0), []).append(
                math.log(r[key]) if log else r[key])
    hours = sorted(byhour)
    med = {h: statistics.median(byhour[h]) for h in hours}
    res = []
    for h in hours:
        around = [((k - h).total_seconds() / 3600, med[k]) for k in hours
                  if abs((k - h).total_seconds()) <= 12 * 3600]
        if len(around) >= 20:
            res.append(med[h] - statistics.linear_regression([t for t, _ in around], [v for _, v in around])[1])
    return math.sqrt(statistics.fmean(x * x for x in res)), statistics.median(len(v) for v in byhour.values())


def divider_step(rs_ohm, rsoil, cm, cd, cp):
    """rms over the sweep of |H(Rsoil*0.99) - H(Rsoil)|, as a fraction of the drive."""
    def h(r, w):
        x = 1 / (w * cm * 1e-12)
        g, b = r / (r * r + x * x), w * (cd + cp) * 1e-12 + x / (r * r + x * x)
        return 1 / complex(1 + rs_ohm * g, rs_ohm * b)
    tot = 0.0
    for dv in reading.SWEEP_DIVISORS:
        w = 2 * math.pi * reading.SWEEP_CLOCK_HZ / dv
        tot += abs(h(rsoil * 0.99, w) - h(rsoil, w)) ** 2
    return math.sqrt(tot / len(reading.SWEEP_DIVISORS))


def main():
    fleet = json.loads((ROOT / "MSV3" / "fleet.json").read_text(encoding="utf-8"))
    board2 = next(b for b in fleet["msv4_boards"]["boards"] if b.get("sensor_id") == 11)["physical_state"]
    weigh11 = [(datetime.fromisoformat(w["at"]), w["grams"]) for w in board2["mass"]["weighings"]]
    water11 = [datetime.fromisoformat(e["at"]) if "at" in e else datetime.fromisoformat("2026-10-27T12:29:00-07:00")
               for e in board2["watering_events"]["events"]]
    weigh10, water10 = comp.load_pot(10)
    weigh10 = [(w["time"], w["grams"]) for w in weigh10 if not w["excluded"]]

    for sensor, label, weighings, waterings, cadence, cp in (
            (10, "sensor 10, board 1, R15 1k, Alocasia (big pot)", weigh10, water10, 1, 8.69),
            (11, "sensor 11, board 2, R15 3.3k, Begonia (small pot)", weigh11, water11, 10, 8.74)):
        every = rows_for(sensor)
        rows = in_window(every, waterings)
        print(f"\n=== {label}: {len(rows)} readings {rows[0]['time']:%m-%d %H:%M} .. {rows[-1]['time']:%m-%d %H:%M}")
        rsoil = statistics.median(r["rsoil"] for r in rows)
        cm = statistics.median(r["cm"] for r in rows)
        cd = statistics.median(r["cd"] for r in rows if r["cd"])
        print(f"  median Rsoil {rsoil:.0f} ohm, Cm {cm:.1f} pF, Cd {cd:.2f} pF")
        tips = {}
        for r in rows:
            if r["tip"] is not None:
                tips.setdefault(r["time"].astimezone(LOCAL).date(), []).append(r["tip"])
        print("  tip daily range, C:", " ".join(f"{max(v) - min(v):.1f}" for _, v in sorted(tips.items()) if len(v) > 50))
        tenth = [r for r in rows if r["wake"] % 10 == 0]
        for key, log, unit in (("rsoil", True, "%"), ("cm", False, "pF"), ("cd", False, "pF"), ("stored", False, "units")):
            k = 100 if log else 1
            line = f"  per-reading noise {key:6}: {per_reading(rows, key, 1.5 * cadence, log) * k:.3f} {unit}"
            if cadence == 1:
                line += f" (every 10th wake: {per_reading(tenth, key, 15, log) * k:.3f})"
            print(line)
        for key, log in (("rsoil", True), ("stored", False)):
            slope, n = per_gram(every, [w for w in weighings if START <= w[0] < END], key, log)
            if slope is None:
                print(f"  {key}: only {n} weighings")
                continue
            noise = per_reading(rows, key, 1.5 * cadence, log)
            swing, per_hour = within_day(rows, key, log)
            print(f"  {key}: {slope * (100 if log else 1):+.4f} {'%' if log else 'units'}/g ({n} weighings); "
                  f"per reading {abs(noise / slope):.2f} g; white noise in an hourly median (n={per_hour:.0f}) "
                  f"~{abs(noise / slope) / math.sqrt(per_hour) * 1.25:.2f} g; within-day swing {abs(swing / slope):.2f} g rms")
        for rs_ohm in (1000, 3300):
            print(f"  divider step for 1 % Rsoil at this soil, R15 {rs_ohm}: "
                  f"{divider_step(rs_ohm, rsoil, cm, cd, cp) * 1e4:.2f}e-4 of drive")


if __name__ == "__main__":
    main()
