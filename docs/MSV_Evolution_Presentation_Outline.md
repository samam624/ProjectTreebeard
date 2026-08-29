# MSV Evolution — Presentation Outline (~30 slides, research-group talk)

Status: **outline/speaker-notes draft**, informal tone (research group, not a formal defense).
`[VERIFY]` marks anything that should be confirmed against live hardware before you present it.

Suggested visuals referenced below (already exist in-repo):
- MSV1 figures: PCB layout, manufactured probe, and Fig. 12/17 response plots from
  `EEC284_Final_Paper (2).pdf`
- `hardware/MSV3_Probe_PCB_Enclosure/renders/assembly_solar_isometric.png`
- `hardware/MSV3_Probe_PCB_Enclosure/renders/exploded_solar_isometric.png`
- `hardware/MSV3_Probe_PCB_Enclosure/renders/pcb_isometric.png`
- `hardware/MSV2_ESP32_DisplayCenter/V2/renders/desktop_deployed_v5_isometric.png`

---

**1. Title**
Project name, your name, UC Davis, research group/date.

**2. The problem**
Plants get under/over-watered because moisture isn't visible; resistive probes corrode from
continuous soil current and drift over time.
*Speaker note:* frame this as "why not just buy a $5 resistive sensor" up front.

**3. Project objectives**
Low power, low corrosion (capacitive, not resistive), local display, remote history, easy setup,
eventually self-powered.

**4. Current system overview (one diagram)**
Probe → BLE advertisement → ESP32 receiver → TFT/local app → Supabase → Netlify dashboard.

**5. Three-generation roadmap**
MSV1 = analog proof of concept → MSV2 = connected platform → MSV3 = dedicated sensing front end
(still WIP). Set expectation: today is mostly MSV2 (done, deployed, lots of lessons) and MSV3
(exciting, unfinished).

**6. MSV1 architecture (brief — you already gave this talk)**
Comb electrodes → astable 555 → monostable 555 → LPF → comparator/transistor → LED.
*Speaker note:* 1 slide, don't relitigate the whole 555 paper.

**7. MSV1 result and the one lesson that matters**
Measured capacitance 274 pF (dry) to 1.28 nF (saturated) vs. the ~80–220 pF the analog stage was
designed around → output saturated, hysteresis resistor had to be removed, comparator flickered.
*This is the seed of every MSV2 design decision on this slide's right side: measure digitally,
don't convert through an analog stage sized for the wrong range.*

**8. Requirements MSV2 inherited from MSV1**
Keep frequency-domain sensing; move to digital measurement; add a wireless link; solve calibration
per-plant instead of per-fixed-threshold.

**9. MSV2 system architecture**
XIAO nRF52840 probe + ESP32-S3 receiver + display + app + cloud. One block diagram slide.

**10. MSV2 sensing: from analog to digital**
Relaxation oscillator in software: drive pin high → float → poll until the pulldown discharges the
node → repeat. No analog LPF stage, no fixed pulse generator — directly counts cycles.

**11. Single-plate vs. dual-plate MSV2**
Original: one driven/sensed plate against a grounded plate (same topology as MSV1's comb).
Rework: alternate which plate drives vs. senses each cycle to cancel net DC bias and reduce
electrode polarization. This dual-plate idea is what MSV3's hardware mux later formalizes.

**12. The two current MSV2 field units**
Thaumatophyllum (10 kΩ pulldown) and Alocasia (56 kΩ pulldown) — same dual-plate architecture,
different RC time constant, different raw frequency range. Explains why the two units' raw numbers
aren't directly comparable without per-device calibration.

**13. MSV2 firmware loop**
Charge/release/count cycles → median filter → send raw value (not a pre-baked percentage) to the
receiver, so calibration logic can change without reflashing every probe.

**14. MSV2 power and BLE strategy**
Sample locally on an interval; advertise (not connect) roughly every ~60 s or on a meaningful
change; deep-sleep between cycles.

**15. ESP32 receiver responsibilities**
Continuous BLE scanning, calibration/smoothing, TFT rendering, local history buffering, Wi-Fi,
HTTPS upload — deliberately the "expensive" node so probes stay cheap and low-power.

**16. Phone + cloud interface**
Local setup flow / QR claim, Supabase storage, Netlify web dashboard, History tab.

**17. Milestone: first real BLE reliability test (Jul 24–31)**
198/200 wake cycles received — 1.00% loss over 10 minutes. One-shot RC timing measurement proved
unstable in real soil and was replaced with the free-running oscillator + alternating electrodes.

**18. Bug story: why one-shot RC timing failed**
Small pulldowns discharged faster than software could poll; large pulldowns saturated; soil
conductivity added instability the lab-bench version never showed. *This is a good "field testing
beats simulation" story slide.*

**19. Bug story: RTC-tick quantization**
Software timing was gated by RTC granularity, producing discrete plateaus in moisture readings
instead of a smooth curve — an architectural limitation of MCU-timed sensing that MSV3's dedicated
FDC chip sidesteps entirely.

**20. Bug story: the QSPI battery drain (Aug 9)**
Root cause: the XIAO's onboard flash chip wasn't in the flash library's supported-device table, so
`flash.begin()` failed silently and the chip never went to deep power-down — batteries died in
~4–6 days instead of lasting for weeks. Fixed and reflashed to both field probes.
*Good "silent failure" cautionary-tale slide.*

**21. Bug story: cloud/receiver reliability (Aug 22–25)**
Three distinct incidents, don't conflate them: (a) a heap-guard mistake that over-tightened and
blocked all uploads, losing ~2,186 queued readings before same-day recovery; (b) an asymmetric
smoothing filter biasing displayed moisture downward, fixed Aug 24; (c) a client-side History-tab
bug where the 30-second auto-refresh timer restarted mid-pagination on >14-day ranges, fixed Aug
25 — separate from a later server-side fix for Supabase's 1000-row pagination cap `[VERIFY current
deployment status of the server-side fix]`.

**22. MSV2 calibration redesign (Aug 21)**
Adaptive per-plant dry/wet-bound auto-calibration with a percent-of-range floor — removes manual
threshold entry, confirmed live via `/device` the same day (`watering-cycle-alerts-v1`).

**23. MSV2 limitations, summarized**
RTC-gated quantization; supply/charging-state coupling; electrode polarization risk; per-unit
calibration required (10k vs 56k pulldowns aren't interchangeable); several integration bugs found
only through weeks of real field data, not bench testing.

**24. Why build MSV3 at all?**
Move sensing off MCU timing entirely; get a real capacitance-to-digital chip instead of a
GPIO-timed approximation; formalize electrode alternation in hardware; add self-powering via solar.

**25. MSV3 hardware architecture**
Custom PCB: E73-2G4M08S1C (nRF52840, no crystal), FDC2214, TMUX1109, BQ25504 harvester, BQ25170
USB charger, TPS7A0233 LDO. Show the enclosure/assembly renders here.

**26. MSV3 sensing: the LC tank**
FDC2214 reads a 6.8 µH inductor + 270 pF tank capacitor; soil capacitance rides on top of that
fixed tank value; 28-bit resolution, no RTC gating.

**27. MSV3 mux: four phases**
TMUX1109 phases 0/1 alternate electrode drive/sense (MSV2's dual-plate idea, now in hardware);
phases 2/3 disconnect the electrodes for an EC-check / bare-tank reference reading.
*Note EC-check is not yet functional — see slide 29.*

**28. MSV3 bring-up results**
LFCLK/RTC1 hang root-caused and fixed (900/900 clean samples, was freezing at 115–122); sleep
current cut from 65–71 µA to 10–15 µA; production firmware flashed and observed live end-to-end
Aug 22.

**29. MSV3 solar harvester test**
Daylight: V_OC ≈ 6 V, I_SC ≈ 30 mA measured directly; real charge current *inferred* (not directly
measured) at ≥10 mA via a no-solder implied-current method. A fully-discharged unit was released
on 2026-08-29 to test solar recovery from empty — result not yet known as of this deck `[VERIFY]`.

**30. Open MSV3 work + takeaways**
Open: EC-check needs a pin bodge (P0.00 has no ADC) and PCB revision; multi-day FDC drift
uncharacterized (don't blame C17 yet — controls pending); wet-soil IDRIVE tuning; overcast-only
solar-sleep test; temp/tank-reference telemetry implemented but not deployed.
Takeaways: field testing found more bugs than simulation ever did; keep raw data end-to-end so
calibration can be redone without reflashing; reuse a proven receiver/cloud stack when only the
sensing front end changes. End with questions.
