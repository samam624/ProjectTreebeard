# MSV1 → MSV2 → MSV3: 30-Slide Research-Group Presentation

**Presenter:** Sam Amezcua  
**Audience:** Graduate students and faculty research group  
**Target length:** Approximately 25--30 min plus questions  
**Citation style:** IEEE numbered references; use the reference list at the end of this file.

> This is a build-ready slide outline, not a script that must be read verbatim. Keep each finished slide visually light: use the “On-slide content” as the visible material and move the detail in “Speaker notes” into PowerPoint notes.

## Slide 1 — Title

**Evolution of the MSV Soil-Moisture Platform**  
*From 555-Timer Sensing to a Connected FDC2214 Probe*

**On-slide content**

- Sam Amezcua, UC Davis ECE
- MSV1 proof of concept → MSV2 connected field platform → MSV3 solar-assisted probe
- August 2026 project status

**Visual:** A three-photo strip: MSV1 PCB/paper image, MSV2 field probe/receiver, and MSV3 PCB render.

**Speaker notes:** The talk is an engineering retrospective: what changed, which tests drove those changes, and what remains uncertain.

## Slide 2 — Why Soil-Moisture Sensing?

**On-slide content**

- Plants need a useful watering signal, not merely a raw electrical measurement.
- Low-cost resistive probes can corrode and polarize in wet soil.
- Capacitive electrodes reduce direct DC conduction.
- A practical product also needs low power, communication, calibration, and history.

**Visual:** One simple chain: `soil → capacitance → probe → receiver → useful decision`.

**Speaker notes:** Frame the project as a complete measurement system. Later failures occurred in every block, not only at the electrode.

## Slide 3 — Design Objectives

**On-slide content**

1. Repeatable moisture response across wet/dry cycles
2. Minimal long-term electrode polarization
3. Battery or harvested-energy operation
4. Wireless raw-data delivery and per-probe calibration
5. Recoverable local/cloud history

**Visual:** Five icons around a probe.

**Speaker notes:** Avoid claiming absolute volumetric water content. The current goal is a stable, plant-specific moisture indicator.

## Slide 4 — Current End-to-End System

**On-slide content**

- Probe: nRF52840-class MCU, capacitive front end, advertise-only BLE
- Receiver: ESP32-S3, local TFT interface, calibration, filtering
- Backend: Supabase history storage
- Client: progressive web application
- MSV2 and MSV3 use the same receiver/cloud family

**Visual:** System block diagram with MSV2/MSV3 probes entering the same receiver.

**Speaker notes:** MSV3 is a probe redesign, not a replacement for the complete receiver/application system.

## Slide 5 — Three Generations at a Glance

**On-slide content**

| Generation | Measurement principle | Output path | Main reason to advance |
|---|---|---|---|
| MSV1 | 555 frequency → analog voltage | LED threshold | Analog range compression |
| MSV2 | GPIO relaxation timing | BLE → receiver/cloud | Timing, supply, and polarization sensitivity |
| MSV3 | FDC2214 resonant conversion | Same receiver/cloud path | WIP: validate stability and harvested power |

**Visual:** Horizontal MSV1 → MSV2 → MSV3 arrow.

**Speaker notes:** This table is the story of the presentation. Each transition was driven by measured behavior rather than feature expansion alone.

## Slide 6 — MSV1 Architecture

**On-slide content**

- Interdigitated PCB comb as the sensing capacitor
- Astable 555: moisture-dependent frequency
- Monostable 555: fixed-width pulse
- RC low-pass filter: duty cycle to voltage
- Comparator + potentiometer + transistor: LED threshold [1]

**Visual:** `comb → astable 555 → monostable 555 → LPF → comparator → LED`.

**Speaker notes:** One comb is the timing/sense node and the other is grounded. MSV1 was self-contained and did not need a digital receiver.

## Slide 7 — MSV1 Experimental Results

**On-slide content**

- Estimated design range: approximately 80--220 pF
- Measured range: 274 pF dry air to approximately 1.25--1.28 nF saturated
- Potting soil: about 27 kHz dry → 7.5 kHz wettest
- Cactus soil: about 25 kHz dry → 7.7 kHz saturated [1]

**Visual:** Replot the MSV1 capacitance/frequency data as one clean line chart.

**Speaker notes:** The frequency response was strong. The failure appeared in the later frequency-to-voltage stage rather than in the sensing electrode itself.

## Slide 8 — Why MSV1 Was Not Enough

**On-slide content**

- Actual capacitance greatly exceeded the design estimate.
- Filter output compressed to roughly 1.07--1.10 V over much of the range.
- 100 kOhm hysteresis feedback caused comparator latching.
- Removing feedback stopped latching but produced near-threshold LED flicker.

**Visual:** Wide frequency range narrowing into a nearly flat voltage line.

**Speaker notes:** This motivated measuring timing/frequency digitally and postponing the moisture-percentage decision until calibration data were available.

## Slide 9 — Requirements Inherited by MSV2

**On-slide content**

- Preserve the useful frequency/capacitance relationship.
- Eliminate the saturated analog voltage conversion.
- Send raw measurements for adjustable calibration.
- Operate from a small rechargeable battery.
- Support long-term unattended observation.

**Visual:** MSV1 limitations on the left, MSV2 requirements on the right.

**Speaker notes:** MSV2 was not only a digital rewrite; it extended a benchtop circuit into an end-to-end monitoring system.

## Slide 10 — MSV2 System Architecture

**On-slide content**

- XIAO nRF52840 Sense probe
- Capacitive relaxation oscillator measured in firmware
- Non-connectable BLE advertisements
- ESP32-S3 wall receiver/display
- Per-probe calibration, local history queue, cloud upload, web UI

**Visual:** Detailed MSV2 signal/data flow diagram.

**Speaker notes:** The probe reports raw values. The receiver owns moisture mapping so calibration can change without reflashing probes.

## Slide 11 — Historical Single-Ended MSV2 Sensing

**On-slide content**

- One electrode driven; return/sense relationship remained fixed.
- Firmware charged, floated, and timed discharge behavior.
- Simple hardware, but sustained direction raised DC-bias/polarization concerns.
- In-soil behavior exposed conductivity and residual-charge effects.

**Visual:** Two-electrode sketch with a fixed drive arrow.

**Speaker notes:** Present this as an earlier MSV2 architecture, not as either of the two current field units.

## Slide 12 — Dual-Plate Alternating MSV2

**On-slide content**

- Alternate which physical electrode is driven and sensed.
- Measure both directions and combine the results.
- Reduce one-direction DC bias and expose electrode asymmetry.
- This switching requirement directly motivated the hardware TMUX in MSV3.

**Visual:** Phase A and Phase B electrode diagrams.

**Speaker notes:** The architecture is still a software-timed relaxation oscillator. “Dual plate” describes electrode use, not two independent sensors.

## Slide 13 — Current MSV2 Units: Same Architecture, Different R

**On-slide content**

| Field unit | Pulldown | Consequence |
|---|---:|---|
| Thaumatophyllum | 10 kOhm | Faster discharge; its own raw/calibration range |
| Alocasia | 56 kOhm | Slower discharge; a different raw/calibration range |

- Do not compare their raw numbers as though resistor values were identical.

**Visual:** Identical dual-plate diagrams with only the resistor highlighted.

**Speaker notes:** Older notes sometimes use “Scindapsus” for the Thaumatophyllum lineage. Normalize the final label or explain it once.

## Slide 14 — MSV2 Measurement Firmware

**On-slide content**

- Charge/float/discharge transitions create a relaxation oscillator.
- Count activity over a short gate and repeat in bursts.
- Median filtering rejects transient outliers.
- Advertise raw values; calibrate at the receiver.

**Visual:** Timing diagram showing charge, float, threshold crossing, repeat.

**Speaker notes:** An earlier one-shot RC approach failed: 10 kOhm was too fast for software timing, while much larger resistance saturated or became dominated by soil/residual-voltage effects.

## Slide 15 — MSV2 Power and BLE Strategy

**On-slide content**

- Approximately 10 s wake/check cadence
- Approximately 60 s BLE advertisement cadence
- Advertise-only packet; no persistent probe connection
- System-ON timed sleep between work periods
- Small rechargeable battery and onboard QSPI flash

**Visual:** One-minute duty-cycle timeline.

**Speaker notes:** Use “timed low-power/System-ON sleep,” not “deepest sleep” or System-OFF. The distinction matters when discussing power limits.

## Slide 16 — Receiver Responsibilities

**On-slide content**

- Scan continuously for probe advertisements.
- Decode probe identity and raw measurement.
- Apply per-probe calibration and filtering.
- Update TFT/local controls.
- Queue history and upload when network/TLS work is safe.

**Visual:** Receiver task lanes: BLE, display, calibration, history, network.

**Speaker notes:** BLE collection and HTTPS/TLS compete for time and memory on the same ESP32-S3; sequencing became a major reliability concern.

## Slide 17 — Phone and Cloud Path

**On-slide content**

- Supabase stores measurements, status, and history.
- Progressive web app displays current state and trends.
- Calibration can adapt per plant/probe.
- Lossless queuing and recovery protect readings during outages.
- Pagination and caching must be validated with multi-week data.

**Visual:** Receiver → Supabase → PWA, with a local recovery queue beside the receiver.

**Speaker notes:** The phone is not directly connected to the probe. This architectural detail explains why receiver firmware is central to both MSV2 and MSV3.

## Slide 18 — July 24: First System-Level Evidence

**On-slide content**

- BLE loss test: 198/200 packets received in 10 min
- Observed loss: 1.00%
- In-soil RC tests exposed timing-range problems.
- History/recovery work began as real data accumulated.

**Visual:** Large `198 / 200` result plus a small test timeline.

**Speaker notes:** Quote this as one observed test result, not a universal BLE packet-loss specification.

## Slide 19 — One-Shot RC Approach Failed

**On-slide content**

- 10 kOhm discharge was too fast for reliable software polling.
- Very large resistance extended timing but introduced saturation/residual-charge issues.
- Soil conductivity made the simple RC model incomplete.
- Decision: count a free-running relaxation oscillation instead.

**Visual:** One-shot waveform labeled “too fast” and “residual voltage,” then an oscillator waveform.

**Speaker notes:** This is the MSV2 equivalent of MSV1's range mismatch: the physical time constant did not fit the measurement mechanism.

## Slide 20 — July 25--31: Oscillator and RTC Quantization

**On-slide content**

- July 25: free-running oscillator method became active.
- Six-day data set contained 10,917 readings.
- A 1 ms sampling gate aligned with the RTC tick and created comb-like plateaus.
- A 5 ms window reduced, but did not fundamentally remove, software-timing sensitivity.

**Visual:** Histogram/strip plot illustrating quantized bands.

**Speaker notes:** This finding helped justify a dedicated frequency/capacitance converter in MSV3.

## Slide 21 — MSV2 Timing and Supply Sensitivity

**On-slide content**

- GPIO polling depends on clock/tick granularity.
- Floating sense nodes are susceptible to noise and EMI.
- Oscillation response couples to supply voltage.
- Low battery could pin values; charging could cause jumps.
- Calibration cannot fully correct unstable acquisition physics.

**Visual:** Moisture, VDD, clock, and EMI arrows all entering the raw measurement.

**Speaker notes:** The receiver's calibration is useful only when the probe's raw metric remains repeatable under equivalent soil conditions.

## Slide 22 — August 9: Hidden QSPI Battery Drain

**On-slide content**

- Two 150 mAh probes reached 0% in about 4--6 days.
- Puya P25Q16H/QSPI remained active after initialization.
- Excess current was approximately 1.3--1.5 mA.
- Explicit deep-power-down/uninitialization corrected both probes.
- JEDEC check: awake `0x856015`, asleep `0x000000`.

**Visual:** Before/after current bars.

**Speaker notes:** This is a strong lesson: a peripheral that is unused by the application can still dominate the power budget if the board support package initializes it.

## Slide 23 — Receiver and Interface Bugs

**On-slide content**

- Blocking TLS uploads interfered with BLE collection.
- Touch-controller traffic could corrupt shared SPI/display state.
- Stale service-worker assets obscured whether fixes were active.
- History required lossless batching, recovery, and unbiased filtering.
- Pagination failures appeared only at longer time ranges.

**Visual:** Bug-to-fix matrix with BLE, display, cache, and history rows.

**Speaker notes:** Separate the August 25 30 s overlap/in-flight infinite-reload bug from the later Supabase 1000-row-cap pagination work. They are distinct defects.

## Slide 24 — Why MSV3?

**On-slide content**

- Replace short-window GPIO polling with dedicated 28-bit conversion.
- Implement electrode swapping in hardware.
- Add reference/diagnostic routing states.
- Reduce measurement dependence on MCU scheduling.
- Pursue solar-assisted long-duration operation.

**Visual:** MSV2 limitations mapped to MSV3 design choices.

**Speaker notes:** MSV3 is not yet proven better in soil. It is designed to remove known MSV2 mechanisms, and the remaining slides distinguish verified bring-up from pending field validation.

## Slide 25 — MSV3 Hardware Architecture

**On-slide content**

- E73-2G4M08S1C / nRF52840 MCU module
- FDC2214 capacitance-to-digital converter
- TMUX1109 dual 4:1 analog multiplexer
- BQ25504 solar harvester + BQ25170 charger
- TPS7A0233 3.3 V LDO and USB/battery source selection

**Visual:** Use the MSV3 PCB isometric render with five numbered callouts.

**Suggested asset:** [`hardware/MSV3_Probe_PCB_Enclosure/renders/pcb_isometric.png`](../hardware/MSV3_Probe_PCB_Enclosure/renders/pcb_isometric.png)

**Speaker notes:** The same BLE receiver/cloud family remains downstream, reducing system-level migration risk.

## Slide 26 — FDC2214 Resonant Measurement

**On-slide content**

- CH0 LC tank: 6.8 uH + 270 pF fixed components
- Electrode capacitance shifts resonant frequency.
- FDC2214 reports a 28-bit digital count [2].
- Live compatibility mapping: `(24,000,000 - count) / 100`, clamped to 16 bits
- Example: 20,243,803 counts → 37,561 transmitted raw value

**Visual:** LC tank schematic and the conversion chain.

**Speaker notes:** The 16-bit compatible value lets the existing calibration path continue while the packet also carries higher-resolution information.

## Slide 27 — TMUX1109: Two Electrodes, Four States

**On-slide content**

1. Capacitance phase A: electrode roles in one direction
2. Capacitance phase B: electrode roles swapped
3. Auxiliary EC routing state
4. Bare-tank/reference routing state

- Hardware switching generalizes the dual-plate idea—but only for the two designed electrodes and four defined states.

**Visual:** Four small routing diagrams or a compact state table.

**Speaker notes:** Do not label this as an arbitrary N-channel array. Graded EC is not operational because DIV_NODE was routed to non-ADC-capable P0.00.

## Slide 28 — MSV3 Bring-Up Results

**On-slide content**

- Aug. 20: I2C, FDC2214, mux, and battery ADC bring-up
- Aug. 21: LFCLK/RTC1 freeze root-caused and repaired
- Verification: 400 + 500 = 900 consecutive samples
- Sleep: 60--71 uA → 10--15 uA after GPIO disconnects
- Aug. 22: first production probe/receiver observed live; 17-byte packet
- Direct sun: 6 V open circuit, 30 mA short circuit

**Visual:** Bring-up timeline with green checks.

**Speaker notes:** The board has no 32.768 kHz crystal. Selecting LFRC before FreeRTOS/SoftDevice initialization fixed freezes seen after 115--122 samples. Solar charging of roughly 10 mA was inferred, not directly measured.

## Slide 29 — Current Status and Open Work

**On-slide content**

**Verified or implemented**

- FDC/mux basic operation, BLE compatibility, LFRC startup, low-current timed sleep
- 17-byte high-resolution packet in the observed production path

**Still open**

- Wet-soil calibration and multi-day drift
- IDRIVE/ring-down tuning
- Overcast duty-cycle and low-battery solar-recovery tests
- EC ADC bodge/next PCB revision
- 23-byte temperature/bare-tank diagnostics: compile-tested, not confirmed deployed

**Visual:** Two-column “verified / open” status board.

**Speaker notes:** Die temperature is electronics temperature, not soil temperature. Verify the receiver `/device` build string immediately before presenting.

## Slide 30 — Takeaways and Discussion

**On-slide content**

- MSV1 proved the capacitive frequency response but compressed it in analog conversion.
- MSV2 delivered the connected platform and revealed clock, power, electrode, and cloud-system interactions.
- MSV3 replaces software timing with a dedicated converter and adds hardware routing plus harvested power.
- The next success criterion is stable multi-day soil data per unit—not a single bench reading.

**Visual:** Return to the three-generation roadmap, with an arrow toward “field validation.”

**Speaker notes:** Invite discussion on calibration protocol, reference channels, soil-test design, and energy-neutral acceptance criteria.

---

## Optional Backup Slides (Not Included in the 30-Slide Count)

### Backup A — Build/Deployment Timeline

Use the exact table in [`MSV_BUILD_MILESTONE_FACT_CHECK.md`](MSV_BUILD_MILESTONE_FACT_CHECK.md). Keep “source,” “compiled,” “flashed,” and “observed live” as different statuses.

### Backup B — Acceptance Test Matrix

| Test | Independent variable | Measure | Minimum useful result |
|---|---|---|---|
| Wet/dry cycle | Water content/time | FDC raw, reference channel, temperature | Repeatable direction and bounded hysteresis |
| Supply sensitivity | Battery voltage/charging state | Raw FDC value | No material mapping shift after compensation |
| Drift | Constant soil condition over days | Raw and calibrated values | Drift characterized with confidence interval |
| Energy | Indoor/overcast/direct sun | harvested current, VBAT, duty cycle | No net loss under the stated lighting profile |
| Recovery | Battery below reconnect threshold | startup sequence and time | Defined, repeatable reprime behavior |

### Backup C — Version Comparison

| Attribute | MSV1 | MSV2 | MSV3 |
|---|---|---|---|
| Front end | 555/LPF/comparator | GPIO relaxation timing | FDC2214 LC tank |
| Electrodes | Single comb pair, fixed roles | Historical single-ended; later alternating dual plate | Two electrodes, TMUX four-state routing |
| MCU | None required for measurement | XIAO nRF52840 Sense | E73 nRF52840 module |
| Output | LED threshold | BLE + receiver/cloud | Same receiver/cloud, 17-byte packet |
| Power | AAA battery | Rechargeable battery, timed System-ON sleep | Solar harvester + charger + timed sleep |
| Main limitation | Analog saturation/flicker | Timing/supply sensitivity and integration defects | Field stability/energy performance not yet characterized |

## Visual Asset Shortlist

- MSV1 figures and plots: [`EEC284_Final_Paper (2).pdf`](../EEC284_Final_Paper%20%282%29.pdf)
- MSV3 PCB: [`hardware/MSV3_Probe_PCB_Enclosure/renders/pcb_isometric.png`](../hardware/MSV3_Probe_PCB_Enclosure/renders/pcb_isometric.png)
- MSV3 assembled solar enclosure: [`hardware/MSV3_Probe_PCB_Enclosure/renders/assembly_solar_isometric.png`](../hardware/MSV3_Probe_PCB_Enclosure/renders/assembly_solar_isometric.png)
- MSV3 exploded view: [`hardware/MSV3_Probe_PCB_Enclosure/renders/exploded_solar_isometric.png`](../hardware/MSV3_Probe_PCB_Enclosure/renders/exploded_solar_isometric.png)
- Receiver enclosure: [`hardware/MSV2_ESP32_DisplayCenter/V2/renders/desktop_deployed_v5_isometric.png`](../hardware/MSV2_ESP32_DisplayCenter/V2/renders/desktop_deployed_v5_isometric.png)

## IEEE-Style References

[1] S. Amezcua, “Design and Experimental Characterization of a Capacitive Soil Moisture Sensor Using a 555 Timer Architecture,” EEC 284 final paper, University of California, Davis, 2026.

[2] Texas Instruments, “FDC2212/FDC2214 Multi-Channel, High Resolution, Noise-Immune Capacitance-to-Digital Converter,” FDC2x1x data sheet, SNOSCZ5B, Oct. 2024.

[3] Nordic Semiconductor, “nRF52840 Product Specification,” ver. 1.1, 2019.

[4] Texas Instruments, “TMUX1108 8:1 and TMUX1109 Dual 4:1 Precision Multiplexers,” data sheet, SCDS406A, Feb. 2024.

[5] Texas Instruments, “BQ25504 Ultra Low-Power Boost Converter With Battery Management for Energy Harvester Applications,” data sheet, SLUSAH0G, Aug. 2023.

[6] MSV2/MSV3 Project, “Status Handoff,” internal engineering status document, Aug. 2026.

[7] MSV2/MSV3 Project, “Moisture Sensor Oscillator Report,” internal engineering report, Jul. 2026.

[8] MSV2/MSV3 Project, “MSV3 LFCLK/RTC1 Hang,” internal failure analysis, Aug. 21, 2026.

[9] MSV2/MSV3 Project, “MSV3 Production Firmware,” internal verification record, Aug. 22, 2026.

## Final Presentation Checks

- Verify the live receiver build string from `/device`; do not infer it from checked-in source.
- Keep Aug. 17 as a limited calendar-label deployment; the full adaptive-calibration rollout is Aug. 22.
- Keep the Aug. 25 overlap/in-flight reload fix separate from later 1000-row-cap pagination work.
- Do not claim the Aug. 26 `cloud-heap-guard-v1` source tag is live without verification.
- Do not present the 23-byte diagnostics packet or temperature/reference features as deployed.
- Label the approximately 10 mA solar battery-charge current as inferred; 6 V open circuit and 30 mA short circuit were measured in direct sun.
- Use “two electrodes/four states” for the TMUX1109 architecture, not “N channels.”
