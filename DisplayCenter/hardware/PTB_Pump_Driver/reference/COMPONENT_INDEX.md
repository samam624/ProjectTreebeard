# PTB_Pump_Driver: component index and datasheets

Built 2026-11-18 for the wall-powered revision: a DC jack plus a series SS34 replaces the two
BA4AAPC holders. The Altium project is `C:\Users\Public\Documents\Altium\PTB_Pump_Driver`.

Each PDF is in `datasheets/`, with a matching `_extracted.txt` (PyMuPDF text, `===PAGE N===`
markers) for searching. As for MSV4, the PDFs are git-ignored and the `.txt` files are tracked. To
re-fetch a PDF, use the source below with `MSV4/hardware/reference/fetch_datasheet.py`.

| Refdes | Part | Role | Datasheet file | Source |
|---|---|---|---|---|
| U1 | Ebyte E73-2G4M08S1C | nRF52840 BLE module | `E73-2G4M08S1C_user_manual.pdf`, `nRF52840_PS_v1.1.pdf` | copied from `MSV4/hardware/reference/datasheets/` |
| U2 | TI TPS709A33DBVR | 3.3 V LDO, 30 V input. **A-version DBV pinout: 1 OUT, 2 GND, 3 IN, 5 EN** | `TPS709_TPS709A33.pdf` (SBVS186H) | ti.com/lit/ds/symlink/tps709.pdf |
| Q1 | AOS AO3400A | Pump low-side N-MOSFET | `AO3400A.pdf` | aosmd.com (via `hardware/altium/Pump_Controller/reference/`) |
| D1, D3 | Vishay SS34-E3/57T | D1 is the pump flyback diode; D3 is in series for reverse polarity | `SS34_vishay.pdf` (doc 88751) | vishay.com/docs/88751/ss32.pdf |
| J1 | Same Sky (CUI) PJ-102AH | 12 V DC input jack. 1 = centre, 2 = sleeve, 3 = switch | `PJ-102AH_cui_dc_jack.pdf` (10/30/2019) | cuidevices.com/product/resource/pj-102ah.pdf |
| C5 | Panasonic ECA-1CHG221B | 220 µF 16 V bulk electrolytic, NHG-A series, 6.3 mm, 5 mm pitch | `ECA-1CHG221B_panasonic_NHG-A.pdf` | industrial.panasonic.com/cdbs/www-data/pdf/RDF0000/ABA0000C1215.pdf |
| Y1 | Seiko SC-32S 32.768 kHz 7 pF ±20 ppm | nRF52840 LFXO crystal | `SC-32S_crystal.pdf` | copied from MSV4 |
| D2 | Bivar SM0805UBWC (schematic) | Status LED, blue 0805 | `SM0805UBWC_LED.pdf` | copied from MSV4 |
| D2 (as built) | Honglitronic HL-PSC-2012H203BC | The blue LED actually fitted, from MSV3/MSV4 stock | `HL-PSC-2012H203BC_blue_led.pdf` | LCSC C497961 |
| (off-board) | Triad WSU120-1000 | 12 V 1 A wall adapter, 5.5 x 2.1 mm, centre positive, <100 mW no-load | `WSU120-1000_triad_adapter.pdf` | catalog.triadmagnetics.com/Asset/WSU120-1000.pdf |
| M1 | Kamoer NKP 12 V DC (NKP-DC-S10B) | Peristaltic pump | **not obtained** | The only copy found was a reseller-hosted Kamoer PDF, whose host no longer resolved on 2026-11-18. The footprint's mechanical numbers come from the Kamoer catalogue drawing; see `hardware/altium/Kamoer_NKP/README.md` |

The 0805 resistors and MLCCs (`TB_R_0805_2012M` / `TB_C_0805_2012M`) carry their values in the
netlist, so they have no per-part datasheet here. The stock parts used for them are listed in
`DisplayCenter/procurement/PTB_Pump_Driver_stock_and_checklist_2boards_2026-11-18.md`.

The BA4AAPC battery holders are removed from this revision. Their drawing is still in
`hardware/altium/Pump_Controller/reference/BA4AAPC.pdf`.
