# Automatic watering board: pump mounting plan, 2026-11-11

**Decided by the maintainer, 2026-11-11.** The watering board is a dedicated PCB. It receives a
periodic signal from the DisplayCenter and runs the pump. Nothing has been built yet. The pump
is the Kamoer NKP 12 V DC with the straight (two-hole) plate. Its Altium library is
`hardware/altium/Kamoer_NKP/`, footprint `KAMOER_NKP_VERTICAL_TABS`.
Background: [AUTO_WATERING_PLAN_2026-10-08.md](AUTO_WATERING_PLAN_2026-10-08.md).

## The plan: one screw path through plate, enclosure wall and standoff

The pump's plate mounts against the **outside** of an enclosure wall, as a normal panel mount.
The motor passes through a hole in that wall into the enclosure. The PCB sits inside, parallel
to the wall. The motor's two solder tabs go through slots in the PCB and are soldered.
(2026-11-18: the slots are now curved, routed and non-plated, so the pump can turn ±30° to line
up a head that is not square to its tabs. See `hardware/altium/Kamoer_NKP/README.md`.)

The two M3 screws go **through the plate and the enclosure wall, into hex standoffs inside**.
The standoffs hold the PCB.

```
   OUTSIDE  │  wall  │                 INSIDE
            │        │
 pump head  ├─plate──┤══ standoff ══════╗
 + tubes    │  (M3)  │                  ║  PCB
            │ motor ─┼──────────────────╫──[tab slots]
            │ (dia29)│                  ║
            ├─plate──┤══ standoff ══════╝
            │  (M3)  │
```

Why:

- **Strength.** The screws clamp the plate to the wall, so the enclosure carries the pump's
  weight, vibration and motor torque. The PCB hangs from the same two standoffs, and the
  tabs carry current only.
- **Water stays outside.** The pump head and tubing fittings sit outside the enclosure. A tube
  that slips off or leaks at the head does not drip onto the electronics.
- **Compact.** The board can sit within the pump's 55 x 41 mm plate outline. The MOSFET,
  flyback diode and connector fit between the motor can and the standoffs, with the diode
  right at the tabs.

## Numbers

The plate's two holes are dia 3.2 (M3 clearance), 48.5 ± 0.15 mm apart. Kamoer dimensions
these on its catalogue drawing. The other motor-end numbers were **scaled off the drawing** and
must be checked with calipers on the real pump (see `hardware/altium/Kamoer_NKP/README.md`).
The key one is the distance from the plate's motor-side face to the motor can's end face,
about 38.7 mm.

Aim for the can's end face to sit about 1.3 mm above the PCB. That puts the bearing boss
inside the PCB's dia 13 hole, with the tabs about 2.3 mm out of the board's bottom face. So:

| Item | Rule | Wall 2 mm | Wall 3 mm |
|---|---|---|---|
| Plate face to PCB top | 38.7 + 1.3 = **40.0 mm** | 40.0 | 40.0 |
| Standoff (M3 hex, F-F) | **40.0 − wall** | 38 mm | 37 mm |
| Outside screw (through plate + wall) | plate ~3.5 + wall + ≥4.5 thread | M3 x 10 | M3 x 12 |
| Inside screw (through 1.6 mm PCB) | | M3 x 6 | M3 x 6 |

Tolerance: being ±1 mm off on standoff length is fine. At +1 mm the tabs still stick about 1.3 mm
out of the bottom for soldering. At −1 mm the can still clears the board, and the boss stays
inside its hole. If a standoff length isn't stocked, round **up**, not down.

## Enclosure wall cut-outs

They match the plate and the PCB footprint, all on the motor's axis:

- dia 31 hole for the dia 29 motor can (1 mm radial clearance).
- 2 x dia 3.2 holes at 48.5 mm, on the line through the motor's axis.

The PCB's holes in `KAMOER_NKP_VERTICAL_TABS` are the same pair at the same spacing, so the
wall and the board line up hole for hole.

## Open

- The wall thickness, and so the exact standoff and screw lengths, depends on the enclosure.
  It is not chosen yet.
- Caliper checks on the pump (tab spacing and alignment with the holes, plate to can face) are
  owed before ordering the PCB.
- Orientation: which way the tubes point is set by rotating the pump 180° about its axis, and
  that also swaps which tab lands on pin 1 (+).
