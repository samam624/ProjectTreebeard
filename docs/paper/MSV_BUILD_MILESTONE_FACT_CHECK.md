# MSV1/MSV2/MSV3 Fact-Check and Build/Milestone Reference

**Purpose:** Shared source of truth for the paper and presentation.  
**As-of date:** August 29, 2026  
**Scope:** Architecture, dated tests, build activation, major fixes, and wording that must remain qualified.

> This file deliberately distinguishes five evidence levels: **designed**, **source present**, **compiled/tested**, **flashed**, and **observed live/deployed**. A later source file does not prove a device was reflashed.

## 1. Reconciled Ground Truth

### MSV1

- Architecture: interdigitated capacitive comb → astable 555 → monostable 555 → RC low-pass filter → comparator/threshold → transistor/LED.
- Predicted capacitance range: approximately 80--220 pF.
- Measured capacitance range: 274 pF in dry air to approximately 1.25--1.28 nF saturated.
- Potting-soil frequency: approximately 27 kHz dry, 15 kHz at 1/3 cup, 10 kHz at 1/2 cup, and 7.5 kHz at 1 cup.
- Cactus-soil frequency: approximately 25 kHz dry to 7.7 kHz saturated.
- The monostable/LPF voltage was compressed near 1.07--1.10 V across much of the moisture range because the circuit was designed around an underestimated capacitance.
- A 100 kOhm comparator-feedback resistor caused latching and was removed. With no hysteresis, the indicator flickered around the threshold for approximately ten minutes.
- Correct conclusion: MSV1 validated the moisture-dependent frequency response but did not provide a robust analog output/threshold across the real capacitance range.

**Primary evidence:** [`EEC284_Final_Paper (2).pdf`](../../MSV1/EEC284_Final_Paper%20%282%29.pdf)

### MSV2

- Probe: XIAO nRF52840 Sense, software-timed relaxation oscillator, burst/median processing, advertise-only BLE.
- Receiver: ESP32-S3 display center performing packet collection, calibration, filtering, history recovery, and cloud upload.
- Client/backend: progressive web application and Supabase data/history path.
- Historical sensing progression:
  1. Simple/single-direction charge-discharge experiments.
  2. One-shot RC approach rejected in soil.
  3. Free-running relaxation-oscillator/frequency counting.
  4. Alternating dual-plate drive/sense to reduce one-direction DC bias.
- The two current MSV2 field units share the same alternating dual-plate architecture:
  - Thaumatophyllum: 10 kOhm pulldown.
  - Alocasia: 56 kOhm pulldown.
- The different pulldowns intentionally produce different raw ranges. Their calibrations are not directly portable.
- Some historical notes use “Scindapsus” for the Thaumatophyllum lineage. This is a naming inconsistency, not a third architecture.
- Power wording: MSV2 uses timed low-power **System-ON** sleep. Do not call it the deepest System-OFF mode.

**Primary evidence:** [`MOISTURE_SENSOR_OSCILLATOR_REPORT.md`](../../MSV2/docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md), [`MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md`](../../MSV2/docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md), and [`STATUS_HANDOFF.md`](../STATUS_HANDOFF.md)

### MSV3

- MCU/module: E73-2G4M08S1C using the nRF52840.
- Capacitance front end: FDC2214 CH0 and fixed 6.8 uH + 270 pF LC tank.
- Electrode routing: TMUX1109 dual 4:1, controlling **two physical electrodes through four defined states**.
- The four states are two electrode-swapped capacitance phases plus two auxiliary EC/bare-tank phases. This is not an arbitrary N-channel array.
- Power path: BQ25504 solar harvester, BQ25170 charger, TPS7A0233 regulator, battery/USB source selection.
- The PCB has no 32.768 kHz crystal. Safe firmware must select LFRC before FreeRTOS and the SoftDevice start.
- Normal behavior retains the MSV2-style approximately 10 s wake/check and approximately 60 s advertisement cadence.
- Packet status:
  - 15-byte packet: prior MSV2 format.
  - 17-byte packet: high-resolution MSV3 production path observed live.
  - 23-byte temperature/bare-tank packet: source/compile/cloud tested only; not confirmed flashed or deployed.
- Die temperature is MCU/electronics temperature, not soil temperature.
- EC design issue: DIV_NODE is routed to P0.00, which is not ADC capable. Graded EC requires a bodge to an analog-capable pin such as P0.03/AIN1 and a PCB correction in the next revision.

**Primary evidence:** [`MSV3_PROBE_BOM.md`](../../MSV3/docs/MSV3_PROBE_BOM.md), [`MOISTURE_SENSOR_FDC2212_FRONTEND.md`](../../MSV3/docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md), [`MSV3_LFCLK_RTC1_HANG_2026-08-21.md`](../../MSV3/docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md), and [`MSV3_PRODUCTION_FIRMWARE_2026-08-22.md`](../../MSV3/docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md)

## 2. Dated Test and Milestone Timeline

| Date (2026) | Event | Evidence level | Safe presentation wording |
|---|---|---|---|
| Jul. 2 | Current Git history begins | Repository boundary | “The available repository chronology begins July 2,” not “the project began July 2.” |
| Jul. 5 | `reboot-screen-fix` receiver build became active | Documented live | Receiver reboot-screen correction was live. |
| Jul. 7 | Receiver reflashed; security review identified 46 findings, including 6 critical | Documented flash/deployment | Critical/high-priority security corrections were deployed. Avoid implying all 46 were fixed that day. |
| Jul. 24 | BLE loss test: 198/200 packets in 10 min | Measured test | “1.00% observed loss in this test.” Do not generalize to all environments. |
| Jul. 24 | One-shot/in-soil RC and history/recovery work | Bench/system tests | Soil tests exposed the one-shot timing mismatch and drove oscillator/history work. |
| Jul. 25 | Free-running relaxation-oscillator method | Documented active | Oscillator/frequency counting replaced the one-shot RC method. |
| Jul. 27 | Dual-plate/raw-value work appears in Git | Source evidence | Say implemented/in source unless a separate flash record is shown. |
| Jul. 28 | Raw values and calibration moved toward the receiver | Documented system transition | Receiver-side calibration enabled per-probe mapping without probe reflashing. |
| Jul. 31 | RTC quantization root cause documented | Data analysis | 1 ms gate produced plateaus; 5 ms reduced the artifact. Data set: 10,917 readings across six days. |
| Aug. 9 | QSPI power fix applied to both probes | Documented live hardware fix | Roughly 1.3--1.5 mA unintended flash current was removed; both probes were corrected. |
| Aug. 11 | Netlify QR/PWA `app.v11`, backlight work, BLE/cloud contention diagnosis | Mixed deployment/diagnosis | Keep the deployment and diagnosis as separate claims. |
| Aug. 12 | Display touch/animation corrections | Documented implementation | Touch/SPI and animation corruption were corrected. |
| Aug. 13--14 | Broad adversarial repository review and remediation | Documented review/deployments | Approximately 35 corrections were applied across the system. The Alocasia probe correction was compiled but was not documented flashed at that time. |
| Aug. 17 | Narrow `lastWateredAt` calendar-label follow-up | Documented deployed follow-up | A limited label/date correction deployed. **The full adaptive-calibration redesign did not deploy on Aug. 17.** |
| Aug. 20 | MSV3 SWD and peripheral bring-up | Bench verified | I2C/FDC2214/mux/battery-ADC bring-up was confirmed. |
| Aug. 21 | MSV3 LFCLK/RTC1 repair | Bench verified | Variant-time LFRC selection fixed freezes at 115--122 samples; 400- and 500-sample runs completed. |
| Aug. 21 | MSV3 sleep-leak repair | Measured test | Disconnecting divider/status GPIOs reduced timed-sleep current from 60--71 uA to 10--15 uA. |
| Aug. 21--22 | Solar characterization | Measured plus inferred | Dim indoor: 2.7 V open circuit/50 uA short circuit. Direct sun: 6 V open circuit/30 mA short circuit. Approximately 10 mA battery charging was inferred, not directly measured. |
| Aug. 22 | Full adaptive-calibration rollout | Documented live | Use Aug. 22—not Aug. 17—as the full rollout date. |
| Aug. 22 | First MSV3 production probe/receiver observed live | Documented live | 17-byte high-resolution packet path was active. |
| Aug. 22 | `watering-cycle-alerts-v1` | Documented live | Build tag carries an Aug. 21 label but is documented in the Aug. 22 rollout grouping. |
| Aug. 22 | `receiver-moisture-smoothing-v1` | Documented live | Receiver moisture smoothing active. |
| Aug. 22 | `msv3-hires-moisture-v2` | Documented live | High-resolution MSV3 compatibility active. |
| Aug. 22 | `lossless-history-batch-v1` | Documented live | Lossless history batching active. Exact within-day tag ordering is not fully recorded. |
| Aug. 23 | Receiver recovery/heap/history build sequence | Multiple transient live builds | Builds progressed through reboot telemetry, heap guards, cleanup, history overlay, smoothing, and recovery. Do not present every intermediate build as a stable release. |
| Aug. 23 | `low-heap-upload-guard-v2` | Erroneous temporary build | It blocked 100% of uploads and was not left active. Use as a failure/correction example only. |
| Aug. 23, ~17:00 PDT | `live-history-recovery-v2` | Last build in that day's documented sequence | Safe wording: final documented receiver state for Aug. 23. |
| Aug. 24 | `centered-moisture-filter-v1` | Documented live | Last clearly tagged live build in the existing records. |
| Aug. 25 | History infinite-reload correction | Documented flashed, no unique tag | Fixed a 30 s overlap/in-flight pagination interaction. Do not equate this with the 1000-row cap. |
| Aug. 25 | 23-byte MSV3 temperature/bare-tank feature | Compiled/cloud-tested only | Do not claim deployed or flashed. |
| Aug. 26 | `cloud-heap-guard-v1` source identity | Source only; committed Aug. 28 | Checked-in receiver source reports this tag, but current live status is unverified. |
| Aug. 26--28 | Supabase 1000-row-cap pagination work | Source/documented work, live status unproven | Distinct from the Aug. 25 infinite-reload fix. Do not say it is live without `/device` and behavior verification. |
| Aug. 28 | Multi-day FDC drift follow-up | Open test plan | Characterization remains pending. |
| Aug. 29 | Claimed empty-battery solar-recovery release/test | Not supported by reviewed repository evidence | Exclude unless the user supplies a bench note or directly confirms the event. |

## 3. Receiver Build Sequence: Aug. 23 Detail

The documents record a rapid sequence rather than one clean release:

1. `reboot-telemetry-v1`
2. Heap-guard v1
3. `low-heap-upload-guard-v2` — erroneous; blocked uploads and was replaced
4. `cloud-cleanup-v3`
5. `history-raw-overlay-v1`
6. `retroactive-history-smoothing-v1`
7. `live-history-recovery-v2` — final documented state that day, approximately 17:00 PDT
8. Aug. 24: `centered-moisture-filter-v1` — last clearly tagged live build

**Presentation rule:** Use this sequence only if discussing iterative debugging. For a simple milestone slide, list `live-history-recovery-v2` and `centered-moisture-filter-v1` and mention that several temporary builds preceded them.

## 4. Quantitative Results Approved for Use

| Quantity | Approved value | Qualification |
|---|---:|---|
| MSV1 measured capacitance | 274 pF to approximately 1.25--1.28 nF | Dry air through saturated soil conditions in the MSV1 paper |
| MSV1 potting frequency | 27 kHz → 7.5 kHz | Dry to wettest reported condition |
| MSV1 cactus frequency | 25 kHz → 7.7 kHz | Dry to saturated |
| MSV1 compressed output | approximately 1.07--1.10 V | Much of the useful moisture range |
| BLE loss test | 198/200 received; 1.00% loss | One 10 min test on Jul. 24 |
| RTC analysis data | 10,917 readings over six days | Used to identify quantized plateaus |
| QSPI excess current | approximately 1.3--1.5 mA | Before flash power-down fix |
| Probe batteries | 150 mAh; depleted in approximately 4--6 days | During QSPI-leak episode |
| MSV3 pre-fix freeze | 115--122 samples | LFCLK/RTC1 corruption symptom |
| MSV3 post-fix samples | 400 + 500 = 900 consecutive | Two documented verification runs |
| MSV3 sleep current | 60--71 uA → 10--15 uA | Before/after GPIO disconnect correction |
| Indoor solar | 2.7 V open circuit; 50 uA short circuit | Dim indoor condition |
| Direct-sun solar | 6 V open circuit; 30 mA short circuit | Direct sunlight condition |
| Battery charge estimate | at least approximately 10 mA | Inferred from battery behavior, not directly measured |
| FDC example | 20,243,803 counts → 37,561 compatible raw | Using `(24,000,000 - count)/100` with 16-bit clamping |
| BLE packet sizes | 15 B MSV2; 17 B MSV3 live; 23 B diagnostic source | 23 B not confirmed flashed/deployed |

## 5. Claims That Require Verification Before Presenting

1. **Current live receiver build.** Query the receiver `/device` endpoint. Source identity alone is insufficient.
2. **Aug. 26 history/heap fixes active on hardware.** Confirm the receiver was reflashed after the source changes.
3. **23-byte diagnostic packet active.** Confirm probe firmware flash and receiver/cloud compatibility in the actual deployed path.
4. **Empty-battery solar-recovery test on Aug. 29.** Add a dated bench note or omit it.
5. **Long-duration MSV3 stability.** Wet soil, FDC drift, temperature dependence, overcast energy balance, and recharge recovery remain open.
6. **Current plant naming.** Decide whether historical “Scindapsus” screenshots should be relabeled or explained as the Thaumatophyllum lineage.

## 6. Common Wording Errors to Avoid

| Avoid | Use instead |
|---|---|
| “Full calibration redesign deployed Aug. 17” | “A narrow date-label fix deployed Aug. 17; the full adaptive-calibration rollout was Aug. 22.” |
| “MSV3 mux supports N channels” | “The TMUX1109 routes two electrodes through four defined states.” |
| “MSV2 uses deep/System-OFF sleep” | “MSV2 uses timed low-power System-ON sleep.” |
| “Solar charged the battery at 10 mA” | “Battery charging of roughly 10 mA was inferred; direct sun measured 6 V open circuit and 30 mA short circuit.” |
| “The Aug. 25 history bug was the Supabase 1000-row cap” | “Aug. 25 fixed an overlap/in-flight reload loop; later work addressed the 1000-row cap.” |
| “Temperature/reference diagnostics are deployed” | “The 23-byte diagnostics path is compile/cloud tested but not confirmed flashed.” |
| “The checked-in build tag is the live build” | “Checked-in source identifies a build; `/device` must verify the flashed receiver.” |
| “MSV3 measures soil temperature” | “MSV3 can report MCU die temperature for electronics diagnostics.” |
| “The two MSV2 probes have different architectures” | “Both use alternating dual plates; 10 kOhm versus 56 kOhm creates different raw ranges.” |

## 7. Relevant Internal Documentation

- Overall status: [`STATUS_HANDOFF.md`](../STATUS_HANDOFF.md)
- Roadmap/open work: [`PRODUCT_ROADMAP.md`](../PRODUCT_ROADMAP.md)
- Known issues: [`KNOWN_ISSUES.md`](../KNOWN_ISSUES.md)
- MSV2 oscillator: [`MOISTURE_SENSOR_OSCILLATOR_REPORT.md`](../../MSV2/docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md)
- Adaptive calibration: [`MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md`](../../MSV2/docs/MSV2_ADAPTIVE_MOISTURE_CALIBRATION.md)
- MSV3 FDC front end: [`MOISTURE_SENSOR_FDC2212_FRONTEND.md`](../../MSV3/docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md)
- MSV3 BOM: [`MSV3_PROBE_BOM.md`](../../MSV3/docs/MSV3_PROBE_BOM.md)
- MSV3 LFCLK failure analysis: [`MSV3_LFCLK_RTC1_HANG_2026-08-21.md`](../../MSV3/docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md)
- MSV3 bench plan: [`MSV3_BENCH_TEST_PLAN_2026-08-21.md`](../../MSV3/docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md)
- MSV3 production firmware: [`MSV3_PRODUCTION_FIRMWARE_2026-08-22.md`](../../MSV3/docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md)
- Temperature/tank-reference status: [`MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md`](../../MSV3/docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md)
- Multi-day drift follow-up: [`MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md`](../../MSV3/docs/MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md)
- Receiver smoothing: [`RECEIVER_MOISTURE_SMOOTHING_2026-08-22.md`](../../DisplayCenter/docs/RECEIVER_MOISTURE_SMOOTHING_2026-08-22.md)
- Lossless batching: [`RECEIVER_LOSSLESS_HISTORY_BATCHING_2026-08-22.md`](../../DisplayCenter/docs/RECEIVER_LOSSLESS_HISTORY_BATCHING_2026-08-22.md)
- Receiver cloud reliability: [`RECEIVER_CLOUD_RELIABILITY_2026-08-23.md`](../../DisplayCenter/docs/RECEIVER_CLOUD_RELIABILITY_2026-08-23.md)
- History smoothing/debounce: [`HISTORY_SMOOTHING_WATERING_DEBOUNCE_2026-08-23.md`](../../DisplayCenter/docs/HISTORY_SMOOTHING_WATERING_DEBOUNCE_2026-08-23.md)
- Retroactive history smoothing: [`HISTORY_RETROACTIVE_SMOOTHING_2026-08-23.md`](../../DisplayCenter/docs/HISTORY_RETROACTIVE_SMOOTHING_2026-08-23.md)

## 8. Cross-Document Consistency Checklist

- [x] Paper and slides use Aug. 22 for the full adaptive-calibration rollout.
- [x] Paper and slides describe TMUX1109 as two electrodes/four states.
- [x] Paper and slides distinguish the Aug. 25 reload loop from later 1000-row pagination.
- [x] Paper and slides call the approximately 10 mA solar current an inference.
- [x] Paper and slides distinguish 15-, 17-, and 23-byte packet status.
- [x] Paper and slides use System-ON timed sleep for MSV2.
- [x] Paper and slides identify Thaumatophyllum as 10 kOhm and Alocasia as 56 kOhm.
- [x] Paper and slides keep the current receiver build marked for verification.
- [x] Unsupported Aug. 29 solar-recovery claims are excluded from the narrative.
