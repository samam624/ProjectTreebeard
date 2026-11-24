# MSV3 unified desktop display enclosure

A rectangular two-part enclosure (front cover + rear shell) with a folding
kickstand, built around the display module and the DisplayCenter controller
PCB soldered together through the display's 14-pin header.

**Current design: prototype 10 (2026-11-23).** Print pack:
[`output/p1s_0p4_pla_v3/`](output/p1s_0p4_pla_v3/) (Bambu P1S, sliced
`.gcode.3mf`, including a one-plate build of the whole enclosure; see its
`PRINT_IN_BAMBU_STUDIO.md`). There is no prototype-10 MK4S pack yet:
OrcaSlicer is not installed on the machine that built v3, so load the v3 STLs
into a slicer by hand. The v2 packs (prototype 9:
[`output/p1s_0p4_pla_v2/`](output/p1s_0p4_pla_v2/),
[`output/mk4s_0p4_pla_v2/`](output/mk4s_0p4_pla_v2/)) and the v1 pack
(prototype 8, 2026-09-24) are kept for history and should not be printed.

## What prototype 10 changes

Prototype 9 printed and fitted well. The maintainer asked for two changes on
2026-11-23:

1. **Fifth screw at the loose corner.** Prototype 9's four screws sit at the
   top-left, two near the top-right and right edge, and the lower right (seen
   from the screen). The bottom corner on the controller/USB side
   (**bottom-left seen from the screen, bottom-right seen from the back**) had
   no fastener, so the seam gapped there. Prototype 10 adds a structural
   standoff there: a rear-shell post (6.0 mm dia.) and a front-cover insert
   boss (6.4 mm dia.), both webbed into the left and bottom walls, at design
   (-56.50, -5.00) / output (-62.89, -18.90). It touches no PCB: nothing in
   the board stack is below design y = 0.14, and the boss edge is 1.9 mm
   below the display PCB. It takes an **M2x16** (the controller-screw length)
   and a fifth heat-set insert. The insert face is the seam, and the post
   stops 0.10 mm short of it so the perimeter walls, not the post, seat.
2. **3.5 mm 45-degree chamfer on every exterior body edge**: the four corner
   edges (they run across the seam), the four front edges and the four back
   edges. The feet get 2.0 mm, and their outer sides moved in 1.5 mm to where
   the corner chamfer meets the bottom face. The bottom-back edge is chamfered
   only between the feet, so the back face runs straight into each foot. The
   cavity's matching inside edges are chamfered too, so no wall is thinner
   than 1.6 mm under a chamfer. The finished cover is trimmed back to the
   chamfered outline. Without that trim, the boss webs' 0.1 mm overlap into
   the walls poked ~0.4 mm nubs through the front chamfer near the corners
   (caught in the first v3 slice, 2026-11-23). 3.5 mm is about the limit. Inside, the
   top-left corner fill clears the controller board's corner by ~0.6 mm. On
   the back, the chamfer clears the controller screw pockets by ~0.4 mm.

Everything else (window, screw positions, USB bay, kickstand, fit-check plate)
is unchanged from prototype 9. Against the as-built PCB reference the overlap
volumes are the same as prototype 9's (front cover 0.05 mm^3 at the display
clamp bosses, rear shell and plate zero).

## Why prototype 9 exists

Prototype 8 was printed and assembled on 2026-11-17 and had six problems:

1. Display edges showed through the window.
2. Two of its four screws could not be fitted, so one side of the stack hung
   loose, and one rear post was too long to close the case.
3. The maintainer asked to mount on the display's right-hand pair plus the
   controller's top pair instead.
4. The USB-C connector was off-centre in its cutout.
5. The top border was 18 mm and the bottom 6 mm.
6. The prop pivot holes did not go through.

Issues 2 and 4 share one root cause. **`MSV3_desktop_display.step` puts the
controller PCB in the wrong place.** It draws the display's 14-pin header 4.0 mm
above the display board's centre line. On the real module the header is
centred on the mounting-hole pattern (measured in a photo), and the controller
hangs on that header. Prototype 8 matched the model and still did not fit the
hardware.

Prototype 9 is built around an **as-built** position: the controller and the
header are moved by **(-3.40, -3.80, -0.30) mm** from the model. The evidence:

| Source | What it gives | Kind |
| --- | --- | --- |
| Front photo, header pads vs display holes (same plane) | X -3.7, Y -4.0 | measured |
| USB-C photo, receptacle vs the printed 16 mm bay | X -3.3 | measured, perspective-corrected |
| Calipers, controller upper-left to display upper-left hole | ~10.5 mm c-c | measured (approximate) |
| Calipers, display lower-left hole to USB-C centre | ~23.5 mm | measured (approximate) |
| Fab drill file (`pcb/DisplayCenter_controller_fabrication_2026-07-14.zip`) | controller holes 2.2 mm, top pair 45.00 mm apart | design data |
| Z | -0.30 is a midpoint guess; the controller bosses leave about 0.6 mm on each face | **inferred** |

The X/Y estimate is good to about +/-0.4 mm. An M2 screw has only +/-0.6 mm of
slack in the display's 3.2 mm holes, so check the fit-check plate before the
full print.

## What changed

- **Window** = the 83.52 x 55.68 mm LCD active area, centred on it. The old
  88 x 58 mm window was 2.24 mm oversize per side, and its right edge sat
  0.26 mm inside the glass's white frame.
- **Mounting**: four M2 screws from the rear. Two go through the display PCB's
  right-hand holes (seen from the screen), which bear on the display PCB as
  before. Two go through the controller PCB's top holes, which have bosses
  stopping 0.6 mm short of each face. The display's lower-left boss and post,
  and the foam-tipped controller post, are gone.
- **Symmetric front**: 15.95 mm top and bottom, 19.24 mm left and right. The
  top is set by the as-built controller edge plus 1.0 mm. The body is now
  122 x 87.58 mm (was 122 x 82), not counting the feet.
- **USB-C**: the cable bay is re-centred on the as-built receptacle and widened
  to 16.5 mm. A notch in the rear edge of the front cover's bottom wall clears
  plug overmolds up to about 10 mm tall.
- **Kickstand**: the pivot hole runs through both shell lugs (v1 had a blind
  insert pocket that opened away from the arm). Each arm has a hex trap on its
  inner face for a captive M2 nut, so each side takes one M2x10 screw and one
  nut. The pivot moved down 8.78 mm with the bottom edge, so the lean geometry
  relative to the desk is unchanged.
- **Fit-check plate**: a 1 mm copy of the front face with four guide tubes.

Screws: M2x16 for the controller holes. For the display holes, M2x20 with a
thin washer, or M2x18 with about 1.2 mm of engagement. M2x10 + nut for each
pivot. All four heat-set inserts go in the front cover. (Prototype 10 adds a
fifth insert and a fifth M2x16 at the corner standoff.)

## Coordinate frame

The supplied PCB STEP stays at its native origin **(0, 0, 0)**, and every
export uses that frame. Fit is checked against
`MSV3_display_asbuilt_pcb_reference.step`: the STEP with solids 0-301
(controller) and 312-326 (display header) moved by the as-built offset, plus a
1.6 mm controller slab. The STEP's own controller slab is 0.411 mm, and the
board edge in the USB photo is about 1.6 mm. Against that reference:

- front cover: 0.05 mm^3, which is the display PCB's copper pads touching the
  two display clamp bosses
- rear shell and fit-check plate: zero overlap
- rear component clearance: 0.57 mm, at the same cavity depth that closed over
  the real stack in prototype 8

## Output files

- `MSV3_display_front_cover.step`, `MSV3_display_rear_shell.step`,
  `MSV3_display_kickstand.step`, `MSV3_display_fit_check_plate.step`
- matching `*_print.stl` files, oriented for printing
- `MSV3_display_enclosure_assembly.step`: closed enclosure, folded stand
- `MSV3_display_enclosure_deployed.step`: stand at 30 degrees
- `MSV3_display_enclosure_exploded.step`
- `MSV3_display_fit_check_assembly.step`: enclosure + as-built PCB stack
- `MSV3_display_pcb_alignment_check.step`: front cover + as-built PCB stack
- `MSV3_display_asbuilt_pcb_reference.step`
- `MSV3_display_front_bezel_reference.step`: untouched legacy bezel
- `design_report.json` (includes the per-hole screw plan) and
  `validation_report.json`

The v1 CAD and slicer zip packs in `output/` belong to prototype 8.

## Regeneration

From the repository root:

```powershell
cd MSV3\hardware\MSV3_Probe_PCB_Enclosure\tools\node; npm install; cd ..\..\..\..\..
node DisplayCenter\hardware\Treebeard_ESP32_DisplayCenter\V2\design\generate_desktop_enclosure.mjs
node DisplayCenter\hardware\Treebeard_ESP32_DisplayCenter\V2\tools\validate_desktop_enclosure.js
```

The validator states the as-built offset independently and fails if the
generator disagrees. If the fit-check plate shows a different offset, change
`controllerAsBuiltOffset` in the generator and `AS_BUILT_OFFSET` in the
validator together.

P1S slicing (Bambu Studio 02.09 installed). Copy the four `*_print.stl` files
into the pack folder first. `--tag` names the pack (`v3` = prototype 10, the
default):

```powershell
python DisplayCenter\hardware\Treebeard_ESP32_DisplayCenter\V2\tools\slice_p1s_pack.py --tag v3
```

Do not point the Bambu Studio command line at the stock presets directly. It
does not resolve their `inherits` chains or the P1S preset's `include` list
(where its start/end G-code lives), and on 2026-11-17 that produced plates with
a `G28`-only start sequence and generic speeds and cooling. The script
flattens the presets itself, using the overlays in `profiles/`, and slices with
the flattened copies in `profiles/p1s_flattened/`. It checks the G-code against
them setting by setting, and refuses the pack on any difference. The overlays
must not set machine keys such as `printable_area`; the script refuses that
too.
