# MSV4 board 2 in a cup of water, then salt (2026-10-14)

Probe 11 (MSV4 board 2, R15 = 3.3 kohm) on the one-minute test image v262168. The maintainer
moved it through one cup of water at a time, same glass and same amount each time, with the blade
about 3/4 immersed:
- Brita-filtered fridge water 12:40-14:16
- tap water 14:16-14:39
- the Brita cup with one, two and then three pinches of salt (14:39, 14:57, 15:08)

Every move is recorded in `MSV3/fleet.json` (msv4_boards board 2 `active_test`). One reading taken
out of the water between moves (15:07:17) was deleted from the cloud at the maintainer's request:
`MSV4_SENSOR11_HISTORY_DELETION_2026-10-14.md`.

Figures and table: `MSV4/scripts/msv4_cup_test_plot.py`
- `data/sensor_logs/msv4_cuptest_timeline_2026-10-14.png`
- `data/sensor_logs/msv4_cuptest_salt_ladder_2026-10-14.png`
- `data/sensor_logs/msv4_cuptest_cm_vs_cd_2026-10-14.png`

## Measured: medians of each settled window

| Phase (window) | Readings | Tip | Rsoil | Cm | Cd | **Cm + Cd - air** | Rsoil behind mask | Soil capacitance (derived) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Air, desk (12:09-12:40) | 31 | 25.25 C | 85.4 kohm | 2.6 pF | 13.6 pF | 2.7 pF | 80 kohm | 0.1 pF |
| Brita (14:04-14:16) | 12 | 21.75 C | 720 ohm | 576 pF | 112.6 pF | 675 pF | 524 ohm | 116 pF |
| Tap (14:26-14:39) | 13 | 23.75 C | 583 ohm | 627 pF | 123.6 pF | **737 pF** | 422 ohm | 130 pF |
| 1 pinch (14:45-14:57) | 12 | 23.0 C | 232 ohm | 557 pF | 175.3 pF | **719 pF** | 139 ohm | 209 pF |
| 2 pinches (15:00-15:08) | 7 | 23.25 C | 205 ohm | 525 pF | 208.5 pF | **721 pF** | 109 ohm | 267 pF |
| 3 pinches (15:20-15:33) | 14 | 23.75 C | 197 ohm | 460 pF | 280.4 pF | **727 pF** | 79 ohm | 422 pF |

Relative to plain tap water (1 / 2 / 3 pinches):

| Reading | 1 pinch | 2 pinches | 3 pinches |
|---|---:|---:|---:|
| Cm + Cd - air | 98 % | 98 % | 99 % |
| Rsoil | 40 % | 35 % | 34 % |
| Rsoil behind the mask | 33 % | 26 % | 19 % |
| Cm | 89 % | 84 % | 73 % |
| Cd | 142 % | 169 % | 227 % |
| Soil capacitance (derived) | 161 % | 206 % | 325 % |

## What it shows

**Measured:**
- **The total coupling ignores salt.** Cm + Cd - air held at 719-727 pF across all three salt
  levels in the same cup (+/-1 %), while Cd more than doubled. Within a phase it scatters about
  +/-0.4 %, against +/-2.7 % for Cd alone: the fit pins the total down far better than its split.
- **Salt moves capacitance from Cm into Cd.** Cm and Cd trade almost exactly against each other,
  reading by reading and phase by phase.
- **Raw Rsoil flattens past one pinch** (232 -> 205 -> 197 ohm). Rsoil behind the mask keeps
  falling (139 -> 109 -> 79 ohm), so it is the better conductivity reading at this end.
- **The derived soil capacitance multiplied 3.25x with salt at constant water.** That is the
  metric that matched the pot's weighed water best (17-19 g RMS).
- **The total does move with how much of the blade is wetted:**
  - It rose from 544 to 675 pF over the first 95 minutes in the Brita while the surface wetted in.
  - It was 675 pF in the Brita with bubbles all over the blade, then 737 pF in tap water with
    none.

**Inferred:**
- **The coupling is set by the blade, not the water.** The soldermask's total coupling is fixed
  by its geometry and by how much of it is wetted. How the fitted model splits that coupling
  between its series branch (Cm, with Rsoil) and its parallel branch (Cd) depends on the water's
  conductivity. A readout that sees only one of the branches, or a single frequency, therefore
  sees conductivity as capacitance. That fits the MSV3 thermal mechanism already on record (soil
  conductivity reaching the FDC as apparent capacitance).
- **The pot result may depend on conductivity.** The derived soil capacitance may track the pot
  well partly because the soil's pore water conducts more as it wets. If so, a fertilized watering
  would read as far more water than it is. The soil-coupled capacitance would not be fooled that
  way, but in the pot it scored worse (44 g RMS). Which one to trust in soil is open.

## Not established, and why

- **The pinches were not measured** ("hard to tell"), so the response per gram of salt is not
  known. The phases differ by up to 2 C in tip temperature.
- **The tap water was in a different glass from the Brita and salt runs.** The Brita phase had
  bubbles and was still settling, so neither water phase is a clean zero-salt reference for the
  salted cup.
- **Water is not soil.** It is one board, with R15 = 3.3 kohm.

## Next: test better electrode models on raw sweeps (planned 2026-10-15)

**Done 2026-10-15:** results in `MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md`. The console image
went in as v262170, because 262169 was taken by the 10-14 ADC-fix image. A tap-water temperature
swing was added.

The fit's Cd should be only the board's own direct capacitance, but in tap water it read 124 pF
against 13.6 pF in air, and salt moved it further. Two causes are suspected (inferred):

- **The sweep runs out of range.** The water's characteristic frequency rose from ~0.5 MHz (tap)
  to ~2.8 MHz (3 pinches), past the 2.67 MHz top of the sweep, so the Cm/Cd split and R became
  unidentifiable at the salty end. The maintainer's point: no soil gets that wet or salty.
- **The model's shape.** A blade is not one resistor and one capacitor.

The cloud keeps only the fitted values, so the models are compared on raw sweeps.
`MSV4/scripts/msv4_raw_sweep_models.py` fits five models to each medium:
- the firmware's lumped model, with Cd free and with Cd fixed at the air value
- Cd fixed plus a constant-phase mask (CPE)
- Cd fixed plus an RC line
- two branches

It reports each model's residual on the averaged sweep against the averaged repeat noise, and the
mask capacitance by medium. Its self-test passes on synthetic sweeps.

**At realistic noise** (50e-4 of drive per sweep, about the probe's own residual in air), **15
sweeps per medium are enough.** With a mask whose CPE exponent is 0.96:
- the CPE fit sat at the noise (ratio 0.9 and 0.5) and kept the mask at 735.5 pF from tap to salt
- the lumped fits stayed at 1.7-2.2 times the noise

**A pure CPE probably does not explain the cup data on its own** (inferred from the same
synthetic runs):
- a CPE mask inflates the lumped Cd only to about 17-73 pF, and makes the lumped residual grow
  with salt
- the probe showed Cd at 124-284 pF, and a residual that FELL from 0.0042 (tap) to 0.0022 (salt)
- the two-branch model, or something not yet considered, may be needed. The raw sweeps decide.

**Capture, probe 11:**

1. **Build the console image** above the installed 262168:
   `powershell -ExecutionPolicy Bypass -File MSV4\scripts\arduino_msv4_probe.ps1 -Sketch arduino/MSV4_Probe_Bringup_BLE -BuildTag rawsweep-2026-10-15 -ApplicationVersion 262169 -Package -AcceptSharedMsv3DeviceType`
2. **Install it:** `msv4_ble_ota.py <zip> --board 2 --preflight-only`, then without
   `--preflight-only`. The updater accepts a console image for board 2. Cloud logging stops while
   it runs.
3. **Capture** with `msv4_ble_console.py --device MSV4-BU-6528 --cmd-timeout 90`, then redirect to
   a file:
   - **open:** 10 x `--cmd "spectrum open"` (blade dry, in air)
   - **air:** 15 x `--cmd "spectrum ab"` (sets Cd0)
   - **tap:** 15 x `spectrum ab` in plain tap water, same glass and depth, after Cm has settled
   - **salt1:** the same, after one pinch, stirred and settled
   - **soil (if possible):** the same, in a cup of moist soil
4. **Analyse:** `python MSV4/scripts/msv4_raw_sweep_models.py --rs 3300 --open open.txt air=air.txt tap=tap.txt salt1=salt1.txt [soil=soil.txt]`
5. **Restore production:** an image above 262169, built with `-SensorId 11 -RsOhms 3300 -CpCentiPf 874`.

The console's own printed fit assumes R15 = 1 kohm (the bring-up sketch has no -RsOhms). The raw
points do not depend on R15; the script fits them with --rs 3300.

## The temperature part did not work

- **Brita warm-up, 16.5 -> 21.75 C:** Cm rose and Rsoil fell about 2 %/C. The tap phase then
  showed the same Cm drift at constant temperature, so that is settling, not a temperature
  coefficient.
- **Warm bath on the 3-pinch cup, 15:33-15:43:** it overshot to 45.5 C, and Rsoil was already
  insensitive at that salt level (the maintainer's objection, agreed). Cm + Cd - air rose
  723 -> 833 pF while heating and stayed at 828-833 pF through 14.5 C of cooling to 31 C. That
  was a one-way change, not a reversible temperature effect.
  - **Candidates, none tested:** water taken up by the mask while hot, the salt finishing
    spreading, bubbles.
- **A fair redo needs:** a rinsed blade, plain tap water (ideally boiled and cooled to room
  temperature), and a gentle 5-10 C swing up and back down.
