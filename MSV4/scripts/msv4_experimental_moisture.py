#!/usr/bin/env python3
"""EXPERIMENTAL moisture reading for an MSV4 probe: the capacitance the soil couples to the blade.

    soil-coupled capacitance  =  Cm + Cd - (this board's Cd in air)        [pF]

Cm and Cd are two of the three numbers the probe already fits and sends on every measurement.
Cm + Cd is the electrode's capacitance at the low-frequency end of the model: what is left when
the soil is treated as a conductor. How well the soil conducts (which is what temperature
changes, and what the present moisture number is made from) sets WHERE in frequency the fitted
branch turns over, not this value. The air value is subtracted so that the reading is zero with
nothing on the blade. An open reading (nothing conducting) has no Cm and counts it as zero.

It is computed from fields that are already stored, so it exists for every reading on record
and needs nothing new from the probe. The History page draws the same quantity
(msv4SoilCoupledPf() in MobileAppPage.h); DisplayCenter/scripts/verify_msv4_history_decode.mjs
holds the two to the same air values.

Not established: that it tracks water. No MSV4 probe has been watered. --watered applies the
test written down before the first watering (MSV4/docs/MSV4_EXPERIMENTAL_MOISTURE_2026-10-10.md).

Reads the CSV that cloud/scripts/fetch_supabase_history.py --raw writes and prints, in local time:

  1. a binned table: temperatures, the stored moisture number, Rsoil, Cm, Cd and the reading;
  2. over --window, how far each of those moves: the range of its hourly medians, the scatter
     inside an hour, and its correlation with the tip temperature;
  3. with --watered, the watering test;
  4. with --figure, the temperatures, the stored number and the reading on one time axis.

  python cloud/scripts/fetch_supabase_history.py --receiver <id> --sensor 10 --raw --since-hours 100 --csv data/sensor_logs/msv4_s10.csv
  python MSV4/scripts/msv4_experimental_moisture.py data/sensor_logs/msv4_s10.csv --window "2026-10-09 19:00" "2026-10-10 12:00"
  python MSV4/scripts/msv4_experimental_moisture.py --self-test
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from datetime import datetime, timedelta
from pathlib import Path

RSOIL_DECIOHM_OPEN = 4000000   # the probe's ceiling: nothing conducting

# Cd with the blade in air, per probe AND PER BOOT. Cd is the node capacitance minus Cp, and Cp is
# whatever that boot is running on, so an air value belongs to one boot. Every boot gets its own
# entry, also when the value does not change, and a reading from a boot with no entry gets no
# value: nothing then says which Cp it was taken on. Nothing is applied backwards either.
#   (sensor, when that boot began, Cd in air in pF, that boot's Cp in pF, where they come from)
AIR_BASELINES = (
    (10, "2026-10-08T21:47:10-07:00", 13.72, 8.69,
     "board 1 in air after its epoxy, 2026-10-08 20:39-21:39: Cd 13.46-13.50 pF on Cp 8.93 pF, "
     "so 22.41 pF at the node; this boot calibrated Cp 8.69 pF on its first reading, part-way in"),
    (10, "2026-10-08T22:07:55-07:00", 13.72, 8.69,
     "application 262161, built with that Cp compiled in (-CpCentiPf 869); "
     "DIS read 'Cp 8.69 pF default' at 22:08"),
    (10, "2026-10-11T11:52:48-07:00", 13.72, 8.69,
     "application 262163, built with that Cp compiled in (-CpCentiPf 869); "
     "DIS read 'Cp 8.69 pF default' at 11:56"),
    (10, "2026-10-11T13:03:43-07:00", 13.72, 8.69,
     "application 262165, built with that Cp compiled in (-CpCentiPf 869); "
     "DIS read 'Cp 8.69 pF default' at 13:07"),
    (10, "2026-10-11T20:39:37-07:00", 13.72, 8.69,
     "application 262166, built with that Cp compiled in (-CpCentiPf 869); "
     "DIS read 'Cp 8.69 pF default' after install and cloud wake 1 fixed the boot time"),
    (11, "2026-10-13T22:47:17-07:00", 13.52, 8.74,
     "board 2 (R15 3.3 kohm) in plain air outside overnight (maintainer), 2026-10-13 23:00 - 10-14 10:00: "
     "median Cd of 66 readings, range 13.25-13.69 pF, tip 10.8-17.0 C; application 262167 built with "
     "-RsOhms 3300 -CpCentiPf 874, DIS 'Cp 8.74 pF default'"),
    (11, "2026-10-14T11:38:16-07:00", 13.52, 8.74,
     "application 262168 on the same built-in Cp (-CpCentiPf 874, DIS 'Cp 8.74 pF default'); the overnight "
     "value carried at the maintainer's choice; dry on the desk its first 7 readings had a median of 13.61 pF"),
    (11, "2026-10-14T23:28:49-07:00", 13.74, 8.74,
     "application 262169 (-CpCentiPf 874); dry in air on the desk its whole boot, 2026-10-14 23:28 - 10-15 13:47: "
     "median Cd of 851 readings, range 13.37-14.27 pF, tip 23.0-25.8 C"),
    (11, "2026-10-15T17:35:51-07:00", 13.74, 8.74,
     "application 262171 on the same built-in Cp (-CpCentiPf 874); booted in moist soil, so the 262169 air "
     "value is carried"),
    (11, "2026-10-15T20:16:38-07:00", 13.74, 8.74,
     "application 262172 (raw sweep in the scan response) on the same built-in Cp; booted in its own pot of "
     "moist soil, so the 262169 air value is carried"),
)
# Which boot a reading belongs to comes from the reading itself: the probe counts one wake a
# minute from 1 at boot, so its boot began (wake - 1) minutes earlier. An entry is that boot's
# when it starts within two minutes of that, plus 0.2 % of the time since (the probe's clock is an
# RC oscillator; board 1 measured 60.0004 s a wake over 37 hours).
WAKE_PERIOD_S = 60.0
BOOT_MATCH_S = 120.0
BOOT_MATCH_PER_UPTIME = 0.002

# The sweep, as the probe sketch has it (SPEC_RS, FE_CLOCK_HZ, SPEC_N).
SWEEP_RS_OHM = 1000.0
SWEEP_CLOCK_HZ = 16000000
SWEEP_DIVISORS = (64, 48, 32, 24, 16, 12, 8, 6)
SIGNAL_FLOOR = 0.001   # of the drive: below this the electrodes add nothing to take a share of

# The watering test, fixed on 2026-10-10 before any MSV4 probe was watered.
WATER_BASELINE_HOURS = 12        # this much undisturbed record before the watering
WATER_PRE_HOURS = (-6, -1)       # "before" is the median over these hours
WATER_POST_HOURS = (2, 8)        # "after" is the median over these, and every hour must hold
WATER_MIN_STEP_PF = 3.0
WATER_MAD_MULTIPLE = 5.0         # times the scatter inside an hour, before
WATER_SWING_MULTIPLE = 3.0       # times the range of the hourly medians over the baseline hours
MIN_READINGS_PER_HOUR = 3


def boot_time(when: datetime, wake: int) -> datetime:
    return when - timedelta(seconds=(wake - 1) * WAKE_PERIOD_S)


def boot_entry(sensor: int, when: datetime, wake: int) -> tuple[float, float] | None:
    """(Cd in air, Cp) for the boot this reading was taken on, or None when that boot has no entry."""
    if wake < 1:
        return None
    uptime = (wake - 1) * WAKE_PERIOD_S
    slack = BOOT_MATCH_S + BOOT_MATCH_PER_UPTIME * uptime
    best = None
    for entry_sensor, start, cd_air, cp, _ in AIR_BASELINES:
        off = abs((datetime.fromisoformat(start) - boot_time(when, wake)).total_seconds())
        if entry_sensor == sensor and off <= slack and (best is None or off < best[0]):
            best = (off, cd_air, cp)
    return best[1:] if best else None


def relative_residual_percent(rsoil: float | None, cm_pf: float, cd_pf: float | None, is_open: bool,
                              residual_percent: float, cp_pf: float | None) -> float | None:
    """The fit residual as a share of what the electrodes add to the sweep.

    The probe sends its residual as a share of the DRIVE. A wetter medium loads the node harder,
    so the response being fitted is larger and the same proportional misfit is a larger number.
    This divides by the size of that response: the rms, over the sweep's frequencies, of the
    fitted transfer minus the transfer with the electrodes switched out (Cp alone).
    """
    if cd_pf is None or cp_pf is None:
        return None
    total = 0.0
    for divisor in SWEEP_DIVISORS:
        w = 2.0 * math.pi * SWEEP_CLOCK_HZ / divisor
        g, b = 0.0, w * (cd_pf + cp_pf) * 1e-12
        if not is_open and rsoil and cm_pf > 0.0:
            x = 1.0 / (w * cm_pf * 1e-12)          # the branch is rsoil - jx
            g, b = rsoil / (rsoil * rsoil + x * x), b + x / (rsoil * rsoil + x * x)
        h = 1.0 / complex(1.0 + SWEEP_RS_OHM * g, SWEEP_RS_OHM * b)
        h_open = 1.0 / complex(1.0, SWEEP_RS_OHM * w * cp_pf * 1e-12)
        total += abs(h - h_open) ** 2
    signal = math.sqrt(total / len(SWEEP_DIVISORS))
    return round(residual_percent / signal, 2) if signal > SIGNAL_FLOOR else None


def soil_coupled_pf(cm_pf: float, cd_pf: float | None, is_open: bool, cd_air_pf: float | None) -> float | None:
    """The reading. Negative values are kept: a reading below the air value is information."""
    if cd_pf is None or cd_air_pf is None:
        return None
    return round((0.0 if is_open else cm_pf) + cd_pf - cd_air_pf, 2)


def parse(record: dict, cd_air_override: float | None = None, cp_override: float | None = None) -> dict | None:
    """One CSV row as a measurement, or None when the probe did not measure on that wake."""
    if record.get("moisture_scale") != "4" or record.get("tank_phase") != "1":
        return None
    if not record.get("raw_fdc_counts") or not record.get("bare_tank_fdc_counts"):
        return None
    stamp = record["timestamp_iso"].replace(" ", "T")
    if stamp.endswith("+00"):
        stamp += ":00"
    when = datetime.fromisoformat(stamp).astimezone()
    word = int(record["bare_tank_fdc_counts"])
    deci = int(record["raw_fdc_counts"])
    is_open = deci >= RSOIL_DECIOHM_OPEN
    cd_centi = (word >> 32) & 0xFFFF
    cd = cd_centi / 100.0 if cd_centi else None
    cm = 0.0 if is_open else (word & 0xFFFF) / 10.0
    sensor, wake = int(record["sensor"]), int(record["wake"])
    entry = boot_entry(sensor, when, wake)
    cd_air = cd_air_override if cd_air_override is not None else (entry[0] if entry else None)
    cp = cp_override if cp_override is not None else (entry[1] if entry else None)
    rsoil = None if is_open else deci / 10.0
    residual = ((word >> 24) & 0xFF) / 100.0

    def quarter(text: str | None) -> float | None:
        return int(text) / 4.0 if text else None

    return {
        "time": when, "sensor": sensor, "wake": wake, "open": is_open,
        "rsoil": rsoil, "cm": cm, "cd": cd, "cd_air": cd_air, "residual": residual,
        "residual_of_signal": relative_residual_percent(rsoil, cm, cd, is_open, residual, cp),
        "cd_soil": None if cd is None or cd_air is None else cd - cd_air,
        "tip": quarter(record.get("board_temp_quarter_c")), "die": quarter(record.get("raw_moisture_khz")),
        "stored": int(record["moisture"]), "coupled": soil_coupled_pf(cm, cd, is_open, cd_air),
    }


def load(path: Path, cd_air_override: float | None, cp_override: float | None = None) -> list[dict]:
    with path.open(encoding="utf-8") as handle:
        rows = [row for row in (parse(record, cd_air_override, cp_override)
                                for record in csv.DictReader(handle)) if row]
    rows.sort(key=lambda row: row["time"])
    return rows


def restarts(rows: list[dict]) -> list[datetime]:
    """When the probe's wake counter started again: a new boot, and possibly a new Cp."""
    return [b["time"] for a, b in zip(rows, rows[1:]) if b["wake"] < a["wake"]]


def boots_without_entry(rows: list[dict]) -> list[tuple[datetime, int]]:
    """(when the boot began, readings) for boots after a probe's first entry that have none."""
    first = {}
    for sensor, start, _, _, _ in AIR_BASELINES:
        first[sensor] = min(first.get(sensor, datetime.fromisoformat(start)), datetime.fromisoformat(start))
    found: list[list] = []
    for row in rows:
        began = boot_time(row["time"], row["wake"])
        if row["cd"] is None or row["cd_air"] is not None or row["sensor"] not in first or began < first[row["sensor"]]:
            continue
        if found and abs((began - found[-1][0]).total_seconds()) <= BOOT_MATCH_S + BOOT_MATCH_PER_UPTIME * (row["wake"] - 1) * WAKE_PERIOD_S:
            found[-1][1] += 1
        else:
            found.append([began, 1])
    return [(began, count) for began, count in found]


def values(rows: list[dict], key: str) -> list[float]:
    return [row[key] for row in rows if row[key] is not None]


def mad(data: list[float]) -> float:
    centre = statistics.median(data)
    return statistics.median(abs(value - centre) for value in data)


def binned(rows: list[dict], key: str, minutes: int, origin: datetime | None = None) -> list[tuple[datetime, float, float, int]]:
    """(bin start, median, scatter inside the bin, count) for bins with enough readings."""
    origin = origin or rows[0]["time"].replace(minute=0, second=0, microsecond=0)
    bins: dict[int, list[float]] = {}
    for row in rows:
        if row[key] is not None:
            bins.setdefault(int((row["time"] - origin).total_seconds() // (minutes * 60)), []).append(row[key])
    return [(origin + timedelta(minutes=minutes * index), statistics.median(data), mad(data), len(data))
            for index, data in sorted(bins.items()) if len(data) >= MIN_READINGS_PER_HOUR]


def watering_test(rows: list[dict], watered: datetime) -> dict:
    """The test fixed before the first watering. `verdict` is PASS, FAIL or the reason it cannot run."""
    out: dict = {"verdict": None}

    def span(first_hour: int, last_hour: int) -> list[dict]:
        return [row for row in rows if row["coupled"] is not None
                and watered + timedelta(hours=first_hour) <= row["time"] < watered + timedelta(hours=last_hour)]

    baseline = binned(span(-WATER_BASELINE_HOURS, 0), "coupled", 60, watered - timedelta(hours=WATER_BASELINE_HOURS))
    pre = span(*WATER_PRE_HOURS)
    post_hours = binned(span(*WATER_POST_HOURS), "coupled", 60, watered + timedelta(hours=WATER_POST_HOURS[0]))
    if any(watered - timedelta(hours=WATER_BASELINE_HOURS) <= at < watered + timedelta(hours=WATER_POST_HOURS[1])
           for at in restarts(rows)):
        out["verdict"] = "CANNOT RUN: the probe restarted inside the test's hours"
        return out
    if len(baseline) < WATER_BASELINE_HOURS:
        out["verdict"] = f"CANNOT RUN: {len(baseline)} of the {WATER_BASELINE_HOURS} hours before the watering have readings"
        return out
    pre_hours = binned(pre, "coupled", 60, watered + timedelta(hours=WATER_PRE_HOURS[0]))
    out["before"] = statistics.median(values(pre, "coupled"))
    out["scatter"] = statistics.median(hour[2] for hour in pre_hours)
    out["swing"] = max(hour[1] for hour in baseline) - min(hour[1] for hour in baseline)
    out["needed"] = max(WATER_MIN_STEP_PF, WATER_MAD_MULTIPLE * out["scatter"], WATER_SWING_MULTIPLE * out["swing"])
    out["after_hours"] = post_hours
    if len(post_hours) < WATER_POST_HOURS[1] - WATER_POST_HOURS[0]:
        # An hour with too few readings does not count. If the record already runs past the last
        # of those hours, waiting will not fill it.
        over = rows[-1]["time"] >= watered + timedelta(hours=WATER_POST_HOURS[1])
        out["verdict"] = (f"{'CANNOT RUN' if over else 'NOT YET'}: {len(post_hours)} of the "
                          f"{WATER_POST_HOURS[1] - WATER_POST_HOURS[0]} hours from +{WATER_POST_HOURS[0]} h to "
                          f"+{WATER_POST_HOURS[1]} h have at least {MIN_READINGS_PER_HOUR} readings")
        return out
    out["after"] = statistics.median(values(span(*WATER_POST_HOURS), "coupled"))
    out["step"] = out["after"] - out["before"]
    out["held"] = all(hour[1] >= out["before"] + out["needed"] for hour in post_hours)
    out["verdict"] = "PASS" if out["step"] >= out["needed"] and out["held"] else "FAIL"
    return out


QUANTITIES = (
    ("moisture number, as stored", "stored"),
    ("Rsoil, ohm", "rsoil"),
    ("Cm, pF", "cm"),
    ("Cd minus air, pF", "cd_soil"),
    ("soil-coupled capacitance, pF", "coupled"),
)


def print_table(rows: list[dict], minutes: int) -> None:
    print(f"\n{'':16}  tip C  die C  moist   Rsoil  Cm pF  Cd pF  coupled pF  +/- in bin  resid %  of signal %   n")
    origin = rows[0]["time"].replace(minute=0, second=0, microsecond=0)
    start = origin
    while start <= rows[-1]["time"]:
        chunk = [row for row in rows if start <= row["time"] < start + timedelta(minutes=minutes)]
        coupled = values(chunk, "coupled")
        if len(chunk) >= MIN_READINGS_PER_HOUR and coupled:
            def mean(key: str) -> str:
                data = values(chunk, key)
                return f"{statistics.fmean(data):5.2f}" if data else "    -"
            rsoil = values(chunk, "rsoil")
            print(f"{start:%Y-%m-%d %H:%M}  {mean('tip')}  {mean('die')}  {statistics.median(values(chunk, 'stored')):5.0f}  "
                  f"{statistics.median(rsoil) if rsoil else float('nan'):6.0f}  {statistics.median(values(chunk, 'cm')):5.1f}  "
                  f"{statistics.median(values(chunk, 'cd')):5.2f}  {statistics.median(coupled):10.2f}  {mad(coupled):10.2f}  "
                  f"{statistics.median(values(chunk, 'residual')):7.2f}  "
                  f"{statistics.median(values(chunk, 'residual_of_signal') or [float('nan')]):11.2f}  {len(chunk):2d}")
        start += timedelta(minutes=minutes)


def print_scoreboard(rows: list[dict], title: str) -> None:
    tips = values(rows, "tip")
    hours = (rows[-1]["time"] - rows[0]["time"]).total_seconds() / 3600.0
    print(f"\n{title}: {len(rows)} readings over {hours:.1f} h, tip {min(tips):.2f} to {max(tips):.2f} C")
    print(f"{'':30}   median  range of hourly medians  as % of median  inside an hour  r with tip")
    for label, key in QUANTITIES:
        pairs = [(row["tip"], row[key]) for row in rows if row[key] is not None and row["tip"] is not None]
        hourly = binned(rows, key, 60)
        if len(pairs) < 10 or len(hourly) < 2:
            continue
        data = [value for _, value in pairs]
        centre = statistics.median(data)
        spread = max(hour[1] for hour in hourly) - min(hour[1] for hour in hourly)
        steady = len(set(data)) < 2 or len({tip for tip, _ in pairs}) < 2
        r = "     -" if steady else f"{statistics.correlation([tip for tip, _ in pairs], data):+6.2f}"
        print(f"{label:30} {centre:8.2f}  {spread:23.2f}  {100.0 * spread / abs(centre) if centre else float('nan'):12.1f} %  "
              f"{statistics.median(hour[2] for hour in hourly):14.2f}  {r}")


def print_watering(rows: list[dict], watered: datetime) -> None:
    result = watering_test(rows, watered)
    print(f"\nWatering test, watered {watered:%Y-%m-%d %H:%M}: {result['verdict']}")
    if "needed" not in result:
        return
    print(f"  before (median, {WATER_PRE_HOURS[0]} h to {WATER_PRE_HOURS[1]} h): {result['before']:.2f} pF")
    print(f"  step needed: {result['needed']:.2f} pF = the largest of {WATER_MIN_STEP_PF:.1f} pF, "
          f"{WATER_MAD_MULTIPLE:.0f} x the scatter inside an hour ({result['scatter']:.2f} pF) and "
          f"{WATER_SWING_MULTIPLE:.0f} x the swing of the {WATER_BASELINE_HOURS} h before ({result['swing']:.2f} pF)")
    for at, centre, _, count in result["after_hours"]:
        print(f"  {at:%m-%d %H:%M}  {centre:7.2f} pF  ({centre - result['before']:+.2f}, {count} readings)")
    if "step" in result:
        print(f"  after (median, +{WATER_POST_HOURS[0]} h to +{WATER_POST_HOURS[1]} h): {result['after']:.2f} pF, "
              f"step {result['step']:+.2f} pF, every hour above the line: {'yes' if result['held'] else 'no'}")
    for label, key in QUANTITIES[:4] + (("tip, C", "tip"),):
        def centre_of(first_hour: int, last_hour: int) -> float | None:
            data = values([row for row in rows if watered + timedelta(hours=first_hour) <= row["time"]
                           < watered + timedelta(hours=last_hour)], key)
            return statistics.median(data) if data else None
        before, after = centre_of(*WATER_PRE_HOURS), centre_of(*WATER_POST_HOURS)
        if before is not None and after is not None:
            print(f"  alongside, {label}: {before:.2f} -> {after:.2f}")


def write_figure(rows: list[dict], window: list[dict], path: Path) -> None:
    """All of `rows` on one time axis; the spans in the titles are over `window`, which is shaded."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    surface, ink, muted, grid, shade = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#efeeea"
    blue, orange = "#2a78d6", "#eb6834"
    zone = rows[0]["time"].tzinfo
    fig, axes = plt.subplots(3, 1, figsize=(11, 8.6), sharex=True, facecolor=surface)
    where = "" if len(window) == len(rows) else " in the shaded hours"

    def series(key: str) -> tuple[list[datetime], list[float]]:
        points = binned(rows, key, 10)
        return [at + timedelta(minutes=5) for at, *_ in points], [centre for _, centre, *_ in points]

    def swing(key: str) -> float:
        hourly = binned(window, key, 60)
        return max(hour[1] for hour in hourly) - min(hour[1] for hour in hourly)

    for key, colour, name in (("tip", blue, "blade tip (in the soil)"), ("die", orange, "board end (in air)")):
        x, y = series(key)
        axes[0].plot(x, y, color=colour, linewidth=2, label=name)
        axes[0].annotate(name, (x[-1], y[-1]), xytext=(6, 0), textcoords="offset points", va="center", color=muted, fontsize=9)
    axes[0].legend(loc="upper left", frameon=False, fontsize=9, labelcolor=muted)
    axes[0].set_title("Temperature, °C", loc="left", color=ink, fontsize=11)

    x, y = series("stored")
    axes[1].plot(x, y, color=blue, linewidth=2)
    axes[1].set_title(f"Moisture number as stored now (from soil resistance, corrected 2 %/°C): "
                      f"hourly medians span {swing('stored'):.0f}{where}", loc="left", color=ink, fontsize=11)

    x, y = series("coupled")
    axes[2].plot(x, y, color=blue, linewidth=2)
    axes[2].set_title(f"Experimental: soil-coupled capacitance, pF (Cm + Cd − air): "
                      f"hourly medians span {swing('coupled'):.2f} pF{where}", loc="left", color=ink, fontsize=11)

    for axis in axes:
        if where:
            axis.axvspan(window[0]["time"], window[-1]["time"], color=shade, linewidth=0, zorder=0)
        axis.set_facecolor(surface)
        axis.grid(True, color=grid, linewidth=0.8)
        axis.tick_params(colors=muted, labelsize=9, length=0)
        axis.margins(x=0.01)
        for side in axis.spines.values():
            side.set_visible(False)
    axes[2].xaxis.set_major_locator(mdates.HourLocator(byhour=range(0, 24, 6), tz=zone))
    axes[2].xaxis.set_major_formatter(mdates.DateFormatter("%m-%d %H:%M", tz=zone))
    fig.subplots_adjust(left=0.07, right=0.87, top=0.95, bottom=0.06, hspace=0.28)
    fig.savefig(path, dpi=140, facecolor=surface)
    print(f"\nFigure: {path}")


def self_test() -> None:
    """The reading on rows whose answer is known, then the watering test on made-up records."""
    def record(when: str, cm_deci: int, cd_centi: int, rsoil_deci: int = 156000, sensor: int = 10, wake: int = 1) -> dict:
        return {"timestamp_iso": when, "sensor": str(sensor), "wake": str(wake), "moisture": "14300",
                "moisture_scale": "4", "tank_phase": "1", "raw_fdc_counts": str(rsoil_deci),
                "bare_tank_fdc_counts": str(cm_deci | (123 << 24) | (cd_centi << 32)),
                "board_temp_quarter_c": "90", "raw_moisture_khz": "92"}

    # 2026-10-10 10:00 PDT is wake 2153 of the boot that began 2026-10-08 22:07:55.
    soil_time, soil_wake = "2026-10-10 17:00:00+00", 2153

    def soil(cm_deci: int, cd_centi: int, rsoil_deci: int = 156000, sensor: int = 10, wake: int = soil_wake) -> dict:
        return parse(record(soil_time, cm_deci, cd_centi, rsoil_deci, sensor, wake))

    # Board 1 in soil then: Cm 28.6 pF, Cd 21.53 pF.
    assert soil(286, 2153)["coupled"] == 36.41
    # In air on this boot's Cp the reading is zero, and a little under the air value stays negative.
    assert soil(0, 1372, RSOIL_DECIOHM_OPEN)["coupled"] == 0.0
    assert soil(0, 1360, RSOIL_DECIOHM_OPEN)["coupled"] == -0.12
    # An open reading has no Cm, whatever the field holds.
    assert soil(5000, 1380, RSOIL_DECIOHM_OPEN)["coupled"] == 0.08
    # A boot with no entry gets no value: the same moment at wake 10 is a boot nine minutes old.
    assert soil(286, 2153, wake=10)["coupled"] is None
    # The match allows for the probe's clock (two wakes out here) and no more (seven).
    assert soil(286, 2153, wake=soil_wake + 2)["coupled"] == 36.41
    assert soil(286, 2153, wake=soil_wake + 7)["coupled"] is None
    # No value before the probe's first entry (wake 168 of the boot of 18:59), for a probe with
    # none, or when Cd was not sent.
    assert parse(record("2026-10-09 04:46:00+00", 0, 1348, RSOIL_DECIOHM_OPEN, wake=168))["coupled"] is None
    assert soil(286, 2153, sensor=11)["coupled"] is None
    assert soil(286, 0)["coupled"] is None
    assert parse(record(soil_time, 286, 2153), cd_air_override=13.0)["coupled"] == 37.13
    # A repeat of an earlier measurement is not a measurement.
    assert parse({**record(soil_time, 286, 2153, wake=soil_wake), "tank_phase": "0"}) is None
    # The residual as a share of the electrode signal, on board 1's own readings of 2026-10-10:
    # dry soil before the watering, the first reading with water on the blade, 40 minutes later,
    # and air. The probe's own number (share of the drive) is the fifth argument.
    assert relative_residual_percent(15320.0, 30.1, 21.51, False, 1.29, 8.69) == 7.2
    assert relative_residual_percent(1077.0, 321.6, 76.88, False, 2.01, 8.69) == 3.59
    assert relative_residual_percent(2753.0, 124.3, 35.96, False, 1.41, 8.69) == 4.12
    assert relative_residual_percent(None, 0.0, 13.72, True, 2.28, 8.69) == 20.5
    # No Cp on record for the boot, or nothing the electrodes add: no value.
    assert relative_residual_percent(2753.0, 124.3, 35.96, False, 1.41, None) is None
    assert relative_residual_percent(None, 0.0, 0.0, True, 2.28, 8.69) is None
    # A row carries it too (record() sends a residual of 1.23 %).
    assert soil(1243, 3596, 27530)["residual_of_signal"] == relative_residual_percent(2753.0, 124.3, 35.96, False, 1.23, 8.69)
    assert soil(1243, 3596, 27530, wake=10)["residual_of_signal"] is None
    # A boot with no entry is reported, with how many readings it cost.
    assert [count for _, count in boots_without_entry([soil(286, 2153), soil(286, 2153, wake=10), soil(286, 2153, wake=10)])] == [2]
    assert boots_without_entry([soil(286, 2153)]) == []

    start = datetime.fromisoformat("2026-10-12T00:00:00+00:00")
    watered = start + timedelta(hours=14)

    def run(after_pf, restart_at: int | None = None, after_every: int = 10) -> list[dict]:
        made = []
        for minute in range(0, 24 * 60, 10):
            if minute >= 14 * 60 and minute % after_every:
                continue
            wake = minute // 10 + 1 if restart_at is None or minute < restart_at else (minute - restart_at) // 10 + 1
            level = 36.4 if minute < 14 * 60 else after_pf((minute - 14 * 60) / 60.0)
            wobble = 0.2 if (minute // 10) % 2 else -0.2
            cm_deci = round((level + wobble - 21.5 + 13.72) * 10)
            made.append(parse(record((start + timedelta(minutes=minute)).isoformat(sep=" "), cm_deci, 2150, wake=wake),
                              cd_air_override=13.72))
        return made

    assert watering_test(run(lambda h: 46.4), watered)["verdict"] == "PASS"
    assert watering_test(run(lambda h: 37.4), watered)["verdict"] == "FAIL"
    # A step that is gone four hours later did not hold.
    assert watering_test(run(lambda h: 46.4 if h < 4 else 36.4), watered)["verdict"] == "FAIL"
    assert watering_test(run(lambda h: 46.4, restart_at=10 * 60), watered)["verdict"].startswith("CANNOT RUN")
    assert watering_test(run(lambda h: 46.4), start + timedelta(hours=6))["verdict"].startswith("CANNOT RUN")
    assert watering_test(run(lambda h: 46.4), start + timedelta(hours=20))["verdict"].startswith("NOT YET")
    # One reading an hour afterwards is not enough to say an hour held, and waiting will not help.
    assert watering_test(run(lambda h: 46.4, after_every=60), watered)["verdict"].startswith("CANNOT RUN")
    assert len(restarts(run(lambda h: 46.4, restart_at=10 * 60))) == 1
    print("PASS soil-coupled capacitance: soil, air, below air, open, a boot with no air value, and the watering test")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--bin", type=int, default=60, help="table bin in minutes")
    parser.add_argument("--window", nargs=2, metavar=("START", "END"), help="local times for the comparison")
    parser.add_argument("--watered", help='local time of a watering, "YYYY-MM-DD HH:MM"')
    parser.add_argument("--cd-air", type=float, help="Cd in air in pF, in place of the table in this script")
    parser.add_argument("--cp", type=float, help="Cp in pF, in place of the table in this script")
    parser.add_argument("--figure", type=Path, help="write the figure to this PNG")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.csv:
        parser.error("a CSV is needed unless --self-test is given")

    loaded = load(args.csv, args.cd_air, args.cp)
    rows = [row for row in loaded if row["coupled"] is not None]
    for began, count in boots_without_entry(loaded):
        print(f"NO AIR VALUE for the boot that began about {began:%Y-%m-%d %H:%M}: its {count} readings are left out. "
              f"Read its Cp (msv4_adv_scan.py --read-build) and add an entry to AIR_BASELINES.")
    if len(rows) < 10:
        raise SystemExit("not enough MSV4 measurements with an air value in that file")
    print(f"{len(rows)} measurements, {rows[0]['time']:%Y-%m-%d %H:%M} to {rows[-1]['time']:%Y-%m-%d %H:%M}, "
          f"sensor {rows[0]['sensor']}, Cd in air {rows[-1]['cd_air']:.2f} pF")

    print_table(rows, args.bin)
    window = rows
    if args.window:
        w0, w1 = (datetime.fromisoformat(t).astimezone() for t in args.window)
        window = [row for row in rows if w0 <= row["time"] <= w1]
        print_scoreboard(window, f"{w0:%m-%d %H:%M} to {w1:%m-%d %H:%M}")
    else:
        print_scoreboard(rows, "Whole file")
    if args.watered:
        print_watering(rows, datetime.fromisoformat(args.watered).astimezone())
    if args.figure:
        write_figure(rows, window, args.figure)


if __name__ == "__main__":
    main()
