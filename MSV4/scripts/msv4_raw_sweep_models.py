"""Fit candidate electrode models to raw MSV4 sweeps captured with the bring-up console.

Why: the firmware's lumped model, Ye = jw*Cd + 1/(1/(jw*Cm) + 1/Gs), traded capacitance between
Cm and Cd as salt went into a cup of water (MSV4/docs/MSV4_BOARD2_CUP_WATER_SALT_TEST_2026-10-14.md),
so its Cd is not only the board's own direct capacitance. Already in plain tap water, with the
interface corner well inside the band, Cd read 124 pF against 13.6 pF in air. The question here:
does a model that holds Cd at the board's air value and gives the mask a shape parameter describe
the measured points as well (residual at the repeat-to-repeat noise), with parameters that stay
put when only the salt changes?

Input is the bring-up console's own print of every measured point (`spectrum open`,
`spectrum ab`), one file per medium, any number of sweeps in each:
      250000 Hz M=16 |H| 0.4123 ph  -31.20 deg
for example
  MSV3/.arduino-build/python-portable/runtime/python.exe MSV4/scripts/msv4_ble_console.py \\
      --device MSV4-BU-6528 --cmd "spectrum ab" --cmd "spectrum ab" --cmd "spectrum ab" > tap.txt
The console's printed fit assumes R15 = 1 kohm; the raw points do not depend on R15, which is
given here with --rs.

Models (Cd0 = the board's own direct capacitance: --cd0, or the lumped fit's Cd in the air sweeps):
  lumped        the firmware's model, Cd free         Cm, Gs, Cd
  lumped-fixed  the firmware's model, Cd = Cd0        Cm, Gs
  cpe           Cd0 || (R + CPE(Q, n))                R, Q, n       n = 1 is an ideal capacitor
  tline         Cd0 || (R0 + open RC line(Rl, Cl))    R0, Rl, Cl    de Levie line along the plate
  two-branch    Cd0 || (C1 + R1) || (C2 + R2)         C1, R1, C2, R2

The measurement model (harmonics folding into bin 1) is the firmware's, reused from
test_msv4_spectrum_fit.py, which mirrors specModel().

    python MSV4/scripts/msv4_raw_sweep_models.py --rs 3300 --open open.txt air=air.txt tap=tap.txt salt1=salt1.txt
    python MSV4/scripts/msv4_raw_sweep_models.py --self-test
"""
from __future__ import annotations

import argparse
import math
import re
import statistics
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

from test_msv4_spectrum_fit import FE_CLOCK_HZ, harmonics, spec_phases

POINT_RE = re.compile(r"(\d+)\s*Hz\s+M=\s*(\d+)\s+\|H\|\s+([\d.]+)\s+ph\s+([+-]?[\d.]+)\s*deg")
PROBE_N = (64, 48, 32, 24, 16, 12, 8, 6)   # the probe's sweep, 250 kHz .. 2.67 MHz


# --- input --------------------------------------------------------------------------------
def parse_sweeps(text: str) -> list[dict]:
    """Every complete sweep in a console log. A sweep restarts when the frequency stops rising."""
    sweeps, cur, last_f = [], [], None
    for line in text.splitlines():
        m = POINT_RE.search(line)
        if not m:
            continue
        f, mm, mag, ph = float(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4))
        if last_f is not None and f <= last_f:
            sweeps.append(cur)
            cur = []
        cur.append((round(FE_CLOCK_HZ / f), mm, mag * np.exp(1j * math.radians(ph))))
        last_f = f
    if cur:
        sweeps.append(cur)
    full = max((len(s) for s in sweeps), default=0)
    return [{"n": np.array([p[0] for p in s]), "m": np.array([p[1] for p in s]),
             "h": np.array([p[2] for p in s])} for s in sweeps if len(s) == full]


def format_point(n: int, m: int, h: complex) -> str:
    """The console's line for one point (specPrintPoints), for the self-test."""
    return f"  {FE_CLOCK_HZ / n:7.0f} Hz M={m:2d} |H| {abs(h):.4f} ph {math.degrees(np.angle(h)):+7.2f} deg"


# --- measurement model ----------------------------------------------------------------------
def predict(ns, ms, ye, rs: float, cp: float) -> np.ndarray:
    """Aliased bin-1 transfer for every (n, m), with the electrode admittance ye(w)."""
    out = np.empty(len(ns), dtype=complex)
    for i, (n, m) in enumerate(zip(ns, ms)):
        w1 = 2.0 * np.pi * FE_CLOCK_HZ / n
        num, den = 0j, 0.0
        for h, wt in harmonics(int(n), int(m)):
            w = w1 * abs(h)
            hv = 1.0 / (1.0 + rs * (ye(w) + 1j * w * cp))
            num += wt * (np.conj(hv) if h < 0 else hv)
            den += wt
        out[i] = num / den
    return out


def _cpe(w, q, n):
    return q * (1j * w) ** n


def _line(w, rl, cl):
    k = np.sqrt(1j * w * rl * cl)
    return np.sqrt(rl / (1j * w * cl)) / np.tanh(k)


# name: (parameter names, log-scaled flags, admittance(w, p, cd0))
MODELS = {
    "lumped": (("Cm", "Gs", "Cd"), (True, True, True),
               lambda w, p, cd0: 1j * w * p[2] + 1.0 / (1.0 / (1j * w * p[0]) + 1.0 / p[1])),
    "lumped-fixed": (("Cm", "Gs"), (True, True),
                     lambda w, p, cd0: 1j * w * cd0 + 1.0 / (1.0 / (1j * w * p[0]) + 1.0 / p[1])),
    "cpe": (("R", "Q", "n"), (True, True, False),
            lambda w, p, cd0: 1j * w * cd0 + 1.0 / (p[0] + 1.0 / _cpe(w, p[1], p[2]))),
    "tline": (("R0", "Rl", "Cl"), (True, True, True),
              lambda w, p, cd0: 1j * w * cd0 + 1.0 / (p[0] + _line(w, p[1], p[2]))),
    "two-branch": (("C1", "R1", "C2", "R2"), (True, True, True, True),
                   lambda w, p, cd0: 1j * w * cd0 + 1.0 / (1.0 / (1j * w * p[0]) + p[1])
                   + 1.0 / (1.0 / (1j * w * p[2]) + p[3])),
}
BOUNDS = {"C": (1e-14, 1e-7), "G": (1e-9, 1.0), "R": (1e-1, 1e8), "Q": (1e-14, 1e-5), "n": (0.3, 1.0)}


def _bounds(names):
    lo, hi = [], []
    for name in names:
        b = BOUNDS["n" if name == "n" else name[0]]
        if name == "n":
            lo.append(b[0]); hi.append(b[1])
        else:
            lo.append(math.log10(b[0])); hi.append(math.log10(b[1]))
    return np.array(lo), np.array(hi)


def fit_model(name: str, sweep: dict, rs: float, cp: float, cd0: float, starts: list) -> dict:
    names, logged, ye = MODELS[name]
    lo, hi = _bounds(names)

    def unpack(x):
        return [10.0 ** v if lg else v for v, lg in zip(x, logged)]

    def resid(x):
        p = unpack(x)
        d = predict(sweep["n"], sweep["m"], lambda w: ye(w, p, cd0), rs, cp) - sweep["h"]
        return np.concatenate([d.real, d.imag])

    best = None
    for s in starts:
        x0 = np.clip([math.log10(v) if lg else v for v, lg in zip(s, logged)], lo + 1e-9, hi - 1e-9)
        try:
            r = least_squares(resid, x0, bounds=(lo, hi), x_scale="jac", max_nfev=3000)
        except (ValueError, FloatingPointError, ZeroDivisionError):
            continue
        if np.isfinite(r.cost) and (best is None or r.cost < best.cost):
            best = r
    p = unpack(best.x)
    return {"p": dict(zip(names, p)), "rms": float(np.sqrt(np.mean(resid(best.x) ** 2)))}


def seeds(name: str, lump: dict, cd0: float) -> list:
    """Starts for each model from the lumped fit, so every model begins near the data."""
    cm, gs, cd = lump["Cm"], lump["Gs"], lump["Cd"]
    r = 1.0 / gs
    ctot = max(cm + cd - cd0, cm)
    cx = max(cd - cd0, 1e-12)
    if name == "lumped-fixed":
        return [(ctot, gs), (cm, gs), (ctot, gs * 3), (ctot, gs / 3)]
    if name == "cpe":
        return [(r * k, ctot ** n * r ** (n - 1.0), n) for n in (0.97, 0.9, 0.8) for k in (0.3, 1.0)]
    if name == "tline":
        return [(r * a, r * b, ctot) for a, b in ((0.3, 1.0), (0.1, 2.0), (0.6, 0.5), (0.01, 3.0))]
    if name == "two-branch":
        return [(cm, r, cx, r / 5), (0.8 * ctot, r, 0.2 * ctot, 5 * r), (0.5 * ctot, r / 2, 0.5 * ctot, 2 * r)]
    raise KeyError(name)


def fit_all(sweep: dict, rs: float, cp: float, cd0: float | None) -> dict:
    lump_starts = [(500e-12, g, 5e-12) for g in (1e-5, 1e-4, 1e-3, 1e-2)]
    out = {"lumped": fit_model("lumped", sweep, rs, cp, 0.0, lump_starts)}
    if cd0 is None:
        return out
    for name in ("lumped-fixed", "cpe", "tline", "two-branch"):
        out[name] = fit_model(name, sweep, rs, cp, cd0, seeds(name, out["lumped"]["p"], cd0))
    return out


def fit_cp(sweeps: list[dict], rs: float) -> float:
    """Cp from `spectrum open` sweeps: electrodes floating, so the electrode admittance is zero."""
    values = []
    for s in sweeps:
        def resid(x):
            d = predict(s["n"], s["m"], lambda w: 0.0, rs, 10.0 ** x[0]) - s["h"]
            return np.concatenate([d.real, d.imag])
        values.append(10.0 ** least_squares(resid, [math.log10(10e-12)], bounds=([-13], [-9])).x[0])
    return statistics.median(values)


def noise_rms(sweeps: list[dict]) -> float | None:
    """Per-component scatter of repeated sweeps about their mean (needs at least two)."""
    if len(sweeps) < 2:
        return None
    hs = np.array([s["h"] for s in sweeps])
    d = hs - hs.mean(axis=0)
    comp = np.concatenate([d.real.ravel(), d.imag.ravel()])
    return float(np.sqrt(np.sum(comp ** 2) / (comp.size * (len(sweeps) - 1) / len(sweeps))))


def derived(name: str, p: dict, cd0: float) -> dict:
    """The capacitance each model attributes to the mask, and a resistance for the water."""
    if name == "lumped":
        return {"mask C": p["Cm"] + p["Cd"] - cd0, "R": 1.0 / p["Gs"], "Cd": p["Cd"]}
    if name == "lumped-fixed":
        return {"mask C": p["Cm"], "R": 1.0 / p["Gs"]}
    if name == "cpe":
        # The CPE's capacitance at a fixed 1 MHz, Q * w^(n-1): it depends on the mask alone. Brug's
        # effective capacitance, Q^(1/n) R^((1-n)/n), also carries R, so it moved with salt in the
        # self-test while the mask did not.
        return {"mask C": p["Q"] * (2.0 * math.pi * 1e6) ** (p["n"] - 1.0), "R": p["R"], "n": p["n"]}
    if name == "tline":
        return {"mask C": p["Cl"], "R": p["R0"] + p["Rl"] / 3.0}
    return {"mask C": p["C1"] + p["C2"], "R": 1.0 / (1.0 / p["R1"] + 1.0 / p["R2"])}


# --- report -------------------------------------------------------------------------------
def mean_sweep(sweeps: list[dict]) -> dict:
    return {"n": sweeps[0]["n"], "m": sweeps[0]["m"], "h": np.mean([s["h"] for s in sweeps], axis=0)}


def report(media: dict[str, list[dict]], rs: float, cp: float, cd0: float) -> dict:
    """Each model fitted to every sweep (stability) and to the medium's averaged sweep (shape).

    The probe's own fit residual in air is about 0.007 of drive, so one sweep cannot tell these
    models apart; averaging N repeats divides the random part of that by sqrt(N). A model is
    describing the electrode when its residual on the AVERAGED sweep is at the averaged noise
    (ratio near 1). A residual that stays well above it is model error, or a systematic error of
    the measurement itself that averaging cannot remove, which all models then share.
    """
    summary = {}
    for medium, sweeps in media.items():
        noise = noise_rms(sweeps)
        noise_avg = noise / math.sqrt(len(sweeps)) if noise else None
        fits = [fit_all(s, rs, cp, cd0) for s in sweeps]
        avg = fit_all(mean_sweep(sweeps), rs, cp, cd0)
        print(f"\n{medium}: {len(sweeps)} sweep(s); repeat noise "
              + (f"{noise * 1e4:.1f}e-4 of drive, {noise_avg * 1e4:.1f}e-4 on the average" if noise else "unknown (one sweep)"))
        print(f"  {'model':12} {'avg rms':>8} {'/noise':>7}   mask C pF (spread)   R ohm (spread)      other")
        summary[medium] = {}
        spread = lambda xs: (max(xs) - min(xs)) / statistics.median(xs) * 50 if len(xs) > 1 else 0.0
        # Half the range across sweeps, as a percentage; a model whose values jump by more than the
        # value itself on repeats is not identifying them from this data.
        pct = lambda v: "+/- >99%" if v > 99 else f"+/-{v:4.1f}%"
        for name in MODELS:
            d_avg = derived(name, avg[name]["p"], cd0)
            ds = [derived(name, f[name]["p"], cd0) for f in fits]
            c = [d["mask C"] * 1e12 for d in ds]
            r = [d["R"] for d in ds]
            other = f"n {d_avg['n']:.3f}" if "n" in d_avg else (f"Cd {d_avg['Cd'] * 1e12:.1f} pF" if "Cd" in d_avg else "")
            rms = avg[name]["rms"]
            print(f"  {name:12} {rms * 1e4:8.1f} {rms / noise_avg if noise_avg else float('nan'):7.2f}   "
                  f"{d_avg['mask C'] * 1e12:8.1f} ({pct(spread(c))})  {d_avg['R']:8.1f} ({pct(spread(r))})   {other}")
            summary[medium][name] = {"rms": rms, "noise": noise_avg, "mask C": d_avg["mask C"] * 1e12, "R": d_avg["R"],
                                     **({"n": d_avg["n"]} if "n" in d_avg else {})}
    if len(media) > 1:
        print("\nmask capacitance by medium (pF): a model whose mask C moves with salt alone is not separating it")
        print(f"  {'model':12} " + " ".join(f"{m:>10}" for m in media))
        for name in MODELS:
            print(f"  {name:12} " + " ".join(f"{summary[m][name]['mask C']:10.1f}" for m in media))
    return summary


# --- self-test ----------------------------------------------------------------------------
def _synthetic(ye, rs, cp, cd0, sigma, count, rng):
    ns = np.array(PROBE_N)
    ms = np.array([spec_phases(n) for n in PROBE_N])
    clean = predict(ns, ms, lambda w: ye(w, cd0), rs, cp)
    return [{"n": ns, "m": ms, "h": clean + rng.normal(0, sigma, clean.size) + 1j * rng.normal(0, sigma, clean.size)}
            for _ in range(count)]


def self_test() -> None:
    rng = np.random.default_rng(7)
    rs, cp, cd0, sigma = 3300.0, 8.74e-12, 13.6e-12, 3e-4
    failures = []

    # 1. The parser reads back what the console prints.
    s = _synthetic(lambda w, c: MODELS["lumped"][2](w, [600e-12, 1 / 500, 120e-12], c), rs, cp, cd0, 0.0, 2, rng)
    text = "\n".join(format_point(n, m, h) for sw in s for n, m, h in zip(sw["n"], sw["m"], sw["h"]))
    back = parse_sweeps("spectrum ab\n" + text + "\nfit (Cp ...)\n")
    if len(back) != 2 or np.max(np.abs(back[0]["h"] - s[0]["h"])) > 2e-4 or list(back[0]["n"]) != list(PROBE_N):
        failures.append("parser round trip")

    # 2. Cp is recovered from open sweeps.
    opens = _synthetic(lambda w, c: 0.0, rs, cp, cd0, sigma, 3, rng)
    if abs(fit_cp(opens, rs) / cp - 1) > 0.03:
        failures.append(f"Cp {fit_cp(opens, rs) * 1e12:.2f} pF, true 8.74")

    # 3. Truth is an ideal mask (lumped with Cd = Cd0): every model fits, CPE finds n near 1.
    truth = lambda w, c: MODELS["lumped-fixed"][2](w, [700e-12, 1 / 420.0], c)
    sw = _synthetic(truth, rs, cp, cd0, sigma, 4, rng)
    fits = fit_all(sw[0], rs, cp, cd0)
    noise = noise_rms(sw)
    if fits["lumped-fixed"]["rms"] / noise > 1.5:
        failures.append("ideal mask: lumped-fixed did not fit")
    if fits["cpe"]["p"]["n"] < 0.97 or abs(derived("cpe", fits["cpe"]["p"], cd0)["mask C"] / 700e-12 - 1) > 0.05:
        failures.append(f"ideal mask: cpe n {fits['cpe']['p']['n']:.3f}")
    if abs(fits["lumped"]["p"]["Cd"] / cd0 - 1) > 0.25:
        failures.append(f"ideal mask: lumped Cd {fits['lumped']['p']['Cd'] * 1e12:.1f} pF, true 13.6")

    # 4. Truth is a spread-out mask (CPE, n = 0.88): the free lumped model puts extra capacitance
    #    into Cd, the fixed lumped model cannot fit, and the CPE model recovers the truth.
    q, n, r = 1.0e-9, 0.88, 420.0
    truth = lambda w, c: MODELS["cpe"][2](w, [r, q, n], c)
    sw = _synthetic(truth, rs, cp, cd0, sigma, 4, rng)
    fits = fit_all(sw[0], rs, cp, cd0)
    noise = noise_rms(sw)
    if fits["cpe"]["rms"] / noise > 1.5 or abs(fits["cpe"]["p"]["n"] - n) > 0.03 or abs(fits["cpe"]["p"]["R"] / r - 1) > 0.1:
        failures.append(f"cpe truth: cpe fit n {fits['cpe']['p']['n']:.3f} R {fits['cpe']['p']['R']:.0f} "
                        f"rms/noise {fits['cpe']['rms'] / noise:.2f}")
    if fits["lumped-fixed"]["rms"] / noise < 3:
        failures.append(f"cpe truth: lumped-fixed fit too well ({fits['lumped-fixed']['rms'] / noise:.2f})")
    if fits["lumped"]["p"]["Cd"] < cd0 + 5e-12:
        failures.append(f"cpe truth: lumped Cd {fits['lumped']['p']['Cd'] * 1e12:.1f} pF did not absorb the spread")

    # 5. Same mask, only the water's resistance changed (salt): the CPE's mask capacitance must not move.
    salty = _synthetic(lambda w, c: MODELS["cpe"][2](w, [r / 3, q, n], c), rs, cp, cd0, sigma, 1, rng)[0]
    c_tap = derived("cpe", fits["cpe"]["p"], cd0)["mask C"]
    c_salt = derived("cpe", fit_all(salty, rs, cp, cd0)["cpe"]["p"], cd0)["mask C"]
    if abs(c_salt / c_tap - 1) > 0.02:
        failures.append(f"cpe mask C moved with R alone: {c_tap * 1e12:.1f} -> {c_salt * 1e12:.1f} pF")

    print("self-test: " + ("passed" if not failures else "FAILED: " + "; ".join(failures)))
    if failures:
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("media", nargs="*", help="name=console_log.txt, one per medium; 'air' sets Cd0")
    parser.add_argument("--rs", type=float, help="R15 in ohms (board 2: 3300)")
    parser.add_argument("--open", type=Path, help="console log of `spectrum open` sweeps, for Cp")
    parser.add_argument("--cp", type=float, help="Cp in pF, instead of --open")
    parser.add_argument("--cd0", type=float, help="the board's own direct capacitance in pF, instead of the air sweeps")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.rs or not args.media or not (args.open or args.cp):
        parser.error("need --rs, --open or --cp, and at least one name=file")
    media = {}
    for item in args.media:
        name, _, path = item.partition("=")
        media[name] = parse_sweeps(Path(path).read_text(encoding="utf-8", errors="replace"))
        if not media[name]:
            parser.error(f"no complete sweeps in {path}")
    cp = args.cp * 1e-12 if args.cp else fit_cp(parse_sweeps(args.open.read_text(encoding="utf-8")), args.rs)
    if args.cd0:
        cd0 = args.cd0 * 1e-12
    elif "air" in media:
        cd0 = statistics.median(fit_all(s, args.rs, cp, None)["lumped"]["p"]["Cd"] for s in media["air"])
    else:
        parser.error("give --cd0 or an 'air' medium")
    print(f"R15 {args.rs:.0f} ohm, Cp {cp * 1e12:.2f} pF, Cd0 {cd0 * 1e12:.2f} pF")
    report(media, args.rs, cp, cd0)


if __name__ == "__main__":
    main()
