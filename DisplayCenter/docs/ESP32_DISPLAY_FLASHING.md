# ESP32 Display Flashing Notes

Use this when reflashing the wall-powered ESP32-S3 display receiver.

## Current Known-Good Status

As of 2026-10-11 19:23 PDT, the display receiver was reflashed on `COM16` with the normal
production build, at the maintainer's word ("yeah lets build this now into the receiver"):

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
- Local-only mode should keep broadcasting the open `MSV2-Display` network and show the dashboard.
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

This build ignores saved WiFi and `WiFiCredentials.h` until you save a choice from the setup app. The TFT should stay on the setup instructions and the receiver should keep broadcasting the open `MSV2-Display` setup network.

If your phone sees `MSV2-Display` but cannot join it, forget/remove the saved `MSV2-Display` network on the phone and join it again. Older firmware used a password for the same SSID, and phones can keep trying the stale saved security settings even after the receiver changes to an open setup network.

After you save home WiFi or local-only mode from the setup app, the receiver writes that choice to `/wifi.json`. Future resets should use that saved choice again:

1. Flash with `-ForceWifiSetup`.
2. Join `MSV2-Display`.
3. Open the setup app.
4. Save home WiFi, or check `Use without home WiFi` and save local-only mode.
5. Let the receiver reboot into either home WiFi or the local `MSV2-Display` dashboard.

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
