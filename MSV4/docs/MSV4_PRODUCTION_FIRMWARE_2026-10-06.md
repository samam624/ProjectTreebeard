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
| Fourth build, same day | `MSV4-BUILD:prod-2026-10-06d`, application version 262153: ADC offset recalibrated at every wake. On board 1 since 22:16 PDT |
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
| `Cp` is measured at every boot with the electrodes floating | `spectrum open` was RAM-only and manual. The sweep opens U3, so it does not depend on what the blade is in. From 262152 the value is published as the DIS hardware revision string (`Cp 8.75 pF`), not in the packet |
| Moisture sweep every 10th wake, not every wake | A sweep and fit keeps the CPU and HFXO up for about 5 s (console, board 1, 2026-10-01) |
| `TIMER3 TASKS_SHUTDOWN` once per wake, after the last sweep | A TIMER that has run and is only stopped keeps drawing current (nRF52 anomaly 78) |
| Boot counter carries only across watchdog, software and lockup resets, and clears after two completed advertising cycles | MSV3's guard rules. A pin reset or power-on starts at zero |
| The watchdog is left unfed if no advertising cycle completes for 10 minutes | A running but silent board cannot be reached; this board has no USB |
| OTA gate at 3500 mV, open when the battery reading is unusable | The MCU only runs above `VBAT_OK` (3.369 V falling). MSV3's 3800 mV would shut OTA over most of the 3.37–3.89 V window |
| TX power +8 dBm | MSV3 production's setting; the bring-up console used +4 |

## Wake cycle

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
| 25 | Flags 2: bit0 die temperature valid, bit1 layout B | bit0 only |

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
  2026-10-01).
- The boot-guard escape and the 10-minute radio-stall reset on this image. Neither has been
  provoked on hardware.
- (Measured since: the buttonless jump out of this image. At 21:20 `msv4_ble_ota.py` caught the
  production image's 5 s window, connected in 2.4 s, jumped it and installed 262150 in 39 s.)
- Any reading in soil. Board 1 has only been measured in air, on a damp towel and in a hand.

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
