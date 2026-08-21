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

The open question is whether indoor light actually outpaces the sleep budget. That's an empirical
question about your panel in your light — measure the panel first, then the harvester.

- [x] Measure panel V_OC and I_SC **under the actual indoor light you plan to deploy in** — not at a window, not under a lamp, unless that's the real condition
      **Measured (super-low-light indoor, real deploy condition): V_OC = 2.7V, I_SC = 50mA.**
      V_OC clears the 600mV cold-start threshold by a wide margin (4.5x) even at this light level —
      good sign, cold start is not looking marginal here.
- [ ] Repeat at a window and under a bright lamp for comparison
- [ ] Confirm V_OC clears the cold-start threshold below
- [ ] Attach the panel with no USB and watch V_STOR rise
- [ ] Compare harvested power against the Test 2 average — that answers "does indoor solar keep up?"

| BQ25504 threshold | Value |
|---|---|
| Cold start — needed to begin from flat | V_IN ≥ 600 mV |
| Continuous harvest — once running | V_IN ≥ 130 mV |
| Own quiescent draw | < 330 nA |

**Set expectations honestly:** indoor light is typically **100–1000× dimmer** than direct sun, and
small panels are usually characterised in full sun. Cold start is the harder threshold — a panel
that sustains harvesting may still never start from flat indoors. If cold start is marginal,
that's a design finding, not a test failure.

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
