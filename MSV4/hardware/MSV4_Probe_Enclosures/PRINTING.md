# Printing and assembly - MSV4 enclosures (v5.3)

## What to print

`output/` contains exactly one file per enclosure. Drag the one you want into
Bambu Studio or OrcaSlicer; every part arrives as its own named object, already
oriented and spaced on the plate.

| File | Objects |
| --- | --- |
| `output/MSV4_arm_solar_PRINT.3mf` | Base, Lid, Solar Arm Left, Solar Arm Right, Solar Panel Frame |
| `output/MSV4_integrated_solar_PRINT.3mf` | Base, Lid |
| `output/MSV4_minimum_profile_solar_PRINT.3mf` | Base, Lid |

Bambu Studio reports that the file is not from Bambu Lab and loads the geometry
only; accept that. The files carry no printer, filament, or process settings,
so your current profile applies.

Everything else is in `output/reference/`: STEP bodies and assemblies for CAD,
the fit-check assemblies with the PCB and battery reference, the two JSON
reports, and `MSV4_case_gasket_TPU_optional.3mf`. The gasket is TPU, so it is
kept out of the rigid-material files; skip it when using silicone cord or RTV.

## Orientation

The supplied orientations are the intended ones:

- bases back-down; the arm base's pivot bosses are flat on the bed
- arm lid roof-down (it prints as an open tray, no supports in the cavity)
- arms flat
- panel frame back-down; its pivot lugs are flat on the bed
- integrated and minimum-profile lids rear-edge-down; add a 6-8 mm brim and do
  not let auto-orient put them on the solar recess face

Auto-orient is safe on the arm file. Do not add supports inside the close-fit
blade throat. No part needs supports: the perimeter chamfers and the
screw-head pockets on the bed faces are 45-degree overhangs.

Recommended starting profile: 0.20 mm layers, 5 walls, 6 top and bottom layers,
25-35% infill, seam away from the probe throat and gasket land. ASA for outdoor
use, PETG for a first fit print; PLA is not recommended in sun.

## Before printing a batch

1. Measure the physical solar panel. The pockets are 36.4 x 30.4 mm and assume
   a 1.6 mm panel thickness.
2. Print one set first. Confirm the PCB, battery, panel, screw-head pockets,
   and your particular heat-set inserts.

## Hardware

Integrated and arm enclosures:

- 4 x M2 heat-set inserts, approximately 3 mm OD x 3 mm long
- 4 x M2 x 8 mm, 90-degree countersunk flat head (same for both versions)
- silicone cord, neutral-cure RTV, or the optional printed TPU gasket

From v5.3 the case screws go in from the back, the face with the LED teardrop,
and the inserts are in the lid. The v5.2 screws (12 mm and 10 mm) are too long:
the blind lid holes end 4.2 mm above the seam, which an 8 mm screw just
reaches. Each head sits in a recessed countersink and finishes flush with or
slightly below the back face. The pockets are cut for countersunk heads; pan
or socket-cap heads will not sit flush in them.

Arm version only:

- 4 x M2 x 5-6 mm pivot screws
- 4 x matching M2 heat-set inserts

Install case inserts from the seam side of the lid into the blind
2.90 x 3.20 mm pockets. Use a temperature-controlled insert tip and stop when
each insert is flush.
The minimum-profile enclosure requires no hardware; the base and lid are
permanently bonded after the electronics pass their functional test.

## Battery space

The roof is 7.0 mm above the PCB top surface. That leaves 3.9 mm above the
tallest top-side component. The 3 mm cell at the reference position over the
low components (X -12.2 to -1.8, Y 102.8 to 123.2) has 2.1 mm spare above it;
resting on the tallest component instead it would have about 0.9 mm. The spare
is deliberate for the first fit print: measure the gap and lower
`lidInnerRoofZ` (and the three lid-top heights with it) afterwards.
Secure the cell with thin VHB or a polyimide strap so it cannot chafe
components.

## Solar wiring

All three panel mounts have two 1.6 mm lead passages centered behind the panel,
4.0 mm apart (2 mm left and right of center), matching the 2.5 V panel
terminals.

On the arm version both leads then enter the case through one 3.0 mm hole in
the side wall of the lid, at the pivot, on the same side as the arm with the
wire channel. Run the leads down that arm's channel, leave a drip loop between
frame and case, and seal the hole with neutral-cure RTV after routing.

## Sealing and final assembly

1. Deburr the PCB rails, throat, screw-head pockets, and gasket groove without
   rounding the sealing lands.
2. Dry-fit the board and battery. The board should be snug but must not bow.
3. Route the panel wires and seal each passage with neutral-cure RTV or
   electronics-compatible epoxy.
4. Bond the solar panel around its complete perimeter. On the integrated
   version, the panel is part of the weather barrier.
5. Apply a thin continuous RTV bead, cord, or the TPU gasket in the groove,
   which is in the seam face of the base.
6. Seal around the exposed blade at the split throat, keeping sealant off the
   sensing region.
7. Turn the case over and fit the four screws from the back. Tighten them
   gradually in a diagonal pattern, only enough to compress the seal.
8. Leak-test the empty housing with tissue inside and a gentle spray.

For all three bases, fill the teardrop LED window from the outside with
optically clear epoxy and allow it to cure before leak testing. Keep epoxy off
the LED package until the board is correctly seated.

## Minimum-profile version

Dry-fit the PCB into the shaped groove in the base. The groove has 0.25 mm XY
and 0.15 mm Z clearance, so compensate globally in the slicer only if the test
print shows that your machine closes this fit. After installing and testing the
PCB, permanently bond the complete base-to-lid seam with electronics-compatible
epoxy or outdoor polyurethane adhesive. The solar panel stops before the E73
antenna keepout; do not enlarge or move the pocket toward the far board end.
