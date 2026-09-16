# MSV2 Status Handoff

Date: 2026-09-14 (the section immediately below is current; older deployment-status notes are historical)

## 2026-09-14 Bootloader 1.0.4 built (NOT installed), and evidence on the 1.0.3 archive dispute

- **Bootloader 1.0.4 exists as a built, verified artifact and is installed on nothing.** No probe
  runs it and none may until the acceptance list in the requirement document passes on probe 99.
  `MSV3/bootloader/generated/1.0.4/msv3-e73-recovery-signed-s140-6.1.1.hex`, sha256
  `47cf4e9b0a1412261efc71b42eec9b0579471921fe75a53283b52f8aee11330c`.
- **USB is a normal transport now, not a window.** Whenever the bootloader is in DFU mode and
  VBUSDETECT shows a cable, USB CDC serial DFU is up for as long as the cable is there -- no timer,
  nothing to miss -- and BLE OTA runs on top of it at the same time. No cable means usb_init() is
  never called and behavior is byte-for-byte 1.0.3, so a battery/solar probe pays nothing.
- **The blocker was never "can both stacks run".** usb_init() already supports a running SoftDevice,
  wait_for_events() already calls tud_task() whenever tusb_inited() regardless of DFU mode, and
  serial DFU *is* USB CDC -- the SLIP layer's tud_cdc_rx_cb() reads straight from
  tud_cdc_read_char(). The real blocker was one level down: dfu_register_callback() holds a single
  m_data_pkt_cb, and single-bank DFU erases the application before Init authentication, so two
  transports feeding one bank would destroy the image with no recoverable copy on either side.
- **`dfu_transport_claim()` is what makes it safe** and is the piece to understand before touching
  any of this. First transport to present traffic while the state machine is idle owns the session;
  the other is refused until dfu_init() resets ownership. Serial silently consumes and drops so the
  host retries; BLE answers NRF_ERROR_INVALID_STATE. Do not add a third caller of the DFU packet
  handlers without going through it.
- **An earlier draft of 1.0.4 was sequential** -- a bounded 10 s wired window, then BLE -- and is
  recorded in the design doc as the safe retreat if both stacks live together destabilize BLE DFU on
  real hardware. It costs a delay before BLE appears.
- **Verification lesson worth keeping.** The link-map check fired on the first build: the USB helper
  was absent from the map because it had been inlined, exactly as check_dfu_mode() already is. It is
  now noinline, and the release build checks for both msv3_usb_attach_if_vbus and
  dfu_transport_claim. A source sentinel proves a patch applied, not that the code reached the
  binary -- different questions for a bootloader that cannot be re-flashed on a coated probe.
- Cost: FLASH 84.52% -> 85.34% (+320 bytes), about 5.7 KB left in the 38 KB region. 1.0.3 was
  rebuilt after the change and still reproduces byte-identically (`a6e0ac08...`), so nothing the
  fleet currently runs moved.

### Evidence on `1.0.3/raw-probes-v65556-manifest.json` (dispute NOT resolved)

`verify_msv3_release_archive.py` currently fails on this one pre-existing entry. Measured
2026-09-14, recorded as evidence only -- no hash in `DISPUTED.json` was edited, per its own rule:

- The path has exactly **one** commit in history (`062d9e1`), blob `2dbae5ef...`. It has never been
  edited since it was introduced.
- `core.autocrlf` is `true` and there is no `.gitattributes`, so it checks out as CRLF. The CRLF
  form hashes to `9073af59...`, which is **exactly** what both `SHA256SUMS.txt` and
  `release-manifest.json` record. On this checkout the artifact matches its integrity records.
- `DISPUTED.json`'s recorded `sha256_on_disk` is `4a979c49...`, and that matches **none** of the
  serializations of the committed content: not LF (`2dbae5ef`), not CRLF (`9073af59`), not either
  without a trailing newline (`a5a166c3` / `9073af59`).
- So line-ending normalization explains why the file agrees with its integrity records, but it does
  **not** explain `4a979c49`. That hash came from content that was in some working tree and was
  never committed. Resolving this is a maintainer judgement call, not a mechanical fix, and the
  verifier should keep failing until someone makes it.

## 2026-09-14 Maintainer decisions: C17 fleet-wide, coating test started, USB/BLE design scoped

- **C17 is physically removed from every probe.** The hardware half of the capless rollout is done
  fleet-wide. This makes probe 6 a matched pair (capless hardware + capless firmware) and makes
  probes 4, 5, 7 and 8 a *mismatched* pair: capless hardware still running images that select mux
  2/3. Finishing those installs is now remediation rather than routine sequencing. A 200 s scan at
  18:41 found none of probes 4, 5, 7 or 8 advertising, so the risk is latent rather than active --
  the rule is to install the capless image on each **before** it is powered back into service.
  Probe 6 was the only MSV3 on the air and is a matched pair: flags `0x56`, wake 50 advancing +2,
  `DATA_CH0` 37,522,374, bare invalid, die 20.50 C, 4263 mV. See `docs/KNOWN_ISSUES.md`.
- **Battery percentage rescale is deliberately deferred.** Confirm the capless system works against
  raw millivolts first. `BATTERY_CURVE`'s 4200 mV -> 100% mapping stays as-is in both copies
  (`Treebeard_ESP32_Display_Runtime.ino`, `MobileAppPage.h`), so a reworked ~3.90 V pack reporting
  about 63% is expected behavior for now and must not be "fixed" as a bug in the meantime.
- **Blade coating validation is running**, on the specimen tracked as "MSV3 Coated Test". First real
  run of the air -> soil -> watering -> return-to-air gate. Not validated until the cycle completes;
  do not coat further production units on a partial result.
- **USB + BLE OTA coexistence: design started and explicitly scoped.** The USB recovery path is
  **MSV3-only** remediation for the resident 1.0.3 bootloader; the signed BLE OTA architecture
  (device type, signed init, monotonic version floor, boot-guard pairing, fleet registry, fail-closed
  preflight) is the long-lived envelope and **carries forward to MSV4**. The USB work may not add an
  MSV3 special case to any of those. Mechanism, risks and bench plan:
  `MSV3/docs/MSV3_USB_AND_BLE_OTA_COEXISTENCE_DESIGN_2026-09-14.md`.
- Key design finding: the bootloader's USB/BLE mutual exclusion is a **policy choice in one if/else
  in `src/main.c`**, not a stack limitation. `usb_init()` already branches on
  `sd_softdevice_is_enabled()` and routes power events through `sd_power_usbdetected/pwrrdy/removed`,
  reserves IRQ priorities 0-1 for the SoftDevice, and `proc_soc()` already forwards SD USB power
  events into TinyUSB. Upstream wrote the coexistence plumbing and simply never calls both
  init paths in one boot. Proposed approach is therefore BLE unconditionally, plus USB additionally
  when VBUS is present, with a single-winner rule before any flash write. Nothing is implemented.

## 2026-09-16 MSV4 front end decided: FDC out, software lock-in in

- **The FDC2214 comes out.** Controlled comparison: sensor 9 (MSV3 electrode, uncoated, fixed-frequency
  AM) is clean; probe 8 (MSV3 electrode, uncoated, FDC resonant) wobbles 1.956%. Same electrode, both
  uncoated, one variable. The AM/FM bench checklist's decision rule fires.
- **Correction: only probe 6 is coated.** This session briefly concluded probes 4/5/6 all were, which
  would have confounded the above. The error was reading `swd_accessible: false` as "coated" — that
  field means "pads reachable right now," also false for a probe buried in a pot. **Never infer
  coating status from it.** Consequence: the entire AM-vs-FM thermal dataset is uncoated probes, so
  none of it needs revisiting.
- **New experiment (maintainer):** uncoated probe by a window for a day read **flat in pure water,
  flat in air, and wobbled hugely in moist soil.** Rules out board, clock, inductor, battery and
  temperature-on-electronics. The wobble needs the soil matrix — i.e. dissolved ions, whose mobility
  rises ~2 %/C. **Outstanding: the salty-water test**, which separates a conductivity-tempco artefact
  from genuine thermally-driven moisture migration. Highest-value experiment pending.
- **Architecture: one chopper switch with phase AND frequency as firmware parameters** — a software
  lock-in. Sweeping phase on a single switch eliminates I/Q mismatch structurally, and the pedestal
  falls out of the N-phase fit, so no balanced branch and no calibration reference are needed. One
  phase = AM detector; four = IQ demodulator; **same board**, so the IQ decision defers to firmware.
- **Frequency: 1–3 MHz, and do NOT auto-track.** Sweep to characterise, then lock — auto-tuning
  rebuilds the exact FM failure that killed the FDC. Sweepability replaces the LCR/VNA measurement
  every analysis has been waiting on.
- **TS3USB221A is a DPDT**, so one part does the whole A/B swap on one GPIO, with `OE` giving a free
  OPEN reference. **Its t_ON = 30 ns / t_OFF = 12 ns caps the chopper at 1–2 MHz, not 8** — the 18 ns
  asymmetry is a temperature-varying duty error. The "900 MHz bandwidth" headline is the *signal* path,
  not the select path.
- **R_on and capacitance trade directly**: the 0.3 ohm TS3A24159 carries 224 pF. Buy the higher-R_on,
  lower-C part and shop USB/HDMI signal switches, not precision muxes.
- Full write-up, topology diagram, BOM and the datasheet traps:
  `MSV3/docs/MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md`

## 2026-09-16 MSV4 BOM drafted for Altium

- `MSV4/docs/MSV4_PROBE_BOM_2026-09-16.md`. Scope per maintainer: nRF module with attached antenna,
  same BQ25504, the lock-in front end, a soil NTC. No USB, no FDC, no 555.
- **The cost saving is already banked, and it is not the MCU.** From this project's own LCSC orders:
  removing the **FDC2214 saves $6.70**, the USB stack (BQ25170 + USBLC6 + PMEG2010 + Type-C) another
  $1.53, plus the 4052 and the tank inductor — **-$8.37** against ~+$1.5 of new parts. That is ~42 %
  off a ~$16.5 electronics BOM before any MCU decision.
- **But two thirds of the ~$50 board is not on the BOM at all** (~$33.5 of fab, assembly, shipping,
  panel, battery, enclosure). **Asked the maintainer for the real breakdown** — quantity, unique part
  count and stocked-vs-not are probably bigger levers than any part swap.
- **Recommendation: stay on nRF52840 / E73-2G4M08S1C ($7.5888 verified) for V4.0.** The nRF52832
  E73-2G4M04S1B (C411306) would save ~$3 — 6 % of the board — but costs revalidating the entire OTA
  chain (fleet registry, signed packages, Guard 2 / bootloader 1.0.3 pairing) on a different
  SoftDevice, and V4 already changes electrode, front end and panel at once. Revisit on V4.1.
- **LCSC blocks automated fetches** (404 on every product URL this session), so the ~$4.59 figure for
  the 52832 module is an unverified search summary. Confirm on the page before ordering.
- **New recommendation: fit a 32.768 kHz crystal and keep `P0.00`/`P0.01` free for it.** ~$0.15
  retires the entire `MSV3_LFCLK_RTC1_HANG` class of problem *and* saves the LFRC recalibration
  wakeups against a 10-15 uA sleep budget. This is a layout constraint — decide before routing.
- Panel is now a **hard electrical spec on the BOM**: `V_OC` <= 2.7 V at 25 C.
- Also corrected for V4: the BQ25504 OV divider sum must be **9-11 MOhm**; MSV3's reworked 8.54 MOhm
  is out of spec and must not be carried over.

## 2026-09-16 REVERSAL: sensor 9 shows the thermal swing too — the readout was never the variable

- **The comparison that justified dropping the FDC is void.** `MSV4_FRONT_END_ARCHITECTURE_2026-09-16.md`
  recorded sensor 9 (MSV3 electrode, uncoated, fixed-frequency AM) as **clean** against probe 8's
  1.956 % wobble, and concluded the modulation scheme was the cause. **Sensor 9 was not clean — it had
  not run long enough.** Observed 2026-09-16: it shows the same daily swing, with the **same
  anti-thermal signature seen from MSV3 in this same pot** (warm afternoon reads drier).
- **Both AM and FM swing, same electrode, same pot.** The FDC, the LC tank, the oscillator and the
  driving circuit are all exonerated. **The cause is the electrode geometry and the soil-probe
  interface** — the sensor is accurately reporting something real at 0.48 mm sensing depth.
- **The FDC still comes out, on different grounds** (maintainer): BOM cost, AM-over-FM for long-term
  deployment, and firmware-settable frequency/phase. The architectural objection to FM — *"the
  interrogation frequency IS the output"* — never depended on the thermal data and survives.
- **What must not be claimed again: removing the FDC does not fix the daily wobble.** The **electrode
  redesign is the fix**, and is now the load-bearing change in V4 rather than a parallel improvement.
  0.48 mm -> 2.90 mm averages interface movement over millimetres instead of a fraction of one.
- **A third mechanism is now on the table and no planned test catches it:** soil-probe *interface
  movement* (soil shrinking and moving against the blade). A glass of water has no interface to move,
  so the salty-water test cannot see it. Separating it from moisture migration needs a **mechanical**
  test, not a chemical one.
- **The sign argues against conductivity tempco.** Warmer reads drier; rising ion mobility should read
  *wetter*. Confirm the polarity chain on both rigs — it is nearly free and could retire a candidate.

## 2026-09-16 V4 stays on the nRF52840; excitation and phase come from TIMER/PPI/GPIOTE

- **MCU decided: E73 / nRF52840, not ESP32.** The low-power architecture is why the project is on
  that part; 10-15 uA sleep and a 528 uW average are not reproducible on an ESP32. Earlier drafts of
  the front-end doc specified ESP32 `LEDC` with phase via `hpoint` — replaced.
- **One TIMER at 16 MHz, four compares, two GPIOTE channels through PPI** makes the excitation square
  wave and the phase-offset chopper select. Both come off the *same counter*, so the phase relation
  is exact and jitter-free by construction — structurally better than two independent LEDC channels.
  Four CCs and four PPI channels, so TIMER1/2 suffice; avoid TIMER0 (SoftDevice).
- **Operating point N = 16, f = 1.000 MHz.** Quadrature lands on ticks 0/4/8/12 exactly, the 16-phase
  diagnostic sweep is exactly the full grid, and 1 MHz is where the TS3USB221A's 18 ns asymmetry costs
  least (6.5 deg). Cost is electrode |Z| rising to ~1990 -> 795 ohm dry to wet, which is what sets Rs.
- **The coarser phase grid is not a coarser measurement.** 16 MHz gives 5x fewer steps than LEDC's
  80 MHz, but `V(theta)` is *fitted* — recovered phase precision is set by SNR and point count, not
  step size. Quadrature is exact on any N divisible by 4.
- **Hard rule: excitation off HFXO, never HFINT.** A shared clock means clock error does not corrupt
  phase — but it moves the interrogation frequency, and a +/-6 % RC with a tempco against a
  frequency-dependent soil impedance injects exactly the FM-class thermal artefact V4 exists to kill.
- SAADC sampled by PPI from the same TIMER, oversampled hard, `CALIBRATEOFFSET` at wake.
- `MSV4/docs/MSV4_PROBE_BOM_DRAFT.md` (09-02) now carries a banner: its TLC555 sensing block is
  superseded; its power chain, BQ25504 network and cost model still stand.

## 2026-09-16 VIN_DC measured at the pin: 5.0 V in sun, 4.1 V at the window

- **Root cause confirmed by direct measurement.** Loaded `SP+` (= VIN_DC) reads **5.0 V in direct
  sun** and **4.1 V indoors against the deployment window** — 1.4x to 1.7x the BQ25504's 3 V
  recommended input maximum, with the 6 V `V_OC` at each MPPT open-circuit sample above the 5.5 V
  absolute maximum.
- **The window figure is the one that matters.** 4.1 V is above any 1S cell state of charge, so the
  PMOS body-diode path conducts **all day in normal indoor deployment** — this was never an
  outdoors-only failure, and probe 6 did not need direct sun to reach 4.49 V. It also means the part
  has been run continuously out of spec, raising the prior on the damaged-BQ25504 branch of the
  post-clamp verification.
- **4.1 V was a spot reading; the window peak is 4.7 V — predicted from probe 6's own cell voltage,
  then measured the same day.** The window runs at ~94 % of the direct-sun condition, not a milder one.
- **The ceiling is arithmetic: `VBAT = 0.78 x V_OC - 0.3 V`.** MPPT holds VIN at its programmed
  fraction of `V_OC` (0.78 x 6.0 = 4.68 V, measured 4.70) and the body diode drags the cell to just
  under it (4.38 V predicted, ~4.44 measured). One expression reproduces every charge observation on
  file, and `VBAT_OV` appears nowhere in it. It also prices the lower-`V_OC` panel fix exactly.
- **The clamp interacts with MPPT, and the old power table was optimistic.** A shunt zener clamps the
  open-circuit excursion too, so the BQ samples `V_OC` = `Vz` and then regulates to 78 % of *that* —
  a 3.3 V clamp operates the panel at **2.57 V**, not 3.3 V. Harmless in sun (77 mW, 146x) but it
  makes the window margin the live question: 1 mA at the window is 4.9x the load, 300 uA is 1.5x,
  which is not a margin. **Re-choose the `RMPPT1/2/3` ratio when fitting the clamp** — once a clamp
  sets the sampled `V_OC`, MPPT is tracking the zener, not a real MPP, and 78 % is 22 % given away.
- **Zener dissipation is a non-issue: 99 mW peak, 1.6 mW average, zero harvest cost.** Verified from
  SLUSAH0G 8.3.1 — the BQ resamples `V_OC` every **16 s** by disabling the charger for **256 ms**.
  MPPT holds VIN at 2.57 V the rest of the time, below the 3.3 V knee, so the clamp conducts only
  during that 1.6 % window — while the charger is disconnected and harvesting nothing anyway. Package
  is not a constraint. **Leakage is the only continuous loss** (daylight-only; budget <20 uA at
  2.6 V = 51 uW, ~10 % of load). The real cost is the operating point, not the diode.
- **The clamp cannot affect sleep current — structurally, not marginally.** Netlist confirms
  `NET SP+ : C4-2, C8-1, L1-1, R9-2, SP+1-1, SP_IC-2` — panel side of the boost only, and **USB is not
  on it** (`VBUS` enters at `Q1-1 -> VSTOR`). Draining the cell into the zener would mean running
  backwards through the high-side PMOS body diode, which is blocked; the same diode that causes the
  overcharge prevents reverse drain. At night VIN_DC ~0 V, so leakage is zero. Leakage is a *daylight
  harvest tax*, never a sleep-current cost — separate budgets.
- **The soft knee is mostly a benefit.** Under load the clamp sits above nominal `Vz`, raising the
  sampled `V_OC` and the operating point, recovering harvest. Safe because the 78 % MPPT ratio is a
  free safety factor the original tolerance stack never claimed: ceiling is `0.78 x V_clamp - 0.3`,
  which allows `V_clamp` to ~5.1 V. Still specify 3.3 V (holds even if MPPT is disabled), but do not
  reject a part that measures 3.8-4.0 V at 30 mA.
- Window `I_SC` is **no longer a gate** — maintainer can duty-cycle the probe deeper into sleep to meet
  whatever the panel delivers. Nice to know, not blocking. (No ammeter on hand; the earlier
  2.7 V / 50 uA figure was indoors under LED light, not the window.)
- **V4 answer: fix the panel, do not design the clamp in. Target `V_OC` <= 2.7 V at 25 C** — under the
  3000 mV recommended max *at open circuit*, with headroom for the -0.3 %/C rise as temperature falls
  (a 3.0 V panel at 25 C is out of spec at 0 C). Fewer cells in series for the same die area is the
  same power at a legal voltage, so it costs nothing — and MPPT then tracks the real MPP, giving V4
  **100 % of MPP where a clamped V3 gets 61 %**.
- **Keep the BQ25504.** 330 nA `I_Q`, operation down to 130 mV, 10 uW minimum input power — the
  harvester was never at fault, the panel/input mismatch was.
- **Datasheet correction: `VIN(CS)` is 600 mV typ / 700 mV max, not 330 mV.** TI raised it in a later
  revision; 330 nA is the *quiescent current*, a different parameter. The bench plan already has this
  right — do not let the old number back in.
- Resistor-only fallback if no zener is available: lower the MPPT ratio to ~50 % so VIN never exceeds
  `VBAT_OV`. **Weaker** — it leaves the 6 V `V_OC` sample violating the 5.5 V absolute maximum every
  16 s. Only the zener kills that excursion.
- **Clamp size settled: 3.3 V, 5 %, SOD-123 or larger** (100 mW worst case). The window data breaks
  the old 3.0-vs-3.3 near-tie — indoors the panel operates at 4.1-4.75 V, so every 100 mV of clamp is
  harvest given up, and 3.3 V is the highest value the `VBAT_OV` tolerance stack allows.
- **The clamp analysis now has a hole: every margin figure uses `I_SC` = 30 mA, a direct-sun
  number.** The only indoor current on record (50 uA) was taken at a deliberately dim spot, not the
  window. **Measure panel `I_SC` at the window** — it decides both break-even (~160 uA continuous,
  ~380 uA over 8 h) and whether a cheap zener's soft-knee leakage, invisible against 30 mA, eats a
  large fraction of a few hundred microamps. Specify reverse leakage near `Vz`, not just `Vz`.
- Free discriminator still open: read `raw_battery_mv` at the same instant as `SP+`. A 0.3-0.4 V gap
  proves the diode path is conducting in situ rather than MPPT setting the operating point.
- Written up in `MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`, which also now records
  that the bench plan's "real deploy condition" panel measurement (2.7 V / 50 uA) is not the
  deployment condition.

## 2026-09-15 MSV4 electrode architecture decided

- **Penetration depth is set by electrode pitch alone** — `depth = lambda/2pi`, where lambda is twice
  the electrode centre-to-centre spacing. Finger count, finger length and board area do not appear.
  MSV3's 1.5 mm pitch means it reads a soil skin **0.48 mm** thick; the v1.2 reaches **2.71 mm**.
- **This explains the coated-probe failures.** A 75 um soldermask + conformal stack holds **14.5%** of
  MSV3's field energy versus 2.7% of the v1.2's. The coating absorbs water, its permittivity climbs,
  then it *saturates* — a large signal that stops responding. It is a geometry problem, not a coating
  chemistry problem; no change of material fixes a 0.48 mm sensor.
- **Decided: two symmetric electrodes inside a guard ring, mirrored front and back**, on an
  18 x 110 mm blade — `guard 1 | clr 1 | A 4.9 | gap 4.2 | B 4.9 | clr 1 | guard 1`. Reaches
  **2.90 mm**, beats the v1.2, keeps electrode swapping (A and B interchangeable), drops the coating
  to 2.6%. Mirroring roughly doubles capacitance *and* raises the soil fraction (48% -> 65% dry).
- **Vias: one pair at the dry end only.** A through-hole in the wet zone is a hole in the coating
  barrier. At 6 MHz a 110 mm trace is lambda/218, so one connection is electrically identical to
  fifty.
- A soil-depth **0402 NTC at the tip costs 0.04 mm of penetration** if its traces are RF-bypassed to
  the guard net (~1 nF to guard at the top: a short at 6 MHz, open at DC).
- Rejected: three-prong G-H-G. `lambda = W - W_ground` appears to allow thin grounds, but the ground
  must still terminate the field — keep `W_ground >= gap` and `W_high/W_ground <= 4.5`, which caps an
  honest 3-prong at **2.47 mm** at 18 mm width.
- Full write-up with the termination limits, mirroring analysis and temperature-sensor routing:
  `MSV3/docs/MSV4_ELECTRODE_ARCHITECTURE_2026-09-15.md`. Scale drawings:
  <https://claude.ai/artifact/LsGnvQqxJycer2YNNZSL75>

## 2026-09-15 VBAT_OV root cause: the 6 V panel, not the divider

- **The BQ25504's overvoltage protection has never bounded the cell on any probe, and the
  2026-09-14 resistor rework could not have fixed it.** The panel is `V_OC` = 6 V (this project's
  own 2026-08-21 measurement), against a BQ25504 input rated **3 V recommended / 5.5 V absolute**.
  A boost converter cannot output below its input: when VIN_DC exceeds VSTOR the panel charges the
  cell straight through the high-side PMOS **body diode**, a passive path with no comparator and no
  enable. `VBAT_OV` is bypassed entirely. Datasheet footnote (2) on page 6 states the requirement
  directly: *"VBAT_OV setting must be higher than VIN_DC."*
- Confirmed against telemetry: probe 6 charged 4248 → 4295 mV on 09-15 with a 3.887 V ceiling
  programmed, tapering with the sun rather than stopping at a threshold. Probe 6's cell reached
  **4487 mV (~4.44 V)** across 09-08 → 09-14. The battery ADC is good to +1.0 % (DMM 4.25 V vs
  4293 mV telemetry), so those readings are real.
- **Fix is a shunt zener on `SP+`, 2.4–3.3 V** (3.3 V best, 3.0 V strictly in spec; **not** 3.6 V or
  above — it fails the tolerance stack against `VBAT_OV`). Costs nothing: measured average load is
  **528 µW** against 90–100 mW of clamped panel, a ~180x margin.
- **New measured power budget** from probe 4's night-only discharge: **126 µA / 528 µW**, 3.03
  mAh/day, ~50 days from full with no sun. Consumption dropped **5.5x around 2026-08-29**; not yet
  attributed.
- Also corrected: the `R_OV1`/`R_OV2` orientation question in `COMPONENT_INDEX.md` is resolved (the
  documented orientation was right), and the divider-sum rule is 9–11 MΩ **with a minimum**, so the
  reworked 8.54 MΩ is out of spec. Maintainer has deliberately left it there for now.
- Full analysis, zener selection table, scrounged-part alternatives, and the marking-code reference:
  `MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md`.

## 2026-09-14 Probe 6 capless install, capless receiver live, and the sensor 9 hybrid rig

- **The capless-aware receiver is now actually running**, not just compile-verified. `Treebeard
  runtime build 2026-09-14 msv3-capless-scale3` was flashed to COM16 (the v1.2 overnight rig that
  had been occupying the bench was unplugged, which was the only thing blocking it). This had to
  happen first: the capless policy's rollout order is receiver-first, and installing scale-3
  firmware against a scale-2 receiver mis-ingests every reading.
- **Probe 6 is on signed capless production v65564** (`MSV3-BUILD:2026-09-14-capless-prod6`),
  installed over BLE in 38 s through the resident 1.0.3 bootloader. DIS read back the exact build
  ID. Verification detail worth reusing: the DIS was proven by re-running the SAME package, which
  `check_running_build()` refuses *before* the jump opcode is written -- so the build ID is read off
  the live device without taking it off the air, and without `--reflash-same-build`.
- Post-install scan: 36 packets, wake advancing, 4264 mV, die 22.75 C, flags `0x54` (capless bit 6
  SET, bareValid CLEAR), `DATA_CH0` 38,019,830, bare invalid, OTA gate open. `MSV3/fleet.json`
  updated to 65564 only after all three postconditions passed.
- **C17 is physically removed from ALL probes** (maintainer, 2026-09-14), so rollout step 3 is done
  fleet-wide and probe 6 is now capless hardware running capless firmware -- a matched pair. The same
  fact inverts the risk on the rest of the fleet: probes 4, 5, 7 and 8 are now capless hardware still
  running images that select mux 2/3, which is the combination
  `MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md` forbids. Finishing those installs is now a fix,
  not a scheduling item.
- Probe 6 still has the as-built BQ25504 OV divider and charges to ~4.26 V; it has not had the
  R11/R14 ~3.90 V storage rework from `edc9705`. **Deliberately deferred** (maintainer): the battery
  percentage rescale waits until the capless system is confirmed working against raw millivolts, so
  `BATTERY_CURVE`'s 4200 mV -> 100% mapping stays as-is for now and a reworked pack reading ~63% is
  expected, not a bug.
- **New sensor 9: the hybrid AFE rig.** A second ESP32-S3 runs the same v1.2 analog front end with
  an MSV3 probe end in place of the module's own pad, advertising from `AC:A7:04:31:8A:09` on the
  identical MSV3 duty cycle as sensor 3. Sensor 3 is untouched as the control, so only the probe end
  differs, and the pair runs for a couple of days to see whether the MSV3 electrode removes the
  temperature-shaped artefact rung 1 did not. `MOISTURE_INVERT_CEILING_BY_SENSOR[9]` = 3300, same as
  sensor 3. AFE sensor identity is now a build parameter (`-SensorId`) with a per-ID build
  directory, for the reason MSV3 made probe identity one in `ba76e29`.
- The probe end was attached at `2026-09-15 01:21 UTC` and sensor 9 immediately read **2409-2411
  mV**, holding within 2 mV across ten advertisements -- a sane dry baseline for this front end. The
  nine earlier unloaded-input rows were deleted from cloud history (ids 617379-617422), backed up
  first and scoped by primary key; the board's wake counter reset to 1 at the power-cycle, so the
  bad and good series are separated by an independent reboot boundary rather than by a judgement
  about which values looked wrong. Learned range re-read clean afterwards at `890/891` with no false
  watering rises, and sensor 3's stayed untouched at `850/1130`.
- Details, plus a serial-monitoring pitfall that can silently leave one of these ESP32-S3 boards in
  download mode: `MSV3/docs/MSV3_CAPACITIVE_V1_2_AFE_CHECKOUT_2026-09-14.md`.

## 2026-09-14 Probe 7 repair, current image, and USB + OTA follow-up

- **Probe 7's TMUX fault is repaired.** Static codes 2/3 first proved that both select GPIOs and
  the EC-side routes worked. The targeted `hotcoldstream` diagnostic then isolated the failure to
  mux code 1: code 0 read about 38.69M while code 1 read about 53.89M, and pressing the TMUX package
  restored code 1 to the normal region. That pressure sensitivity identified a package solder
  joint, not an FDC, tank, firmware, or mux-decoder failure.
- The TMUX was resoldered. A temporary rework bridge stopped CH0 conversions and produced the
  bring-up console's ambiguous `I2C read failed` message; after cleanup, `i2cscan` found `0x2A`,
  `fdcid` returned `0x5449/0x3055`, `fdctest` completed 10/10 with zero failures, and repeated
  flips held both code 0 and code 1 near 36.73M with only normal `STATUS=0x8 CH0_UNREAD`.
- **Probe 7 currently runs signed targeted bring-up application v65565**, build
  `2026-09-14-probe7-mux-diag`. Its successful activation raised the on-device rollback floor to
  65565. A paired capless production v65565 equality-version restore was built and archived before
  installing the diagnostic; do not try to restore the older v65556 image.
- **Open requirement from the maintainer:** USB and BLE OTA must coexist in the eventual MSV3
  recovery design. The present bootloader's `DEFAULT_TO_OTA_DFU` behavior provides automatic BLE
  recovery when the application is absent but removes bootloader USB CDC/DFU. Do not treat that
  mutual exclusion as the desired final design. Preserve signed OTA, rollback protection, and
  invalid-application recovery while restoring a dependable USB firmware path and application USB
  serial access. Acceptance criteria and the failure mode are recorded in
  `MSV3/docs/MSV3_USB_AND_BLE_OTA_COEXISTENCE_REQUIREMENT_2026-09-14.md`.

## 2026-09-14 MSV3 capless production / no bare-tank measurement

- **Decision:** C17 is DNP and must be removed from every healthy MSV3 during rollout. Production
  firmware measures only connected electrode phases (mux 0/1), never selects disconnected mux 2/3,
  leaves legacy bare bytes 19-22 zero/invalid, and advertises flag bit 6 as the capless identity.
- **Probe 8 evidence:** removing C17 moved the connected dry count from about 20.49M to 39.41M.
  Two-hand contact moved it from about 39M to 27M, roughly four times the former raw response.
  Both phases were stable at IDRIVE 16 with `STATUS=0x8` and no amplitude fault. A deliberate bare
  test returned exact over-range `0x0FFFFFFF` plus `ERR_WD`, consistent with the calculated 11-12
  MHz capacitor-free disconnected tank exceeding the FDC2214 limit.
- **Why the trade is accepted:** the real-pot result showed bare normalization removed only 11.7%
  of the final day's daily harmonic. The dominant bulk-soil-temperature and probe/soil-interface
  terms are not observed by the disconnected board tank; the measured approximately 4x useful
  response is worth retiring that small correction.
- **Receiver contract:** capless flag bit 6 selects moisture scale 3, a wider raw-count plausibility
  range, and a separate cloud calibration epoch. Bare/reference temperature compensation is forced
  off for scale 3; raw connected counts and die temperature remain available for a future physical
  soil/interface model. Legacy C17/scale-2 packets still parse during rollout.
- **Prepared, not deployed:** signed application v65564 packages are archived for Probes 4, 5, 6,
  and 8. Probe 7's mux is now repaired and has its own v65565 production restore package, but it
  currently remains on the matching v65565 diagnostic image. DisplayCenter has only been
  compile-verified because COM3 is occupied by the v1.2 overnight test. Probe 8 remains on signed
  diagnostic v65563; do not install capless production until the capless-aware receiver is actually
  running.
- Full measurements, hashes, invariants, and receiver-first rollout order:
  `MSV3/docs/MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md`.

## 2026-09-01 DisplayCenter learned percentages, periodic freeze, and cloud reliability

- **Resolved and deployed to COM16.** Canonical incident record:
  `DisplayCenter/docs/RECEIVER_PERCENT_RANGES_DISPLAY_FREEZE_AND_UPLOAD_FIX_2026-09-01.md`.
  Production identity is `MSV2 runtime build 2026-08-31 durable-cloud-outbox-v7-smooth-display`.
- Supabase was not dropping accepted readings and was not over capacity. The apparent connection
  refusals were local `mbedtls_ssl_setup()` contiguous-allocation failures: the receiver built a
  large history `String` before TLS reserved its buffers. TLS now connects first and compact rows
  stream through one bounded row buffer.
- Missing percentages were a receiver scheduler regression. Treating any nonempty history queue
  as emergency pressure indefinitely starved `runCloudAutoCalPull()` on a continuously reporting
  system. State pulls now yield only at 75% outbox capacity, and learned core ranges are cached in
  `/autocal-cache-v1.json` for immediate post-reset display recovery.
- The every-few-seconds display pause was one LittleFS open/append/flush/close per unique probe
  wake; measured transactions took 29-240 ms. Healthy uploads now copy and acknowledge a
  checksum-protected 64-row RTC ingress ring directly. LittleFS is used only as 8,192-row outage
  spillover, so `cloudJournalWriteDiag.atMs` stayed `0` across healthy live cycles.
- Capacity is suitable for the intended 10-16 sensor fleet: 96 rows per paced upload versus
  10-16 normal rows/minute, or about 35/minute with one three-second bench sensor plus 15 normal
  sensors. Sixteen sensors all producing unique rows every three seconds (320/minute) is explicitly
  outside the supported envelope.
- Final live acceptance: three HTTP 200 upload cycles, consecutive failures `0`, dropped rows `0`,
  journal storage errors `0`, durable backlog `0` after acknowledgement, and learned percentages
  restored for sensors 1-3. Sensor 4 is intentionally `auto=false`; sensor 5 remains cloud
  `ready=false`.
- `DisplayCenter/scripts/verify_receiver_cloud_recovery.mjs` encodes the scheduler, RTC, direct
  upload, TLS-first streaming, and capacity invariants. The standard DisplayCenter compile/upload
  wrapper runs this check before Arduino CLI so a regression is blocked before flashing.

## 2026-08-30 DisplayCenter touch/display corruption and brightness overlay redraw bugs

- Two independent bug threads on the ESP32-S3 receiver's shared ST7796/XPT2046 SPI bus, both
  field-confirmed resolved. Full write-up:
  `DisplayCenter/docs/DISPLAY_TOUCH_CORRUPTION_AND_OVERLAY_REDRAW_2026-08-30.md`. This also
  corrects `DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`'s stale "RESOLVED" status - that
  bug's idle-polling case stayed fixed, but a related active-gesture case silently recurred.
- **Card-art corruption during active touch.** An isolation test (touch SPI fully disabled)
  proved touch SPI traffic during a gesture - not idle polling - was the trigger. Fixed by
  rewriting `handleTouchInput()` to use the T_IRQ GPIO pin (zero SPI cost) as the primary
  down/up signal, spending exactly one real SPI transaction per plain tap/long-press (down from
  ~5-20+), plus a bidirectional bus-turnaround guard (`touchAcquisitionPending`,
  `lastDisplaySpiActivityMs`) so that one read waits for quiet on both the touch and display
  sides. Corruption dropped from most gestures to not reproduced in subsequent testing. Not
  architecturally eliminated - full elimination would need a hardware check (verify `TFT_CS`
  holds high at the ST7796 controller pin during an XPT2046 transfer), not done since the
  deployed unit can't be reworked physically right now.
- **Brightness overlay**, four separate bugs, all fixed: (1) most buttons were doing a full
  modal repaint instead of the targeted redraw the drag path already had - added matching
  targeted functions for the screen/sound buttons; (2) the slider's value text left stale
  leading digits behind when shrinking (e.g. "100%" -> "9%" reading like "19%") - same
  fixed-width-clear fix already used for the battery header; (3) rapid button-mashing could
  drift the cached tap position outside a button's hitbox by release time and misfire the
  overlay closed - fixed by latching the touched control once at the confirmed press
  (`OverlayTouchTarget` enum) instead of re-deriving it from a possibly-drifted coordinate at
  release; (4) closing the overlay either did a full flashing `renderUi()` rebuild or (after
  replacing that with a lighter `restoreCardsFromCache()`) left gutter-colored residue between
  cards, since only each card's own rect was repainted - fixed by filling the modal's panel
  rect with the real background color first.
- Two phone-a-friend (Codex) consultations under `display-touch-corruption-2026-08-30` directly
  changed the outcome both times - once catching that a symptom's timing was inconsistent with
  the working theory (leading to the real active-gesture mechanism), once correctly predicting
  that a coordinate-pinning fix alone was too narrow and the right fix was latching an immutable
  gesture target. Both consultations are worth reading in full if this area breaks again.
- Deployed: `MSV2 runtime build` flashed to COM16 across several iterations this session, each
  compiled clean and hash-verified; final state includes all fixes above.

## 2026-08-30 Persistent phone-a-friend tandem lifecycle

- The external `C:\Users\samca\Desktop\agent-supervisor` phone-a-friend MCP tool was upgraded from
  an internally contradictory one-shot draft to version 0.2.0 with an explicit
  `start -> continue -> finish` lifecycle.
- One stable `consultation_id` now retains the same Claude/Codex advisor session across discovery,
  planning, implementation, verification, and final review. Replies expose `active`, `turn`, and
  `stage`; `finish` forces `final-review`, returns the last independent review, and closes the
  session.
- Server instructions now require the primary agent to bring meaningful new evidence back at each
  checkpoint and to finish the consultation before its user-facing answer. The friend prompt asks
  what evidence should be returned next, making the relationship iterative instead of one call that
  resolves one narrow issue.
- The privacy boundary remains context-only: no workspace/path argument, no direct project access,
  no tools/network, and no recursive phone-a-friend. Each advisor receives an isolated temporary
  directory that is removed on finish, failure, cancellation, timeout, or 12-hour idle expiry.
- Same-ID calls are serialized; failed/timed-out sessions are discarded so a later call cannot
  overlap a provider request that ignored cancellation. One server process allows at most 16 active
  consultations.
- `npm.cmd run check`, all 31 tests, and `npm.cmd run build` pass. The rebuilt
  `dist/src/mcp-server.js` is registered globally with Codex and at Claude user scope; both point to
  the same compiled file, and Claude reported the server connected. Restart existing editor chat
  clients to guarantee they reload the new tool schema and server instructions.
- Billing-safety guards were not modified.

## 2026-08-30 MSV3 FDC diagnostics, field firmware, and coating finding

- The original complaint was valid: the MSV3 connected FDC count had a repeatable daily movement of
  roughly 0.68-1.00%, while the watering-adjacent stable step was only about -0.71% in count. A
  complete audit verified CH0, coherent MSB-before-LSB reading, all 28 count bits, BLE byte order,
  receiver decoding, and raw-history storage. This was not a wrong-channel or truncation problem.
- The 23-byte MSV3 diagnostic packet is now represented end to end: nRF52840 die temperature,
  disconnected bare-tank FDC count, and mux phase accompany the existing connected count and battery
  voltage. The History page conditionally charts die temperature, connected raw count, bare count,
  and an experimental `100*((Db/Dm)^2-1)` capacitance-above-bare trace.
- In the first 269 paired rows (12:06-17:20 PDT), die temperature spanned 20.75-28.25 C. Bare count
  correlated -0.9946 with die temperature; a first-order bare normalization reduced connected-count
  slope by about 47.5%. This establishes that the reference is useful, but the residual still contains
  electrode/soil/coating behavior and must not be labeled volumetric water content.
- Probe acquisition source now uses `RCOUNT_CH0=0xFFFF`, full-current activation, a bounded wait for
  `STATUS.CH0_UNREADCONV`, and STATUS-before-DATA ordering. The temporary 300 ms mux delay was removed:
  it occurred while the FDC was shut down and could not settle the oscillator. IDRIVE and
  SETTLECOUNT remain bench-tuning items, not guessed fixes.
- A first fresh-data flash incorrectly polled global `STATUS.DRDY`. With reset-default
  `ERROR_CONFIG.DRDY_2INT=0`, that readiness indication never appeared, so the probe stopped
  advertising. The production sketch was corrected to poll `CH0_UNREADCONV`; data returned after the
  reflash. The modified bring-up sketch still contains a DRDY poll and must be reconciled before its
  FDC read commands are treated as authoritative.
- The apparent battery collapse to approximately 3% was a display-calibration error, not a measured
  cell collapse: VBAT was about 3.74 V. The receiver's old universal 400 mV charging correction pushed
  that value to the bottom of the LiPo curve. Current working-tree code removes the inferred charging
  voltage offset and uses measured voltage directly, retaining `raw_charging` only as diagnostic data.
- The decisive physical finding was water trapped in pockets underneath the nail-polish blade
  coating. After the nail polish was completely removed, the prior drift reversed while drying and
  then settled to a very flat air baseline. This moves coating/edge water retention and soil/probe
  equilibration ahead of intrinsic FDC thermal drift as the leading field cause.
- Current repair plan: wait for 24-48 hours of stable dry-air data; seal the entire cut FR-4 perimeter
  with a narrow MG Chemicals 832HD epoxy U-bead overlapping 0.5-1.0 mm onto both solder-mask faces;
  cure; apply thin MG Chemicals 419D acrylic face coats; cure 48 hours; then run the staged air -> dry
  soil -> measured watering -> return-to-air validation. Do not trap residual moisture under epoxy.
- V4 should retain the accurate description "outer-layer copper under solder mask," reserve a 1 mm
  copper/via-free routed-edge band, avoid mouse-bite edges in the sensing region, fully depanelize,
  then use post-routing dielectric edge sealing. JLCPCB offers acrylic conformal coating and
  epoxy-filled vias, but no documented standard routed-edge epoxy process; conductive edge plating is
  not a substitute.
- Deployment boundary at documentation time: the last explicit receiver flash record is
  `MSV2 runtime build 2026-08-30 msv3-reference-normalization-v1`; the working tree now carries the
  newer `history-custom-date-range-v1` source identity and the battery correction removal. A live
  `/device` query was unavailable, so do not assume those later receiver changes are flashed without
  checking. Migrations 036-038 and their cloud/UI changes are also present in the working tree; the
  earlier deployment note still marks 036 pending, so verify linked migration/function state before
  claiming them live.
- Detailed records:
  `MSV3/docs/MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md`,
  `MSV3/docs/MSV3_FDC_THERMAL_DRIFT_ROOT_CAUSE_2026-08-30.md`, and
  `MSV3/docs/MSV3_FDC_REFERENCE_COMPENSATION_2026-08-30.md`.

## 2026-08-29 Bounded History Downloads for Dense Probes

- Root-caused Alocasia GP's failing seven-day History load: its >100,000 eligible rows forced
  roughly 100 internal Supabase RPC pages plus multiple large browser responses, even though the
  rendered chart is only 800 pixels wide.
- Migration 036 adds an authorized density probe and stable sampled history reader. The edge
  function now retains every row up to 2,400 points, then selects one real row out of every N
  across the frozen range. A 100,001-row window becomes 2,381 transmitted points (every 42),
  without deleting or rewriting stored history.
- The embedded History page requests the same 2,400-point budget, limits continuation attempts,
  guards an empty cursor page, and explicitly reports both stored and plotted counts.
- Deployment is pending: apply migration 036, deploy `plant-dashboard`, then compile/flash
  `MSV2 runtime build 2026-08-29 bounded-history-v1`.
- Full design and verification: `DisplayCenter/docs/HISTORY_DENSITY_DOWNSAMPLING_2026-08-29.md`.

## 2026-08-24 Centered Moisture Presentation Filter

- Root-caused Alocasia GP's blue line repeatedly falling below the raw centroid and later
  popping upward. Live sensor-3 history showed raw hourly medians around 898-903 while the
  presentation fell to 879-882. This was the asymmetric drying ratchet accepting low-side
  excursions while ignoring equivalent high-side samples; the later jump was the filter
  correcting its own accumulated bias.
- Replaced the receiver and browser-history implementations with a five-packet median, median
  absolute deviation, symmetric Q8 1/8 EMA, and symmetric 5%-of-span step cap. A robust-center
  watering rise still snaps after two confirmations, but snaps to the median rather than a raw
  transient peak.
- Replaced the obsolete deadband/stall History controls with robust-center-window and moisture-EMA
  controls. Bumped browser settings storage to `.v2`, so old ratchet settings cannot carry over.
- Expanded `scripts/verify_receiver_moisture_filter.mjs` with Alocasia-derived stationary-noise,
  symmetric-noise, isolated-spike, watering-step, and dry-down regressions. It and the battery
  parity verifier pass.
- Compiled at 2,202,076 bytes flash / 77,252 bytes RAM and flashed COM16 with LittleFS preserved.
  `/device` confirmed `MSV2 runtime build 2026-08-24 centered-moisture-filter-v1`, BLE scanning,
  cloud sync healthy, zero queue drops, and zero consecutive upload failures.
- Full analysis: `docs/MOISTURE_CENTERED_FILTER_FIX_2026-08-24.md`.

## 2026-08-23 Full-Directory Review Remediation and Live History Recovery

- **Live recovery at 17:00 PDT:** the History chart stall was ingestion, not rendering. The live
  receiver had a full `512/512` RAM queue, 2,186 dropped readings, repeated snapshot
  `connection refused`/`send payload failed` errors, and a pending display write retrying through
  the migration-019 HTTP 400 bug.
- Applied migration 033 to the linked Supabase project. Display sync immediately changed to HTTP
  200 and the old queue began draining, proving the one-second failing display/TLS loop was a
  direct contributor.
- Reduced receiver history batches from 32 to 16 rows, added exponential display-sync failure
  backoff, and reserve cloud TLS work for history whenever the queue is at least 75% full. Flashed
  `MSV2 runtime build 2026-08-23 live-history-recovery-v2` to COM16 with LittleFS preserved.
- Post-flash `/cloud` reported snapshot HTTP 200, display HTTP 200, zero drops, the 16-row batch
  size, and roughly 82-85KB free heap. A direct share-history read returned sensor-3 rows through
  `2026-08-24T00:00:31.187Z` (17:00:31 PDT); its temporary verification token was revoked.
- The 2,186 readings dropped before recovery and the remaining stale RAM queue discarded by the
  approved flash cannot be reconstructed. Current ingestion is live again.

- Closed the receiver's partial `sensors[]`/`autoCal[]` synchronization fix: BLE now publishes
  filter state and packet fields as one update, while render, `/data`, cloud, and auto-calibration
  callers take short value snapshots under the matching mutex. Duplicate advertisement frames are
  rejected before either the moisture or battery filter mutates.
- Fixed the moisture dry-side escape hatch so ordinary sub-watering upward jitter preserves
  accumulated dry-side evidence. The capped convergence step now occurs on the twentieth
  qualifying packet, with a host regression verifier covering symmetric alternating noise.
- Bumped the receiver identity to `2026-08-23 live-history-recovery-v2`; this source can no longer
  masquerade as the already-flashed `retroactive-history-smoothing-v1` binary.
- Root-caused the continuous display-setting POST HTTP 400 to the PL/pgSQL upsert's ambiguous
  `on conflict (receiver_id)` target. Migration 033 uses the named primary-key constraint;
  receiver telemetry now also includes the Edge Function error text on failures.
- MSV3 production defaults now disable serial logging/USB-host wait and the temporary overnight
  LED load. Real wet-soil threshold/IDRIVE tuning remains a physical validation gate.
- Automation now fails closed: Arduino wrappers throw on CLI failures and compile before upload,
  the orphaned SD helper no longer names a nonexistent default sketch, the agent supervisor cannot
  approve failed/timed-out required tests, dashboard verifiers discover the active bundle, and fresh
  Supabase setup runs through the highest migration (currently 033).
- **Deployment state:** migration 033 and the receiver recovery build are live. The separate MSV3
  production firmware has not been reflashed; wet-soil plus 24-48-hour soak checks remain external
  work.

## Historical: 2026-08-23 Receiver Cloud Reliability, Telemetry, and History Raw Overlay

- User reported periodic reboots, History-tab lag, and stuck moisture values. A same-day
  Claude+Codex collaboration session (`tools/agent-supervisor`) synthesized a 7-step plan
  but both implementation attempts died from `billing-safety.ts`'s `$2`/call budget cap —
  too low for a task this size. Worth raising before trying that route again.
- Of the plan, 3 steps were already correctly implemented in the working tree from the
  aborted run (cross-core `sensors[]` race fix, 60s upload-latency default with a backlog
  nudge, and the moisture filter's bounded stalled-wake escape hatch). Found and fixed one
  real bug in that work: a persisted `syncIntervalSeconds: 300` would never pick up the new
  60s default on reflash — added a one-time migration.
- Added step-2 telemetry: per-subsystem RTC checkpoints, an intentional-restart marker,
  live/min free-heap and largest-free-block tracking, BLE/cloud task stack high-water
  marks, a history-queue high-water mark, and a one-time-per-boot LittleFS boot record —
  all exposed under a new `health` object in `/device`. Fixed `resetReasonName()` missing
  ESP32-S3-specific reset reasons (first real reset came back "unknown" without this).
- Root-caused the live "connection refused" upload failures using that telemetry: all five
  cloud-network functions (bootstrap, display-settings, config-pull, auto-cal-pull,
  snapshot-upload) share a limited TCP/TLS resource pool, and only one of the five had any
  cleanup (`http.end()`/`stop()`) on its failed-connection path. Fixed all five; added
  exponential backoff on consecutive failures. A first attempt at a low-heap guard was
  raised too high (122880 bytes) without checking it against the receiver's real ~80-95KB
  normal operating range, and blocked 100% of uploads for a few minutes before being caught
  and reverted — documented in full in the detailed doc below, including the mistake.
- Confirmed Netlify (`msv2-plants.netlify.app`) is already serving the fixed `v15` service
  worker from the 2026-08-17 stale-cache bug — not a deploy gap. The screenshots in this
  investigation were actually the receiver's own local page (`MobileAppPage.h` at
  `10.0.0.35`), a separate frontend from the Netlify dashboard.
- Diagnosed (not yet fixed) a real weakness in the moisture filter's escape hatch: it resets
  its stalled-wake counter on any upward tick, even sub-deadband noise, so it can take far
  longer than "20 wakes" to fire against symmetric noise.
- Added a raw-value diagnostic overlay to the receiver's local History chart
  (`MobileAppPage.h`) — dashed line showing the unsmoothed `moisture` value alongside the
  existing smoothed `display_moisture` line, same timestamps, shared tooltip.
- Current live build: `MSV2 runtime build 2026-08-23 history-raw-overlay-v1`, flashed to
  `COM16`/`10.0.0.35`, compiled at 2,160,164 bytes flash / 74,940 bytes RAM.
- **Not done**: `displaySync`'s HTTP 400 (failing continuously, never investigated), the
  History tab's incremental-polling backend work (step 6, needs its own deploy sign-off),
  the escape-hatch fix itself, a real regression test for the filter, and — most
  importantly — a real multi-day soak. Only ~15 minutes of clean post-fix data exists as of
  this handoff.
- Full write-up, including the mistake made and corrected mid-session:
  `docs/RECEIVER_CLOUD_RELIABILITY_2026-08-23.md`.

## 2026-08-22 MSV3 High-Resolution Moisture Follow-Up

- Replaced the initial `(25M - raw) / 1000`-equivalent receiver mapping with
  `clamp_u16((24,000,000 - rawFdcCounts) / 100)`. The nail-polish-covered probe's measured
  20M dry -> 19M wet span now produces 40,000 -> 50,000: 10,000 useful derived steps rather
  than 1,000. The roughly 3,000-count bench noise becomes roughly 30 derived steps for the
  receiver presentation filter to suppress.
- Migration 029 adds `raw_fdc_counts`, `raw_moisture_khz`, and `moisture_scale` to durable
  history. The original 28-bit FDC value is now retained in Supabase, not just BLE/serial/
  local `/data`.
- Scale transitions are isolated automatically: the history insert RPC starts a fresh
  per-sensor calibration epoch when `moisture_scale` changes, and history readers chart only
  the latest scale. Old rows are preserved but cannot masquerade as a watering jump.
- Migration 029 and `receiver-snapshot` are deployed. Receiver build
  `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2` compiled at 2,133,296 bytes flash /
  58,508 bytes RAM and was flashed successfully to COM16 with hash verification.
- Live receiver verification: MSV3 raw `20,243,803` maps to measured/display `37,561` at
  scale 2, exactly matching the new formula.
- Live end-to-end verification after the scheduled cloud upload: HTTP 200; raw FDC
  `20,243,185` produced measured `37,568` while the smoothed display held at `37,561`.
  A forced normal auto-cal refresh then changed sensor 4's cloud-derived bounds from the
  old ~4,760 scale to `37,574`, proving the scale-2 history insert and automatic reset both
  executed successfully in production.

## 2026-08-22 Global Receiver Moisture Smoothing

- All probe generations now share one receiver-side presentation filter after protocol
  conversion: legacy calibrated packets, MSV2 raw-kHz packets, and MSV3 wide FDC packets.
  Normal upward jitter is ignored, drying follows downward through a learned deadband and
  capped step, and a significant rise snaps immediately as watering.
- Measurement truth is preserved. Receiver/API `moisture` is the smoothed display value and
  `measuredMoisture` is the unsmoothed derived value. Supabase history keeps the latter in
  its existing `moisture` column (so auto-calibration is unchanged) and migration 028 adds
  `display_moisture` for the user-facing curve. Old rows/firmware fall back compatibly.
- Migration 028 is applied, `receiver-snapshot` is deployed, and Netlify production is
  deployed as `app.v15.js` / `msv2-cloud-app-v15`.
- Receiver firmware `MSV2 runtime build 2026-08-22 receiver-moisture-smoothing-v1` compiled
  at 2,132,916 bytes flash / 58,508 bytes RAM and was flashed successfully to COM16 with
  hash verification. Live `/device`, `/data`, and `/cloud` checks passed; both cloud upload
  and display sync returned HTTP 200.
- Full design, threshold, data-contract, and rollout details:
  `docs/RECEIVER_MOISTURE_SMOOTHING_2026-08-22.md`.

## 2026-08-22 Adaptive Multi-Cycle Moisture Calibration

- Migrations 024-027 and `receiver-snapshot` are deployed. History is resampled into one
  median per five-minute bucket, so changing upload cadence cannot bias the learned range.
- A watering is now a supported time-series event, not one upward sample: two hours of
  pre-water context, three hours of post-water response, and 6-12 hours of retained
  elevation are checked. The probe-3 August 9 `1202 -> 851 -> 1282` transient and probe
  1's short square-wave plateaus are rejected; the retained August 17 probe-2/probe-3
  watering remains.
- `autocal.ts` combines all valid 21-day watering cycles. Five-minute distribution bounds
  are a weak live prior, events are weighted by evidence and seven-day recency, and after
  three cycles a median/MAD guard prevents one outlier from taking over. New events enter
  provisionally at 25% weight and are promoted or removed when their retention window
  fills, so every upload can improve the estimate without immediately trusting a spike.
- Current replayed model bounds are approximately probe 2 `164-182` and probe 3
  `888-1303`; raw readings `170` and `984` resolve to about `33%` and `23%`. Probe 3 has
  one confirmed cycle versus probe 2's two, so their reported model confidence is still
  intentionally moderate and will improve with additional real waterings.
- Moisture alert semantics now match on every surface: `11-100%` normal, `1-10%` steady
  yellow, and exactly `0%` red/flashing/buzzer. Battery thresholds are intentionally
  separate and unchanged. The periodic battery-percentage swings remain a separate open
  investigation; no battery correction was slipped into this moisture change.
- Netlify production is deployed as `app.v14.js` with service-worker cache
  `msv2-cloud-app-v14`. The receiver was compiled and flashed successfully on COM16; flash
  hash verification passed and `/device` confirmed build `MSV2 runtime build 2026-08-21
  watering-cycle-alerts-v1`. Live `/data` then resolved probe 1/2/3 to `15%`, `33%`, and
  `23%`, all normal rather than the stale firmware's red/yellow states.
- Nine auto-calibration regression tests pass, covering multi-cycle contribution,
  continuously changing priors, provisional evidence, multi-pour clustering, outliers,
  readiness, and shared-pot scaling. The auto-calibration response contract did not change;
  receivers still consume `ready`, `dryBound`, and `wetBound`. The alert-threshold firmware
  change does require a receiver flash, which the production receiver has now received.
- Full design and operations reference:
  `docs/TREEBEARD_ADAPTIVE_MOISTURE_CALIBRATION.md`.

## 2026-08-17 Netlify Service Worker Stale-Data Bug

User reported the Netlify dashboard (`msv2-plants.netlify.app`) kept showing old data on a
normal page reload, requiring a hard reload (Ctrl+Shift+R) every single time to see fresh
readings. This turned out to be a real bug in `web/plant-dashboard/sw.js`, unrelated to the
auto-calibration work also happening this session - found and fixed, but **left
undeployed** at the user's explicit request (Netlify pushes are on hold this session
alongside the pending `app.v13.js` cache-bust from the auto-cal redesign work - see that
section below).

- **Root cause**: the service worker's `fetch` handler had exactly two branches - the app
  shell (`/`, `/index.html`, navigations), handled correctly with a network-first
  `cache:"no-store"` fetch, and a catch-all fallback for literally everything else that was
  **cache-first**: `caches.match(request).then(cached => cached || fetch(...))`. That
  fallback branch caught the page's own `fetch()` calls to the Supabase `plant-dashboard`
  edge function (`/api/share-snapshots`, etc.) - the actual live moisture/battery/snapshot
  data - not just the intended static assets (plant GIFs, `app.vXX.js`, jsQR). Once that
  data URL was cached on first load, every normal reload kept serving that same stale
  response from Cache Storage indefinitely, since nothing ever invalidated or re-fetched it.
- **Why hard reload "fixed" it**: a forced/hard reload is a documented special case where
  the browser bypasses the active service worker entirely for that one navigation - the
  page temporarily has no SW controller, so *every* fetch it makes, including the data
  call, goes straight to network. A normal reload stays under the SW's control and kept
  hitting the same stale cache entry forever.
- **Fix** (`web/plant-dashboard/sw.js`): the fallback branch now only cache-firsts
  same-origin requests under `/assets/` (the actual versioned, immutable static assets -
  matches the `/assets/*` immutable-cache scoping already in `_headers`/`netlify.toml`).
  Everything else, including the cross-origin Supabase API calls, now goes straight to
  `fetch(event.request)` with no Cache Storage involvement at all - no caching, no
  fallback-to-stale, always live.
- Side effect (harmless, not acted on): `/manifest.webmanifest` and `/app-icon.svg` are
  still listed in the `install` handler's precache array but are no longer read from cache
  by the `fetch` handler either (they're not under `/assets/`) - matches those two paths'
  own `Cache-Control: no-store` in `_headers` anyway, so this is more consistent than
  before, just slightly redundant precache work. Not worth trimming for two tiny files.
- **Status: fixed in the working tree, not deployed.** Will ship in the same Netlify push
  as the `app.v13.js` cache-bust from the auto-cal redesign work whenever that's approved -
  see that section's own "not deployed" note.

## Current State

- ESP32 display runtime renders the plant grid, animated pixel art, BLE readings, diagnostics, and phone setup app.
- ESP32 display runtime was last flashed successfully to the display receiver on `COM16` (port numbering shifts between sessions - always run `arduino_display.ps1 -Action board-list` first) with:
  - `.\scripts\arduino_display.ps1 -Action upload -Port COM16`
  - Firmware build tag: `MSV2 runtime build 2026-08-22 msv3-hires-moisture-v2`.
  - Build size after flash: 2,133,296 bytes, 67% of program storage; 58,508 bytes (17%) of dynamic memory.
  - The header sun-button single-tap has been replaced (Codex, same day) with a double-tap-anywhere-on-dashboard gesture to open the brightness overlay - `TOUCH_DOUBLE_TAP_MIN_GAP_MS`/`TOUCH_DOUBLE_TAP_WINDOW_MS` in the `.ino`. `drawBrightnessButton()`/the header sun icon are gone. This freed up plain single-tap for the new tap-to-expand card detail view below. `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md` still describes the old single-tap-header-button scheme and needs a pass to catch up.
  - Local uncommitted working tree - none of the 2026-08-12 firmware/doc changes are committed yet (see `git status`/`git diff`).
- Phone app supports:
  - Status dashboard.
  - Plant setup.
  - Receiver WiFi setup.
  - Receiver pairing ID/claim code.
  - Config version display.
  - Read-only Cloud Status panel for automatic bootstrap, upload, and config-pull status.
  - Add/Delete plant setup controls that resize the local `cards[]` array before saving to the display.
  - Auto layout by plant count: 6 plants -> `3x2`, 7 plants -> `4x2` with the bottom row centered, 8 plants -> `4x2`.
- Receiver stores local config in LittleFS:
  - `/ui.json`
  - `/wifi.json`
  - `/device.json`
  - `/cloud.json`
- Receiver exposes:
  - `/app`
  - `/ui`
  - `/data`
  - `/snapshot`
  - `/device`
  - `/claim`
  - `/config-meta`
  - `/cloud`
- `/snapshot` returns the cloud-upload payload locally.
- `/snapshot` includes plant setup summary under `config.cards[]` for cloud/app display.
- Receiver-side snapshot upload is implemented behind `/cloud.enabled`.
- `/cloud` reports upload status:
  - last attempt time
  - last success time
  - last HTTP status
  - last error string
- Mock cloud tooling exists:
  - `scripts/mock_cloud_server.py`
  - `scripts/relay_snapshot_once.py`
- Mock cloud server has an auto-refreshing browser dashboard at `/` with plant names and moisture health bars when the latest receiver firmware has uploaded config card metadata.
- Mock cloud server also serves PWA shell routes: `/manifest.webmanifest`, `/sw.js`, and `/app-icon.svg`, plus iOS home-screen metadata.
- Cloud API draft exists at `docs/CLOUD_API_DRAFT.md`.
- Supabase hosted backend scaffolding exists:
  - `supabase/migrations/001_msv2_cloud.sql`
  - `supabase/functions/receiver-snapshot/index.ts`
  - `supabase/functions/plant-dashboard/index.ts`
  - `supabase/functions/claim-receiver/index.ts`
  - `supabase/migrations/002_msv2_store_snapshot_rpc.sql`
  - `supabase/migrations/003_msv2_read_latest_snapshots_rpc.sql`
  - `supabase/migrations/005_msv2_receiver_bootstrap.sql`
  - `supabase/migrations/006_msv2_receiver_bootstrap_fix.sql`
  - `supabase/migrations/007_msv2_hash_secret_bytea_fix.sql`
  - `supabase/migrations/008_msv2_pgcrypto_schema_fix.sql`
  - `supabase/migrations/009_msv2_household_share_tokens.sql`
  - `supabase/functions/receiver-bootstrap/index.ts`
- Claim/user access scaffold exists:
  - `supabase/migrations/004_msv2_households_and_claims.sql`
  - `msv2_households`
  - `msv2_household_members`
  - `msv2_receiver_claims`
  - `msv2_claim_receiver`
  - `msv2_read_user_latest_snapshots`
  - `supabase/config.toml`
  - `docs/SUPABASE_SETUP.md`
- Hosted `plant-dashboard` Edge Function is deployed and returned HTTP 200.
- `claim-receiver` and updated `plant-dashboard` Edge Functions are deployed.
- `plant-dashboard` Edge Function now provides the no-login household app APIs:
  - `/functions/v1/plant-dashboard/api/share-claim`
  - `/functions/v1/plant-dashboard/api/share-snapshots`
  - tested successfully with receiver `MSV2088A3104A7AC`
  - share snapshot read returned `plant_count: 2` with Basil and Mint readings
- Static no-login household app shell exists at `web/plant-dashboard/index.html`:
  - receiver connection with `receiverId` and `claimCode`
  - long random household share tokens
  - share token persistence in browser `localStorage`
  - household-scoped dashboard via the Supabase API endpoints
  - dark Plant Status Center visual style
  - animated plant GIF cards copied to `web/plant-dashboard/assets/plant_gifs/`
  - PWA files exist: `manifest.webmanifest`, `app-icon.svg`, `sw.js`
  - Netlify config exists: `netlify.toml`, `_headers`, `_redirects`
  - unsigned share snapshot API requests return `401 share token required`
- Important: Supabase Edge Functions returned HTML as `text/plain` with `Content-Security-Policy: default-src 'none'; sandbox`, so the browser app must be hosted as a static page elsewhere. Edge Functions should stay API-only.
- `plant-dashboard` root now returns an API-only JSON note instead of raw HTML.
- Static app package exists at `web/msv2-plant-dashboard-static.zip`.
- Netlify production deploy is live at `https://msv2-plants.netlify.app`.
- Receiver bootstrap scaffold exists so future firmware can register itself after WiFi setup and receive a per-device upload token.
- ESP32 firmware now has first-pass cloud auto-bootstrap:
  - calls `receiver-bootstrap` after station WiFi is connected
  - sends `receiverId`, private `receiverSecret`, `claimCode`, and build tag
  - stores returned `uploadUrl`, per-device `apiToken`, and interval in `/cloud.json`
  - leaves existing manual cloud config alone if an upload URL and token already exist
- ESP32 local app no longer exposes manual Cloud Sync URL/token editing in normal setup; use `GET /cloud` for status and `/cloud` API calls only for debugging/reset testing.
- Auto-bootstrap was tested successfully against hosted Supabase:
  - `bootstrap.lastHttpStatus: 200`
  - `bootstrap.lastError: ""`
  - `hasApiToken: true`
  - `syncIntervalSeconds: 60`
  - cloud sync enabled automatically
- Per-device-token snapshot upload was tested successfully against hosted Supabase:
  - `upload.lastHttpStatus: 200`
  - `upload.lastError: ""`
  - `upload.lastSuccessMs` populated
- Bootstrap SQL fix `006` removes ambiguity in `msv2_register_receiver` by using the primary key constraint in the upsert and `return query` for the returned token row.
- Hash SQL fix `007` updates `msv2_hash_secret` to call `digest(convert_to(..., 'UTF8'), 'sha256')` because the hosted database reported `function digest(text, unknown) does not exist`.
- Pgcrypto schema fix `008` sets `search_path = public, extensions` for the hash/register functions because Supabase reported `function digest(bytea, unknown) does not exist` when `pgcrypto` was not visible from the function search path.
- Firmware now includes bootstrap HTTP response text in `/cloud.bootstrap.lastError` when the cloud returns a non-2xx status.
- Current cloud config path: Supabase can store receiver configs and the ESP32 display can poll/apply newer versions, but the Netlify app hides remote editing for now to keep the hosted app read-only and lower traffic.
- WiFi/setup behavior is currently working well in bench testing:
  - First-time setup can still be tested with `-ForceWifiSetup`.
  - The forced setup build ignores saved WiFi until the setup app saves a choice.
  - Saving home WiFi or local-only mode writes `/wifi.json` and `/wifi-setup-unlock-v2.flag`.
  - After that save, resets should return to the saved mode rather than first-time setup.
  - Local-only mode keeps broadcasting the open `MSV2-Display` AP and returns to the dashboard.
  - If a phone sees `MSV2-Display` but cannot join, forget the saved phone network first because older firmware used password `moisture` for the same SSID.
  - BLE scanning was relaxed to reduce WiFi AP contention: 3-second scans with a 250 ms idle gap, and scan interval/window `120/40`.
  - The save/reboot screen now clears cached pixel-art animation state and suppresses display repainting while restart is pending, so old plant art should not bleed through the reboot message.

## 2026-07-06 Sprite/Art Pipeline Update

- Plant sprite generation was normalized so every generated ESP32 sprite frame is emitted on an `84x84` transparent canvas.
  - `scripts/generate_plant_sprites.py` now supports `--target-width` and `--target-height`, defaulting to `84`.
  - Frames are trimmed, aspect-preserving downsampled if needed, and bottom-aligned on the shared canvas.
  - The generated firmware/header/mobile previews now all use the same canvas dimensions, which keeps art scale consistent across plant choices.
- ESP32 sprite rendering was updated for better sparse-layout sizing.
  - `drawPlantSpriteScaled` and animation diff rendering now use shared sprite geometry.
  - Sprites can draw at crisp integer upscales when a sparse card has room, then fall back to `1x` or downsample for dense layouts.
  - This fixed tiny art in `1x1`, `2x1`, and `3x1` layouts while preserving dense grid fit.
- The ESP32 phone dashboard now caps plant setup at 16 plants.
  - Clicking `Add Plant` at the limit shows `Limit reached: the display supports up to 16 plants.`
  - Saving a config with more than 16 cards is blocked.
  - Embedded service-worker cache was bumped to `msv2-app-v2` so phones pick up the fixed `/app`.
- Two new source sprite sheets were added under `assets/`:
  - `GREEN-BLUE-PURP-PLANTER.png`
  - `YELL-ORG-RED-PLANTER.png`
- New helper script:
  - `scripts/generate_planter_sheet_gifs.py`
  - Detects exact plant components from those sheets using the published sprite sizes.
  - Converts black backgrounds to transparent.
  - Chooses 3 deterministic random color frames per plant by default to keep firmware size reasonable.
  - Upscales sheet-derived GIFs `4x` before the main sprite generator so they visually match the older normalized art after the generator's normal `0.5` scale.
- New generated source GIFs in `assets/plant_gifs/`:
  - `color_vine_pot.gif`
  - `color_grass_planter.gif`
  - `color_round_pot.gif`
  - `color_bonsai_bowl.gif`
  - `color_zigzag_pot.gif`
  - `color_hanging_vine.gif`
  - `color_hanging_clump.gif`
- The planter sheet GIF workflow is documented in `assets/plant_gifs/README.md`.
- Important size note:
  - Embedding all 18 color frames for all 7 new sheet-derived plants made firmware too large at about `112%` of program storage.
  - The final flashed build uses 3 frames per sheet-derived plant and compiles at about `2,059,353 bytes`, `65%` of program storage.
- Final regeneration/flash state:
  - Ran `python scripts\generate_planter_sheet_gifs.py`.
  - Ran `python scripts\generate_plant_sprites.py`.
  - Compiled successfully with `powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 compile`.
  - Flashed successfully to the ESP32 display receiver on `COM13` with `powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13`.

## 2026-07-07 Security Hardening Pass

A full-repo review (firmware, Supabase backend, scripts, web dashboard) found 46 issues, 6 critical. The critical/high findings were fixed and **deployed live** the same day — not just committed. Scoped specifically to preserve the no-login QR/claim-code onboarding flow unchanged; nothing here adds a step for the end user.

- **Supabase RPC lockdown** (`supabase/migrations/011_msv2_lock_rpcs_to_service_role.sql`, `012_msv2_revoke_public_execute.sql`): every RPC (`msv2_read_latest_snapshots`, `msv2_claim_receiver`, `msv2_register_receiver`, etc.) was reachable directly via `/rest/v1/rpc/<fn>` with just the public API key, completely bypassing every auth check written into the Edge Functions. Now locked to `service_role` only.
  - **011 alone did not work** — Postgres grants `EXECUTE` to the `PUBLIC` pseudo-role by default on function creation, and revoking from `anon, authenticated` specifically doesn't touch that. Verified live (`msv2_check_rate_limit`, granted only to `service_role`, still returned `204` to an anonymous request). 012 explicitly revokes from `PUBLIC` too and is the one that actually closed it — confirmed via direct `curl` against the live project, now returns `401`.
  - If you add a new RPC later: `revoke ... from public` on it, not just from named roles, or it's wide open regardless of what you did/didn't grant.
- Rate limiting added on registration/claim attempts (keyed by `receiver_id`), claim codes are now hashed at rest (were the one plaintext credential), and a real `msv2_revoke_household_share` RPC exists now (the dashboard's "Forget" button previously only cleared `localStorage`, never actually invalidated the token server-side).
- `receiver-snapshot`: removed the shared `MSV2_RECEIVER_TOKEN` master-token bypass and the unscoped fleet-wide snapshot listing (`GET` with no `receiverId` used to return the latest 20 snapshots across every household). See `docs/SUPABASE_SETUP.md` for the updated auth flow.
- Firmware:
  - `secureClient.setInsecure()` replaced with real TLS validation against the actual root CAs Supabase presents (Google Trust Services / GlobalSign), pinned as PEM constants in the `.ino`. Confirmed live against the real bootstrap/config-pull/snapshot-upload endpoints.
  - Local admin API (`/ui`, `/cloud`, `/wifi` POST/DELETE) used to have no auth and stayed open indefinitely, including forever in local-only AP mode. Now gated behind a 15-minute post-boot grace window (`adminUnlockUntilMs`) — covers every mode with one mechanism, zero added friction for actual setup (which happens in minutes), but reconfiguring later needs a power-cycle to reopen the window.
  - Fixed a stored-XSS gap in `MobileAppPage.h` (`card.sensor` and the art-key label fallback weren't escaped, unlike everything else in that file).
- Web dashboard: extracted the inline `<script>` in `index.html` to `assets/app.js` so a real CSP (no `unsafe-inline` on scripts) is possible; added CSP + baseline security headers across all three places Netlify config lives (`netlify.toml` x2, `_headers`); SRI-pinned the `jsQR` CDN script.
- **Migration history was out of sync with reality** on the live database — `supabase migration list --linked` showed 004-010 as unapplied even though their tables/functions were clearly live (device was actively using them). This came from applying SQL by hand in the dashboard editor in the past instead of `supabase db push`. Fixed via `supabase migration repair 001..010 --status applied --linked` (bookkeeping only, no schema change) before pushing 011/012. **If you hand-edit SQL again, remember to repair the tracking table afterward** or the next `db push` will try to re-run already-applied migrations and fail on non-idempotent statements like `create policy`.
- Incident during verification: a test `POST /ui` with an empty `{}` body to the live device wiped its plant card config (full-replace semantics in `writeUiJson`, not a merge). Recovered by pushing `ui/display_default.json` (generic herb-themed placeholder cards) via `push_display_ui.py`. The original custom names/art are still recoverable from the last cloud snapshot: `select snapshot->'config'->'cards' from msv2_latest_snapshots where receiver_id = 'MSV2088A3104A7AC' order by received_at desc limit 1;` — not yet done as of this handoff.
- Everything verified live end-to-end, not just compiled: firmware flashed to the physical device on `COM13`, migrations applied to the real Supabase project, both edge functions redeployed, web dashboard redeployed to `https://msv2-plants.netlify.app`. Commits: `825c1fc`, `9d959ec` (plus `451574c` checkpoint before the pass started).
- Not done in this pass (by scope, not oversight): the Medium/Low findings from the review (docs drift elsewhere, dead code in `plant-dashboard/index.ts`'s unused `renderApp()`, script robustness issues in `esp32_packet_loss_test.py`/`mock_cloud_server.py`, stale plant-art whitelist in the dashboard vs. the new `color_*` sprite keys, etc.). **All of these were picked up and closed in the 2026-08-13/14 full-repo review pass below** - `renderApp()` deleted, both scripts hardened, the art whitelist updated and deployed.

## 2026-07-24 Moisture Probe RC Sensing + Overnight Battery Test

First real end-to-end work on the capacitive moisture probe (XIAO nRF52840 Sense on `COM15`/`COM5`, board FQBN corrected to `Seeeduino:nrf52:xiaonRF52840Sense`) alongside the display receiver (`COM13`). Probe sensing: precharge `SENSE_PIN` (`D0`) HIGH, release to `INPUT`, time the RC fall to LOW as a proxy for dielectric constant/moisture.

- New standalone test sketch `arduino/MSV2_Moisture_RC_Test/` validates the RC read loop and battery read independent of the BLE sketch; kept in sync with `MSV2_Moisture_BLE_Sleep` on every sensing-parameter change.
- Battery reading (`PIN_VBAT`/`VBAT_ENABLE`, both on the Sense board's battery-charge divider) calibrated against a multimeter: schematic-nominal comp is 2.961 (`R1=1M, R2=510K`), empirically calibrated to `2.946`, within normal 1%-resistor tolerance of nominal.
- BLE packet-loss measured at the probe's 3s test cadence: **1.00% over 10 minutes** (198/200 wake cycles received) via `scripts/esp32_packet_loss_test.py`.
- New logging/charting tooling: `scripts/log_sensor_readings.py` (polls the receiver's `/data` and appends to CSV), `scripts/plot_sensor_readings.py` (renders two single-series line charts - moisture, battery - never dual-axis), `scripts/fetch_supabase_history.py` (pulls history via `supabase db query --linked`, bypassing the need for the receiver's own per-device token).
- **Cloud history pipeline added** so overnight data survives the laptop being off: `supabase/migrations/013_msv2_sensor_history.sql` adds an insert-only `msv2_sensor_history` table (the existing `msv2_latest_snapshots` is upsert-only by design, keyed on `receiver_id`, and only ever holds the single latest snapshot). `receiver-snapshot/index.ts` now inserts one history row per plant on every snapshot upload, in addition to the existing latest-snapshot upsert. The receiver's existing `scheduleCloudSyncSoon()` (15s-throttled fast-path on new BLE data) means history already lands every ~15-18s at the probe's current fast test cadence, with zero firmware cadence changes.
- **Display app gained a History tab** (`MobileAppPage.h`): per-probe, per-range (6h/24h/3d/7d) moisture + battery charts, auto-refreshing every 30s while visible, fetched through a new local `/history` proxy endpoint on the receiver (`handleHistoryProxy()` in the `.ino`) that forwards to `receiver-snapshot`'s `history=1` GET route using the receiver's own stored token.
  - Hit and fixed a real bug along the way: `http.getString()` on the ESP32 has a **hard ~31.6KB response-size ceiling** (confirmed consistent across requests, not a timeout) that silently truncates mid-JSON past ~218 rows. Real fix was pagination end-to-end (new `id`-cursor param on `msv2_read_sensor_history`, migration `014_msv2_sensor_history_pagination.sql`), 150-row pages assembled client-side in a loop - not a bigger buffer or longer timeout, since either risks the ESP32 blocking long enough to freeze the display again (see next bullet).
  - Also fixed: `SENSOR_RENDER_INTERVAL_MS` was throttling sensor-card redraws to once per 10s regardless of how fast new data arrived; dropped to 2000ms.
  - **Caused one real incident**: an early streaming-proxy attempt for `/history` hung indefinitely (chunked-transfer-encoding from the Supabase edge function wasn't being read correctly), which froze the whole display (WebServer runs in the same loop as rendering) until reflashed with a fix. Recovered by reflashing normally - a clean upload's own hard-reset was enough, no manual intervention needed. Root cause: don't hand-roll a chunked-stream proxy on this HTTPClient; use the same `getString()` pattern already proven elsewhere in this firmware (just watch the size ceiling above).
  - Separately, **opening a raw serial connection to the ESP32-S3 with DTR+RTS asserted can drop it into the ROM bootloader** (native USB-Serial/JTAG, not a real UART) and it won't boot the app until reflashed/hard-reset. Don't poll this board's serial for diagnostics; use its HTTP API instead.
- **LED scheme changed for tonight's battery test**: onboard RGB disabled (`ENABLE_STATUS_LEDS = false`); external LED on `D1` now pulses once per wake/advertise cycle (not per raw BLE packet - Bluefruit has no per-packet callback, and a literal ~25-blink strobe every 500ms would defeat the point of measuring the LED's own power draw).
- **Probe firmware stripped down for the overnight run**: GATT/peripheral-connection stack (`BLEDfu`/`BLEDis`/`BLEBas`, connect callbacks) commented out - this design only ever advertises and is never connected to, so none of it was reachable; the unused System-Off deep-sleep path likewise commented out (real future work, not deleted); `ENABLE_SERIAL_LOGS` set to `false` for the unattended run.
- **Precharge tuning saga** (all on `MOISTURE_PRECHARGE_US`, the fixed HIGH-drive duration before releasing `SENSE_PIN` to `INPUT`):
  - Original 500us was insufficient - baseline moisture jumped from ~12,000 to ~17,700us (much more consistent) after bumping to 10ms.
  - A "verified" precharge (drive HIGH in bursts, check via `analogRead` until near-VDD, cap at 60 steps) was tried and reverted: checking the charge means releasing the pin to read it, which starts the very discharge it's trying to avoid - confirmed live, it read `moisture=0` every cycle. Also surfaced (and left fixed, harmless either way) that `AR_DEFAULT` is a fixed 3.6V ADC range on this chip, not ratiometric to actual ~3.3V VDD, making a naive 98%-of-1023 threshold unreachable.
  - 20ms (further margin beyond the working 10ms) was tried and **caused touch response to invert** - reading collapsed far below baseline on contact instead of rising. Reverted to the validated 10ms. Notable complication discovered afterward: essentially all of this touch testing happened with the probe USB-tethered to the laptop (needed for flashing/serial), and tethering turned out to be a real confound (see below) - so the 20ms-specific causation is less certain in hindsight than it seemed at the time.
- **Major finding: USB tethering itself was injecting noise into the measurement.** The floating high-impedance `SENSE_PIN` during the discharge phase acts as an antenna; a USB-tethered laptop provides a ground-loop/mains-hum coupling path, and a human hand is a good antenna for that same ambient noise - explaining touch-dependent instability that had nothing to do with sensor hardware. Confirmed by the user unplugging USB entirely: readings immediately became clean and consistent. Likely explains some/most of the touch weirdness chased over the session; the resistor-value question below is now a robustness/uptime improvement rather than an active bug fix, since real deployment (battery power, no laptop nearby) is exactly the condition that already tested clean.
- **`MOISTURE_FALL_TIMEOUT_US` bug found via the above**: fixed at 50ms, which turned out to be too short once USB noise (which had been causing false-early threshold trips, i.e. artificially short readings) was removed - every sample was hitting the timeout and reporting `MOISTURE_INVALID` (`0xFFFF` = 65535), which reads as a plausible high value but means "no measurement." Bumped to 500ms (generous, not yet tightened - see next work).
- **Hardware finding once actually inserted in soil**: readings became wildly unstable (151 to 60,000+ observed across a handful of cycles - not physically plausible for real soil moisture). Diagnosed as very likely the DNC (unpopulated) pulldown resistor footprint: with no dominant, well-defined discharge resistor, the baseline discharge relies on undefined parasitic leakage, and wet soil's variable ionic conductivity can provide an unstable competing resistive path - the same category of effect as the finger-bridging artifact seen on the bench, but continuous and uncontrolled rather than a discrete touch event. A dominant pulldown (discussed: 10K over 1M, since a lower value both cuts discharge/uptime ~100x *and* is far less perturbable by marginal parasitic resistances in parallel, without costing sensitivity to the real capacitive signal, which is R-independent in relative terms) should fix this, but needs solder access, not firmware. **Not fixed yet** - deferred to tomorrow.
- **Tonight's run is a battery-life test only, not a valid moisture calibration run.** Moisture data is expected to be unreliable until the pulldown resistor is populated. The point of tonight is measuring the external LED's actual impact on overnight battery life; moisture/BLE/cloud pipeline all being exercised for real is a bonus, not the goal.
- Pre-existing, unrelated bug noticed in passing (not fixed, cosmetic, doesn't affect sensor data): the receiver's missed-packet counter wraps to a huge number (`~2^24`) on the first packet after any probe reboot, since the wake-counter-gap math doesn't account for the counter resetting to a small value. Isolated to the `missedTotal` diagnostic stat.

## 2026-07-25 Oscillator Sensing Port, Auto Mode, and the RC-Timing-Was-Never-Going-To-Work Postmortem

The 2026-07-24 run confirmed the one-shot RC precharge/discharge approach is fundamentally unfixable: no resistor value survives real, variable soil conductivity, since a single timed discharge has no way to reject noise or self-correct drift (see that section). Today replaced the sensing architecture entirely, rebuilt calibration around it (twice, as the scale kept moving), added a self-calibrating "auto mode" to stop hand-tuning thresholds altogether, reworked probe power behavior for a real overnight test, and fixed a real silent-failure bug in the config-save path. Everything below is flashed and live as of this handoff.

- **Oscillator sensing ported into `MSV2_Moisture_BLE_Sleep.ino`**, replacing `readMoistureFallUs()`/`readAveragedMoisture()` entirely. Architecture (bench-validated separately in `arduino/MSV2_Moisture_Probe_Test/` + `docs/MOISTURE_SENSOR_OSCILLATOR_REPORT.md`, same free-running-astable-plus-frequency-counting principle as the MSV1 555-timer proof-of-concept): free-run the sensing node (charge to HIGH, discharge to LOW, immediately recharge, repeat) and count cycles in a fixed window, rather than timing a single discharge. Median-of-5 × 1ms bursts (`OSC_SAMPLE_WINDOW_MS`/`OSC_MEDIAN_SAMPLES`) rejects scheduler-jitter outliers; total active sensing time per reading is 5ms (down from up to several hundred ms worst-case). **Requires a 100kΩ pulldown resistor populated on `SENSE_PIN`** (the earlier unpopulated/DNC footprint from 2026-07-24) — this is a hardware precondition the firmware can't compensate for.
- **Moisture value inverted at the source**: raw oscillator frequency runs backwards for a field called "moisture" (drier soil oscillates faster), so `readAveragedMoisture()` now transmits `MOISTURE_INVERT_CEILING (200) - frequencyKHz` instead of raw frequency, restoring the original "higher = wetter" convention for every consumer downstream.
- **Calibration recalibrated twice** as the transmitted scale moved (RC-µs hundreds/thousands → raw kHz 10-90 → inverted ~110-190) across every hardcoded dry/wet default: `Treebeard_ESP32_Display_Runtime.ino`'s 8-card default config and 4 fallback sites, `MobileAppPage.h`'s preset table, `ui/display_default.json`. Current defaults: dry≈120, wet≈175 (herb preset). **These remain provisional** — no real-soil calibration data exists yet (all bench validation used touch/hand proximity as a moisture stimulus).
- **Auto mode added** (`supabase/migrations/017_msv2_sensor_auto_calibration.sql`, `receiver-snapshot/index.ts`'s new `autocal=1` branch, `runCloudAutoCalPull()` in the `.ino`) to stop this recalibration treadmill: per-sensor moisture range self-learned from history using the 5th/95th percentile over a trailing 21-day window (not raw min/max — a single outlier or a probe-removal transient barely moves a percentile, whereas it would permanently redefine a raw max). Gated by a "ready" check (≥50 samples, ≥3 days of history) before trusting/showing a percentage — shows "Learning..." (web) / muted (physical display) until then, **falling back to the manual/preset range as a working estimate** while learning (not blocking to nothing), since that range is at least on the current sensing scale. Also detects watering events (≥15% of range rise within ≤3h, clustered within 12h into one event) and predicts a watering interval from the median gap of the last ≤6 events, driving an "overdue" flag that forces the existing danger-color styling. Setup tab gained a per-card "Auto-calibrate" checkbox that disables the now-irrelevant manual dry/wet inputs.
- **Battery reading smoothed** with an exponential moving average (`smoothedBatteryPercent`, 0.2 factor, same constant already used for the oscillator's own frequency smoothing) so the History tab's charging curve isn't dominated by per-sample ADC noise — still one real ADC read per wake, only the transmitted/charted value is smoothed.
- **Probe power rework for tonight's test**: measure/check cadence dropped to every 10s (cheap, ~5ms active), but actual BLE advertising (the expensive radio-on part) now only happens every 60s normally, or immediately if moisture swings ≥15 units between checks (a real watering/removal event, not sensor noise) — matches the "measure often, radio rarely" pattern already sketched in `docs/PRODUCT_ROADMAP.md`'s probe section. `wakeCount` now only increments on actual advertises so the receiver's missed-packet diagnostic still means "real over-the-air loss," not artificially inflated by the new power-saving skips. External LED no longer pulses per-advertise (pure battery cost, no signal value) — now blinks 3× per wake only while moisture is at/below a **fixed** threshold (134, matching the 25%-of-default-range "danger" cutoff used elsewhere) - fixed because the probe is advertise-only with no BLE receive path, so it has no way to learn the live auto-calibrated or per-plant manual range.
- **Physical display now blinks red** (500ms phase) instead of a static border for low/overdue cards, mirroring the probe's own alert. Had to add a forced ~500ms redraw path specifically while a card is blinking (`anyLowMoistureCardVisible()`), since the normal redraw gate only fires on new BLE data (`sensorDataDirty`) — which, under the probe's new 60s-normal-advertise cadence, would have made "blinking" look like a color change once a minute instead of an actual blink.
- **Real bug found and fixed**: manual dry/wet saves were silently failing and nobody noticed. Root cause was two compounding issues - (1) `isAdminActionAllowed()`'s 15-minute post-boot admin window (a deliberate security control from the 2026-07-07 pass - see that section) never refreshed, so any receiver that had been up for a while (exactly the overnight-test scenario) silently rejected every save with 401; (2) `MobileAppPage.h`'s `saveConfig()` mutated the shared `config` object (used by the Status tab's rendering) *before* the save's success was known, so a rejected save still looked like it took effect in the app while the receiver's file never changed - explaining "the app updates but the physical display doesn't." Fixed both: `isAdminActionAllowed()` now slides the window forward on each successful action (doesn't weaken the "must reboot to gain admin access at all" property - an already-expired window still can't extend itself), and `saveConfig()` now collects into a local candidate and only commits it to shared state after a confirmed 200, surfacing a parsed, readable error otherwise. Added a persistent hint in the Setup tab explaining the reboot-to-reconfigure behavior.
- **Known, deferred**: the Netlify-hosted `web/plant-dashboard` has its own hardcoded art catalog (`known` array in `assets/app.v11.js` + static GIFs in `assets/plant_gifs/`) completely separate from the firmware's embedded one - newer sprite keys (used by the current live cards, e.g. Scindapsus/Alocasia) aren't in either, so they silently fall back to `sprout_pot` there. Fix is known (update the array + add the GIFs) but **blocked on Netlify deploy minutes until end of month** - not done.
- **Not yet done**: real-soil calibration data (everything above used touch/hand-proximity bench testing); confirming the 100kΩ pulldown is actually populated on the deployed field probe(s), not just the bench rig; a solar-panel charging test planned for tomorrow in direct sunlight (motivated the battery smoothing work - panel only appears to charge in direct sun, not even slight shade).

## 2026-08-09 QSPI Power-Down Bug (Root Cause of the Battery Drain), Cadence Restored, and VDD-Coupling Confirmed in Moisture Sensing

Triggered by reviewing a week of Supabase history from a vacation trip: both field probes (Alocasia, Scindapsus) ran their batteries to 0% in ~4-6 days despite a 30-minute sleep/500ms-advertise duty cycle that should have supported many months on a 150mAh cell. Full data review published as an artifact; key findings and the fix below.

- **Root cause found: `powerDownExternalFlash()` was silently never running.** `Adafruit_SPIFlashBase::begin()` (bundled library, `Seeeduino:nrf52` core v1.1.13) enables the QSPI peripheral (`nrfx_qspi_init()` → `nrf_qspi_enable()`) *before* checking its hardcoded device table (GD25Q\*/S25FL\*/W25Q\*/AT25\*/MB85RS\* only). The XIAO nRF52840 Sense's actual onboard flash is a **Puya P25Q16H**, not in that table, so `flash.begin()` always returns `false` on this board - and since our code's old `if (!flash.begin()) { return; }` guard bailed out early with no matching `_trans->end()`, the QSPI peripheral was left enabled for the rest of the probe's life, every single boot, and the flash chip never received its `0xB9` deep-power-down command. This tracks almost exactly with real-world reports on this board family: `~1.3-1.5mA of idle current from QSPI left active` (Seeed/Nordic forum threads), matching our own inferred ~1.5-1.6mA average draw.
- **Fix**: `powerDownExternalFlash()` in both `MSV2_Moisture_BLE_Sleep.ino` and `MSV2_Moisture_BLE_Sleep_Alocasia.ino` now talks to `flashTransport` directly (`begin()` → `runCommand(0xB9)` → `delay(10)` → `end()`), bypassing `flash.begin()`'s device-table dependency entirely - `runCommand()` is a raw opcode transfer that doesn't need the chip to be "recognized."
- **Live-confirmed on real hardware**, not just reasoned from source: added a wake/sleep "ping" diagnostic (`logFlashDiagnostic()`) that sends `0xAB` (release from deep power-down), reads the JEDEC ID, re-sends `0xB9`, reads again - all in one boot, deliberately with no status-poll loop (avoids a known, unrelated QSPI-specific DPD-exit bug on this exact chip, `zephyrproject-rtos/zephyr#55543`). Captured on Scindapsus: `awake ID: 0x856015  asleep ID: 0x000000` - `0x85` is Puya's real manufacturer ID, `6015` matches the P25Q16H's type/16Mbit capacity, and it goes fully silent immediately after our own `0xB9` in the same boot. Proof, not inference.
- **Ruled out**: the onboard RGB LED being stuck on (`ENABLE_STATUS_LEDS=false` skips the off-write, and GPIO `OUT` defaults LOW = on for this active-low LED) was a live suspect earlier in this investigation but the user physically checked both probes - LED confirmed off. Not the cause.
- **New tooling**: `scripts/serial_capture.py` (a reconnecting serial logger - survives the COM-port renumbering during a DFU reflash, which a plain `arduino-cli monitor` session cannot) and `.claude/skills/flash-moisture-probe/SKILL.md`, documenting the whole capture recipe including two hard-won gotchas: (1) the capture script and the upload command cannot run concurrently - the DFU tool needs exclusive port access for its bootloader-touch handshake; (2) `while (!Serial) {}` is not a reliable "wait for a real terminal" gate on this board's TinyUSB stack (it resolves true too early post-reset) - use a flat `delay(1500)` instead, confirmed empirically. Added `BOOT_HOST_WAIT_MS`/the flat delay to both probe sketches for future bench debugging; one-time cost at boot only (`setup()` runs once per power cycle), harmless to leave in permanently.
- **Cadence restored**: `WAKE_PERIOD_MS` set back to `10000` (from `1,800,000`/30min) on both probes now that the QSPI leak is fixed, reactivating the dormant `bigSwing`/`MIN_SWING_ADVERTISE_INTERVAL_MS` immediate-advertise-on-watering logic (`ADVERTISE_INTERVAL_MS` was already `60000`, so this was the only constant that needed to change). Both flashed and live-confirmed via Supabase showing the wake counter advancing every ~60-70s as expected. This also doubles as a real-world overnight battery test: computed active-time budget is ~38x higher than the 30-min config, expected to cost ~40-60 days of battery life at this cadence (vs. the theoretical 100s-of-days at 30min) if the fix is genuinely working, vs. an obvious ~8-12% overnight drop if the QSPI leak somehow persists. **Alocasia (~96-100% battery at test start) is the meaningful test; Scindapsus is still critically low (0-3%, unaddressed since ~Aug 7) and isn't a fair test right now** - needs an actual recharge first.
- **Scindapsus quantization rechecked over a fresh 5-day window - artifact persists, but this is expected and unrelated to the QSPI fix.** 67.6% of all 3,945 readings over the 5 days are the single value 222 (pinned near the sensor's ceiling). Root cause is the same one already known: Scindapsus's battery has been at/near 0% continuously since ~Aug 7, and this design has no reference cancellation against VDD drift, so a critically low/unstable supply corrupts the oscillator reading. Battery needs an actual recharge before this probe's moisture data means anything.
- **New finding this session, beyond quantization**: VDD/battery-state coupling into the moisture reading isn't limited to "battery critically low" - **active charging also visibly corrupts the reading** (large jumps observed correlating with charge events), via the same underlying mechanism (charge-current IR drop + possible charger switching noise disturbing the shared VDD rail that the GPIO-timed oscillator has zero rejection of). Root-caused precisely: the oscillator's timing is ratiometric to VDD (GPIO HIGH = VDD, input threshold = a fraction of VDD) with no independent reference, unlike the planned FDC2212 replacement, which measures LC-tank resonant frequency (a property of L and C only, not voltage) against a stable reference *clock* - a different measurement domain entirely, not just "a reference exists." Full mechanism writeup and field evidence added to `docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md`'s 2026-08-09 section - this is concrete confirmation of that doc's existing "battery-voltage drift" rationale, not a new problem.
- **Not yet done**: check tomorrow morning whether Alocasia's overnight battery drop matches the "fix is working" prediction (<~1-2%) or the "still broken" one (~8-12%); recharge/swap Scindapsus's battery; bench-check Scindapsus's BQ25100 `~CHG` pin for the noise hypothesis that explained its erratic battery% readings (still open, see the vacation-review memory); consider a firmware-side mitigation of discarding/flagging moisture samples taken while charging or below a battery floor, independent of the FDC2212 hardware timeline; the receiver's `DEFAULT_STALE_SECONDS` (currently uncommitted locally at `3900`, sized for the old 30-min probe cadence) needs revisiting now that probes are back on ~60s - not done as part of this session, flagged for whoever picks that file back up.

## 2026-08-11 Home-Screen QR Scanner Repair and Production Deploy

- Root-caused the missing iPhone camera prompt: the live scanner waited for the jsQR CDN fallback before calling `getUserMedia()`, and its SRI value did not match the actual pinned jsQR 1.4.0 response. Browsers rejected the decoder, the camera line was never reached, and a catch-all message misreported the decoder failure as camera unavailability.
- The reviewed decoder is now vendored as `web/plant-dashboard/assets/jsQR-1.4.0.min.js` with its Apache 2.0 license and correct SHA-384 pin. The production CSP no longer permits jsDelivr, and all three Netlify header sources explicitly allow `camera=(self)` while disabling microphone access.
- Camera permission is now requested directly from the Scan QR click before decoder initialization. Permission, missing-camera, busy-camera, preview, and decoder failures have distinct user-facing guidance. Live scanning is throttled and downsampled to reduce iPhone CPU load, and camera tracks are released on Stop, page hide, or navigation.
- Added a `Scan Photo` fallback using the phone's native image/camera picker. The selected image is decoded locally; nothing is uploaded.
- Cache-safe upgrade: application code moved to `assets/app.v11.js`, the service-worker cache moved to `msv2-cloud-app-v11`, and both the versioned app and decoder are precached. This avoids the previous one-year immutable `assets/app.js` URL trapping existing Home-Screen installs on stale code.
- Local verification command: `node scripts/verify_plant_dashboard_qr.mjs`. It launches a hidden headless Edge instance against localhost with a fake camera and verifies production CSP/permissions policy, v11 service-worker precaching, camera-before-decoder ordering, SRI-checked same-origin decoder load, MediaStream release, denied-permission guidance, and end-to-end decoding of `scripts/fixtures/msv2-pairing-qr.png`. No real claim request is sent; the verifier intercepts it locally.
- Deployed to the existing `msv2-plants` Netlify production site on 2026-08-11 as deploy `6a7c09d1a8e26d5ff34c2c9b` after the user approved publishing. Verified both the immutable deploy URL and `https://msv2-plants.netlify.app`: root HTML references `app.v11.js`, CSP and camera policy are correct, service worker reports v11 with `no-store`, and the live app/decoder hashes exactly match the locally tested bytes.
- Remaining validation: confirm on the real iPhone that the existing Home-Screen v10 installation upgrades, the first-use iOS prompt appears, the rear-camera preview and QR recognition work, the photo fallback works, and Stop/reopen releases and reacquires the camera normally.

## 2026-08-12 Touch/Brightness UI Consistency and Display Reliability Fixes

Started as a UI-consistency pass on the touch/brightness feature Codex had added (see
`docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`), then expanded into a real reliability
investigation once physical testing turned up genuine display corruption. Everything below is
flashed and user-confirmed working as of this handoff. Session ran collaboratively with Codex
(the user ran both in parallel, feeding each round's results to the other).

**UI consistency and touch-control bugs (first pass):**

- The new touch UI (brightness modal, calibration screens, WiFi setup screens) was drawn with
  hardcoded Adafruit_GFX-style primary colors (neon green `0x07E0`, pure red `0xF800`, etc.)
  instead of the brand palette every other screen uses. Rebased the shared `TFT_*` palette
  constants onto the exact web-dashboard tokens (`web/plant-dashboard/index.html` `:root`) and
  renamed them to match (`TFT_ACCENT`/`TFT_WARNING`/`TFT_DANGER`/`TFT_SURFACE`/`TFT_MUTED`) so a
  future edit can't silently drift back to neon colors.
- `+`/`-` brightness buttons were snapping straight to 100%/0% instead of stepping by 10. Root
  cause: the live slider-drag hit-region and the release-time button hit-regions used different,
  independently-hardcoded coordinates that overlapped by ~10px on the button's inner edge - any
  touch sample landing there mid-tap latched a "user is dragging" flag that made the release
  handler skip button detection entirely. Fixed by excluding the exact button hit-boxes from the
  drag-preview check.
- Dragging the slider flashed the whole modal panel - `drawBrightnessOverlay()` repainted the
  entire panel on every touch sample. Split into `drawBrightnessSliderRow()` for live updates
  (just the value text, track, fill, and knob) so a drag redraws a small strip instead of the
  whole modal.
- The low-moisture blink cadence (every 500ms) was redrawing every visible card, not just the
  blinking one(s) - `renderDashboardDataOnly()` now takes a `blinkOnly` flag and
  `slotNeedsBlink()` scopes it down.
- Auto-cal-learned moisture bounds/overdue flags were only ever applied on a full `renderUi()`
  rebuild, which almost never happens again once cards are on screen (see the `displayDirty`
  call sites) - a background auto-cal update could sit applied-but-invisible for a long time,
  then suddenly appear the moment something incidental (like closing the brightness overlay)
  forced a full re-render, looking like closing the menu itself caused a plant to flip to
  "overdue". Fixed with `refreshSlotMoistureBounds()`, called from the lightweight per-tick path
  too.

**Display corruption investigation (second pass, triggered by physical testing after the above):**
full write-up with code-level detail in `docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`
(status: RESOLVED). Summary:

- Symptoms: random red pixels in the animated plant art, frames occasionally looking like two
  poses superimposed, and (briefly, mid-investigation) one card's sprite flashing to a
  *different* card's sprite for about half a second. All started specifically after the
  touch/brightness feature was added - confirmed via `git diff HEAD` that the animation
  rendering code itself was untouched, pointing at an interaction with the new touch traffic on
  the shared SPI bus rather than a bug in the (unchanged) animation code.
- Root cause 1: `tft.writePixel()` issues a full column/row-address-set + memory-write command
  sequence (~13 bytes, several DC pin toggles) for *every single pixel*. The old animation
  renderer called this once per changed (diff) or opaque pixel, so a single frame update could
  issue hundreds of separate command sequences within one held SPI transaction - both slow and,
  per Codex's review, a large surface for a leaked/misinterpreted byte to land somewhere visible.
  Fixed by replacing the whole renderer (`drawPlantSpriteScaled`/`drawPlantSpriteDiffScaled`,
  both deleted) with a single `drawPlantSpriteComposited()` that resolves an entire row into a
  buffer (transparent pixels substituted with the background color) and sends it as one
  `writeAddrWindow()`+`writePixels()` burst - one command sequence per row instead of one per
  pixel, and no separate `fillRect()` pre-clear (eliminates the visible clear-then-rebuild flash
  a full redraw used to cause).
- Root cause 2 (found by Codex): `XPT2046_Touchscreen` runs in polling mode without an IRQ pin
  (deliberately, per an existing code comment - the library's own interrupt-based gating was
  found unreliable on this board), which means *every* `touchScreen.touched()` call performs a
  real SPI transaction, forever, whether or not a finger is on the glass - continuous idle bus
  traffic that didn't exist before touch was added. Those idle transactions could leak bytes into
  whatever ST7796 address window the display renderer had last left open, landing at a
  deterministic position (explaining the "2 pixels always in the same spot on every card"
  symptom precisely). Fixed with a passive `digitalRead(TOUCH_IRQ) == HIGH` check in
  `handleTouchInput()` before ever touching the SPI bus - skips the transaction entirely while
  idle (T_IRQ is active-low), but keeps polling once a touch is in progress so release detection
  still works. This is a level check re-evaluated every poll, not an attached interrupt, so it
  doesn't have the missed-edge failure mode the original no-IRQ design was written to avoid.
- Also closed two related but distinct redraw-coverage gaps found along the way, both in
  `drawPlantCardDataOnly()` (the per-tick fast redraw path, as opposed to a full `renderUi()`
  which repaints everything at once): (a) the art region's bottom edge and the "Moist" row's own
  clear rect didn't quite meet (a persistent-artifact risk, any stray pixel landing in the gap
  could never self-heal), and (b) the moist and battery rows were *always* fully cleared and
  repainted on every low-moisture blink tick regardless of whether either value had actually
  changed - including the battery row, which has nothing to do with a moisture alert. Both
  fixed: gaps closed with `min()`-based clamps that only ever extend coverage, and each row now
  caches its last-drawn value/color per `PlantArtSlot` and only repaints when something about it
  actually changed (the moisture row's *color* still updates every blink tick as the alert
  signal, but without the clear step, since same-width text/bar redraws cleanly in place).
- SPI clock (`DISPLAY_SPI_HZ`) was also dropped from the Arduino_GFX ESP32 default of 40MHz to
  10MHz as a signal-integrity margin trade, and the touch poll interval widened from 16ms to
  32ms - both cheap, low-risk changes tried before the two root causes above were found; kept in
  place since neither was shown to hurt anything, even though the T_IRQ fix turned out to be the
  one that actually mattered.

## 2026-08-12 Tap-to-Expand Plant Card Detail View

Built on top of Codex's same-day double-tap rework above (brightness overlay moved off the
header button onto a double-tap gesture specifically to free up plain single-tap for this).
Compiled clean and flashed to the display receiver on `COM16`; not yet physically tapped/verified
on the real touchscreen as of this handoff.

- Tapping a plant card now expands it in place into a diagnostics view - same card footprint
  (`x`/`y`/`w`/`h`), same rounded-rect/border/name-row/Moist/Batt-bar chrome as the normal card,
  with extra rows filling the region the plant sprite would otherwise occupy. Tapping again
  (the expanded card or anywhere else on the dashboard) collapses it back to normal. Only one
  card can be expanded at a time; only one plain tap is needed (not a long-press or a
  gesture) since double-tap is now reserved for the brightness overlay.
- New `drawPlantCardDetail(PlantArtSlot& slot)` in the `.ino` - self-contained (no split
  full/data-only variant like `drawPlantCard`/`drawPlantCardDataOnly`, since only one card is
  ever expanded, a full repaint of just that card on every refresh is cheap). Content density
  scales with the card's actual size using the *same* geometry `drawPlantArtInCard()` already
  uses to size the sprite (`y + 24`, height `max(24, h - 91)`) - the diagnostic rows are quite
  literally drawn into the space the art would have used, so bigger cards (fewer plants
  configured) automatically show more rows, and row spacing (`clampInt(infoH / N, min, max)`)
  stretches to fill taller cards instead of leaving them sparse. Below the `h < 76` compact
  threshold (already used elsewhere for the smallest grid size) it falls back to the existing
  compact Moist/Batt layout plus a small RSSI-quality dot in the corner, since there's no room
  for anything else. Wider cards (`w >= 200`, roughly 1-2 cards per row) get a two-column
  stat grid close to the phone app's Diagnostics tab (Probe, RSSI, Age, Wake, Received, Missed,
  Loss%, dry/wet range, plus raw oscillator kHz/charging state when the probe firmware sends
  raw fields); narrower cards get a single stacked column with fewer rows.
- Touch-state plumbing (`handleTouchInput()` in the `.ino`): a tap-up that *doesn't* get
  completed into a double-tap within `TOUCH_DOUBLE_TAP_WINDOW_MS` is a genuine single tap, but
  that can only be known once the window has passed with no second tap. New
  `resolvePendingDashboardTap(now)`, called every `loop()` iteration (not gated on the touch
  controller being polled, since the panel can sit idle with no finger on it for the whole
  window), checks elapsed time against `lastDashboardTapMs` and then calls
  `handleDashboardSingleTap(x, y)`. The tap's coordinates are stashed into dedicated
  `pendingSingleTapX/Y` at tap-up time rather than re-read from `lastTouchX/lastTouchY` when
  resolved later, because a second, unrelated gesture starting before the window elapses would
  otherwise overwrite those first.
- `handleDashboardSingleTap(x, y)`: if a card is already expanded, any tap collapses it;
  otherwise it hit-tests `artSlots[]` (already has each visible card's live `x/y/w/h`) and
  expands whichever card the tap landed on. Respects the same `wifiSetupPortalActive`/
  `pendingRestartMs` guard the double-tap path already used.
- `PlantArtSlot` gained a cached `plant[24]` name field (the detail view's periodic refresh
  path only has the `PlantArtSlot`, not the original JSON `card` object, to read from).
  Factored the plant/label/name/title-with-"Probe N"-fallback resolution logic that used to be
  inline in `drawPlantCard()` into a shared `resolvePlantName()` used by both
  `drawPlantCard()` and `cachePlantArtSlot()`, so the name shown in the detail view can never
  drift from the one shown on the normal card.
- `expandedCardSlot` (an `artSlots[]` index, -1 = none) is cleared automatically in `renderUi()`
  if a config reload leaves it pointing past the new card count - otherwise a reconfigure to
  fewer cards, then back up, could make an unrelated card appear "expanded" the next time the
  count grew back to include that index again.
- `renderAnimatedPlantArt()` skips the expanded slot (no sprite is drawn into the detail view,
  so there's nothing to animate there); the periodic sensor-render gate in `loop()` now also
  fires while a card is expanded (reuses the existing 2s `SENSOR_RENDER_INTERVAL_MS`, no new
  constant) so the Age/RSSI rows don't go stale for a full minute between real BLE updates.
- Not yet done: physical confirmation that a single tap on the real touchscreen expands the
  right card, that the double-tap-for-brightness gesture still works cleanly alongside it (no
  accidental double-tap-detected-as-single-tap-then-second-tap-expands-a-card ambiguity in
  practice), and that the smallest (4x4, 16-card) grid size's compact+dot fallback is legible.
  `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md` also still needs a pass to document the
  double-tap gesture and this feature - not done as part of this session.

## 2026-08-12 Surgical-Redraw Skill and Detail-View Refresh Fix

Follow-up to the tap-to-expand detail view above, after the user physically tested it: the
diagnostic content itself looked right, but the periodic refresh was visibly repainting more
than it should ("refresh issues"), and the header IP address had drifted left. Both fixed,
plus a new standing rule for all future display work. Compiled clean and flashed to `COM16`;
not yet re-verified on the physical screen as of this handoff.

- **New project skill: `.claude/skills/display-surgical-redraw/SKILL.md`.** Codifies a core
  tenet for this firmware going forward: once a display feature is on screen and just
  updating over time, its redraw path must only repaint the pixels that actually changed -
  never a whole card or the whole screen as routine/steady-state behavior, full repaints
  reserved for genuine one-time "this just appeared/changed layout" events. Grounded in real
  history, not just style preference: the shared ST7796/XPT2046 SPI bus (see
  `docs/DISPLAY_TOUCH_BRIGHTNESS_IMPLEMENTATION.md`) turned "too many writes" into an actual
  correctness bug once already (`docs/DISPLAY_ANIMATION_ART_CORRUPTION_2026-08-12.md`), and a
  full-panel repaint on every touch sample is exactly what made the brightness slider look
  frozen/flashing before `drawBrightnessSliderRow()` split it out from
  `drawBrightnessOverlay()`. Documents the established full-draw/data-only function-pair
  pattern (`drawPlantCard`/`drawPlantCardDataOnly`, `drawBrightnessOverlay`/
  `drawBrightnessSliderRow`, and now `drawPlantCardDetail`/`drawPlantCardDetailDataOnly`
  below) with a checklist for applying it to the next new feature. Load this skill before
  writing or reviewing any `drawPlantCard*`/`render*`/`draw*` function in this firmware.
- **Root cause of the detail-view refresh issue**: the first cut of the feature had only one
  drawing function, `drawPlantCardDetail()`, and the periodic refresh path (`loop()`'s
  `detailOpen` gate, every ~2s while a card is expanded, plus every low-moisture blink tick)
  called it directly - meaning a full `fillRoundRect` background repaint, a full info-band
  `fillRect`, and every diagnostic row got redrawn unconditionally on every single tick, even
  when nothing had changed. This is the exact anti-pattern the new skill exists to catch.
- **Fix**: added `drawPlantCardDetailDataOnly()`, the data-only counterpart. Never fills the
  card background or the info band; only the border re-strokes unconditionally (cheap, matches
  `drawPlantCardDataOnly()`'s own existing precedent). Every diagnostic row (signal
  label, RSSI, age, wake, loss, received, missed, moisture range, raw oscillator/charging) is
  diffed against per-slot cached *formatted text* (new `PlantArtSlot` fields,
  `lastDetailRssiText` etc. - caching the formatted string rather than the raw value avoids a
  separate "how is this bucketed for display" logic path that could drift out of sync with
  what's actually on screen) and only clears+redraws via a new
  `drawDetailStatRowIfChanged()` helper when the text actually differs. Moist/Batt rows reuse
  the exact same diff-and-redraw logic (and the same cache fields) as
  `drawPlantCardDataOnly()`'s own moist/batt rows. `renderDashboardDataOnly()` now calls
  `drawPlantCardDetailDataOnly()` for the expanded slot on every periodic tick;
  `drawPlantCardDetail()` (full paint) is only reached from `renderUi()`'s full rebuild, i.e.
  the moment a card is first expanded or the layout otherwise changes.
- To keep the two draw functions from silently drifting apart on what a row's text/color
  should be (a real risk once the logic exists in two places), extracted all the
  formatting/computation into a shared `computeDetailStats()` filling a new `PlantDetailStats`
  struct, called by both functions; `seedDetailCache()` (called at the end of the full paint)
  seeds every field the data-only path diffs against. `PlantDetailStats` had to be defined up
  near `PlantArtSlot`/`DashboardGrid` (not next to where it's actually used) - same reason
  those two already have manual prototypes at that spot: Arduino's auto-generated-prototype
  insertion point is before any struct definitions further down the file, so a function
  signature referencing a not-yet-declared struct type fails to compile otherwise. Hit this
  exact error on the first compile attempt, moved the struct, fixed on the second.
- **IP address position fix**: the header's `App <ip>` text was right-anchored at
  `tft.width() - 50` - a margin sized to clear the old header sun button. Now that Codex's
  double-tap rework removed that button entirely (see the section above), the reserved 50px
  was just empty space, making the address look like it had "slid left." Changed to
  `tft.width() - 8`, matching the title's own 8px left margin for a symmetric header.
- **User-confirmed working on the physical receiver 2026-08-12**: the expanded card's periodic
  refresh no longer flashes, and the header IP address is back in the top-right corner.

## 2026-08-13/14 Full-Repo Hostile Review Pass

User asked for a from-scratch, "reviewer on a bad day" pass over the entire repo -
every Arduino sketch, every Python script, the Supabase SQL/edge functions, and the
web dashboard - not a single feature. Done as 8 parallel read-only review agents (one
per area), triaged by hand, fixed, compile/syntax-checked, then committed and
deployed end-to-end the same session. Everything below is live as of this handoff,
not just committed - see the deploy status note at the end of this section.

**Probe firmware:**

- `MSV2_Moisture_BLE_Sleep_Alocasia.ino` was missing the charge-settle poll-loop fix
  its own header comment claimed was already ported from Scindapsus -
  `countOscCycles()` drove the plate HIGH and released it to `INPUT` with zero wait,
  unlike Scindapsus's validated poll loop (same "locks onto a value, then jumps"
  quantization risk this exact fix already closed once on the other probe). Fixed
  in the file, compiled clean. **Not yet flashed to the physical Alocasia board** -
  user said to do that later, not part of this session's hardware work.
- `MSV2_Moisture_Probe_Test.ino` (the bench reference this sensing approach was
  ported from originally) had drifted stale, still reproducing two already-fixed
  real-firmware bugs (`SAMPLE_WINDOW_MS=1`, a fixed 40-NOP charge-settle delay).
  Updated to match the deployed, validated approach so future bench A/B comparisons
  aren't measuring already-fixed bugs.

**Display receiver (`Treebeard_ESP32_Display_Runtime.ino`, `MobileAppPage.h`) - flashed
and user-confirmed working on the physical receiver (`COM16`):**

- `isAdminActionAllowed()` used a raw `millis() >=` comparison instead of the
  rollover-safe idiom used elsewhere in the file - would have silently reopened the
  unauthenticated local admin API after ~49.7 days of uptime. Fixed.
- `DEFAULT_STALE_SECONDS` was still `3900` (sized for a stale 30-min probe-cadence
  assumption, flagged as open in the "Suggested Next Work" list below as of
  2026-08-09) - dropped to `180` to match the current ~60s cadence. **This closes
  that open item.**
- `displayDirty`/`staleSeconds` globals made `volatile` (cross-core race between
  the cloud-sync task and the render loop); dry/wet moisture bounds now clamped to
  prevent a signed-overflow path in `moisturePercent()`.
- `MobileAppPage.h`: `saveWifi()`/`forgetWifi()` were dumping raw JSON error bodies
  into the UI instead of the parsed `error` field `saveConfig()` already used
  correctly (new shared `parseErrorText()` helper now used by all three);
  `loadReadings()`/`loadWifi()`/`loadClaim()`/`loadCloud()` had no error handling,
  so a dropped connection could leave the status text frozen on a stale
  "Updated HH:MM:SS" forever; added a duplicate-Probe-ID check before save; fixed
  an inconsistent HTML-escaping gap on RSSI/moisture/battery values.
- Replaced the pairing QR code's dependency on a third-party rasterizing service
  (`api.qrserver.com`) with a vendored, fully client-side QR encoder
  (`kazuhikoarase/qrcode-generator`, MIT, verified via direct download plus a
  minified-vs-unminified byte-comparison test before embedding, +21KB flash / 67%
  total). The third party was free and worked over the internet, but was
  completely unreachable when viewing the pairing screen over the receiver's own
  local-only AP (a normal supported mode) - the QR silently never rendered there.
  **User confirmed on the physical receiver post-flash: dashboard renders fine and
  the new on-device QR code looks good.**

**Other Arduino sketches:**

- `MSV2_BLE_Adv_Receiver.ino`: bounded the `while(!Serial)` boot wait to 3s (was
  the only sketch in the repo with an unbounded one); fixed the missed-packet
  counter's wraparound-on-reboot math (same fix already live in the main
  receiver - this is the "cosmetic, not fixed" item first noted in the
  2026-07-24 section, closed here for this sketch; the main receiver's own
  version turned out to already be fixed on inspection, just undocumented).
- `Treebeard_ESP32_DeviceIdentity_Dump.ino`: `LittleFS.begin(true)` was formatting the
  filesystem on the exact mount-failure case this "never writes/formats" rescue
  tool exists to recover data from. Changed to `begin(false)`.
- `README.md`: TFT pin table (DC/RST/CS) was cyclically swapped vs the real
  firmware constants - would have misdirected anyone wiring new hardware.

**Supabase SQL - migrations 019, 020, 021 all pushed to the live project and
functionally verified (not just deployed):**

- 019 (`msv2_receiver_display_settings`) had been written and committed in an
  earlier session (`516838a`) but, it turns out, never actually pushed -
  `supabase migration list --linked` showed it missing remotely; confirmed this
  wasn't the known migration-tracking-drift class of issue (see 2026-07-07
  section) by querying `pg_proc` directly and confirming its functions genuinely
  didn't exist live yet, so this was a clean push, not a repair situation.
- 020: `msv2_read_share_sensor_history` and `msv2_clear_share_sensor_history` were
  the only two RPCs in the schema's history still granted execute to
  `anon, authenticated` - every sibling RPC has been `service_role`-only since the
  2026-07-07 hardening pass, so these two were reachable directly via the public
  anon key, bypassing the edge function's auth/CORS layer entirely. Locked to
  `service_role` to match. Also adds a missing index for the id-cursor
  sensor-history pagination introduced in 014/018 (was doing a full scan).
- 021: mitigates (does **not** fully close - see the migration's own SQL comments
  for the full reasoning) the `receiver-bootstrap` registration race, where
  `receiver_id` is derived directly from the device's own MAC address and is
  passively observable by anyone in WiFi range of its setup AP before the real
  device's own first bootstrap call. A cryptographically complete fix needs a
  manufacturing-time pre-shared secret, which would break this project's
  deliberate zero-touch onboarding design (see "Important Preferences" below) -
  that tradeoff wasn't made unilaterally. Instead: requires a non-empty claim code
  on first-time registration (costs the real device nothing - it always has one
  ready by the time it calls bootstrap) and adds a global rate limit
  (`register-new-global`, 20/hr) on brand-new-receiver creation, which the
  existing per-receiver_id limit does nothing to stop for a bulk/speculative
  squatting campaign across the whole MAC-derived id space. Verified live by
  directly calling `msv2_register_receiver` on the real DB with a disposable test
  id: confirmed it now genuinely rejects a claim-code-less registration and
  succeeds with one; test row deleted after.

**Supabase edge functions - `plant-dashboard` and `receiver-snapshot` redeployed
and verified live:**

- `plant-dashboard/index.ts`: malformed JSON bodies on 4 POST routes were escaping
  to the top-level catch, which returns an HTML error page - breaking
  JSON-consuming clients and violating the function's own "API-only JSON"
  contract (see the 2026-07-07 section's note about this exact HTML-vs-JSON
  boundary). New `parseJsonBody()` helper surfaces a clean 400 instead - confirmed
  live post-deploy by POSTing deliberately malformed JSON to `/api/share-claim`
  and getting back `{"ok":false,"error":"invalid JSON body"}` with
  `content-type: application/json`, not HTML. Also deleted the confirmed-dead
  320-line `renderApp()` (zero references, the "dead code" item flagged as
  out-of-scope in the 2026-07-07 section - closed here). Clamped
  `sinceHours`/`limit`/`beforeId` on the history route.
- `receiver-snapshot/index.ts`: same `sinceHours`/`limit` clamp on the GET history
  route; bounded the `snapshot.plants` array before looping one RPC call per
  entry (was unbounded).

**Web dashboard - deployed to Netlify production (`https://msv2-plants.netlify.app`)
and verified against the live URL, not just the draft:**

- The app's own copy of `assets/plant_gifs/` and its art-key catalog were missing
  all 7 `color_*` sprites added in the 2026-07-06 pipeline update (this is the
  "known, deferred" item from that section's own writeup, finally closed) -
  anyone using those plants silently saw the wrong icon. Copied the GIFs in,
  updated the catalog array and the service worker's precache list.
- Fixed a real camera-privacy bug in the QR scanner: an out-of-order
  `getUserMedia()` resolution from a fast double-tap on "Scan QR" could overwrite
  the shared `state.scanStream` field, so the stream actually feeding the visible
  video element lost its only reference and was never `.stop()`'d - the camera
  hardware indicator could stay on indefinitely after the user thought they'd
  stopped scanning.
- Confirmed via research (Netlify's own docs: header values are concatenated
  across every matching rule, not overridden by the more specific one) that all
  three header configs (`netlify.toml` x2, `_headers`) had a broad `/*` and a
  specific `/assets/*` rule both setting `Cache-Control` - per RFC 7234,
  `no-store` anywhere in that concatenated list wins, silently defeating the
  long-cache/immutable strategy for every versioned asset since those files
  existed. Restructured into non-overlapping path-scoped rules in all three
  files; confirmed live via `curl -I` against the production URL post-deploy that
  `app.v12.js`/the GIFs get clean `public,max-age=31536000,immutable` and
  `index.html`/`sw.js` get clean `no-store`, no concatenation.
- Added missing `.catch()` handling on the Connect and Refresh button click
  handlers; disabled the Connect button for the duration of its request.

**Python tooling** (`scripts/`): `mock_cloud_server.py` had drifted from the real
Supabase functions' actual response shapes; `esp32_packet_loss_test.py`'s loss-rate
math silently misreported instead of flagging a mid-test reboot;
`generate_planter_sheet_gifs.py` hardcoded a transparency palette index without
verifying the placeholder color actually quantized there (could silently corrupt
shipped sprite art); `generate_plant_sprites.py` had a `ZeroDivisionError` on the
documented valid `--max-poses 1` and a `slugify()` bug on digit-leading filenames;
`antenna_rssi_logger.py` had no serial-reconnect handling; `fetch_datasheet.py`
could leave a truncated file trusted by later runs. All fixed.

**Deploy status - everything from this pass is fully live, not just committed:**
git commit `2d7c7e4`; Supabase migrations 019/020/021 pushed and functionally
tested against the real DB; both edited edge functions redeployed and verified;
web dashboard on Netlify production, verified; display receiver firmware flashed
and user-confirmed working on physical hardware. **Not yet flashed to physical
hardware**: the Alocasia probe fix and the other Arduino sketch fixes
(`MSV2_BLE_Adv_Receiver`, `Treebeard_ESP32_DeviceIdentity_Dump`,
`MSV2_Moisture_Probe_Test`) - committed to git, compiled clean, deliberately
deferred per the user ("no lets do that later").

Full findings/triage detail (including everything checked and found clean, e.g. no
leaked credentials anywhere in git history, the 2026-07-07 RPC-lockdown holding for
every function through migration 021) lives in this session's Claude memory
(`msv2_full_repo_review_2026-08.md`) for whoever picks this back up.

## 2026-08-17 Auto-Calibration Redesign Implementation

Implements all six items from the "2026-08-16 Auto-Calibration ('Learning Mode')
Redesign" section directly below. Code changes only in this pass - **not yet deployed**:
the new migration hasn't been pushed to Supabase, the two edge functions haven't been
redeployed, the Netlify site hasn't been redeployed, and no firmware has been flashed to
physical hardware. Both firmware sketches (`Treebeard_ESP32_Display_Runtime`,
`MSV2_Moisture_AFE_Test`) compile clean via `arduino_display.ps1`/`arduino_moisture_afe_test.ps1
-Action compile`.

Two open design questions from the punch list were resolved with the user before
implementing:
- **Item 1 (daysSinceLastWatering fate)**: keep it, informational only. The event-clustering
  SQL (`rises` CTE) and the clustering loop in `deriveAutoCal()` both stay; only the
  `overdue`/`predictedIntervalDays` forcing/alerting behavior built on top was removed.
- **Item 6 (drift guard design)**: "track drift as its own signal" (of the three candidates
  listed) - a flag, not a clamp, so a genuinely lower-water plant's learned range can still
  adapt; a household with chronic late watering instead sees a visible notice.

**Item 1 - overdue/predicted-interval removed:**
- `supabase/functions/receiver-snapshot/index.ts`'s `deriveAutoCal()`: dropped
  `predictedIntervalDays`/`overdue` computation and `AUTOCAL_MAX_EVENTS_FOR_INTERVAL`;
  `daysSinceLastWatering` and the `rises`-clustering loop stay.
- **Follow-up fix, live-tested and deployed same day**: `MobileAppPage.h`'s "watered X ago"
  note (see item 5 below) originally used `daysSinceLastWatering`, a rolling
  hours/24 float with no concept of calendar-day boundaries - watering at 11pm and
  checking at 9am the next morning is ~10 hours ago (rounds to "watered today") even
  though it's clearly yesterday. User caught this live on the physical device. Fixed by
  adding `lastWateredAtMs` (the actual event timestamp) alongside the existing float
  everywhere in the pipeline (`deriveAutoCal`'s return, `AutoCalState`, both
  `sensorDataJson()`/`snapshotJson()` emit sites, `runCloudAutoCalPull()`'s parse) and
  rewriting `MobileAppPage.h`'s label function (`daysAgoText` → `wateredAgoText`) to
  compare local calendar dates (`new Date(ms).setHours(0,0,0,0)`) instead of a rolling
  24h window - this can only be done client-side, since the server has no idea what
  timezone the viewer is in. Compiled, deployed (`receiver-snapshot` redeployed), and
  reflashed to the physical receiver same session - confirmed live.
- Firmware (`Treebeard_ESP32_Display_Runtime.ino`): `resolveMoistureColor()` deleted entirely
  (call sites now call `healthColor()` directly, per the punch list's own note that it
  "collapses to plain healthColor()"). `resolveMoistureBounds()`/`refreshSlotMoistureBounds()`
  no longer output/track `overdue`; `slotNeedsBlink()` (shared by the blink alert and the
  buzzer's `serviceWaterAlert()`) dropped its overdue OR-clause, so both alerts are now
  purely "moisture < 25%".
- `MobileAppPage.h`: `renderStatus()`'s `overdue` computation removed; the Sound Alerts
  copy reworded from "stays overdue for water" to "stays critically low on water".
- `web/plant-dashboard/assets/app.v12.js` → renamed to **`app.v13.js`** (cache-bust, same
  reasoning as the v10→v11 and v11→v12 bumps earlier this project - an in-place edit
  wouldn't reach already-installed Home Screen PWAs). `resolveMoisture()`'s overdue-forces-red
  logic removed. `index.html` and `sw.js` (cache name + precache list) updated to match.

**Item 3 - manual recalibration/reset control:**
- New migration `supabase/migrations/023_msv2_calibration_reset_and_drift.sql`: table
  `msv2_sensor_calibration_reset` (receiver_id, sensor_id, reset_at) + RPC
  `msv2_reset_sensor_calibration(share_token, receiver_id, sensor_id)`, same share-token
  household-auth pattern as `msv2_clear_share_sensor_history` (016).
  `msv2_read_sensor_auto_calibration` rebuilt to clamp its percentile window's lower bound
  to `greatest(now() - lookback, reset_at)` - History-tab data is untouched, only the
  calibration math ignores anything before the reset.
- `supabase/functions/plant-dashboard/index.ts`: new route
  `/api/share-autocal-reset` (`shareAutocalResetApi`). Deliberately **not** added to the
  Netlify dashboard - matches the existing read-only-Netlify decision (see 022's retired
  share-display RPCs).
- `MobileAppPage.h` Setup tab: new "Reset calibration" button next to each auto-enabled
  card's checkbox (`resetSensorCalibration()`), using the same self-minted share-token
  pattern as `loadHistory()`/`clearAllHistory()` - no new receiver-local endpoint needed.

**Item 4 - AFE bench sensor smoothing:** `MSV2_Moisture_AFE_Test.ino` now transmits
`smoothedMv` (rounded) instead of raw `medianMv` - one line, the EMA was already computed
and logged to serial, just never wired into `advertiseReading()`.

**Item 5 - raw-to-percent moisture text once calibrated:** `resolveMoistureBounds()` gained
an `outCalibrated` flag (true only when auto-cal bounds are actually in use, i.e. the old
early-return branch). `drawPlantCard()`/`drawPlantCardDataOnly()`/`computeDetailStats()` all
switch their moisture text from `"%u"` to `"%u%%"` when calibrated; the data-only diff
tracking (`slot.lastMoistureCalibrated`) was extended so a calibration-readiness flip alone
(without a raw-value change) still triggers a redraw. `MobileAppPage.h`'s `renderStatus()`
does the same for the local app.

**Item 6 - dry-bound floor clamp (revised twice after further discussion the same day):**
the first cut (bucketed 3-window monotonic-decrease flag, informational only) was
superseded before deployment once the user clarified the actual goal - stop chronic
over/under-watering from destroying the recommendation interval, not just get notified
about it, with no absolute ground-truth moisture value available to clamp against (differs
per plant/pot/soil). The second cut (flat `AUTOCAL_DRY_FLOOR_MAX_DROP = 15` raw units) was
itself revised once the user pointed out a flat unit count is meaningless across this
deployment's three probes, which sit on very different raw scales (confirmed live via
Supabase query mid-session: sensor 1 at `dryBound=183/wetBound=358` vs sensor 3 at
`dryBound=1078/wetBound=1265`). **Landed design: anchor each sensor to its own first
full-lookback-window calibration (not the earlier 3-day "ready" gate - the first 21 days
run completely unclamped so several real watering cycles get observed first), then
hard-clamp the dry bound to never drift more than `AUTOCAL_DRY_FLOOR_MARGIN_PCT` (10%,
provisional) of that sensor's own anchored (wet - dry) range below its anchor.**
- Table `msv2_sensor_calibration_reset` (migration 023, rewritten in place twice since
  never deployed) gained `anchor_dry_bound` and `anchor_wet_bound`, both nullable, separate
  from `reset_at` (also nullable) so a row can exist for anchor-only or reset-only reasons
  independently. The two anchor columns are always written together (the margin needs both
  to mean anything).
- New RPC `msv2_set_sensor_calibration_anchor_if_unset(receiver_id, sensor_id, dry, wet)`
  (service_role only, no share-token - internal system write, not a user action):
  idempotent, only ever fills in a null anchor pair, never overwrites an existing one.
- `msv2_reset_sensor_calibration` (item 3's manual reset button) now also clears both
  anchor columns to null - a reset means "different plant/pot," so the old anchor is as
  stale as the old learned range; a fresh one gets captured automatically once the
  newly-reset window is full again.
- `msv2_read_sensor_auto_calibration` returns each sensor's `anchorDryBound`/`anchorWetBound`
  alongside the raw computed `dryBound`/`wetBound`; the earlier 3-bucket CTEs from the first
  revision are gone entirely.
- `receiver-snapshot/index.ts`'s `deriveAutoCal()` does the actual clamping, gated on a new
  `AUTOCAL_LOOKBACK_DAYS = 21` (must match the SQL's `p_lookback_days` default) full-window
  check, separate from and later than the existing 3-day `ready` gate: if the window is
  full and no anchor exists yet, it flags `needsAnchor` (the GET handler then calls
  `msv2_set_sensor_calibration_anchor_if_unset` with the current raw dry+wet bounds,
  best-effort/fire-and-await, not blocking on failure); if an anchor exists, `dryBound` in
  the response is clamped to `max(rawDryBound, anchorDry - 0.10*(anchorWet - anchorDry))` -
  this is the value firmware/app actually use for percent/color, so the interval genuinely
  cannot collapse past the floor, not just get a warning about collapsing.
- `dryBoundTrendingLower` **keeps its exact name and JSON field** but now means "the clamp
  is actively engaged" rather than "3 buckets happened to decrease" - zero firmware/app
  changes needed as a result. Physical-display Range-row recoloring and the local app's
  `.notice.warn` line both still work unmodified, now backed by a real corrective action
  instead of a passive observation.
- Researched via web search before landing on the clamp-vs-flag decision - see the
  "shifting baseline"/"boiling frog" drift literature and the CGM/irrigation-controller
  domain comparisons discussed in chat; the chosen approach mirrors the "sanity min/max on
  an adaptive baseline" pattern from monitoring tooling, anchored per-sensor since no
  universal ground truth exists.
- **Live-queried mid-session, not deployed from**: pulled real numbers for the actual
  deployed receiver (`MSV2088A3104A7AC`) via `supabase db query --linked` to answer a live
  user question ("why does 297 read as ~65%?"). Confirmed the currently-*live* (pre-this-session,
  unmodified) auto-cal already shows sensor 1 (Thaumatophyllum) at `dryBound=183`,
  `wetBound=358` - and a day-by-day min/max breakdown showed a suspiciously flat 8/2-8/8
  plateau (~180-183 every day, near-zero variance) sitting inside the 21-day window, lining
  up exactly with the pre-8/9 QSPI battery bug (see that section above) rather than genuine
  soil dryness. Real range since the 8/9 fix looks like ~280-430. Not a bug in anything
  built today - this is live evidence of exactly the failure mode item 6 targets, plus a
  reminder that whichever sensor gets its anchor captured first after deployment should
  probably get a manual "Reset calibration" first if its window still contains pre-fix
  data, so the anchor isn't captured from a still-corrupted floor.

**Not done in this pass:**
- Nothing pushed/deployed/flashed - see the note at the top of this section.
- `web/msv2-plant-dashboard-static.zip` (mentioned elsewhere in this doc) was not
  regenerated; it will be stale against `app.v13.js` until a fresh export happens.
- No physical-hardware testing of the reset-calibration button, the percent-text switch,
  or the drift notice - compiles clean but unverified on real screens/probes.

## 2026-08-16 Auto-Calibration ("Learning Mode") Redesign — Handoff Notes

User reviewed how the moisture auto-calibration/"learning mode" system actually works (see
`supabase/migrations/017_msv2_sensor_auto_calibration.sql` and `deriveAutoCal()` in
`supabase/functions/receiver-snapshot/index.ts` for the current implementation) and wants
several changes. Nothing below is implemented yet - this is the punch list for next session.

**1. Remove the predicted-watering-interval / "overdue" feature entirely.** User's reasoning:
"the whole purpose of this system is to go off of moisture not timing, that's its whole upside" -
a calendar-style watering-schedule prediction cuts against the product's actual value
proposition. Touches every layer that currently reads `autoCal.overdue`:
- `receiver-snapshot/index.ts`'s `deriveAutoCal()` (`AUTOCAL_CLUSTER_GAP_MS`,
  `AUTOCAL_MAX_EVENTS_FOR_INTERVAL`, the event-clustering loop, `predictedIntervalDays`,
  `overdue`) - remove the computation. **Check before deleting the "rises" pipeline entirely**:
  the SQL function's `rises` CTE (candidate-jump flagging: gap ≤3h, delta ≥15% of range) exists
  only to feed this clustering - if nothing else in the system reads `rises`, that whole
  candidate-detection stage in `msv2_read_sensor_auto_calibration` becomes dead weight too and
  should come out alongside it, not just the TypeScript consumer.
- Firmware: `resolveMoistureColor()`'s `overdue` parameter (forces dangerColor) and
  `refreshSlotMoistureBounds()`/`resolveMoistureBounds()`'s `slot.overdue`/`outOverdue`
  assignments - `resolveMoistureColor()` collapses to plain `healthColor()` once this is gone,
  could be inlined/removed.
- `MobileAppPage.h`'s `overdue` computation in `renderStatus()`.
- `web/plant-dashboard/assets/app.v12.js`'s `resolveMoisture()` - **note this was added THIS
  SAME SESSION** (2026-08-16, to fix the Netlify/display color mismatch) specifically to mirror
  the firmware's overdue-forces-red behavior. Once the firmware behavior changes, this needs a
  matching follow-up edit, not just a one-time fix-and-forget.
- Open question for tomorrow, not decided here: does `daysSinceLastWatering` (informational,
  not a forcing/alerting signal by itself) stay as a nice-to-have display stat, or does the
  whole watering-event-detection apparatus go? User's stated reasoning suggests the latter, but
  wasn't explicitly asked.

**2. No changes to the 21-day calibration window or the 3-day minimum calibration time** -
explicitly confirmed good as-is (`msv2_read_sensor_auto_calibration`'s `p_lookback_days := 21`
default, and `AUTOCAL_MIN_DAYS_READY = 3` / `AUTOCAL_MIN_SAMPLES_READY = 50` in
`receiver-snapshot/index.ts`).

**3. Add a manual recalibration/reset control** - a way to force a sensor back into "learning"
mode and rebuild its range from fresh data, for cases like moving a probe to a new plant/pot.
Not a trivial flag flip: readiness and the dry/wet bounds are computed live from a rolling
window over real `msv2_sensor_history` rows (`min(received_at)` as the readiness clock, 21-day
percentile window for the bounds) - there's no separate "calibration state" to simply clear.
Two directions, leaning toward the second:
- Destructive: delete/exclude that sensor's history before the reset point. Loses real
  historical chart data - probably wrong.
- **Recommended: a per-sensor "calibration epoch" timestamp** (new small table, e.g.
  `receiver_id` + `sensor_id` + `reset_at`, or a column somewhere queryable by
  `msv2_read_sensor_auto_calibration`) that the auto-cal SQL function clamps its window's lower
  bound against (`greatest(now() - interval, reset_at)`) instead of using
  `now() - lookback_days` unconditionally. Keeps full history intact for the History tab/charts;
  only the calibration math ignores anything before the reset. Needs its own migration and a
  new RPC (e.g. `msv2_reset_sensor_calibration`) plus a UI control - most natural home is the
  local app's Setup tab next to the existing auto-calibrate checkbox, not something exposed on
  Netlify (stays consistent with the read-only-Netlify decision from earlier this session).

**4. More smoothing on the AFE bench sensor's ADC readings** - "this data jumps around a lot
even with our current implementation." **This one has a concrete, already-diagnosed fix**, found
earlier this same session: `MSV2_Moisture_AFE_Test.ino` already computes an EMA-smoothed value
(`smoothedMv`, alpha 0.2) every reading cycle - but only prints it to serial for diagnostics.
The value actually broadcast over BLE and stored in history is `medianMv` (line 149,
`advertiseReading(medianMv)`), the raw median-of-5, not the smoothed one. The smoothing work is
already written and never wired up. Fix: transmit `smoothedMv` (rounded) instead. If that's not
smooth enough once it's actually live, tune the alpha constant down (e.g. 0.2 → 0.1) for a
slower-moving average - straightforward once the EMA is actually in the transmit path.

**5. Once the 3-day calibration period finishes, switch the displayed value from raw to
percentage as a visual "done calibrating" indicator.** Confirmed the physical display currently
always shows the raw value as text regardless of calibration state - `drawPlantCard()`
(`Treebeard_ESP32_Display_Runtime.ino` ~line 3086-3094): `snprintf(moistureText, ..., "%u",
reading->moisture)`, with the percent value only driving the color bar underneath, never the
text itself. Same pattern repeats in the diff-redraw siblings (`drawPlantCardDataOnly()`
~3126-3154, `computeDetailStats()` ~3258-3280) - all three need the same change, not just one.
Proposed behavior: once a sensor's bounds are auto-cal-derived (not manual/preset), swap the
text to `"%u%%"` with `moisturePct` instead of the raw value. Also applies to `MobileAppPage.h`'s
`moistureMetric` (currently always shows the raw value too) for consistency between the display
and the local app.

**6. Guard against the learned range silently drifting over time from chronic late watering.**
Real concern, not yet addressed anywhere: since `dryBound` is the 5th percentile of a rolling
21-day window, a household that's consistently late watering will see each cycle's unusually-low
minimum become part of the new normal, pulling `dryBound` progressively lower - the alert
threshold chases the neglectful behavior instead of holding a meaningful floor, exactly
backwards from what an alert system should do. Not designed yet; candidate directions to weigh
next session, none chosen:
- Clamp the learned `dryBound` so it can never drift more than some fixed margin below the
  original manual/preset value - anchors drift to a sane starting reference but caps how far
  genuine long-term adaptation (e.g. a plant that's just naturally drought-tolerant) can go too.
- Track the learned bound across successive windows and flag a sustained one-directional drift
  as its own signal ("this plant's watering has been trending later") rather than only guarding
  against it - turns the failure mode into a feature.
- Keep the adaptive bound for day-to-day color/percent display, but maintain a separate,
  slower-moving or fixed "hard floor" that still fires red/danger regardless of what the rolling
  window currently considers normal.

## Last Known User Feedback

- Display and app are working well after the WiFi/setup fixes.
- User confirmed the first-time setup/local network flow looks good for now.
- User noticed a brief visual artifact where pixel art stayed visible during the reboot message; this was fixed and flashed in `2026-07-05 reboot-screen-fix`.
- New plant GIF additions are working in the dropdown after regeneration/flash.
- Latest sprite feedback: normalized old and new sprites now look much closer in size after the sheet-derived GIFs were upscaled before sprite generation.
- Version numbers are visible and incrementing.
- Mock cloud test concept makes sense and looks good.
- 2026-08-12: user confirmed the touch/brightness UI now matches the rest of the app, the
  `+`/`-` brightness buttons step correctly, the slider tracks smoothly with no flash, and all
  animation corruption (red pixels, superimposed frames, the probe-sprite swap, and the
  moist/batt row flashing on every blink tick) is resolved - "everything looks good now."
- Later 2026-08-12 hardware feedback found the small header brightness activator still required
  many attempts. The sun button was removed and replaced with a release-based double tap anywhere
  on the dashboard (120-650ms between releases); wake-only touches and resistive contact bounce do
  not count. XPT2046 cannot provide a true two-finger gesture because it reports only one point.
- Final 2026-08-12 round: user confirmed the tap-to-expand plant card detail view works well on
  the physical receiver, including after the redraw-discipline fix - the periodic refresh no
  longer flashes the whole card, and the header IP address is back in the top-right corner -
  "this is all working great."
- 2026-08-14: after the full-repo review pass and flashing the display receiver, user confirmed
  on the physical hardware: "looks great, QR good too" - the dashboard renders normally
  post-flash and the new fully-offline client-side QR code (replacing the third-party
  api.qrserver.com dependency) renders correctly on the real screen.

## Important Preferences

- Prefer static checks and focused edits.
- Keep the ESP32 local-first.
- Cloud features should stay disabled unless explicitly enabled.
- User wants a product path suitable for family/friends: simple setup, phone app, remote monitoring eventually.
- User specifically wants future setup where each ESP32 already has its own ID/secret and, after WiFi setup, connects to cloud automatically without manually entering device IDs into Supabase.

## Suggested Next Work

**Groomed 2026-09-05.** What stood here was four superseded layers dating back to 2026-07-25, kept
by ranking rather than by decision, and largely overtaken by the OTA campaign. Every item has now
been given a verdict instead of a position in a queue. The old text is archived at the bottom of
this section so nothing is silently lost.

### Do next, in this order

1. ~~**Push the branch.**~~ Done 2026-09-05. `msv3-ble-ota` is on origin.
2. ~~**Make the probe identity a build parameter.**~~ Done 2026-09-05.
3. ~~**Replay/downgrade test.**~~ Done 2026-09-05: the bootloader **accepts** an older validly
   signed image. `dfu_init.c`'s version check is an explicit empty template. Mitigated host-side
   via `installed_application_version` in the registry; a real fix is bootloader-side and needs
   SWD. See `docs/KNOWN_ISSUES.md`.
4. **Longer probe-99 soak of the ERROR_CONFIG change**, acceptance defined before the clock starts,
   then ship it to probe 4 over BLE. Current evidence is 58 healthy bench readings.
5. **Migrate bench boards 1-3 off the stock bootloader** by USB, canary first. These boards keep
   USB CDC, so this never needed SWD — it was mis-filed as blocked.
6. **Thermal falsification tests** (D1 below): dry probe 4's pot and refit; water probe 5; shade
   probe 4 for a day. Physical actions, then elapsed time and cloud analysis.

### Superseded 2026-09-06 by the conformal-coating campaign

Probes 4 and 5 are being conformally coated, which permanently covers their SWD pads and USB
connectors. That reorders everything above: the only question that now matters for those two boards
is whether a task becomes *impossible* afterwards. Items 4 and 6 above are unaffected and simply
wait; item 5 concerns boards that are not being coated.

**Decided:** coat probes 4 and 5 only. Probe 99 is never coated and remains the permanently
USB/SWD-recoverable board that every image is bench-proven on. **Build and SWD-install bootloader
1.0.2** — justified because both probes must be retrieved for coating anyway and probe 5 must be
re-flashed regardless, so the mass-erase cost the "freeze 1.0.1" recommendation was priced against
is already sunk.

**1.0.2 is anti-rollback only.** Erase-after-authenticate was dropped: it needs Start/Init
reordering and an async-erase wait in the completion handler, which is the state-machine
restructuring implicated in the completion-gate defect. RESETREAS was dropped from the bootloader
scope entirely — it was an application bug, see `docs/KNOWN_ISSUES.md`.

**Done 2026-09-06, no hardware required:**

- `readResetReason()` fix, boot banner decodes the cause by name, and a build check that fails on
  any direct `NRF_POWER->RESETREAS` access. Negative control confirmed it fires.
- Bootloader 1.0.2 built from `MSV3/bootloader/patches/msv3-1.0.2-anti-rollback.patch`, applied and
  sentinel-verified by `build_msv3_release.ps1`. `msv3_pending_app_version_get` and the
  `0x4d535633` magic are confirmed present in the linked binary at both the check site
  (`dfu_init_prevalidate`) and the commit site (`bootloader_dfu_update_process`).
- Seven signed test packages under `MSV3/bootloader/generated/1.0.2/`, N=65550: probe 99 gets
  N−1/N/N+1, probes 4 and 5 get N−1/N. Every package's embedded identity and version verified
  against its filename, zero mismatches. Package and migration-package names now carry both.
- The two pre-guard-1 signed packages moved to `MSV3/bootloader/quarantine/` with hashes and a
  README; `test_ota_preflight.py` repointed so its assertion still runs.
- `MSV3/scripts/verify_msv3_signing_key.py` — the key restore drill. Confirms a candidate PEM
  matches the fingerprint the registry pins AND can still produce a verifying signature. Negative
  control with a different valid P-256 key (also 227 bytes) is correctly refused.

**Blocked on the single GP/SWD session**, in order: install 1.0.2 on probe 99; instrument settings
saves and *callback identity*; power-cut matrix; N−1/N/N+1 matrix; the true-hang guard-2 test. Then
probes 4 and 5: DMM probe 5's cell first, pre-erase readback, hardware qualification, SWD-install
1.0.2, seed the floor by installing N **over BLE** so the tested transaction writes it, re-qualify
under final firmware, then the destructive N−1 rejection and battery-only cold boot before GP comes
down.

**Not yet done and deliberately so:** `installed_application_version` for probes 4 and 5 stays `0`
until N is actually installed and booted. Writing it at build time would make the registry
knowingly false.

### Blocked, with the reason named

- **Guard 2's watchdog-hang test** needs SWD attached *and recovery proven first*, because a hang
  before advertising is the unrecoverable state itself and this bootloader has no USB CDC. The
  "guard 1 does not cover a hard I2C hang" item is not separate work — it is this test's coverage
  gap, and should be read as one item.
- **Probe 5's migration** needs the probe physically retrieved, not SWD.
- **Probe 5's cell identity** (resting 4280-4441 mV, above a single LiPo's ceiling) needs a DMM at
  the probe. Treat as a safety check at the next visit: either the divider/calibration is wrong or
  the cell is genuinely overvoltage.
- **Blade coating validation** needs a repaired specimen and weeks of air -> soil -> watering ->
  air cycling.
- **Fault-reason-on-the-wire** can have its protocol, encoding and decoder written now; hardware
  verification needs the receiver plugged in.

### Dropped, with the reason

- **Dual-bank DFU forensics.** The remaining question is which of three known activation defects
  fired historically, answerable by one SWD read of the settings page. Dropped as planned work:
  SDK11 dual-bank is *staging, not rollback* — it protects the old image during download and then
  destroys it at activation — so it cannot deliver the post-boot revert that motivated it. Take
  the settings-page read only if SWD is attached for something else.
- **Auto-calibration redesign deployment** (top of the 2026-08-17 list). Deferred behind the
  thermal finding: a daily thermal excursion larger than a watering step means any calibration
  learned now would be fitting temperature. Revisit after D1.
- **`--prn 4` at weak RSSI.** Not research. Either a runbook line or, better, tooling that selects
  it automatically below about -70 dBm.

### Converted from "open question" to "known and decided"

- **The charging flag is not trustworthy** (advertisement bit0). Probe 4 never asserts it while its
  battery visibly climbs; probe 5 flips it 682 times in 1498 readings at its regulation threshold.
  This is a measured finding, not an investigation. It *is* consumed: the receiver forwards
  `rawCharging` into the uploaded payload and the local app, so the remaining work is to stop
  presenting it as "charging" (or to relabel it raw BQ25170 STAT), not to study it further.
- **`claim-receiver` Edge Function.** Checked 2026-09-05: no code consumer anywhere in the repo —
  only `cloud/supabase/config.toml`, the setup doc, and this file reference it; the no-login
  `plant-dashboard` share-claim flow superseded it. Decision: retire. Left deployed for now because
  removing a live Edge Function is an outward-facing change that should be done deliberately.

<details>
<summary>Archived pre-2026-09-05 backlog (superseded; kept for provenance)</summary>

> ## Suggested Next Work
>
> Top of the list as of 2026-08-17 (the 2026-08-14 list below is superseded by ranking,
> not by completion):
>
> 1. Deploy and verify the 2026-08-17 auto-calibration redesign implementation (see that
>    section above): push migration `023_msv2_calibration_reset_and_drift.sql`, redeploy
>    `receiver-snapshot` and `plant-dashboard` edge functions, redeploy the Netlify site
>    (now on `app.v13.js`), flash `Treebeard_ESP32_Display_Runtime` to the physical receiver, and
>    flash/bench-verify `MSV2_Moisture_AFE_Test`'s smoothed-value fix. Then physically test:
>    the reset-calibration button in the local app's Setup tab, the raw-to-percent moisture
>    text switch once a sensor calibrates, and (once enough history exists to actually drift)
>    the dry-bound trend notice. None of this has been deployed or hardware-tested yet -
>    code-complete and compiling only.
>
> Superseded by ranking, top of the list as of 2026-08-14 (the 2026-08-09 list below is
> superseded by ranking, not by completion - its battery/hardware items are physical-world tasks
> that session's software review pass didn't touch and are presumably still open):
>
> 1. Flash the probe/bench-sketch fixes from the 2026-08-13/14 review to their
>    physical boards - explicitly deferred this session ("no lets do that later").
>    Most important: `MSV2_Moisture_BLE_Sleep_Alocasia.ino`'s charge-settle poll-loop
>    fix, a real quantization-risk bug on live hardware. Lower priority:
>    `MSV2_BLE_Adv_Receiver.ino`, `Treebeard_ESP32_DeviceIdentity_Dump.ino`,
>    `MSV2_Moisture_Probe_Test.ino` (bench tools, not continuously deployed).
> 2. Decide whether the registration-race mitigation in migration 021 (claim-code-
>    required first registration + a global new-receiver rate limit) is sufficient,
>    or whether the residual risk (a determined, physically-proximate attacker can
>    still win the race - see that migration's own SQL comments for why a complete
>    fix needs a manufacturing-time secret) is worth a bigger design change. Not
>    urgent given the actual threat model for a small family/friends deployment.
> 3. `claim-receiver` Edge Function still exists but is superseded by the no-login
>    `plant-dashboard` share-claim flow - decide whether to keep it or retire it
>    (open since the 2026-07-07 pass, still undecided).
>
> Superseded by ranking, top of the list as of 2026-08-09 (item 6 below is now DONE -
> see the 2026-08-14 section above):
>
> 1. Check the overnight battery test: has Alocasia's battery held within ~1-2% (fix working) or dropped ~8-12% (QSPI leak somehow still active)?
> 2. Recharge/swap Scindapsus's battery - it's been at/near 0% since ~Aug 7 and is blocking clean data from that probe on every front (moisture *and* the battery-fix test).
> 3. Bench-check Scindapsus's BQ25100 `~CHG` pin for noise (leading theory for its erratic battery% readings even before the QSPI fix - see the 2026-08-09 section above).
> 4. Continue the FDC2212/MSV3 PCB migration open items (`docs/MOISTURE_SENSOR_FDC2212_FRONTEND.md`) - now with concrete field evidence (not just theory) that VDD-coupling is a real, currently-happening problem, both from low battery and from active charging.
> 5. Consider a firmware-only mitigation in the meantime: discard/flag moisture samples taken while charging or below a battery floor, independent of the FDC2212 hardware timeline.
> 6. ~~Revisit the receiver's `DEFAULT_STALE_SECONDS`~~ - **done 2026-08-14**, dropped 3900 -> 180.
>
> Superseded, top of the list as of 2026-07-25:
>
> 1. Check tonight's overnight results (via the now-fixed History tab, or `scripts/fetch_supabase_history.py`): are oscillator readings solid/stable in real soil (vs. last night's RC-timing sawtooth-to-saturation), and has auto mode's "ready" gate cleared for any sensor yet.
> 2. Confirm the 100kΩ pulldown is actually populated on the deployed field probe(s), not just the bench rig — the firmware can't compensate if it isn't there.
> 3. Tomorrow's solar-panel charging test in direct sunlight — panel only appeared to charge in direct sun, not even slight shade; the battery EMA smoothing landed specifically so this test's charging curve reads cleanly.
> 4. Once real soil data exists, revisit the provisional dry/wet defaults (currently 120/175, bench-touch-derived) - or just lean on auto mode once enough per-sensor history accumulates, rather than hand-tuning again.
> 5. ~~Update `web/plant-dashboard`'s hardcoded art catalog~~ - **done 2026-08-14**, all 7 `color_*` GIFs and catalog keys added, deployed to production.
> 6. ~~Fix the receiver's missed-packet-counter wraparound-on-reboot bug~~ - **done 2026-08-14** for `MSV2_BLE_Adv_Receiver.ino`; the main receiver's own `onResult()` was found already correctly guarding this (undocumented until now).
>
> Older, still open from the 2026-07-07 pass:
>
> 1. Recover the original 8 plant card names/art from the last cloud snapshot (query in the section above) and push them back with `push_display_ui.py` — the device is currently running the generic `ui/display_default.json` placeholder cards.
> 2. Work through the Medium/Low findings from the 2026-07-07 review that were explicitly out of scope for the security pass (see that section for the list).
> 3. `claim-receiver` Edge Function still exists but is superseded by the no-login `plant-dashboard` share-claim flow — decide whether to keep it or retire it.
>

</details>

## Quick Test Commands

Fetch the probe's Supabase sensor history into a CSV (survives the laptop being off overnight):

```powershell
python scripts\fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 1 --since-hours 24 --csv data\sensor_logs\alocasia_overnight.csv
python scripts\plot_sensor_readings.py --csv data\sensor_logs\alocasia_overnight.csv --title "Alocasia overnight"
```

Log probe readings directly from this laptop instead (requires the laptop to stay on):

```powershell
python scripts\log_sensor_readings.py --url http://10.0.0.236 --sensor 1 --interval 5 --csv data\sensor_logs\local_log.csv
```

BLE packet-loss test against the display receiver's `/data`:

```powershell
python scripts\esp32_packet_loss_test.py --url http://10.0.0.236 --sensors 1 --duration 600 --csv packet_test.csv
```

Mock cloud:

```powershell
python scripts\mock_cloud_server.py --host 0.0.0.0 --port 8088
```

Relay one snapshot from ESP32 to mock cloud:

```powershell
python scripts\relay_snapshot_once.py --receiver http://BOARD_IP --cloud http://127.0.0.1:8088
```

Inspect mock cloud snapshots:

```powershell
curl http://127.0.0.1:8088/api/v1/snapshots
```

Open the mock cloud dashboard:

```text
http://localhost:8088/
```

Direct receiver uploader test through the `/cloud` API:

```text
http://PC_LAN_IP:8088/api/v1/receivers/RECEIVER_ID_FROM_APP/snapshot
```

Receiver local checks:

```text
http://BOARD_IP/app
http://BOARD_IP/cloud
http://BOARD_IP/snapshot
http://BOARD_IP/device
http://BOARD_IP/config-meta
```
# 2026-08-22: receiver lossless history batching deployed

- Fixed the lossy five-minute history path. The receiver now queues every unique BLE wake and
  uploads acknowledged 32-row batches while retaining the existing 300-second normal HTTPS
  cadence. New readings no longer overwrite the previous four minute-cadence readings before a
  snapshot.
- Migration 030 adds exact observation timestamps and receiver-scoped idempotency IDs; migration
  031 adds atomic batch ingestion. The batch-aware `receiver-snapshot` Edge Function is deployed.
- Receiver build `MSV2 runtime build 2026-08-22 lossless-history-batch-v1` is live on COM16.
  Live full-batch retries/draining succeeded with zero queue drops. MSV3 wakes 135-138 are separate
  Supabase rows spaced 69.5-70.4 seconds apart with full raw FDC counts.
- Queue capacity is 512 receiver-observed wakes and is RAM-resident. See
  `docs/RECEIVER_LOSSLESS_HISTORY_BATCHING_2026-08-22.md` for protocol, retry behavior, outage
  headroom, and the reset/power-loss limitation.

## 2026-08-23: display card and animation pipeline repair

- Replaced each card's battery bar with a compact color-coded `Batt: N%` value in the top-right
  header and renamed the visible `Moist` label to `Moisture` in full, compact, expanded, and
  data-only redraw paths.
- Reclaimed the old battery row for artwork: a 137px-tall four-card layout now provides an 80px
  art band instead of 46px. Runtime scaling now aspect-fits directly; the former integer-divisor
  path turned an 84px sprite into only 42px whenever the available height was 43-83px.
- Repaired `generate_plant_sprites.py` so every GIF pose uses one shared crop and retains logical
  canvas coordinates. The old per-frame trim/re-center path made pose-dependent bounds look like
  random motion. `cactus_cluster.gif` also contained a detached 231px source artifact 33px below
  its main plant; crop selection now prevents that speck from shrinking/lifting only pose 3.
- Source GIF durations are emitted into `plant_sprites.h` and honored by the receiver. The old
  random 380-620ms interval is gone; only the initial phase is staggered. All unique poses are
  preserved by default instead of silently capping them at three.
- Verification: five sprite geometry/timing tests pass, the 1-16-card layout contract passes, and
  the final normal display firmware compiles at 2,197,400 bytes flash / 77,044 bytes RAM. Upload to
  COM16 is intentionally pending explicit approval because flashing reboots the receiver and can
  discard any rows still in its RAM-only history queue.

## 2026-08-25: MSV3 software-only board temperature + bare-tank reference (compile-verified, not flashed)

- Added two raw observables with no hardware change: nRF52840 die temperature (SoftDevice
  `sd_temp_get()` with a balanced HFXO request, never touching LFCLK/RTC1) and a bare-tank FDC
  reference reading (EC-check mux codes 2/3, disconnecting the electrodes) taken once per
  advertising wake. Neither is soil temperature; neither is compensated/computed anywhere yet -
  both are raw values carried through to Supabase. Full design and verification details in
  `docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md`.
- MSV3 probe packet widened 17->23 bytes (first 17 bytes unchanged); receiver, cloud Edge
  Function, and migration 035 updated to match, all backward compatible with older/shorter
  packets.
- Probe and receiver compile clean; migration/tests verified locally (`node
  scripts/verify_supabase_migrations.mjs`, `node --test supabase/functions/receiver-snapshot/
  *.test.ts`, 23/23). Nothing flashed or deployed - all three require separate explicit
  authorization.

## 2026-08-25: MobileAppPage.h History tab infinite-reload fix (flashed, live)

- Root cause: the History tab's 30s auto-refresh `setInterval` fired unconditionally, including
  while a previous `loadHistory()` call was still mid-pagination. A >14-day range needs up to
  14-30 sequential Supabase page fetches; if that took longer than 30s (routine for a wide
  range), the timer restarted the whole fetch from scratch every tick - a >14-day load could
  never finish, which looked like the History view resetting and reloading forever.
- Fix: a `historyLoadInFlight` flag, set for the duration of any `loadHistory()` call and
  cleared only by whichever call is still current when it finishes. The 30s auto-refresh now
  skips its tick while a load is already running instead of stacking a new one on top.
  Manual triggers (tab click, sensor/range dropdown change) are unaffected - they still
  cancel-and-restart immediately via the existing `historyRequestId` guard, which is correct UX.
- Verified: both embedded `<script>` blocks in `MobileAppPage.h` parse clean (`node -e
  "new Function(...)"` on each extracted block). Compiled and flashed to the ESP32 display
  receiver on COM16; hash-verified.

## 2026-08-25: agent-supervisor (Claude+Codex coop) root-cause fix + new live-pairing tool

- `tools/agent-supervisor/` (the batch Claude-Codex planning CLI) was root-caused from real
  `.agent-runs/` logs - 6 of the last 16 real runs ended in a bare error with nothing else, and
  every run started with zero awareness of `CLAUDE.md`. Fixed the same day: `settingSources: []`
  (which silently disabled CLAUDE.md loading) corrected to `["project"]`; a repo-root `AGENTS.md`
  added so Codex gets equivalent grounding (its own auto-loaded-instructions convention,
  confirmed via web search); the shared `AbortController` no longer aborts the whole run on an
  ordinary (non-billing) failure - it degrades to solo-provider mode instead of discarding the
  other provider's completed work; added a per-call wall-clock timeout (Codex had none before);
  fixed a false-negative in `doctor` (a Windows/Node security change broke its own Codex-auth
  check, not an actual auth problem - confirmed Codex really is logged in and working). 20/20
  tests pass, two full live validation runs completed with real API calls.
- Added `--resume-claude`/`--resume-codex [id|auto]` (`src/session-discovery.ts`): finds and
  resumes an existing real Claude/Codex conversation for this repo automatically, confirmed live
  against this exact interactive session and a real Codex VSCode-tab session. Real constraint
  found live: a Codex thread can't be resumed while it's simultaneously open in another window
  (single-writer lock) - the orchestrator's solo-mode degrade (above) handles that gracefully.
- Built a second, separate tool in response to "I want to watch both agents work in real time and
  keep talking to them afterward": `src/live-server.ts`, a local-only HTTP+SSE server + browser
  page (`public/live.html`) with two live-streaming panels (Claude/Codex side by side), each in a
  real resumable session, each shown the other's last reply on every round after the first so
  they build on each other rather than answering in isolation. Read-only by design (discussion
  tool, not an implementation one - use the batch CLI with `--allow-write` for that). Smoke-tested
  live end-to-end (real streaming, real tool calls, real cost tracking). First real use: the
  design session that produced the MSV3 board-temperature/bare-tank-reference feature above.
- Full detail in `tools/agent-supervisor/README.md` (updated) and memory
  `msv2_agent_supervisor_fix_and_resume`. Launch note: `npm.cmd`/`npx.cmd` mangle arguments when
  run through a Git-Bash-backed shell on this machine (`'C:\Program' is not recognized...`) -
  invoke `node node_modules/tsx/dist/cli.mjs src/index.ts run ...` (or `src/live-server.ts`)
  directly, or use a native PowerShell/cmd terminal where `npm run ...` works fine.

## 2026-08-28: Four-board MSV3 SWD bring-up + EC-check analog fix (flashed, live)

All 4 freshly-assembled MSV3 boards taken through SWD bootloader install + USB DFU bring-up flash,
all confirmed alive with no hangs (LFRC active, FDC2214 correct, clean BLE init/advertise/stop).
`scripts/swd_flash_hex.py` gained a one-shot `--bootloader` mode requiring 3s of continuous stable
contact before erasing (a single-read check was confirmed insufficient on real hardware first).
`DIV_NODE`/`VDIV` bodged from digital-only `P0.00` to analog-capable `P0.03`/AIN1 per the fix plan
in `docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 5; firmware updated to a real `analogRead()`.
Found and fixed a real bug this introduced (old `P0.00` trace still live in parallel on the PCB,
left unconfigured instead of disconnected). Found one board with a genuinely dead mux (`muxsweep`
shows zero switching) and, separately, one board with a `VDIV` net pegged near 3V regardless of mux
code (points at `R17` or the mux's `VDIV`-throw pins, not yet confirmed which). Closed a real open
item from the bench test plan: first confirmed real-bridge EC-check test on this design, both
digital and analog paths, on a third, healthy board. Full detail, per-board results table, and open
items: `docs/MSV3_FOUR_BOARD_SWD_BRINGUP_2026-08-28.md`.

## 2026-08-31: EC-check distilled-water / dry-soil bench check (confirmed-good board)

Following up the 8/28 finger-bridge EC-check confirmation (`mux 2` + `divnode`, analog `P0.03`
bodge) with two real substrates on the same board. `divnode` (EC-check, mux 2, `A=3V3 B=VDIV`)
read `0/4095 (0.0 mV)` in both distilled water and dry soil, across 7 reads each. Electrode-swap
phase (`fdcstream`, mux 1) `DATA_CH0` shifted between the two conditions (~15.93M in the distilled-
water run vs. ~20.34M in the dry-soil run) — a plausible dielectric response to water immersion on
the capacitive tank channel, not yet a controlled A/B (different runs, not alternated back-to-back
on the same setup), so not conclusive on its own.

**Reading:** this is the expected result, not evidence of a mis-sized `R17`. Distilled water has
almost no free ions (poor conductor by design) and dry soil has no pore moisture to carry current —
neither substrate should produce an EC-check signal, and neither did. Combined with the 8/28
finger-bridge test (which did read nonzero), the EC-check path has now shown correct behavior at
both ends: signal on a genuinely conductive bridge, silence on two non-conductive ones. The
open item is still what Test 5/6 originally called for: **moist** soil (real pore-water conductivity,
not bone-dry) or a salted-water reference, to see where a real deployment substrate lands in the
range and inform the `R17` re-derivation already flagged in
`docs/MSV3_BENCH_TEST_PLAN_2026-08-21.md` Test 5.

**Same-day follow-up — soil moistened with distilled water:** `divnode` stayed effectively at the
noise floor (17 reads: fifteen at `0/4095`, two at `1-2/4095` / 0.7-1.5 mV) — still no real
EC-check signal. Expected, for the same reason as above: distilled water carries the moisture but
contributes no ions of its own, so this only tests whether the soil's *own* dissolved
salts/minerals are enough to register, not whether the sensor works. `DATA_CH0` (mux 1,
electrode-swap) sat at ~18.00-18.01M and drifted slightly upward over a 10-sample stream —
between the dry-soil value (~20.34M) and the direct-distilled-water value (~15.93M) from the
earlier runs. That ordering (dry highest, moist soil in between, standing water lowest) is a
physically sensible permittivity gradient on the capacitive tank channel, worth tracking, though
still not a controlled same-session A/B.

**Still open, now more specifically:** this hasn't yet separated "R17 sized wrong" from "the water
used had no ions to detect." The next test that actually distinguishes them is the same soil
wetted with **tap water** (or a pinch of salt/fertilizer dissolved in water) instead of distilled —
if that produces a clear `divnode` jump like the 8/28 finger bridge did, the circuit and `R17` are
fine and today's zero readings were a test-medium artifact, not a hardware one. If it's still flat
with real dissolved ions in the mix, that would actually support suspecting `R17`'s value.

**Same-day follow-up — true dry-air baseline (no substrate at all):** `divnode` over 12 reads:
mostly `0/4095`, occasional `1-5/4095` (0-3.7 mV), same noise-floor character as the "moist soil +
distilled water" run above. This confirms that run's `0-2/4095` readings were indistinguishable
from no substrate at all, not a weak signal — the ADC's real rest-noise floor is ~0-5 raw counts
(~4 mV), so any future detection threshold should sit above that. `DATA_CH0` (mux 1) held tight at
~20.500-20.506M, essentially matching the dry-soil run (~20.34M) and confirming the ordering seen
so far on the capacitive tank channel: dry air ≈ dry soil (~20.3-20.5M) > moist soil (~18.0M) >
standing distilled water (~15.9M) — a clean, monotonic, physically sensible permittivity gradient
across four independent conditions now. The capacitive/moisture channel looks like it's genuinely
discriminating substrate states; the EC-check/conductivity channel still has exactly one
above-noise result (the 8/28 finger bridge) and needs the tap-water/saline test above to get a
second.

**Same-day follow-up — tap water, and a likely root cause:** `DATA_CH0` (mux 1) read ~16.04-16.05M,
close to the standing-distilled-water value (~15.93M) as expected (water's dielectric constant
dominates the tank reading regardless of dissolved ions). `divnode` (EC-check, mux 3, swapped
polarity) stayed at the noise floor across 13 reads (eleven `0/4095`, two `2/4095`/1.5 mV) — no
above-noise signal from a real, ion-bearing conductive medium. Combined with the dry-air baseline
above, this is no longer ambiguous: tap water is definitely conductive, so the EC-check circuit's
lack of response now points at the divider itself, not the test medium.

Confirmed from the real netlist (`MSV3/hardware/MSV3_Probe_PCB/reference/netlist/MSV3_sch.NET`):
`R17` = **100k**, wired `R17-1` to `GND` and `R17-2` to the `VDIV` net (also `MUX-7`, `MUX-11`,
`U1-11`/old `P0.00`). So EC-check is a plain divider, `VDIV = 3.3V * R17/(R17 + R_electrodes)`,
matching every observation to date: huge `R_electrodes` (dry/open) → ~0V, near-zero
`R_electrodes` (wire-short/finger) → full-scale. With `R17` at 100k, clearing even the ~5-count
noise floor needs `R_electrodes` below roughly 80-90 MΩ — tap water reading nothing implies this
electrode geometry's actual resistance through water is far above that, well beyond what bulk
tap-water resistivity would suggest, most likely because the electrode spacing/contact area is
small enough to push the effective path resistance that high. A finger clears it easily because
skin contact area/pressure gives a much lower resistance than these electrodes see in water.

**Likely root cause, not yet fully confirmed:** `R17` (100k) is undersized for the real
dry/wet-salty resistance range this electrode geometry presents — matches the re-derivation this
doc already flagged as an open item once real analog data existed. **Next step:** multimeter the
actual resistance between the two electrode pads directly (once in tap water, once in moist soil)
to get a real number, then size a replacement `R17` around it (geometric mean of the dry/wet range,
per the existing note) rather than more liquid trial-and-error.

**Same-day follow-up — direct electrode-pad multimeter reading, board unplugged (medium not yet
recorded):** measured `>2M` — pegged the meter's own range ceiling, not a real value. **Not yet
confirmed which condition this was taken under** (electrodes in tap water/soil vs. disconnected in
open air) — that distinction matters: dry/open reading `>2M` is trivial and expected, while a
wet/in-medium reading `>2M` would be strong direct confirmation that real `R_electrodes` is well
above the 100k `R17`, not just "somewhere above 90M" as inferred from the earlier `divnode` math.
**Open item:** get a real number past the meter's ceiling — series a known large resistor (e.g.
10M) between a known voltage and one electrode, put the electrodes in the actual medium, measure
the divider midpoint, and solve `R_electrodes = V_meas * R_known / (V_supply - V_meas)` — same
principle as the EC-check circuit itself. That number is what should actually size the replacement
`R17`, rather than picking a value on a guess.

**Same-day follow-up — measurement method, no soldering:** decided to build the known-resistor
divider entirely off-board (breadboard + a separate spare ESP32 as the 3.3V source/ADC reader),
clipping directly onto the same two bare electrode pads the multimeter already touched, with the
MSV3 board fully powered off throughout. Powered off, the TMUX1109's switches sit open/undriven, so
the mux, `R17`, and the E73 are fully out of the path — no need to "isolate" the mux by any special
sequence, and zero risk to the working board's already-fussy mux. Chosen for exactly this reason:
avoids any solder rework on the one confirmed-good board.

**Same-day follow-up — external jig built, results likely confounded by body leakage.** Standalone
ESP32-S3 breadboard jig (`3V3 -> Electrode A -> [medium] -> Electrode B -> GPIO1 -> R_known -> GND`,
`analogReadMilliVolts()`), calibrated against a real wire-short: raw short-circuit reading was
3129.0-3129.5 mV against an assumed 3300 mV, confirmed via direct multimeter (3V3-to-GND on the
devkit reads 3.29V, rail is fine) to be entirely an ESP32-S3 ADC nonlinearity near the top of its
range, not a supply issue — corrected by using the measured short as the live reference instead of
an assumed constant.

With that calibration in place: tap water, distilled water, and soil-wetted-with-distilled-water
all read **~0.8 Mohm** — suspiciously identical. Distilled water should read far higher resistance
than tap water; reading the same number is a strong sign something other than the medium is
dominating all three, most likely the operator's own hand-to-hand body resistance in parallel
across the electrodes while holding bare wire during the dip (same order of magnitude as the 8/28
finger-bridge result, and a ~0.9M body path in parallel with true water resistance in the tens of
Mohms computes to almost exactly ~0.8-0.9M — consistent with what was seen). Also inconsistent with
the onboard `divnode` reading ~0 in tap water: a real 0.8M `R_ext` through the 100k `R17` should
have produced a clearly visible ~367 mV signal there, and it never has.

**Not yet confirmed, needs a hands-off re-test:** repeat with zero bare-skin contact on the exposed
metal (hold only insulated wire, or leave alligator clips attached hands-off once clipped) before
trusting any of today's three numbers for sizing `R17`. No salt/saline reference available yet
either way — not blocking this specific re-test.

**Same-day follow-up — second confound found: DC electrode polarization, resistance climbs with
hold time.** Cross-check against the multimeter: it overloads (`>2M`) on the same wet electrodes
where the jig read `0.8-1M`, which looked contradictory until noticing the jig's reading climbs the
longer the leads are held in the medium. Likely cause: sustained DC across electrodes in an ionic
medium drives electrode polarization (ion migration to the electrode surface, plus electrolysis gas
bubbles at longer hold times), progressively reducing effective contact and raising the *apparent*
resistance over time — a textbook reason simple two-wire DC conductivity probes drift, and part of
why real conductivity meters use AC excitation instead of sustained DC. This reconciles rather than
contradicts the multimeter: both are likely watching the same climb at different points in time,
not disagreeing about a fixed value. Separately validated the jig itself: probing a known 20M
resistor (dry, no electrode/electrolyte effects) reads back cleanly, confirming the circuit and
calibration are sound — this confound is specific to wet-electrode DC behavior, not the
measurement setup.

**Directly relevant to the product, not just this bench test:** this is the same reason the real
MSV3 firmware never holds a sustained DC bias on the electrodes — it takes a fast single-shot read,
and the mux alternates electrode polarity between reads for the related reason of canceling
DC-bias-driven drift. A fast jig reading (taken within ~1s of first contact) is therefore the more
representative number for sizing `R17` than a "let it settle" reading — the settled value reflects
a polarization artifact the real firmware's read pattern is specifically designed to avoid, not the
medium's true resistance. **Open item:** redo tap water / soil readings capturing the fast
(near-instant) value specifically, alongside the hands-off requirement above.

**Same-day follow-up — third confound found and fixed: mux ESD-diode injection while probing
Node A/B on the live board.** Root cause of the multimeter-vs-jig disagreement, confirmed against
the real TMUX1109 datasheet (SCDS406A): Absolute Maximum Ratings lists switch pin voltage (`VS`/
`VD`, Sx/Dx) as **-0.5V to VDD+0.5V** — with the board powered off (`VDD=0V`), that's a hard limit
of +0.5V, and the jig was driving 3.3V onto those same pins, ~2.8V past rated maximum every test.
"Fail-Safe Logic" (section 6.3.4) does **not** cover this: it's explicitly scoped to the control
pins (`EN`, `A0`, `A1`) only, not the switch/signal pins Node A/B connect to. 3.3V is well above a
silicon diode's ~0.5-0.6V forward-conduction threshold, so this was almost certainly forward-biasing
the switch pins' internal ESD/clamp diodes into the floating (unpowered) VDD rail, injecting a
parasitic low-impedance path between Node A/B that has nothing to do with the medium — explaining
both the multimeter disagreement (its ohmmeter test voltage stays well under that threshold, so it
never triggered this path and was correctly reporting the true, much higher resistance) and the
run-to-run scatter (floating VDD net's charge state varies). 10M in series limited the fault
current to sub-microamp, so real damage risk to the board was low, but this is not a valid
measurement path regardless. **Fix:** moved the divider to a spare unpopulated probe fork (same
electrode geometry, zero chip in the path) instead of Node A/B on the working board.

**Result on the clean setup — distilled water: 170-190 Mohm.** First trustworthy R_ext number in
this whole investigation. Confirms the `divnode` noise-floor reads were never a broken circuit:
at the current `R17`=100k, even this reading would produce `VDIV = 3.3V * 100k/(100k+180M) ~= 1.8
mV` — below the ~5-count (~4mV) noise floor established by the dry-air baseline, in *distilled*
water specifically (the least conductive realistic wet case). `R17` isn't wrong by a little, it's
off by roughly three orders of magnitude from what's needed to see this. **Open item:** get a real
soil number on this same clean (spare-probe, hands-off) setup — that's the deployment-relevant low
end of the range. Once both ends exist (soil low end, distilled-water-or-better high end), size
`R17` near their geometric mean per the original bench-plan note.

**Same-day follow-up — first multi-medium comparison came back scrambled, then resolved cleanly
after two more fixes.** Initial 10M-resistor pass across school tap water, the campus filtered-
water bottle-filler, and soil wetted with the filtered water gave `600-700M` / `200-220M` / `550M`
respectively — tap water reading *less* conductive than filtered water, and everything clustered
within one order of magnitude despite media that should differ far more. Considered (and initially
over-weighted) a soldermask-over-the-electrodes theory: since the nodes have no bare-metal contact
with the medium, each electrode is technically a small capacitor in series with the medium's real
resistance, and a step-response derivation showed a decaying-voltage-read-as-a-static-resistor
would exactly produce the earlier "resistance climbs the longer I hold it" symptom. That theory was
walked back once new data came in: on the clean spare-probe setup (no mux/chip in the path),
readings now hold steady while held, which is much better explained by the already-diagnosed mux
floating-VDD-diode charging effect (present in the earlier, mux-connected measurements the "climbs
with hold time" observation was actually made on) than by an electrode-mask RC decay that would
still be present regardless of whether a chip is anywhere in the circuit. Soldermask is real
(no bare-metal galvanic contact exists here) but is not the dominant confound at these resistance
levels — the scrambled comparison is attributed to human-timing inconsistency between the three
separate cup tests instead, now considered resolved rather than open.

Second fix: swapped `R_KNOWN` from 10M to 20M (the largest resistor on hand) since `R_ext` was
consistently landing far above 10M, deep in the compressed/low-voltage part of that divider's
range — roughly doubles the signal voltage at every reading for `R_ext` in the 100M+ range that
this design is actually operating in.

**Clean result, physically sensible for the first time this session:**

| Medium | R_ext |
|---|---|
| Tap water | ~200M |
| Moist soil | ~500M |
| Filtered water | ~800-900M |
| Air (open) | ~1000M |

Ranking now matches expectation (tap water more conductive than filtered water, since filtration
strips some of the dissolved minerals; soil in between; open air least conductive) and is
internally consistent with the earlier "soil + filtered water" result (550M then vs. ~500M for
moist soil now). Top end (filtered water, air) is expected to be less consistent/resolvable — the
signal there is smallest and most noise-sensitive with any fixed reference resistor — which doesn't
matter for the actual use case (distinguishing "needs water" from "well-watered" only requires
resolution in the wet range, not at the dry end).

**Provisional `R17` recommendation:** geometric mean of the realistic wet-to-near-dry range
(~200M to ~900M) is `sqrt(200M * 900M) ~= 424M` — a standard value of **330M or 470M** would center
the divider's sensitivity where irrigation decisions actually get made. Provisional because (a) no
salty/high-conductivity reference exists yet to firm up the true low end (real wet-salty soil could
read lower than plain tap water), and (b) this hasn't been validated with an actual `R17` swap and
retest on real board hardware yet — still a breadboard/spare-probe estimate.

## 2026-08-31 (closing): EC-check invalidated at the design level — soldermask/coating block it,
## not the resistor. Four-board mux triage clean. Status below.

**The `R17`-sizing effort above, while real and now in the BOM, does not make EC-check work.**
Follow-up question crystallized the actual blocker: the electrode nodes have soldermask over them
("there always has been and always will be" — not a manufacturing accident, and conformal coating
is about to add a second insulating layer on top). A DC resistive conductivity measurement needs
actual bare-metal galvanic contact with the soil; soldermask (+ soon coating) removes that
entirely, regardless of `R17`'s value. Extrapolating the divider math to what conformal coating
would additionally require (~10G-range `R17`) makes the underlying problem explicit: at that scale
the "signal" (real soil resistance) becomes comparable to every other uncontrolled leakage path on
the board (PCB surface leakage, mux off-state leakage, ADC input leakage) — the measurement stops
being specific to soil conductivity at all. There is no resistor value that fixes a missing
galvanic path. This is also consistent with a real, stated design goal (limiting current leakage
into the soil), not just an accident of PCB fab defaults — soldermask and EC-check are directly in
tension, and soldermask wins.

**TMUX1109's remaining justification, now that EC-check is off the table:** two functions survive,
both already real and unrelated to soil conductivity — (1) **electrode-swap** (mux codes 0/1),
which periodically reverses which physical electrode is sense vs. reference on the *capacitive*
tank measurement, to cancel DC-bias-driven electrode polarization/migration (this predates
EC-check in the design history and has nothing to do with it); (2) **bare-tank reference read**
(mux codes 2/3, repurposed from the dead EC-check use), disconnecting the electrodes for a
parasitic-only capacitance baseline, already live in the production firmware for
temperature/drift compensation. Confirmed via the real FDC2214 datasheet that its own
`MUX_CONFIG`/`AUTOSCAN` feature is unrelated — that's channel-to-channel scanning across up to 4
independent tanks, not the electrode-polarity swap TMUX provides; no redundancy, no overlap.
**Forward-looking note for a future PCB revision only, not this batch:** EC-check specifically was
what pushed this design to a full 4-destination-per-electrode mux (TMUX1109's shape — each
electrode reaching `TANK_HOT`/`TANK_REF`/`3V3`/`VDIV`). The two surviving functions only need 3
destinations per electrode (two swap roles + disconnect), so a cheaper/simpler mux could replace
TMUX1109 next time. Not actionable on the four boards already built.

**Bodge-wire disposition:** the `P0.03`→`VDIV` EC-check bodge wire is being removed from all four
boards for a clean seal under conformal coat (cosmetic/reliability call, user's own decision — not
required electrically, since `P0.03` was unused before this bodge and costs nothing left in place).
Precedent already exists on this board for the equivalent situation: the old `P0.00` trace also
stays physically wired to `VDIV` in parallel and is just `gpioDisconnect()`'d in firmware rather
than removed. Once the bodge wire is gone, `divnode`/`DATA_CH0`-via-`VDIV` reads no meaning at all
and should not be treated as data — not that they ever were valid soil-conductivity data to begin
with.

**Real FDC2214 tank capacitance, measured (see the two same-day entries above)**: fresh/never-wet
dry ~50-60pF (matches original ~64-96pF bare-sensor RC estimate), fully wet ~500pF, moist soil
~400-500pF, "dried after wet" plateaus ~360pF (residual moisture, not a fault). Both
`MSV3_PROBE_BOM.md` and `MOISTURE_SENSOR_FDC2212_FRONTEND.md` open items 1 and 7 updated with this
and the `R17` finding. **Superseded 2026-09-14:** the earlier conclusion that padding cap C17 had to
stay relied on treating 300 pF as a hard device floor. Probe 8 subsequently proved the connected
approximately 88 pF capless tank stable at IDRIVE 16 and measured about four times the prior raw
response. C17 is now DNP; the resulting disconnected bare tank is over frequency and production
must never select it. See `MSV3_CAPLESS_NO_BARE_TANK_POLICY_2026-09-14.md`.

**Four-board mux/tank triage, same day** (full detail, per-board data, and pass/fail methodology:
`MSV3_FOUR_BOARD_MUX_TRIAGE_2026-08-31.md`): built to answer "does the mux still switch, and is
the FDC tank reading real" now that EC-check itself is out of scope. Reused this project's own
already-validated threshold (~11% step = healthy, <1% = dead mux) rather than inventing a new one.
**All four boards passed.** Board 1 (previously marked broken from EC-check symptoms) and board 2
(previously marked working) both confirmed healthy — the "broken" mark on board 1 is now understood
as a false positive from testing a feature that can't pass on any board, not a hardware fault.
Board 3 clean. **Board 4 was initially fully dead** (`DATA_CH0` pegged at the 28-bit max sentinel
`0xFFFFFFF` on every mux code, `STATUS` never showing `ERR_WD`/`ERR_AHW`/`ERR_ALW` despite the
stuck reading) — ruled out an `idrive` drive-current mismatch (0-31 sweep, no change; the sweep's
own instant read-back was shown to be a timing artifact, confirmed by a clean separate `fdcinit`+
`fdcread` reproducing the identical stuck value) and ruled out a soft/stuck firmware state (a full
hardware reset via `sleep`→`wake`, genuine FDC2214 SD-pin shutdown with "registers cleared," made
no difference). `L2`'s DC resistance measured normal throughout, which didn't contradict the real
cause since a misaligned-but-still-contacting part can pass DC continuity while failing real
oscillation. **Root cause: `L2` was physically seated sideways** — found by inspection, fixed by
reseating, retested clean (11.37% step, matching the other three boards).

---

### Status: EC-check (soil-conductivity/salinity sensing) — **invalidated, not pursuing further**

- Root cause: soldermask over the electrodes (permanent, by design/fab default) blocks the
  galvanic contact a DC resistive measurement needs; conformal coating about to make it worse, not
  better. No resistor value fixes a missing conductive path.
- The `R17` sizing work this session (100k → 500M, Ohmite HVC0805Z5006JET, $1.13, now in the BOM)
  is real and harmless to leave in place, but does not make EC-check functional — it was answering
  the wrong layer of the problem, discovered only after the sizing work was already done.
- `divnode`/`VDIV` should not be read as meaningful data going forward on any board. The bodge wire
  enabling that analog read is being removed from all four boards (cosmetic/coating reasons, not
  required electrically).
- What still works and matters: the primary capacitive moisture tank (FDC2214) is healthy on all
  four boards as of today's triage, with more real signal range than originally estimated. TMUX1109
  still earns its keep on electrode-swap (polarization cancellation) and bare-tank reference
  (temperature/drift compensation) — both independent of EC-check's failure.
- Open, not urgent: settle-vs-polarization timing window for the moisture tank reading itself
  (flagged in `MOISTURE_SENSOR_FDC2212_FRONTEND.md` open item 7); a future PCB revision could drop
  to a simpler/cheaper mux now that EC-check no longer demands the full 4-destination-per-electrode
  shape.

## 2026-09-03: MSV3 24-hour water immersion + air recovery — clean, and the bare-tank reference
## channel is validated against a forced step

Full detail, numbers, and caveats: `MSV3/docs/MSV3_WATER_IMMERSION_RECOVERY_2026-09-03.md`.

One MSV3 (sensor ID 4, `MSV3-MOISTURE-004` -- corrected 2026-09-03 from an initial misattribution to
sensor 5; confirmed against `msv2_sensor_history`, where only sensor 4 shows the immersion step, and
sensor 5 was never immersed. Production `MSV3_Moisture_BLE_Sleep` firmware, cloud
history at the normal 1/minute cadence) sat fully immersed in a cup of tap water for ~24 hours,
then was lifted out into open air.

- **Signal magnitude:** normalized capacitance-above-bare went ~23-24% (air) -> ~118% (immersed)
  -> 23.8% (air again). That is a **4.96x change in electrode-contributed capacitance**, ~94
  percentage points of swing. Raw connected `DATA_CH0` ~20.35M dry, ~15.33M wet. Latest row checks
  exactly against `msv3ReferenceExcessPercent()`: `(22632650/20341560)^2 - 1 = 23.795%`.
- **No recovery tail.** The removal edge is as vertical as the entry edge — baseline within about
  one sample. Expected for a water cup (no porous medium), but the *absence* of a slow multi-hour
  decay is the real content: water wicked into the laminate or under soldermask would dry out over
  hours as an exponential tail on the connected channel, and after 24 hours of immersion there was
  none. Addresses the ingress worry in `MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md`
  for the clean-water case only — it does **not** replace that document's scrap-FR-4 coupon soak,
  which is about long-term wet *ionic soil*.
- **No conflict with the ~360 pF dried-after-wet plateau** in the 2026-08-31 entry above. That was
  measured in soil, which holds residual water against the blade; this test has no medium. Full
  baseline return in water is not evidence the soil plateau was wrong.
- **Bare-tank reference validated.** It held 22.62-22.64M across the whole three-day window with no
  step at either edge, while the connected channel moved 4.6M counts — direct evidence the swing
  was in the electrode path, not a board-wide parasitic/supply/thermal shift. This channel had only
  ever been checked against ambient drift before; this is the first time it has been held still
  against a large deliberately-forced step. The normalized trace also stays flat across a ~6 degC
  diurnal swing in both wet and dry states, which is the thermal cancellation working as designed.
- **Residual offset is below resolution** — post-soak 23.8% vs a pre-soak baseline reading ~23-24%
  off the chart. Any residual is under ~1 point against a ~94-point signal.
- **The jagged board-temperature sawtooth on the final day is room AC cycling** (maintainer
  confirmed), not a fault. Bare-tank goes jagged in step with it, which is the expected signature,
  and the band started before the probe came out of the water. Unrelated to the Alocasia GP AFE
  rig's mains-coupling bands — different node, different mechanism.
- **Open/unrecorded:** the coating state of this specific board at test time was not written down,
  which limits how far the ingress result generalizes. Still open elsewhere: the dry-vs-pre-soaked
  matched-blade A/B (`MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md`), ionic/salty media, and the
  settle-vs-polarization timing window (`MOISTURE_SENSOR_FDC2212_FRONTEND.md` open item 7).

## 2026-09-03/04 (overnight): MSV3 BLE OTA works end to end — jump, transfer, boot, on a deployed
## probe on battery. Full detail: `MSV3/docs/MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md`.

**The headline: a conformal-coated probe in soil can now be updated with no cable and no SWD.**
Proven on probe 4 (sensor 4) sitting in its pot on battery: jump -> bootloader -> signed transfer
-> application boot, 34 s, and **13 mV of battery** for a complete firmware update (3983 -> 3970 mV).

### Two root causes, both found by device-side instrumentation after three wrong theories

- **Firmware.** A write authorize reply carrying `BLE_GATT_STATUS_SUCCESS` must ALSO set
  `update = 1` and hand `offset`/`len`/`p_data` back to the SoftDevice. Replying SUCCESS with
  `update = 0` is rejected with `NRF_ERROR_INVALID_PARAM`, and a rejected reply is never
  transmitted — so the client's write hung ~28 s until the application's own idle-peer disconnect,
  and the board never reset. Error statuses need none of this, which is why every rejection path
  worked and only the success path — the one that mattered — was silently dead. The probe's own
  log said it in one line: `checkpoint=8 op=1 gattStatus=0x0 replyRc=7`.
- **Client.** After a buttonless jump the bootloader keeps the APPLICATION's address (the
  peer-data handoff carries it across the reset). `address + 1` applies only when the bootloader
  starts with no valid application. The client was scanning an address nothing was on and
  reporting "the jump did not take effect" for a jump that had worked. Both behaviours were then
  observed on the same probe within an hour and both are handled.

Also corrected and worth keeping: the reset no longer happens inside the BLE event callback
(`NRF_SUCCESS` means the SoftDevice accepted the response, not that it was sent), every path out of
the authorize handler replies exactly once, and the power gate is evaluated before the reply rather
than after — previously it could never reject anything visibly.

**Method note.** Prepared writes, Bleak version and the Windows GATT cache were each plausible,
each cost a full test cycle, and each was wrong. One instrumented build answered it. On this board
a flash is ~30 s: instrument early instead of reasoning further from source.

### Speed measured to its ceiling

`--profile` partitions a transfer's wall clock. Writes are negligible (~0.44 s); packet-receipt
waits are essentially the whole cost. Per-packet cost is flat at 26-31 ms regardless of PRN, so the
constraint is packet delivery, not receipt round-trips — PRN 16 is the measured optimum and PRN 32
is worse. **Practical transfer-phase ceiling ~8-10 kB/s (~16 s for a 131 KB image).** Beating it
needs shorter connection intervals compiled into the bootloader, which needs SWD; not worth it,
since the transfer is already the smaller half of the wall clock. Throughput tracks signal, not
settings: -55 dBm 28 s, -58 dBm 34 s, -65 dBm 32 s, -74 dBm 47 s.

### Probe 4 migrated off the stock bootloader without SWD

USB carried the bootloader, BLE carried the application. The serial DFU step "fails" at the
application stage having already written SD+BL — expected, because the new bootloader's
`DEFAULT_TO_OTA_DFU` removes USB CDC and the port disappears. That is the documented migration
path for the rest of the fleet.

### Tooling

All three flashers moved to the repository root `scripts/` — `ble_ota_update.py` (primary),
`ble_dfu_recover.py` (recovery), `swd_flash_hex.py` (SWD). MSV3-only release machinery stays under
`MSV3/scripts/`. The move also repaired eight doc references that had pointed at
`scripts/swd_flash_hex.py` since before the 2026-08-29 reorg filed it under `MSV3/`.
`swd_flash_hex.py` gained `--wait-contact` (hold pogo pins without forcing a mass erase) and had
its stale default hex path fixed.

`ReceiptGuard` in `ble_ota_update.py` closes upstream dfu_lib's "PRN Timeout, continuing anyway",
which had previously produced a corrupted image that failed Receive Firmware Image with status 6.
It validates each receipt's reported byte count against what was actually sent, tolerates the
clear()-after-receipt race, and is armed only between Receive Firmware Image and Validate so
connection-phase cancellations cannot poison a retry — a false positive that cost one aborted
attempt on the live probe before it was narrowed. `scripts/test_receipt_guard.py` covers the
dropped / delayed / early / stale / duplicate / wrong-offset / cancellation cases; 12 tests pass.

### Data integrity

79 polluted rows removed from `msv2_sensor_history` for sensor 5 — the bench board briefly ran as
sensor 5 and interleaved into the real probe's stream. Identified by value (20.42M vs the real
probe's 19.42M, an 866k-count gap) inside a 92-minute window, and confirmed to be the only sensor-5
rows ever above 20M. Pollution stopped by re-IDing the bench board to sensor 99, which is above the
receiver's `MAX_SENSOR_ID = 16` and therefore dropped before it reaches the cloud.

### Open

- ~~**Mutated-image negative tests not done.**~~ **Done 2026-09-05** — see
  `MSV3/docs/MSV3_TAMPERED_IMAGE_REJECTION_2026-09-05.md`. Seven mutations were offered to probe
  99; each one's rejection stage and status byte was predicted from the bootloader source first,
  and all seven predictions held. Payload and length mutations are refused at Validate with status
  5 (`CRC_ERROR`), signature mutations at Init Packet with status 6 (`OPER_FAILED`). The corrupted
  build id was never observed running. The claim is now scoped rather than absolute: replay of an
  older validly signed release is **untested and expected to be accepted** (the version-check hook
  in `dfu_init.c` is an unimplemented upstream template), and a validly signed image, key
  compromise and parser defects remain outside it.
  - The load-bearing operational finding: **`dfu_start_pkt_handle()` erases the application before
    the init packet is even received**, so no device-side rejection is free. Even a signature
    failure, caught before a single image byte is sent, costs the running firmware. That is why the
    host-side preflight is not redundant with the device's check — on a coated probe it is the only
    thing protecting availability.
  - Three harness bugs were found and fixed en route, all of the shape *"concluded rejected from
    the absence of success"*: a failed BLE scan, a successful install, and a silent (timed-out)
    device were each scored as a refusal. The second is the dangerous one — a device that
    **accepted** a tampered image would have been reported as refusing it.
- ~~The bench board runs `MSV3-MOISTURE-099` built from a local edit.~~ **Resolved 2026-09-05.**
  Identity, serial logging and the build tag are build parameters
  (`arduino_msv3_probe.ps1 -SensorId <n> [-SerialLogs] [-BuildTag <tag>]`), and the production
  sketch `#error`s if `MSV3_SENSOR_ID` is missing. There is no longer a line to edit or to remember
  to put back, and `(commit, sensor id, build tag)` reproduces an image exactly.
- Probes 1, 2, 3 and 5 are still on the stock bootloader with pre-OTA firmware. Each needs the same
  one-time USB migration before it can be updated over BLE.

## 2026-09-05: FDC2214 ERROR_CONFIG was gating the watchdog flag, and the status was being thrown
## away. Full record: `MSV3/docs/MSV3_FDC_ERROR_CONFIG_2026-09-05.md`.

Closes the `ERROR_CONFIG` item in `docs/KNOWN_ISSUES.md`, and corrects it in two places.

- **`fdcInit()` now writes `ERROR_CONFIG = 0x0020` (`WD_ERR2INT` only)** in both sketches, before
  the `CONFIG` write that starts conversions. The register resets to zero on every hardware
  shutdown, which this firmware performs on every wake, so it has to be rewritten every time —
  verified across six `probeSleep()`/`probeWake()` cycles.
- **The gate is measured, not inferred.** Same induced fault throughout, register toggled
  underneath it: after any previously latched status had been cleared, `ERR_WD` was reported on 6 of
  6 settled reads with the bit set, **was not newly reported with it clear (0 of 6)**, and returned
  on 6 of 6 when re-set. Healthy tank clean either way. Reproduced on two independent runs with
  identical raw numbers. That is what turns "switched off, not clear" from a datasheet reading into
  a result.
- **Correction to the old wording:** only `ERR_WD` was ever gated. `ERROR_CONFIG` has no
  `AH_WARN2INT`/`AL_WARN2INT` bit, so `STATUS.ERR_AHW`/`ERR_ALW` are not gated by it — a statement
  about configuration semantics, not a demonstration, since neither amplitude warning was induced
  on hardware. The `*2OUT` bits report into `DATA_CHx`, a different destination.
- **Correction to my own first framing:** `ERR_WD` was never the detector for a permanently dead
  sensor. A watchdog timeout produces no unread conversion, so `CH0_UNREADCONV` never sets and the
  read fails on the readiness timeout — which always worked, and which `guard 1` already turns into
  advertise-but-mark-invalid. Calling the `errWd` branch "dead code guarding the failure it exists
  to catch" was too strong.
- **The real defect, now fixed.** `fdcWaitForCh0UnreadConversion()` polled `STATUS` and discarded
  it on timeout. `STATUS` is read-to-clear, so the polling loop consumes the flags it looks past,
  and on a dead tank the loop always times out — one poll returned `0x800` (`ERR_WD`) and the code
  threw it away. It now **OR-accumulates** every flag seen and returns it even on a timeout (OR,
  not last-value, or an earlier poll's flag is still lost).
- **That is a behaviour change, not just a diagnostic one, and the docs say so.** On the timeout
  path nothing moves. But a successful read now returns the union of what was seen while polling,
  so a watchdog event during the poll reaches the `errWd` branch and that one reading is discarded
  — measured as `STATUS=0x808` with a valid count on the first conversion after a fault cleared.
  Conservative, costs at most one sample per fault (read-to-clear), but it changes what gets
  stored. That same sample is the only evidence in the run that the `errWd` branch is reachable at
  all, since no `IDRIVE` code flags a completed conversion directly.
- **A negative result worth keeping:** an `IDRIVE` sweep of codes 0–12 found no marginal setting on
  this board. Code 0 stops conversions (1 of 6 complete); codes 1–12 complete 6 of 6 with no flag.
  It is a cliff, not a ramp, so "conversion completes but is flagged" cannot be induced directly —
  which is why the accumulation fix is what made the gating observable at all.
- **Staged on probe 99, not probe 4.** No false `ERR_WD` in 58 healthy bench measurements (40 soak
  + 18 post-wake, six cold wakes). That is a bench sample on one board, not a statement about
  temperature, supply or long-run behaviour — and a false positive silently takes a *healthy* probe
  off the air, which is worse than the fault it detects. Probe 4 after a longer probe-99 run.
- Open follow-up, deliberately not folded in: the accumulated status is available where the probe
  decides to advertise a fault, but the advertisement still says only "invalid". A reason code
  would let the receiver separate a dead oscillator from a dead bus, and that changes the
  probe/receiver contract.
