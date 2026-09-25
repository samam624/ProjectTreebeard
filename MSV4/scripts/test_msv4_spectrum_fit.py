"""Regression test for the MSV4 `spectrum` fit, and the evidence for dropping Cs.

Replicates the firmware's fit (`specNodeH` / `specModel` / `specFit` in
`MSV4_Probe_Bringup_BLE.ino`) in Python so a change to the estimator can be judged before it
is flashed. A bootloader-free app reflash is cheap, but a wrong estimator is silent -- it
returns plausible numbers -- so this exists to keep that from happening.

The question it was written to answer (2026-09-23): `Cs` (soil permittivity) is documented as
NOT identifiable below 4 MHz in moist soil, so should the firmware stop fitting it and report
conductance only? The trap is that an unidentifiable free parameter can still be doing useful
work -- soaking up model error that would otherwise land on `Rsoil`, the number that matters.
Removing it is only safe if `Rsoil` does not get worse.

Two stages, because the repo rule is to self-test an estimator on synthetic input before
trusting it:

  `lumped`  (instant)  -- truth generated from the same lumped model the fit assumes, so the
                          fit SHOULD recover the parameters exactly. Verifies the replica, then
                          asks what forcing Cs=0 does to Rsoil when the truth really has a Cs.
  `field`   (~20 min)  -- truth generated from the 2D field solve, i.e. distributed physics the
                          lumped model cannot represent. This is the realistic case: it carries
                          the ~0.2-1 % of drive model error that makes Cs unidentifiable in the
                          first place. Needs 104 field solves up to 132 MHz (the aliasing
                          harmonics), so it is slow and runs on demand.

Run: python MSV4/scripts/test_msv4_spectrum_fit.py [lumped|field|both]
"""
import os
import sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# --- constants mirrored from the firmware -------------------------------------------------
FE_CLOCK_HZ = 16_000_000
SPEC_RS = 1000.0                                  # R15
SPEC_N = [64, 48, 32, 24, 16, 12, 8, 6, 4]        # 250 kHz .. 4 MHz
CP_DEFAULT = 12e-12
L_PLATE_M = 0.0811


def spec_phases(n):
    """Firmware `specPhases`: largest usable phase count that divides n."""
    for m in (16, 12, 8, 6, 4):
        if m <= n and n % m == 0:
            return m
    return 0


def harmonics(n, m):
    """The (h, weight) pairs the firmware's `specModel` sums over. h<0 means conjugated."""
    out = []
    for k in range(-8, 9):
        h = 1 + k * m
        if h % 2 == 0 or h == 0 or h > 63 or h < -63:
            continue
        out.append((h, 1.0 / (h * h)))
    return out


def node_h(w, p, cp):
    """Firmware `specNodeH`. p = [Cm, Gs, Cs, Cd]. Divider transfer at the SENSE node."""
    cm, gs, cs, cd = p
    zm = 1.0 / (1j * w * cm)
    ys = gs + 1j * w * cs
    ye = 1j * w * cd + 1.0 / (zm + 1.0 / ys)
    return 1.0 / (1.0 + SPEC_RS * (ye + 1j * w * cp))


def model_h(n, m, p, cp):
    """Firmware `specModel`: aliased bin-1 for a square-wave drive sampled at m phases."""
    w1 = 2.0 * np.pi * FE_CLOCK_HZ / n
    num, den = 0j, 0.0
    for h, wt in harmonics(n, m):
        hv = node_h(w1 * abs(h), p, cp)
        if h < 0:
            hv = np.conj(hv)
        num += wt * hv
        den += wt
    return num / den


def aliased_from_callable(n, m, h_of_w):
    """Same aliasing sum, but H(w) comes from an arbitrary source (e.g. the field solve)."""
    w1 = 2.0 * np.pi * FE_CLOCK_HZ / n
    num, den = 0j, 0.0
    for h, wt in harmonics(n, m):
        hv = h_of_w(w1 * abs(h))
        if h < 0:
            hv = np.conj(hv)
        num += wt * hv
        den += wt
    return num / den


# --- the fit ------------------------------------------------------------------------------
# Mirrors the firmware: log-space parameters (keeps them positive, makes decades comparable)
# and a multi-start over Gs across four decades, keeping the best residual. The optimiser is
# scipy's trust-region LM rather than the firmware's hand-rolled one; for an A/B between two
# parameter sets that is the fairer comparison, since both sides get the same solver.
G_STARTS = (1e-5, 1e-4, 1e-3, 1e-2)

# Bounds exist because an unbounded log-space search walks a parameter to zero and the model
# divides by it. Wide enough to never bind on a real solution.
P_LO = np.array([1e-15, 1e-9, 1e-18, 1e-15])
P_HI = np.array([1e-6, 1e1, 1e-6, 1e-6])


def _unpack(lx, p0, free_idx):
    p = p0.copy()
    p[free_idx] = np.clip(np.exp(np.clip(lx, -60.0, 10.0)), P_LO[free_idx], P_HI[free_idx])
    return p


def fit(ns, ms, hmeas, cp, free_mask, p_fixed):
    """Returns (params, rms). free_mask selects which of [Cm, Gs, Cs, Cd] are fitted."""
    free_idx = [i for i, f in enumerate(free_mask) if f]
    best, best_rms = None, np.inf
    for g0 in G_STARTS:
        p0 = np.array([500e-12, g0, 20e-12, 5e-12])
        for i, v in enumerate(p_fixed):
            if v is not None:
                p0[i] = max(v, P_LO[i])

        def resid(lx, p0=p0):
            p = _unpack(lx, p0, free_idx)
            r = []
            for n, m, hm in zip(ns, ms, hmeas):
                d = model_h(n, m, p, cp) - hm
                r += [d.real, d.imag]
            return np.array(r)

        try:
            x0 = np.log(np.clip(p0[free_idx], P_LO[free_idx], P_HI[free_idx]))
            sol = least_squares(resid, x0, method='lm', max_nfev=8000)
        except Exception:
            continue
        rms = float(np.sqrt(np.mean(sol.fun ** 2)))
        if rms < best_rms:
            best, best_rms = _unpack(sol.x, p0, free_idx), rms
    return best, best_rms


def gen_lumped(ns, ms, p_true, cp):
    return [model_h(n, m, p_true, cp) for n, m in zip(ns, ms)]


def identifiable(p):
    """The three combinations a sweep can actually observe.

    Ye(s) = s*[Gs(Cd+Cm) + s(CdCs+CdCm+CmCs)] / [Gs + s(Cs+Cm)], s = jw. Dividing numerator
    and denominator by Gs leaves three coefficients, so a 4-parameter fit is one degree of
    freedom short of determined and a 3-parameter one (Cs=0) is exactly determined.
    """
    cm, gs, cs, cd = p
    return np.array([cd + cm, (cd * cs + cd * cm + cm * cs) / gs, (cs + cm) / gs])


def stage_identifiability():
    """The evidence for the 2026-09-23 change. Prints PASS/FAIL on each claim."""
    ns = SPEC_N
    ms = [spec_phases(n) for n in ns]
    cp = CP_DEFAULT
    p_true = np.array([671e-12, 1 / 355.0, 14.9e-12, 5e-12])
    h = gen_lumped(ns, ms, p_true, cp)

    print("=== claim 1: the 4-parameter model is degenerate ===")
    p4, r4 = fit(ns, ms, h, cp, [True] * 4, [None] * 4)
    dmag = max(abs(abs(model_h(n, m, p4, cp)) / abs(model_h(n, m, p_true, cp)) - 1)
               for n, m in zip(ns, ms))
    inv_t, inv_f = identifiable(p_true), identifiable(p4)
    dinv = max(abs(inv_f / inv_t - 1))
    print(f"  truth  Rsoil {1/p_true[1]:7.1f}  Cm {p_true[0]*1e12:6.1f} pF  "
          f"Cs {p_true[2]*1e12:7.4f} pF  Cd {p_true[3]*1e12:6.2f} pF")
    print(f"  fitted Rsoil {1/p4[1]:7.1f}  Cm {p4[0]*1e12:6.1f} pF  "
          f"Cs {p4[2]*1e12:7.4f} pF  Cd {p4[3]*1e12:6.2f} pF   (fit rms {r4:.1e})")
    print(f"  worst per-frequency |H| mismatch : {dmag*1e6:.2f} ppm")
    print(f"  worst identifiable-combo mismatch: {dinv*100:.6f} %")
    ok1 = dmag < 1e-6 and abs(1 / p4[1] - 355.0) / 355.0 > 0.01
    print(f"  -> {'PASS' if ok1 else 'FAIL'}: different parameters, indistinguishable data, "
          f"Rsoil off by {(1/p4[1])/355*100-100:+.1f}%\n")

    print("=== claim 2: dropping Cs makes Rsoil seed-stable ===")
    seeds = [(500, 1e-3, 20, 5), (300, 1e-3, 5, 2), (900, 1e-3, 40, 30), (700, 1e-2, 10, 8)]
    r4s, r3s = [], []
    for cm0, g0, cs0, cd0 in seeds:
        a, _ = fit(ns, ms, h, cp, [True] * 4, [cm0 * 1e-12, g0, cs0 * 1e-12, cd0 * 1e-12])
        b, _ = fit(ns, ms, h, cp, [True, True, False, True], [cm0 * 1e-12, g0, 0.0, cd0 * 1e-12])
        r4s.append(1 / a[1])
        r3s.append(1 / b[1])
    sp4 = (max(r4s) - min(r4s)) / np.mean(r4s) * 100
    sp3 = (max(r3s) - min(r3s)) / np.mean(r3s) * 100
    print(f"  4-param Rsoil across seeds: {' '.join(f'{v:7.1f}' for v in r4s)}  spread {sp4:.3f}%")
    print(f"  3-param Rsoil across seeds: {' '.join(f'{v:7.1f}' for v in r3s)}  spread {sp3:.3f}%")
    ok2 = sp3 < 0.01 and sp3 < sp4
    print(f"  -> {'PASS' if ok2 else 'FAIL'}: shipped 3-param fit is reproducible\n")
    return ok1 and ok2


def report(tag, p, rms, base=None):
    cm, gs, cs, cd = p
    line = (f"  {tag:22s} Rsoil {1/gs:8.1f} ohm  Cm {cm*1e12:7.1f} pF  "
            f"Cs {cs*1e12:6.2f} pF  Cd {cd*1e12:5.2f} pF  rms {rms:.2e}")
    if base is not None:
        line += f"   dRsoil {(1/gs)/(1/base[1])*100-100:+6.2f}%"
    print(line)


# --- stage 1: lumped truth ----------------------------------------------------------------
def stage_lumped():
    ns = SPEC_N
    ms = [spec_phases(n) for n in ns]
    cp = CP_DEFAULT
    # Truth taken from the 2026-09-19 field-solve validation, moist soil (er 15, 0.02 S/m).
    p_true = np.array([671e-12, 1.0 / 355.0, 14.9e-12, 5e-12])

    print("=== stage 1: truth generated from the lumped model itself ===")
    print(f"  truth                  Rsoil {355:8.1f} ohm  Cm {671:7.1f} pF  Cs {14.9:6.2f} pF")
    h = gen_lumped(ns, ms, p_true, cp)

    p4, r4 = fit(ns, ms, h, cp, [True, True, True, True], [None] * 4)
    report("4-param (current)", p4, r4)
    p3, r3 = fit(ns, ms, h, cp, [True, True, False, True], [None, None, 0.0, None])
    report("3-param (Cs=0)", p3, r3, base=p_true)
    print("  ^ no noise: the 4-param fit must recover truth. The 3-param row is the BIAS that")
    print("    forcing Cs=0 introduces when the soil really does have a permittivity term.\n")

    # Now with realistic model error. The field solve measured lumped-model error at
    # ~0.2-1 % of drive; emulate the low end as correlated (not white) error so it cannot
    # simply average out, which is the pessimistic and realistic case.
    print("=== stage 1b: same, plus 0.2% of drive correlated model error ===")
    rng = np.random.default_rng(7)
    for trial_err in (0.002, 0.005, 0.01):
        d4, d3 = [], []
        for _ in range(12):
            drift = rng.normal(0, trial_err) + 1j * rng.normal(0, trial_err)
            tilt = rng.normal(0, trial_err)
            hn = [hv + drift + tilt * (i / len(h)) for i, hv in enumerate(h)]
            a, _ = fit(ns, ms, hn, cp, [True, True, True, True], [None] * 4)
            b, _ = fit(ns, ms, hn, cp, [True, True, False, True], [None, None, 0.0, None])
            if a is not None:
                d4.append((1 / a[1]) / 355.0 * 100 - 100)
            if b is not None:
                d3.append((1 / b[1]) / 355.0 * 100 - 100)
        print(f"  err {trial_err*100:4.1f}% of drive | Rsoil error, 4-param: "
              f"{np.mean(d4):+6.2f}% +/- {np.std(d4):4.2f}   "
              f"3-param: {np.mean(d3):+6.2f}% +/- {np.std(d3):4.2f}")
    print("  ^ if the 3-param spread is no worse, Cs is not protecting Rsoil and can go.\n")


# --- stage 2: field-solve truth ------------------------------------------------------------
def stage_field():
    from msv4_electrode_fieldsolve import solve

    MSV4 = dict(conductors=[(-7.087, -2.3625, 1.0), (2.3625, 7.087, 0.0),
                            (-8.745, -8.123, 0.0), (8.123, 8.745, 0.0)],
                half_width=9.0, faces='both')
    ns = SPEC_N
    ms = [spec_phases(n) for n in ns]
    cp = CP_DEFAULT

    cache = {}

    def h_of_w(w, soil, gap):
        key = (round(w, 3), soil, gap)
        if key not in cache:
            y = solve(t=1.6, tm=0.02, f=w / (2 * np.pi), soil=soil, air_gap=gap, **MSV4)[0]
            ye = y * L_PLATE_M
            cache[key] = 1.0 / (1.0 + SPEC_RS * (ye + 1j * w * cp))
        return cache[key]

    conditions = [
        ("baseline  (er 15, 0.020)", (15.0, 0.020), 0.0),
        ("er +10%   (er 16.5)     ", (16.5, 0.020), 0.0),
        ("sigma +10% (0.022)      ", (15.0, 0.022), 0.0),
        ("5 um air gap            ", (15.0, 0.020), 0.005),
    ]
    print("=== stage 2: truth generated from the 2D field solve (distributed physics) ===")
    base4 = base3 = None
    for label, soil, gap in conditions:
        h = [aliased_from_callable(n, m, lambda w, s=soil, g=gap: h_of_w(w, s, g))
             for n, m in zip(ns, ms)]
        p4, r4 = fit(ns, ms, h, cp, [True, True, True, True], [None] * 4)
        p3, r3 = fit(ns, ms, h, cp, [True, True, False, True], [None, None, 0.0, None])
        print(f"\n{label}")
        report("4-param (current)", p4, r4, base=base4)
        report("3-param (Cs=0)", p3, r3, base=base3)
        if base4 is None:
            base4, base3 = p4, p3
    print("\n  Read the dRsoil column: Rsoil MUST move with sigma and MUST NOT move with er.")
    print("  Cm must collapse on the gap row. Whichever fit does that is the one to ship.")


if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else 'ident'
    if which in ('ident', 'lumped', 'both'):
        ok = stage_identifiability()
    if which in ('lumped', 'both'):
        stage_lumped()
    if which in ('field', 'both'):
        stage_field()
    if which in ('ident', 'lumped', 'both') and not ok:
        sys.exit(1)
