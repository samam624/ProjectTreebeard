#!/usr/bin/env python3
"""Clean small-signal schematic of the moisture sensor, for slides: symbols and designators only.

The detailed, annotated version (values, I/Q-plane directions, tempco curves) is
msv4_small_signal_iq_figure.py. This one is the mental model: drive -> Rs -> sense node, the
electrode-in-soil network (direct field Cd; mask A -> soil R || C -> mask B), and a textbook I/Q
demodulator. The real front end does the I/Q with one chopper swept in phase, not two mixers.

    pip install schemdraw
    python MSV4/scripts/msv4_sensor_schematic.py
"""

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import schemdraw  # noqa: E402
import schemdraw.dsp as dsp  # noqa: E402
import schemdraw.elements as elm  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "docs" / "MSV4_SENSOR_SMALL_SIGNAL_SCHEMATIC.png"
RAIL, TAP, Y_I, Y_Q, X_SPLIT = 6.0, 8.2, 8.2, 3.2, 13.5

plt.rcParams.update({"mathtext.fontset": "stix"})

with schemdraw.Drawing(show=False) as d:
    d.config(unit=3, fontsize=17, lw=1.6, bgcolor="white", font="STIXGeneral", margin=0.5)

    # Drive and source resistance.
    elm.Ground().at((0, 0))
    elm.Line().up().at((0, 0)).length(1.5)
    elm.SourceSin().up().label("$v_{drv}$", loc="bottom", ofst=0.25)
    elm.Line().up().toy(RAIL)
    elm.Resistor().right().label("$R_s$")
    sense = elm.Dot().label("$v_s$", loc="bottom", ofst=0.25)

    # Board parasitic.
    elm.Line().right().length(1.5)
    elm.Dot()
    elm.Capacitor().down().toy(0).label("$C_p$", loc="bottom")
    elm.Ground()

    # Electrode in soil: direct field, then mask A -> soil (R || C) -> mask B.
    elm.Line().right().at((4.5, RAIL)).length(2.5)
    elm.Dot()
    elm.Capacitor().down().toy(0).label("$C_d$", loc="bottom")
    elm.Ground()
    elm.Line().right().at((7, RAIL)).length(3)
    elm.Capacitor().down().length(1.6).label("$C_{m,A}$", loc="bottom")
    elm.Dot()
    elm.Line().left().length(0.9)
    elm.Resistor().down().length(2.3).label("$R_{soil}$", loc="top")
    elm.Line().right().length(1.8)
    elm.Capacitor().up().length(2.3).label("$C_s$", loc="top")
    elm.Line().left().length(0.9)
    elm.Dot().at((10, 2.1))
    elm.Capacitor().down().at((10, 2.1)).toy(0).label("$C_{m,B}$", loc="bottom")
    elm.Ground()
    # Rect corners are relative to the cursor and rotate with the drawing direction: pin both.
    elm.Rect(corner1=(5.9, -0.75), corner2=(11.9, 6.7), ls="--", lw=1.0).at((0, 0)).theta(0)
    elm.Label().at((5.95, 7.1)).label("electrode in soil", loc="center", halign="left", fontsize=15)

    # I/Q demodulator, fed from the sense node.
    elm.Line().up().at(sense.center).toy(TAP)
    elm.Line().right().tox(X_SPLIT)
    elm.Dot()
    elm.Line().right().length(0.8)
    mix_i = dsp.Mixer().anchor("W")
    elm.Line().down().at((X_SPLIT, TAP)).toy(Y_Q)
    elm.Line().right().length(0.8)
    mix_q = dsp.Mixer().anchor("W")
    osc = dsp.Oscillator().at((mix_i.S[0], (Y_I + Y_Q) / 2 + 0.35)).anchor("center") \
        .label("$\\omega$", loc="left", ofst=0.15)
    dsp.Arrow().up().at(osc.N).to(mix_i.S)
    dsp.Line().down().at(osc.S).length(0.35)
    shift = dsp.Box(w=1.2, h=0.75).anchor("N").label("90°")
    dsp.Arrow().down().at(shift.S).to(mix_q.N)
    for mix, name in ((mix_i, "$I$"), (mix_q, "$Q$")):
        dsp.Line().right().at(mix.E).length(0.6)
        dsp.Filter(response="lp").anchor("W")
        dsp.Arrow().right().length(1.2).label(name, loc="right", ofst=0.15)

    # schemdraw saves with a transparent background unless told otherwise, whatever bgcolor says;
    # black lines on nothing are invisible in a dark image viewer.
    d.save(str(OUT), dpi=300, transparent=False)

print(f"wrote {OUT}")
