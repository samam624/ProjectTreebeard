# DisplayCenter

The shared ESP32-S3 receiver/display product. Continuously scans for BLE advertisements from
**both** MSV2 and MSV3 probes, renders readings on a local TFT, hosts a phone-facing setup/status
web app, and bootstraps itself with the cloud backend (`../cloud/`) for remote history.

## Fast ESP32 Display Update Loop

Flash `arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino` once to the ESP32 devkit. The sketch renders probe readings on an SPI TFT through `GFX Library for Arduino`, scans MSV2/MSV3 BLE advertisements, and hosts a tiny WiFi updater for live UI JSON pushes.

Install Arduino libraries:

- ESP32 board package
- `GFX Library for Arduino`
- `ArduinoJson` by Benoit Blanchon, not the similarly named `Arduino_JSON`

Your display wiring:

| Display Pin | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| SCK / CLK | GPIO 12 | SPI clock |
| MOSI / SDA | GPIO 13 | SPI data to display |
| MISO | Not connected | Normal display writes do not need it |
| DC | GPIO 14 | Data/command |
| RST | GPIO 15 | Reset |
| CS | GPIO 16 | Chip select |
| VCC | 3.3V, or 5V if supported | Use what your module supports |
| GND | GND | Ground |
| LED / BL | 3.3V or 5V | Backlight is always on with this wiring |

The display driver and GPIOs are configured directly in the ESP32 display sketch. After changing the driver or pins, reflash the ESP32 once.

For developer convenience, you may still put WiFi credentials in a local file:

```text
arduino/Treebeard_ESP32_Display_Runtime/WiFiCredentials.h
```

Use [WiFiCredentials.example.h](arduino/Treebeard_ESP32_Display_Runtime/WiFiCredentials.example.h) as the template:

```cpp
#define WIFI_SSID "Your WiFi SSID"
#define WIFI_PASSWORD "Your WiFi Password"
```

`WiFiCredentials.h` is ignored by git. It is now only a fallback/default credential source. For friend/family setup, flash generic firmware and configure WiFi from the phone app instead.

On boot, the ESP32 first checks whether local-only mode was saved from the phone app. If local-only mode is off, it tries WiFi credentials saved in LittleFS by the phone app, then falls back to `WiFiCredentials.h` if present. While it is trying home WiFi, the TFT says which network it is joining. If it cannot connect within 15 seconds, it switches to first-time setup mode and the TFT shows plain setup instructions:

- SSID: `MSV2-Display`
- Password: none; it is an open setup network
- Board URL: `http://192.168.4.1`

Phones that support captive portals should automatically open the setup app after joining `MSV2-Display`. If the phone only says "No Internet" and does not open a page, open Safari/Chrome and type `192.168.4.1`.

The first-time setup page also has a `Use without home WiFi` checkbox. If that is saved, the receiver remembers local-only mode, keeps broadcasting `MSV2-Display`, and shows the plant dashboard without joining a home network. To join home WiFi later, connect your phone to `MSV2-Display`, open `http://192.168.4.1/app`, uncheck local-only mode, enter the home WiFi name/password, and tap `Save & Reboot`.

If the receiver has saved home WiFi credentials and cannot reconnect on boot, it reboots and retries the saved network twice before falling back to setup mode. This helps with slow routers and brief home-network outages without trapping users forever.

Edit [ui/display_default.json](ui/display_default.json), then push without reflashing:

```powershell
python scripts/push_display_ui.py --url http://BOARD_IP_FROM_SERIAL/ui --file ui/display_default.json
```

For a live edit loop:

```powershell
python scripts/push_display_ui.py --url http://BOARD_IP_FROM_SERIAL/ui --file ui/display_default.json --watch
```

Useful endpoints:

- `GET http://192.168.4.1/` opens the phone-friendly plant dashboard and setup UI.
- `GET http://192.168.4.1/app` opens the same app UI.
- `GET http://192.168.4.1/ui` returns the current UI JSON.
- `POST http://192.168.4.1/ui` replaces the UI JSON and redraws the display.
- `GET http://192.168.4.1/data` returns the latest probe readings.
- `GET http://192.168.4.1/snapshot` returns the current cloud snapshot payload without uploading it.
- `GET http://192.168.4.1/device` returns receiver identity, firmware build, network mode, IP, and endpoint paths.
- `GET http://192.168.4.1/claim` returns the receiver ID and local claim code used for app/cloud pairing.
- `GET http://192.168.4.1/config-meta` returns the active UI config schema/version/source metadata.
- `GET/POST/DELETE http://192.168.4.1/cloud` reads, saves, or resets optional cloud settings and upload status.
- `GET http://192.168.4.1/manifest.webmanifest`, `/sw.js`, and `/app-icon.svg` support the installable web-app shell.

This removes the slow flash cycle for display iteration. You only reflash when changing the runtime itself: BLE parsing, display driver setup, networking behavior, or the renderer's capabilities.

## Phone App V1

The first phone interface is hosted directly by the ESP32 display hub. Open the board URL printed in Serial Monitor from your phone while it is on the same WiFi network:

```text
http://BOARD_IP_FROM_SERIAL/
```

The app has three tabs:

- `Status`: live plant cards using `/data` readings and the active `/ui` plant config.
- `Setup`: configure receiver WiFi, view the receiver pairing code, edit plant names/probe IDs/pixel art, and choose adaptive or manual moisture calibration per plant.
- `Diagnostics`: probe RSSI, wake counter, age, received packets, missed packets, and packet-loss percentage.

The Setup tab includes moisture presets for common plant categories:

- `Herbs`
- `Tropical`
- `Succulent`
- `Fern`
- `Dry houseplant`

Saving from the phone writes the same `ui.json` used by the display, so changes appear on the TFT immediately. This is the foundation for a later cloud-backed/native app; for now it keeps setup local, fast, and friend-friendly without app store or account setup.

### Adaptive moisture calibration and alerts

With `cards[].moisture.auto` enabled, the receiver obtains a continuously learned range
from the cloud instead of treating the configured `dry`/`wet` values as permanent. The
backend gives equal time weight to five-minute history buckets, validates waterings using
their pre-water, post-water, and retained dry-down readings, and combines all supported
watering cycles from the trailing 21 days. New events contribute provisionally until
later readings confirm that the rise persisted in soil.

Moisture alert semantics are intentionally different from battery semantics:

- `11-100%`: normal/accent color.
- `1-10%`: steady yellow warning.
- `0%`: red, flashing, and eligible for the receiver's watering buzzer.

Battery keeps its own existing warning thresholds. See
[Treebeard Adaptive Moisture Calibration](../MSV2/docs/TREEBEARD_ADAPTIVE_MOISTURE_CALIBRATION.md) for the
model, deployment, diagnostics, and reset behavior.

The ESP32 also serves a web app manifest, service worker, and icon. After opening the app on your phone, use the browser's add-to-home-screen/install action if your browser offers it. Some browsers treat local `http://` device pages as shortcuts rather than full PWAs; the later cloud app will solve that with HTTPS. True remote monitoring still requires the cloud relay described in the product roadmap.

### Phone WiFi Setup

After flashing generic receiver firmware:

1. Power on the receiver.
2. If it is not already on home WiFi, connect your phone to the open `MSV2-Display` WiFi network. No setup-network password is needed.
3. Wait for the setup page to appear. If it does not, open `http://192.168.4.1/app`.
4. Go to `Setup > Receiver WiFi` if the page did not open there already.
5. Either enter the home WiFi name/password, or check `Use without home WiFi` to keep the receiver local-only.
6. Tap `Save & Reboot` or `Use Local Only`.
7. If you entered home WiFi, reconnect your phone to home WiFi. If you chose local-only, stay on `MSV2-Display` when you want to edit the receiver.
8. The receiver screen should leave setup mode and show the plant dashboard.

The app writes WiFi/local-only settings to `LittleFS` at `/wifi.json` through the `/wifi` endpoint. Use `Forget WiFi` to clear saved settings and reboot back into setup mode.

### Receiver Pairing

The receiver now creates a persistent identity on first boot and stores it in `LittleFS` at `/device.json`.

- `receiverId` identifies the physical display hub.
- `claimCode` is shown in the phone app under `Setup > Receiver Pairing`.
- `receiverSecret` stays on the receiver and is not exposed by the phone app.

For now, the claim code is only local metadata. The next cloud/app step is to let a signed-in user enter or scan that code to attach the receiver to their household account.

### Config Versioning

The display UI JSON now carries a `_meta` object:

```json
"_meta": {
  "schema": 1,
  "version": 1,
  "updatedMs": 0,
  "updatedBy": "repo-default"
}
```

Every time the receiver accepts a new `/ui` update, it increments `_meta.version`, stamps `updatedMs`, and sets `updatedBy` to `receiver-local`. The phone app shows this under `Setup` as `Config v...`. This is the local foundation for later cloud sync, where the app/cloud can compare versions before overwriting a plant setup.

### Automatic Cloud Status

After the receiver joins home WiFi, current firmware bootstraps itself with the hosted Supabase backend using its stored `receiverId` and private `receiverSecret`. The backend returns a per-device snapshot upload URL/token, and the receiver stores those private values in `LittleFS` at `/cloud.json`.

The ESP32 phone app now shows `Setup > Cloud Status` as a read-only status/debug panel. It reports automatic bootstrap, snapshot upload, and cloud config-pull status from `GET /cloud`; it no longer asks users to paste upload URLs or API tokens during normal setup.

The snapshot also includes the display's plant setup summary (`config.title` and `config.cards[]`) so a cloud app can show plant names, art keys, and moisture calibration without separately reaching back into the local receiver.

For backend prototyping on your PC, run the local mock cloud (lives in `cloud/`, the shared backend, not here):

```powershell
python ..\cloud\scripts\mock_cloud_server.py --host 0.0.0.0 --port 8088
```

It implements the draft claim and snapshot endpoints from [Treebeard Cloud API Draft](../cloud/docs/CLOUD_API_DRAFT.md), stores latest snapshots in memory, and exposes a tiny auto-refreshing dashboard at:

```text
http://localhost:8088/
```

The mock dashboard also serves `/manifest.webmanifest`, `/sw.js`, `/app-icon.svg`, and iOS web-app metadata so it behaves like the shape of the future installable PWA. On another phone/computer on the same WiFi, open:

```text
http://PC_LAN_IP:8088/app
```

Local `http://` pages may install as simple home-screen shortcuts; a real hosted HTTPS app will provide the more complete PWA install behavior.

On iPhone, use Safari's share menu and choose `Add to Home Screen`; Chrome on iPhone generally does not expose the same PWA install flow.

The raw snapshot JSON is still available at:

```text
http://localhost:8088/api/v1/snapshots
```

Relay one receiver snapshot into the mock cloud from your PC:

```powershell
python ..\cloud\scripts\relay_snapshot_once.py --receiver http://BOARD_IP_FROM_SERIAL --cloud http://127.0.0.1:8088
```

To test the receiver-side uploader itself against the local mock server, use `POST /cloud` directly with the mock receiver endpoint:

```text
http://PC_LAN_IP:8088/api/v1/receivers/RECEIVER_ID_FROM_APP/snapshot
```

The app's Cloud Status panel and `GET /cloud` show the last upload attempt, success, HTTP status, and error. For normal hosted use, prefer the automatic Supabase bootstrap path instead of manual `/cloud` edits.

## Dashboard UI Schema

The ESP32 display sketch is now a small dashboard runtime. Flash it once, then iterate on the dashboard by pushing JSON. The current renderer supports a landscape plant-card grid:

```json
{
  "title": "MSV2 Garden",
  "layout": {
    "type": "grid",
    "orientation": "landscape",
    "columns": 4,
    "rows": 2,
    "gap": 6,
    "padding": 6
  },
  "cards": [
    {
      "sensor": 1,
      "plant": "Basil",
      "art": "sprout_pot",
      "moisture": {
        "dry": 350,
        "wet": 800
      }
    }
  ]
}
```

Live-editable JSON values:

- `title`: dashboard heading.
- `theme.background`, `theme.surface`, `theme.text`, `theme.muted`, `theme.accent`, `theme.warning`, `theme.danger`: card and bar colors.
- `layout.orientation`: `landscape` or `portrait`; the firmware chooses a matching TFT rotation.
- `layout.columns`, `layout.rows`, `layout.gap`, `layout.padding`: grid shape and spacing.
- `cards[]`: plant cards shown left-to-right, top-to-bottom.
- `cards[].sensor`: BLE sensor ID, currently `1` through `16`.
- `cards[].plant`: card title.
- `cards[].art`: one of `sprout_pot`, `cup_plant`, `wide_planter`, or `cactus_cluster`. The older names `sprout`, `leafy`, and `tall` are still accepted as aliases.
- `cards[].moisture.auto`: use cloud-learned moisture bounds when enough supported history exists.
- `cards[].moisture.dry` and `cards[].moisture.wet`: calibration points used to convert raw moisture into a health bar.

Each card shows the plant name, a compact `Batt: 100%`-style reading at the top right, an animated pixel-art plant, and a `Moisture` health bar. Older probe firmware without battery data shows `Batt: --%`. The included sprite choices can be swapped from JSON without reflashing, but adding new bitmap assets still requires regenerating `plant_sprites.h` and flashing the display firmware once.

The ESP32 local setup app automatically recalculates `layout.columns` and `layout.rows` when plants are added or deleted. Current defaults are 6 plants as `3x2`, 7 plants as `4x2` with the bottom row centered, and 8 plants as `4x2`.

## Plant Art Pipeline

Original plant GIFs live in:

```text
assets/plant_gifs/
```

Add new downloaded GIFs there with simple filenames. The filename becomes the JSON/app art key:

```text
assets/plant_gifs/fern.gif         -> "art": "fern"
assets/plant_gifs/monstera.gif     -> "art": "monstera"
assets/plant_gifs/snake_plant.gif  -> "art": "snake_plant"
```

Regenerate the ESP32 sprite assets:

```powershell
python scripts\generate_plant_sprites.py
```

The generator writes:

- `arduino/Treebeard_ESP32_Display_Runtime/plant_sprites.h`
- `arduino/Treebeard_ESP32_Display_Runtime/PlantSpriteCatalog.h`
- `arduino/Treebeard_ESP32_Display_Runtime/MobileAppPage.h` art picker options and previews
- `generated/plant_sprites/previews/*.png`
- `generated/plant_sprites/mobile_art_catalog_snippet.js`

The firmware automatically uses `PlantSpriteCatalog.h` for display-side sprite lookup. The generator also updates the phone app art picker in `MobileAppPage.h`, so future GIFs added to `assets/plant_gifs/` appear in the mobile dropdown after regenerating and reflashing the ESP32.

Animation frames keep the source GIF's shared logical-canvas coordinates and per-frame durations. The generator preserves every unique pose by default, applies one union crop to every pose instead of trimming and re-centering poses independently, and ignores tiny detached specks that would otherwise control the crop. Use `--max-poses N` only when intentionally trading animation detail for flash space. Run the sprite geometry regression tests with:

```powershell
python -m unittest discover -s scripts -p "test_generate_plant_sprites.py" -v
```

If you ever want to generate the preview snippet without touching the embedded phone app:

```powershell
python scripts\generate_plant_sprites.py --skip-mobile-app
```

If Pillow is missing:

```powershell
python -m pip install -r ../requirements.txt
```

## Crash Isolation

Near the top of the ESP32 display sketch is the bring-up stage:

```cpp
#define TREEBEARD_BRINGUP_STAGE 3
```

Use `0` for serial-only safe boot, `1` for display-only, `2` for display plus WiFi updater, and `3` for the full display/WiFi/BLE dashboard runtime.

If the board reboots, open Serial Monitor at `115200` and look for:

```text
[Treebeard boot] previous checkpoint before reset:
[Treebeard boot] reset reason:
```

Those two lines identify the last subsystem touched before the panic.

## Flashing Without The IDE

The Arduino IDE bundles `arduino-cli`, and this repo includes PowerShell wrappers that use that bundled executable.

Codex/project-specific flashing notes live in [ESP32 Display Flashing Notes](docs/ESP32_DISPLAY_FLASHING.md). The display receiver usually flashes on `COM13`.

Compile or upload the ESP32 display runtime:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 compile
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload
```

The display helper uses the ESP32-S3 `huge_app` partition scheme so there is room for the compiled pixel-art sprites.

Open the ESP32 serial monitor:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 monitor
```

Compile or upload the MSV2 XIAO probe firmware (lives in `../MSV2/`, not here):

```powershell
powershell -ExecutionPolicy Bypass -File ..\MSV2\scripts\arduino_probe.ps1 compile
powershell -ExecutionPolicy Bypass -File ..\MSV2\scripts\arduino_probe.ps1 upload
```

Compile or upload the custom MSV3 E73 probe firmware only through its guarded helper (lives in `../MSV3/`):

```powershell
powershell -ExecutionPolicy Bypass -File ..\MSV3\scripts\arduino_msv3_probe.ps1 -Action doctor
powershell -ExecutionPolicy Bypass -File ..\MSV3\scripts\arduino_msv3_probe.ps1 -Action compile
powershell -ExecutionPolicy Bypass -File ..\MSV3\scripts\arduino_msv3_probe.ps1 -Action upload -Port COMx
```

MSV3 has no 32.768 kHz crystal, while the stock PCA10056 Arduino variant assumes one. The helper
reapplies/verifies the required LFRC selection after a clone or Arduino core upgrade and forces a
clean build. The sketch refuses to compile with the unsafe clock selection. Portable toolchain
and hardware caveats are indexed in [Known Issues](../docs/KNOWN_ISSUES.md).

The display helper defaults to `COM13`; the MSV2 probe helper defaults to `COM5`. Override with `-Port COMx` if Windows changes the ports:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13
```

Install the ESP32 core and libraries once:

```powershell
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "GFX Library for Arduino" ArduinoJson
```

Find the board port:

```powershell
arduino-cli board list
```

Use `scripts\arduino_display.ps1` for compilation and upload rather than a bare
`arduino-cli compile --fqbn esp32:esp32:esp32s3` command. The wrapper carries the receiver's
required N16R8 octal-PSRAM and partition settings, preserves LittleFS, separates diagnostic build
artifacts, and runs the DisplayCenter regression gate before a binary can be flashed.

## BLE Packet-Loss Testing

The ESP32 `/data` endpoint reports cumulative packet stats for each probe:

- `received`: new wake packets received since ESP32 boot.
- `missed`: missed wake count since the previous received packet.
- `missedTotal`: cumulative missed wake count.

Run a live packet-loss test from your PC:

```powershell
python scripts\esp32_packet_loss_test.py --url http://BOARD_IP_FROM_SERIAL --sensors 1 2 --duration 300
```

Optional CSV capture:

```powershell
python scripts\esp32_packet_loss_test.py --url http://BOARD_IP_FROM_SERIAL --sensors 1 2 --duration 900 --csv packet_test.csv
```

For low-power probe tuning, change these in `../MSV2/arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino`, flash both XIAOs, and rerun the same test:

```cpp
const uint32_t WAKE_PERIOD_MS = 3000;
const uint32_t ADVERTISE_MS = 500;
```

`WAKE_PERIOD_MS` is the full wake-to-wake period; `ADVERTISE_MS` is only the BLE advertising window inside that period. Suggested `ADVERTISE_MS` sweep: `750`, `500`, `300`, `200`, `150`, `100`, then stop when `missedTotal` starts increasing. Keep the ESP32 display hub plugged in and scanning continuously during the test.
