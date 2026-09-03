# Project rename: MSV2 -> Treebeard (2026-09-02)

Cosmetic/internal rename only. **No wire format, database, or persisted identifier changed.**

## "MSV2" means three different things — do not blanket-replace it

| category | examples | action |
|---|---|---|
| Project branding | doc prose, `[MSV2 web]` log prefixes, build tag, UI strings | renamed |
| Hardware generation | `MSV2/` dir, `MSV2_Moisture_*.ino`, "the MSV2 probe" | **kept** |
| Wire / DB / persisted | see keep-list below | **kept** |

MSV1/MSV2/MSV3 are real probe generations (see `CLAUDE.md`). Renaming generation references is
wrong, and renaming anything in the keep-list breaks the deployed fleet or orphans stored data.

## Keep-list — DO NOT rename these

- `'M','S','V','2'` BLE manufacturer-data magic bytes, and `MSV2_COMPANY_ID`.
  Changing either means the receiver stops hearing every probe until all are reflashed in lockstep.
- `snprintf(receiverId, ..., "MSV2%012llX", mac)` — **the receiver's cloud identity.** Change this
  prefix and the receiver becomes a new device to Supabase: all history, calibration epochs, and
  the user's share/claim link are orphaned. This is the single most destructive edit in the repo.
- BLE device names `MSV2-MOISTURE-001`, `MSV2-AFE-TEST`; setup SSID `MSV2-Display`.
- Supabase RPCs `msv2_read_share_*`, `msv2_reset_sensor_calibration`,
  `msv2_read_sensor_auto_calibration`; table `msv2_receivers`.
- Browser `localStorage` keys `msv2.*`; service-worker cache key string `"msv2-cloud-app-v15"`
  (the surrounding JS const was renamed to `TREEBEARD_CACHE`; only the *value* is load-bearing).
- `MSV2_MOISTURE_SCALE_VERSION`, `MSV2_RAW_PACKET_MIN_LEN` / `_WIDE_LEN`, `MSV2_MAGIC` — these
  describe the MSV2 *generation's* wire format and are referenced by MSV3 code for compatibility.
- `MSV2_RECEIVER_TOKEN` in `cloud/docs/` — documents a real former Supabase env var. Renaming it
  would falsify what a user may actually have set.
- `DisplayCenter/hardware/MSV2_ESP32_DisplayCenter/` — the **Altium** project. Bound to an external
  CAD path (`C:/Users/Public/Documents/Altium/...`) and embedded in STEP assembly references.

## What was renamed

- 6 sketch dirs + their `.ino` files: `MSV2_ESP32_Display_Runtime` -> `Treebeard_ESP32_Display_Runtime`,
  etc. (via `git mv`, so history follows), plus `TFT_eSPI_User_Setup_Treebeard_ESP32S3.h`.
- 25 project-scoped identifiers -> `TREEBEARD_*` (`ENABLE_WIFI`, `BUILD_TAG`, `SAFE_BOOT*`,
  `FORCE_WIFI_SETUP`, `CACHE`, the PWA asset blobs, ...), consistently in code *and* docs.
- Prose: "MSV2 runtime / receiver / cloud / Display" -> Treebeard. "MSV2 probe / sensing / design /
  method / raw-kHz" deliberately left alone.

## Build tags

The prefix changed with this rename, so it was given its **own** date and suffix
(`Treebeard runtime build 2026-09-02 treebeard-rename-v1`) rather than reusing the previous one.
Every historical record at or before 2026-08-31 legitimately reads `MSV2 runtime build ...`
because that is what those binaries actually reported — 24 such strings in dated incident reports
were briefly rewritten by the mechanical pass and then **restored**. Do not "fix" them.

## Regression trap found during this work

The first pass renamed `MSV2_FORCE_WIFI_SETUP` in the firmware but **not** in
`arduino_display.ps1`, which passes it as `-DMSV2_FORCE_WIFI_SETUP=1`. The rename regex used
`\bMSV2_...`, and in `-DMSV2_` the `D` and `M` are both word characters, so there is no word
boundary and the match silently failed. The force-wifi-setup build would have compiled cleanly and
done nothing.

If you ever repeat a rename here, sweep for `[A-Za-z0-9_]MSV2_...` as well as `\bMSV2_...`, and
verify defines actually reach the compiler by grepping the generated `compile_commands.json`.

## Verification

Data safety was proven at **diff level**, not by occurrence counts (equal counts can hide a
delete-plus-insert). Across the entire diff exactly one line touched any keep-list identifier: the
`MSV2_CACHE` -> `TREEBEARD_CACHE` const rename, with its cache-key string byte-identical.

Also confirmed: no NVS/Preferences use anywhere (all persistence is LittleFS); none of the 15
LittleFS paths contains "MSV2", so none needed changing; 8 firmware `#ifdef` guards all
`TREEBEARD_*` with the single matching `-D`; `EraseFlash=none` and the FQBN unchanged;
`WiFiCredentials.h` still gitignored after the directory move.

**Note for whoever flashes this:** `LittleFS.begin(true)` has format-on-mount-failure enabled
(pre-existing). Let the history outbox drain to zero before flashing, or a transient mount failure
takes `/history-outbox-v1.bin` with it.
