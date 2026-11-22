# MSV2 DisplayCenter enclosure v2 - Prusa MK4S / 0.4 mm / PLA

The same prototype-9 parts as `../p1s_0p4_pla_v2/`, sliced in OrcaSlicer 2.5.0
(command line) for a Prusa MK4S with a 0.4 mm nozzle and black PLA.

| Order | Project | Time | PLA | Supports |
| --- | --- | --- | --- | --- |
| 1 | `MSV2_DisplayCenter_MK4S_0p4_PLA_fit_check_plate_v2.3mf` | 29 min | 8.4 g | none |
| 2 | `MSV2_DisplayCenter_MK4S_0p4_PLA_front_and_stand_v2.3mf` | 1 h 18 min | 20.2 g | none |
| 3 | `MSV2_DisplayCenter_MK4S_0p4_PLA_rear_shell_v2.3mf` | 4 h 23 min | 74.9 g | tree, build plate only |

Settings: OrcaSlicer's `Prusa MK4S 0.4 nozzle`, `0.20mm STRUCTURAL @MK4S 0.4`,
and `Generic PLA @Prusa MK4S` profiles, with these changes:
- 3 walls, 5 top and 5 bottom layers, 20% gyroid infill
- 5 mm outer brim
- bed 60 C on any sheet; nozzle 230 C first layer, then 220 C
- filament colour black

The flattened profiles are in `../../profiles/orca_mk4s/`.

Each 3MF opens in OrcaSlicer with its settings and contains the sliced G-code.

**One-plate build (what was printed on 2026-11-17):**
`MSV2_DisplayCenter_MK4S_0p4_PLA_full_enclosure_v2.3mf` holds the front cover,
rear shell and kickstand on one plate (no fit-check plate): 5 h 35 min, 95.5 g.
Tree supports are on for the rear shell only; the cover and kickstand have
`enable_support = 0` per object. The parts are separate objects, so one can be
cancelled from the printer menu without stopping the others. It was copied to
the USB drive as `DisplayCenter_enclosure_0.4n_0.2mm_PLA_MK4S_5h35m.gcode`.

The command-line slicer cannot render thumbnails, so the printer shows no
preview image. In project 2 the kickstand stands 4.4 mm in front of the cover,
so their brims merge; snap them apart after the print.

The G-code checks the printer model (`M862.3 P "MK4S"`). On a plain MK4 the
printer warns about the model mismatch first. For an MK4, re-slice with the
`Prusa MK4 0.4 nozzle` profile.

Print the fit-check plate first and check it, as described in
`../p1s_0p4_pla_v2/PRINT_IN_BAMBU_STUDIO.md`.
