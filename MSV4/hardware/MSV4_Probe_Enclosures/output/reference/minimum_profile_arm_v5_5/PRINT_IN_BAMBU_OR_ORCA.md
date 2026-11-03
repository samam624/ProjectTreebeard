# MSV4 minimum-profile arm enclosure v5.5

Use `MSV4_minimum_profile_arm_solar_PRINT.3mf` from the parent `output/`
directory for the pre-arranged five-object plate. The five `*_print.stl` files
in this folder are equivalent individually oriented bodies for slicers that do
not preserve the 3MF layout.

Print the base back-down, the lid roof-down, both arms flat, and the panel frame
back-down. Start with 0.20 mm layers, five walls, six top/bottom layers, and
25-35% infill. PETG is suitable for the first fit; use ASA for outdoor service.
No supports are required, and none should be added inside the PCB groove or
probe throat.

The PCB registers in the shaped base groove with 0.25 mm XY clearance and
rests on four broad 3 x 4 mm floor-tied pads at the 1.6 mm substrate underside.
All pads are in component-free areas. Confirm all four contact bare FR4 and
that no bottom-side component bears on the base. This body intentionally has
no narrow screw-case shelves or lid compression fins. Tape the body seam for
the first fit test; do not bond it until the PCB, 10 x 20 x 3 mm battery, panel
wiring, and arm sweep have all been checked.

Final assembly uses four M2 x 5-6 mm pivot screws and four nominal 3 mm OD x
3 mm heat-set inserts. There are no case screws or body gasket. After the fit
test, route both panel leads through the left-side 3 mm exit, bond and seal the
complete base/lid seam, seal the wire exit, exposed-blade throat, LED window,
and panel perimeter, then leak-test the empty housing before installing the
electronics.
