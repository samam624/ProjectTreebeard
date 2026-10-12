#!/usr/bin/env python3
"""Synthetic test of the production image's battery saver (application 262165).

From 262165 the wake's battery reading sets one of two levels:

  Full      everything the build does; two LED blinks before each sweep but a boot's first
  Saving    below 3500 mV: no LED; moisture (and with it the connectable window) every 30th
            wake at most

This replays that rule, wake by wake, on made-up battery records and checks what the probe would
do on each wake. The threshold and counts are read out of the sketch, and the rule's own lines
are checked as source text, so this cannot quietly test something the sketch no longer does.

What is synthetic: every battery record. No MSV4 cell has been taken down to this level. What
this proves is the logic: when the level changes, what each level allows, and that Saving cannot
close the way in.

Run:  python MSV4/scripts/test_msv4_battery_saver.py
"""

from __future__ import annotations

import sys

import test_msv4_warm_fit as fit

_number, _require = fit._number, fit._require

SAVING_BELOW_MV = _number(r"constexpr float SAVER_SAVING_BELOW_MV = ([\d.]+)f;")
RETURN_MV = _number(r"constexpr float SAVER_RETURN_MV = ([\d.]+)f;")
CONFIRM_WAKES = int(_number(r"constexpr uint8_t SAVER_CONFIRM_WAKES = (\d+);"))
SAVING_EVERY = int(_number(r"constexpr uint8_t SAVER_SAVING_EVERY_N_WAKES = (\d+);"))
LONG_WINDOW_BOOT_WAKES = int(_number(r"const uint8_t LONG_WINDOW_BOOT_WAKES = (\d+);"))
OTA_MIN_BATTERY_MV = _number(r"const uint16_t OTA_MIN_BATTERY_MV = (\d+);")
PRODUCTION_EVERY = int(_number(r"#else\s+const uint8_t MOISTURE_EVERY_N_WAKES = (\d+);"))

# The rule's own lines. If one of these is gone the replica below is testing something else.
for line in (
    "enum class PowerLevel : uint8_t { Full, Saving };",
    "const float savingBelow = SAVER_SAVING_BELOW_MV + (now == PowerLevel::Saving ? SAVER_RETURN_MV : 0.0f);",
    "return vbatMv < savingBelow ? PowerLevel::Saving : PowerLevel::Full;",
    "if (!g_powerLevelSet || wanted == g_powerLevel)",
    "if (++g_powerPendingWakes >= SAVER_CONFIRM_WAKES)",
    "const uint8_t floorWakes = g_powerLevel == PowerLevel::Saving ? SAVER_SAVING_EVERY_N_WAKES : 0;",
    "return MOISTURE_EVERY_N_WAKES > floorWakes ? MOISTURE_EVERY_N_WAKES : floorWakes;",
    "if (!g_powerLevelSet || g_powerLevel != PowerLevel::Full || g_wakesSinceBoot <= 1) return;",
    "if (g_wakesSinceBoot < 255) g_wakesSinceBoot++;",
    "saverJudge(vbatOk, vbatMv);",
    "const uint8_t moistureEvery = saverMoistureEveryWakes();",
    "if (g_wakesUntilMoisture > moistureEvery - 1) g_wakesUntilMoisture = moistureEvery - 1;",
    "const bool longWindow = g_wakesUntilMoisture == 0 || g_wakesSinceBoot < LONG_WINDOW_BOOT_WAKES;",
    "ledAnnounceMeasurement();\n    g_reading = takeMoistureReading();",
    "g_wakesUntilMoisture = g_reading.valid ? moistureEvery - 1 : 0;",
    "(g_powerLevel != PowerLevel::Full ? ADV_FLAG2_SAVER : 0) |",
    # No level may change what a wake sends or how often the probe wakes.
    "Bluefruit.Advertising.setInterval(SHORT_ADV_INTERVAL_UNITS, SHORT_ADV_INTERVAL_UNITS);",
    "const uint32_t WAKE_PERIOD_MS = 60000;",
):
    _require(line)

FULL, SAVING = "Full", "Saving"


class Probe:
    """The saver and the wake schedule, as saverJudge() and loop() have them."""

    def __init__(self, build_every: int):
        self.build_every = build_every
        self.level = FULL
        self.level_set = False
        self.pending_wakes = 0
        self.until_moisture = 0
        self.since_boot = 0

    def wanted(self, mv: float) -> str:
        saving_below = SAVING_BELOW_MV + (RETURN_MV if self.level == SAVING else 0.0)
        return SAVING if mv < saving_below else FULL

    def judge(self, mv: float | None) -> None:
        if mv is None:
            self.pending_wakes = 0
            return
        wanted = self.wanted(mv)
        if not self.level_set or wanted == self.level:
            self.level, self.level_set, self.pending_wakes = wanted, True, 0
            return
        self.pending_wakes += 1
        if self.pending_wakes >= CONFIRM_WAKES:
            self.level, self.pending_wakes = wanted, 0

    def every(self) -> int:
        return max(self.build_every, SAVING_EVERY if self.level == SAVING else 0)

    def wake(self, mv: float | None, sweep_ok: bool = True) -> dict:
        """One wake with that battery reading (None = unusable). What the probe does on it."""
        self.judge(mv)
        every = self.every()
        if self.until_moisture > every - 1:
            self.until_moisture = every - 1
        window = self.until_moisture == 0 or self.since_boot < LONG_WINDOW_BOOT_WAKES
        self.since_boot = min(self.since_boot + 1, 255)
        measured = led = False
        if self.until_moisture == 0:
            measured = True
            led = self.level_set and self.level == FULL and self.since_boot > 1
            self.until_moisture = every - 1 if sweep_ok else 0
        else:
            self.until_moisture -= 1
        return {"level": self.level, "measured": measured, "fresh": measured and sweep_ok, "window": window,
                "led": led, "saver_flag": self.level != FULL}


def run(build_every: int, record: list[float | None], sweeps: list[bool] | None = None) -> list[dict]:
    probe = Probe(build_every)
    return [probe.wake(mv, sweeps[i] if sweeps else True) for i, mv in enumerate(record)]


def spacing(wakes: list[dict], level: str) -> list[int]:
    """Wakes from one measurement to the next, both taken at that level."""
    at = [i for i, w in enumerate(wakes) if w["measured"]]
    return [b - a for a, b in zip(at, at[1:]) if wakes[a]["level"] == level and wakes[b]["level"] == level]


def first(wakes: list[dict], level: str) -> int:
    return next(i for i, w in enumerate(wakes) if w["level"] == level)


failures: list[str] = []


def check(name: str, got, want) -> None:
    if got != want:
        failures.append(f"{name}: got {got!r}, want {want!r}")


def invariants(name: str, wakes: list[dict]) -> None:
    """What must hold whatever the battery does."""
    # A measurement wake always opens the connectable window, and so do a boot's first wakes.
    check(f"{name}: every measurement wake is connectable", all(w["window"] for w in wakes if w["measured"]), True)
    check(f"{name}: a boot's first wakes are connectable", all(w["window"] for w in wakes[:LONG_WINDOW_BOOT_WAKES]), True)
    # The way in never closes for longer than the longest cadence.
    gaps, last = [], -1
    for i, w in enumerate(wakes):
        if w["window"]:
            gaps.append(i - last)
            last = i
    longest = max(SAVING_EVERY, PRODUCTION_EVERY)
    check(f"{name}: no gap between connectable wakes over {longest}", max(gaps) <= longest, True)
    # The LED is a Full-level thing only, and only on a measurement.
    check(f"{name}: LED only at Full, on a measurement",
          all(w["level"] == FULL and w["measured"] for w in wakes if w["led"]), True)
    check(f"{name}: the flag is the level", all(w["saver_flag"] == (w["level"] != FULL) for w in wakes), True)


def ramp(start: float, stop: float, wakes: int) -> list[float]:
    return [start + (stop - start) * i / (wakes - 1) for i in range(wakes)]


def main() -> int:
    rows = []

    def note(name: str, wakes: list[dict]) -> None:
        invariants(name, wakes)
        counts = {level: sum(1 for w in wakes if w["level"] == level) for level in (FULL, SAVING)}
        rows.append((name, len(wakes), counts[FULL], counts[SAVING],
                     sum(w["measured"] for w in wakes), sum(w["window"] for w in wakes), sum(w["led"] for w in wakes)))

    # A full cell: the saver does nothing, on either cadence.
    for every in (1, PRODUCTION_EVERY):
        wakes = run(every, [3800.0] * 600)
        note(f"steady 3800 mV, moisture every {every}", wakes)
        check(f"full/{every}: level", {w["level"] for w in wakes}, {FULL})
        check(f"full/{every}: spacing", set(spacing(wakes, FULL)), {every})
        check(f"full/{every}: LED on every measurement but the boot's", [w["led"] for w in wakes if w["measured"]],
              [False] + [True] * (len(wakes) // every - 1))
        check(f"full/{every}: flag clear", any(w["saver_flag"] for w in wakes), False)

    # Nothing changes anywhere above the threshold: the old 3700 and 3550 mV are Full now.
    for mv in (3700.0, 3690.0, 3560.0, 3540.0, 3500.0):
        wakes = run(PRODUCTION_EVERY, [mv] * 120)
        check(f"{mv:.0f} mV is Full", {w["level"] for w in wakes}, {FULL})

    # A slow discharge through the threshold, one-minute test build and production build.
    for every in (1, PRODUCTION_EVERY):
        record = ramp(3600.0, 3400.0, 4001)   # 0.05 mV a wake
        wakes = run(every, record)
        note(f"discharge 3600 -> 3400 mV, moisture every {every}", wakes)
        saving_at = first(wakes, SAVING)
        # The level changes on the second reading in a row below the threshold.
        below = next(i for i, mv in enumerate(record) if mv < SAVING_BELOW_MV)
        check(f"discharge/{every}: Saving on reading {CONFIRM_WAKES} below", saving_at, below + CONFIRM_WAKES - 1)
        check(f"discharge/{every}: spacing at Full", set(spacing(wakes, FULL)), {every})
        check(f"discharge/{every}: spacing at Saving", set(spacing(wakes, SAVING)), {max(every, SAVING_EVERY)})
        check(f"discharge/{every}: no LED after Full", any(w["led"] for w in wakes[saving_at:]), False)
        check(f"discharge/{every}: one way", all(not (a["level"] == SAVING and b["level"] == FULL)
                                                  for a, b in zip(wakes, wakes[1:])), True)

    # Charging back: Saving is left only RETURN_MV above the threshold, and the closer
    # measurements start at once rather than after the long wait already counted out.
    record = [3400.0] * 40 + ramp(3400.0, 3600.0, 4001)
    wakes = run(PRODUCTION_EVERY, record)
    note("charge 3400 -> 3600 mV, production", wakes)
    check("charge: starts Saving", wakes[0]["level"], SAVING)
    to_full = first(wakes, FULL)
    check("charge: leaves Saving at the return level", record[to_full - CONFIRM_WAKES + 1] >= SAVING_BELOW_MV + RETURN_MV
          and record[to_full - CONFIRM_WAKES] < SAVING_BELOW_MV + RETURN_MV, True)
    next_measure = next(i for i in range(to_full, len(wakes)) if wakes[i]["measured"])
    check("charge: a measurement within the build's cadence of reaching Full", next_measure - to_full < PRODUCTION_EVERY, True)
    check("charge: LED back at Full", wakes[next_measure]["led"], True)

    # The band between the threshold and its return level holds whichever level it was entered from.
    wakes = run(PRODUCTION_EVERY, [3600.0] * 5 + [3520.0] * 300)
    note("3520 mV from above", wakes)
    check("band from above: stays Full", {w["level"] for w in wakes}, {FULL})
    wakes = run(PRODUCTION_EVERY, [3480.0] * 5 + [3520.0] * 300)
    note("3520 mV from below", wakes)
    check("band from below: stays Saving", {w["level"] for w in wakes}, {SAVING})

    # Single readings that stand off do not move the level: one high reading in fifty at the
    # size measured on board 1 (22 mV), and the same thing downward.
    record = [3540.0 + (22.0 if i % 50 == 49 else 0.0) for i in range(1000)]
    wakes = run(PRODUCTION_EVERY, [3480.0, 3480.0] + record)
    note("Saving at 3540 mV, one reading in fifty 22 mV high", wakes)
    check("spike up: stays Saving", {w["level"] for w in wakes}, {SAVING})
    record = [3510.0 - (22.0 if i % 50 == 49 else 0.0) for i in range(1000)]
    wakes = run(PRODUCTION_EVERY, record)
    note("Full at 3510 mV, one reading in fifty 22 mV low", wakes)
    check("spike down: stays Full", {w["level"] for w in wakes}, {FULL})
    # Two in a row do.
    wakes = run(PRODUCTION_EVERY, [3510.0] * 10 + [3490.0, 3490.0] + [3510.0] * 100)
    check("one low reading: still Full", wakes[10]["level"], FULL)
    check("two low readings: Saving", wakes[11]["level"], SAVING)
    check("then held by the return band", wakes[-1]["level"], SAVING)

    # A boot takes its level from the first usable reading, with no second one to wait for.
    for mv, level in ((3800.0, FULL), (3450.0, SAVING)):
        wakes = run(PRODUCTION_EVERY, [mv] * 200)
        note(f"boot at {mv:.0f} mV", wakes)
        check(f"boot at {mv:.0f}: level at once", wakes[0]["level"], level)
        check(f"boot at {mv:.0f}: first wake measures", wakes[0]["measured"], True)
        check(f"boot at {mv:.0f}: no LED on the boot's own measurement", wakes[0]["led"], False)
        check(f"boot at {mv:.0f}: LED on the next one", next(w for w in wakes[1:] if w["measured"])["led"], level == FULL)

    # A battery that cannot be read: nothing is assumed about it. The level stays where it was,
    # the LED stays dark until a reading has said Full, and the schedule runs on.
    wakes = run(PRODUCTION_EVERY, [None] * 100)
    note("battery never readable", wakes)
    check("unreadable: level stays Full", {w["level"] for w in wakes}, {FULL})
    check("unreadable: no LED", any(w["led"] for w in wakes), False)
    check("unreadable: build cadence", set(spacing(wakes, FULL)), {PRODUCTION_EVERY})
    wakes = run(PRODUCTION_EVERY, [3450.0] * 5 + [None] * 100 + [3800.0] * 5)
    check("unreadable later: Saving held", {w["level"] for w in wakes[5:105]}, {SAVING})
    check("unreadable between two low readings breaks the run",
          [w["level"] for w in run(PRODUCTION_EVERY, [3800.0, 3490.0, None, 3490.0, 3490.0])],
          [FULL, FULL, FULL, FULL, SAVING])

    # A failed sweep is retried on the next wake at either level, with its window.
    wakes = run(1, [3450.0] * 40, [False] * 5 + [True] * 35)
    note("Saving, the first five sweeps fail", wakes)
    check("failed sweep: retried every wake until one works", [w["measured"] for w in wakes[:7]],
          [True, True, True, True, True, True, False])
    check("failed sweep: not fresh", [w["fresh"] for w in wakes[:6]], [False] * 5 + [True])
    check("failed sweep: the cadence starts from the one that worked", [i for i, w in enumerate(wakes) if w["measured"]][-1],
          5 + SAVING_EVERY)

    # Saving begins where an update is refused anyway, so it never hides a way in that was open.
    check("Saving begins at or below the OTA floor", SAVING_BELOW_MV <= OTA_MIN_BATTERY_MV, True)

    print(f"From the sketch: Saving below {SAVING_BELOW_MV:.0f} mV, left {RETURN_MV:.0f} mV higher, "
          f"after {CONFIRM_WAKES} wakes in a row; moisture every {SAVING_EVERY}th wake at most while saving.\n")
    print(f"{'battery record':52} wakes   Full Saving  measured  connectable  LED")
    for name, n, full, saving, measured, windows, led in rows:
        print(f"{name:52} {n:5d}  {full:5d} {saving:6d}  {measured:8d}  {windows:11d}  {led:3d}")
    for line in failures:
        print("FAIL", line)
    print(f"\n{'FAIL' if failures else 'PASS'} battery saver: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
