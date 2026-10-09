#!/usr/bin/env python3
"""Modelled charge budget of the MSV4 production image, and what each remaining lever is worth.

NOTHING HERE IS A MEASURED CURRENT. No supply current has been measured on an MSV4 board. Inputs
are of three kinds, and the output is only as good as the weakest:

  SPEC      nRF52840 product specification typical figures at 3 V, the same ones used in
            MSV4/docs/MSV4_PRODUCTION_FIRMWARE_2026-10-06.md.
  MEASURED  times and counts taken on board 1 (wake length, cadence), and counts read from the
            sketch (events per burst, phase points per sweep). The measurement-wake lengths are
            from a millisecond advertisement listener on 2026-10-07, good to about 0.2 s.
  ASSUMED   the per-event radio overhead (chosen so the linear-regulator model lands on that
            record's 262154 table) and the sleep current (MSV3's measured 10-15 uA; MSV4's own
            has not been measured).

Run:  python MSV4/scripts/msv4_power_budget.py
"""

from __future__ import annotations

from dataclasses import dataclass, replace

# ------------------------------------------------------------------------------- SPEC (mA) ----
TX_MA = {8: (32.7, 14.8), 4: (21.4, 9.6), 0: (10.6, 4.8)}   # (linear, DC/DC), radio only
RX_MA = (9.9, 4.6)
CPU_MA = (6.3, 3.3)
TIMER_HFXO_MA = 0.82    # one TIMER at 16 MHz on HFXO; scaled by the CPU ratio under DC/DC
SAADC_MA = 1.24         # specified with DC/DC; the 262154 table used it as is for the linear case

# ------------------------------------------------------------------- MEASURED / from source ----
PDU_MS = 47 * 8 / 1000.0             # 31-byte payload -> 47-byte PDU at 1 Mbps, per channel
CHANNELS = 3
SPEC_N = (64, 48, 32, 24, 16, 12, 8, 6)
ADC_CONVERSION_US = 12.0             # TACQ 10 us + about 2 us conversion, per oversample
# Sweep + fit, blade in air, board 1, 2026-10-07: first advertising frame against the 60 s grid.
# Four cold starts: 3.02 s (262155, counter 61) and 2.95 s (262157, counter 1). One sample taken
# on 2026-10-06 by another method read 2.7 s. Warm-start fit: 1.00 s (262157, counter 11).
MEASUREMENT_WAKE_S = 3.0
MEASUREMENT_WAKE_WARM_S = 1.0
FIT_FULL_EVERY_N = 12                # 262157: one full fit in this many measurements

# ---------------------------------------------------------------------------------- ASSUMED ----
EVENT_OVERHEAD_MS = 1.6              # CPU and radio ramp per advertising event
RX_SLOT_MS = 0.33                    # listen after each channel of a connectable event
SLEEP_UA = (10.0, 15.0)              # MSV3's measured range, used for MSV4 until it is measured
POINT_OVERHEAD_MS = 0.15             # front-end start and SAADC setup per phase point
SETTLE_MS = 1.5                      # delay(2) ends on a tick boundary: 0.98-1.95 ms


def phases(n: int, cap: int = 16) -> int:
    """Phase points for timer period n: the sketch's specPhases(), optionally capped.

    The phase count has to divide n, so a period of 6 cannot be sampled at 4 points and keeps 6.
    """
    fits = [m for m in (16, 12, 8, 6, 4) if m <= n and n % m == 0]
    return max((m for m in fits if m <= cap), default=min(fits))


@dataclass(frozen=True)
class Config:
    dcdc: bool = True
    warm_fit: bool = True        # 262157 onward; False is four cold starts every time
    tx_dbm: int = 8
    wakes: int = 10              # wakes per measurement interval
    wake_period_s: float = 60.0
    burst_events: int = 16       # short non-connectable burst
    window_events: int = 56      # connectable window: 1 s at 20 ms, then 4 s at 250 ms
    windows_per_interval: float = 1.0
    phase_cap: int = 16
    oversample: int = 64
    orientations: int = 2

    @property
    def interval_s(self) -> float:
        return self.wakes * self.wake_period_s


BASE = Config()


def sweep_times(c: Config) -> tuple[float, float, float]:
    """(total, cpu-busy, adc-on) seconds for one measurement's sweep."""
    points = sum(phases(n, c.phase_cap) for n in SPEC_N) * c.orientations
    adc_ms = 2 * c.oversample * ADC_CONVERSION_US / 1000.0
    busy_ms = adc_ms + POINT_OVERHEAD_MS
    return (points * (busy_ms + SETTLE_MS) / 1000.0, points * busy_ms / 1000.0,
            points * adc_ms / 1000.0)


# The fit's CPU time is what is left of the measured wake after the modelled baseline sweep.
FIT_AIR_S = MEASUREMENT_WAKE_S - sweep_times(BASE)[0]
FIT_AIR_WARM_S = MEASUREMENT_WAKE_WARM_S - sweep_times(BASE)[0]
FIT_AIR_AVERAGE_S = (FIT_AIR_S + (FIT_FULL_EVERY_N - 1) * FIT_AIR_WARM_S) / FIT_FULL_EVERY_N


def budget(c: Config, sleep_ua: float) -> dict[str, float]:
    """Charge in mC per measurement interval."""
    r = 1 if c.dcdc else 0
    ratio = CPU_MA[1] / CPU_MA[0] if c.dcdc else 1.0
    tx = PDU_MS * CHANNELS * TX_MA[c.tx_dbm][r] + EVENT_OVERHEAD_MS * CPU_MA[r]      # uC
    conn = tx + RX_SLOT_MS * CHANNELS * RX_MA[r]
    bursts = c.wakes - c.windows_per_interval
    adv = (bursts * c.burst_events * tx + c.windows_per_interval * c.window_events * conn) / 1000.0
    total_s, busy_s, adc_s = sweep_times(c)
    sweep = busy_s * CPU_MA[r] + adc_s * SAADC_MA + total_s * TIMER_HFXO_MA * ratio
    fit = (FIT_AIR_AVERAGE_S if c.warm_fit else FIT_AIR_S) * CPU_MA[r]
    sleep = sleep_ua * 1e-3 * c.interval_s
    return {"advertising": adv, "sweep": sweep, "fit": fit, "sleep": sleep,
            "total": adv + sweep + fit + sleep}


def per_day(mc: float, interval_s: float) -> float:
    return mc * 86400.0 / interval_s / 3600.0


def main() -> None:
    print(f"Sweep: {sum(phases(n) for n in SPEC_N) * 2} phase points, {sweep_times(BASE)[0]:.2f} s. "
          f"Fit in air: {FIT_AIR_S:.2f} s from four cold starts,\n{FIT_AIR_WARM_S:.2f} s warm, "
          f"{FIT_AIR_AVERAGE_S:.2f} s on average with one full fit in {FIT_FULL_EVERY_N}.\n")

    print("Budget per 10 minutes (mC), sleep at 10 / 15 uA")
    columns = (("linear (262154)", replace(BASE, dcdc=False, warm_fit=False)),
               ("DC/DC (262155)", replace(BASE, warm_fit=False)),
               ("+ warm fit (262157)", BASE))
    print(f"{'':14}" + "".join(f"{name:>22}" for name, _ in columns))
    tables = [[budget(cfg, s) for s in SLEEP_UA] for _, cfg in columns]
    for key in ("advertising", "sweep", "fit", "sleep", "total"):
        print(f"{key:14}" + "".join(f"{t[0][key]:>14.1f} /{t[1][key]:>6.1f}" for t in tables))
    print(f"{'mAh/day':14}" + "".join(
        f"{per_day(t[0]['total'], 600):>14.2f} /{per_day(t[1]['total'], 600):>6.2f}" for t in tables))
    print("  (the production record's 262154 table, from a 2.7 s wake: 10 / 4.2 / 11 / 5-8 mC,"
          " 1.2-1.3 mAh/day)\n")

    sleep_mid = sum(SLEEP_UA) / 2
    base_day = per_day(budget(BASE, sleep_mid)["total"], BASE.interval_s)
    levers = [
        ("TX +8 -> +4 dBm", replace(BASE, tx_dbm=4)),
        ("TX +8 -> 0 dBm", replace(BASE, tx_dbm=0)),
        ("short bursts 16 -> 8 events", replace(BASE, burst_events=8)),
        ("wake every 2 min, measure every 10", replace(BASE, wakes=5, wake_period_s=120.0)),
        ("connectable window without the fast second", replace(BASE, window_events=16)),
        ("connectable window every 30 min", replace(BASE, windows_per_interval=1 / 3)),
        ("sweep: 4 phases per frequency", replace(BASE, phase_cap=4)),
        ("sweep: oversample 64x -> 16x", replace(BASE, oversample=16)),
        ("sweep: one orientation", replace(BASE, orientations=1)),
        ("measure every 20 min", replace(BASE, wakes=20)),
        ("measure every 30 min", replace(BASE, wakes=30)),
    ]
    print("Levers NOT taken (the maintainer deferred all of these on 2026-10-07), against the")
    print(f"262157 baseline of {base_day:.2f} mAh/day (sleep {sleep_mid:.1f} uA)")
    print(f"{'lever':46}{'saves mAh/day':>14}{'of total':>10}")
    for name, cfg in levers:
        day = per_day(budget(cfg, sleep_mid)["total"], cfg.interval_s)
        print(f"{name:46}{base_day - day:>14.3f}{(base_day - day) / base_day:>10.0%}")
    for name, ua in (("sleep: each 1 uA", 1.0), ("sleep: U3/U4 power gate, at most 2 uA", 2.0),
                     ("sleep: RAM retention off, at most 1.4 uA", 1.4)):
        day = ua * 24 / 1000.0
        print(f"{name:46}{day:>14.3f}{day / base_day:>10.0%}")
    print(f"\nSleep alone is {per_day(budget(BASE, SLEEP_UA[0])['sleep'], 600):.2f}-"
          f"{per_day(budget(BASE, SLEEP_UA[1])['sleep'], 600):.2f} mAh/day at the assumed 10-15 uA; "
          f"at the chip's specified 3.2 uA it would be {3.2 * 24 / 1000:.2f}.")

    # The receiver listens 25 ms in every 75, on one channel at a time, so it hears any one
    # advertising event with probability about 1/3 (uploads, which stop the scan for 2-3 s, are
    # not modelled).
    print("\nChance the receiver misses a whole burst, scan duty 1/3: "
          + ", ".join(f"{n} events {(2 / 3) ** n:.1%}" for n in (16, 12, 8, 6)))


if __name__ == "__main__":
    main()
