# Printing and assembly - MSV4 low-profile tapered enclosures

## Before printing

1. Measure the physical solar panel. Both pockets are 36.4 x 30.4 mm and assume
   a 1.6 mm panel thickness.
2. Print one set before a batch. Confirm the PCB, panel, countersinks, and your
   particular heat-set inserts.
3. Use ASA for the outdoor part when possible. PETG is useful for the first fit
   print. PLA is not recommended for a sun-exposed final enclosure.

Recommended starting profile:

- 0.20 mm layer height
- 5 walls/perimeters
- 6 top and bottom layers
- 25-35% infill
- seam away from the probe throat and gasket land
- elephant-foot compensation if needed

All supplied STL meshes are watertight and have zero non-manifold edges.
The probe end has intentionally sloped upper and lower exterior faces. Do not
add supports inside the close-fit blade throat; remove only slicer-generated
support from the exterior if your profile adds it.

## What to open directly in Bambu Studio

Use the files ending in `_print.stl`; they are already exported in millimetres
and placed on the print bed. STEP files are for CAD inspection and fit checks,
not the normal slicer workflow.

For the planned first comparison, unzip these two packs and import the listed
STLs into Bambu Studio:

- Static/non-arm test: `MSV4_v5_integrated_solar_slicer_pack.zip`
    - `MSV4_integrated_solar_base_print.stl`
    - `MSV4_integrated_solar_lid_rear_edge_print.stl`
- Adjustable-arm test: `MSV4_v5_arm_solar_slicer_pack.zip`
    - `MSV4_arm_solar_base_print.stl`
    - `MSV4_arm_solar_lid_print.stl`
    - `MSV4_solar_arm_left_print.stl`
    - `MSV4_solar_arm_right_print.stl`
    - `MSV4_solar_panel_frame_print.stl`

The optional `MSV4_case_gasket_TPU_print.stl` is included in both packs. Put it
on a separate TPU plate, or omit it when using silicone cord or formed-in-place
RTV instead. Do not import assembly or fit-check STEP files as printable parts;
they contain references that are intentionally not included in the slicer
packs.

Keep the minimum-profile pack for the later bonded-case phase, after firmware,
sensing, battery behavior, and solar charging have stabilized. A screwless
bonded arm version may be evaluated later if the adjustable-panel test justifies
it; no such variant is included in this release.

## Integrated-panel version

Import:

- `MSV4_integrated_solar_base_print.stl`
- `MSV4_integrated_solar_lid_rear_edge_print.stl`
- optionally `MSV4_case_gasket_TPU_print.stl`

The base is floor-down. The lid is rear-edge-down; keep that orientation and
add a 6-8 mm brim. Do not auto-orient it onto the solar recess face.

## Arm-panel version

Import:

- `MSV4_arm_solar_base_print.stl`
- `MSV4_arm_solar_lid_print.stl`
- `MSV4_solar_arm_left_print.stl`
- `MSV4_solar_arm_right_print.stl`
- `MSV4_solar_panel_frame_print.stl`
- optionally `MSV4_case_gasket_TPU_print.stl`

The arms are laid flat. Print the panel frame with its rear wall on the bed.
The case-side pivot bosses belong to the base, following the robust MSV3 layout.

## Minimum-profile integrated-panel version

Import:

- `MSV4_minimum_profile_integrated_solar_base_print.stl`
- `MSV4_minimum_profile_integrated_solar_lid_rear_edge_print.stl`

This is a closed two-piece enclosure with a thin back/base and a solar lid. It
has no gasket, fasteners, inserts, or arms. Print the base floor-down. Print the
lid rear-edge-down with a 6-8 mm brim, matching the integrated lid orientation.
Do not auto-orient the lid onto the solar recess face.

Dry-fit the PCB into the shaped groove in the base. The groove has 0.25 mm XY
and 0.15 mm Z clearance, so compensate globally in the slicer only if the test
print shows that your machine closes this fit. After installing and testing the
PCB, permanently bond the complete base-to-lid seam with electronics-compatible
epoxy or outdoor polyurethane adhesive. The solar panel stops before the E73
antenna keepout; do not enlarge or move the pocket toward the far board end.

## Hardware

For the integrated and arm enclosures:

- 4 x M2 heat-set inserts, approximately 3 mm OD x 3 mm long
- silicone cord, neutral-cure RTV, or the optional printed TPU gasket

Case screw length:

- integrated-panel version: 4 x M2 x 10 mm, 90-degree countersunk flat head
- arm-panel version: 4 x M2 x 8 mm, 90-degree countersunk flat head

For the arm version, also use:

- 4 x M2 x 5-6 mm pivot screws
- 4 x matching M2 heat-set inserts

Install case inserts from the seam side into the blind 2.90 x 3.20 mm pockets.
Use a temperature-controlled insert tip and stop when each insert is flush.
The minimum-profile enclosure requires no hardware; the base and lid are
permanently bonded after the electronics pass their functional test.

## Sealing and final assembly

1. Deburr the PCB rails, throat, countersinks, and gasket groove without
   rounding the sealing lands.
2. Dry-fit the board and top-side battery. The board should be snug but must not
   bow. The roof is 4.0 mm above the PCB; secure the cell with thin VHB or a
   polyimide strap so it cannot chafe components.
3. Route the panel wires and seal each passage with neutral-cure RTV or
   electronics-compatible epoxy.
4. Bond the solar panel around its complete perimeter. On the integrated
   version, the panel is part of the weather barrier.
5. On the arm version, leave a drip loop in the wire between frame and case.
6. Apply a thin continuous RTV bead, cord, or the TPU gasket in the groove.
7. Seal around the exposed blade at the split throat, keeping sealant off the
   sensing region.
8. Tighten the case screws gradually in a diagonal pattern, only enough to
   compress the seal.
9. Leak-test the empty housing with tissue inside and a gentle spray.

For all three bases, fill the teardrop LED window from the outside with
optically clear epoxy and allow it to cure before leak testing. Keep epoxy off
the LED package until the board is correctly seated.

The case screws finish flush with the single-level top skin. There are no roof
scallops, raised screw towers, or protruding closure ears.
