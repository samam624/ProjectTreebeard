"""MSV4 as-drawn electrode vs the v1.2 three-prong: capacitance, depth, insertion.

Runs the 2D field solve in `msv4_electrode_fieldsolve.py` over both geometries under identical
soil so the two are directly comparable, and writes the numbers to
`MSV4/generated/electrode_compare_<date>.json`.

Prompted by the 2026-09-23 question "the MSV4 traces look smaller than the v1.2 -- is the
capacitance signal getting buried when going for depth". Answer, from the runs below: no. Each
MSV4 plate is half the width of the v1.2 hot electrode (4.725 vs 9.5 mm), but there are four of
them (A and B, mirrored front and back) against the v1.2's one, on one face. Per unit length the
mirrored MSV4 is 1.15-1.25x the v1.2 in lossless soil and ~1.45x in real conductive soil.

Two results here correct claims made elsewhere in the repo:

1. `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md` gives penetration 2.90 mm for MSV4 and
   2.71 mm for the v1.2, from `lambda/2pi`. Solved honestly, the ranking reverses: MSV4 as drawn
   reaches 50% of its response within 1.0 mm, the v1.2 within ~1.4 mm. `MSV4_ELECTRODE_FIELD_SOLVE`
   already noted that `lambda/2pi` overstates MSV4 because the plate-to-guard field is shallow;
   applying the same solve to the v1.2 puts both on one footing. MSV4 is still ~5x deeper than
   MSV3, which was the design goal. The guard ring is the accepted cost.
2. The MSV4 single-sided pattern is 0.75x the v1.2 per unit length, not the 0.49x quoted in the
   architecture doc. Mirroring is still what carries the design; it over-recovers rather than
   breaking even.

Geometry of the v1.2 is G3.5 / 2 / H9.5 / 2 / G3.5 over 20.5 mm, SINGLE SIDED (confirmed by the
maintainer 2026-09-23). Its sensing length is NOT recorded anywhere in this repo; V12_LENGTH_MM
below is an assumption and is the one number here that wants a ruler. It scales the v1.2 absolute
column only -- every per-metre and per-cent figure is independent of it, and
`ratio_equal_length` in the conductive study removes the assumption entirely.

Run: python MSV4/scripts/msv4_electrode_compare.py
"""
import json
import os
import sys
from datetime import date

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from msv4_electrode_fieldsolve import solve  # noqa: E402

F = 1e6                  # 1 MHz, the commissioned operating point
W = 2 * np.pi * F
RS = 1000.0              # R15, the divider series resistor
CP = 12e-12              # SENSE-node parasitic, firmware default until `spectrum open` measures it
VDD = 3300.0             # mV

L_MSV4_MM = 81.1         # plate length, IPC-2581 export 2026-09-18 23:03
V12_LENGTH_MM = 55.0     # ASSUMED -- see module docstring

# MS_A driven, MS_B + guard grounded, copper on both faces.
MSV4 = dict(conductors=[(-7.087, -2.3625, 1.0), (2.3625, 7.087, 0.0),
                        (-8.745, -8.123, 0.0), (8.123, 8.745, 0.0)],
            half_width=9.0, faces='both')
MSV4_1S = dict(MSV4, faces='single')
V12 = dict(conductors=[(-4.75, 4.75, 1.0), (-10.25, -6.75, 0.0), (6.75, 10.25, 0.0)],
           half_width=10.25, faces='single')

# (label, relative permittivity, conductivity S/m) -- potting mix across a wetting cycle.
STATES = [("bone dry", 4, 0.002), ("dry-ish", 8, 0.01), ("moist", 15, 0.02),
          ("wet", 25, 0.05), ("saturated", 30, 0.10)]

_cache = {}


def yprime(geom, soil, soil_region=None, key=None):
    """Admittance per metre of plate length, memoised. Returns complex S/m."""
    if key is not None and key in _cache:
        return _cache[key]
    y = solve(t=1.6, tm=0.02, f=F, soil=soil, soil_region=soil_region, **geom)[0]
    if key is not None:
        _cache[key] = y
    return y


def pf(y, length_m):
    return (y * length_m).imag / W * 1e12


def divider_mv(y_total):
    """Divider node magnitude in mV. H = 1 / (1 + Rs*(Ye + jw*Cp))."""
    return abs(1.0 / (1.0 + RS * (y_total + 1j * W * CP))) * VDD


def study_lossless():
    """Per-metre capacitance in lossless soil -- pure geometry, no conductivity."""
    rows = []
    for er in (1, 4, 10, 15, 20, 30):
        a = pf(yprime(MSV4, (er, 0.0), key=('m4', er)), 1.0)
        s = pf(yprime(MSV4_1S, (er, 0.0), key=('m4s', er)), 1.0)
        v = pf(yprime(V12, (er, 0.0), key=('v12', er)), 1.0)
        rows.append(dict(er=er, msv4_both_pF_per_m=round(a, 1), msv4_single_pF_per_m=round(s, 1),
                         v12_pF_per_m=round(v, 1), ratio_both_over_v12=round(a / v, 3),
                         ratio_single_over_v12=round(s / v, 3)))
    return rows


def study_conductive():
    """1 MHz apparent capacitance in real conductive soil, at each part's own length."""
    l4, l12 = L_MSV4_MM * 1e-3, V12_LENGTH_MM * 1e-3
    rows = []
    for name, er, sig in STATES:
        y4 = yprime(MSV4, (er, sig), key=('m4c', er, sig))
        y12 = yprime(V12, (er, sig), key=('v12c', er, sig))
        c4, c12 = pf(y4, l4), pf(y12, l12)
        rows.append(dict(state=name, er=er, sigma_S_per_m=sig,
                         msv4_pF=round(c4, 1), msv4_absZ_ohm=round(1 / abs(y4 * l4)),
                         v12_pF=round(c12, 1), v12_absZ_ohm=round(1 / abs(y12 * l12)),
                         # equal-length ratio: removes the v1.2 length assumption entirely
                         ratio_equal_length=round(pf(y4, l12) / c12, 3),
                         msv4_divider_mV=round(divider_mv(y4 * l4))))
    return rows


def study_penetration():
    """Share of the er-20 response from soil within d mm of the board surface.

    Higher share at small d means SHALLOWER. Reproduces the MSV4 column published in
    MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md (19/32/50/70/82/90%) exactly, which is what
    makes the v1.2 column trustworthy as a comparison.
    """
    rows = []
    refs = {}
    for tag, geom in (('msv4', MSV4), ('v12', V12)):
        refs[tag] = (pf(yprime(geom, (1, 0.0), key=(tag, 1)), 1.0),
                     pf(yprime(geom, (20, 0.0), key=(tag, 20)), 1.0))
    for d in (0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0):
        row = dict(depth_mm=d)
        for tag, geom in (('msv4', MSV4), ('v12', V12)):
            hw = geom['half_width']
            # soil only within d of the outer surface; air beyond
            region = (lambda X, Z, d=d, hw=hw:
                      (np.abs(Z) <= 0.8 + 0.02 + d) & (np.abs(X) <= hw + d))
            c = pf(yprime(geom, (20, 0.0), soil_region=region), 1.0)
            c_air, c_full = refs[tag]
            row[tag + '_share_pct'] = round((c - c_air) / (c_full - c_air) * 100, 1)
        rows.append(row)
    return rows


def study_insertion():
    """Partial insertion. The blade is 81.1 mm but typically goes 50-60 mm into the pot.

    Sections in parallel: inserted length sees soil, the remainder sees air. Valid because at
    1 MHz the plate is lambda/218 -- electrically a point -- so the 2D per-metre solve just adds.
    Error is the ~1-2 mm fringe at the soil/air boundary.

    The air section is a small OFFSET (1.95 pF at 55 mm, against Cp = 12 pF). Insertion depth
    matters as a GAIN term instead: 55 mm is 68% of the sensed length.
    """
    l_full = L_MSV4_MM * 1e-3
    y_air = yprime(MSV4, (1, 0.0), key=('m4', 1))

    def total(state, l_ins):
        _, er, sig = state
        return yprime(MSV4, (er, sig), key=('m4c', er, sig)) * l_ins + y_air * (l_full - l_ins)

    sweep = []
    for l_mm in (40, 50, 55, 60, 70, L_MSV4_MM):
        row = dict(inserted_mm=l_mm, pct_of_sensed_length=round(l_mm / L_MSV4_MM * 100, 1),
                   air_section_pF=round(pf(y_air * (l_full - l_mm * 1e-3), 1.0), 2))
        for st in STATES:
            y = total(st, l_mm * 1e-3)
            row[st[0] + '_pF'] = round(pf(y, 1.0), 1)
            row[st[0] + '_mV'] = round(divider_mv(y))
        sweep.append(row)

    # Error budget at the stated typical 55 mm.
    l0 = 55e-3
    budget = []
    for st in STATES:
        name, er, sig = st
        base = divider_mv(total(st, l0))
        d_1mm = divider_mv(total(st, l0 + 1e-3)) - base
        d_10mm = divider_mv(total(st, l0 + 10e-3)) - base
        y_wetter = yprime(MSV4, (er, sig * 1.1), key=('m4w', er, sig))
        d_wet = divider_mv(y_wetter * l0 + y_air * (l_full - l0)) - base
        pct_per_mm = abs(d_1mm) / base * 100
        budget.append(dict(
            state=name, baseline_mV=round(base), pct_per_mm_insertion=round(pct_per_mm, 2),
            mV_per_10mm_deeper=round(d_10mm, 1), mV_per_10pct_wetter=round(d_wet, 1),
            insertion_error_vs_signal=round(abs(d_10mm / d_wet), 1) if d_wet else None,
            # 1.956% is probe 8's daily wobble -- a magnitude reference, not an attribution
            mm_to_fake_1p956pct=round(1.956 / pct_per_mm, 2)))
    return dict(sweep=sweep, error_budget_at_55mm=budget)


def main():
    out = dict(
        generated=str(date.today()),
        source='MSV4/scripts/msv4_electrode_compare.py over msv4_electrode_fieldsolve.py',
        conditions=dict(frequency_Hz=F, board_thickness_mm=1.6, soldermask_um=20,
                        Rs_ohm=RS, Cp_F=CP, vdd_mV=VDD,
                        msv4_plate_length_mm=L_MSV4_MM,
                        v12_length_mm=V12_LENGTH_MM, v12_length_is_assumed=True,
                        v12_single_sided=True),
        lossless_per_metre=study_lossless(),
        conductive_1MHz=study_conductive(),
        penetration_er20=study_penetration(),
        insertion=study_insertion(),
    )
    here = os.path.dirname(os.path.abspath(__file__))
    dest = os.path.normpath(
        os.path.join(here, '..', 'generated', 'electrode_compare_' + out['generated'] + '.json'))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, 'w') as fh:
        json.dump(out, fh, indent=2)
    print(json.dumps(out, indent=2))
    print('\nwrote ' + dest, file=sys.stderr)


if __name__ == '__main__':
    main()
