#!/usr/bin/env python3
"""Is the blade-tip thermistor worth having, or would the chip's own temperature do?

An MSV4 probe sends two temperatures with every reading: the NTC at the blade tip, in the soil
(R17), and the nRF52840's die temperature, at the board end in the air. The receiver corrects the
soil resistance to 25 C at 2 %/C of the TIP. This asks what the correction would be worth made
from the die instead, on the probe's own stored record:

  1. how the two temperatures differ, and what filter turns the die into the tip;
  2. the coefficient each one asks for (ln Rsoil against temperature, with a straight line in
     time for the drying), and whether it is the same from one stretch to the next;
  3. what is left of the resistance's daily wobble after each correction. "Wobble" is what the
     moisture competition uses: the range of the hourly medians about a straight line through
     the stretch, here also in grams of water at the moisture number's own per gram.

The stretches come from the record itself and MSV3/fleet.json: the day before the pot's last
watering (settled, dry), the hours before that day (the blade still settling after planting),
and from SETTLE_AFTER_WATER_HOURS after the watering to the end (moist). Nothing is fitted across
a watering.

A filtered die is always fitted on the settled dry day and then run, unchanged, on the others:
a filter tuned and judged on the same hours would only show that two smooth curves can be made
to agree.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 10 --raw --since-hours 116 --csv data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_ntc_vs_die.py data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_ntc_vs_die.py --self-test
"""

from __future__ import annotations

import argparse
import math
import statistics
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

import msv4_experimental_moisture as reading
import msv4_moisture_competition as competition

EC_TEMPCO_PER_C = 0.02            # the receiver's correction, MSV4_EC_TEMPCO_PER_C
EC_REFERENCE_C = 25.0
SETTLE_AFTER_PLANTING_HOURS = 1   # the first hour in soil is the blade going in
SETTLE_AFTER_WATER_HOURS = 4      # water still running past the blade; the competition waits 2
MIN_PER_HOUR = 5
FILTER_MINUTES = range(15, 361, 15)
UNITS_PER_PERCENT = 10000.0 * math.log10(1.01)   # the moisture number is 10000 per decade of ohms


def lowpass(minutes: np.ndarray, values: np.ndarray, tau: float) -> np.ndarray:
    """First-order lag with time constant tau minutes, over unevenly spaced readings."""
    if tau <= 0:
        return values.copy()
    out = np.empty_like(values)
    out[0] = values[0]
    for i in range(1, len(values)):
        out[i] = out[i - 1] + (1.0 - math.exp(-(minutes[i] - minutes[i - 1]) / tau)) * (values[i] - out[i - 1])
    return out


def corrected(ln_r: np.ndarray, temp: np.ndarray, per_c: float) -> np.ndarray:
    """ln of the resistance referred to 25 C, the way the receiver does it."""
    return ln_r + np.log(np.maximum(0.25, 1.0 + per_c * (temp - EC_REFERENCE_C)))


def line_fit(y: np.ndarray, columns: list[np.ndarray]) -> tuple[np.ndarray, np.ndarray]:
    design = np.column_stack([np.ones(len(y))] + columns)
    beta, *_ = np.linalg.lstsq(design, y, rcond=None)
    return beta, y - design @ beta


def wobble(minutes: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    """(sd, range of hourly medians) of y about a straight line in time, both in percent."""
    _, left = line_fit(y, [minutes])
    hours: dict[int, list[float]] = {}
    for m, v in zip(minutes - minutes[0], left):
        hours.setdefault(int(m // 60), []).append(v)
    medians = [statistics.median(v) for _, v in sorted(hours.items()) if len(v) >= MIN_PER_HOUR]
    return 100.0 * float(left.std()), 100.0 * (max(medians) - min(medians))


def die_to_tip(minutes, die, tip, where) -> tuple[float, float, float, float]:
    """(tau, gain, offset, rms) of the filtered, scaled die that best gives the tip over `where`."""
    best = None
    for tau in FILTER_MINUTES:
        smooth = lowpass(minutes, die, tau)
        gain, offset = np.polyfit(smooth[where], tip[where], 1)
        rms = float(np.sqrt(np.mean((offset + gain * smooth[where] - tip[where]) ** 2)))
        if best is None or rms < best[3]:
            best = (float(tau), float(gain), float(offset), rms)
    return best


def stretches(times: list[datetime], watered: datetime | None) -> dict[str, np.ndarray]:
    """Boolean masks over the readings for each stretch that the record is long enough to hold."""
    first = times[0] + timedelta(hours=SETTLE_AFTER_PLANTING_HOURS)
    out: dict[str, np.ndarray] = {}

    def add(name: str, start: datetime, end: datetime) -> None:
        mask = np.array([start <= t < end for t in times])
        if mask.sum() >= 6 * 60:    # six hours of one-minute readings, or it says nothing
            out[f"{name}, {start:%m-%d %H:%M} to {end:%m-%d %H:%M}"] = mask

    if watered is None:
        add("whole record", first, times[-1] + timedelta(seconds=1))
        return out
    day = timedelta(hours=24)
    if watered - day > first:
        add("settling after planting", first, watered - day)
    add("dry, settled", max(first, watered - day), watered)
    add("moist", watered + timedelta(hours=SETTLE_AFTER_WATER_HOURS), times[-1] + timedelta(seconds=1))
    return out


def units_per_gram(rows: list[dict], weighings: list[dict], waterings: list[datetime]) -> float | None:
    """The moisture number's own slope against the pot's weight, from the competition's pairing."""
    pairs = [p for p in competition.pair(rows, competition.merge_repeats(weighings, waterings), waterings) if not p["saucer"]]
    for result in competition.score(rows, pairs, waterings):
        if result["name"] == "moisture number in use" and result.get("slope"):
            return result["slope"]
    return None


def report(rows: list[dict], watered: datetime | None, per_gram: float | None) -> None:
    times = [r["time"] for r in rows]
    minutes = np.array([(t - times[0]).total_seconds() / 60.0 for t in times])
    tip = np.array([r["tip"] for r in rows])
    die = np.array([r["die"] for r in rows])
    ln_r = np.array([math.log(r["rsoil"]) for r in rows])
    parts = stretches(times, watered)
    settled = next((name for name in parts if name.startswith("dry, settled")), next(iter(parts)))
    grams = (lambda percent: f"{percent * UNITS_PER_PERCENT / per_gram:6.1f} g") if per_gram else (lambda percent: "")

    print("\n1. The two temperatures")
    print(f"{'stretch':52} {'tip C':>13} {'die C':>13} {'die-to-tip filter fitted on this stretch alone':>48}")
    for name, where in parts.items():
        tau, gain, offset, rms = die_to_tip(minutes, die, tip, where)
        print(f"{name:52} {tip[where].min():5.2f}-{tip[where].max():5.2f}   {die[where].min():5.2f}-{die[where].max():5.2f}   "
              f"{tau:3.0f} min, x{gain:.2f}, {offset:+.2f} C, rms {rms:.2f} C")
    tau, gain, offset, rms = die_to_tip(minutes, die, tip, parts[settled])
    proxy = offset + gain * lowpass(minutes, die, tau)
    print(f"The filter from the settled dry day ({tau:.0f} min, x{gain:.2f}, {offset:+.2f} C), run unchanged: filtered die minus tip")
    for name, where in parts.items():
        miss = proxy[where] - tip[where]
        print(f"   {name:52} mean {miss.mean():+.2f} C, sd {miss.std():.2f} C, {miss.min():+.2f} to {miss.max():+.2f}")

    print("\n2. The coefficient each temperature asks for: ln Rsoil = a + b * time + k * temperature")
    print(f"{'stretch':52} {'tip alone':>16} {'die alone':>16} {'both: tip, die':>26}")
    for name, where in parts.items():
        cells = []
        for columns in ([tip], [die], [tip, die]):
            beta, left = line_fit(ln_r[where], [minutes[where]] + [c[where] for c in columns])
            ks = ", ".join(f"{100 * b:+.2f}" for b in beta[2:])
            cells.append(f"{ks} %/C ({100 * left.std():.2f} %)")
        print(f"{name:52} {cells[0]:>16} {cells[1]:>16} {cells[2]:>26}")
    print("   in brackets: the sd of what is left. The receiver assumes -2.00 %/C of the tip.")
    print("   The tip itself, made late by a filter (0 = as read): coefficient, sd left")
    for name, where in parts.items():
        cells = []
        for late in (0, 30, 60, 120):
            beta, left = line_fit(ln_r[where], [minutes[where], lowpass(minutes, tip, late)[where]])
            cells.append(f"{late} min {100 * beta[2]:+.2f} %/C ({100 * left.std():.2f} %)")
        print(f"   {name.split(',')[0]:26} " + " | ".join(cells))

    print("\n3. What is left of the resistance's wobble after each correction (sd %, range of hourly medians %"
          + (", that range in grams)" if per_gram else ")"))
    die_fits = {}
    for name, where in parts.items():
        beta, _ = line_fit(ln_r[where], [minutes[where], die[where]])
        die_fits[name] = -float(beta[2])
    for name, where in parts.items():
        print(name)
        options = [("none", ln_r),
                   (f"tip, {100 * EC_TEMPCO_PER_C:.1f} %/C (what the receiver does)", corrected(ln_r, tip, EC_TEMPCO_PER_C)),
                   (f"die as read, {100 * EC_TEMPCO_PER_C:.1f} %/C", corrected(ln_r, die, EC_TEMPCO_PER_C))]
        for other, k in die_fits.items():
            tag = "its own best" if other == name else f"best on '{other.split(',')[0]}'"
            options.append((f"die as read, {100 * k:.2f} %/C ({tag})", corrected(ln_r, die, k)))
        options.append((f"die filtered {tau:.0f} min x{gain:.2f}, {100 * EC_TEMPCO_PER_C:.1f} %/C", corrected(ln_r, proxy, EC_TEMPCO_PER_C)))
        for label, y in options:
            sd, span = wobble(minutes[where], y[where])
            print(f"   {label:62} {sd:5.2f}  {span:6.2f} %  {grams(span)}")
    if per_gram:
        print(f"Grams: the moisture number moves {per_gram:.1f} units per gram of water by the pot's weighings, "
              f"and {UNITS_PER_PERCENT:.1f} units per percent of resistance.")
    step = 10000.0 * math.log10(1.0 + EC_TEMPCO_PER_C * 0.25)
    print(f"Both temperatures arrive in steps of 0.25 C; one step is {step:.1f} units of the moisture number"
          + (f", {step / per_gram:.1f} g." if per_gram else "."))


def self_test() -> None:
    """A made-up pot: soil follows the air two hours late, the resistance follows the soil at 2 %/C."""
    start = datetime.fromisoformat("2026-11-01T00:00:00+00:00")
    rng = np.random.default_rng(7)
    minutes = np.arange(0.0, 4 * 24 * 60, 1.0)
    air = 23.0 + 3.0 * np.sin(2 * np.pi * minutes / 1440.0) + 1.5 * np.maximum(0.0, np.sin(2 * np.pi * (minutes - 600) / 1440.0)) ** 8
    soil = lowpass(minutes, air, 120.0)
    ln_r = math.log(15000.0) + 0.0005 * minutes / 60.0 - 0.02 * (soil - 25.0) + rng.normal(0.0, 0.005, len(minutes))
    rows = [{"time": start + timedelta(minutes=float(m)), "tip": float(round(s * 4) / 4), "die": float(round(a * 4) / 4),
             "rsoil": float(math.exp(v))} for m, s, a, v in zip(minutes, soil, air, ln_r)]
    times = [r["time"] for r in rows]
    watered = start + timedelta(hours=60)
    parts = stretches(times, watered)
    assert [name.split(",")[0] for name in parts] == ["settling after planting", "dry", "moist"], list(parts)
    dry = next(where for name, where in parts.items() if name.startswith("dry, settled"))
    assert times[int(np.argmax(dry))] == watered - timedelta(hours=24) and dry.sum() == 1440
    tip = np.array([r["tip"] for r in rows]); die = np.array([r["die"] for r in rows]); y = np.array([math.log(r["rsoil"]) for r in rows])
    # The soil's own temperature gives back the 2 %/C that went in; the air's does not.
    beta, _ = line_fit(y[dry], [minutes[dry], tip[dry]])
    assert abs(100 * beta[2] + 2.0) < 0.15, beta
    beta, _ = line_fit(y[dry], [minutes[dry], die[dry]])
    assert 100 * beta[2] > -1.6, beta
    # Corrected from the tip the wobble is the noise; from the air as read it is several times that.
    none = wobble(minutes[dry], y[dry])[1]
    by_tip = wobble(minutes[dry], corrected(y, tip, 0.02)[dry])[1]
    by_die = wobble(minutes[dry], corrected(y, die, 0.02)[dry])[1]
    assert by_tip < 1.0 < by_die and by_tip < none / 5, (none, by_tip, by_die)
    # The filter finds the two hours that were put in, and with it the die does what the tip does.
    tau, gain, offset, rms = die_to_tip(minutes, die, tip, dry)
    assert 105 <= tau <= 135 and abs(gain - 1.0) < 0.05 and rms < 0.15, (tau, gain, offset, rms)
    by_proxy = wobble(minutes[dry], corrected(y, offset + gain * lowpass(minutes, die, tau), 0.02)[dry])[1]
    assert by_proxy < 1.0, by_proxy
    assert abs(lowpass(minutes, np.full(len(minutes), 5.0), 60.0) - 5.0).max() < 1e-12
    print("PASS NTC against die: stretches, coefficients, wobble and the die-to-tip filter on a made-up pot")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.csv:
        parser.error("a CSV is needed unless --self-test is given")
    # Only boots that have an air value on record: those are the boots since the probe was planted,
    # which leaves out the hours it spent being handled before that.
    loaded = [r for r in reading.load(args.csv, None) if r["coupled"] is not None]
    rows = [r for r in loaded if not r["open"] and r["tip"] is not None and r["die"] is not None and r["rsoil"]]
    if not rows:
        raise SystemExit("no conducting MSV4 readings with both temperatures, from a boot with an air value, in that file")
    weighings, waterings = competition.load_pot(rows[0]["sensor"])
    watered = waterings[-1] if waterings and rows[0]["time"] < waterings[-1] < rows[-1]["time"] else None
    per_gram = units_per_gram(loaded, weighings, waterings) if watered else None
    print(f"{len(rows)} conducting readings, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}, sensor {rows[0]['sensor']}"
          + (f"; watered {watered:%Y-%m-%d %H:%M}" if watered else "; no watering inside the record"))
    report(rows, watered, per_gram)


if __name__ == "__main__":
    main()
