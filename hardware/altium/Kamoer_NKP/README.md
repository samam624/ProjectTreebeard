# Kamoer NKP 12 V DC peristaltic pump Altium library

Install `Kamoer_NKP.IntLib` and place `Kamoer_NKP_12VDC` (designator `M?`).
It is the two-wire brushed-DC NKP (the 12 V "DC" motor code), with a
3 mm ID x 5 mm OD silicone pump tube. It is **not** the four-wire stepper NKP.

## Symbol

A circled M with pin 1 = **+** at the top and pin 2 = **-** at the bottom.
Pin 1 goes to the motor tab with the red mark. On a peristaltic pump,
polarity sets the flow direction, so if it pumps backwards, swap the two leads.
The pump is not polarity-sensitive in any damaging way.

Your driver still needs the flyback diode across these two pins (stripe
to pin 1 / +12 V). See `DisplayCenter/docs/AUTO_WATERING_PLAN_2026-10-08.md`.

## Footprints

### Default: `KAMOER_NKP_VERTICAL_TABS`, standing on the motor tabs

The pump stands upright on the PCB with its motor end down. Its two motor
solder tabs pass through **curved slots** and are soldered from below. Two
**M3 x 40 mm hex standoffs** go from the PCB to the straight plate's two
holes. They carry the pump's weight and vibration, so the tabs carry only
current.

**Why the slots are curved (2026-11-18).** On the delivered pumps the pump
head is not square to the motor tabs, so with the plate on the standoffs the
tabs do not land on the standoff line. Each tab therefore sits in an arc slot
centred on the motor axis. You can turn the pump up to **±30°** to line up the
head, and the tab slides along its arc. JLCPCB drills plated slots, so a plated
slot can only be straight. Their help page also says irregular plated slots
are not guaranteed. The arcs are therefore **routed non-plated slots** with
copper arc pads around them. The tab is soldered to the **bottom** pad. There
is no plated barrel, which is acceptable because the standoffs carry the load.

```
        tubes ->  ┌──────────────┐   pump head (23.5)
     screw ──>  ══╪══ plate ═════╪══  <── screw
                ║ │              │ ║
       M3 x 40  ║ │  motor dia29 │ ║  M3 x 40
      standoff  ║ │              │ ║  standoff
                ║ └──┬───────┬───┘ ║   can face ~1.3 above board
  PCB ══════════╩════╪══[ ]══╪═════╩════   dia 13 hole for the bearing boss
     screw ──>       tab     tab          tabs soldered in arc slots, ~2.3 out the bottom
```

- Standoff holes: NPTH dia 3.2 at 48.5 mm (the plate's own spacing).
- Tab slots: **routed, non-plated arc slots** (board cutouts), 1.0 mm wide.
  Each centreline is the R 11.25 circle (tabs 22.5 apart), ±40° about the
  nominal tab angle, with round ends. That is ±30° of rotation, plus 8.2° for
  half of the 3.2 mm tab, plus spare. Geometrically the tab clears at up to
  about ±33.8°. Pin 1 is centred at 180° (x = −11.25) and pin 2 at 0°.
  JLCPCB rules: non-plated slots are at least 1.0 mm wide (±0.2) and CNC
  routed; copper must be at least 0.2 mm from a routed edge. The pump board's
  Board Outline Clearance rule is 0.3 mm, so the copper is set back 0.32 mm and
  the pad corners end up 0.308 mm from the slot.
- Tab pads: copper arcs on **both** layers, built **only from pads named `1`/`2`**, so all
  the copper carries the pin's net. Each arc has an inner and an outer 1.5 mm band of
  overlapping 1.5 x 1.0 rectangular SMD pads, one every 3° over ±48°. One radial bridge pad
  past each end of the slot joins the two bands. It sits on the last band pad's angle and
  width, so each arc end is flush. That is 68 pads per pin per layer. **Solder mask:** each
  pad keeps its normal rule-sized opening. On each side, one mask region also follows the
  copper outline 0.05 mm outside it, with straight ends parallel to the flush copper ends.
  It exposes the joint and the bare strip beside the slot, but none of the surrounding pour.
  History: a 0.5° step between the end pad and the bridge left 0.046 mm mask slivers, and a
  plain ±54° wedge opening exposed pour copper. Tenting the pads from the script did not
  work: `IsTenting_*` read back False, and setting the mask expansion crashed
  ScriptingSystem.dll. The tab is soldered on the bottom. A tab at any angle is
  about 0.4 mm from the copper, and the solder fillet bridges that.
  *History:* the first build (2026-11-18) drew the arcs as copper regions touching the
  stitch pads. Altium left those regions with No Net, so the router was blocked and DRC
  reported region-to-pad shorts. Do not go back to regions.
- Three plated stitch holes per pin, 0.6 mm drill with a 1.2 mm pad, on R 12.75 at the
  nominal angle and ±25°. They tie top copper to bottom. Pin 1's are square and rotated
  radially. Unrotated, a square's corner came within 0.15 mm of the slot. Pin 1 (`+`) is
  the tab beside the red dot. Rotating the pump 180° swaps the tab ends and also turns the
  tubes to point the other way.
- Both sides of the joint are bare copper (top included), and both layers get paste openings. If you order a
  stencil or assembly, delete the paste on these pads, or ignore it: the pump is
  hand-soldered.
- Bearing boss: a dia 13 routed hole under the motor's centre, so the
  end cap sits close to the board.
- Where you can put parts: none inside the dia 29 can circle. Between the
  can and the plate's outline (Mech 13, about 40 mm overhead), parts of any
  normal height fit. You can put the MOSFET, diode and connector there to
  keep the board within the pump's 55 x 41 mm shadow.
- Hardware, **as planned 2026-11-11**: the plate is screwed to the outside of
  the enclosure wall, through the wall, into the standoffs inside that hold
  the PCB. See `DisplayCenter/docs/AUTO_WATERING_PUMP_MOUNT_PLAN_2026-11-11.md`.
  The plate face to PCB distance stays 40 mm, so the standoff is
  **40 − wall thickness** (M3 hex, F-F). The outside screw is M3, long
  enough for the plate (~3.5 mm) + the wall + ≥4.5 mm of thread. The inside
  screw is M3 x 6 through the 1.6 mm PCB. The plate's holes are dia 3.2 (M3
  clearance). With no wall, it is 40 mm standoffs and M3 x 8 screws.

**Measure before you fab.** These motor-end numbers were scaled off the
catalogue drawing. They are not dimensioned on it:

| Measure on your pump | Scaled value | Footprint assumes |
|---|---|---|
| Tab centre-to-centre | 22.5 mm | arc slots on R 11.25 |
| Tabs in line with the plate's 2 holes? | **no** on delivered pumps (2026-11-18) | arc slots allow ±30° |
| Tab width x thickness | ~3.2 x ~0.5 mm | 1.0 mm slot (0.8 at the −0.2 tolerance) |
| Tab length past the can's end face | ~5.2 mm | ~2.3 mm out the board's bottom face with 40 mm standoffs |
| Bearing boss diameter x height | ~10.7 x 2.2 mm | dia 13 hole |
| Plate (motor side) to can end face | ~38.7 mm | standoff = this + 1 to 1.5 mm, so 40 mm |

If any of these differ, change the matching constant in
`generator/GenerateKamoerNKP.pas`, along with its expected value in
`Test-KamoerNKP.ps1`, then rebuild.

### Alternatives, connected by wires

| Footprint | Bracket | How it mounts |
|---|---|---|
| `KAMOER_NKP_STRAIGHT_PANEL` | Straight plate: diamond-shaped, **2 holes** | The PCB is the mounting panel. The plate and pump head sit on top. The dia 29 motor hangs through a **dia 31 board cutout** and needs about 43 mm clear below the board. Use 2 x M3 screws. |
| `KAMOER_NKP_LPLATE_FLAT` | L plate: a foot with **4 holes** | The foot is screwed flat to the PCB with M4 screws in either hole pair. The motor runs parallel to the board, about 6 mm above it, so low parts can sit under it. |

On these two the tabs end up far from the board, so they use wire pads:
two plated holes, 1.3 mm drill, 2.5 mm pad, 5.08 mm pitch, pin 1 square and
marked `+`. You can solder the wires straight in, or fit a 2-way 5.08 mm
screw terminal in the same holes. `VERTICAL_TABS` needs the straight
(2-hole) plate. An L-plate pump cannot stand on its tabs this way.

### Dimensions used

Source: the NKP page of the Kamoer product catalogue (catalogue page 02,
"Straight" and "L plate" drawings, unit mm).

| Item | Value | Footprint |
|---|---|---|
| Straight plate holes | 2 - dia 3.2 at 48.5 +/-0.15 | NPTH dia 3.2 at x = +/-24.25 |
| Straight plate outline | 54.5 x 40.3 | silk / Mech 13 |
| Motor | dia 29, 44 long behind the plate | dia 31 cutout; 3D body below the board |
| Pump head | 23.5 in front of the plate | 3D body 23.5 tall |
| L plate holes | 4 - dia 4.3 at 48 +/-0.15 and 37 +/-0.15, 8.5 from the plate face | NPTH at x = +/-24.0, +/-18.5 |
| L plate size | foot 56 wide, upright 46 wide, 40 tall; motor 47 and head 20.3 either side of the plate face | silk / 3D |

**Measured vs. scaled:** the hole diameters, hole spacings, the 8.5 mm offset,
the motor diameter and the overall lengths are dimensioned on the drawing.
Everything else is approximate, scaled off the drawing to about ±1 mm: the
plate's diamond outline, the tube-stub positions, the L-plate ear depth, the
motor axis height (~20.7 mm) and the head width. Before you fab, check the
two hole spacings with calipers against the pump in hand.

### Layers

- Top Overlay: bracket outline, cutout edge or motor overhang, tube
  direction, `+` at pin 1.
- Mechanical 1: courtyard. On the straight plate it includes the two tube
  stubs, which stick out about 14 mm past the plate edge, flat in the
  board plane, 8 to 15 mm above the board.
- Mechanical 13: assembly outline and extruded 3D bodies, the same
  convention as `SC-32S_C97604`.
- Straight plate only: one Board Cutout region on Multi-Layer.

### DRC notes

- With the default Hole Size rule (max 2.54 mm), the 3.2 and 4.3 mm NPTH
  holes are flagged. Raise that rule to 5 mm or scope an exception to this component.
- The cutout is a routed board cutout, not a drilled hole, so JLCPCB
  mills it. Keep copper at least 0.3 mm from its edge.

## Electrical (Kamoer catalogue)

12 V DC, 0.25 A running. The catalogue does not give a start/stall
current. Measure the winding resistance; stall current ≈ 12 V / R. Size the
MOSFET and supply for that, not for 0.25 A.

`Manufacturer Part Number` is set to `NKP-DC-S10B`, which is the code third-party
listings give for the 3 x 5 mm tube 12 V version. Check it against the label on
your pump. The part is excluded from the LCSC BOM upload; it is bought separately.

## Rebuild

With Altium installed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\generator\Build-KamoerNKP.ps1
```

This generates the SchLib, PcbLib and LibPkg, compiles the IntLib, and then runs
`Test-KamoerNKP.ps1`. The test reads the symbol's footprint links from the
SchLib, and reads back every pad, hole, board-cutout extent and copper/mask region
count from the PcbLib.
It compares them against numbers typed into the test independently of the
generator.

Do not trust `IntegratedLibraryManager.ModelCount` across rebuilds in one
Altium session. On 2026-11-11 it kept reporting the previous build's two
links for a rebuilt IntLib whose `LibCrossRef.Txt` listed all three. If an
installed copy of this library shows only two footprints, remove it and
install it again, or restart Altium.
