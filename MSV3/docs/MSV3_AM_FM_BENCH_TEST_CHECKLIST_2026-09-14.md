# Bench checklist: v1.2 front end on an MSV3 electrode

**Build date:** 2026-09-14
**Goal:** Decide whether the v1.2's thermal advantage comes from its **modulation scheme** or from its
**electrode geometry**, by running a known-good amplitude front end on an MSV3 electrode and standing
it in the shared pot next to the FDC.

Background and the analysis this came from:
[MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md](MSV3_AM_VS_FM_SENSING_THERMAL_SELECTIVITY_2026-09-13.md).
This page is meant to stand alone at the bench. You should not need to read that one to do this.

## Why this build and not a breadboard replica

A v1.2 module is one PCB: oscillator, detector and blade, joined by a narrow neck. Cut the neck and
the top section is a complete, known-good, fixed-frequency amplitude front end. Wire it to an MSV3
electrode and modulation scheme is the **only** thing that differs from sensor 8 beside it.

A hand-built replica would add "did I build it right" as a second variable. There are 10 spare v1.2
modules and 5 bare MSV3 probe PCBs, so the transplant costs nothing.

## What you need

- [ ] 1 spare v1.2 module (of 10)
- [ ] 1 bare MSV3 probe PCB (of 5)
- [ ] Spare ESP32 (note the pin caveat in step 4)
- [ ] Craft knife, soldering iron, 5 jumper wires, multimeter

**No components required.** No CMOS switch, no diode, no capacitor. The v1.2 already contains all of it.

## Step 1 — Prepare the donor

- [ ] Find the narrow neck where two traces run from the electronics down to the blade.
- [ ] **Cut both traces.** If you skip this, the v1.2's own blade stays in parallel with the MSV3
      electrode and swamps the comparison. This is the one step that is not just jumpers.
- [ ] Confirm with a multimeter that the blade is now isolated from the top section.
- [ ] Identify the two stubs on the electronics side. **Continuity to the header's GND pin** tells you
      which is ground. The other is the driven node.
- [ ] Write down which is which. Swapping them reads a flat line and costs you a day.

## Step 2 — Wire to the MSV3 electrode

On a bare board with no mux populated, `MS_NODE_A` and `MS_NODE_B` go straight to the electrode combs.

- [ ] driven stub → `MS_NODE_A`
- [ ] header GND → `MS_NODE_B`
- [ ] **Keep both wires under 5 cm.** 30 cm adds roughly 30 pF against an 80 pF electrode, which
      recreates the exact dilution problem this experiment exists to study.

## Step 3 — Consider sealing the routed edge

Bare PCBs have unsealed routed FR-4 edges. That is how water got into probe 4.

- [ ] Decide: seal the perimeter with epoxy tonight so it cures, or accept the risk for a 5-day run.
- [ ] **Do not use nail polish.** That is what trapped liquid pockets against probe 4.
- [ ] If unsealed, know the signature: a monotonic one-way ramp that sensor 8 beside it does not share
      is ingress, not soil.

## Step 4 — Wire to the ESP32

Same as the existing sensor 3 rig.

| from | to |
|---|---|
| header VCC | ESP32 3V3 (**not** 5V/VIN) |
| header GND | ESP32 GND |
| header AOUT | ESP32 ADC pin |

- [ ] **Pin caveat.** On an **ESP32-S3**, GPIO4 works as-is (ADC1_CH3). On a **classic ESP32**, move to
      GPIO32-39. GPIO4 there is ADC2 and the radio will break it.

## Step 5 — Firmware

- [ ] Clone `MSV2/arduino/MSV2_Moisture_AFE_Test/MSV2_Moisture_AFE_Test.ino`.
- [ ] Change `SENSOR_ID` to an unused number. In use: 2, 3, 4, 5, 8.
- [ ] Change nothing else. It already advertises in MSV2 manufacturer-data format, the receiver ingests
      it, and it lands in `msv2_sensor_history`.
- [ ] Keep the 10 s measurement cadence adopted 2026-09-07, so duty cycle does not become a second
      variable.

## Step 6 — Bench validation BEFORE it goes in soil

Fifteen minutes here saves discovering a dead rig four days later.

| test | expected |
|---|---|
| electrode in air | high reading, dry end |
| wrapped in a wet paper towel | clearly lower |
| dunked in tap water | lower still |
| salt water | lower again, or saturating |

- [ ] **Record the air and water numbers.** That is your span, and you will want it when interpreting
      four days of soil data that only moves a few percent.
- [ ] Nothing moves → the 555 is not running, or the neck cut took the wrong trace.
- [ ] Moves the wrong way → drive and ground are swapped.

## Step 7 — Deploy

- [ ] Into the **shared Alocasia pot**, alongside sensors 2, 3 and 8.
- [ ] Insert to the same depth as sensor 8, as closely as you can.
- [ ] Note the exact insertion time. Expect a settling transient; discount the first 24 h.
- [ ] **Do not water for 4-5 days.** A watering event is a 16-26 % swing and it destroys the window.
- [ ] Confirm all four sensors are landing rows in `msv2_sensor_history` before walking away.

## Step 8 — Analysis, after 4-5 clean days

Score it with the **per-day detrend** method. Do not use a single linear detrend across the whole
window; that error is what produced a wrong answer on 2026-09-13.

- [ ] Remove **each day's own** straight line, then correlate within that day.
- [ ] Drop any day whose swing exceeds ~5 %; that is a watering or handling event.
- [ ] Report mean `r`, mean slope in %/C, and mean daily swing across the clean days.
- [ ] Use probe 8's die temperature as the shared thermal reference.

### Reading the result

| observation | conclusion |
|---|---|
| transplant flat, sensor 8 swings | **modulation scheme**. V4 changes topology to fixed-frequency AM. |
| both swing together | **electrode geometry**. V4 keeps the readout, changes mask thickness and area. |
| transplant flat but its drying signal is tiny too | divider saturated. Nothing is proven; the v1.2's `R_s` is wrong for this electrode. |
| transplant ramps one way, sensor 8 does not | edge ingress, see step 3. Not a result. |

### Reference numbers to beat

From the same pot, 2026-09-09 to 09-13, per-day method:

| sensor | mean r vs temperature | slope %/C |
|---|---:|---:|
| v1.2 on its own blade (sensor 3) | +0.10 to +0.45 | +0.008 to +0.036 |
| MSV3 FDC (sensor 8) | +0.477 | +0.064 |
| MSV3 FDC (probe 4, own pot) | **-0.928** | **-0.387** |
| MSV3 bare tank (board only) | -0.994 | -0.041 |

If the transplant lands near the sensor 3 row, modulation scheme wins. If it lands near sensor 8,
geometry wins.
