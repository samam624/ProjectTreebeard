# MSV4 production firmware, first image — 2026-10-06

Dated record. Which image a board is running now is in `MSV3/fleet.json` (`msv4_boards`).

Until this date the only MSV4 image was the bring-up console, which does nothing unless a BLE
client is connected. `MSV4/arduino/MSV4_Moisture_BLE_Sleep` is the first image that runs
unattended: it wakes once a minute, measures, advertises to the DisplayCenter receiver and sleeps.

| | |
|---|---|
| Sketch | `MSV4/arduino/MSV4_Moisture_BLE_Sleep/MSV4_Moisture_BLE_Sleep.ino` |
| First build | `MSV4-BUILD:prod-2026-10-06`, application version 262148, sensor ID 10 (23-byte packet) |
| Second build, same day | `MSV4-BUILD:prod-2026-10-06b`, application version 262150: adds die temperature (26-byte packet, layout A) |
| Third build, same day | `MSV4-BUILD:prod-2026-10-06c`, application version 262152: layout B, which adds `Cd` and moves `Cp` out of the packet. On board 1 21:55–22:16 PDT |
| Fourth build, same day | `MSV4-BUILD:prod-2026-10-06d`, application version 262153: ADC offset recalibrated at every wake. On board 1 22:16–22:55 PDT |
| Fifth build, same day | `MSV4-BUILD:prod-2026-10-06e`, application version 262154: lighter advertising, 1 ms sweep settle, cheaper fit arithmetic. On board 1 2026-10-06 22:55 – 2026-10-07 11:55 PDT |
| Sixth build, 2026-10-07 | `MSV4-BUILD:prod-2026-10-07`, application version 262155: the nRF52840's DC/DC regulator is switched on after the first wake of each boot, and every packet reports it (flags 2 bit 3). On board 1 2026-10-07 11:56–13:06 PDT |
| Seventh build, same day | `MSV4-BUILD:prod-2026-10-07b`, application version 262157: warm-start fit (one fit from the last full fit's answer, four cold starts as the fallback), reported in flags 2 bit 4. On board 1 2026-10-07 13:07–23:17 PDT. (262156 is an uninstalled fallback package, see the DC/DC result) |
| Burn test build, 2026-10-07 night | `MSV4-BUILD:burn-2026-10-07`, application version 262158: the seventh build with `-BurnFloorMv 3700`. On board 1 2026-10-07 23:18 – 2026-10-08 13:48 PDT. See "Discharge burn test build" |
| Eighth build | `MSV4-BUILD:prod-2026-10-07c`, application version 262159: the seventh build under a new label (two bytes differ). On board 1 2026-10-08 13:49–18:59 PDT |
| Ninth build | `MSV4-BUILD:prod-2026-10-08`, application version 262160: `Cp` is calibrated only after a sane moisture reading in air, never at boot; flags 2 bit 6 says when it has not been. On board 1 2026-10-08 18:59–22:07 PDT. See "`Cp` is calibrated only after an open reading" |
| One-minute TEST build | `MSV4-BUILD:fast1min-2026-10-08`, application version 262161: the ninth build with `-MoistureEveryNWakes 1` (moisture measured on every wake, every wake connectable) and `-CpCentiPf 869` (the `Cp` board 1 was running on when it was updated in soil). See "One-minute test build" |
| Advertised name | `MSV4-MOISTURE-010` |
| Build | `MSV4\scripts\arduino_msv4_probe.ps1 -Sketch arduino/MSV4_Moisture_BLE_Sleep -SensorId 10 -BuildTag <tag> -ApplicationVersion <n> -Package -AcceptSharedMsv3DeviceType` |
| Install | `MSV4\scripts\msv4_ble_ota.py <zip> --board 1` (run `--preflight-only` first) |
| Verify | `MSV4\scripts\msv4_adv_scan.py --sensor 10 --require-progress`, then `--read-build <address>` |

## Installed on board 1, 2026-10-06 15:32 PDT

All measured, from the maintainer's computer over BLE at about −65 to −78 dBm:

| Check | Result |
|---|---|
| DIS firmware revision / software revision | `MSV4-BUILD:prod-2026-10-06` / `262148` |
| Advertised name and address | `MSV4-MOISTURE-010` at `F6:26:7D:76:AD:95` |
| Advertisement counter | 2, 3, 4, then 7, 8, exactly 60 s apart (15:33:11 … 15:39:11) |
| After a client connected and read DIS at counter 6 | Counter carried on at 7 and 8: no reset, cadence unchanged |
| Boot `Cp` calibration | 8.8 pF, the same value the console's `spectrum open` gave on 2026-10-01 |
| Battery | 3823–3832 mV across six packets |
| Tip temperature | 26.50–26.75 °C |
| Moisture fit, blade as the maintainer left it for the solar test | `Rsoil` saturated (≥ 400 kΩ), `Cm` < 0.05 pF, RMS residual 0.0230 of drive |

The moisture row is what "nothing conducting" looks like, as in air. The 2.3 % residual has no
console figure to compare against, so whether that is normal for this board in air is not known.

### The transfer

- `msv4_ble_ota.py` jumped the bring-up console into the bootloader through the buttonless DFU
  service (write acknowledged in 0.03 s); the bootloader came up on the application's address.
- **All three attempts through `scripts/ble_dfu_recover.py` failed.** The first streamed to 100 %
  with about 64 "PRN Timeout, continuing anyway" messages and was then refused with Receive
  Firmware Image status 6. The second was refused at Start DFU (status 2, stale session). The
  third ended in status 6 again. That client's 0.8 s receipt deadline lets it keep streaming
  after a late receipt (inferred cause; the timeouts and the status codes are measured).
- **This bootloader's application DFU is single-bank.** After the first failure the bootloader was
  advertising as `AdaDFU` on the recovery address `…AD:96` with no application to return to. It
  stays there and accepts a new transfer; nothing else was needed to recover.
- The transfer was moved onto the hardened client settings `scripts/ble_ota_update.py` uses (6 s
  receipt deadline, receipt guard, negotiated MTU, PRN 16). It then completed in **57 s** on the
  first session, after one stale-session reset.

## Power: what the cycle costs and what 262154 changed

**Time, measured** (cloud timestamps and a frame listener, 2026-10-06):

| | Before (262148–262153) | 262154 |
|---|---|---|
| Wake spacing | 60.00 s median, 101 intervals | unchanged |
| Ordinary wake | 5 s connectable window; frames heard spanning up to 4.98 s | 4 s non-connectable burst |
| Measurement wake, sweep plus fit, blade in air | 3.8 s median, 12 wakes (range 3.2–6.0) | 2.7 s (one clean sample, and a second consistent with it) |
| `Cd` / fit residual, blade in air | 12.96–13.04 pF / 0.0229–0.0231 | 12.99–13.01 pF / 0.0229 |
| Receiver packets missed | 2 of 149 (1.3 %) | 0 of 11, of which 7 were short bursts |

**Energy, modelled only.** No current has been measured on this board. Using the nRF52840
product specification's typical figures in LDO mode (the Arduino core never enables DC/DC):
TX at +8 dBm 32.7 mA, RX 9.9 mA, CPU from flash 6.3 mA, TIMER at 16 MHz with HFXO 0.82 mA,
SAADC 1.24 mA, System ON with RTC 3.2 µA.

| Per 10 minutes | Before | 262154 |
|---|---:|---:|
| Advertising | about 32 mC (56 events × 10 windows) | about 10 mC (9 bursts of 16 events, one window of 56) |
| Sweep | about 4.7 mC | about 4.2 mC |
| Fit arithmetic, blade in air | about 13.8 mC | about 11 mC |
| Sleep, if MSV4 matches MSV3's measured 10–15 µA | 5–8 mC | unchanged |
| **Total** | **about 55–60 mC, 2.1–2.4 mAh/day** | **about 31–34 mC, 1.2–1.3 mAh/day** |

What changed in 262154:

- **Advertising.** The full 5 s connectable window is opened only on measurement wakes (every
  tenth) and on the first three wakes after a boot. Every other wake sends the same packet as a
  4 s non-connectable, non-scannable burst at 250 ms: about 16 events instead of about 56 and no
  receive slot after each. It is spread over 4 s deliberately: the receiver listens 25 ms in every
  75 and stops scanning for 2–3 s around each cloud upload, so a dense burst would be lost whole
  whenever it met an upload. Flags 2 bit 2 marks a short burst.
  **Consequence:** an updater or a DIS read can wait up to ten minutes for a connectable window.
  `msv4_ble_ota.py` and `msv4_adv_scan.py --read-build` wait for one themselves; this was
  exercised at 23:10–23:15, when the DIS was read through the window at counter 21.
- **Sweep settle** 5 ms → about 1 ms (the maintainer's call). `delay(2)` is used because the
  tick-based delay ends on a tick boundary: `delay(1)` can be anything from 0 to 0.98 ms.
- **Fit arithmetic.** The divider model is evaluated in real arithmetic instead of through the
  general complex type. Same result (3e-16 over 200,000 random cases), fewer operations.

The fit was expected to speed up by 3× or more and did not: the wake shortened by 1.1 s, about
0.6 s of it the sweep. **Inferred:** with the blade in air the fit is degenerate and runs to its
iteration limit on all four starting points, and its time is dominated by divisions, whose count
did not change. In soil it should converge in far fewer iterations; that has not been measured.
(2026-10-07: a statement-for-statement replica of the solver supports neither half. Each start
takes about 20 iterations, not the limit of 60, and four cold starts cost as much in modelled
soil as in air. See "Warm-start fit".)

Left alone, with the reason:

- **TX power** stays at +8 dBm. After the advertising change it is worth about 0.09 mAh/day at
  +4 dBm, and it would have been a second radio change on top of the first.
- **Measurement interval** stays at 10 minutes.
- **DC/DC regulator** is not enabled. See below. (Enabled in 262155 on 2026-10-07; the result is
  at the end of the next section.)

### The chip's DC/DC regulator

The nRF52840's core runs at about 1.3 V. On its linear regulator the supply current equals the
core current and the rest of the voltage is burned as heat; the DC/DC converter trades voltage
for current instead, which is why the specification's 32.7 mA at +8 dBm becomes 14.8 mA and
6.3 mA of CPU becomes 3.3 mA. It needs an inductor between the chip's DCC and DEC4 pins. In
sleep it makes no difference.

**The E73-2G4M08S1C module almost certainly has that inductor inside it. This is inferred from
four independent sources; nothing has been measured on a Treebeard board.** (Researched later
on 2026-10-06; the first version of this section said "not established". Tested on board 1 on
2026-10-07: see "Result on board 1" at the end of this section.) The module does *not*
have the separate inductor for the high-voltage stage (REG0, pin DCCH), which does not matter
here: MSV4 ties VDD and VDDH to the same 3.3 V rail, so that stage is bypassed.

1. **Ebyte's current table can only have been taken in DC/DC mode.** Module figures from the
   manual against the nRF52840 specification, mA at 3 V:

   | | Ebyte module | Nordic, radio only, DC/DC | Nordic, CPU + radio, DC/DC | Nordic, radio only, linear |
   |---|---:|---:|---:|---:|
   | TX +8 dBm | 17.05 | 14.8 | – | 32.7 |
   | TX +4 dBm | 12.68 | 9.6 | – | 21.4 |
   | TX 0 dBm | 7.63 | 4.8 | 8.1 | 10.6 |
   | RX 1 Mbps | 7.71 | 4.6 | 8.6 | 9.9 |
   | RX 2 Mbps | 8.27 | 5.2 | – | 11.1 |

   Every Ebyte figure is below the linear-regulator figure for the radio alone, and they are not
   copies of Nordic's numbers. Ebyte's 5 V figure at 0 dBm is 7.25 mA, almost the same as at 3 V,
   which is what a working main-stage converter behind a *linear* high-voltage stage gives
   (Nordic's figure with both stages converting is 3.0 mA). That matches item 3.
2. **The inductor has nowhere else to be.** The module's 43 pads include VDD, VDDH and DCCH but
   neither DCC nor DEC4, so a main-stage inductor cannot be added from outside.
3. **The designer of the nRFMicro**, a board built on this module, writes in its wiki that the
   E73 uses Nordic's reference configuration with DC/DC on the main stage, and that the 10 µH on
   DCCH for the high-voltage stage has to be added on the carrier board.
4. **ZMK turns the main-stage converter on by default on two boards built on this module**
   (`nrfmicro_nrf52840.dts`: `&reg1 { regulator-initial-mode = <NRF5X_REG_MODE_DCDC>; }`;
   `bluemicro840/Kconfig`: `BOARD_ENABLE_DCDC ... default y`). The nRF52840 specification says
   enabling it with no LC filter "will inhibit device operation, including debug access", so
   those keyboards would not run if the inductor were missing.

Two reports that look like the opposite and are not:

- A MySensors forum thread (September 2017) measured that Ebyte's module has no DC/DC inductors:
  with `DCDCEN` set it sat in a boot loop until two inductors were soldered between DCC and
  DEC4. That was Ebyte's **nRF52832** module, a different part. It is the origin of most "the
  Ebyte module has no inductor" statements, and it is a measured example of what a missing
  inductor looks like.
- A Nordic DevZone report of the E73-2G4M08S1C drawing 30 mA after
  `sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE)`. Never resolved; the poster suspected their
  measurement setup. The chip was running, which is not what a missing inductor does.

No photograph or X-ray of the module with its shield off was found, and Ebyte publishes no
schematic of it, so the last step is still a test on our own hardware. Nothing in this project
has ever exercised the converter: the Arduino core does not set `DCDCEN` (its only power call
passes a zeroed configuration) and the MSV3 bootloader leaves `ENABLE_DCDC_1` at 0, so the MSV3
fleet running on the same module proves nothing either way.

It was not tried on board 1. If the inductor were missing after all, the chip would stop or
loop through resets (the 2017 nRF52832 measurement was a boot loop), and the application would
switch the converter on again after every reset, so the board would stay silent until it was
reflashed over SWD. Modelled saving once enabled: about 0.4 mAh/day on top of 262154.

### Result on board 1, 2026-10-07: it runs with the converter on

Application 262155 (`MSV4-BUILD:prod-2026-10-07`) went on over BLE at 11:55:56–11:56:32 PDT
(36 s of transfer, first session).

How 262155 does it:

- **The converter is requested at the end of the first wake after each boot,** not in
  `setup()`. The first wake, with its full connectable window, runs on the linear regulator. A
  board with no inductor would therefore reset into one connectable window after another
  instead of going silent, and could be recovered over BLE without SWD. That path was not
  needed and has not been exercised.
- **Every packet reports `DCDCEN` as read back from the register** when the packet is built
  (flags 2 bit 3). The request is `sd_power_dcdc_mode_set()`; the bit is a direct read of
  `0x40000578`. Checked in the built image by disassembly: `loop()` calls the SVC with 1 and
  ANDs bit 0 of that register into byte 25. The core does clear the register on the way into
  `setup()` (TinyUSB calls `nrfx_power_init()` with a zeroed configuration), which is why the
  request cannot be made by the bootloader and why a readback is carried.
- The high-voltage stage (`DCDCEN0`) is left off.

Measured, from the maintainer's computer at −63 to −81 dBm:

| Check | Result |
|---|---|
| DIS firmware / software / hardware revision, read at 11:58 | `MSV4-BUILD:prod-2026-10-07` / `262155` / `Cp 8.83 pF` |
| Advertisement counter | 2 to 11, none missing, 60 s apart (11:57:33 … 12:06:36); it carried on through the DIS connection |
| `DCDCEN` readback (flags 2 bit 3) | set in all ten packets |
| Battery, counters 2–11 | 3827–3832 mV, mean 3829. Under 262154 at 11:43–11:44: 3825, 3829 mV |
| Sweep at counter 11, converter on | `Cd` 12.98 pF, residual 0.0229, open |
| Sweep at counter 1, same boot, taken before the converter is requested | `Cd` 12.95 pF, residual 0.0228, open |
| Same quantities under 262154 | `Cd` 12.96–13.01 pF, residual 0.0229 |
| Tip / die temperature | 24.0–24.25 °C / 24.75–25.0 °C |

What that does and does not establish:

- **The chip sleeps, wakes, sweeps, accepts a connection and transmits at +8 dBm with `DCDCEN`
  set** (measured). The specification says a chip with no LC filter would not, and the one
  measured example of a missing inductor boot-looped, so the E73-2G4M08S1C's main-stage
  inductor is taken as present on this module. That last step is still an inference from the
  specification; nobody has seen the part.
- **The one sweep taken with the converter on is inside the scatter of the sweeps taken with it
  off** (`Cd` 12.95–13.04 pF and residual 0.0228–0.0231 across every earlier reading in air).
  One sample; the hourly record will show whether that holds.
- **No supply current has been measured,** so the saving is still the modelled 0.4 mAh/day. The
  battery readings are taken before each wake's load and cannot show it.
- **Counter 1 was not heard** (the listener started as its window closed), so "bit 3 is clear on
  the first wake" is from the source and the disassembly, not from a packet.

A fallback package was built before the install and not used: the 262154 source (commit
`c1f9991`) rebuilt as `MSV4-BUILD:prod-2026-10-07-ldo`, application version 262156, so that a
board looping with the converter on could be put back on the linear regulator in one transfer.

### Warm-start fit (262157, 2026-10-07)

Until 262155 every measurement ran the fit four times, from four starting conductances a decade
apart, and kept the best. From 262157 a measurement first runs **one** fit started from the last
full fit's answer (the anchor) and keeps it only if both hold:

- its residual is within 10 % (plus one count of the packet's residual field) of the anchor's;
- the answer is still near the anchor: reported `Rsoil` and `Cd` each within a factor of 1.25.

Otherwise the four cold starts run exactly as before and the best of all five is taken, so the
worst case is the old behaviour plus one short fit. Every 12th measurement (two hours) is a full
fit regardless, and so is the first after a boot and the retry after an invalid reading.
Flags 2 bit 4 says which path produced a reading.

Three details, each there for a reason found in testing:

- **The warm fit starts from the anchor, not from the previous warm answer.** `Cm` and `Gs` are
  flat directions whenever little conducts, and solver steps taken there on rounding noise would
  add up from one measurement to the next.
- **An open anchor is not used as it stands.** At 1e-16 S the solver has no slope to follow back
  when the medium starts to conduct. The start is then a cold start's `Cm` with `Gs` at the wire
  ceiling (1/400 kΩ), keeping only `Cd`. A first version that fitted `Cd` alone while open cost a
  fifth as much in air and was dropped: with a true 50–100 kΩ it went on reporting "open" until
  the next full fit, where four cold starts reported 72–250 kΩ.
- **The move limit exists because this solver can stall.** With an anchor at 2 kΩ and the medium
  now at 364 Ω, one warm fit in eight stopped at 357 Ω with a residual 25 % above the minimum;
  restarting it from its own answer finished the job. A fit that had to travel means the medium
  changed, which is when a full fit is wanted anyway.

**Synthetic test** (`MSV4/scripts/test_msv4_warm_fit.py`: the sketch's solver and this logic
replicated statement for statement, constants read from the sketch, each scenario run through
the warm logic and through four cold starts on identical sweeps). The sweeps are the model plus
a fixed distortion sized to board 1's 2.3 % residual in air; none is a board reading, and no
sweep has been recorded in soil.

| Scenario | Sweeps | Full fits | Worst `Rsoil` difference | Model evaluations, warm path vs cold |
|---|---:|---:|---:|---:|
| Air, steady (two noise seeds) | 72 + 72 | 6 + 6 | both open | 25 %, 24 % |
| Moist soil 355 Ω, steady | 36 | 3 | 0.000 % | 14 % |
| Damp towel 5.9 kΩ / hand grip, steady | 24 / 12 | 2 / 1 | 0.000 % | 23 % / 21 % |
| Soil, `Rsoil` ±20 % thermal cycle | 36 | 4 | 0.000 % | 17 % |
| Air, then into moist soil / onto a towel | 13 / 13 | 2 / 2 | 0.000 % | 22 % / 28 % |
| Soil at 2 kΩ watered to 355 Ω | 13 | 2 | 0.000 % | 22 % |
| Soil, then pulled out into air | 13 | 2 | both open | 29 % |
| Towel drying 5.9 kΩ → open | 40 | 14 | 0.000 % | 47 % |
| Soil drying 355 Ω → 100 kΩ | 40 | 25 | 0.000 % | 76 % |
| Conduction creeping in from open | 30 | 12 | 0.000 % | 60 % |

All 13 pass: the packet would carry the same `Rsoil`, open verdict, `Cd` and (when open) `Cm`
either way. Every step change was caught on the sweep it happened. The fast ramps cost more
because `Rsoil` leaves the 1.25 window every sweep or two; they change a decade in two to three
hours, which a pot does not.

What the replica says about the fit as it was (modelled, not timed on a board):

- **Four cold starts are about 400 model evaluations in air and about 450 in moist soil.** So
  the fit is no cheaper in soil. The 262154 note above expected it to converge in far fewer
  iterations there; the replica does not support that.
- **It is not the iteration limit.** Each start takes about 100 evaluations, roughly 20
  iterations, where the limit is 60. That corrects the inference in the 262154 section.
- **In every conducting case all four cold starts end on the same answer.** The four starts
  matter in air, where two families of answer fit equally well (no conduction, or a short
  circuit behind `Cm`), and the cold fit picked the collapsed one on 13 of 432 synthetic air
  sweeps (six noise seeds). The probe reports that as an invalid reading and retries a minute
  later. The warm path did it on 3 of the same 432, each one a scheduled full fit.
- A warm fit is about 69 evaluations in air (it still has to walk `Gs` down from the ceiling)
  and about 25 in soil. With one full fit in twelve that averages **24 % of the old fit time in
  air and 13 % in soil**.

**Measured on board 1, blade in air, 2026-10-07.** 262157 went on over BLE at 13:06:40–13:07:35
(52 s of transfer, first session). DIS read at 13:09: `MSV4-BUILD:prod-2026-10-07b` / `262157` /
`Cp 8.66 pF`. Counters 1 to 13 heard, none missing.

The length of a measurement is the time from the 60 s wake grid to the first advertising frame of
that wake, taken with a millisecond listener on the maintainer's computer. Windows often delivers
the first frame of a window late, so the grid is the earliest of the non-measurement wakes and
each figure is good to about 0.2 s.

| Wake | Fit path (flags 2 bit 4) | Sweep + fit | `Cd` / residual |
|---|---|---:|---|
| 262155, counter 61, 12:56:36 | four cold starts | 3.02 s | 12.94 pF / 0.0229 |
| 262157, counter 1, 13:07:37 | four cold starts (no anchor yet) | 2.95 s | 13.12 pF / 0.0232 |
| 262157, counter 11, 13:17:35 | **warm, accepted** | **1.00 s** | 13.11 pF / 0.0232 |

- **The measurement wake is 2.0 s shorter on a warm fit.** Less the 0.65 s modelled sweep that is
  a fit of about 0.35 s against about 2.35 s, which is what the replica predicted for air
  (69 against 400 evaluations).
- **The warm fit reported what the full fit had ten minutes earlier:** open, `Cd` 13.11 against
  13.12 pF, the same residual. One warm sample so far; bit 4 in every packet is the running
  record of how often it is accepted.
- Both full-fit samples read about 3.0 s. The 2.7 s in the 262154 table above was one sample
  taken another way; the budget model now uses 3.0.
- Modelled, at the specification's 3.3 mA of CPU current with the converter on: the fit slice
  falls from 7.8 to 1.7 mC per 10 minutes, about 0.24 mAh/day, and the whole budget from
  0.81–0.93 to 0.57–0.69 mAh/day (`MSV4/scripts/msv4_power_budget.py`). No current has been
  measured.

Two things this boot showed that are not about the fit:

- **`Cd` steps at a reboot because `Cp` is recalibrated at every boot; their sum does not.**
  `Cp` + `Cd` was 8.78 + 12.96–13.01 (262154), 8.83 + 12.93–12.98 (262155) and 8.66 + 13.11–13.12
  (262157): 21.74–21.81 pF across three boots, while `Cd` alone stepped 0.18 pF at the last
  reboot. Compare `Cd` within one boot, or compare the sum.
- **The first battery reading after this boot was 3850 mV; the next twelve were 3823–3829.** The
  first wake is also the only one with the converter off, but the stored history has ten more
  single readings 12–19 mV off since 23:10 the night before, under 262154 and in the dark too,
  so it is one of a kind of outlier this board already had. Cause not established.

### Discharge burn test build (prepared 2026-10-07, installed that night at 23:18)

For the window charge test the maintainer asked for an image that draws the cell down overnight,
so that a day of charging starts from a known low level. It is the production sketch built with
one extra parameter; without the parameter none of it is compiled.

| | |
|---|---|
| Build | `arduino_msv4_probe.ps1 ... -BuildTag burn-<date> -BurnFloorMv <mV>` (the script refuses the parameter without a tag starting `burn`, and that tag without the parameter) |
| What it does | Between wakes the CPU spins with the LED lit instead of sleeping, 55 s of every 60, and the DC/DC converter stays off. The wakes, the packets and the OTA window are unchanged |
| Load | Modelled about 6–7 mA: the specification's 6.3 mA of CPU on the linear regulator for 55 s in 60, plus about 1 mA of LED (470 Ω). Not measured |
| Seen from outside | LED lit; `msv4_adv_scan.py` prints `BURNING` (flags 2 bit 5) and `DC/DC off` |

It ends itself, once per boot:

- a battery reading at or below the floor pauses the burn for that minute, and a second in a row
  ends it (single readings stand 12–25 mV off about once in two hours);
- ten hours after boot it ends wherever the cell is;
- an unusable battery reading pauses it, and three in a row end it;
- it does not start after a watchdog or lockup reset.

**Why it has a floor at all, and why the sketch refuses one below 3600 mV.** Left running, about
6 mA empties this cell's usable window in a few hours. The BQ25504 then switches the 3V3 rail off
(3.37 V nominal, 3.42 V at the top of its tolerance; the ADC's own reading is good to 3 %),
nothing can reach the board, and when the panel has brought the cell back to 3.73 V the image
boots and burns again.

**What is not retained.** "Finished" is a RAM flag. After a soft reset the burn would run again
if the cell has charged above the floor in the meantime, so the production image goes back on
before a day's charge record is trusted to it.

Prepared on 2026-10-07, both passing `msv4_ble_ota.py --preflight-only`:

| Package | Application version | For |
|---|---:|---|
| `MSV4_Moisture_BLE_Sleep-v262158-burn-2026-10-07-signed-ota.zip` | 262158 | the night: floor 3700 mV, about 125 mV below the cell's level that day |
| `MSV4_Moisture_BLE_Sleep-v262159-prod-2026-10-07c-signed-ota.zip` | 262159 | the morning after |

Checked without a board: the 262159 image differs from the installed 262157 image in two bytes,
the version digit and the tag letter, so the burn is compiled out completely and the morning
image is the firmware already running. In the 262158 image the disassembly shows the stop logic
(the floor compared as 1850 mV at the divider node, the two-in-a-row counter, the ten-hour cap)
and the spin loop (LED set, watchdog fed, 55 s bound).

**First run, board 1, 2026-10-07.** The maintainer asked for it at about 23:08 ("Set up the LED
burner now"). 262158 went on over BLE at 23:17:42–23:18:28 (45 s of transfer, first session); DIS
read at 23:19: `MSV4-BUILD:burn-2026-10-07` / `262158` / `Cp 8.73 pF`.

| Check | Result |
|---|---|
| Flags 2 bit 5 (burn) and bit 3 (converter) | burn set, converter off, in all of counters 1–9 |
| Wake cadence | unchanged, 60 s (23:18:32 … 23:26:30) |
| Battery | 3825, 3829, 3830, 3823, 3825, 3821, 3823, 3821, 3818 mV; 3834 mV at 23:09 before the update |
| Fitted slope over those nine | **−1.1 mV/min**. In the dark the night before, without the burn: −0.17 mV/h |
| Die minus tip temperature | 1.0–1.25 °C, against 0.25 °C at 23:09 |
| Sweep at counter 1 | `Cd` 12.93 pF, residual 0.0226, open: the measurement is not disturbed |

So the burn runs, the wakes and packets carry on through it, and the cell falls about four
hundred times faster than at rest. At that rate the 3700 mV floor is roughly two hours away.
Not seen by anyone: the LED. How and when it stopped is recorded in the solar charge test
record once it has.

**It stopped itself at 01:47 on 2026-10-08**, 2 h 29 min after the boot, on the first two
readings in a row at or below the floor (3700 and 3698 mV); a single 3698 mV three minutes
earlier did not end it. The fall over the whole burn was −47.3 ± 0.5 mV/h. At 13:27 the scanner
showed the burn flag clear and the converter on at counter 850, so the board had not reset. The
numbers are in [the solar charge test record](MSV4_OUTDOOR_SOLAR_CHARGE_TEST_2026-10-06.md)
under 2026-10-08.

**262159 went back on at 13:48 on 2026-10-08** (43 s of transfer, first session), at the
maintainer's request. DIS read at 13:50: `MSV4-BUILD:prod-2026-10-07c` / `262159` /
`Cp 8.74 pF`; counters 3 to 8 heard by 13:56 with the converter on and the burn flag clear.

### `Cp` is calibrated only after an open reading (262160, installed 2026-10-08 18:59)

**What happened on board 1, 2026-10-08.** The maintainer handled the board to put epoxy on the
bottom thermistor and the side edge of the PCB, and afterwards saw `Cd` low and staying low.
From the stored history, and the DIS string read at 16:29:

| Time (PDT) | Event | `Rsoil` | `Cd` | `Cp` of that boot | `Cp` + `Cd` |
|---|---|---|---:|---:|---:|
| 13:52–15:29 | 262159, in the window, untouched | open | 12.82–13.00 pF | 8.74 pF | 21.56–21.74 pF |
| 15:34–15:35 | handling starts: signal strength at the receiver jumps, tip thermistor +1.5 °C for one wake | | | | |
| 15:39, 15:49 | moved | open | 13.23, 13.24 pF | 8.74 pF | 21.97–21.98 pF |
| 15:56:21 → 15:59:11 | **the board reset** (counter 128 → 1). Two wakes missing from the history in between |
| 15:59:11 | first reading of the new boot, something conducting across the blade | 13.3 kΩ, `Cm` 55.7 pF | 10.40 pF | **15.89 pF** | 26.29 pF |
| 16:09, 16:19, 16:29 | hands off | open | 6.44, 6.40, 6.45 pF | 15.89 pF | 22.29–22.34 pF |
| 16:39 | | open | 5.59 pF, residual at the field's ceiling (≥ 2.55 %) | 15.89 pF | 21.48 pF |

- **Measured:** this boot's `Cp` is 7.15 pF above the last one's, and `Cd` is low by that
  amount, less the 0.3 pF the node really gained. `Cp` + `Cd` moved 0.3 pF across the reset. The
  electrode did not change; the calibration did.
- **Why the reset happened was not established.** Nothing reports the reset reason. It fell
  between two scheduled wakes, which a firmware hang would not (the watchdog is 8 s), and the
  board came back by itself, which a battery disconnect long enough to open the BQ25504's switch
  would not (maintainer). What that leaves is the 3V3 rail's enable (`VBAT_OK`, set by megaohm
  ladders a finger can load), the bare `RESET` test point and the crystal pins.
- **This corrects a claim in this record.** "The sweep opens U3, so it does not depend on what
  the blade is in" is wrong. With U3 open the electrodes still reach the SENSE node through its
  off-capacitance, and what they are touching comes with them.

**Why it matters beyond `Cd`.** `Cp` and `Cd` enter the model only as their sum, so `Rsoil` and
`Cm` do not depend on `Cp`, as long as `Cd` can absorb it. The fit keeps `Cd` positive. A `Cp`
larger than the node capacitance the fit finds pins `Cd` at nothing and the error goes into
`Rsoil`. In the synthetic moist soil (355 Ω) `Rsoil` reads 0.9 % high with `Cp` zero, 1.2 % high
with the synthetic air calibration and 3.8 % high with a `Cp` taken with a hand on the blade.
Synthetic: no sweep has been recorded in soil, and the test's distortion is invented.

**The rule in 262160** (`calibrateCpAfterOpenReading()`):

- Nothing is calibrated at boot.
- The moisture reading comes first, and the calibration runs only on the strength of a **sane
  reading in air** (the maintainer's requirement, 2026-10-08: "it should definitely check for a
  sane moisture value before running it first"). Sane means all three:
  - the reading is valid (the fit converged to positive, finite values);
  - nothing is conducting (`Rsoil` at the 400 kΩ ceiling, moisture number 0);
  - the fit residual is below 0.0255, the ceiling of the packet's residual field. Board 1's 262
    undisturbed open readings in the stored history of 2026-10-06 to 2026-10-08 are
    0.0195–0.0234; the one taken while the board was being carried (16:39 on 2026-10-08) is at
    the ceiling.
- `Cp` is then measured straight away and kept for the rest of the boot. The reading that
  allowed it is put on the new `Cp` by arithmetic, and so is the warm fit's anchor. A reading
  that is not sane leaves everything as it was, and the next measurement tries again.
- Until then `Cp` is taken as **zero** (it was 12 pF when a calibration failed). `Cd` is then the
  whole node capacitance, about 21.7 pF on board 1 in air, and **flags 2 bit 6** is set so nobody
  reads it as a change in the electrode.
- A candidate is refused if it is outside 2–25.5 pF or would leave less than 1 pF of `Cd`.
- The DIS hardware revision string follows it: `Cp 0.00 pF default` until a calibration is
  accepted, `Cp 8.74 pF` after. It is now the sketch's own characteristic (same UUID, same
  service), because BLEDis writes its strings once.

So a board that resets with a hand or wet soil on the blade reports the node capacitance, marked,
until it next takes a sane reading in air, and a board that boots in soil and stays there never
calibrates in that boot. The residual does not depend on which `Cp` the fit was given (the
replica returns the same residual and the same `Cp` + `Cd` for 0, 5.37, 8.8 and 12 pF), so the
limit taken from board 1's calibrated readings applies to its uncalibrated ones.

**Not covered:** a medium too dry to conduct also reads open, and a calibration taken there
would be accepted. How far such a medium moves the floating sweep has not been measured.

**Synthetic test** (`MSV4/scripts/test_msv4_cp_calibration.py`: the fit replica from the
warm-start test with this rule around it, the rule's lines checked as source text; every row
against a probe booted in air and left alone, on identical sweeps):

| Scenario | Calibrates | `Cd` against the undisturbed probe | `Rsoil` error (undisturbed probe) |
|---|---|---:|---:|
| 262160, boot in air | reading 0 | 0.02 pF | open |
| 262160, boot with a hand on it, then let go | reading 2, the first open one | 0.01 pF | same as the reference |
| 262160, a hand at every reading | never | 0.01 pF, plus the `Cp` it lacks | same as the reference |
| 262160, boot in moist soil, stays there | never | (reference pinned) | 0.87 % (1.15 %) |
| 262160, soil, out into air, back into soil | reading 3 | 0.01 pF | 1.13 % (1.15 %) |
| 262160, air, then two hours in soil | reading 0 | 0.02 pF | 1.15 % (1.15 %) |
| 262160, boot while being carried (open, residual 0.029), then set down | reading 2, the first sane one | 0.01 pF | open |
| 262160, hand, let go but still carried, set down | reading 3 | 0.01 pF | same as the reference |
| **262159**, boot with a hand on it, then let go | at boot | **7.25 pF low**, as on the board | same as the reference |
| **262159**, boot in moist soil, stays there | at boot | (reference pinned) | **3.83 %** (1.15 %) |

The warm-start test still passes 13 of 13 on the edited sketch, the scanner's self-test passes
with the new bit, and `verify_msv4_packet_abi.mjs` passes with checks added for it.

**On board 1, 2026-10-08.** `Cd` stayed at 6.21–6.27 pF from 16:49 to 18:39 (advertisement
counter 166 at 18:44: no reboot since 15:59), and the maintainer asked whether the board had to
go into a pot to fix it. It did not: only a boot recalibrates. 262160 went on over BLE at the
18:59:10 measurement wake at the maintainer's word ("flash"): 38 s of transfer, first session,
−66 dBm, the board sitting in air and not being touched.

| Check | Result |
|---|---|
| DIS firmware / software / hardware revision, read at 19:00:02 | `MSV4-BUILD:prod-2026-10-08` / `262160` / **`Cp 8.93 pF`**, with no "default": the calibration ran on the first reading, and the string, now the sketch's own characteristic, reads back |
| Packets, counters 2–4 (19:00:52–19:02:53) | open, **`Cd` 13.33 pF**, residual 0.0227, bit 6 clear, converter on, 3825–3827 mV, tip 25.25 °C |
| `Cp` + `Cd` | 22.26 pF, against 22.13–22.16 pF over the two hours before the update (15.89 + 6.24–6.27) |
| `Cp` against the last undisturbed boot | 8.93 pF against 8.74 pF at 13:49, before the epoxy; earlier boots 8.66–8.83 pF |

So the first of the three paths is seen on hardware: a boot in air calibrates at its first
reading. **Not yet seen on hardware:** a boot with something on the blade (bit 6 set,
`Cp 0.00 pF default`, `Cd` near 22 pF, then the calibration at the first sane reading), and a
reading refused for its residual.

### First reading in soil (board 1, Alocasia pot, 2026-10-08 21:57)

The maintainer pushed board 1 into the Alocasia pot at about 21:47 ("moment of truth"), the
epoxy having had about six hours. Soil wetness and insertion depth were not stated; the last
watering on record for that pot is 2026-09-22.

| Time (PDT) | Event | `Rsoil` (at 25 °C) | `Cm` | `Cd` | Residual | Tip / die |
|---|---|---:|---:|---:|---:|---|
| 21:36–21:41 | in air, untouched (262160, boot of 18:59, `Cp` 8.93 pF) | open | | 13.48–13.50 pF | 0.0227 | 26.0 / 26.25 °C |
| 21:42–21:46 | picked up | | | | | tip to 29.75 °C |
| 21:46:54 → 21:47:10 | **the board reset as it was pushed in** (counter 168 → 1; the maintainer saw the start-up blink). 3820–3837 mV throughout |
| 21:47:10 | first reading of the boot, blade part-way in | open | | 14.05 pF | 0.0199 | 29.25 / 28.25 °C |
| 21:49 | DIS: `Cp 8.69 pF`. The calibration ran on that open reading and is in the air range | | | | | |
| **21:57:09** | **first reading with the blade firmly in** | **20,209.9 Ω (20,310.9 Ω)** | **15.2 pF** | **21.39 pF** | 0.0133 | 25.25 / 26.25 °C |

- **It conducts in soil, and the fit is better there than in air** (residual 0.0133 against
  0.0227). Moisture number 12,943.
- **It is nothing like the modelled moist soil** (355 Ω, 671 pF): 57 times the resistance and
  a forty-fourth of the series capacitance. One reading, ten minutes after insertion, in soil of
  unstated wetness; the blade has not settled and no watering has been seen.
- **`Cd` went up, 13.5 → 21.4 pF, not down to the fit's floor.** The synthetic test's soil rows
  pin `Cd` at nothing; this soil does not. `Cp` + `Cd` is 30.08 pF.
- **The calibration rule met a boot during insertion and came out right, by timing.** The
  reading four seconds after the reset was still open, so the calibration ran, and it read
  8.69 pF. Had the blade been conducting by then it would have stayed uncalibrated and flagged.
  The path where a boot finds something conducting has still not been seen on hardware.
- **The tip thermistor reads the soil colder than the air was:** 25.25 °C against 26.0 °C before
  it was picked up, still settling.
- **Second reset during handling in one day**, again with the battery steady. Cause still not
  established; no image reports its reset reason.

### One-minute test build (262161, 2026-10-08)

With board 1 just planted the maintainer asked for the moisture measurement every minute for
testing: "Lets bring the candence back up to 1 min for testing right now. I like that we can tune
that knob but not necessary when solar charging was so effective." Two build parameters, both
compiled out when they are not given:

| Parameter | What it does | Cost or caution |
|---|---|---|
| `-MoistureEveryNWakes 1` | The sweep and fit run on every one-minute wake instead of every tenth. A full fit still comes once in twelve measurements, now every 12 minutes. Every wake is a measurement wake, so every wake opens the 5 s connectable window | Modelled 2.24–2.36 mAh/day against 0.57–0.69 (`msv4_power_budget.py` with one wake per interval), about four times; nothing measured. The script refuses a tag starting `prod` for any cadence but ten |
| `-CpCentiPf 869` | The image starts on this `Cp` instead of zero | Only for a probe that is already in soil, with the value its previous boot calibrated (DIS read `Cp 8.69 pF` at 21:49). The update's reboot happens in soil, where 262160's rule never calibrates, so without it `Cd` would step up by the whole 8.69 pF and stay there for as long as the probe is planted. Flags 2 bit 6 stays set (nothing was calibrated this boot) and the DIS string reads `Cp 8.69 pF default` |

The calibration test gained three rows for a build that carries its own `Cp` (boots in soil and
stays; a hand at every reading; soil then air, where it calibrates as usual) and passes 13 of
13; the warm-start test, the scanner self-test and the packet check pass.

**On board 1, in the Alocasia pot.** 262161 went on over BLE at the 22:07:07 measurement wake
(39 s of transfer, first session, −65 dBm).

| Time (PDT) | Image, counter | `Rsoil` (at 25 °C) | `Cm` | `Cd` | Residual | Fit |
|---|---|---:|---:|---:|---:|---|
| 21:57:09 | 262160, 11 | 20,209.9 Ω (20,310.9) | 15.2 pF | 21.39 pF | 0.0133 | full |
| 22:07:07 | 262160, 21 | 19,109.2 Ω (19,204.7) | 16.1 pF | 21.60 pF | 0.0131 | **warm, accepted** |
| 22:07:54 | **262161**, 1 | 19,012.4 Ω (19,107.5) | 16.4 pF | 21.62 pF | 0.0130 | full |
| 22:09:53 | 262161, 3 | 18,702.6 Ω (18,796.1) | 16.6 pF | 21.63 pF | 0.0130 | warm |
| 22:10:53 | 262161, 4 | 18,788.2 Ω (18,882.1) | 16.4 pF | 21.66 pF | 0.0128 | warm |
| 22:11:53 | 262161, 5 | 18,874.9 Ω (19,063.6) | 16.8 pF | 21.65 pF | 0.0127 | warm |
| 22:12:53 | 262161, 6 | 18,704.9 Ω (18,798.4) | 16.6 pF | 21.69 pF | 0.0130 | warm |

- **Every wake is a fresh reading**, 60 s apart, and DIS reads
  `MSV4-BUILD:fast1min-2026-10-08` / `262161` / `Cp 8.69 pF default`.
- **`Cd` carried straight across the update:** 21.60 pF on the calibrated 8.69 pF, 21.62 pF on
  the built-in 8.69 pF.
- **262160's rule refused to calibrate on a boot in soil.** First sight of that path on
  hardware: the first reading conducted, bit 6 is set in every packet, and the DIS string says
  `default`.
- **The warm fit is accepted in soil.** First sight of that too (the Open list's "watch flags 2
  bit 4 there"). Its timing has not been taken.
- **Reading-to-reading scatter at one minute:** `Rsoil` 18.70–18.87 kΩ over four readings
  (0.9 % end to end), `Cm` 16.4–16.8 pF, `Cd` 21.63–21.69 pF.
- **`Rsoil` came down 7 % in the first quarter of an hour** (20.2 → 18.8 kΩ) while `Cm` and
  `Cd` crept up, with the tip steady at 25.25 °C. That is what soil closing on a freshly
  inserted blade would do; inferred, nothing else was observed.

## The battery reading depended on the boot (found and fixed 2026-10-06)

The maintainer noticed the battery "jump" after the 21:41–21:55 gap. Measured, same cell, in the
dark, no charging possible:

| Image, boot time | Battery readings | Mean |
|---|---|---:|
| 262148, 15:32 (first hour / last hour) | 3821–3832 / 3823–3830 mV | 3826 mV |
| 262150, 21:20 | 3820–3829 mV, flat from the first reading | 3823 mV |
| console 262151, 21:41 (BLE connected, so under load) | 3816, 3816 mV | |
| **262152, 21:55** | 3829, then 3834–3844 mV | **3838 mV** |
| 262153, 22:16 (recalibrates every wake) | 3818–3827 mV | 3821 mV |

The 262152 boot read about 13 mV high for its whole 21 minutes. Its first reading, 3829, is the
console's 3816 plus 13, and it then rose the same 10 mV or so that any reading does once a
transfer's load comes off. The step went away the moment the offset was recalibrated.

Cause, as far as it is established: all of these images calibrated the SAADC offset **once, at
boot**. 262152 was the first to run that calibration earlier in `setup()`, before the GATT
services are registered (so that `Cp` could go into the DIS string). **Measured:** that boot's
calibration was off by about 7 LSB at the ADC input, and the four other boots agree with each
other within 3 mV. **Inferred, not tested:** that the earlier position in `setup()` is why. It
could equally be chance.

The fix does not depend on which: from 262153 the offset is recalibrated at the start of every
wake, with `OVERSAMPLE` forced to bypass first so each calibration is taken in the same
configuration. Scatter is unchanged (9 mV peak to peak, as before), so one calibration per wake
adds no noise. It also follows the offset's drift with temperature, which a boot-time
calibration cannot.

**Cloud rows for sensor 10 between 21:55 and 22:16 on 2026-10-06 carry battery values about
13 mV high.** They have not been altered.

## Where the code comes from

- **Hardware layer** (pins, SAADC, TIMER3/PPI/GPIOTE front end, spectrum sweep, the
  Levenberg-Marquardt fit, the crystal guard): copied from `MSV4_Probe_Bringup_BLE`, which is the
  code measured on board 1 on 2026-10-01 and 2026-10-06. The per-phase start/stop path and the fit
  are unchanged.
- **Duty cycle, advertising window, OTA hold, packet framing**: follow
  `MSV3_Moisture_BLE_Sleep`.

Deliberate differences from the two parents:

| Change | Reason |
|---|---|
| No BLE UART console; DIS and buttonless DFU only | The console is unauthenticated: anyone in range could drive the front end |
| Sensor ID is a required build parameter (`-SensorId`) | Same rule as MSV3: (commit, sensor id) must reproduce an image |
| `Cp` is measured at every boot with the electrodes floating | `spectrum open` was RAM-only and manual. The sweep opens U3, so it does not depend on what the blade is in (**wrong, measured 2026-10-08:** a hand on the blade at boot gave 15.89 pF against 8.74; from 262160 it is taken only after an open reading, see "`Cp` is calibrated only after an open reading"). From 262152 the value is published as the DIS hardware revision string (`Cp 8.75 pF`), not in the packet |
| Moisture sweep every 10th wake, not every wake | A sweep and fit keeps the CPU and HFXO up for about 5 s (console, board 1, 2026-10-01) |
| `TIMER3 TASKS_SHUTDOWN` once per wake, after the last sweep | A TIMER that has run and is only stopped keeps drawing current (nRF52 anomaly 78) |
| Boot counter carries only across watchdog, software and lockup resets, and clears after two completed advertising cycles | MSV3's guard rules. A pin reset or power-on starts at zero |
| The watchdog is left unfed if no advertising cycle completes for 10 minutes | A running but silent board cannot be reached; this board has no USB |
| OTA gate at 3500 mV, open when the battery reading is unusable | The MCU only runs above `VBAT_OK` (3.369 V falling). MSV3's 3800 mV would shut OTA over most of the 3.37–3.89 V window |
| TX power +8 dBm | MSV3 production's setting; the bring-up console used +4 |
| Warm-start fit, from 262157 | The console's `moisture` command still runs the four cold starts every time; it is a manual command and its time does not matter. See "Warm-start fit" |

## Wake cycle

(As first written, for 262148. From 262154 step 3 is the full connectable window only on
measurement wakes and the first three wakes after a boot; see "Power" below.)

Every 60 s:

1. Battery through the Q1 divider, then tip NTC temperature.
2. On the first wake after boot and every 10th wake after: sweep both electrode orientations at
   eight frequencies (250 kHz–2.67 MHz), average, and fit `{Cm, Gs, Cd}`. A failed sweep is retried
   on the next wake.
3. Advertise for 5 s (20 ms for the first second, then 250 ms), connectable. A central that
   connects is held up to 30 s so it can write the DFU jump, then dropped.
4. Sleep the rest of the minute.

## Packet

Manufacturer data. Three forms exist; every reader accepts all of them.

| Application | Length | Notes |
|---|---|---|
| 262148 | 23 bytes | Layout A |
| 262150 | 26 bytes | Layout A plus die temperature |
| 262152 onward | 26 bytes | **Layout B** (flags 2 bit 1): adds `Cd`, moves the residual, drops `Cp` |

Bytes 0–18 keep the MSV3 production layout; flag bit 7 marks an MSV4 packet. The three extra bytes
came from dropping the TX Power structure from the advertisement, which nothing reads: Flags (3)
+ this (2 + 26) = 31.

| Bytes | Layout B (current) | Layout A |
|---|---|---|
| 0–1 | Company ID `0xFFFF` | same |
| 2–5 | Magic `MSV2` | same |
| 6 | Sensor ID | same |
| 7–9 | Advertisement counter, 24-bit LE | same |
| 10–12 | `Rsoil`, 24-bit LE, units of 0.1 Ω, **not** temperature-corrected. Saturates at 4,000,000, which means nothing is conducting; it is a ceiling, not a measured 400 kΩ. Zero when bit 5 is set | same |
| 13 | Fit RMS residual, units of 1e-4 of drive | always 0 |
| 14–15 | Battery mV | same |
| 16 | Flags: bit1 moisture measured on this wake, bit2 tip temperature valid, bit4 OTA allowed, bit5 moisture invalid, bit7 MSV4. Bits 0, 3 and 6 are always 0 | same |
| 17–18 | Tip NTC temperature, int16 LE, quarter-°C | same |
| 19–20 | `Cm`, uint16 LE, units of 0.1 pF. Reads 0 while nothing conducts | same |
| 21–22 | `Cd`, uint16 LE, units of 0.01 pF; 0 = not sent | 21: `Cp` in 0.1 pF (120 = default). 22: residual |
| 23–24 | nRF52840 die temperature, int16 LE, quarter-°C. The board end, not the soil | same (absent in 262148) |
| 25 | Flags 2: bit0 die temperature valid, bit1 layout B, bit2 short non-connectable burst (262154 onward), bit3 DC/DC regulator enabled, read back from `DCDCEN` as the packet is built (262155 onward; clear on the first wake of every boot), bit4 the moisture fields came from an accepted warm-start fit (262157 onward; it stays with the reading through its repeats), bit5 discharge burn active (test builds made with `-BurnFloorMv` only), bit6 no `Cp` calibration accepted this boot, so `Cd` is the whole node capacitance (262160 onward) | bit0 only |

`Rsoil` never needs more than 22 bits, which is where byte 13 came from. A reader that still takes
bytes 10–13 as one 32-bit number sees an out-of-range resistance on a layout B packet and rejects
the reading rather than misreading it.

The layout is an ABI between `startAdv()` in the sketch, `MSV4/scripts/msv4_adv_scan.py`, the
receiver, the cloud function and the receiver's History page.
`DisplayCenter/scripts/verify_msv4_packet_abi.mjs` checks all five against each other in the
receiver's build gate, and `verify_msv4_history_decode.mjs` runs the page's decoding on synthetic
rows.

`Rsoil` is sent raw, with the tip temperature beside it, so the 2 %/°C conductivity correction
stays off the probe like every other calibration in this project.

## Which values are live measurements

Prompted by the History page showing flat lines at 0 and 400,000 on the evening of 2026-10-06.
The bring-up console was put back on the probe for fourteen minutes (21:41–21:55) to read the raw
sweep where the board sat. All measured:

| Console, board as left for the solar test | Result |
|---|---|
| `spectrum open` (electrodes floating) | `Cp` 8.76 pF ± 10.3 %, fit RMS 0.0206. Phase −0.9° at 250 kHz to −8.6° at 2.67 MHz |
| `spectrum ab` (electrodes connected) | Phase −2.1° to −20.1°, `\|H\|` 0.994 to 0.946: clearly not the open sweep, so the electrodes are switched in and measured |
| Fit of that sweep | `Cd` **13.00 pF** ± 9 %, `Cm` 0.00 pF, `Rsoil` 1.0e17 Ω, RMS 0.0230 |
| `moisture`, four repeats | `Cd` 12.98, 13.01, 13.03, 13.01 pF. `Rsoil` 9.4e13, 8.2e15, 1.6e17, 9.5e13 Ω |
| Electrode A alone / B alone | `Cd` 12.98 / 13.05 pF |

What that says:

- **The transport is not losing anything.** The production packets before and after agree with
  the console to the last digit sent: residual 0.0228–0.0231, `Cm` 0, `Cp` 8.8 pF, and after
  262152 `Cd` 13.03 pF.
- **The blade had nothing conductive on it.** 13 pF of capacitance and no conduction is the
  bring-up record's result for air.
- **In that state most of the packet is not a measurement.** `Rsoil` in air is numerical noise
  spanning three decades, which is why the probe reports a ceiling instead. Moisture is 0 because
  it is derived from that ceiling. `Cm` is 0 because nothing touches the plates. `Cp` is a
  once-per-boot calibration.
- **`Cd` is the live one,** repeatable to about 0.02 pF, and layouts before B did not send it.

Changes made because of it: layout B carries `Cd` at 0.01 pF; the History page no longer draws
the ceiling as a resistance or `Cm` for an open reading, and says "OPEN, nothing conducting"
instead; `Cp` is labelled as a calibration constant.

**The residual floor is in the instrument, not the soil.** The floating-electrode sweep already
fits no better than 2.06 % RMS, and its `|H|` reads above 1 at the top two frequencies (1.003 and
1.011), which no RC divider can do. Its cause has not been looked for.

## What a receiver without the port does with it

This applies to any receiver still on build `2026-09-14 msv3-capless-scale3` or older. Read from
the source first, then observed on the DisplayCenter at 20:01 before it was reflashed:

- The packet passes the company-ID, magic, sensor-ID (1–16) and length checks and is parsed as an
  MSV3 wide packet.
- The receiver requires an MSV3 FDC count between 5,000,000 and 40,000,000. `Rsoil` never exceeds
  4,000,000, so every MSV4 packet takes the **failed-measurement path**: sensor 10 shows as alive,
  its battery mV and percentage update, and nothing is written to history or the cloud.

So **no MSV4 reading, battery included, reaches the cloud until the receiver understands flag
bit 7.** Until a receiver carrying the port below is flashed, `msv4_adv_scan.py --csv` logs every
packet a computer in BLE range hears.

## Receiver port (flashed to the DisplayCenter 2026-10-06 20:02 PDT)

Build tag `Treebeard runtime build 2026-10-06 msv4-conductance-scale4`; 2,277,702 bytes (72 % of
program space), all ten checks in the receiver's build gate pass.

Measured on the receiver's serial log, before and after the flash:

| | Sensor 10 |
|---|---|
| Old firmware, 20:01 | `sensor 10 reported a FAILED measurement (wake 270); still reachable, reading not stored` — the behaviour read from the source above, now observed |
| New firmware, wake 272 | `rawBatteryMv=3825 batteryPercent=74 rawFdcCounts=4000000 measuredMoisture=0 tankPhase=1 boardTempQ4C=95 boardTempValid=1` |
| New firmware, wakes 273–274 | same values with `tankPhase=0` (repeats of that measurement) |
| Cloud | `upload ok: HTTP 200 compact history=7 queued=0` 94 s after the first sensor 10 packet |

The battery percentage and the open/zero moisture value match the figures worked out by hand
below. Sensors 1, 4 and 7 kept reporting through the same build. **Not checked:** that the
sensor 10 rows are in the database (inferred from the 200 and the emptied queue, since the
function rolls a whole batch back on one bad row), and anything with the blade in soil.

MSV4 values ride in the fields their MSV3 counterparts already use, so the durable history record
and the cloud columns are unchanged. Moisture scale version **4** is what tells every consumer
which meaning applies.

| MSV4 quantity | Receiver field | Cloud column / JSON key |
|---|---|---|
| `Rsoil`, 0.1 Ω | `rawFdcCounts` | `raw_fdc_counts` (`raw.fdcCounts`) |
| Tip temperature | `rawBoardTempQuarterC` | `board_temp_quarter_c` |
| Measured on this wake (1) or repeated (0) | `tankPhase` | `tank_phase` |
| `Cm`, `Cd` | `rawBareTankFdcCounts` = `Cm` \| `Cd` << 16, `bareTankValid` false | `raw.msv4.{cmDeciPf,cdCentiPf}` → `bare_tank_fdc_counts`, one word: bits 0–15 `Cm`, 16–23 `Cp` (layout A rows only), 24–31 residual, 32–47 `Cd` in 0.01 pF |
| Fit residual | `msv4FitRmsE4`; in the history record, the top byte of `rawFdcCounts`, masked off before upload | `raw.msv4.fitRmsE4` → same word |
| Die temperature | `msv4DieTempQuarterC`; in the history record, the `rawMoistureKHz` slot offset by 1000 so 0 stays "absent" | `raw.msv4.dieTempQuarterC` → `raw_moisture_khz`, plain quarter-°C |
| Scale | `msv4` → `moistureScale` 4 | `moisture_scale` |

Receiver-side calibration:

- **Moisture number** = `10000 × log10(400 kΩ / Rsoil@25°C)`: 0 with nothing conducting, about
  30,500 at 355 Ω. `Rsoil` is corrected to 25 °C at 2 %/°C from the tip NTC when that is valid.
  As with every other scale, the per-plant dry/wet bounds turn it into a percentage.
- **Battery percent** = share of the usable window, 3369 mV (load cut-off) to 3889 mV (charge
  limit), on the existing LiPo curve. 3825 mV reads about 74 %; on the plain curve it would read
  46 %.
- A reading is accepted when bit 5 is clear and `Rsoil` is between 1 Ω and 400 kΩ. The MSV3
  temperature-compensation learner is reported as off for MSV4.

The new check `DisplayCenter/scripts/verify_msv4_packet_abi.mjs` is in the build gate and fails the
build if the probe sketch, the receiver and `msv4_adv_scan.py` stop agreeing on the layout.

### Cloud and History page (same evening)

- `receiver-snapshot` v28 stores the `Cm`/`Cp`/residual word, v29 the die temperature, v30 `Cd`.
  No database migration: all of it goes into existing columns that a scale-4 row otherwise leaves
  null.
- Receiver builds `msv4-history-charts` (21:10), `msv4-die-temp-v3` (21:19) and
  `msv4-layout-b-v4` (21:53) add MSV4 cards to the History page, the last adding the `Cd` card
  and the open-circuit handling described above: tip thermistor temperature (relabelled; it had been showing under "Board (die)
  temperature"), die temperature, soil resistance as measured and at 25 °C, `Cm`, fit residual,
  `Cp`, and signal strength (every probe). The MSV3 FDC cards skip MSV4 rows, battery percent uses
  the MSV4 window, and chart axes show decimals when the grid step is below 1.

Read back from the database after the second probe build went on (measured):

| Counter | Battery | Tip | Die | `Rsoil` | Measured this wake | `Cp` | Residual |
|---:|---:|---:|---:|---|---|---:|---:|
| 1 | 3820 mV | 22.75 °C | 24.0 °C | open | yes | 8.8 pF | 2.28 % |
| 2 | 3825 mV | 22.75 °C | 23.5 °C | open | no | 8.8 pF | 2.28 % |
| 3 | 3821 mV | 23.0 °C | 23.5 °C | open | no | 8.8 pF | 2.28 % |

After 262152 (measured, counters 1–3 at 21:55–21:57): 3829–3839 mV, tip 23.0 °C, die
23.5–24.0 °C, open, `Cd` 13.03 pF, residual 2.29 %, and the DIS hardware revision reads
`Cp 8.75 pF`.

Rows uploaded before v28 (20:03–21:06) have battery, `Rsoil`, tip temperature and signal strength
only. **Not seen:** the History page itself in a browser. Its decoding was run against synthetic
rows and its script parses, but nobody has looked at the rendered charts yet.

## Not yet measured

- Sleep current, and awake time and charge per wake, of this image. Every energy figure for it is
  modelled.
- Whether `Cp` measured with the blade in soil differs from `Cp` in air (8.8 pF, board 1,
  2026-10-01). With a hand on the blade it does: 15.89 pF at the 15:59 boot on 2026-10-08. Soil
  itself is still unmeasured.
- Whether `Cd` sits at the fit's floor (0.01 pF on the wire) in moist soil with an air-calibrated
  `Cp`. The synthetic test's does; if board 1's does, `Rsoil` is carrying the difference.
- Why board 1 reset while it was being handled on 2026-10-08. No image reports its reset reason.
- The boot-guard escape and the 10-minute radio-stall reset on this image. Neither has been
  provoked on hardware.
- (Measured since: the buttonless jump out of this image. At 21:20 `msv4_ble_ota.py` caught the
  production image's 5 s window, connected in 2.4 s, jumped it and installed 262150 in 39 s.)
- (Measured since: a reading in soil. 20.2 kΩ, `Cm` 15.2 pF, `Cd` 21.39 pF in the Alocasia pot
  at 21:57 on 2026-10-08; see "First reading in soil".) Still unmeasured: soil of known
  wetness, a watering, and the settled value.

## Open

- [x] Receiver: parse flag bit 7, give MSV4 its own moisture scale version, store tip temperature
      — *written and compiled 2026-10-06; see "Receiver port"*
- [x] Flash a receiver with the port and confirm sensor 10 is ingested — *done 2026-10-06 20:02*
- [x] Read sensor 10's rows back from the cloud — *done 2026-10-06, see above*
- [x] Cloud: store `Cm`/`Cp`/fit residual and die temperature; History page: MSV4 cards, battery
      window, labels — *done 2026-10-06*
- [ ] Look at the History page for sensor 10 in a browser and confirm the cards render
- [ ] The cloud PWA (`cloud/web/plant-dashboard`) has not been touched; it has no raw charts
- [ ] MSV4 still shares MSV3's DFU device type `0x3356`. `msv4_ble_ota.py` refuses an MSV3 image
      by its identity string, but the bootloader itself would accept one
- [ ] Measure sleep current and per-wake charge
- [x] Enable the nRF52840 DC/DC regulator — *done over BLE 2026-10-07 in 262155; board 1 runs
      with it on, see "Result on board 1"*
- [ ] Measure the supply current with the converter on and off; the 0.4 mAh/day saving is still
      modelled
- [x] Cut the fit time — *done 2026-10-07 in 262157 with a warm start: 3.0 s to 1.0 s per
      measurement wake in air, see "Warm-start fit"*
- [ ] Time the fit with the blade in soil and watch flags 2 bit 4 there: the replica predicts
      four cold starts cost what they do in air and a warm fit about a third of the air one, and
      no sweep has been recorded in soil to check either
- [x] Install 262160 and see it calibrate after its first open reading — *done 2026-10-08
      18:59: `Cp 8.93 pF`, `Cd` 13.33 pF, bit 6 clear*
- [x] See the rule refuse a boot with something on the blade — *done 2026-10-08 22:07: 262161
      booted in soil, bit 6 set, `Cp 8.69 pF default`. Not yet seen: the calibration that
      follows at the first sane reading in air, and a build without `-CpCentiPf` in soil
      (`Cp 0.00 pF default`, `Cd` near 30 pF).* Board 1's `Cd` rows from 15:59 to 18:59 on
      2026-10-08 are 7.15 pF low
- [ ] Put the ten-minute cadence back on board 1 when the one-minute testing is over (262161
      is a test image)
- [ ] Single battery readings stand 12–25 mV apart from their neighbours about once every two
      hours (11 of 860 in the stored history of 2026-10-06 23:10 to 2026-10-07 13:34, of either
      sign, in the dark as well; the 3850 mV on the first wake of the 262157 boot is one). Cause
      not established. Listed in the solar charge test record
