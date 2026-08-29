# ESP32 Display Flashing Notes

Use this when reflashing the wall-powered ESP32-S3 display receiver.

## Current Known-Good Status

As of 2026-07-07, the display receiver was reflashed on `COM13` with the normal (non-forced-setup) build, as part of a security hardening pass. The build tag string is unchanged from the source (this pass didn't touch `MSV2_BUILD_TAG`):

```text
MSV2 runtime build 2026-07-05 reboot-screen-fix
```

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
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13
```

The `upload` action compiles first, then flashes. If an older version of the helper does not compile first, run `compile` once before `upload`.

The helper uses Arduino IDE's bundled `arduino-cli.exe` at:

```text
C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

It uploads:

```text
arduino/MSV2_ESP32_Display_Runtime
```

with this board profile:

```text
esp32:esp32:esp32s3
USBMode=hwcdc
CDCOnBoot=cdc
PartitionScheme=huge_app
FlashSize=4M
PSRAM=disabled
EraseFlash=none
```

## Useful Commands

List ports:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 board-list
```

Compile only:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 compile
```

Upload to a different port:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13
```

Serial monitor:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 monitor -Port COM13
```

## First-Boot Verification

To verify the first-time setup path without erasing identity, cloud config, or plant setup:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13 -ForceWifiSetup
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
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13
```

The underlying firmware switch lives near the top of
`arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino` and defaults to normal mode:

```cpp
#ifndef MSV2_FORCE_WIFI_SETUP
#define MSV2_FORCE_WIFI_SETUP 0
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

- `COM13`: ESP32-S3 display receiver.
- `COM5`: XIAO/nRF moisture probe helper default.
- `COM11`: another serial device may appear depending on what is plugged in.
