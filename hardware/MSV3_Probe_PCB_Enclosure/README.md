# MSV3 probe enclosure and adjustable solar mount

This is the seventh prototype for `MSV3_Probe_PCB.step`. It encloses only the
upper electronics section; the long pointed capacitive probe remains exposed.
The case follows the PCB head, snaps together externally, pinches the PCB before
gluing, and supports a movable solar-panel carrier on two side arms. The USB-C
opening is intentionally removed in this revision.

## PCB thickness correction and retention

The Altium STEP represents the board as 1.81224174 mm thick. This design uses
the real JLCPCB thickness of **1.60 mm**, with the STEP's top surface kept at
Z = 0.

The block-like internal pads have been removed. Two continuous base shelves and
two matching lid compression rails now run along component-free PCB edge
margins. The lid rails apply **0.12 mm vertical interference** when snapped
closed. Lower side guides leave 0.05 mm nominal lateral clearance, preventing
the PCB from drifting sideways. The shelves reach only 0.15 mm onto the board,
and validation reports no component collision.

## Waterproof-oriented closure

The former open probe throat is now a solid end wall. The enclosure split runs
through the PCB thickness, so the tapered probe opening is divided between the
base and lid: lay the PCB into the base, then install the lid to pinch around
the probe. The opening has 0.20 mm XY and 0.15 mm Z clearance for epoxy or
outdoor polyurethane adhesive. The USB-C wall is closed, the pivot insert
pockets are blind, and the two enclosure wire entries are only 1.6 mm. After
final soldering, bond the probe slot, wire entries, and full snap seam.

## Solar-panel assembly

- Nominal panel size: **30 mm wide x 54 mm tall** (portrait)
- Front panel recess: **30.4 x 54.4 mm x 1.6 mm deep**
- Lightweight tray body: 36 x 60 x 3.0 mm
- Closed rear wall: 1.4 mm thick except for the two wire passages
- Two panel wire holes: 2.2 mm diameter, both inside the recess near its top
  and bottom ends
- Case wire entry: two 1.6 mm holes through the left side wall, positioned away
  from the antenna/top region and sized to be filled with adhesive
- Left arm wire route: 2.6 mm wide x 0.55 mm deep open channel
- Enclosure pivots: centered at the former USB-C side-opening position
- Panel pivots: centered vertically on the solar-panel carrier
- Folded position: panel tray is parallel above the enclosure
- Two roof-clearing pivot arms, 36 mm center-to-center and only 1.6 mm thick
- The 36 mm spacing is the rounded practical minimum: the limiting rear roof
  corner is 32.23 mm from the pivot, leaving approximately 0.77 mm clearance
  after allowing for the arm's 3 mm rounded end
- Arm holes: 2.4 mm M2 clearance
- Enclosure and frame insert pockets: 2.9 mm diameter x 3.2 mm deep
- Axial printing clearance between each arm and pivot boss: 0.25 mm

Use four short M2 heat-set inserts and approximately M2 x 5-6 mm screws: two at the brain
enclosure and two at the panel carrier. Tighten them enough to hold the selected
solar angle by friction. The enclosure-side bosses are part of the base, so the
panel load does not pull on the removable snap lid. Fill both 1.6 mm side wire
entries with adhesive after the leads are soldered.

## Ready-to-use files

The `output` directory contains:

- `MSV3_brain_enclosure_assembly.step` - all five parts in the folded position
- `MSV3_brain_enclosure_exploded.step` - separated inspection view
- `MSV3_brain_enclosure_base.step` and `MSV3_brain_enclosure_lid.step`
- `MSV3_solar_arm_left.step` and `MSV3_solar_arm_right.step`
- `MSV3_solar_panel_frame.step`
- matching `*_print.stl` files, already placed on the print bed
- `design_report.json` and `validation_report.json`

The STEP files open directly in Fusion 360 as editable solid bodies.

## Native Fusion 360 generator

To rebuild the design with a parametric Fusion timeline:

1. Open **Utilities > Scripts and Add-Ins** in Fusion 360.
2. Add `fusion/MSV3_Brain_Enclosure` as a script location.
3. Run **MSV3_Brain_Enclosure**.
4. The script creates Base, Lid, Left Arm, Right Arm, and Solar Panel Frame
   components and exports a native F3D plus STEP copies to
   `output/fusion_exports`.

The constants and user parameters expose the important board, panel, clamp,
wire-hole, and pivot dimensions for later adjustment.

## Suggested first print

- Material: PETG, ASA, or ABS; PETG is the easiest first prototype
- Layer height: 0.20 mm
- Walls/perimeters: 4
- Top/bottom layers: 5
- Supports: none expected with the supplied STL orientations
- Print one complete set before committing to a batch

Check the actual solar panel with calipers before final production. The supplied
30.4 x 54.4 mm recess gives 0.20 mm clearance per side around a true 30 x 54 mm
panel, but panel laminations and printer calibration can vary.

## Environmental note

This geometry is designed to be adhesive-sealed, but it is not yet a certified
watertight housing. Use conformal coating, completely bond the probe exit and
case seam, pot the wire passages after soldering, and pressure/leak-test a final
printed assembly before outdoor deployment.

## Regenerating and validating

Install the local geometry dependencies if needed:

```powershell
npm.cmd install --prefix hardware\MSV3_Probe_PCB_Enclosure\tools\node
```

Generate and validate from the repository root:

```powershell
node hardware\MSV3_Probe_PCB_Enclosure\design\generate_enclosure.mjs
node hardware\MSV3_Probe_PCB_Enclosure\tools\validate_enclosure.js
```

The current validation passes all STEP/STL import, print-bed, PCB clearance,
PCB clamp, and five-solid assembly checks. Inspection images are under
`renders`.
