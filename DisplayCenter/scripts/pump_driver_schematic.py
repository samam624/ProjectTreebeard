#!/usr/bin/env python3
"""Clean schematic of a low-side switch driving a small brushed DC pump: symbols and designators only.

The pump is drawn as what a brushed DC motor is electrically: winding resistance, winding
inductance and a back-EMF source in series. Around it are the parts a microcontroller pin needs to
switch it: a logic-level N-channel MOSFET, a gate resistor, a pull-down that keeps the pump off
while the pin floats, and a flyback diode for the winding's stored current.

    pip install schemdraw
    python DisplayCenter/scripts/pump_driver_schematic.py
"""

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import schemdraw  # noqa: E402
import schemdraw.elements as elm  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "docs" / "PUMP_DRIVER_SCHEMATIC.png"
RAIL, X_IN, X_PD, X_MOTOR, X_DIODE = 10.0, 0.0, 2.0, 8.0, 11.0

plt.rcParams.update({"mathtext.fontset": "stix"})

with schemdraw.Drawing(show=False) as d:
    d.config(unit=3, fontsize=17, lw=1.6, bgcolor="white", font="STIXGeneral", margin=0.5)

    # The switch, source on ground.
    # reverse() mirrors the symbol left-to-right, putting the gate on the input side.
    q = elm.NFet().reverse().at((X_MOTOR, 0.9)).anchor("source")
    elm.Label().at((X_MOTOR + 0.45, 1.65)).label("$Q_1$", loc="center", halign="left")
    elm.Line().down().at(q.source).toy(0)
    elm.Ground()

    # Microcontroller pin, pull-down, gate resistor.
    elm.Dot(open=True).at((X_IN, q.gate[1])).label("GPIO", loc="left", ofst=0.2)
    elm.Line().right().at((X_IN, q.gate[1])).tox(X_PD)
    elm.Dot()
    elm.Resistor().down().toy(0).label("$R_{PD}$", loc="bottom")
    elm.Ground()
    elm.Resistor().right().at((X_PD, q.gate[1])).tox(q.gate[0]).label("$R_G$")

    # The pump motor: winding resistance, winding inductance, back-EMF.
    drain = (X_MOTOR, q.drain[1] + 0.6)
    elm.Line().up().at(q.drain).toy(drain[1])
    elm.Dot()
    elm.SourceV().up().length(1.9).label("$E$", loc="top", ofst=0.25)
    elm.Inductor2().up().length(1.9).label("$L_a$", loc="top", ofst=0.2)
    elm.Resistor().up().toy(RAIL).label("$R_a$", loc="top", ofst=0.2)
    # Rect corners are relative to the cursor and rotate with the drawing direction: pin both.
    elm.Rect(corner1=(X_MOTOR - 2.7, drain[1] + 0.25), corner2=(X_MOTOR + 1.0, RAIL - 0.3),
             ls="--", lw=1.0).at((0, 0)).theta(0)
    elm.Label().at((X_MOTOR - 2.65, RAIL + 0.1)).label("pump motor", loc="center",
                                                      halign="left", fontsize=15)

    # Flyback diode across the motor, and the supply.
    elm.Line().right().at(drain).tox(X_DIODE)
    elm.Diode().up().toy(RAIL).label("$D_1$", loc="bottom", ofst=0.25)
    elm.Line().left().tox(X_MOTOR)
    elm.Dot().at(((X_MOTOR + X_DIODE) / 2, RAIL))
    elm.Vdd().at(((X_MOTOR + X_DIODE) / 2, RAIL)).label("$V_S$")

    # save() defaults to a transparent background, which shows black-on-black in a dark viewer.
    d.save(str(OUT), dpi=300, transparent=False)

print(f"wrote {OUT}")
