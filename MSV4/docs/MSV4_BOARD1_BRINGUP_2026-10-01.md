# MSV4 board 1 bring-up — 2026-10-01

First MSV4 board built, flashed and brought up end to end. This is a dated record of what was
true on this date; it is not a description of the board's current state.

**Result:** every console-reachable item in the checklist's "Bring-up checks" stage passed after
three solder faults were reworked. Still open: scope timing of DRV/READ_CNTL (`phase 0/4/8/12`), a
real soil test (waiting on thermistor epoxy), and an MSV4-specific DFU device type.

| | |
|---|---|
| BLE name / app address | `MSV4-BU-AD95`, `F6:26:7D:76:AD:95` |
| Bootloader DFU address | `F6:26:7D:76:AD:96` (`AdaDFU`) |
| Bootloader | MSV3 1.0.3 (`--msv3 1.0.3`), MBR + S140 6.1.1 |
| App at end of session | `bringup-dietemp`, `app_version=262147` |
| Cell | 60 mAh, 4.05 V from the factory |

## Power-up

- All four unpowered short checks (VSTOR, 3V3, VBAT, SP) passed before first power.
- Bench supply on SP at **2.5 V / 10 mA** limit (the checklist's 50 mA also works). 3V3 and VSTOR
  came up.
- The cell was already at 4.05 V, above VBAT_OV (3.89 V), and the BQ25504 drew **no current** from
  the supply. That is the correct OV behaviour, but zero current is also what a dead charger looks
  like; charging up to 3.89 V and stopping there still needs to be shown with a cell under ~3.85 V.
- **Correction to earlier advice:** with MPPT at 77.9 % of VOC, the charger pulls a stiff bench
  supply into its current limit, so a supply sitting at its limit (around 1.9–2.0 V) is normal
  while charging. That alone does not indicate a short.

## Flashing — what worked, and the gotchas

1. Build: `MSV4\scripts\arduino_msv4_probe.ps1 -BuildTag first -Package -AcceptSharedMsv3DeviceType`
2. SWD, on the ESP32-S3 flasher (COM4): `python scripts\swd_flash_hex.py --port COM4 --msv3 1.0.3`.
   All segments written and verified.
3. **The board did not start after the flash.** The script's nRESET pulse and a tweezer short of
   RST to GND did nothing. Measured: the 1.0.3 hex writes only UICR `0x10001014` (the bootloader
   address) and no `PSELRESET`. The mass erase clears `PSELRESET`, so **the RST pad is not a reset
   pin until firmware configures it again** (inferred from the hex contents plus the observed
   non-response). The fix was `python scripts\swd_enter_dfu.py --port COM4`, which resets the core
   through SYSRESETREQ over SWD; the LED came up and `AdaDFU` advertised immediately. After the app
   had run once, `reset_reason=0x1` (pin reset) was seen, so the pad works again from then on.
4. App over BLE: `python scripts\ble_dfu_recover.py <zip> --device F6:26:7D:76:AD:96`. Many
   `PRN Timeout, continuing anyway` lines; the bootloader still validated and activated the image.
5. `dfu enter` from the app → reinstall `v262146` with no power cycle: **passed**. The app came back
   on LFXO with `reset_reason=0x4` (soft reset, not watchdog). This was the open question in the
   bring-up doc about the LFRC-bootloader to LFXO-app handoff with a running WDT.

**Python on this machine:** `MSV3\.arduino-build\python-portable\` does not exist here, so every
documented `MSV3\.arduino-build\python-portable\runtime\python.exe ...` command fails. Plain
`python` (3.12, with `bleak` and `pyserial`) runs all of them. In PowerShell, a relative path to an
exe also needs a leading `.\`.

## Solder faults found and reworked

All three came from hand-pasted fine-pitch parts with too much paste. None was visible from the
top.

| # | Symptom | Measured | Cause / fix |
|---|---|---|---|
| 1 | Suspected by eye | 0 Ω across R23 (READ_nOE to 3V3) | Bridge at U4 pins 8/9. Wicked. Caught before firmware ever drove READ_nOE low |
| 2 | `swap a/b`: SENSE **and DRV** fell from 820 to 53 mV mean | MS_A to MS_B 0 Ω | A/B short (U3 D+/D− side). Fixed |
| 3 | Same as 2 | 0 Ω across R15 (DRV to SENSE) | **Tombstoned part under U4**, not visible from the top. Reflowed; R15 now reads 1 kΩ |

How the diagnosis worked: with R15 bypassed **and** A shorted to B, U3 in A or B mode connects DRV
straight to GND through two switch on-resistances, so the hi-drive GPIO itself sags. Either fault
alone does not collapse DRV. **Rule of thumb from this:** if DRV_OUT moves with the swap state,
look for a SENSE–DRV short; electrode loading can only ever move SENSE.

Also note: with the board idle, DRV is held `OUT LOW`, so DRV reads ~133 Ω to GND on a meter
(the GPIO's pull-down transistor). That is not a fault; SENSE only reads the same if it is
shorted to DRV.

## Measurements (all measured on board 1)

| Check | Result |
|---|---|
| `info` | LFCLK XTAL running now, **RC at setup entry** (LFRC bootloader); start wait 253–376 ms |
| `vbat` vs meter | 3.990–3.992 V vs 3.99 V on a meter: **< 0.1 %** |
| `vdd` | 3316–3328 mV |
| `temp` | 21.5–24 °C at rest; 27.5 °C under a hand |
| `temp` DIE (v262147) | 29.0–29.75 °C at rest, ~6–8 °C above the tip NTC |
| `drive 16` | 1 MHz, DRV hi-drive under GPIOTE; `drive off` returns it low |
| 10× drive + BLE disconnect | All clean: `pins` safe, HFXO off, no reset, drive restarts |
| `sweep 16` (OPEN, air) | Clean triangle, peak at k = 8; A = 666 mV vs ideal 8/π²·825 = 669 mV; residual 57 mV ≈ triangle harmonics (theory ~53 mV) |
| `spectrum open` | **Cp = 8.8 pF** (±10 %), repeatable across runs |

`iq` at 1 MHz, electrode A, SENSE channel (DRV stayed 770–776 mV throughout):

| Medium | Amplitude | Phase | `spectrum ab` fit |
|---|---|---|---|
| OPEN reference | 784 mV | 178.3° | — |
| Air | 760 mV | 182.3° | Rsoil 10¹⁴–10¹⁷ Ω (no conduction), Cd 14–20 pF |
| Damp paper towel | 642 mV | 190.5° | Rsoil **5.9 kΩ** ±11.5 %, Cm 26.7 pF, Cd 29 pF |
| Hand grip | 372 mV | 221.5° | Rsoil **1.6–1.9 kΩ** ±17 %, Cm 51–55 pF, Cd 134–147 pF |

A and B agree to 0.4 % in air.

## What the sensing data is telling us

The firmware measures the divider `H(f) = SENSE/DRV` at eight frequencies (250 kHz – 2.67 MHz),
in both electrode orientations, and fits three physical parameters. Physics:
[MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md](MSV4_ELECTRODE_FIELD_SOLVE_2026-09-19.md) and
[MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md](MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md).

- **Rsoil (1/Gs)**: resistance of the ionic path through the medium between the plates. This is
  the moisture signal: lower means wetter. `Rsoil @25C` / `Rsoil25` is the same value corrected
  to 25 °C with the tip NTC at 2 %/°C. It measures **conductivity**, so wetter and saltier both
  push it down; turning it into volumetric water content needs a per-soil calibration, which has
  not been done.
- **Cm**: the solder mask over each plate in series. Physically fixed, so a change in Cm means a
  change in **contact**: air gaps, crusting, partial wetting. Rsoil rising with Cm steady means
  the medium dried; Rsoil rising with Cm collapsing means the blade lost contact.
- **Cd**: capacitance that never passes through the medium (board fringe, anything nearby). It
  should not track moisture in soil. A hand is a large conductive body wrapped around the board,
  which is why Cd jumped to ~140 pF in the hand test. Read that as the hand, not as a fault.
- **Cp**: stray capacitance on the SENSE node, from `spectrum open`. Stored and used by the fit.
- **Interface corner Gs/(2πCm)**: the frequency above which the masks become transparent. When
  it falls inside the sweep band (towel ~1.0 MHz, hand ~1.8 MHz), the fit is well conditioned.
- **The ± %** is the fit's own uncertainty. Values in the tens of % with a sane Rsoil are usable.
  Thousands to millions of % means the fit is degenerate, usually because there is no conductive
  path at all (air).

**In air the fit reports 10¹⁴–10¹⁷ Ω, or occasionally 0.0 Ω with a huge uncertainty.** Both mean
"no conduction". MSV4 Lab now shows these as `open (dry)` / `no fit` and charts Rsoil on a log
axis capped at 1 MΩ.

**Inferred, not yet measured:** the towel's Cm (26.7 pF) and the hand's (~53 pF) are far below the
~671 pF the field solve predicts in soil, which fits paper and skin only touching the plates in
patches. The first real soil test will establish board 1's actual Cm and Rsoil range.

## Firmware and tooling changes made in this session

- `MSV4_Probe_Bringup_BLE.ino`: `temp` appends `DIE=xx.xx C`, the nRF52840 die temperature read via
  `sd_temp_get` (0.25 °C steps, ±5 °C uncalibrated per Nordic). Installed as v262147.
- `msv4_lab.py`: parses `DIE=` as `die_c`.
- `lab.html` / `lab.js`: Chip temperature card and trend option; Rsoil shown as `open (dry)` /
  `no fit` and charted on a log axis capped at 1 MΩ.
- Build checklist: the `bu-lfclk` item now expects RC at setup entry.

## Open items

- [ ] Scope DRV and READ_CNTL at `phase 0/4/8/12` against the bring-up doc's timing table
- [ ] Epoxy the tip NTC, then run the soil test (`spectrum open`, `spectrum ab`, `moisture` in damp mix)
- [ ] Show the charger actually charging, then stopping at ~3.89 V, using a cell below 3.85 V
- [ ] MSV4 needs its own DFU device type before fleet use; today it shares MSV3's (`-AcceptSharedMsv3DeviceType`)
- [ ] Bootloader or app should restore `PSELRESET` so RST works straight after a mass erase, or the
      flash procedure should end with `swd_enter_dfu.py` instead of relying on the nRESET pulse
- [ ] Update the documented commands that point at the missing portable Python
