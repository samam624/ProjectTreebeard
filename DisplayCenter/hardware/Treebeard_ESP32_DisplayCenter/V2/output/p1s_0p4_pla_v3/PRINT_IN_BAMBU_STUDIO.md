# MSV2 DisplayCenter enclosure v3 - P1S / 0.4 mm / PLA

Prototype 10 (2026-11-23), sliced the same day. Every file here is a sliced
`.gcode.3mf` for a Bambu Lab P1S with a 0.4 mm nozzle, a Textured PEI Plate
and PLA. Send one to the printer and print it. Nothing needs re-slicing.

Prototype 10 is prototype 9 (the v2 pack) with two changes:

- a **fifth screw** at the bottom corner on the controller/USB side. Seen from
  the screen that is bottom-left; seen from the back, bottom-right, next to the
  USB bay. It is a structural standoff only and touches no PCB.
- a **3.5 mm chamfer on every outside edge** of the body (2 mm on the feet).

| File | What is on the plate | Time | PLA | Supports |
| --- | --- | --- | --- | --- |
| `..._full_enclosure_v3.gcode.3mf` | **whole enclosure on one plate**: front cover, rear shell, kickstand | 3 h 11 min | 83.0 g | rear shell only |
| `..._fit_check_plate_v3.gcode.3mf` | fit-check plate (unchanged from v2) | 27 min | 8.3 g | none |
| `..._front_and_stand_v3.gcode.3mf` | front cover + kickstand | 1 h 03 min | 19.1 g | none |
| `..._rear_shell_v3.gcode.3mf` | rear shell | 2 h 26 min | 64.4 g | tree, build plate only |

The board stack and screw positions are unchanged from v2. If your board
stack already fitted a v2 print, skip the fit-check plate.

Tree supports are on for the rear shell and switched off per object for the
cover and the kickstand. The parts are separate objects, so one can be skipped
from the printer without stopping the others.

Settings: Bambu `0.20mm Standard @BBL X1C` (0.20 mm layers), with 3 walls,
5 top and 5 bottom layers, 20% gyroid infill, and a 5 mm outer brim. Generic
PLA at 220 C, with the bed at 55 C. Sliced by `tools/slice_p1s_pack.py --tag v3`
with the BBL profiles installed on 2026-11-23 (02.07.00.08; v2 used
02.09.00.74). The full Bambu start G-code is in every plate (bed levelling,
nozzle wipe, purge line, textured-plate Z offset), and all 397 compared
settings per plate match the flattened presets.

## Sending a file to the printer

- **Bambu Studio**: File > Open, pick the `.gcode.3mf`. It opens already
  sliced, as a preview. Click **Print plate**, choose the P1S, and leave bed
  levelling on. Map filament 1 to the AMS slot or external spool holding the
  PLA, then Send.
- **microSD card**: copy the `.gcode.3mf` to the card, put it in the P1S, then
  pick the file under Files on the touchscreen.

The filament colour in the file is Bambu's default green. It is only a label;
the print uses whatever PLA is loaded.

## Hardware

| Where | Part | Qty |
| --- | --- | --- |
| Front cover insert pockets (2.9 mm x 3.2 mm deep) | M2 heat-set insert | **5** |
| Display PCB right-hand holes | M2x20 + one thin M2 washer under the head (or M2x18, about 1.2 mm engagement) | 2 |
| Controller PCB top holes | M2x16 | 2 |
| **Corner standoff** (new) | M2x16 | 1 |
| Kickstand pivots | M2x10 screw + M2 hex nut | 2 + 2 |

The inserts go in the **front cover**, not the rear-shell posts. The rear posts
have plain 2.4 mm clearance holes. The corner insert sits at the open end of
its boss, flush with the seam; the boss has a 1.5 mm screw-tip relief above
the insert, so the M2x16 gets full thread engagement.

## Assembly

1. Clear the five 2.4 mm case-screw passages and both pivot holes with a
   2.4 mm drill if stringing or first-layer squish narrowed them.
2. Install the five inserts in the front cover. Do not bottom the iron against
   the visible face. Press the corner insert flush with the end of its boss.
   It must not stand proud, or the case will not close at that corner.
3. Kickstand: press an M2 nut into the hex trap on each arm's inner face (the
   faces that point at each other). Fit the stand between the shell lugs, then
   drive an M2x10 from the outside of each lug, through the lug and arm, into
   the nut. Tighten until the stand holds its angle.
4. Put the board stack into the front cover, screen down. Close with the
   rear shell, then fit the five case screws from the back.
5. The controller board floats about 0.6 mm in depth between its boss and its
   post. That is intentional, because its depth was not measured. If it rattles,
   add a thin washer or foam pad.
6. Mount the antenna inside, away from the LCD's metal frame and clear of the
   new corner post.

The four STL files are included for re-slicing.
