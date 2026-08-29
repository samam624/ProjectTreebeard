# MSV3 Probe — Lab Bench Test Plan

**Date:** 2026-08-21
**Board:** MSV3 probe PCB, on `COM3`, FQBN `adafruit:nrf52:pca10056`
**Firmware:** `arduino/MSV3_Probe_Bringup_Test/` — flashed and lab-ready, including `sleepbench`
**Baseline to compare against:** `DATA_CH0` ≈ 20.33 M (dry electrodes, USB power)

> Build/upload only through `scripts/arduino_msv3_probe.ps1`. A stock PCA10056 build selects a
> nonexistent LF crystal and can watchdog-reset in `Bluefruit.begin()`. The wrapper repairs and
> verifies LFRC, cleans stale objects, and supplies the NFC GPIO flags. See `docs/KNOWN_ISSUES.md`.

Everything through Phase 6 already passes on USB power. This session closes the parts that need a
battery, a light source, and a scope — plus the three firmware readings that have never been
checked against physical ground truth.

An interactive version of this checklist (with persistent checkboxes) is published as an Artifact;
this file is the offline copy that travels with the repo.

---

## Bring

- LiPo cell, plus header/connector and a soldering iron
- Multimeter (voltage + µA range)
- Current meter — Nordic PPK2 or Joulescope ideal for Test 2
- Oscilloscope with a 10× probe (Test 4)
- The real deployment solar panel
- A bright lamp, and access to a window
- Wet soil or a cup of water (Tests 5–6)
- USB-C cable + laptop

---

## Test 1 — Battery, and the three unverified readings

> **Gates Tests 2 and 3. Do this first.**

Three separate firmware bugs were found on 8/21, and all three are still unproven for the same
reason: no cell has ever been attached to this board, so any self-consistent number passed.
Attaching a battery and comparing against a multimeter closes all three at once.

- [ ] Solder the battery header / connector
- [ ] Measure cell open-circuit voltage on the multimeter — **write it down**
- [ ] Attach the cell, then run `vbat` and compare
- [ ] With USB plugged in, run `chgstatus` — STAT should now read LOW (charge in progress)
- [ ] Unplug USB, run `chgstatus` again — PG should flip HIGH
- [ ] Confirm the board boots and runs on battery alone — the only real test of power mux Q1
- [ ] On battery power, re-run `i2cscan`, `fdcid`, `fdcread` — should match the USB baseline

**Pass:** `vbat` reads within a few percent of the multimeter, and *tracks* as the cell charges or
discharges. Record the exact ratio — that becomes the calibration constant.

| If `vbat` vs multimeter is… | Then |
|---|---|
| ≈ 2× too high | Divider ratio applied twice, or R29/R30 aren't the 100k/100k the netlist claims |
| ≈ 17× too low | The ADC reference/resolution fix didn't take — regressed to 10-bit @ 3.6 V default |
| ≈ (V_bat + 3.3)/2 | Divider polarity regressed — EN pin driven HIGH instead of LOW |
| off by < 5% | Normal tolerance. Record the ratio and move on |

**Why STAT matters:** until 8/21 the firmware read STAT as a plain input, but the board has **no
pull-up** on that net and the pin is open-drain — so it was reading a floating pin and reported
"charging" with no battery present. It now correctly reads HIGH ("no battery"). A real cell
charging is what proves the LOW state works too.

---

## Test 2 — Sleep current

> **Needs Test 1.**

The whole-board sleep sequencing is entirely datasheet-derived — no current has ever been measured
on this board. This is the single measurement that confirms it landed.

- [x] Put the meter in series with the **battery** line (USB must be out — the CDC peripheral alone swamps µA)
- [x] Run `sleepbench 300`, then immediately unplug USB
- [x] Record the steady sleep current
      **Measured: ~71µA, BLE never initialized this boot.** Well above the ~10µA pass bar. Ruled
      out by direct check: charger-status pull-up left on (code always releases it), FDC stuck in
      Sleep not Shutdown (SD pin confirmed a clean 3.3V while asleep), mux parked on an EC-check
      code (confirmed code 0, electrode-swap, and both tank nodes read 0V). **Root cause still
      unidentified** — real excess above the datasheet-derived floor, not yet explained by anything
      in the troubleshooting table above.
- [x] Repeat after `blestart` to include the SoftDevice's LFRC calibration cost
      **Measured: ~60-70µA** (`blestart` then `blestop` before sleeping, so SoftDevice stays
      initialized but isn't actively advertising). Statistically the same as the BLE-off figure —
      the SoftDevice's background idle cost isn't distinguishable from the existing ~65µA unknown
      excess at this measurement precision.

**ROOT CAUSE FOUND AND FIXED (2026-08-21).** `probeSleep()` released `VBAT_DIV_EN` via plain
`pinMode(PIN_VBAT_DIV_EN, INPUT)` instead of `gpioDisconnect()` — on this core, `INPUT` means
high-Z *direction* but the digital input buffer stays **connected**. Both divider pins can float
toward VBAT (up to ~4.2V) while the chip's IO supply is 3.3V, which is exactly the CMOS
shoot-through condition the code's own `INTB`/`DIV_NODE` handling was already written to avoid —
just never applied to the divider pins themselves, or to `VBAT_DIV`, or to charger `STAT`/`PG`
(which only get `gpioDisconnect()`'d *after* a `chgstatus` call runs — sitting connected if sleep
happens before one ever has). Fixed by explicitly `gpioDisconnect()`-ing all four
(`VBAT_DIV_EN`, `VBAT_DIV`, `CHG_STAT`, `CHG_PG`) inside `probeSleep()`.

Three prior hypotheses (charger pull-up, USBD low-power mode, HFCLK/HFXO stop) were each tested
and cleanly ruled out by direct A/B measurement before this was found — see the negative results
above/below. This was the actual cause.

- [x] Re-measured after the fix: **10-15µA**, BLE off. Right at the ~10µA pass bar and much closer
      to the ~4.1µA predicted floor than the prior ~65-71µA.

      **Remaining ~5-10µA above the typ-component floor: plausibly explained, not a new concern.**
      `gpioDisconnect()` disables the digital input buffer but not the pin's ESD protection diodes
      to VDD — a separate structure. `VBAT_DIV` can still float toward VBAT through R29; once it
      exceeds ~VDD+0.3V (~3.6V) the clamp diode conducts, giving `I ≈ (VBAT−3.6V)/100kΩ` — ~1µA at
      3.7V, ~6µA at 4.2V, matching both the observed battery range and the residual gap. Not
      confirmed by direct measurement (would need probing `VBAT_DIV`'s actual clamped voltage
      during sleep), but physically sound. Rest of the gap is normal typ-vs-max/measurement-method
      slop: the 4.1µA budget uses typical not worst-case figures, a plain DMM doesn't average
      periodic watchdog/BQ25504 pulses as cleanly as a real current profiler would, and
      charger/harvester max (vs typ) quiescent figures alone could add another 1-3µA.

      **Solar margin correction:** comparing 10-15µA sleep draw against the 50µA dim-indoor I_SC
      directly (as stated earlier in this doc) was wrong — those are at different voltages, and
      50µA is *short-circuit* current (measured at V=0), not usable power. Comparing power
      instead: sleep is ≈37-56µW at 3.7V; the panel's V_OC×I_SC = 2.7V×50µA ≈ 135µW is only a
      theoretical ceiling, before real maximum-power-point tracking and conversion losses bring it
      down further. Real margin is positive but much thinner than "3-5x" — solar most likely works
      now, but isn't confirmed with real confidence until the full `blecycle` average (below) is
      measured, since that's the actual average draw solar has to beat, not just the sleep floor.
- [ ] Also capture peak current during a wake, and the average across one full `blecycle`

| Predicted contributor | Typ |
|---|---|
| nRF52840 — System ON, RAM retained, RTC wake on LFRC | 3.16 µA |
| BQ25170 — quiescent draw from battery, charge disabled | 0.35 µA |
| BQ25504 — V_STOR quiescent | 0.33 µA |
| FDC2214 — shutdown | 0.20 µA |
| TPS7A0233 — I_Q | 0.025 µA |
| TMUX1109 — leakage | < 0.01 µA |
| **Predicted floor, BLE off** | **≈ 4.1 µA** |
| + SoftDevice LFRC recalibration, BLE on | +1–2 µA |

**Pass:** anything under **~10 µA** means the sequencing works. At 5 µA a 150 mAh cell lasts years,
so the exact figure matters far less than the order of magnitude.

| If it reads high by… | Look at |
|---|---|
| ~35 µA | FDC is in Sleep mode, not Shutdown — SD pin isn't going HIGH |
| ~33 µA *only in wet soil* | Mux parked on an EC-check code, leaking 3V3 through the soil |
| ~250 µA | A charger-status pull-up was left enabled after a read |
| ~700 µA or ~1.4 mA | SDA and/or SCL held LOW — TWIM not disabled |
| milliamps | USB still enumerated, or the LED is on |

**Expect ~1 Hz blips.** The nRF52840's watchdog cannot be stopped once armed, so the CPU surfaces
briefly about once a second to feed it. That's microseconds of CPU time — a fraction of a µA
averaged — and it's what the shipping firmware does anyway. Measure the average, not the floor
between blips.

---

## Test 3 — Solar harvester (BQ25504)

> **Needs Test 1. Needs light.**

**2026-08-22 overnight plan:** battery was at 4.25V (near the ~4.26V VBAT_OV ceiling) after the
8/21 cold-start/latch test, leaving little headroom to observe real charging. Deliberately draining
it overnight on battery power via `led on` (status LED, ~1-3mA through R16, USB unplugged) so
tomorrow's daylight test starts from a real, meaningfully depleted state instead of already-nearly-full
— gives an actual test of whether the harvester can recharge from a realistic discharge, not just
re-confirm the cold-start/latch mechanism already proven 8/21. Check VBAT in the morning before
assuming any particular state of charge.

The open question is whether indoor light actually outpaces the sleep budget. That's an empirical
question about your panel in your light — measure the panel first, then the harvester.

- [x] Measure panel V_OC and I_SC **under the actual indoor light you plan to deploy in** — not at a window, not under a lamp, unless that's the real condition
      **Measured (super-low-light indoor, real deploy condition): V_OC = 2.7V, I_SC = 50µA.**
      (Corrected 2026-08-21 — originally logged as 50mA, a units misread; it's 50µA.)
      V_OC clears the 600mV cold-start threshold by a wide margin (4.5x) even at this light level —
      cold start is not looking marginal.
      **Superseded note (later 2026-08-21):** the "I_SC < sleep current, solar can't outpace it"
      conclusion below was written against the *old* ~65-70µA sleep-current bug. That bug is fixed
      (see the sleep current fix in Test 2 above) — sleep is now 10-15µA, so this is no longer a
      blocking conclusion, just stale math. Re-evaluate once a real loaded-current measurement
      exists (see below).
- [x] Repeat at a window and under a bright lamp for comparison — **done 2026-08-22, real direct
      sun.** Bare panel: **V_OC = 6V, I_SC = 30mA** — dramatically higher than both the dim-indoor
      baseline (2.7V/50µA) and the nighttime LED proxy (4.62V raw, current never measured). This is
      the real-world number; the LED test's V_OC alone was misleadingly close without the current to
      back it up.
- [x] Confirm V_OC clears the cold-start threshold below — yes, both at dim-indoor (2.7V) and under
      the bright LED (4.62V raw, well above 600mV).
- [x] Attach the panel with no USB and watch V_STOR rise — **done, real cold start confirmed on
      hardware.** Starting from a fully dead/unpowered board (no battery, no USB, panel freshly
      wired to SP+/GND), VSTOR climbed from 0V to a **2.53-2.67V plateau** under a single bright LED
      held above the panel (higher when moved closer), then decayed slowly back to 0V when the light
      was removed (expected — no battery to hold charge, only the small VSTOR decoupling caps).
      **The plateau is below the ~3.04V VBAT_UV threshold** (see the newly-derived real threshold
      table below) — so under this specific light source, VSTOR never reached the point where it
      would actually start charging an attached battery. Loaded SP+ (VIN_DC) measured only 0.74V
      under the same LED, vs. 4.62V true open-circuit — consistent with a point-source LED
      delivering very little photocurrent despite a deceptively high V_OC; a boost converter can
      raise voltage but not manufacture power that isn't available at the input. Not evidence of a
      circuit fault — VSTOR responded correctly and proportionally to light intensity throughout.
- [x] Compare harvested power against the Test 2 average — that answers "does indoor solar keep up?"
      **Answered for direct sun, 2026-08-22 — yes, by ~3 orders of magnitude.** True in-series
      current at SP+/VIN_DC is still not measured (still needs the soldering-iron trace break), but
      a no-solder proxy closed this well enough: see "Implied-current method" below. Real direct sun
      gives **≥10mA** into the battery, vs. the ~10-15µA Test 2 sleep floor. Not measured yet: real
      net current under the *actual* sleep-cycle duty cycle (board asleep, not sitting awake in the
      bench-sketch command loop) — see the overcast note below, that's the next real open item.

**2026-08-22 daytime session — full results:**

- **Cold start under real sun, panel wired to board, no battery attached:** SP+ (loaded VIN_DC) =
  5.5V, VSTOR = 4.93V, 3V3 = 3.3V. Confirms cold start and boost regulation hold up under real sun,
  not just the LED proxy. **VSTOR (4.93V) exceeded the ≈4.26V VBAT_OV ceiling** — the first time
  it's been driven that high. Plausible/expected (nothing to sink current with no battery attached,
  boost should throttle at OV), but **not yet cross-checked against C10/C11's voltage rating or the
  mux/LDO input rating** — worth a datasheet check before repeating this exact no-battery/full-sun
  condition for long periods.
- **VSTOR→VBAT latch recurrence, confirmed a second time, independently.** Battery (3.58V, drained
  overnight per the plan above) was reattached while VSTOR sat at 2.36V — below the ~3.04V VBAT_UV
  threshold, so the internal PFET was open. Result: **3V3 collapsed to 0V** (a more complete
  collapse than 8/21's undefined ~1.6V — explained by even less VSTOR headroom this time for the
  3V3 LDO, which is fed only from VSTOR/VBUS, never VBAT directly). **Fix confirmed again:**
  re-exposing the panel to direct sun with the battery attached pushed VSTOR back above 3.04V, the
  switch re-latched, and 3V3 snapped back to a clean 3.3V. This is now an empirically repeated
  (not just predicted) field behavior: **every battery disconnect/reconnect needs one direct-light
  exposure to re-prime the board** before it'll run on battery alone.
- **Implied-current method (no solder required) — new, derived this session.** While actively
  charging in direct sun, VSTOR sits measurably above VBAT (real ohmic drop across the closed
  internal PFET, since real current is flowing through its finite on-resistance). The datasheet
  gives `R_BAT(on)`, "Resistance of switch between VBAT and VSTOR when turned on": **MAX = 2Ω**
  (VBAT = 4.2V, VSTOR load = 50mA test condition; no min/typ published) — BQ25504 datasheet
  SLUSAH0G, page 7, Electrical Characteristics, verified against the rendered table image (not just
  the extracted text). A measured 20mV delta (VSTOR = 3.62V, VBAT = 3.60V) gives
  **I ≥ ΔV / R_max = 20mV / 2Ω = 10mA** — a lower bound, since 2Ω is a worst-case max, not typ, so
  real current is likely higher. Consistent order-of-magnitude with the 30mA bare-panel I_SC above.
  Caveat: the datasheet spec was characterized at VBAT = 4.2V, not this reading's 3.6V — treat as a
  solid ballpark, not a lab-grade number; the real in-series measurement remains the authoritative
  source once there's a soldering iron on hand.
- **Overcast/active-load confound, noted not yet resolved.** Under overcast light (SP+ ≈ 3.3V) with
  the board sitting in the interactive bring-up-sketch command loop (not `sleepbench` — Serial/I2C
  active, plausibly several mA), VBAT showed no visible charging. Not a harvester fault — an active
  bench session draws far more than the ~10-15µA real sleep floor, and even decent overcast light is
  unlikely to outpace mA-class load on this panel size. **Real open item:** repeat under overcast
  with the board in `sleepbench <s>` (USB unplugged) to test the actual deployed duty cycle against
  real (non-full-sun) light, instead of full sun or an artificially-awake board.

**Real programmed thresholds (derived 2026-08-21 from the actual populated R9-R15 and the
datasheet's own formulas, verified against TI's own worked design examples — not estimated):**

| BQ25504 threshold | Formula (VBIAS = 1.25V typ) | This board's real resistors | Computed value |
|---|---|---|---|
| VBAT_OV (charge ceiling, boost disables above this) | 1.5·VBIAS·(1+R11/R14) | R11=5.62MΩ (VRDIV-side), R14=4.42MΩ (GND-side) | **≈4.26V** |
| VBAT_UV (PFET VSTOR→VBAT gate threshold) | VBIAS·(1+R12/R15) | R12=5.90MΩ (VRDIV-side), R15=4.12MΩ (GND-side) | **≈3.04V** |
| MPP / VOC sample ratio | (R10+R13)/(R9+R10+R13) | R9=4.42MΩ (top), R10=10MΩ+R13=5.6MΩ (bottom) | **≈78%** (typical solar range is 70-80%) |
| VBAT_OK | not populated (pin 11 floating, OK_HYST/OK_PROG tied GND) | — | feature disabled on this board |

VBAT_UV ≈3.04V is a real, useful number for interpreting future tests: a battery attached under
weak light will show STAT/PG activity but **won't actually receive charge current until VSTOR
clears ~3.04V**, independent of whatever the BQ25170 (USB path) is doing.

| BQ25504 threshold | Value |
|---|---|
| Cold start — needed to begin from flat | V_IN ≥ 600 mV |
| Continuous harvest — once running | V_IN ≥ 130 mV |
| Own quiescent draw | < 330 nA |

**Set expectations honestly:** indoor light is typically **100–1000× dimmer** than direct sun, and
small panels are usually characterised in full sun. Cold start is the harder threshold — a panel
that sustains harvesting may still never start from flat indoors. If cold start is marginal,
that's a design finding, not a test failure.

**BQ25170 (USB charger) ISET/VSET — open item since 2026-08-01, closed 2026-08-21.** Pulled the
real resistor values from the netlist and the datasheet's actual lookup table/formula: R8=27kΩ on
VSET → **4.20V** (Table 7-1's exact "1-cell LiIon: 4.20V" entry, the standard safe LiPo target).
R7=10kΩ on ISET → ICHG = KISET/RISET = 300Ω·A/10kΩ ≈ **30mA** (KISET=300 typ, confirmed from the
Electrical Characteristics table). Both values are sane and safely in-range (RVSET valid range is
18-100kΩ, RISET valid range is 0.375-30kΩ) — no bug, this can be considered verified.

---

## Test 4 — Tank ringdown and probe capacitance

> **Independent of the others. Scope required.**

Worth more than just "what's the capacitance." The FDC2214's internal oscillator is only specified
to **35 / 43.4 / 55 MHz** — a ±23% part-to-part spread — and every capacitance derived from a
`DATA_CH0` reading inherits that error. This test calibrates it for this specific chip.

| Known / predicted | Value |
|---|---|
| L2 — Coilcraft LPS4018-682MRC | 6.8 µH |
| C17 — fixed tank cap, across TANK_HOT/TANK_REF | 270 pF |
| Ideal bare tank, L2 + C17 only | 3.71 MHz |
| Implied f_SENSOR today, assuming f_INT = 43.4 MHz | 3.29 MHz |
| Implied total tank C at that frequency | ≈ 345 pF |
| **Implied stray + electrode C beyond C17** | **≈ 75 pF** |

- [ ] Run `fdcinit` so the tank is actively driven, then scope TANK_HOT with a **10× probe** and read the frequency
- [ ] **At that same moment**, run `fdcread` and record `DATA_CH0` — see the warning below, this is what makes the result valid
- [ ] Compute the real internal oscillator frequency for this chip
- [ ] Back out total tank capacitance, then subtract C17 for the stray + electrode figure
- [ ] Repeat with electrodes in wet soil — the two numbers bracket the real sensing range
- [ ] *Optional true ringdown:* `sleep` to shut the FDC down, inject a small step across the tank, measure the decay envelope for unloaded f₀ and Q

```
f_INT   = f_scope ÷ (DATA_CH0 / 2^28)
C_total = 1 / ((2π · f_sensor)² · L)
```

> **The probe changes what you're measuring.** A 10× probe adds roughly 10–15 pF. Against a ~345 pF
> tank that shifts the frequency by about 2%. That's why `DATA_CH0` must be read *while the probe is
> still attached* — both measurements then see the same loaded tank and the probe capacitance
> cancels out of the ratio. Comparing a probed scope reading against an unprobed FDC reading
> silently bakes the probe into your answer.

**What this unlocks:** once f_INT is known for this chip, every future `DATA_CH0` converts to an
absolute capacitance instead of a relative number — which is what makes cross-probe comparison and
a physically-grounded calibration curve possible.

---

## Test 5 — EC-check electrode bridge

> **30 seconds. Open since 8/20.**

The EC-check path has only ever been observed at rest, reading LOW. It's never been shown to
actually respond to a conductive bridge.

- [ ] `mux 2`, then `divnode` — expect LOW with electrodes dry
- [ ] Bridge both electrode pads (wet finger, or dip in water), then `divnode` again
- [ ] `mux 0` to park back off the EC-check path when done

**Pass:** LOW when dry, HIGH when bridged. If it never goes HIGH, check R17 and the mux S3/S4
throws before suspecting the electrodes.

**2026-08-22 — ROOT DESIGN ISSUE FOUND: this circuit can never do what it was built for.**
`DIV_NODE` (`VDIV`) is wired to the E73 module's pin 11 / nRF52840 `P0.00` (`XL1`) — confirmed via
the real nRF52840 datasheet (rendered table images, not the extracted text) that this pin has **zero
analog capability**: SAADC, COMP, and LPCOMP are all hard-restricted to `AIN0`–`AIN7`
(`P0.02/03/04/05/28/29/30/31`) and none of them can reach `P0.00`. Its "Analog input" label in the
pin table is only its XL1 crystal-oscillator function, unrelated to general sensing. So this net can
only ever be `digitalRead` — a binary wet/dry threshold — never a graded value. **The actual design
intent was to measure real soil salt content (EC) on the same sensor as moisture**, which requires
an analog reading; a binary threshold cannot do that regardless of firmware. Not caught in the 8/1
pre-order layout reviews — those were scoped to the mux part (RON/bandwidth) and trace/ground-plane
routing, never to which MCU pin `DIV_NODE` would land on.

**Fix, confirmed against the real netlist — two AIN-capable pins are unused on this board:**
`P0.03`/`AIN1` (module pin 3) and `P0.04`/`AIN2` (module pin 18). **Module pin 18 is physically
unreachable** (routed underneath the SMD module, no test access). **Module pin 3 is a castellated
edge pad — reachable, and is the real fix.** Plan, for the next lab session (needs a soldering
iron, not doable in the field):
1. Bodge a jumper wire from the `VDIV` net (tap at R17 pin 2, or the `MUX-7`/`MUX-11` legs — not yet
   decided which is more physically accessible) to module pin 3 (`P0.03`/`AIN1`). Leaving the
   existing trace to module pin 11 (`P0.00`) in place is fine electrically (a GPIO input in parallel
   doesn't meaningfully load the divider) — firmware just needs to stop reading it.
2. Firmware: replace `pinMode`/`digitalRead` on `PIN_DIV_NODE` with a real SAADC `analogRead()` on
   the new pin — reuse the already-correct 12-bit/`AR_INTERNAL_3_0` pattern from `readVbatMv()`
   (fixed 2026-08-21), don't re-derive ADC config from scratch.
3. Once real analog EC data exists, **re-derive R17** — with a graded reading, the right value isn't
   about crossing one digital threshold anymore, it's whichever value centers the voltage swing
   across the real dry/wet-salty soil resistance range for this electrode geometry (roughly the
   geometric mean of that range) — unknown until real soil data exists at that point.
4. **Next PCB revision:** route `DIV_NODE` directly to `P0.03` (or another convenient AIN pin) from
   the schematic stage, so no bodge wire is needed on future boards.

Not done yet — deferred to the next lab session with a soldering iron. Today's live bridge test
below still has diagnostic value as a stopgap (confirms the binary mechanism at least works and
gives a first real R_bridge data point), but is not a substitute for real analog EC data.

**2026-08-22: `sensesweep <n>` added**, closing a related gap found the same night — the
electrode-swap mux codes (0/1) had also never actually been *alternated* cycle-to-cycle
(only ever read individually via `mux`/`muxsweep`), which matters because alternation is what
cancels DC-bias-driven drift between the two physical electrodes, same reasoning as MSV2's
`driveIsPlateA` swap. `sensesweep` exercises both gaps together: each cycle alternates the
electrode-swap phase for an LC-tank reading, then the paired EC-check phase for a `DIV_NODE`
reading, using the mux's own address-bit structure to keep both alternations in lockstep. Bridging
the electrodes with a real conductive bridge (this test's own remaining checklist above) is still
the way to actually pass Test 5 — `sensesweep` only proves the *mechanism* cycles correctly, it
doesn't substitute for the real bridge test. See `docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md`.

**2026-08-22, same day — `sensesweep 10` actually run on real hardware for the first time**
(reflashed the bring-up sketch first; it hadn't been re-flashed since before this session's
`sensesweep` addition). After `fdcinit`, all 10 cycles came back clean:

- Tank readings (codes 0/1) clustered tightly: 20,228,634–20,231,273 (~0.013% spread), matching the
  8/20 baseline's tightness. **Phases 0 and 1 read statistically the same value** — exactly what a
  working DC-bias-cancelling swap should do (same physical tank, alternating drive polarity, same
  magnitude both ways). `STATUS=0x8` (`CH0_UNREAD` only) on every cycle, zero `ERR_WD`/`ERR_AHW`/
  `ERR_ALW`.
- EC-check readings (codes 2/3): `DIV_NODE=LOW` on all 10 cycles, correctly in lockstep with the
  tank phase — matches the expected dry/open resting state.
- **First real hardware confirmation the alternation mechanism itself works correctly** — was
  previously only mechanism-untested. Real conductive-bridge validation (this test's own checklist
  above) is still the only remaining open item in this test.

---

## Test 6 — Re-check wake settle in wet soil

> **Closes the one caveat from 8/21.**

The 8/21 settle measurement found no transient at all — but it was taken with dry electrodes. Wet
soil raises sensor capacitance and changes tank Q, which is exactly the condition that could
introduce one.

- [ ] Put the electrodes in wet soil, then run `wakeprofile 12`
- [ ] Check whether sample `[0]` sits inside the spread of samples `[6]`–`[11]`
- [ ] Confirm no `ERR_AHW` / `ERR_ALW` / `ERR_WD` flags on the first conversions

**Pass:** same as dry — sample 0 already inside the steady-state band, no error flags. If a
transient *does* appear, raise `BLE_WAKE_DISCARD_CONVERSIONS` past the point where it flattens;
that constant exists for exactly this case.

**Also watch IDRIVE.** Wet soil loads the tank harder. If `ERR_ALW` (amplitude too low) shows up in
soil but not in air, the `0x8000` IDRIVE setting needs bench tuning — it was a first guess that
happened to land well in air and has never been stressed.

---

## Command reference

Serial console at 115200 baud on `COM3`.

| Command | Does |
|---|---|
| `vbat` | Battery divider read, corrected scaling |
| `chgstatus` | BQ25170 STAT / PG, with pull-ups applied for the read |
| `fdcinit` | Configure FDC2214, start free-running conversion |
| `fdcread` | One `DATA_CH0` + `STATUS` |
| `fdcstream <n>` | Stream n readings, ~150 ms apart |
| `muxsweep` | All 4 mux codes with labels |
| `mux <0-3>` | 0/1 electrode-swap · 2/3 EC-check |
| `divnode` | EC-check midpoint digital read |
| `sleep` / `wake` | All ICs down / full wake with settle |
| `sleepbench <s>` | Silent sleep so USB can be unplugged and current measured |
| `wakeprofile <n>` | Cold wake, n undiscarded conversions |
| `blecycle <n>` | n full wake/measure/advertise/sleep cycles |
| `blestart` / `blestop` | Continuous advertising on/off |
| `wdtstatus` | Reset reason, LFCLK source, watchdog state |

**Reflash:**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action upload -Port COM3
```

No SWD leads, no UF2 drag. The wrapper supplies `-DCONFIG_NFCT_PINS_AS_GPIOS` in **both**
`compiler.c.extra_flags` and `compiler.cpp.extra_flags`, enforces LFRC, and cleans stale objects.

---

## Bring back

The multimeter-vs-`vbat` pair, sleep current with BLE on and off, panel V_OC/I_SC in real indoor
light, the scope frequency plus simultaneous `DATA_CH0`, and the wet-soil `wakeprofile` output.
Those five close every open item on this board.
