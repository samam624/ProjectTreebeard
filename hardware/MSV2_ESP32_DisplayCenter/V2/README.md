# MSV3 unified desktop display enclosure

This revision retains the proven screen-window dimensions from
`display_enclosure.step`, but builds a new rectangular enclosure around both
PCBs soldered together through their headers. The separate controller pod and
all snap-fit enclosure clips have been removed.

## PCB registration and enclosure

- The supplied two-PCB assembly remains at its native STEP origin
  **(0, 0, 0)**. Every enclosure export uses that same coordinate frame, so
  importing the PCB and enclosure without moving either component aligns them.
- Finished rectangular body envelope: **122 x 82 mm** before the USB feet.
- The 83.52 x 55.68 mm active LCD area remains centered in the original window.
- Display glass recess below the bezel face: approximately **0.147 mm**.
- Rear component clearance: approximately **0.869 mm**.
- Controller-to-front-cap clearance: approximately **2.0 mm**.
- Two foam-tipped rear posts stabilize the unsupported upper end of the
  controller PCB.

The front cover is a new solid rather than a patched copy of the old bezel. It
has one flat 1.02 mm face, one LCD opening, and continuous walls on all four
sides. There are no legacy fastener circles, repair bands, controller side slot,
or other seams on the visible face. The enclosure is clamped by exactly four M2
screws installed from the rear through real, unobstructed PCB mounting holes
into blind heat-set-insert pockets in the front cover:

1. Display PCB lower-left mounting hole.
2. Display PCB lower-right mounting hole.
3. Display PCB upper-right mounting hole.
4. Controller PCB upper-left mounting hole.

The display PCB upper-left hole is not used because the controller PCB directly
blocks it; the fourth screw instead uses the controller PCB hole immediately
above that corner. Rear screw passages are **2.4 mm**, recessed screw-head
pockets are **4.6 mm**, and front-cover insert pockets are **2.9 mm diameter x
3.2 mm deep** with at least **1.99 mm** of material remaining to the visible face.
The controller-corner insert column reaches to within approximately **0.049 mm**
of that PCB and is reinforced to the left extension wall; the matching rear post
is approximately **0.08 mm** from the opposite PCB face.

## USB-C cable path

The USB-C connector faces downward. A 16 mm wide bottom/rear cable bay opens
around the connector tunnel, and two 8 mm feet lift the enclosure above the
desk. A straight USB-C plug can therefore enter from below and its cable can
turn toward the rear without supporting the enclosure on the cable.

## Antenna placement

The supplied antenna geometry has a 9.4 mm hinged base. The rear-right mount is
now one unobstructed **10.0 mm circular through-hole**—there is no D-flat,
counterbore, or second diameter. The antenna folds 90 degrees horizontally
across the upper rear, entirely inside the front-view outline.

## Adjustable desk prop

The enlarged U-shaped kickstand has approximately **48.5 mm** pivot-to-foot arm
length and **4.0 mm** thick arms. It folds inside the body-and-feet footprint and
pivots on two M2 screws through 2.4 mm clearance holes into **2.9 mm diameter x
3.2 mm deep** heat-set-insert pockets. Tighten both screws to set the friction.
The deployed STEP shows a 30-degree example position.

## Output files

- `MSV3_display_enclosure_assembly.step` - closed enclosure, folded stand, and
  folded rear antenna reference
- `MSV3_display_enclosure_deployed.step` - stand shown at 30 degrees
- `MSV3_display_fit_check_assembly.step` - enclosure plus the complete PCB and
  antenna references
- `MSV3_display_pcb_alignment_check.step` - front cover plus the complete native-
  origin two-PCB assembly, with the rear left open for direct alignment checks
- `MSV3_display_enclosure_exploded.step` - separated inspection arrangement
- `MSV3_display_front_cover.step` - unified front cover with insert seats
- `MSV3_display_front_bezel_reference.step` - untouched source bezel reference
- `MSV3_display_rear_shell.step` - unified rear shell, USB bay, feet, screw
  bosses, and antenna bulkhead cutout
- `MSV3_display_kickstand.step` - separate adjustable prop
- `MSV3_display_antenna_positioned.step` - antenna articulated into its folded
  rear position
- matching `*_print.stl` files for the three printed parts
- `design_report.json` and `validation_report.json`

The STEP files import directly into Fusion 360 as solid bodies. PETG is a good
first-prototype material. Use approximately 0.25 mm foam or TPU pads on the two
controller support posts and verify the exact RP-SMA bulkhead pigtail, screw
lengths, USB plug overmold, and heat-set-insert length before final production.

## Regeneration

From the repository root:

```powershell
node hardware\MSV2_ESP32_DisplayCenter\V2\design\generate_desktop_enclosure.mjs
node hardware\MSV2_ESP32_DisplayCenter\V2\tools\validate_desktop_enclosure.js
```
