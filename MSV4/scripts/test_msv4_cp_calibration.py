#!/usr/bin/env python3
"""Synthetic test of WHEN the production image calibrates Cp (application 262160).

Up to 262159 the image measured Cp once at every boot, electrodes floating, whatever the blade was
in. That sweep also sees what the electrodes are touching, through U3's off-capacitance: on
2026-10-08 board 1 reset with a hand on it, calibrated 15.89 pF against 8.66-8.83 pF in air, and
reported Cd 7 pF low until its next boot.

From 262160 `calibrateCpAfterOpenReading()` takes the calibration straight after the first reading
of a boot that finds nothing conducting. Until then Cp is taken as zero, so Cd is the whole node
capacitance.

This runs that rule, and the old one, on identical synthetic sweeps and compares each with a probe
that was booted in air and never disturbed. The fit is the replica in test_msv4_warm_fit.py; the
rule's own lines are checked as source text, so this cannot quietly test something the sketch no
longer does.

What is synthetic: every sweep (see test_msv4_warm_fit.py). The hand figures are board 1's own
packet of 2026-10-08 15:59. What the floating sweep reads in moist soil has never been measured;
the hand's 15.9 pF is used for it, and only the OLD rule's soil row depends on that. The invented
distortion also makes every fitted capacitance smaller than the one put in (8.8 pF of parasitics
calibrates as 5.4 pF), far more so in soil, so the capacitances here are not board values and in
the soil rows even the undisturbed probe has its Cd pinned at the fit's floor. What carries over
is the direction: the larger the Cp, the further Rsoil reads from the 355 ohm put in.

Run:  python MSV4/scripts/test_msv4_cp_calibration.py
"""

from __future__ import annotations

import random
import sys

import test_msv4_warm_fit as fit

_number, _require = fit._number, fit._require

SPEC_CP_DEFAULT = _number(r"#else\s+constexpr double SPEC_CP_DEFAULT = ([\de.-]+);")
SPEC_CP_START = _number(r"constexpr double SPEC_CP_START = ([\de.-]+);")
SPEC_CP_MIN = _number(r"constexpr double SPEC_CP_MIN = ([\de.-]+);")
SPEC_CP_MAX = _number(r"constexpr double SPEC_CP_MAX = ([\de.-]+);")
SPEC_CD_MIN_AFTER_CAL = _number(r"constexpr double SPEC_CD_MIN_AFTER_CAL = ([\de.-]+);")
SPEC_CAL_MAX_RMS = _number(r"constexpr double SPEC_CAL_MAX_RMS = ([\d.]+);")
# The rule this file mirrors. If any of these lines changes, so must Board below.
for line in (
    "static double g_specCp = SPEC_CP_DEFAULT;",
    "constexpr double SPEC_CP_DEFAULT = MSV4_CP_CENTI_PF * 1e-14;",
    "double p[3] = {1e-15, 1e-12, SPEC_CP_START}, sig[3];",
    "const bool freeP[3] = {false, false, true};",
    "specFit(s, p, freeP, 0.0, sig);",
    "if (p[2] < SPEC_CP_MIN || p[2] > SPEC_CP_MAX) return false;",
    "const bool saneOpenReading = rsoil >= FIT_OPEN_OHM && bestRms < SPEC_CAL_MAX_RMS;",
    "if (!g_cpCalibrated && saneOpenReading) cd = calibrateCpAfterOpenReading(cd);",
    "if (!measureCp(cp)) return cd;",
    "const double shift = g_specCp - cp;",
    "if (cd + shift < SPEC_CD_MIN_AFTER_CAL) return cd;",
    "g_fitAnchor[2] += shift;",
    "if (!(g_fitAnchor[2] > 0.0)) g_fitAnchorValid = false;",
    "(g_cpCalibrated ? 0 : ADV_FLAG2_CP_DEFAULT);",
):
    _require(line)
# measureCp() has exactly one caller, and it sits behind the open test.
if fit.SKETCH.count("measureCp(") != 2:
    raise SystemExit("measureCp() is defined or called somewhere this test does not know about")


# ------------------------------------------------------------------ firmware replica --------
def measure_cp(hfloat: list[complex]) -> float | None:
    """`measureCp()`: one free parameter, Cd standing in for Cp."""
    p = [1e-15, 1e-12, SPEC_CP_START]
    fit.spec_fit(hfloat, p, [False, False, True], 0.0)
    if p[2] < SPEC_CP_MIN or p[2] > SPEC_CP_MAX:
        return None
    return p[2]


def _cd_centi(cd: float) -> int:
    return min(max(int(cd * 1e14 + 0.5), 1), 65535)


class Board:
    """`takeMoistureReading()` with the 262160 rule around the fit."""

    def __init__(self, _hfloat_at_boot=None, cp_build: float = SPEC_CP_DEFAULT):
        self.probe = fit.Probe(True)
        self.cp = cp_build          # zero, or -CpCentiPf for a probe updated while in soil
        self.calibrated = False

    def reading(self, hmeas: list[complex], hfloat: list[complex]) -> dict:
        out = self.probe.measure(hmeas, self.cp)
        if not out["valid"]:
            return out
        cd = out["best"][2]
        rsoil = 1.0 / out["best"][1]
        sane_open_reading = rsoil >= fit.FIT_OPEN_OHM and out["rms"] < SPEC_CAL_MAX_RMS
        if not self.calibrated and sane_open_reading:
            cd = self._calibrate_after_open(cd, hfloat)
        out["cd_centi"] = _cd_centi(cd)
        return out

    def _calibrate_after_open(self, cd: float, hfloat: list[complex]) -> float:
        cp = measure_cp(hfloat)
        if cp is None:
            return cd
        shift = self.cp - cp
        if cd + shift < SPEC_CD_MIN_AFTER_CAL:
            return cd
        self.cp = cp
        self.calibrated = True
        if self.probe.anchor is not None:
            self.probe.anchor[2] += shift
            if not self.probe.anchor[2] > 0.0:
                self.probe.anchor = None
        return cd + shift


class OldBoard:
    """Up to 262159: Cp from the floating sweep at boot, whatever the blade is in."""

    def __init__(self, hfloat_at_boot: list[complex]):
        self.probe = fit.Probe(True)
        cp = measure_cp(hfloat_at_boot)
        self.cp = 12e-12 if cp is None else cp
        self.calibrated = True

    def reading(self, hmeas: list[complex], hfloat: list[complex]) -> dict:
        return self.probe.measure(hmeas, self.cp)


# ------------------------------------------------------------------ synthetic instrument ----
AIR_FLOAT = fit.CP          # floating electrodes in air: the node's own parasitics
HAND_FLOAT = 15.9e-12       # the same sweep with a hand on the blade: board 1, 2026-10-08
# Board 1's first packet after that reset: 13.3 kohm, Cm 55.7 pF, and a node capacitance of
# 15.89 + 10.40 pF, which is Cd 17.5 pF on the air Cp.
TOUCH = (55.7e-12, 1 / 13296.0, 17.5e-12)
# "moved": in air, nothing conducting, but being carried while it measures, with a hand near
# enough to load the floating sweep. Board 1's one such reading (2026-10-08 16:39) was open with
# its residual off the packet's scale. Random noise does not do that to this fit (it can lower
# the residual), so the disturbance is fit.sweep()'s own distortion applied a second time at one
# and a half times its size, which takes the residual from 0.011 to 0.029 and leaves the reading
# open.
MEDIA = {"air": (fit.AIR, AIR_FLOAT), "hand": (TOUCH, HAND_FLOAT), "soil": (fit.SOIL, HAND_FLOAT),
         "moved": (fit.AIR, HAND_FLOAT)}
MOVED_EXTRA = 1.5


def floating_sweep(c_float: float, rng: random.Random) -> list[complex]:
    """Electrodes floating: the node sees one capacitance and nothing else."""
    return fit.sweep((1e-15, 1e-16, c_float - fit.CP), rng)


def measurement_sweep(medium: str, rng: random.Random) -> list[complex]:
    h = fit.sweep(MEDIA[medium][0], rng)
    if medium == "moved":
        xs = [(fit.FE_CLOCK_HZ / n) / fit.F_TOP for n in fit.SPEC_N]
        h = [v * complex(1.0 + MOVED_EXTRA * 0.060 * x * x, MOVED_EXTRA * 0.045 * x)
             for v, x in zip(h, xs)]
    return h


def run(name: str, steps: list[str], board_type, expect_calibrated_at: int | None,
        must_pass: bool = True, seed: int = 20261008) -> bool:
    """Feed one board and an undisturbed reference the same sweeps.

    The reference booted in air and calibrated there, which is what every boot of board 1 up to
    2026-10-08 15:59 did. Wherever the reference has a reading, the board passes when it
    - has one too, and agrees on open or conducting;
    - is no further from the resistance put in than the reference is (0.1 % of slack);
    - has the reference's Cd to 0.05 pF once it has calibrated, and the reference's Cd plus the
      reference's Cp before (not judged where the reference's own Cd sits on the fit's floor);
    - calibrated on the reading it was expected to.
    """
    rng = random.Random(seed)
    sweeps = [(measurement_sweep(m, rng), floating_sweep(MEDIA[m][1], rng)) for m in steps]
    reference = OldBoard(floating_sweep(AIR_FLOAT, rng))
    board = board_type(sweeps[0][1])
    cp_build = board.cp
    ok, notes = True, []
    calibrated_at = None
    worst_cd = err_board = err_ref = 0.0
    for i, (hmeas, hfloat) in enumerate(sweeps):
        was = board.calibrated
        got, ref = board.reading(hmeas, hfloat), reference.reading(hmeas, hfloat)
        if board.calibrated and not was:
            calibrated_at = i
        if not ref["valid"]:
            continue
        if not got["valid"]:
            ok = False
            notes.append(f"reading {i} ({steps[i]}): invalid where the reference is valid")
            continue
        if steps[i] == "moved" and not (got["open"] and got["rms"] >= SPEC_CAL_MAX_RMS):
            # Otherwise the row is not testing the residual gate at all.
            ok = False
            notes.append(f"reading {i}: the 'moved' sweep read open={got['open']} with residual "
                         f"{got['rms']:.4f}, so it did not exercise the residual limit")
        if got["open"] != ref["open"]:
            ok = False
            notes.append(f"reading {i} ({steps[i]}): open {got['open']} vs reference {ref['open']}")
        elif not ref["open"]:
            true_deci = 10.0 / MEDIA[steps[i]][0][1]
            mine = abs(got["rsoil_deci"] - true_deci) / true_deci
            theirs = abs(ref["rsoil_deci"] - true_deci) / true_deci
            err_board, err_ref = max(err_board, mine), max(err_ref, theirs)
            if mine > theirs + 0.001:
                ok = False
                notes.append(f"reading {i} ({steps[i]}): Rsoil {got['rsoil_deci'] / 10:.1f} ohm, reference "
                             f"{ref['rsoil_deci'] / 10:.1f}, put in {true_deci / 10:.1f}")
        if ref["cd_centi"] > 1:
            want = ref["cd_centi"] + (0 if board.calibrated else round((reference.cp - cp_build) * 1e14))
            dev = abs(got["cd_centi"] - want) / 100.0
            worst_cd = max(worst_cd, dev)
            if dev > 0.05:
                ok = False
                notes.append(f"reading {i} ({steps[i]}): Cd {got['cd_centi'] / 100:.2f} pF, "
                             f"{(got['cd_centi'] - ref['cd_centi']) / 100:+.2f} from the reference")
    if isinstance(board, Board) and calibrated_at != expect_calibrated_at:
        ok = False
        notes.append(f"calibrated at reading {calibrated_at}, expected {expect_calibrated_at}")
    if must_pass:
        verdict = "PASS" if ok else "FAIL"
    else:
        verdict = "wrong, as it was" if not ok else "UNEXPECTEDLY RIGHT"
    when = "at boot" if isinstance(board, OldBoard) else (
        "never" if calibrated_at is None else f"reading {calibrated_at}")
    print(f"{verdict:17} {name:44} Cp {board.cp * 1e12:5.2f} pF ({when:9})  Cd off by {worst_cd:4.2f} pF  "
          f"Rsoil error {err_board * 100:4.2f} % (reference {err_ref * 100:4.2f} %)")
    for note in notes[:2]:
        print("                    " + note)
    return ok == must_pass


def main() -> int:
    # What this synthetic board calibrates in air, as a previous boot would have reported it.
    own_cp = measure_cp(floating_sweep(AIR_FLOAT, random.Random(7)))
    with_own_cp = lambda boot: Board(boot, cp_build=own_cp)
    print("Reference: a probe booted in air and left alone. 'Rsoil error' is against the resistance\n"
          "put into the sweep; 'Cd off by' is against the reference (see run()).\n")
    results = [
        run("262160, boot in air", ["air"] * 14, Board, 0),
        run("262160, boot with a hand on it, then let go", ["hand"] * 2 + ["air"] * 12, Board, 2),
        run("262160, a hand at every reading", ["hand"] * 14, Board, None),
        run("262160, boot in moist soil, stays there", ["soil"] * 14, Board, None),
        run("262160, soil, out into air, back into soil", ["soil"] * 3 + ["air"] * 4 + ["soil"] * 7,
            Board, 3),
        run("262160, air, then two hours in soil", ["air"] * 2 + ["soil"] * 12, Board, 0),
        run("262160, boot while being carried, then set down", ["moved"] * 2 + ["air"] * 12, Board, 2),
        run("262160, hand, let go but still carried, set down",
            ["hand"] * 2 + ["moved"] + ["air"] * 11, Board, 3),
        # A probe updated while planted: the build carries the Cp it last calibrated in air.
        run("built with its own Cp, boots in soil, stays", ["soil"] * 14, with_own_cp, None),
        run("built with its own Cp, a hand at every reading", ["hand"] * 14, with_own_cp, None),
        run("built with its own Cp, soil, then out into air", ["soil"] * 3 + ["air"] * 11, with_own_cp, 3),
        run("262159, boot with a hand on it, then let go", ["hand"] * 2 + ["air"] * 12, OldBoard, None,
            must_pass=False),
        run("262159, boot in moist soil, stays there", ["soil"] * 14, OldBoard, None, must_pass=False),
    ]
    failed = results.count(False)
    print(f"\n{'FAILED' if failed else 'passed'}: {len(results) - failed} of {len(results)} rows behave as stated")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
