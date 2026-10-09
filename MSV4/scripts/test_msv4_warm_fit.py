#!/usr/bin/env python3
"""Synthetic-sweep test of the production image's warm-start fit (application 262157).

`runSpectrumFit()` in MSV4/arduino/MSV4_Moisture_BLE_Sleep tries one fit started from the last
full fit's answer and falls back to the four cold starts when that fit's residual is not as good
or its answer has moved. A wrong estimator is silent, so this runs the same logic on a computer
before it goes on a board.

What is replicated: `specNodeH`, `specModel`, `specFit` (the hand-rolled Levenberg-Marquardt,
step for step, in IEEE doubles as on the probe) and the warm/cold orchestration. The constants are
READ FROM THE SKETCH, and the orchestration's key lines are checked as source text, so this file
cannot quietly test something the sketch no longer does.

What is not: the sweep itself. The "measurements" are the model plus a fixed distortion sized to
give the 2.3 % residual board 1 shows in air, plus small noise. Nothing here is a board reading,
and no sweep has been recorded in soil.

Each scenario is run twice on identical data: through the warm-start logic, and with the four
cold starts every time (the 262155 behaviour). The test passes when the two agree on what the
packet would carry, and it reports how many model evaluations each took.

Run:  python MSV4/scripts/test_msv4_warm_fit.py
"""

from __future__ import annotations

import math
import random
import re
import sys
from pathlib import Path

SKETCH = (Path(__file__).resolve().parents[1] / "arduino" / "MSV4_Moisture_BLE_Sleep"
          / "MSV4_Moisture_BLE_Sleep.ino").read_text(encoding="utf-8")


def _number(pattern: str) -> float:
    match = re.search(pattern, SKETCH)
    if not match:
        raise SystemExit(f"sketch no longer contains /{pattern}/; update this test with it")
    return float(match.group(1).replace("UL", ""))


def _require(text: str) -> None:
    if text not in SKETCH:
        raise SystemExit(f"sketch no longer contains {text!r}; update this test with it")


FE_CLOCK_HZ = _number(r"constexpr uint32_t FE_CLOCK_HZ = (\d+)UL;")
SPEC_RS = _number(r"constexpr double SPEC_RS = ([\d.]+);")
FIT_WARM_ACCEPT = _number(r"constexpr double FIT_WARM_ACCEPT = ([\d.]+);")
FIT_WARM_SLACK = _number(r"constexpr double FIT_WARM_SLACK = ([\de.-]+);")
FIT_WARM_MAX_MOVE = _number(r"constexpr double FIT_WARM_MAX_MOVE = ([\d.]+);")
FIT_COLD_CM = _number(r"constexpr double FIT_COLD_CM = ([\de.-]+);")
FIT_COLD_CD = _number(r"constexpr double FIT_COLD_CD = ([\de.-]+);")
RSOIL_MIN_VALID_OHM = _number(r"constexpr double RSOIL_MIN_VALID_OHM = ([\d.]+);")
FIT_FULL_EVERY_N = int(_number(r"constexpr uint8_t FIT_FULL_EVERY_N = (\d+);"))
FIT_OPEN_OHM = _number(r"constexpr double FIT_OPEN_OHM = ([\d.]+);")
RSOIL_DECIOHM_MAX = int(_number(r"constexpr uint32_t RSOIL_DECIOHM_MAX = (\d+)UL;"))
SPEC_N = [int(v) for v in re.search(r"SPEC_N\[\] = \{([\d, ]+)\};", SKETCH).group(1).split(",")]
G_STARTS = [float(v) for v in re.search(r"gStart\[\] = \{([\de, .-]+)\};", SKETCH).group(1).split(",")]
# The orchestration this file mirrors. If any of these lines changes, so must measure() below.
for line in (
    "if (g_fitAnchorValid && g_fitsSinceFull + 1 < FIT_FULL_EVERY_N)",
    "memcpy(p, g_fitAnchor, sizeof(p));",
    "if (1.0 / p[1] >= FIT_OPEN_OHM)",
    "p[0] = FIT_COLD_CM;",
    "p[1] = 1.0 / FIT_OPEN_OHM;",
    "if (rms <= g_fitAnchorRms * FIT_WARM_ACCEPT + FIT_WARM_SLACK && fitNearAnchor(p))",
    "const double r = fmin(1.0 / p[1], FIT_OPEN_OHM);",
    "const double ra = fmin(1.0 / g_fitAnchor[1], FIT_OPEN_OHM);",
    "return r <= ra * FIT_WARM_MAX_MOVE && ra <= r * FIT_WARM_MAX_MOVE &&",
    "p[2] <= g_fitAnchor[2] * FIT_WARM_MAX_MOVE && g_fitAnchor[2] <= p[2] * FIT_WARM_MAX_MOVE;",
    "double p[3] = {FIT_COLD_CM, g0, FIT_COLD_CD}, sig[3];",
    "rsoil < RSOIL_MIN_VALID_OHM)",
    "g_fitAnchorValid = false;",
    "g_fitAnchorValid = isfinite(bestRms) && best[0] > 0.0 && best[1] > 0.0 && best[2] > 0.0;",
    "for (int it = 0; it < 60; it++)",
    "for (int tries = 0; tries < 8 && !improved; tries++)",
    "const bool done = (ss - ss2) < 1e-12 * ss;",
    "const double step = dx[a] > 3 ? 3 : (dx[a] < -3 ? -3 : dx[a]);",
):
    _require(line)

NAN = float("nan")


def spec_phases(n: int) -> int:
    return next((m for m in (16, 12, 8, 6, 4) if m <= n and n % m == 0), 0)


PHASES = [spec_phases(n) for n in SPEC_N]


# ------------------------------------------------------------------ firmware replica --------
def node_h(w: float, p: list[float], cp: float) -> complex:
    """`specNodeH`. p = [Cm, Gs, Cd]."""
    try:
        a = 1.0 / p[1]
        b = 1.0 / (w * p[0])
        d = a * a + b * b
        dr = 1.0 + SPEC_RS * (a / d)
        di = SPEC_RS * (w * (p[2] + cp) + b / d)
        m = dr * dr + di * di
        return complex(dr / m, -di / m)
    except (ZeroDivisionError, OverflowError):
        return complex(NAN, NAN)   # the probe gets inf or NaN here; either way the fit is refused


def model(n: int, m: int, p: list[float], cp: float) -> complex:
    """`specModel`: the aliased bin 1 of a square-wave drive sampled at m phases."""
    w1 = 2.0 * math.pi * FE_CLOCK_HZ / n
    num, den = 0j, 0.0
    for k in range(-8, 9):
        h = 1 + k * m
        if (h & 1) == 0 or h == 0 or h > 63 or h < -63:
            continue
        wt = 1.0 / (h * h)
        hv = node_h(w1 * abs(h), p, cp)
        if h < 0:
            hv = hv.conjugate()
        num += wt * hv
        den += wt
    return num / den


class Counter:
    evals = 0


def spec_fit(hmeas: list[complex], p: list[float], free: list[bool], cp: float) -> float:
    """`specFit`, statement for statement. Mutates p; returns the RMS residual."""
    idx = [i for i in range(3) if free[i]]
    nf, nr = len(idx), 2 * len(hmeas)

    def resid(q):
        Counter.evals += 1
        out = []
        for n, m, hm in zip(SPEC_N, PHASES, hmeas):
            d = model(n, m, q, cp) - hm
            out += [d.real, d.imag]
        return out

    lam = 1e-3
    r = resid(p)
    ss = sum(v * v for v in r)
    it = 0
    while it < 60:
        jac = [[0.0] * nf for _ in range(nr)]
        for a in range(nf):
            q = list(p)
            q[idx[a]] *= math.exp(1e-4)
            rt = resid(q)
            for i in range(nr):
                jac[i][a] = (rt[i] - r[i]) / 1e-4
        amat = [[sum(jac[i][a] * jac[i][b] for i in range(nr)) for b in range(nf)] for a in range(nf)]
        g = [-sum(jac[i][a] * r[i] for i in range(nr)) for a in range(nf)]
        improved = False
        tries = 0
        while tries < 8 and not improved:
            mm = [[amat[a][b] * (1.0 + lam if a == b else 1.0) for b in range(nf)] + [g[a]]
                  for a in range(nf)]
            for c in range(nf):
                pv = c
                for r2 in range(c + 1, nf):
                    if abs(mm[r2][c]) > abs(mm[pv][c]):
                        pv = r2
                mm[c], mm[pv] = mm[pv], mm[c]
                if abs(mm[c][c]) < 1e-300:
                    break
                for r2 in range(c + 1, nf):
                    f = mm[r2][c] / mm[c][c]
                    for k in range(c, nf + 1):
                        mm[r2][k] -= f * mm[c][k]
            dx = [0.0] * 3
            for c in range(nf - 1, -1, -1):
                v = mm[c][nf]
                for k in range(c + 1, nf):
                    v -= mm[c][k] * dx[k]
                dx[c] = v / mm[c][c] if abs(mm[c][c]) > 1e-300 else 0.0
            q = list(p)
            for a in range(nf):
                step = 3 if dx[a] > 3 else (-3 if dx[a] < -3 else dx[a])
                q[idx[a]] *= math.exp(step)
            rt = resid(q)
            ss2 = sum(v * v for v in rt)
            if ss2 < ss:
                done = (ss - ss2) < 1e-12 * ss
                p[:] = q
                r = rt
                ss, lam, improved = ss2, lam * 0.3, True
                if done:
                    it = 1000
            else:
                lam *= 10
            tries += 1
        if not improved:
            break
        it += 1
    Counter.evals += nf   # the covariance pass re-evaluates the Jacobian; its result is unused here
    return math.sqrt(ss / nr)


class Probe:
    """The state `runSpectrumFit()` keeps between measurements."""

    def __init__(self, warm_enabled: bool):
        self.warm_enabled = warm_enabled
        self.anchor: list[float] | None = None
        self.anchor_rms = 0.0
        self.since_full = 0

    def near_anchor(self, p: list[float]) -> bool:
        r = min(1.0 / p[1], FIT_OPEN_OHM)
        ra = min(1.0 / self.anchor[1], FIT_OPEN_OHM)
        return (r <= ra * FIT_WARM_MAX_MOVE and ra <= r * FIT_WARM_MAX_MOVE
                and p[2] <= self.anchor[2] * FIT_WARM_MAX_MOVE
                and self.anchor[2] <= p[2] * FIT_WARM_MAX_MOVE)

    def measure(self, hmeas: list[complex], cp: float) -> dict:
        best, best_rms, warm = [0.0, 0.0, 0.0], 1e30, False
        accepted = False
        if self.warm_enabled and self.anchor is not None and self.since_full + 1 < FIT_FULL_EVERY_N:
            p = list(self.anchor)
            if 1.0 / p[1] >= FIT_OPEN_OHM:
                p[0] = FIT_COLD_CM
                p[1] = 1.0 / FIT_OPEN_OHM
            rms = spec_fit(hmeas, p, [True, True, True], cp)
            if math.isfinite(rms):
                best, best_rms = p, rms
                if rms <= self.anchor_rms * FIT_WARM_ACCEPT + FIT_WARM_SLACK and self.near_anchor(p):
                    self.since_full += 1
                    warm = accepted = True
        if not accepted:
            for g0 in G_STARTS:
                p = [FIT_COLD_CM, g0, FIT_COLD_CD]
                rms = spec_fit(hmeas, p, [True, True, True], cp)
                if rms < best_rms:
                    best, best_rms = p, rms
            valid = math.isfinite(best_rms) and all(v > 0.0 for v in best)
            self.anchor = list(best) if valid else None
            if valid:
                self.anchor_rms = best_rms
            self.since_full = 0
        out = wire(best, best_rms, warm)
        if not out["valid"]:
            self.anchor = None   # takeMoistureReading(): an invalid answer seeds nothing
        out["best"] = list(best)   # for test_msv4_cp_calibration.py, which acts on the fit itself
        return out


def wire(best: list[float], rms: float, warm: bool) -> dict:
    """`takeMoistureReading()`: what the packet would carry."""
    cm, gs, cd = best
    if not (gs > 0.0) or not (cm > 0.0) or not (cd > 0.0) or not math.isfinite(rms):
        return {"valid": False}
    rsoil = 1.0 / gs
    if not math.isfinite(rsoil) or rsoil < RSOIL_MIN_VALID_OHM:
        return {"valid": False}
    r_deci = min(int(rsoil * 10.0 + 0.5), RSOIL_DECIOHM_MAX)
    return {"valid": True, "warm": warm, "rsoil_deci": r_deci, "open": r_deci >= RSOIL_DECIOHM_MAX,
            "cm_deci": min(int(cm * 1e13 + 0.5), 65535), "rms_e4": min(int(rms * 1e4 + 0.5), 255),
            "cd_centi": min(max(int(cd * 1e14 + 0.5), 1), 65535), "rms": rms}


# ------------------------------------------------------------------ synthetic instrument ----
CP = 8.8e-12
AIR = (1e-15, 1e-16, 13.0e-12)           # nothing conducting, Cd 13 pF (board 1, 2026-10-06)
TOWEL = (26.7e-12, 1 / 5900.0, 29e-12)   # damp paper towel, board 1, 2026-10-01
HAND = (53e-12, 1 / 1750.0, 140e-12)     # hand grip, same day
SOIL = (671e-12, 1 / 355.0, 5e-12)       # moist soil, field solve 2026-09-19 (modelled)
DAMP = (26.7e-12, 1 / 100e3, 13.0e-12)   # air's Cd with a little conduction: the onset case
F_TOP = FE_CLOCK_HZ / min(SPEC_N)


def sweep(truth, rng: random.Random, noise: float = 2e-4) -> list[complex]:
    """One synthetic sweep: the model, a fixed distortion that grows with frequency, and noise.

    The distortion stands in for whatever makes board 1's residual 2.3 % of drive even with the
    electrodes floating (its |H| reads above 1 at the top two frequencies). Its form is invented;
    only its size is matched.
    """
    out = []
    for n, m in zip(SPEC_N, PHASES):
        x = (FE_CLOCK_HZ / n) / F_TOP
        distortion = complex(1.0 + 0.060 * x * x, 0.045 * x)
        out.append(model(n, m, list(truth), CP) * distortion
                   + complex(rng.gauss(0, noise), rng.gauss(0, noise)))
    return out


def with_rsoil(truth, ohms: float):
    return (truth[0], 1.0 / ohms, truth[2])


# ------------------------------------------------------------------------------ scenarios ----
def scenario(name: str, truths: list, must_refit_at: frozenset[int] = frozenset(),
             seed: int = 20261007) -> tuple[bool, int, int]:
    """Run identical sweeps through the warm logic and through four cold starts every time.

    Passes when the packet would carry the same thing either way:
      - never invalid where the cold fit is valid (the reverse is allowed, and happens: in air the
        cold fit sometimes picks the collapsed ~0 ohm answer, which the probe reports as invalid);
      - the same open/conducting verdict, and Rsoil within 1 % when conducting;
      - Cd within 0.03 pF or 1 %; Cm within 0.5 pF of the cold fit's when open. (Just above the
        ceiling both paths report a few pF of Cm; that is the cold fit's behaviour too.)
    """
    rng = random.Random(seed)
    warm_probe, cold_probe = Probe(True), Probe(False)
    ok = True
    warm_evals = cold_evals = full = warm_invalid = cold_invalid = 0
    worst = 0.0
    notes = []
    for i, truth in enumerate(truths):
        h = sweep(truth, rng)
        Counter.evals = 0
        w = warm_probe.measure(h, CP)
        warm_evals += Counter.evals
        Counter.evals = 0
        c = cold_probe.measure(h, CP)
        cold_evals += Counter.evals
        warm_invalid += not w["valid"]
        cold_invalid += not c["valid"]
        if not w["valid"]:
            if c["valid"]:
                ok = False
                notes.append(f"sweep {i}: invalid where the cold fit is valid")
            continue
        full += not w["warm"]
        if i in must_refit_at and w["warm"]:
            ok = False
            notes.append(f"sweep {i}: a step change was accepted by the warm fit")
        if not c["valid"]:
            continue
        if w["open"] != c["open"]:
            ok = False
            notes.append(f"sweep {i}: open {w['open']} vs cold {c['open']}")
        elif not c["open"]:
            dev = abs(w["rsoil_deci"] - c["rsoil_deci"]) / c["rsoil_deci"]
            worst = max(worst, dev)
            if dev > 0.01:
                ok = False
                notes.append(f"sweep {i}: Rsoil {w['rsoil_deci'] / 10:.1f} vs cold {c['rsoil_deci'] / 10:.1f}")
        elif abs(w["cm_deci"] - c["cm_deci"]) > 5:
            ok = False
            notes.append(f"sweep {i}: open, Cm {w['cm_deci'] / 10:.1f} vs cold {c['cm_deci'] / 10:.1f} pF")
        if abs(w["cd_centi"] - c["cd_centi"]) > max(3, 0.01 * c["cd_centi"]):
            ok = False
            notes.append(f"sweep {i}: Cd {w['cd_centi'] / 100:.2f} vs cold {c['cd_centi'] / 100:.2f}")
    print(f"{'PASS' if ok else 'FAIL'}  {name:42} {len(truths):3} sweeps  full fits {full:2}  "
          f"invalid {warm_invalid}/{cold_invalid}  worst Rsoil dev {worst * 100:6.3f} %  "
          f"evaluations {warm_evals:6} vs {cold_evals:6} ({warm_evals / cold_evals:5.1%})")
    for note in notes[:6]:
        print("        " + note)
    return ok, warm_evals, cold_evals


def decade_ramp(truth, start_ohm: float, decades: float, count: int) -> list:
    return [with_rsoil(truth, start_ohm * 10 ** (decades * i / (count - 1))) for i in range(count)]


def main() -> int:
    n = FIT_FULL_EVERY_N
    print("Each row: the warm-start logic against four cold starts on the same synthetic sweeps.\n"
          "'invalid' is warm path / cold-only.\n")
    results = [
        scenario("air, steady", [AIR] * (6 * n)),
        scenario("air, steady, other noise", [AIR] * (6 * n), seed=3),
        scenario("moist soil, steady", [SOIL] * (3 * n)),
        scenario("damp towel, steady", [TOWEL] * (2 * n)),
        scenario("hand grip, steady", [HAND] * n),
        scenario("soil, Rsoil +-20 % thermal cycle",
                 [with_rsoil(SOIL, 355.0 * (1 + 0.2 * math.sin(2 * math.pi * i / 24)))
                  for i in range(3 * n)]),
        scenario("air, then into moist soil at sweep 5", [AIR] * 5 + [SOIL] * 8, frozenset({5})),
        scenario("air, then a damp towel at sweep 5", [AIR] * 5 + [TOWEL] * 8, frozenset({5})),
        scenario("soil at 2 kohm, watered to 355 ohm at 5",
                 [with_rsoil(SOIL, 2000.0)] * 5 + [SOIL] * 8, frozenset({5})),
        scenario("soil, then pulled out into air at 5", [SOIL] * 5 + [AIR] * 8, frozenset({5})),
        scenario("towel drying 5.9 kohm -> open", decade_ramp(TOWEL, 5900.0, 2.7, 40)),
        scenario("soil drying 355 ohm -> 100 kohm", decade_ramp(SOIL, 355.0, 2.45, 40)),
        scenario("conduction creeping in from open", list(reversed(decade_ramp(DAMP, 10e3, 2.0, 30)))),
    ]

    # Scale against the one measured time there is: four cold starts in air are the measurement
    # wake (2.7 s on board 1) less the sweep (0.65 s, modelled), 2.05 s.
    rng = random.Random(1)
    counts = {}
    for label, truth in (("air", AIR), ("moist soil", SOIL)):
        probe = Probe(True)
        Counter.evals = 0
        probe.measure(sweep(truth, rng), CP)
        cold = Counter.evals
        warm = []
        for _ in range(8):
            Counter.evals = 0
            probe.measure(sweep(truth, rng), CP)
            warm.append(Counter.evals)
        counts[label] = (cold, sum(warm) / len(warm))
    per_eval = 2.05 / counts["air"][0]
    print(f"\nModel evaluations per fit, and the time if four cold starts in air are the 2.05 s they\n"
          f"are taken to be ({per_eval * 1e3:.2f} ms per evaluation):")
    for label, (cold, warm) in counts.items():
        average = (cold + (n - 1) * warm) / n
        print(f"  {label:11} four cold starts {cold:4} = {cold * per_eval:4.2f} s   warm {warm:5.1f} = "
              f"{warm * per_eval:4.2f} s   average with one full fit in {n}: {average * per_eval:4.2f} s "
              f"({average / cold:4.0%})")

    print("\nOpen anchor, then the medium starts to conduct a little (the case a warm fit could miss):")
    for ohms in (300e3, 200e3, 100e3, 50e3, 20e3, 10e3):
        rng = random.Random(5)
        warm_probe, cold_probe = Probe(True), Probe(False)
        warm_probe.measure(sweep(AIR, rng), CP)
        h = sweep(with_rsoil(DAMP, ohms), rng)
        w, c = warm_probe.measure(h, CP), cold_probe.measure(h, CP)

        def text(row):
            if not row["valid"]:
                return "invalid"
            return "open" if row["open"] else f"{row['rsoil_deci'] / 10:.0f} ohm"
        print(f"  true {ohms / 1e3:4.0f} kohm: warm path ({'warm fit' if w.get('warm') else 'full fit'}) "
              f"-> {text(w):>11}   four cold starts -> {text(c):>11}")

    failed = sum(1 for ok, _, _ in results if not ok)
    print(f"\n{'FAILED' if failed else 'passed'}: {len(results) - failed} of {len(results)} scenarios")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
