# MSV Evolution — IEEE Paper Outline (2-page conference format)

Status: **outline/draft for the author to expand into full prose.** Every dated claim below is
sourced from a doc in `docs/` or from source comments; items without a confirmed source are marked
`[VERIFY]` and should be checked against a live `/device` query or the physical hardware before the
paper is submitted, since this was drafted in a read-only/no-network session.

---

## Title (draft)

> Evolution of the MSV Soil-Moisture Platform: From 555-Timer Sensing to a Connected FDC2214 Probe

## Author block

Sam Amezcua
Department of Electrical and Computer Engineering
University of California, Davis
Davis, California, USA
scamezcua@ucdavis.edu

*(No co-authors, per author instruction.)*

## Abstract (draft, ~150 words)

> This paper summarizes three generations of a capacitive soil-moisture monitoring platform. MSV1
> established the sensing principle with an interdigitated PCB probe, cascaded astable/monostable
> 555 timers, a low-pass filter, and a hysteresis comparator; its measured capacitance range
> (≈274 pF–1.28 nF) exceeded the ≈80–220 pF range the analog stage was tuned for, producing output
> saturation and comparator latching. MSV2 replaced the analog conversion chain with an nRF52840
> software relaxation oscillator, alternating dual-plate excitation, BLE-advertisement-only
> transport to a dedicated ESP32-S3 receiver, and a cloud-backed dashboard with adaptive
> per-plant calibration. Field deployment surfaced RTC-timing quantization, supply-coupling,
> polarization, and battery-drain failure modes, most of which were root-caused and fixed. MSV3
> replaces MCU-timed sensing with a dedicated FDC2214 LC-tank front end and a TMUX1109 electrode
> mux on a solar-harvesting custom PCB, while retaining the MSV2 receiver and cloud stack unchanged.
> Bench bring-up reduced sleep current to 10–15 µA and reached 900/900 clean acquisition cycles
> after correcting a low-frequency clock misconfiguration; several sensing and power-path items
> remain open.

**Index Terms**—capacitive sensing, soil moisture, Bluetooth Low Energy, FDC2214, nRF52840,
energy harvesting, embedded systems.

---

## I. Introduction (~100 words)

- Resistive soil-moisture probes drive continuous current through the soil, causing electrode
  corrosion and long-term drift; capacitive sensing avoids a continuous conductive path.
- Design objective across all three revisions: a low-power probe that reports moisture to a local
  receiver/display and an optional cloud dashboard.
- Contribution of this paper: lessons from three design iterations — MSV1 (analog proof of
  concept, covered only briefly; full detail in the original MSV1 paper [1]), MSV2 (first
  connected, deployed system), and MSV3 (dedicated capacitance-to-digital front end, work in
  progress).
- Thesis: improving the sensor required not just a better analog/RF front end, but power-aware
  firmware, preserved raw data end-to-end, real field testing, and reliable cloud transport — most
  of MSV2's hardest bugs were systems-integration failures, not sensing-principle failures.

---

## II. Architecture Evolution (~300 words)

**Suggested single figure — three-generation block diagram:**

```text
MSV1: comb electrodes -> astable 555 -> monostable 555 -> LPF -> comparator/transistor -> LED
MSV2: comb electrodes (alternating dual-plate) -> nRF52840 relaxation-oscillator timing
      -> BLE advertisement -> ESP32-S3 receiver -> TFT + local app -> Supabase + web dashboard
MSV3: comb electrodes <-> TMUX1109 <-> FDC2214 + LC tank (6.8uH / 270pF)
      -> E73 (nRF52840) BLE -> same MSV2 receiver + cloud stack
```

### A. MSV1 (brief — full treatment is the cited prior paper [1])
- Single interdigitated PCB comb; one plate driven by the astable 555 stage, the opposing plate
  grounded (single-ended excitation).
- Astable stage generates a moisture-dependent oscillation; monostable stage pulse-shapes it before
  an RC low-pass filter converts frequency to a DC level; a comparator with a 100 kΩ hysteresis
  resistor drives an LED watering indicator. AAA-battery powered, no wireless link.
- **Key failing:** the LPF/comparator stage was sized for a predicted ≈80–220 pF sensing range;
  measured capacitance ran ≈274 pF (dry) to ≈1.28 nF (saturated soil) — nearly an order of
  magnitude higher — so the output voltage saturated to <100 mV of usable swing across most of the
  moisture range. The 100 kΩ hysteresis feedback resistor was undersized for this range and had to
  be removed to un-latch the comparator, which then reintroduced ~10 minutes of threshold flicker
  near the switch point with no hysteresis at all.
- This result — not an inaccurate sensing principle, but an analog conversion stage that could not
  be resized without a full board respin — is the direct motivation for MSV2 measuring timing
  digitally instead of converting through an analog LPF.

### B. MSV2 (primary system; most paper space here)
- Seeed XIAO nRF52840 (Sense) probe: replaces the analog LPF with a software relaxation
  oscillator (drive pin high → float → poll until the RC network discharges through a pulldown →
  repeat), giving a direct digital period/frequency measurement.
- **Single-plate → dual-plate evolution, same underlying architecture:** the original MSV2 design
  drove one plate against a grounded opposing plate (single-ended, same electrical topology as
  MSV1's comb). The reworked design alternates which plate drives and which senses each cycle,
  averaging the net DC bias across the soil to reduce electrode polarization.
- **The two field units are not the same topology.** Thaumatophyllum runs the reworked dual-plate
  firmware (alternating drive/sense, 10 kΩ pulldown on each plate). Alocasia has not been migrated
  to that rework and still runs the original single-plate firmware (one driven/sensed plate against
  a grounded plate, 100 kΩ pulldown) — confirmed directly in
  `MSV2/arduino/MSV2_Moisture_BLE_Sleep_Alocasia/MSV2_Moisture_BLE_Sleep_Alocasia.ino`, whose own header
  comments state this explicitly. The two units therefore differ in both topology and pulldown
  value, which is why raw-to-percentage calibration is per-device rather than global — and why
  Alocasia is not evidence for dual-plate's anti-polarization benefit.
- Calibration and raw-value handling were moved to the receiver so the probe firmware could change
  without re-deriving calibration constants on-device.
- Transport: BLE advertisement broadcast only (no pairing/connection) from each probe; a dedicated
  ESP32-S3 receiver continuously scans, renders a local TFT display, buffers history, and forwards
  to a Supabase-backed cloud dashboard (Netlify).
- Adaptive per-plant calibration (auto-learned dry/wet bounds with a percent-of-range floor) removes
  the need for manual per-plant threshold entry.

### C. MSV3 (extensive; explicitly work-in-progress)
- Custom PCB around the E73-2G4M08S1C (nRF52840) module. The module has **no 32.768 kHz crystal**,
  so every build must select the internal LFRC clock before FreeRTOS/SoftDevice start — a hard,
  build-enforced constraint (`scripts/arduino_msv3_probe.ps1`), not a firmware nicety.
- Sensing moves off MCU-timed GPIO polling entirely: an FDC2214 capacitance-to-digital converter
  reads a dedicated LC tank (Coilcraft 6.8 µH inductor + 270 pF C0G/NP0 padding capacitor) at 28-bit
  resolution, eliminating the RTC-tick quantization and supply-coupling sensitivity that MSV2's
  software timing approach had.
- A TMUX1109 analog mux switches the sensing electrodes through **four phases**: phase 0/1
  alternate which electrode drives vs. senses (the same anti-polarization idea MSV2 pioneered in
  firmware, now done cleanly in hardware); phase 2/3 disconnect the electrodes entirely for an
  EC-check / bare-tank reference reading.
- Power: BQ25170 handles USB charging; a BQ25504 boost-converter energy harvester plus a
  self-latching VSTOR↔VBAT topology lets a small solar cell recharge the battery; TPS7A0233
  regulates the digital sensing chain (E73, TMUX1109, FDC2214) off a clean 3.3 V rail rather than
  raw battery.
- Reuses the MSV2 ESP32-S3 receiver, BLE identity scheme, Supabase backend, and dashboard
  unchanged — only the probe changed. The live wire packet grew from MSV2's 15 bytes to 17 bytes to
  carry the wider FDC reading; a further 17→23-byte widening (board-die-temperature and bare-tank
  reference channels) exists in source but **is not yet flashed to any field unit**.

---

## III. Tests, Failures, and Milestones (~300 words)

**Table I — Selected test/milestone results**

| Date | Test or milestone | Result |
|---|---|---|
| 2026-07-05 | Display reboot-screen artifact fix | Pixel art no longer stuck visible during reboot message; flashed same day |
| 2026-07-24 to 07-31 | MSV2 BLE reliability + oscillator rework | 198/200 wake cycles received (1.00% loss over 10 min, `scripts/esp32_packet_loss_test.py`); one-shot RC timing replaced with free-running oscillator + alternating dual-plate excitation |
| 2026-08-09 | MSV2 battery-drain investigation | Root cause: unsupported Puya QSPI flash left active by a silently-failing `flash.begin()`; fixed and reflashed to both field probes |
| 2026-08-21 | MSV2 calibration rollout | `watering-cycle-alerts-v1` (adaptive dry/wet-bound auto-calibration) flashed and confirmed live via `/device` |
| 2026-08-21 | MSV3 LFCLK/RTC1 bring-up | Prior freeze at 115–122 samples traced to LFXO fallback on a crystal-less board; fixed to 900/900 clean samples same day |
| 2026-08-21 | MSV3 sleep-current fix | Reduced from 65–71 µA to 10–15 µA (GPIO-disconnect gap on VBAT/CHG pins) |
| 2026-08-22 | MSV3 production firmware | First probe+receiver build (`msv3-hires-moisture-v2` / `lossless-history-batch-v1`) flashed and observed live end-to-end |
| 2026-08-22 (daylight) | MSV3 solar harvester test | V_OC ≈ 6 V, I_SC ≈ 30 mA measured; real charge current **inferred** (not directly measured) at ≥10 mA via a no-solder R_BAT(on) method |
| 2026-08-23 | Receiver cloud-reliability incident | `low-heap-upload-guard-v2` mistakenly raised the heap floor and blocked all uploads; ~2,186 queued readings were lost before `live-history-recovery-v2` restored ingestion the same day |
| 2026-08-24 | Receiver filtering fix | `centered-moisture-filter-v1` — corrected an asymmetric smoothing filter that had been biasing displayed moisture downward |
| 2026-08-25 | MobileAppPage History-tab fix | Client-side >14-day History range fixed (30-second auto-refresh timer had been restarting mid-pagination) |
| most recent commit `[VERIFY deployment]` | Server-side History pagination | Added pagination past Supabase's 1000-row cap plus a low-heap guard on **all** cloud calls; this is a **separate bug** from the 8/25 client-side fix above, not a duplicate |
| 2026-08-29 | MSV3 solar-recovery test | Fully-discharged unit flashed with production sleep-cycling firmware and a 300 ms mux-settle fallback fix, then unplugged to test solar recovery from empty — **in progress, result not yet known** |

**Architectural limitations vs. fixed implementation bugs (MSV2):**
- *Architectural:* RTC-tick-gated frequency quantization; supply-voltage/charging-state coupling
  into readings; dependence on GPIO threshold timing; electrode polarization without alternating
  drive (motivated MSV3's hardware mux).
- *Fixed bugs:* silently-failing QSPI flash init draining batteries in ~4–6 days; blocking
  cloud/TLS calls starving BLE and display timing; RAM history-queue overflow under sustained
  outages; an asymmetric presentation filter biasing displayed moisture low.

---

## IV. MSV3 Status and Remaining Work (~120 words, clearly labeled WIP)

- EC/conductivity sensing is **not operational**: the analog divider node was routed to P0.00, a
  pin with no ADC/comparator capability on this MCU. A bodge to P0.03 (AIN1) is identified but not
  yet performed; a PCB revision is needed for a real fix.
- No external 32.768 kHz crystal — every build must select LFRC before FreeRTOS/SoftDevice start;
  this is enforced by `scripts/arduino_msv3_probe.ps1`, not left to manual discipline.
- A multi-day post-insertion moisture rise is observed but **not yet explained**; a dated test plan
  (`docs/MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md`) explicitly avoids attributing it to the
  C17 tank capacitor without completing bare-tank and unpowered-in-soil controls first.
- IDRIVE is untuned for wet soil; ERR_AHW/ERR_ALW behavior under real wet load is unverified.
- Board-die-temperature and bare-tank reference telemetry are implemented and compile-verified
  (23-byte packet) but **not deployed** to any field unit.
- Solar recovery from a fully-discharged battery is an open test as of 2026-08-29.

---

## V. Conclusion (~70 words)

> MSV1 demonstrated that interdigitated capacitance tracks soil moisture but was limited by an
> analog conversion stage sized for the wrong capacitance range. MSV2 turned that principle into a
> complete wireless monitoring system and exposed integration failures — timing, power, and cloud
> reliability — that only appeared under real field deployment. MSV3 preserves the proven receiver
> and cloud architecture while replacing MCU-timed sensing with a dedicated LC-based front end;
> remaining work is primarily physical validation rather than architectural redesign.

---

## References (IEEE format, draft)

```text
[1] S. Amezcua, "Design and Experimental Characterization of a Capacitive Soil Moisture Sensor
    Using a 555 Timer Architecture," EEC 284 final paper, Dept. Elect. Comput. Eng., Univ.
    California, Davis, CA, USA, 2026.

[2] Texas Instruments, "FDC2x1x Multi-Channel, High-Resolution Capacitance-to-Digital Converter
    for Capacitive Sensing Applications," SNOSCZ5B, Oct. 2024.

[3] Nordic Semiconductor, "nRF52840 Product Specification," ver. 1.1, doc. 4413_417, Feb. 2019.

[4] Texas Instruments, "TMUX1109 5-V, +/-2.5-V, Low-Leakage-Current, 4:1, 2-Channel Precision
    Multiplexer," SCDS406A, Feb. 2024.

[5] Texas Instruments, "BQ25504 Ultra-Low-Power Boost Converter With Battery Management for
    Energy Harvester Applications," SLUSAH0G, Aug. 2023.
```

---

## Claims to avoid (author checklist before submission)

- Do not describe both current field units as dual-plate — Alocasia is still the original
  single-plate/100 kΩ firmware; only Thaumatophyllum runs the reworked dual-plate/10 kΩ firmware.
  Verified against `MSV2/arduino/MSV2_Moisture_BLE_Sleep_Alocasia/MSV2_Moisture_BLE_Sleep_Alocasia.ino`
  2026-08-29 (an earlier draft of this outline had this backwards, stating "56 kΩ dual-plate" for
  Alocasia — that number does not appear anywhere in the firmware or hardware source).
- Do not describe MSV3 EC/conductivity sensing as operational.
- Do not call MCU die temperature "soil temperature."
- Do not claim the 23-byte temperature/tank-reference packet is deployed — it is compile-verified
  only.
- Do not attribute the unresolved multi-day drift to C17 (or to temperature generally) without
  completing the controls in `docs/MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md`.
- Do not state the solar harvester's charge current as directly measured — it is inferred via the
  R_BAT(on) method, not a direct ammeter reading.
- Before presenting, confirm the receiver's actual current `/device` build tag rather than citing
  `cloud-heap-guard-v1` as "currently live" — the source has since changed without a tag bump.
