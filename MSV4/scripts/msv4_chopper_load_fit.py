"""Refit MSV4 raw sweeps with U4's switched R18 load modelled exactly (board 5 air leak test).

The firmware fit (specModel) treats SENSE as a divider: H = 1 / (1 + Rs*(Ye + jw*Cp)). On the
board, U4 also connects SENSE to R18 (100 kohm into C18, 1 nF) for half of every drive period
and parks R18 on ground for the other half. That load is not in the model. This script solves
the switched circuit exactly in periodic steady state and fits Ye = jw*Cd + series(Gs, Cm) with
and without the load, to see whether the "Rsoil" a 5.6 kohm board reports in air is that load.

Circuit, per firmware frontEndStart() and MSV4/hardware/reference/PIN_NET_REFERENCE.md:
  DRV  high on timer ticks [0, n/2), low on [n/2, n); 16 MHz ticks.
  READ_CNTL high on [k, k + n/2) mod n. High = Sel 1 = both filter inputs parked on GND,
  so R18 is connected to SENSE on the other half.
  ADC reads the filter caps after a 2 ms (20 tau) settle; modelled as the period mean.
States: v (SENSE), vcm (across Cm), vc (C18), z (integral of vc), plus a constant 1.

Run --self-test first: with the load term removed the simulator must reproduce specModel's
harmonic sum (computed to many harmonics) to better than 1e-5.
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import numpy as np
from scipy.linalg import expm
from scipy.optimize import least_squares

FE_CLOCK_HZ = 16_000_000
SPEC_N = [64, 48, 32, 24, 16, 12, 8, 6]
R18 = 100e3
C18 = 1e-9


def spec_phases(n: int) -> int:
    return next((m for m in (16, 12, 8, 6, 4) if m <= n and n % m == 0), 0)


# ------------------------------------------------------------------------- firmware model ---
def node_h(w, p, cp, rs):
    cm, gs, cd = p
    ye = 1j * w * cd + 1.0 / (1.0 / gs + 1.0 / (1j * w * cm))
    return 1.0 / (1.0 + rs * (ye + 1j * w * cp))


def spec_model(n, m, p, cp, rs, kmax=8, hmax=63):
    """specModel: aliased bin 1 of a square drive sampled at m phases (firmware truncation)."""
    w1 = 2 * math.pi * FE_CLOCK_HZ / n
    num, den = 0j, 0.0
    for k in range(-kmax, kmax + 1):
        h = 1 + k * m
        if h % 2 == 0 or abs(h) > hmax:
            continue
        wt = 1.0 / h ** 2
        hv = node_h(w1 * abs(h), p, cp, rs)
        num += wt * (hv.conjugate() if h < 0 else hv)
        den += wt
    return num / den


# ------------------------------------------------------------------ switched-load simulator ---
def _a_matrix(ct, gs, cm, rs, w, d, load):
    """5x5 augmented system for one segment: drive level d (0/1), filter connected w (0/1)."""
    g18 = 1.0 / R18
    a = np.zeros((5, 5))
    # SENSE node
    a[0, 0] = (-1.0 / rs - gs - (w * g18 if load else 0.0)) / ct
    a[0, 1] = gs / ct
    a[0, 2] = (w * g18 / ct) if load else 0.0
    a[0, 4] = d / rs / ct
    # series branch capacitor
    a[1, 0] = gs / cm
    a[1, 1] = -gs / cm
    # C18: through R18 to SENSE when connected, to ground otherwise
    a[2, 0] = w / (R18 * C18)
    a[2, 2] = -1.0 / (R18 * C18)
    # running integral of vc
    a[3, 2] = 1.0
    return a


def sim_h(n, m, p, cp, rs, load=True):
    """Simulated H at timer period n: X1(mean C18) / X1(mean C19) over m chopper phases."""
    cm, gs, cd = p
    ct = cp + cd
    tick = 1.0 / FE_CLOCK_HZ
    half = n // 2
    cache = {}

    def seg(w, d, ticks):
        key = (w, d, ticks)
        if key not in cache:
            cache[key] = expm(_a_matrix(ct, gs, cm, rs, w, d, load) * ticks * tick)
        return cache[key]

    xs, xd = 0j, 0j
    for i in range(m):
        k = (i * n) // m
        # connected window = chopper LOW = [k + half, k + n) mod n
        def w_at(t):
            return 0 if ((t - k) % n) < half else 1

        edges = sorted({0, half, k % n, (k + half) % n, n})
        phi = np.eye(5)
        segs = []
        for t0, t1 in zip(edges[:-1], edges[1:]):
            if t1 == t0:
                continue
            e = seg(w_at(t0), 1 if t0 < half else 0, t1 - t0)
            segs.append(e)
            phi = e @ phi
        # periodic steady state of states 0..2 (z is reset; 1 stays 1)
        a = phi[:3, :3]
        c = phi[:3, 4]
        x0 = np.zeros(5)
        # lstsq: with the branch open (Gs -> 0) vcm decouples and its periodic state is
        # undefined (eigenvalue 1); the minimum-norm answer leaves v and vc exact.
        x0[:3] = np.linalg.lstsq(np.eye(3) - a, c, rcond=1e-13)[0]
        x0[4] = 1.0
        x = x0.copy()
        for e in segs:
            x = e @ x
        mean_vc = x[3] / (n * tick)
        # C19 sees DRV (stiff) through R19 when connected: mean = overlap of window and DRV high / n
        overlap = sum(1 for t in range(n) if w_at(t) and t < half) / n
        th = 2 * math.pi * i / m
        e_th = complex(math.cos(th), -math.sin(th))
        xs += mean_vc * e_th
        xd += overlap * e_th
    return xs / xd


# --------------------------------------------------------------------------------- fitting ---
def fit(h, rs, cp, model, starts):
    """Fit [Cm, Gs, Cd] in log space; returns (params, rms, ok)."""
    def resid(q):
        p = np.exp(q)
        r = np.array([model(n, spec_phases(n), p, cp, rs) - hv for n, hv in zip(SPEC_N, h)])
        return np.concatenate([r.real, r.imag])

    # Bounds keep the matrix exponentials finite: Gs down to 1e-12 S (1 Tohm) is "open".
    lo = np.log([1e-15, 1e-12, 1e-15])
    hi = np.log([1e-8, 1e-1, 1e-10])
    best = None
    for p0 in starts:
        q0 = np.clip(np.log(p0), lo + 1e-9, hi - 1e-9)
        s = least_squares(resid, q0, bounds=(lo, hi), x_scale=1.0, max_nfev=400)
        rms = math.sqrt(np.mean(s.fun[:8] ** 2 + s.fun[8:] ** 2))
        if best is None or rms < best[1]:
            best = (np.exp(s.x), rms)
    return best


def fit_cp_only(h, rs, model):
    """Electrodes floating: fit Cp alone (Cd fixed tiny, branch open)."""
    def resid(q):
        p = (1e-15, 1e-16, 1e-18)
        r = np.array([model(n, spec_phases(n), p, math.exp(q[0]), rs) - hv for n, hv in zip(SPEC_N, h)])
        return np.concatenate([r.real, r.imag])
    s = least_squares(resid, [math.log(9e-12)])
    return math.exp(s.x[0]), math.sqrt(np.mean(s.fun[:8] ** 2 + s.fun[8:] ** 2))


STARTS = [(500e-12, g, 5e-12) for g in (1e-5, 1e-4, 1e-3, 1e-2)] + [(20e-12, 7e-6, 20e-12),
                                                                    (20e-12, 1e-9, 20e-12)]


def firmware_model(n, m, p, cp, rs):
    return spec_model(n, m, p, cp, rs)


def loaded_model(n, m, p, cp, rs):
    return sim_h(n, m, p, cp, rs, load=True)


def self_test() -> None:
    rs = 5600.0
    for p, cp in (((20e-12, 1 / 130e3, 15e-12), 8.9e-12), ((60e-12, 1 / 5e3, 25e-12), 0.0),
                  ((1e-12, 1e-16, 1e-18), 9e-12)):
        worst = 0.0
        for n in SPEC_N:
            m = spec_phases(n)
            a = sim_h(n, m, p, cp, rs, load=False)
            b = spec_model(n, m, p, cp, rs, kmax=4000, hmax=10 ** 9)
            worst = max(worst, abs(a - b))
        print(f"self-test no-load vs full harmonic sum: worst |dH| = {worst:.2e}")
        assert worst < 1e-5, "simulator does not reproduce the divider without the load"
    print("self-test passed")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--sweep-csv", type=Path, help="msv4_adv_scan.py --sweep-csv output")
    ap.add_argument("--rs", type=float, default=5600.0)
    ap.add_argument("--cp", type=float, default=0.0, help="applied Cp in pF (firmware's)")
    ap.add_argument("--limit", type=int, default=0, help="fit at most this many sweeps")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    rows = list(csv.DictReader(args.sweep_csv.open()))
    if args.limit:
        rows = rows[-args.limit:]
    cp = args.cp * 1e-12
    print("counter | firmware model: Rsoil, Cm, Cd, rms | with switched R18: Rsoil, Cm, Cd, rms")
    for r in rows:
        h = [complex(float(r[f"re{i}"]), float(r[f"im{i}"])) for i in range(8)]
        out = []
        for model in (firmware_model, loaded_model):
            p, rms = fit(h, args.rs, cp, model, STARTS)
            rsoil = 1 / p[1]
            rtxt = "open" if rsoil >= 4e5 else f"{rsoil / 1e3:.1f}k"
            out.append(f"{rtxt:>8} {p[0] * 1e12:7.1f}pF {p[2] * 1e12:6.2f}pF {rms:.5f}")
        print(f"{r['counter']:>7} | {out[0]} | {out[1]}")


if __name__ == "__main__":
    main()
