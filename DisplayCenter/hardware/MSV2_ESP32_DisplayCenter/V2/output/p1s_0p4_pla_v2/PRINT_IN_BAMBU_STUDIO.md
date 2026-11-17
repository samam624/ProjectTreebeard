# MSV2 DisplayCenter enclosure v2 - P1S / 0.4 mm / PLA

Prototype 9 (2026-11-17). It fixes the six problems found on the v1 print:
screen edges showing, boards not lining up, the half-mounted stack, the
off-centre USB-C, the uneven top and bottom borders, and the blind prop holes.

All three projects are pre-sliced for a Bambu Lab P1S with a 0.4 mm nozzle,
Textured PEI Plate, 0.20 mm layers, three walls, 20% gyroid infill, and a 5 mm
outer brim. Open each 3MF in Bambu Studio, pick the loaded PLA spool, and print
the plate as prepared. Do not auto-orient.

## Step 1: fit-check plate (print this first)

`MSV2_DisplayCenter_P1S_0p4_PLA_fit_check_plate_v2.3mf`: about 39 min, 8.3 g.

The controller board's position was worked out from photos and calipers, not
measured directly, so it is good to about +/-0.4 mm. The display holes give
an M2 screw only +/-0.6 mm of slack. Check the position before the 7-hour
print:

1. Lay the plate, tubes up, on the screen of the soldered board stack.
2. Drop an M2 screw or a 2 mm drill bit down each of the four tubes.
3. All four must drop into their PCB holes at the same time:
   - display PCB, right-hand pair (seen from the screen side)
   - controller PCB, top pair
4. With a full-white screen, the window should show only lit pixels, with
   nothing important cut off.

If a tube misses its hole, note which tube and which way the hole is, and the
offset will be corrected before printing the enclosure.

## Step 2: front cover and kickstand

`MSV2_DisplayCenter_P1S_0p4_PLA_front_and_stand_v2.3mf`: about 1 h 47 min, 20.2 g.
No supports. Front cover prints face down, kickstand on its foot.

## Step 3: rear shell

`MSV2_DisplayCenter_P1S_0p4_PLA_rear_shell_v2.3mf`: about 5 h 16 min, 58.5 g.
Stands on its right side wall. Build-plate-only tree supports are enabled.

The enclosure alone takes about 7 h 03 min and 78.7 g PLA.

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

The four STL files are included for reslicing.
