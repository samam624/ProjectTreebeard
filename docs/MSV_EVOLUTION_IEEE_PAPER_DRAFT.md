# Evolution of the MSV Soil-Moisture Platform: From 555-Timer Sensing to a Connected FDC2214 Probe

**Sam Amezcua**  
Department of Electrical and Computer Engineering  
University of California, Davis  
scamezcua@ucdavis.edu

> Working draft for an approximately two-page IEEE two-column paper. Bracketed editor notes identify material to replace with a figure, photograph, or final measured value. The deployment language in this draft distinguishes checked-in source, compiled firmware, flashed firmware, and observed live builds.

## Abstract

This paper summarizes three generations of a capacitive soil-moisture sensor. MSV1 used cascaded 555 timers but compressed the useful response because its analog stage was designed for 80--220 pF while measurements reached 274 pF--1.28 nF. MSV2 replaced analog conversion with digitally timed relaxation oscillation and added BLE, an ESP32-S3 receiver, cloud history, and plant-specific calibration. Field use exposed clock quantization, supply coupling, electrode-bias risk, flash leakage, and receiver/cloud defects. MSV3 is a work in progress using an FDC2214 resonant converter, a TMUX1109 to route two electrodes through four states, and solar harvesting. Bring-up verified a 17-byte packet, 900 consecutive post-repair samples, and 10--15 uA timed-sleep current. Wet-soil stability and energy-neutral operation remain open.

**Index Terms—** capacitive soil-moisture sensing, Bluetooth Low Energy, FDC2214, nRF52840, energy harvesting, wireless sensor node.

## I. Introduction

Capacitive electrodes reduce the corrosion and polarization risks of low-cost resistive probes. The MSV project progressed from an analog proof of concept (MSV1), through a connected nRF52840/ESP32-S3 platform (MSV2), to a dedicated capacitance-converter and solar probe (MSV3). The objective remained a repeatable, low-power moisture indicator with wireless history. This paper emphasizes the measured shortcomings that motivated each transition.

## II. Architecture Evolution

### A. MSV1: Analog Frequency-to-Voltage Measurement

MSV1 used an interdigitated PCB comb, astable and monostable 555 timers, an RC low-pass filter, and a comparator/LED alarm [1]. Potting-soil frequency changed from approximately 27 kHz dry to 7.5 kHz wet, while extracted capacitance reached 1.28 nF. However, the downstream voltage remained near 1.07--1.10 V because the RC chain assumed a much smaller range. A 100 kOhm feedback resistor caused comparator latching; removing it produced near-threshold flicker. Direct digital timing in MSV2 preserved the useful frequency response without this analog compression.

### B. MSV2: Connected Digital Relaxation Oscillator

The XIAO nRF52840 Sense probe counts repeated capacitive charge/discharge transitions, applies burst/median filtering, and broadcasts raw values in non-connectable BLE advertisements. Early MSV2 sensing fixed the drive direction; the later dual-plate method swaps the driven and sensed electrodes to reduce sustained DC bias. Current Thaumatophyllum and Alocasia units use this same architecture but 10 kOhm and 56 kOhm pulldowns, respectively, so their raw ranges require separate calibration.

An ESP32-S3 receiver performs calibration, display, history recovery, and cloud upload; a progressive web application presents the stored data. The probe uses timed System-ON sleep, approximately 10 s wake/check intervals, and approximately 60 s advertisements.

### C. MSV3: Resonant Conversion, Electrode Multiplexing, and Solar Power

MSV3 replaces GPIO timing with an FDC2214 28-bit converter and a 6.8 uH/270 pF LC tank [2]. A TMUX1109 routes two electrodes through four states: two swapped capacitance phases and two auxiliary EC/bare-tank phases [4]. The E73 nRF52840 PCB lacks a 32.768 kHz crystal, requiring internal LFRC selection before the RTOS/SoftDevice start [3]. Its power path uses a BQ25504 harvester, BQ25170 charger, and TPS7A0233 regulator [5]. MSV3 preserves the MSV2 receiver/cloud path; its observed packet is 17 bytes. A 23-byte die-temperature/bare-tank format is compile-tested but not confirmed deployed.

> **Suggested Fig. 1.** A three-column block diagram: MSV1 comb -> 555/LPF/comparator; MSV2 dual electrodes -> GPIO relaxation timing -> BLE; MSV3 dual electrodes -> TMUX1109/FDC2214 -> BLE, with the common ESP32 receiver/cloud path shown beneath MSV2 and MSV3.

## III. Tests, Milestones, and Corrective Work

The main verified events are summarized in Table I. Dates indicate the day documented in the repository; they do not imply second-level deployment accuracy.

**TABLE I — SELECTED VERIFIED PROJECT EVENTS**

| Date (2026) | Test, milestone, or correction | Result or significance |
|---|---|---|
| Jul. 24 | BLE loss test | 198 of 200 advertisements received in 10 min; 1.00% observed loss. |
| Jul. 24--25 | In-soil RC and oscillator tests | One-shot RC timing was impractical; free-running relaxation frequency became the MSV2 method. |
| Jul. 31 | RTC timing analysis | A 1 ms gate produced quantized plateaus; widening the window to 5 ms reduced the artifact. |
| Aug. 9 | Probe power investigation | QSPI flash was left active at roughly 1.3--1.5 mA; deep-power-down/uninitialization restored expected behavior on both probes. |
| Aug. 12--14 | Receiver/UI stabilization and broad review | Touch/SPI, animation, BLE/cloud coexistence, caching, and numerous defensive defects were corrected. |
| Aug. 21 | MSV3 LFCLK and sleep-current repair | LFRC-at-startup eliminated the 115--122-sample freeze; 400- and 500-sample runs completed. Sleep current fell from 60--71 uA to 10--15 uA. |
| Aug. 22 | MSV3 production and system rollout | First MSV3 production probe/receiver were observed live; high-resolution packets, lossless history batching, receiver smoothing, watering alerts, and the full adaptive-calibration rollout were documented. |
| Aug. 23--25 | History recovery/filtering/pagination work | Live/history consistency, centered filtering, and an infinite-reload pagination defect were corrected in successive receiver builds. |

MSV2's short gate was quantized by the RTC tick, while supply voltage also shifted the oscillator. More critically, QSPI flash remained awake at roughly 1.3--1.5 mA and exhausted 150 mAh batteries in four to six days. Receiver testing found BLE/TLS contention, touch/SPI corruption, stale cached assets, and long-history pagination defects. These are system-integration failures rather than sensing-only failures.

MSV3 initially froze after 115--122 samples because post-startup LFCLK reconfiguration corrupted RTC1. Variant-time LFRC selection produced 900 consecutive samples. Disconnecting divider/status pins reduced timed-sleep current from 60--71 uA to 10--15 uA. Direct sun produced 6 V open circuit and 30 mA short circuit; roughly 10 mA battery charging was inferred rather than directly measured.

## IV. Limitations and Next Work

MSV3 still requires wet-soil calibration, FDC2214 drive/ring-down tuning, multi-day drift measurement, and overcast/low-battery solar tests. Graded conductivity is blocked because DIV_NODE is routed to non-ADC-capable P0.00; the board requires a bodge to an analog pin and a later PCB revision. Die temperature must not be labeled soil temperature. The current receiver build must also be verified through `/device`; checked-in source does not prove what is flashed.

## V. Conclusion

MSV evolved from an analog proof of concept to a connected field platform and then a dedicated, solar-assisted probe. Its central lesson is that electrode physics, clocking, power, packets, receiver recovery, calibration, and history must be validated together. MSV3's value will be established by long-duration soil and energy testing.

## References

[1] S. Amezcua, “Design and Experimental Characterization of a Capacitive Soil Moisture Sensor Using a 555 Timer Architecture,” EEC 284 final paper, University of California, Davis, 2026.

[2] Texas Instruments, “FDC2212/FDC2214 Multi-Channel, High Resolution, Noise-Immune Capacitance-to-Digital Converter,” FDC2x1x data sheet, SNOSCZ5B, Oct. 2024.

[3] Nordic Semiconductor, “nRF52840 Product Specification,” ver. 1.1, 2019.

[4] Texas Instruments, “TMUX1108 8:1 and TMUX1109 Dual 4:1 Precision Multiplexers,” data sheet, SCDS406A, Feb. 2024.

[5] Texas Instruments, “BQ25504 Ultra Low-Power Boost Converter With Battery Management for Energy Harvester Applications,” data sheet, SLUSAH0G, Aug. 2023.

[6] MSV2/MSV3 Project, “Status Handoff,” internal engineering status document, Aug. 2026.

[7] MSV2/MSV3 Project, “Moisture Sensor Oscillator Report,” internal engineering report, Jul. 2026.

[8] MSV2/MSV3 Project, “MSV3 LFCLK/RTC1 Hang,” internal failure analysis, Aug. 21, 2026.

[9] MSV2/MSV3 Project, “MSV3 Production Firmware,” internal verification record, Aug. 22, 2026.

## Final Editing Checklist

- Place the text in the official IEEE conference template and make the final spacing/figure adjustments required to hold two pages.
- Replace uA, uH, kOhm, arrows, and ranges with proper LaTeX/IEEE symbols if typesetting in LaTeX.
- Add one architecture figure and, only if space permits, one compact milestone/comparison table.
- Verify the receiver's live `/device` build string immediately before presenting.
- Confirm whether the plant should be labeled “Thaumatophyllum” throughout or whether “Scindapsus” must remain in historical screenshots.
- Do not describe the 23-byte diagnostic packet, temperature/reference channels, Aug. 26 source tag, or later history pagination work as deployed without new evidence.
