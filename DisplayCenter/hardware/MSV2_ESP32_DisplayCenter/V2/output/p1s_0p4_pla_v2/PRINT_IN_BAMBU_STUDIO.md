# MSV2 DisplayCenter enclosure v2 - P1S / 0.4 mm / PLA

Prototype 9 (2026-11-17), re-sliced on 2026-11-19. Every file here is a
sliced `.gcode.3mf` for a Bambu Lab P1S with a 0.4 mm nozzle, a Textured PEI
Plate and PLA. Send one to the printer and print it. Nothing needs re-slicing.

| File | What is on the plate | Time | PLA | Supports |
| --- | --- | --- | --- | --- |
| `..._full_enclosure_v2.gcode.3mf` | **whole enclosure on one plate**: front cover, rear shell, kickstand | 3 h 08 min | 85.1 g | rear shell only |
| `..._fit_check_plate_v2.gcode.3mf` | fit-check plate | 27 min | 8.3 g | none |
| `..._front_and_stand_v2.gcode.3mf` | front cover + kickstand | 1 h 01 min | 20.2 g | none |
| `..._rear_shell_v2.gcode.3mf` | rear shell | 2 h 26 min | 66.1 g | tree, build plate only |

The one-plate build matches the MK4S pack's `full_enclosure`. Tree supports
are on for the rear shell and switched off per object for the cover and the
kickstand. The parts are separate objects, so one can be skipped from the
printer without stopping the others. The two split plates are the same parts,
for when the plate is busy or one part has to be reprinted.

Settings: Bambu `0.20mm Standard @BBL X1C` (0.20 mm layers), with 3 walls,
5 top and 5 bottom layers, 20% gyroid infill, and a 5 mm outer brim. Generic
PLA at 220 C, with the bed at 55 C.

## Sending a file to the printer

- **Bambu Studio**: File > Open, pick the `.gcode.3mf`. It opens already
  sliced, as a preview. Click **Print plate**, choose the P1S, and leave bed
  levelling on. Map filament 1 to the AMS slot or external spool holding the
  PLA, then Send.
- **microSD card**: copy the `.gcode.3mf` to the card, put it in the P1S, then
  pick the file under Files on the touchscreen.

The filament colour in the file is Bambu's default green. It is only a label;
the print uses whatever PLA is loaded.

## Fit check

The controller board's position comes from photos and calipers, good to about
+/-0.4 mm. The display holes give an M2 screw only +/-0.6 mm of slack. If this
board stack has not already been fitted to a prototype-9 print (the MK4S
one-plate build of 2026-11-17 counts), print the fit-check plate first:

1. Lay the plate, tubes up, on the screen of the soldered board stack.
2. Drop an M2 screw or a 2 mm drill bit down each of the four tubes.
3. All four must drop into their PCB holes at the same time:
   - display PCB, right-hand pair (seen from the screen side)
   - controller PCB, top pair
4. With a full-white screen, the window should show only lit pixels, with
   nothing important cut off.

If a tube misses its hole, note which tube and which way the hole is, so the
offset can be corrected before the enclosure is printed.

## Hardware

| Where | Part | Qty |
| --- | --- | --- |
| Front cover insert pockets (2.9 mm x 3.2 mm deep) | M2 heat-set insert | 4 |
| Display PCB right-hand holes | M2x20 + one thin M2 washer under the head (or M2x18, about 1.2 mm engagement) | 2 |
| Controller PCB top holes | M2x16 | 2 |
| Kickstand pivots | M2x10 screw + M2 hex nut | 2 + 2 |

The inserts go in the **front cover**, not the rear-shell posts. The rear posts
have plain 2.4 mm clearance holes.

## Assembly

1. Clear the four 2.4 mm case-screw passages and both pivot holes with a
   2.4 mm drill if stringing or first-layer squish narrowed them.
2. Install the four inserts in the front cover. Do not bottom the iron against
   the visible face.
3. Kickstand: press an M2 nut into the hex trap on each arm's inner face (the
   faces that point at each other). Fit the stand between the shell lugs, then
   drive an M2x10 from the outside of each lug, through the lug and arm, into
   the nut. Tighten until the stand holds its angle.
4. Put the board stack into the front cover, screen down. Close with the
   rear shell, then fit the four case screws from the back.
5. The controller board floats about 0.6 mm in depth between its boss and its
   post. That is intentional, because its depth was not measured. If it rattles,
   add a thin washer or foam pad.
6. Mount the antenna inside, away from the LCD's metal frame.

## Why these files replaced the 2026-11-17 ones

The first v2 files (`*_v2.3mf`, removed on 2026-11-19; they are in git
history) must not be printed. They were sliced with the Bambu Studio 02.09
command line pointed at the stock P1S, process and filament presets. That
command line does not resolve those presets' `inherits` chains or the P1S
preset's `include` list (which is where its G-code lives), so the slices ran on
generic defaults:

- start G-code was `G28` / `G1 Z5`: no bed levelling, nozzle wipe, purge line
  or textured-plate Z offset, and Z homed while the nozzle was heating and
  oozing; the end G-code was generic too
- 122 settings differed, for example 500 mm/s^2 acceleration (10000 is
  standard), 60 mm/s inner walls (300), and a 20% minimum PLA fan (100%), which
  is why the old estimates were twice as long

`tools/slice_p1s_pack.py` flattens each preset itself (`inherits`, `include`,
and the overlay in `profiles/`). It slices with those flattened presets, then
checks every setting in the G-code against them: 415 per plate, with the five
custom G-code blocks compared character by character. It refuses the pack on
any difference. The flattened presets are kept in `profiles/p1s_flattened/`.
It also fills two `slice_info` fields that the command line leaves blank or
uninitialised, `printer_model_id` (`C12`) and `first_layer_time`, as the GUI
would. The G-code itself is not touched; its MD5 is checked after
repackaging. Details are in
`sliced_release_report.json`.

The four STL files are included for re-slicing.
