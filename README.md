# MSV2

This repo has two BLE probe sketches and an ESP32 display runtime.

For the path from prototype to family/friend-ready product, see [MSV2 Product Roadmap](docs/PRODUCT_ROADMAP.md). The first remote-monitoring API sketch lives in [MSV2 Cloud API Draft](docs/CLOUD_API_DRAFT.md).

Supabase hosted-backend setup notes live in [MSV2 Supabase Setup](docs/SUPABASE_SETUP.md).

The installable phone web app is a static Netlify app in:

```text
web/plant-dashboard/
```

Supabase still handles receiver bootstrap, snapshot storage, and the dashboard APIs.

## Fast ESP32 Display Update Loop

Flash `arduino/MSV2_ESP32_Display_Runtime/MSV2_ESP32_Display_Runtime.ino` once to the ESP32 devkit. The sketch renders probe readings on an SPI TFT through `GFX Library for Arduino`, scans MSV2 BLE advertisements, and hosts a tiny WiFi updater for live UI JSON pushes.

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
| CS | GPIO 14 | Chip select |
| DC | GPIO 15 | Data/command |
| RST | GPIO 16 | Reset |
| VCC | 3.3V, or 5V if supported | Use what your module supports |
| GND | GND | Ground |
| LED / BL | 3.3V or 5V | Backlight is always on with this wiring |

The display driver and GPIOs are configured directly in the ESP32 display sketch. After changing the driver or pins, reflash the ESP32 once.

For developer convenience, you may still put WiFi credentials in a local file:

```text
arduino/MSV2_ESP32_Display_Runtime/WiFiCredentials.h
```

Use [WiFiCredentials.example.h](arduino/MSV2_ESP32_Display_Runtime/WiFiCredentials.example.h) as the template:

```cpp
#define WIFI_SSID "Your WiFi SSID"
#define WIFI_PASSWORD "Your WiFi Password"
```

`WiFiCredentials.h` is ignored by git. It is now only a fallback/default credential source. For friend/family setup, flash generic firmware and configure WiFi from the phone app instead.

On boot, the ESP32 first tries WiFi credentials saved in LittleFS by the phone app, then falls back to `WiFiCredentials.h` if present. If it cannot connect within 15 seconds, it starts its own setup access point:

- SSID: `MSV2-Display`
- Password: `moisture`
- Board URL: `http://192.168.4.1`

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
- `GET http://192.168.4.1/snapshot` returns the future cloud snapshot payload without uploading it.
- `GET http://192.168.4.1/device` returns receiver identity, firmware build, network mode, IP, and endpoint paths.
- `GET http://192.168.4.1/claim` returns the receiver ID and local claim code for future app/cloud pairing.
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
- `Setup`: configure receiver WiFi, view the receiver pairing code, and edit plant names, probe IDs, pixel-art choices, and dry/wet moisture calibration values.
- `Diagnostics`: probe RSSI, wake counter, age, received packets, missed packets, and packet-loss percentage.

The Setup tab includes moisture presets for common plant categories:

- `Herbs`
- `Tropical`
- `Succulent`
- `Fern`
- `Dry houseplant`

Saving from the phone writes the same `ui.json` used by the display, so changes appear on the TFT immediately. This is the foundation for a later cloud-backed/native app; for now it keeps setup local, fast, and friend-friendly without app store or account setup.

The ESP32 also serves a web app manifest, service worker, and icon. After opening the app on your phone, use the browser's add-to-home-screen/install action if your browser offers it. Some browsers treat local `http://` device pages as shortcuts rather than full PWAs; the later cloud app will solve that with HTTPS. True remote monitoring still requires the cloud relay described in the product roadmap.

### Phone WiFi Setup

After flashing generic receiver firmware:

1. Power on the receiver.
2. If it is not already on home WiFi, connect your phone to the `MSV2-Display` WiFi network.
3. Open `http://192.168.4.1/app`.
4. Go to `Setup > Receiver WiFi`.
5. Enter the home WiFi name/password.
6. Tap `Save & Reboot`.
7. Reconnect your phone to home WiFi and open the new receiver IP shown on the display/Serial Monitor.

The app writes credentials to `LittleFS` at `/wifi.json` through the `/wifi` endpoint. Use `Forget WiFi` to clear saved credentials and reboot back into setup mode.

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

### Optional Cloud Settings

The receiver can store future cloud relay settings in `LittleFS` at `/cloud.json`, managed from `Setup > Cloud Sync` or the `/cloud` endpoint.

Stored fields:

- `enabled`: whether cloud sync should upload receiver snapshots.
- `uploadUrl`: future HTTPS snapshot upload URL.
- `syncIntervalSeconds`: future upload interval, clamped from 60 seconds to 24 hours.
- `apiToken`: stored privately on the receiver; the app only shows whether one exists.

When `enabled` is true, the receiver uploads the `/snapshot` payload to `uploadUrl` on the configured interval while connected to station WiFi. The `/cloud` response and phone app show last attempt time, last success time, last HTTP status, and last error. Leave cloud sync disabled for local-only use.

The snapshot also includes the display's plant setup summary (`config.title` and `config.cards[]`) so a cloud app can show plant names, art keys, and moisture calibration without separately reaching back into the local receiver.

For backend prototyping on your PC, run the local mock cloud:

```powershell
python scripts\mock_cloud_server.py --host 0.0.0.0 --port 8088
```

It implements the draft claim and snapshot endpoints from [MSV2 Cloud API Draft](docs/CLOUD_API_DRAFT.md), stores latest snapshots in memory, and exposes a tiny auto-refreshing dashboard at:

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
python scripts\relay_snapshot_once.py --receiver http://BOARD_IP_FROM_SERIAL --cloud http://127.0.0.1:8088
```

To test the receiver-side uploader itself, set the Cloud Sync upload URL in the phone app to the mock server's receiver endpoint:

```text
http://PC_LAN_IP:8088/api/v1/receivers/RECEIVER_ID_FROM_APP/snapshot
```

Then enable Cloud Sync and save. The app's Cloud Sync panel and `GET /cloud` show the last upload attempt, success, HTTP status, and error.

## Probe Power Strategy

The preferred low-power strategy is a hybrid sensor-check/send schedule:

- Wake briefly every few minutes to power/read the moisture sensor.
- Only advertise over BLE when the moisture value changes meaningfully, or when a normal heartbeat interval expires.
- Use a slower heartbeat such as 30 minutes for unchanged plants.
- After a watering event or large moisture jump, temporarily send faster so the display/app updates quickly.

Example target behavior:

```text
Every 3-5 minutes:
  wake
  read moisture
  if moisture changed by a threshold OR 30 minutes since last BLE send:
    advertise for about 500 ms
  sleep
```

This keeps the XIAO/nRF radio off most of the time while still making watering events appear quickly. The ESP32 display hub remains wall-powered and scans continuously, so the battery/solar burden stays on the tiny probe side where it matters.

The tradeoff is firmware complexity: the probe needs to remember its last sent moisture value and last BLE-send time across normal low-power sleep. It does not require remote wake, because BLE cannot wake a truly sleeping nRF radio.

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
- `cards[].moisture.dry` and `cards[].moisture.wet`: calibration points used to convert raw moisture into a health bar.

Each card shows the plant name, an animated pixel-art plant, a moisture health bar, and a battery health bar. Battery appears when the probe advertisement includes the optional battery percent byte; older probe firmware still works but shows `--%`. The included sprite choices can be swapped from JSON without reflashing, but adding new bitmap assets still requires regenerating `plant_sprites.h` and flashing the display firmware once.

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

- `arduino/MSV2_ESP32_Display_Runtime/plant_sprites.h`
- `arduino/MSV2_ESP32_Display_Runtime/PlantSpriteCatalog.h`
- `arduino/MSV2_ESP32_Display_Runtime/MobileAppPage.h` art picker options and previews
- `generated/plant_sprites/previews/*.png`
- `generated/plant_sprites/mobile_art_catalog_snippet.js`

The firmware automatically uses `PlantSpriteCatalog.h` for display-side sprite lookup. The generator also updates the phone app art picker in `MobileAppPage.h`, so future GIFs added to `assets/plant_gifs/` appear in the mobile dropdown after regenerating and reflashing the ESP32.

If you ever want to generate the preview snippet without touching the embedded phone app:

```powershell
python scripts\generate_plant_sprites.py --skip-mobile-app
```

If Pillow is missing:

```powershell
python -m pip install -r requirements.txt
```

## Crash Isolation

Near the top of the ESP32 display sketch is the bring-up stage:

```cpp
#define MSV2_BRINGUP_STAGE 3
```

Use `0` for serial-only safe boot, `1` for display-only, `2` for display plus WiFi updater, and `3` for the full display/WiFi/BLE dashboard runtime.

If the board reboots, open Serial Monitor at `115200` and look for:

```text
[MSV2 boot] previous checkpoint before reset:
[MSV2 boot] reset reason:
```

Those two lines identify the last subsystem touched before the panic.

## Flashing Without The IDE

The Arduino IDE bundles `arduino-cli`, and this repo includes PowerShell wrappers that use that bundled executable.

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

Compile or upload the XIAO probe firmware:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_probe.ps1 compile
powershell -ExecutionPolicy Bypass -File scripts\arduino_probe.ps1 upload
```

The display helper defaults to `COM13`; the probe helper defaults to `COM5`. Override with `-Port COMx` if Windows changes the ports:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM13
```

You can still call `arduino-cli` directly if you prefer.

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

Compile:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32s3 arduino/MSV2_ESP32_Display_Runtime
```

Upload, replacing `COM7` with your port:

```powershell
arduino-cli upload -p COM7 --fqbn esp32:esp32:esp32s3 arduino/MSV2_ESP32_Display_Runtime
```

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

For low-power probe tuning, change these in `arduino/MSV2_Moisture_BLE_Sleep/MSV2_Moisture_BLE_Sleep.ino`, flash both XIAOs, and rerun the same test:

```cpp
const uint32_t WAKE_PERIOD_MS = 3000;
const uint32_t ADVERTISE_MS = 500;
```

`WAKE_PERIOD_MS` is the full wake-to-wake period; `ADVERTISE_MS` is only the BLE advertising window inside that period. Suggested `ADVERTISE_MS` sweep: `750`, `500`, `300`, `200`, `150`, `100`, then stop when `missedTotal` starts increasing. Keep the ESP32 display hub plugged in and scanning continuously during the test.
