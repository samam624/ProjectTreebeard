# Pump controller Altium footprints

Created 2026-11-11. **Usable for placement and board-size planning; not yet a fully released fabrication library.** Your existing 0805 passive library is unchanged.

Install **[Pump_Controller.IntLib](Pump_Controller.IntLib)** using Altium's file-based library controls. Editable [SchLib](Pump_Controller.SchLib), [PcbLib](Pump_Controller.PcbLib), and [LibPkg](Pump_Controller.LibPkg) are included. All five new symbols place directly with one embedded footprint each. [Dimension preview](footprint-preview.svg).

| Part | Symbol to place | Embedded footprint | Status |
|---|---|---|---|
| TI TPS70933DBVR | `TPS70933DBVR` | `TB_TPS709_DBV0005A` | Pad geometry and pinout checked against TI DBV drawing; 1 IN, 2 GND, 3 EN, 4 NC, 5 OUT. |
| AOS AO3400A | `AO3400A` | `TB_AOS_SOT23_PO00001M` | AOS recommended land pattern; 1 gate, 2 source, 3 drain. |
| Vishay SS34-E3/57T | `SS34-E3_57T` | `TB_VISHAY_SMC_SS34` | **SMC / DO-214AB**, not SMA. Pad 1 cathode/banded end, pad 2 anode. |
| MPD BA4AAPC, place two | `BA4AAPC_LAYOUT_CHECK` | `TB_MPD_BA4AAPC_REVH` | Drawing dimensions implemented. **Confirm PCB-view orientation and terminal polarity on a physical holder before routing/fabrication.** |
| Lab-sourced electrolytic | `C_RADIAL_D10_P5` | `TB_CAP_RADIAL_D10_P5` | **220 µF, ≥25 V, 5 mm lead pitch, ≤10 mm diameter, ≤20 mm height, ≤0.6 mm leads.** Pad 1 positive. This is a deliberately generic envelope, not an exact MPN. |

## Parts you already have libraries for

| Part | Existing usable source |
|---|---|
| E73-2G4M08S1C | [Local copy of your existing IntLib](existing/E73/E73-2G4M08S1C.IntLib). Altium resolves its footprint; all 43 symbol pin numbers have corresponding pads. Original remains in `Documents/Altium Libraries/Micro-Controllers/nRF52840/`. |
| SC-32S 32.768 kHz crystal | Existing [SchLib](../SC-32S_C97604/SC-32S_C97604.SchLib) and [PcbLib](../SC-32S_C97604/SC-32S_C97604.PcbLib), footprint `SC-32S_SMD3215_2P`. Two 1.0 × 1.8 mm pads at ±1.0 mm. |
| Kamoer NKP pump | Existing [PcbLib](../Kamoer_NKP/Kamoer_NKP.PcbLib) and [mounting notes](../Kamoer_NKP/README.md). `KAMOER_NKP_VERTICAL_TABS` is available alongside two other mounting orientations. Motor-tab spacing and dimensions were scaled in that earlier library and still require physical measurement. |
| 0805 resistors and ceramic capacitors | Use your existing library. No replacement passive library generated. |
| SWD contacts | PCB pads; no purchased connector selected. |
| Fuse / fuse holder | **Not selected; no footprint frozen.** Reserve space until its form factor and rating are chosen. Motor starting/stall current is still needed for rating selection. |

The crystal and pump source libraries contain their footprints. Their existing integrated-library queries did not report the expected full model sets in this Altium session (crystal 0, pump 2 rather than 3), even after recompiling separate copies. Use the linked source libraries for those parts; do not treat this as proof their original IntLib links are good. `existing/Crystal/` and `existing/Pump/` are diagnostic rebuild copies, not replacements for your originals.

## Mechanical details

All dimensions are millimetres, as viewed from the component side. Courtyards are on Mechanical 1; assembly outlines and conservative extruded body envelopes are on Mechanical 13. The capacitor's square body envelope intentionally reserves the full can diameter in both axes; it is not a detailed cylindrical model.

The BA4AAPC footprint origin is midway between its mounting holes. Body envelope: 57.48 × 63.09 × 15.88 high. NPTH mounting holes: Ø2.03 at `(0, ±27.305)`. Electrical holes: Ø1.02, pad diameter 2.2, at `(26.82, -21.135)` and `(26.82, -8.435)`, matching the drawing's 12.70 pitch and 6.17 offset from the lower mounting hole. Contacts are intentionally named **LOWER_CONTACT / UPPER_CONTACT**, not +/−. The drawing does not explicitly label polarity in its PCB layout. These IDs are our library convention, not manufacturer terminal numbers. Check orientation, polarity, and mounting screw clearance with a real sample before fabricating.

The holder drawing distinguishes the **Ø2.36 hole in the plastic** from its **Ø2.03 PCB hole**. Use an appropriate screw/eyelet arrangement; a 2-56 screw should not be assumed to clear a Ø2.03 PCB hole.

Place two separate holder instances and route the series link after confirming polarity. Do not put components under the holder body. Battery insertion/removal clearance is additional to the body courtyard.

## Verification evidence and remaining gate

- Genuine Altium-generated binary sources and integrated library, reopened through Altium's APIs.
- Saved pin names/types, pad numbers, positions, sizes, hole sizes, plating, and body presence checked in both directions. [Readback](AuditGeometry-report.json).
- Each new symbol resolves exactly its one intended embedded PCB footprint. [Model report](VerifyModels-report.json).
- Five actual instances placed from the IntLib; metadata exported from those instances. [Placed BOM](placed-bom.csv).
- **Compiled connectivity remains unverified:** Altium's project compile API returned `false`, zero components/nets, and no reported violations. Native project recompile also did not produce a netlist. A saved [test sheet](Library_Test.SchDoc) and [project](Library_Test.PrjPcb) are provided to reproduce this. They are an isolated library test, **not the pump circuit**. Do not interpret the empty netlist as a passed check.
- [Validation summary](validation.json) reports the passed checks separately from the incomplete compiler gate.

The component-generation skill requires: “A placed instance compiles and its generated netlist shows the expected pin-to-net mapping.” See [the exact verification rule](C:/Users/samca/.codex/skills/altium-component-generator/SKILL.md). That gate has not passed, so this pack is for planning, not a claim of completed fabrication validation. Physical holder and pump checks are also outstanding.

## Sources and sourcing

Manufacturer drawings are retained under `reference/`:

- [TI TPS709 datasheet](https://www.ti.com/lit/ds/symlink/tps709.pdf), DBV0005A board layout 4214839/K, August 2024. Do not substitute TPS709A/B pinouts; do not tie EN to the 12 V rail.
- [AOS AO3400A](https://www.aosmd.com/sites/default/files/res/data_sheets/AO3400A.pdf), Rev 3.1, July 2023; [SOT23 land pattern](https://www.aosmd.com/res/packaging_information/SOT23.pdf), PO-00001 M.
- [Vishay SS32–SS36 drawing](https://www.vishay.com/docs/88751/ss32.pdf), document 88751, 23 April 2020, page 3. Chosen pads are 1.72 × 3.20 at ±3.205: 8.13 outside span and 4.69 inside gap, satisfying the published dimensions.
- [MPD BA4AAPC drawing](https://www.memoryprotectiondevices.com/datasheets/BA4AAPC/BA4AAPC-datasheet.pdf), Rev H, 6 March 2019.

Live LCSC browser check, 2026-11-11: TPS70933DBVR [C89347](https://www.lcsc.com/product-detail/C89347.html) 5,950; AO3400A [C20917](https://www.lcsc.com/product-detail/C20917.html) 339,570; Vishay SS34-E3/57T [C13354](https://www.lcsc.com/product-detail/C13354.html) 5,990. All three MOQ/multiple 5. Stock changes; recheck at ordering. MPD BA4AAPC [C6748055](https://www.lcsc.com/product-detail/C6748055.html) was **out of stock**; [Digi-Key 2439225](https://www.digikey.com/en/products/detail/mpd-memory-protection-devices/BA4AAPC/2439225) is an alternate catalog source, with availability not reconfirmed here. Lab capacitor intentionally has no exact MPN or LCSC code.

## Reproduction

`generator/prepare.py` writes the DelphiScript and manifest. `Invoke-AltiumScript.ps1` runs each script in a unique temporary project. Build to a **fresh output directory** to avoid Altium's open-document cache. Run `GeneratePump.pas`, `AuditGeometry.pas`, `VerifyModels.pas`, `PlaceAndCompile.pas`, and `CompileSavedTest.pas`; then `validate.py` checks the root readback reports. The Altium scripts require a functioning licensed Altium session; the existing session's schematic compiler failure remains unresolved. No user project was modified and no Altium process was terminated.
