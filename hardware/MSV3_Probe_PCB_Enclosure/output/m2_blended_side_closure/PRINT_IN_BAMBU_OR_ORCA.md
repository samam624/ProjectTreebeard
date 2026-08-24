# MSV3 M2 blended-side enclosure: slicer pack

No Fusion 360 or CAD conversion is required. Import these five STL files
directly into Bambu Studio or OrcaSlicer; dimensions are in millimeters:

1. `MSV3_brain_enclosure_base_M2_blended_side_inserts_print.stl`
2. `MSV3_brain_enclosure_lid_M2_blended_side_countersunk_print.stl`
3. `MSV3_solar_arm_left_M2_blended_side_print.stl`
4. `MSV3_solar_arm_right_M2_blended_side_print.stl`
5. `MSV3_solar_panel_frame_M2_blended_side_print.stl`

All five files are already oriented on the build plate. Do not use automatic
orientation. The base prints floor-down, the lid prints exterior-roof-down,
the arms print flat, and the panel frame prints rear-wall-down with its recess
facing upward.

Suggested first-print settings:

- PETG, ASA, or ABS
- 0.20 mm layer height
- four walls/perimeters
- five top and bottom layers
- no supports expected
- elephant-foot compensation enabled for the four bed-facing countersinks

Closure hardware:

- four M2 x 5 mm 90-degree countersunk flat-head screws (preferred)
- M2 x 6 mm screws also clear the modeled tip relief
- four M2 heat-set inserts, 2.90 mm pocket diameter x 3.20 mm deep

Install the inserts into the base bosses from the open seam before installing
the PCB and lid. Do not overtighten the screws on the first print.
