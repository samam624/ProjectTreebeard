# ESP32 Display Flashing Notes

Use this when reflashing the wall-powered ESP32-S3 display receiver.

## Build-two receiver (second DisplayCenter, V2 enclosure)

There are now two receivers. Identify them by chip MAC (`esptool read-mac`), not by COM port:
the main receiver is `28:84:85:46:70:D8` (historically `COM16`); the build-two board is
`28:84:85:27:9a:d8`.

**Status 2026-11-21 ~20:30 PST: ready to hand over to the maintainer's parents.** Cloud receiver
ID `MSV2D89A27858428`, its own household. Both receivers now run `Treebeard runtime build
2026-11-21 no-clear-history-v1` (the History tab's "Clear All History" button is gone). Build two's
copy was compiled with the gitignored `WiFiCredentials.h` set aside, so it carries no home WiFi and
sits in the first-time setup portal with its identity and cloud token kept. Tested alongside the
main receiver first: see [MULTI_RECEIVER_TEST_2026-11-21.md](MULTI_RECEIVER_TEST_2026-11-21.md).

**Earlier, 2026-11-21 ~17:55 PST: working.** Its USB connector broke off the board while being
plugged in (roughly the 50th insertion). The maintainer resoldered it with more reinforcement. Checked
afterwards on `COM3`:

- The display comes up.
- It enumerates as `VID_303A&PID_1001` with serial `28:84:85:27:9A:D8`, as both the serial port and
  the JTAG unit.
- The boot log arrived clean at 115200.
- A line sent from the PC (`ping`) got the firmware's unknown-command reply, so data moves both ways.

Opening the port reset the board once (`rst:0x15 USB_UART_CHIP_RESET`), which is normal for the
native USB. Firmware (`boot-repaint-v2`), flip (on) and touch calibration are unchanged. It is
still in the first-time WiFi setup portal, so it is not yet scanning for probes. The connector's
strength under cable strain was not tested.

**Firmware confirmed current, 2026-11-21 ~18:45 PST, before commissioning.** `origin/main`
(`6e6c0b7`) had no receiver firmware change after `24a3ee0`. A clean `arduino_display.ps1 -Action
compile` of that source was byte-identical to the 11-20 `boot-repaint-v2` image (app SHA-256
`28bc414b…`, plus the same bootloader and partition table). `esptool verify-flash` on `COM3` (MAC
`28:84:85:27:9a:d8`) then matched the board's own MD5 for the bootloader, partition table,
boot_app0 and application. The check was read-only, so LittleFS was untouched. Afterwards the
firmware answered `ping` over serial.

As of 2026-11-20 ~14:35 PST the build-two board runs `Treebeard runtime build 2026-11-20
boot-repaint-v2`, flashed the same way. Hash verified. It is `screen-flip-v2` plus the single
boot repaint described under the open issue below. Serial showed `boot repaint at 10001 ms` on
its first boot. Its flip setting (on) carried over in LittleFS. (`boot-repaint-v1`, flashed
~14:26, repainted at 10 s, 30 s, 2 min and 5 min; the maintainer asked for the 10 s one only.)

As of 2026-11-19 ~13:45 PST the build-two board, on `COM3` over its native USB, ran
`Treebeard runtime build 2026-11-19 screen-flip-v2`, flashed with `arduino_display.ps1 -Action
upload -Port COM3`. Hash verified. Before the first flash (`screen-flip-v1`, ~13:20) its
partition table was read back and matched the build's `huge_app` 4 MB layout, so LittleFS
(including its touch calibration) was kept. It is in the first-time WiFi setup portal, with the
flip set on through `flip on` over serial.

What the build adds:

- **Screen flip**, for the V2 enclosure, which holds the panel upside down relative to the board.
  You can set it in five places: the **Flip screen** button at the bottom right of the WiFi
  setup screen; long-press the dashboard and tap **Flip screen**; the phone app's Setup page
  under **Screen Orientation**; `POST /orientation {"flipped":true}`; or `flip on` / `flip off`
  over USB serial. It is stored in `/orientation.json` on the receiver only
  and is never written to the cloud-synced ui.json `rotation`, because that config is shared
  between receivers. Touch calibration stays in the physical rotation-1 frame, so a flip does
  not need a recalibration. A calibration run while flipped puts each target at the matching
  rotation-1 corner.
- **Boot screen diagnostics.** Until the first real screen is drawn, the "MSV2 booting" screen
  shows the current boot step and the reset reason, so a board on a wall charger that stops
  there shows where it stopped.
- **Boot watchdog.** If `setup()` has not returned 180 s after it starts, an `esp_timer` calls
  `abort()`. The panic writes a core dump with loopTask's backtrace to the coredump partition,
  then reboots, and the boot screen then shows `reset: panic`. Decode the dump against the
  build's ELF in `.arduino-build/display-normal/`. Not yet exercised on hardware.
- **USB serial commands** (115200, newline-terminated): `flip on`, `flip off`, and
  `ble-beacon <1-600>`. The last advertises a non-connectable `MSV2-DISPLAY` beacon for that many
  seconds so a PC can measure this board's BLE signal. The receiver otherwise never transmits
  over BLE. In the setup portal this is also the first BLE init, and BLE then stays up until
  the next reboot.

BLE signal from the board to the PC, measured 2026-11-19 ~13:45 PST. The antenna was inside the
V2 enclosure and the board was next to the maintainer's computer (not its final location). The
PC heard 48 reports in 70 s from `28:84:85:27:9a:d9` at +9 dBm TX (`esp_ble_tx_power_get` raw
level 11): median −35 dBm, range −41 to −33. The board's WiFi AP beacon (same antenna, 2412 MHz)
read −39 dBm on the PC's WiFi adapter in five scans out of five. At this distance the reading
says the antenna is connected and radiating, but nothing about loss at range.

Open issue, reported 2026-11-19: the maintainer has seen this board stay on "MSV2 booting"
indefinitely after being plugged in. Measured on 2026-11-19:

- A reset over USB, and a real unplug/replug (reset reason `power-on`) with a serial reader
  attached, both reached the setup portal in about 3 s.
- The coredump partition (0x3F0000) was blank before the flash. Core dumps to flash are enabled
  in this core (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH 1`), so the board had not panicked. Panics
  include the task and interrupt watchdog resets. Brownout resets write no core dump.
- While it was stuck, the serial port was enumerated and silent for 12 s with the port open,
  and showed no reset or re-enumeration.

Not yet reproduced. On `screen-flip-v2`, the maintainer unplugged and replugged it a few more
times with no serial reader attached, and it booted normally every time (2026-11-19 ~13:55 PST).
No firmware change targeted a cause, so treat the issue as intermittent and unexplained, not
fixed. If it recurs, read the boot screen's step line, then the coredump partition once the
boot watchdog has fired. ~~In first-time setup mode, the setup screen is painted before anything slow
runs, so a stall that keeps "MSV2 booting" visible has to come before `setupWifi()`, or from
local-only mode, which paints nothing until after BLE init. That is inferred from the code
path, not measured.~~ **Wrong, see 2026-11-20: the firmware was not stalled at all.**

**Recurred 2026-11-20, and it is not a hang.** The maintainer plugged the board into the PC
after it had been unplugged for a while ("the first time in a while") and it stayed on "MSV2
booting". The maintainer's hypothesis is that it only happens after the board has been
unpowered for some time, which is why replugging it straight away on 11-19 never reproduced it.
Measured, in order (times PST, session clock):

- Windows `DEVPKEY_Device_LastArrivalDate` for the composite device: 13:37:33. It did not
  change for the rest of the session, so the board never reset or re-enumerated.
- The boot screen read `initializing XPT2046 touch` on the step line and `reset: power-on`
  (maintainer, read off the panel).
- COM3 opened with DTR and RTS both deasserted (asserting RTS resets an ESP32-S3 over
  USB-Serial-JTAG): silent for 8 s.
- ~13:45: sending the unknown word `ping` over COM3 returned `[Treebeard cmd] unknown; ...`
  within 4 s. Only `loop()` calls `serviceSerialCommands()`, so `setup()` had returned and the
  loop was running. That is also why the boot watchdog never fired: `setup()` returning
  disarms it.
- 13:51:01: `flip off` printed `[Treebeard display] flipped=false`, which runs a full repaint.
  The maintainer confirmed the screen then showed the right content. `flip on` at 13:53:39
  restored the setting.

So every display write after the `initializing XPT2046 touch` step line was lost: the next
`bootLog()` (drawn unconditionally after the first `touched()` check) and the setup or
dashboard paint. By 13:51 writes landed again. A static screen is never repainted, so the stale
boot screen stayed up indefinitely. Not established: whether the panel recovered on its own
before 13:51 or was recovered by the repaint, and whether the touch controller was also
unreachable during the window.

**Third occurrence, same day.** After another long unplug the maintainer plugged it in at
14:12:17 (`LastArrivalDate`), and it showed the same symptom (step line not read this time).
`ping` was answered at 14:14:06. A `flip off` + `flip on` pair at 14:14:17-14:14:23 (two full
repaints, final setting unchanged) brought up the correct screen, which the maintainer
confirmed. So the panel was accepting writes again less than 2 minutes after plug-in. The lost
window starts after the `initializing XPT2046 touch` step line is drawn, a few seconds into
boot, and ends at some point within those 2 minutes. Its exact length is not measured.

**Mitigation, `boot-repaint-v2` (2026-11-20).** `serviceBootRepaint()` in `loop()` repaints the
current full screen once, at 10 s of uptime, and never again that boot. 10 s alone is the
maintainer's choice. It uses `repaintCurrentScreen()`, the same repaint the flip uses (renamed
from `repaintAfterOrientationChange()`). It runs ahead of the overlay and setup-portal early
returns, and it holds off while a finger is down or a touch read is pending. It logs
`[Treebeard display] boot repaint at <ms> ms`. This hides the symptom; it does not explain it.
~~Not yet tested against a real cold plug-in: the next one needs the board unplugged for a while
first.~~ The dead window was measured only as under 2 minutes. If the screen is still stale after
10 s, the window outlasted the repaint, and later repaints (as `boot-repaint-v1` had) are the
next step.

**The repaint worked, reported 2026-11-21 ~18:45 PST.** The maintainer: "the loading screen hung
earlier and the 10s repaint caught it." So on that boot the screen stuck as before, and the
display was accepting writes again by 10 s of uptime. That narrows the lost window to under 10 s
for that boot. This was observed by the maintainer, not captured: no serial log or step line was
recorded. The maintainer did not say which boot it was. The likely one is the first plug-in after
the USB connector was resoldered, since the board was unpowered during the repair, but that is
inferred. The cause of the lost display writes is still unknown. If a later boot's window runs
past 10 s, the screen will stay stale again.

Flashing it needed `DisplayCenter/scripts/verify_msv4_packet_abi.mjs` brought up to date with
MSV4 commit `0b97b6b` (2026-11-19), which had broken the receiver's pre-flash gate. The mask
check is now counted in minutes (`g_minutesSinceMaskCheck`/`MASK_CHECK_MINUTES = 1440`). There is
a new report-only `measureCp(g_bootOpenCp)` in `MSV4_REPORT_BOOT_OPEN_CP` bench builds. The
"one caller" count became the rule it stood for: only `calibrateCpAfterOpenReading()` writes
`g_specCp`/`g_cpCalibrated`, and the boot call stays bench-only. Mutation-tested: a boot call that
writes the applied Cp, an ungated boot call, an extra caller and a second writer on a shared
line are all caught.

Ruled out:

- **The touch library re-routing the bus.** `XPT2046_Touchscreen::begin()` (library 1.4)
  calls `SPIClass::begin()` with no pins, but in core 3.3.12 that returns at once when the bus
  is already started (`if (_spi) return true;`), and `Arduino_HWSPI::begin()` (GFX 1.6.9) has
  already started it with the display's pins. Read from source, not measured.
- **The 32 kHz crystal pads.** `TFT_RST` (GPIO15) and `TFT_CS` (GPIO16) are the S3's
  XTAL_32K_P/N pins, but this build uses `CONFIG_RTC_CLK_SRC_INT_RC=y`, so those pads are never
  enabled.
- **A corrupted touch calibration from a phantom press.** The library's pressure is
  `z1 + 4095 - z2`, so a touch controller that does not answer (both reads equal, all zeros or
  all ones) reads as pressure 4095, a hard press. That would start `runTouchCalibration()`
  unseen, but four identical points fail its accuracy check and the previous calibration is
  restored. Read from source.

Instruments that would separate the remaining causes (panel ignoring writes vs. the shared SPI
bus not reaching either chip): record the raw XPT2046 pressure from the boot `touched()` check
(4095 would mean the touch controller was not answering either), and read the ST7796S back over
the wired `TFT_MISO`/GPIO10 (`RDDPM` 0Ah, `RDDMADCTL` 0Bh, `RDDCOLMOD` 0Ch) after each boot
stage. Neither exists yet. JTAG over the built-in USB was not available: the JTAG interface
(`MI_02`) is bound to Microsoft's generic `winusb.inf` with no `DeviceInterfaceGUIDs`, so
OpenOCD's libusb fails with `LIBUSB_ERROR_NOT_FOUND`. Fixing that needs an admin driver
install (Espressif's driver or Zadig).

Separately, the "previous checkpoint before reset" line cannot do its job. `lastCheckpoint*`
are `RTC_DATA_ATTR` with initializers, and the ESP-IDF bootloader reloads initialized RTC data
on every reset except a deep-sleep wake. So the line reads "fresh boot" after an ordinary reset
too, and it read exactly that after every reset on 2026-11-19. Moving the checkpoints to
`RTC_NOINIT_ATTR` with a validity magic, like `rtcCloudStallRestartMagic`, would make them
survive.

## Current Known-Good Status (main receiver)

As of 2026-11-10 ~00:45 PST, the receiver on `COM16` runs
`Treebeard runtime build 2026-11-10 tls-psram-v1`, flashed with `arduino_display.ps1 -Action
upload`: mbedTLS allocates from PSRAM (`installTlsPsramAllocator()`; `/device`
`health.tlsAllocator` reads `psram`). Before it, on `msv4-display-hold-v1` after 11.6 h: 104
TLS connects failed with -32512 (MBEDTLS_ERR_SSL_ALLOC_FAILED), largest internal block 31,732
on nearly every reading, free-heap low-water 7,680 bytes, reset reason `panic/exception` twice
running. First reading after the flash: largest internal block 51,188, free 93,244.

Before that, as of 2026-10-27 ~23:25 PDT, the receiver on `COM16` runs
`Treebeard runtime build 2026-10-28 msv4-display-hold-v1` (the tag's date is the next day's), flashed
with `arduino_display.ps1 -Action upload`: the MSV4 drying hold in the presentation filter
(`MSV4_DISPLAY_DRYING_HOLD_2026-10-27.md`). Cloud uploads returned HTTP 200 after the flash.

Before that, as of 2026-10-25 16:22 PDT, the receiver on `COM16` (chip MAC `28:84:85:46:70:D8`) runs
`Treebeard runtime build 2026-10-15 msv4-raw-sweep-v3`, rebuilt from `main` and reflashed with
`arduino_display.ps1 -Action upload`. Another agent session had flashed a microphone-array test
sketch onto it; that sketch carried an 8 MB partition table (app0 0x10000-0x340000, spiffs at
0x670000). The receiver's 4 MB `huge_app` LittleFS at 0x310000 was read back before the restore
and was intact (superblock, receiver ID `MSV2088A3104A7AC`, `/wifi.json`, `/cloud.json`,
`/ui.json`, history outbox), so no setup was needed. After the flash: hash verified, upload
`HTTP 200` with nothing queued, sensors 4, 7, 10 and 11 received, and `/device` reporting the
build above with the same receiver ID. Sensor 10's cloud history has one gap, 16:13-16:23 PDT.

**Before flashing any ESP32 here, check which board is on the port.** The mic-array board also
enumerates as an `ESP32 Family Device` (it was on `COM3`). Its serial output is `mic0..mic7` level
lines, not `[Treebeard ...]` lines.

Before that, the last flash recorded here was on 2026-10-15 at 20:57 PDT (`msv4-raw-sweep-v3`, see
`MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md`). As of 2026-10-14 10:47 PDT, the display receiver was reflashed on `COM16` with the normal
production build:

```text
Treebeard runtime build 2026-10-14 tls-stall-restart
```

It follows a 6.7 h upload stall (`DisplayCenter/docs/RECEIVER_TLS_STALL_2026-10-14.md`).
- **Error capture:** each failed upload connect now records its real `lastError()` code, shown in
  `/cloud` under `upload.lastTlsFailure`.
- **Stall restart:** after 30 min with no successful upload while WiFi stays up, the receiver
  restarts itself. After such a restart it waits 6 h before another, and that boot has no chime.
- **Checks:** all twelve `DisplayCenter/scripts/verify_*.mjs` checks passed and the flash hash
  verified. `/device` reports the build above, and the first upload returned `HTTP 200` at 27 s
  of uptime with the queue empty.

Inferred from timing: the build below also carries commit `e048a60` (battery percent scaled to
the charge plateau). It was committed at 22:29:37 PDT on 10-11, and the receiver booted at
~22:29 PDT under the unchanged `-262166` tag, which ran until the stall.

The 2026-10-11 20:48 PDT flash on `COM16` used this production build:

```text
Treebeard runtime build 2026-10-11 water-now-region-v2-msv4-262166
```

It keeps `water-now-region-v2` unchanged and adds the 20:39:37 boot of MSV4 board 1 application
262166 to the History page's air-baseline table. Before flashing, a 75-second no-reset capture
proved `COM16` was the receiver: `[Treebeard web]` and `[Treebeard cloud]` logs, sensor 10 wake 8,
and a successful `HTTP 200` upload. The guarded upload passed all eleven receiver checks and the
flash hash verified. Afterward, WiFi and BLE came up, sensor 10 was received at wakes 10 and 11,
uploads returned `HTTP 200` with no queued rows, and `/device` reported the exact build above.

The build before it, at 19:58 PDT, was:

```text
Treebeard runtime build 2026-10-11 water-now-region-v2
```

One change from `water-now-region-v1` below: a region-calibrated card gets a small yellow
bracket under its meter, from the overdue line (−21 %) to the watering line (0 %, the meter's
dotted marker). It marks the stretch of the meter in which the card says `WATER NOW`. The
maintainer picked it from two mock-ups ("I like the right one the bar under looks great",
`WATER_NOW_METER_MOCKUP_2026-10-11.png`); `WATER_NOW_DISPLAY_AS_BUILT_2026-10-11.png` is the
screen drawn from the sketch's geometry and that evening's values.

- `drawWaterNowRange()` draws it wholly below `drawBar()`'s rectangle, so a bar repaint never
  touches it. It is drawn in the two full draws of a regular card and inside the two
  value-changed clears, which now reach three rows further down to cover it.
- Regular cards only: a compact card (thirteen cards and up) has no rows under its bar.
- The receiver's page and the dashboard (`app.v17.js`, deployed) draw the same bracket.

Measured: before the flash `COM16` showed `[Treebeard ...]` lines, sensor 4 and an upload.
After it: `auto-calibration updated` at 19:58:26, sensor 10 received at wakes 416 and 417 (the
probe did not restart), uploads `HTTP 200` at 19:58:44 and 19:59:47, `/device` reporting the
build tag above, the served page carrying the bracket's markup, and the live dashboard serving
`app.v17.js`. **Not seen:** the display itself.

The build before it, 19:23 PDT the same day, at the maintainer's word ("yeah lets build this
now into the receiver"):

```text
Treebeard runtime build 2026-10-11 water-now-region-v1
```

It takes each sensor's 0 % and 100 % from the cloud's "water now" region and says `WATER NOW`
on a card below 0 % (`cloud/docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md`, "Built").
What changed in the sketch:

- The calibration pull asks for `autocal=1&region=1`. The answer keeps the field names
  (`ready`, `dryBound`, `wetBound`) and adds `region`; `dryBound` is the top of the region and
  `wetBound` the level the pot settled at.
- A region-calibrated card prints `WATER NOW` where it printed a negative percent, and drops the
  row's "Moisture" label while it does (the two do not fit on a four-column card).
- The red, blinking, buzzing threshold moved from −10 % to −21 %, on the display, the
  receiver's page and the dashboard bundle (`app.v16.js`, **not deployed**: the live dashboard
  still runs `app.v15.js` at −10 %).
- A sensor with no region yet (`ready` false) shows its manual range, as "learning" did before.

Measured: before the flash `COM16` showed `[Treebeard ...]` lines, sensors 4, 7 and 10 and an
upload `HTTP 200`. After it: `auto-calibration updated` at 19:23:24, sensor 10 received at wakes
381 and 382 (the probe did not restart), uploads `HTTP 200` at 19:23:42 and 19:24:45, `/device`
reporting the build tag above, and `/data` giving sensor 10 `region` true with 14,640 and 20,778
(96 % at a reading of 20,521) and sensors 1 and 4 not ready. **Not seen:** the display itself,
and `WATER NOW` on any card (no sensor is below its line).

The build before it, 13:09 PDT the same day:

```text
Treebeard runtime build 2026-10-11 msv4-air-baseline-262165
```

One change: the History page's air-value table gains the boot of MSV4 board 1 that began at
13:03:43 that day (probe application 262165). Measured: before the flash `COM16` showed
`[Treebeard ...]` lines; after it, WiFi came up at 13:09:44, sensor 10 was received at wake 8
(13:10:39), snapshot uploads returned `HTTP 200` at 13:09:47 and 13:11:05, and the page served
at the receiver's address carries the new entry. Wake 7 fell in the flash and was not received.
Not captured: the boot line with the build tag. Not seen: the cards in a browser.

The build before it, 11:59 PDT the same day:

```text
Treebeard runtime build 2026-10-11 msv4-air-baseline-262163
```

One change: the History page's air-value table gains the boot of MSV4 board 1 that began at
11:52:48 that day (probe application 262163), so the two experimental cards keep their values
across the probe update. Measured: before the flash `COM16` showed `[Treebeard ...]` lines and
sensor 10 at wake 6 of the new boot; after it, WiFi came up at 11:59:32, sensor 10 was received
at wakes 8 and 9, snapshot uploads returned `HTTP 200` at 11:59:54 and 12:00:58, and the page
served at the receiver's address carries the new entry. Wake 7 fell in the flash and was not
received. Not captured: the boot line with the build tag. Not seen: the cards in a browser.

The build before it, 2026-10-10 21:01 PDT:

```text
Treebeard runtime build 2026-10-10 msv4-relative-residual-v3
```

It removes the raw fit-residual History card at the maintainer's word ("remove the fit residual
raw from history"); the card "Fit residual as a share of the electrode signal" replaces it.
Measured: before the flash `COM16` showed `[Treebeard ...]` lines; after it, snapshot uploads
returned `HTTP 200` at 21:01:19 and 21:02:34, sensor 10 was received at wake 2815 (the probe did
not restart), and the page served at the receiver's address has no raw residual card and has
the relative one and the experimental one. Not captured: the boot line with the build tag.

The build before it, 20:43 PDT the same day, `msv4-relative-residual-v2`, added that relative
card and kept the raw one relabelled. Measured then: sensor 10 at wakes 2797 and 2798, an upload
`HTTP 200` at 20:44:30, the new card and its function in the served page.

The build before it, 14:19 PDT the same day:

```text
Treebeard runtime build 2026-10-10 msv4-soil-coupled-v1
```

That one added the History card "EXPERIMENTAL moisture reading: soil-coupled
capacitance" (`MSV4/docs/MSV4_EXPERIMENTAL_MOISTURE_2026-10-10.md`); the moisture number, the
plant percentage and the alerts are unchanged. Measured: before the flash `COM16` showed
`[Treebeard ...]` lines, a sensor 7 packet and an upload; after it, WiFi and the BLE scanner
came up at 14:19:10, sensor 10 was received at wakes 2413, 2414 and 2415 (the probe did not
restart), snapshot uploads returned `HTTP 200` at 14:19:37, 14:20:41 and 14:21:43, and the page
served at the receiver's address contains the new card and its air-value table. The boot line
with the build tag was not captured (the log was attached five seconds after the reset).
**Not seen:** the card rendered in a browser. A page that was already open keeps the old script
until it is reloaded.

The build before it, 2026-10-06 21:53 PDT:

```text
Treebeard runtime build 2026-10-06 msv4-layout-b-v4
```

That was the fourth build flashed that evening (`msv4-conductance-scale4` at 20:02,
`msv4-history-charts` at 21:10, `msv4-die-temp-v3` at 21:19); each added to the one before.

It adds MSV4 conductance probes (flag bit7, moisture scale 4); see
`MSV4/docs/MSV4_PRODUCTION_FIRMWARE_2026-10-06.md`. Measured after the flash: WiFi and the BLE
scanner came up, sensors 1, 4, 7 and 10 were received, and a snapshot upload returned `HTTP 200`
with seven history rows and an empty queue.

`COM16` follows the USB socket, not the board: this receiver and the ESP32-S3 SWD flasher share
the same USB identifiers and carry no serial number, so either one shows up as `COM16` there.
Before flashing, confirm it is the receiver by its `[Treebeard ...]` log lines
(`python scripts/serial_capture.py --port COM16 --duration 75 --no-reset --out <file>`).

Earlier known-good build, 2026-09-02:

```text
Treebeard runtime build 2026-09-02 treebeard-rename-v1
```

The learned-percentage, periodic-freeze, and cloud-upload incident is resolved. The enforced
architecture and live evidence are in
`RECEIVER_PERCENT_RANGES_DISPLAY_FREEZE_AND_UPLOAD_FIX_2026-09-01.md`.

Notable behavior changes from the 2026-07-07 flash (see `docs/STATUS_HANDOFF.md` for the full writeup):

- Outbound cloud calls (bootstrap, config-pull, snapshot upload) now validate the server's TLS certificate against real root CAs instead of accepting anything. Verified live — snapshot uploads still return `HTTP 200`.
- `POST`/`DELETE` on `/ui`, `/cloud`, `/wifi` now only work within 15 minutes of boot (`adminUnlockUntilMs`). After that window, they return `401` until the device is power-cycled. This applies in every mode, including local-only AP mode, which previously stayed open to unauthenticated local admin requests forever.
- **Caution:** `POST /ui` does a full replace, not a merge. Posting `{}` (or any payload missing `cards`) wipes the plant card config, not a no-op. Read `GET /ui` first if testing against a live device with real configured cards.

Known-good behavior from bench testing (pre-2026-07-07, still holds):

- First-time setup can be tested with `-ForceWifiSetup` without erasing identity, plant setup, or cloud config.
- After saving home WiFi or local-only mode, resets should return to that saved mode.
- Local-only mode should keep broadcasting the open `Display-Center` network and show the dashboard.
- The save/reboot screen now clears animated plant art and stays clean while restart is pending.

## Known Good Command

From the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM16
```

The `upload` action runs the receiver cloud/display regression gate, compiles, then flashes. Do
not bypass a failed gate: it protects RTC staging, auto-calibration scheduling/cache recovery,
outage-only journal writes, TLS-first streaming, acknowledgement ordering, and the 10-16 sensor
capacity assumptions.

The helper uses Arduino IDE's bundled `arduino-cli.exe` at:

```text
C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

It uploads:

```text
DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime
```

with this board profile:

```text
esp32:esp32:esp32s3
USBMode=hwcdc
CDCOnBoot=cdc
PartitionScheme=huge_app
FlashSize=4M
PSRAM=opi
EraseFlash=none
```

`PSRAM=opi` is required for the receiver's ESP32-S3-WROOM-1U-N16R8 module. Keep `FlashSize=4M`
until the persisted LittleFS data has an explicit partition migration; changing that setting can
relocate the filesystem even though the physical module has 16 MB of flash.

## Useful Commands

List ports:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action board-list
```

Compile only:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action compile
```

Upload to a different port:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM16
```

Serial monitor:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action monitor -Port COM16
```

## First-Boot Verification

To verify the first-time setup path without erasing identity, cloud config, or plant setup:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM16 -ForceWifiSetup
```

This build ignores saved WiFi and `WiFiCredentials.h` until you save a choice from the setup app. The TFT should stay on the setup instructions and the receiver should keep broadcasting the open `Display-Center` setup network.

If your phone sees `Display-Center` but cannot join it, forget/remove the saved network on the phone and join it again. Firmware before `rename-pass2-v3` (2026-11-20) broadcast the same open setup network as `MSV2-Display`, and older firmware than that used a password for it; phones can keep trying stale saved security settings, so forget either old entry.

After you save home WiFi or local-only mode from the setup app, the receiver writes that choice to `/wifi.json`. Future resets should use that saved choice again:

1. Flash with `-ForceWifiSetup`.
2. Join `Display-Center`.
3. Open the setup app.
4. Save home WiFi, or check `Use without home WiFi` and save local-only mode.
5. Let the receiver reboot into either home WiFi or the local `Display-Center` dashboard.

The forced setup build writes `/wifi-setup-unlock-v2.flag` when you save home WiFi or local-only mode. After that, manual resets should return to the saved state instead of setup. Use `Forget WiFi` from the local dashboard when you intentionally want to clear the saved choice and return to first-time setup.

To restore normal behavior:

```powershell
powershell -ExecutionPolicy Bypass -File DisplayCenter\scripts\arduino_display.ps1 -Action upload -Port COM16
```

The underlying firmware switch lives near the top of
`DisplayCenter/arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino` and defaults to normal mode:

```cpp
#ifndef TREEBEARD_FORCE_WIFI_SETUP
#define TREEBEARD_FORCE_WIFI_SETUP 0
#endif
```

## Codex/Sandbox Note

Arduino CLI stores cores, tools, and temp extraction files under:

```text
C:\Users\samca\AppData\Local\Arduino15
```

That directory is outside the workspace sandbox. If a Codex command reports `Access is denied`, rerun the same helper command with escalated permissions instead of changing the build path. The repo helper already has the right FQBN and sketch path.

## Port Notes

Current common ports:

- `COM16`: ESP32-S3 display receiver at the 2026-09-01 deployment (the number can change after
  USB re-enumeration; use `-Action board-list` rather than assuming it permanently).
- `COM5`: XIAO/nRF moisture probe helper default.
- `COM11`: another serial device may appear depending on what is plugged in.
