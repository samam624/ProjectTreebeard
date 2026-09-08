# MSV3 BLE OTA proof

**Status:** PASSED on the USB-recoverable bench MSV3. Reset-safe version 1 was loaded by USB, then
version 2 was transferred, validated, activated, and rebooted entirely over the computer's BLE
radio. The production sensing sketch did not gain OTA support as part of this proof.

> **2026-09-03 update: the production sensing sketch now HAS OTA, and it has been proven on
> hardware.** See "Production sketch integration" at the end of this document.
>
> **2026-09-04 update: adoption item 6 is half done.** Six of six *host-crash* interruption cases
> pass on the bench board — see "Interrupted-transfer campaign" at the end. **Still do not
> conformal-coat a probe**, but the reason is now specific rather than blanket: RF-loss and
> power-loss interruption are untested, and power loss is the only class that can interrupt a
> flash write. Both need SWD attached first.
>
> **2026-09-05 update: the mutated-image negative tests are done.** They were the last thing
> keeping this document's integrity story at "the host-side window is narrowed" rather than a
> statement about the device. Seven mutations were offered to probe 99, each one's rejection stage
> and status byte predicted from the bootloader source before the run; all seven predictions held.
> Full record, including the scope of what it does and does not establish:
> [`MSV3_TAMPERED_IMAGE_REJECTION_2026-09-05.md`](MSV3_TAMPERED_IMAGE_REJECTION_2026-09-05.md).
> One finding from it belongs here: **the application is erased at Start DFU, before the init
> packet is received**, so no device-side rejection is free — even a signature failure costs the
> running firmware. The host-side preflight is therefore not redundant with the bootloader's check;
> on a coated probe it is the only thing protecting availability.

The checked-in test sketch is now version 3 at +8 dBm after the later fixed-position RSSI A/B test;
version 3 was loaded over USB and does not change the version-1-to-version-2 BLE proof below.

## What "entirely over BLE" means

USB was physically connected during this first test for bench power and emergency recovery. After
version 1 had been loaded, USB carried no version-2 firmware data and performed no upload. The
computer discovered the running application through its Bluetooth adapter, requested the
buttonless transition, rediscovered the BLE bootloader, sent every version-2 byte over BLE, and
requested activation. Therefore the same application update is physically possible on a
battery-powered probe with no USB connection.

## Bench test

- The probe advertises continuously as `MSV3-OTA-TEST`.
- `OTA_TEST_VERSION` controls the on-board status LED flashes per group. The proven BLE transfer
  was version 1 (one flash) to version 2 (two flashes); the current source is version 3 (three
  flashes) for the subsequent +8 dBm RF test.
- Build/package version 2 only through the LFRC-enforcing wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 `
  -Action compile -Sketch arduino/MSV3_BLE_OTA_Test
```

- Send `MSV3/.arduino-build/msv3-probe/MSV3_BLE_OTA_Test.ino.zip` with a Nordic legacy-DFU
  compatible phone client. In Bluefruit Connect: connect to `MSV3-OTA-TEST`, choose Updates,
  choose Use Custom Firmware, and select the ZIP. Keep the phone close to the probe.
- Success is automatic reboot followed by two LED flashes per group and the return of
  `MSV3-OTA-TEST` advertising. USB must not be used for uploading during the transfer.
- USB recovery, if needed:

```powershell
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 `
  -Action upload -Sketch arduino/MSV3_BLE_OTA_Test -Port COM9
```

The ZIP is an application-only Nordic DFU package for device type `0x0052` and S140 6.1.1
firmware ID `0x00B6`.

## Live result

The computer test used portable Python 3.11.9, Bleak 1.1.1, and
[`recrof/nrf_dfu_py`](https://github.com/recrof/nrf_dfu_py) commit
`409e4b75d55c8d75859022217dfba0db33730f16e`. Those temporary dependencies are under the ignored
`MSV3/.arduino-build/` directory; no system Python package was installed.

```powershell
MSV3\.arduino-build\python-portable\runtime\python.exe `
  MSV3\.arduino-build\nrf_dfu_py\dfu_cli.py `
  --scan --prn 4 --delay 0.6 --retry 1 --verbose `
  MSV3\.arduino-build\msv3-probe\MSV3_BLE_OTA_Test.ino.zip `
  MSV3-OTA-TEST
```

Observed sequence:

1. Found application `MSV3-OTA-TEST` at `DB:AD:4D:9D:21:BA`.
2. Reset-safe buttonless request succeeded; the OTA bootloader appeared at
   `DB:AD:4D:9D:21:BB`.
3. Bootloader accepted application mode, 126,948-byte size, init packet, and PRN=4.
4. All 126,948 bytes transferred; Receive Firmware and Validate both returned success.
5. Activate & Reset completed and `MSV3-OTA-TEST` reappeared at its original address.

The transfer itself took about 3 minutes 40 seconds at the deliberately conservative 20-byte
ATT payload and PRN=4. A later controlled test can tune throughput after interrupted-transfer
recovery is proven.

## Bootloader 0.11.0 guard

Do not use the Arduino core's stock `BLEDfu` transition on a field unit with bootloader 0.11.0.
[Adafruit bootloader issue 399](https://github.com/adafruit/Adafruit_nRF52_Bootloader/issues/399)
reports that its soft jump can retain nRF52840 ACL protection, stall at the first DFU flash write,
and leave a single-bank device without a valid application. This test supplies the same Nordic
buttonless GATT service but enters OTA using the bootloader's documented `GPREGRET=0xA8` plus a
full system reset.

## Production path

The probe does not need to scan or otherwise know that the receiver exists. Its normal manufacturer
advertisement can remain connectable. When an update is pending, the DisplayCenter recognizes the
sensor ID in that advertisement and initiates the connection during an explicit maintenance
window. It then requests the reset-safe DFU transition, reconnects to the bootloader, and transfers
the package.

Before field deployment, this still needs:

1. A bootloader built and bench-tested for signed images, rollback-safe dual-bank operation, and
   BLE recovery when the application is absent (`DEFAULT_TO_OTA_DFU`).
2. Battery/VSTOR gating so erase/write never starts near undervoltage.
3. A signed manifest containing hardware target, firmware version, size, and SHA-256; reject
   cross-model and downgrade images.
4. The DisplayCenter legacy-DFU central/client and interrupted-transfer tests.
5. A sparse authenticated maintenance schedule so normal probe power remains advertise-only.

## Planned V3 adoption sequence

Do not OTA-update inaccessible probes with today's production image or bootloader yet. The live
test proves the radio and application-update mechanism, but the current single-bank bootloader can
still lose the application if power or radio fails after it invalidates bank 0. The reset-safe
service fixes the observed bootloader-0.11.0 soft-jump/ACL failure; it does not by itself provide
power-loss rollback.

1. Finish and validate the V3 battery/VSTOR power path first.
2. Add `ResetSafeBLEDfu` to the real production sketch while preserving its existing manufacturer
   payload. Advertise the DFU service during an explicit maintenance window; the probe still never
   needs to scan for or poll the receiver.
3. Give every probe an unambiguous update identity (sensor ID plus BLE address) and update one at a
   time. Refuse packages for a different hardware generation or an older firmware version.
4. Build/flash the production-safe bootloader once through USB or SWD before probes become
   inaccessible. Require signed packages, rollback-safe dual-bank storage, and BLE DFU as the
   recovery mode when no valid application exists.
5. Measure a complete update's energy use on the real battery and storage capacitors. Gate entry on
   a proven battery/VSTOR margin above the hardware disconnect threshold, and keep sensing plus
   scheduled load-disconnect logic suspended until activation finishes.
6. Deliberately interrupt BLE and power at early, middle, validation, and activation stages. Every
   case must either boot the old application or return to BLE recovery without USB/SWD.
7. Only after those tests pass, use the computer updater for manual V3 maintenance. Later, port the
   same central-side sequence to DisplayCenter for unattended fleet updates.

The first battery-only trial should remain physically accessible with USB/SWD recovery nearby. A
successful update while USB happens to be absent is not enough; interrupted updates are the
acceptance test that makes future no-USB maintenance safe.

## Production sketch integration — 2026-09-03

Step 2 of the adoption sequence above is done and proven on hardware. `MSV3_Moisture_BLE_Sleep.ino`
now registers `ResetSafeBLEDfu` and can be updated entirely over BLE.

### What was added

- `ResetSafeBLEDfu.{h,cpp}` copied into the production sketch folder. Arduino only compiles files
  inside a sketch folder, so this is a duplicate of the copies in `MSV3_BLE_OTA_Test/`. **If either
  copy changes, change both.**
- `bledfu.begin()` in `bleInit()`. The service is registered **in GATT only** and deliberately not
  added to the advertising payload: `startAdv()`'s `static_assert` shows Flags(3) + TxPower(3) +
  mfg header(2) + mfg(23) = exactly the 31-byte legacy PDU limit, leaving no room for a service
  UUID. An updater finds the probe by `DEVICE_NAME` in the scan response. **The manufacturer
  payload is byte-for-byte unchanged**, verified live: `mfg[0:8]=4d 53 56 32 63 02 00 00`
  (`MSV2` magic, sensor id, wake count) with `mfg_len=21` = the 23-byte payload minus the 2-byte
  company ID the scanner strips.
- An OTA connection watch plus a hold (`OTA_ADV_LISTEN_MS`, `OTA_CONNECTED_HOLD_MS`) in `loop()`.

### The hold is the part that actually makes it work

Registering the DFU service alone is decorative. At the original 500 ms advertising window the
probe stopped advertising and slept before any central could finish connecting.

**Three consecutive OTA attempts failed identically** with the client reporting `Could not get GATT
services: Unreachable` about 8 s after connecting. The probe's own serial log showed why: no OTA
hold was ever entered and the cycle ran its normal 9.96 s. The first implementation checked
`Bluefruit.connected()` exactly **once**, at the end of the 500 ms window, and the client's
connection completes at roughly 0.6 s — just past that one-shot check. Polling across the whole
window instead fixed it.

Successful run, from the probe's serial log:

```
14:38:23.013  advertising window opens (wake=2)
14:38:29.642  COM12 dropped        <- DFU write landed, chip reset into the bootloader
   ... 3m31s of BLE transfer; no USB CDC because the application is not running ...
14:42:00.945  COM12 reconnects
14:42:01.364  [DIAG] MSV3 production probe booting
```

The **6.6 s** between the advertising window opening and the reset is the proof the hold worked: in
the failed attempts that same span was the full 9.96 s cycle with the probe ignoring the client.
Transfer took 3 m 31 s, comparable to this document's original 3 m 40 s.

Verified by BLE scan afterwards: the same board (`DB:AD:4D:9D:21:BA`) came back advertising the v2
name, having been sent only over the radio.

Note there is **no `[DIAG] OTA hold ended` line on a successful update** — `resetSafeDfuWrite()`
system-resets before execution reaches that print. Its absence is expected on success, not a fault.

### Test setup

Client rebuilt to match this document's original: `recrof/nrf_dfu_py` at commit `409e4b7` and
**bleak pinned to 1.1.1** (pip resolves to 3.x by default, a major-version jump away from what was
proven). Installed into an isolated venv at `MSV3/.arduino-build/dfu-venv` rather than the original
portable Python — same guarantee that no system package is touched. All of it is under the ignored
`.arduino-build/`, so it must be rebuilt on a fresh clone.

The default 5 s scan is too short to find a probe advertising this rarely — **`--wait` is required**
(it retries every 2 s until found; it took five cycles). The bench probe used a temporary
non-production identity (`SENSOR_ID = 99`, `MSV3-MOISTURE-099/098`) so a DisplayCenter in range
could not ingest open-air bench readings as sensor 5's real plant data — `BLE_MAGIC` is the real
production magic. That identity was reverted before commit.

### Open, and deliberately not resolved

- **The OTA-capable advertising schedule is not power-validated.** The first integration used the
  20 ms advertising interval for the entire 5 s window, roughly 250 scheduled events/minute. The
  post-integration audit below keeps the full 5 s opportunity but uses 20 ms for the first second
  and 250 ms for the remaining four, roughly 66 events (about 74% fewer). That is a scheduling
  reduction, not a measured whole-board current result; a real battery/solar budget is still due.
- **Interrupted-transfer testing has not been done** (item 6 of the adoption sequence). Only clean,
  USB-powered, close-range updates have been proven. This is the acceptance test for no-USB
  maintenance and it is still outstanding.
- **The bootloader is unchanged** — still single-bank 0.11.0 with no `DEFAULT_TO_OTA_DFU`. A failed
  transfer after bank 0 is invalidated still loses the application. This remains the reason not to
  conformal-coat.

## Post-integration OTA audit — 2026-09-03

Compile-verified in both the production and proof sketches; hardware re-test is still required.

- Advertising now uses Bluefruit's native fast-to-slow mode: 20 ms for 1 s, then 250 ms for the
  rest of the unchanged 5 s discovery window. The SoftDevice also owns a 5 s hard timeout, so an
  application-side stall cannot accidentally leave advertising enabled indefinitely.
- A Bluefruit connection callback latches the connection handle, and the code stops advertising
  before its final connection check. This closes the boundary race where a connection could land
  between the last 20 ms poll and `Advertising.stop()` and then remain active unnoticed.
- A peer that connects but never writes the DFU command is explicitly disconnected after the 30 s
  service-discovery hold. Previously it remained connected indefinitely, kept radio connection
  events running through later sleep cycles, and could prevent later advertisements.
- Manufacturer payload, scan-response-name, and advertising-start failures now return failure.
  The cadence is advanced only after the SoftDevice accepts the advertisement, so a failed send is
  retried on the next 10 s wake rather than being silently treated as delivered for a full minute.
- DFU-service registration is checked. Normal sensor telemetry retains a 500 ms window if service
  setup failed, rather than paying five seconds for an OTA path that is unavailable.
- The reset-safe service now checks the authorized-write reply and verifies `GPREGRET=0xA8` before
  resetting. If the reset call unexpectedly returns, it clears that retained request so a later
  watchdog or pin reset cannot enter OTA spuriously. Both sketch-local copies remain identical.

Required bench checks before accepting this revision: verify normal packets during both the fast
and slow portions, repeat a complete application-to-bootloader OTA handoff, connect without issuing
DFU and confirm forced disconnect/recovery after 30 s, and measure average current across at least
one complete 60 s no-client cycle. These changes do not solve the single-bank/interrupted-transfer
or unsigned-image blockers above.

## Wrong-target hardening and fleet verification — 2026-09-04

Probe 4 was re-verified and the OTA path was hardened against sending the wrong image to the wrong
probe. No firmware was flashed to any fleet board; every change below is compile- or test-verified
on the host.

### Probe 4 is healthy

Two scans, 11:02 and 11:15 PDT, 150 s and 200 s (`scripts/msv3_fleet_scan.py`):

| | |
|---|---|
| Address / name | `F2:16:32:1B:2B:47`, `MSV3-MOISTURE-004` — both match the registry |
| Sensor ID on air | 4, and **no other address claimed ID 4** across either scan |
| Wake counter | 573 → 584, advancing during the scan — cycling, not stuck |
| Battery | 3955 → 3954 mV, not charging |
| Flags | `0x1e` then `0x1c` — bit4 set in both, so OTA-capable with the power gate open |
| Die temperature | 22.25 / 22.75 °C |
| `DATA_CH0` | ~19.34 M connected, ~22.48 M bare reference |
| Signal | −67 to −72 dBm, 29 packets |

The phase bit alternating between the two scans (`0x1e` → `0x1c`) is the electrode-swap working as
designed, not noise.

**Uptime arithmetic — a trap worth recording.** `wakeCount` is named for a wake but increments
inside `loop()`'s `if (shouldAdvertise)` block, so it counts *advertisements*, about one per 60 s
`ADVERTISE_INTERVAL_MS`, not one per 10 s `WAKE_PERIOD_MS` check. Multiplying by 10 s understates
uptime roughly sixfold and briefly made both probes look as though they had reset that morning. At
the real cadence, wake 584 is about 9.7 hours — continuous operation since the 2026-09-03 night
OTA, with no reset. Strictly the field counts *advertising windows*, and the hours figure assumes
the 60 s interval held throughout; 29 received packets against roughly 11 counter increments is the
same distinction seen from the other side. The misleading name is kept (it is the receiver-visible
wire field) with a comment in the sketch saying what it actually counts.

### Probe 5 has never been migrated

Same scans: `MSV3-MOISTURE-005` at `F9:18:9B:CA:10:B2`, **flags `0x0d`/`0x0c` — bit4 clear at
4419 mV**, far above the 3800 mV gate, while charging. By this document's own established
signature that is firmware with no OTA feature rather than a shut gate. Only probe 4 was migrated
on 2026-09-03. Probe 5 still needs the USB SoftDevice+bootloader step before BLE can reach it at
all; `MSV3/fleet.json` records it as `ota_capable: false` and the flasher refuses it until that
changes.

### The defect that was actually fixed

`SENSOR_ID` and `DEVICE_NAME` were two separately hand-edited constants, and targeting was by raw
BLE address with nothing connecting the two. The hazard was live, not theoretical: on 2026-09-04
**both** stale packages sitting in the build directories contained `MSV3-MOISTURE-004` while the
committed source said 005 — an ordinary leftover from the previous night's reverted local edit.
Sending one to probe 5 would have given two probes the same sensor ID and silently mixed two
plants' history in the receiver, with no error anywhere and no way to see it over the radio.

Three changes, in increasing order of what they rule out:

1. **The name is now derived from the ID.** `makeDeviceName()` builds `MSV3-MOISTURE-NNN` from
   `SENSOR_ID` at compile time, so the two can no longer disagree; a half-finished
   re-identification is now unrepresentable rather than merely discouraged. `static_assert`s pin
   the contract — negative-tested by corrupting the digit arithmetic and confirming
   `arm-none-eabi-g++` refuses. The literal string still lands in `.rodata`, verified by extracting
   `MSV3-MOISTURE-005` from the freshly built application image, which is what makes step 2
   possible.
2. **`MSV3/fleet.json`** binds each sensor ID to its application address, its recovery address
   (application + 1), the bootloader installed on it, and the device type that bootloader accepts.
   This is the part that makes the check real: a board already sitting in its bootloader advertises
   no manufacturer payload, so the registry is the only thing that can say which probe an address
   belongs to. An operator-supplied `--expect-identity` flag was considered and rejected — it would
   only prove someone typed the same string twice.
3. **`scripts/ble_ota_update.py --probe N`** resolves through the registry and refuses, *before
   touching the radio*: a package whose embedded identity is not the target's; an unregistered
   address; a device type the target's bootloader would reject; a signature that does not verify
   against the public key compiled into that bootloader release; the unset `0xFFFFFFFF` application
   version the raw Arduino wrapper emits; a package containing anything but an application; and,
   when the application is observed on air, a sensor ID, name, or power gate that disagrees.

Identity is read from the **application image**, not the ZIP manifest, which is unsigned metadata.
Signature verification uses the **public** key from `release-manifest.json`, so it proves the image
will actually verify on the board and needs no access to the signing secret.

Four further holes were found in review and closed:

- **An old package's name proves nothing.** Reading `MSV3-MOISTURE-004` out of an image only shows
  what that firmware *advertises*. Before the derivation landed, the name and `SENSOR_ID` were
  separate constants, so a pre-2026-09-04 signed image could legitimately carry the name 004 while
  putting sensor ID 5 on the wire — precisely the mismatch the check exists to catch, and invisible
  to it. `IDENTITY_CONTRACT_MIN_APP_VERSION = 65538` separates the two eras, and anything below it
  needs `--allow-legacy-package`. **The shipped `msv3-1.0.1-signed-ota.zip` (moved to `MSV3/bootloader/quarantine/` on 2026-09-06 — pre-guard-1, identity 005) is version 65537 and is
  therefore now treated as legacy** — correctly, since it predates the invariant.
  `build_msv3_release.ps1`'s default application version is bumped to 65538 so the next release
  carries the contract without anyone remembering to.

  A marker string compiled into the image would read more plainly than a version number, but the
  Arduino core builds with `-fdata-sections -Wl,--gc-sections`, so an unreferenced constant is not
  reliably retained, and one kept alive by a serial print would vanish with `ENABLE_SERIAL_LOGS`.
  The version travels in the signed init packet, which no linker can drop.

  **Be precise about what the threshold proves.** It is an authenticated *release-policy*
  boundary, not cryptographic evidence that a binary was compiled from invariant-bearing source.
  It stops a historical package being reused by accident, which is the failure that actually
  happened. It does not stop a deliberate relabel:
  anyone holding the signing key can build and sign anything.

  > **Corrected 2026-09-06.** This paragraph used to name `-SkipApplicationCompile` as that
  > unsafe path, because it packaged whatever hex was already sitting in
  > `.arduino-build/msv3-probe/` and stamped it with whatever version it was given. The switch
  > now builds the bootloader only and emits no application package at all — a same-sensor
  > stale image would have passed both the identity and the version check while being the
  > wrong code, so the workflow was removed rather than guarded.

  **On a deployed probe the flag alone is not enough.** `--allow-legacy-package` would otherwise
  restore the exact defect: an old image whose name says 004 while its compiled `SENSOR_ID` is 5
  passes the name check, and the live check describes the application currently *running*, not the
  one about to be sent. So a deployed target requires the specific image's application SHA-256 in
  that probe's `legacy_package_sha256` — a reviewable registry edit rather than a keystroke. The
  allowlist is **empty for every probe**, deliberately: no deployment record establishes which
  exact pre-65538 image is on probe 4 (the one it received on 2026-09-03 was a locally hand-edited
  004 build that was never committed), so the honest answer is to rebuild rather than to
  retroactively bless an image nobody can identify.

- **The override was fleet-wide.** `--allow-identity-mismatch` is now refused whenever the target
  is `deployed: true`. It exists for the bench board's uncommitted identity; letting it apply to a
  field probe would leave the main safety property defeatable by one flag. The legitimate fix for a
  deployed board whose registry entry is wrong is to correct the registry, which leaves an
  auditable change where a command-line flag does not.

- **An application could have been mistaken for an orphaned bootloader.** The predicate is now a
  named, tested function (`looks_like_bootloader`). It holds because the production sketch
  registers the DFU service in GATT but deliberately keeps it out of the advertising payload —
  `startAdv()`'s `static_assert` shows Flags(3) + TxPower(3) + mfg(2+23) fills the 31-byte legacy
  PDU exactly — so a DFU UUID on air means the bootloader, and `MSV3-MOISTURE-NNN` contains no
  "dfu" to collide with `AdaDFU`. This matters most under `--skip-jump`.

- **The trust root hung on one editable file.** Verifying against `release-manifest.json` only
  proves the package matches whatever key that file currently names, and it is an ordinary tracked
  JSON document a regenerated release would overwrite. Each registry entry now pins
  `bootloader_key_sha256` (SHA-256 of the 64-byte `Qx||Qy` point,
  `0fc63419…d3976daa`, shared by 1.0.0 and 1.0.1). A manifest that disagrees is refused with no
  override, and an OTA-capable probe with no pinned fingerprint is refused rather than trusted.

`--allow-identity-mismatch` relaxes only the identity comparisons, and only on a non-deployed
board. Signature, device type, image integrity, the registry's address list, the legacy-package
gate, and the power gate are all still enforced under it — each of those scopings is tested.

Ordering is the property that matters, not the existence of the checks. A jump takes a live sensor
off the air for the length of a transfer, and on a coated probe there is no cable to undo it with.
Two tests cover it. `test_refusal_happens_before_any_radio_call` drives the real `run()` with the
BLE entry points replaced by tripwires and asserts none is reached, for five file-level refusal
causes. `test_live_failures_trip_before_the_jump` covers the four causes that can only be seen on
air — wrong sensor ID, wrong name, a shut power gate, pre-OTA firmware — letting the scan happen
and asserting the jump never does.

### Also fixed

- **`ResetSafeBLEDfu.{h,cpp}` drift is now blocked, not requested.** The two sketch-local copies
  were kept in step by a comment. `arduino_msv3_probe.ps1` now hashes both and refuses to build if
  they differ — negative-tested by appending a line to one copy and confirming the build stops.
  That file is the write-authorize path where a SUCCESS reply must set `update=1`, which cost a day
  of hardware debugging to find once; silent drift would resurrect it in whichever sketch was not
  edited.
- **`scripts/msv3_fleet_scan.py`** decodes the 23-byte payload against the registry, so fleet
  health is a command rather than hand-decoded hex. It flags duplicate sensor IDs, unregistered
  addresses, name mismatches, pre-OTA firmware, and (with `--require-progress`) a stalled wake
  counter.
- **Two documentation errors corrected.** The handoff doc claimed `-GenerateSigningKey` would
  overwrite an existing signing key; it cannot — `build_msv3_release.ps1` only reaches the
  generator when the file is absent, and the generator opens with mode `"xb"` and refuses on
  `FileExistsError`. And the `0x3356` wording that doc listed as outstanding had already been
  fixed in `MSV3/bootloader/README.md`.

### Test evidence

    python scripts/test_receipt_guard.py     12/12 pass
    python scripts/test_ota_preflight.py     96/96 pass
    arduino_msv3_probe.ps1 -Action compile   131,336 bytes, LFCLK guard OK, duplicate check OK
    arm-none-eabi-g++ negative test          static_assert fires on corrupted digit arithmetic

Real CLI runs against the shipped releases, all exit 2 with the radio untouched:

| Command | Refusal |
|---|---|
| `--probe 5` with the 1.0.1 signed release | `probe 5 is registered ota_capable=false (adafruit-stock-0.11.0)` |
| `--probe 4` with the 1.0.1 signed release | version 65537 `predates the compile-time guarantee` |
| `--probe 4` with that release **and `--allow-legacy-package`** | `deployed=true, so --allow-legacy-package is not sufficient on its own`, and it prints the sha256 to allowlist if records ever justify it |
| `--probe 4` with `msv3-1.0.1-initial-install-from-stock.zip` | `contains more than an application` — a SoftDevice+bootloader package must never go over BLE |
| `--app-address AA:BB:CC:DD:EE:FF` | not in the registry, and it lists the three that are |

### The positive path, proven without touching a probe

Rejections alone prove nothing — a preflight that refused everything would pass every test above.
`--preflight-only` runs every check and stops at the radio boundary, so a release can be shown
deliverable without taking a sensor off the air.

A real signed release was built for probe 4 and run through it: `SENSOR_ID` temporarily set to 4,
compiled through the wrapper, packaged with `adafruit-nrfutil dfu genpkg --dev-type 0x3356
--application-version 65538` and the real fleet signing key, then:

```
Package: msv3-happy-65538.zip: MSV3-MOISTURE-004 (sensor 4), device_type=0x3356,
         app_version=65538, 131344 bytes, signed
Target:  probe 4 (MSV3-MOISTURE-004) app=F2:16:32:1B:2B:47 recovery=F2:16:32:1B:2B:48
         bootloader=msv3-1.0.1 device_type=0x3356
  integrity    OK -- signature verifies against the key compiled into msv3-1.0.1, device_type 0x3356
  identity     OK -- MSV3-MOISTURE-004 matches probe 4

Preflight passed. Stopping before the radio (--preflight-only).      exit 0
```

That single run exercises the derived name (`SENSOR_ID = 4` alone produced `MSV3-MOISTURE-004` in
the image), the registry-pinned public key, the device type, the version gate, and the identity
match. The same package aimed at `--probe 5` is refused. `SENSOR_ID` was reverted to the committed
fleet value and the tree rebuilt afterwards; nothing was flashed.

### Still outstanding, unchanged

- **Interrupted-transfer testing (adoption sequence item 6).** Nothing here touches it. It remains
  the acceptance test and the reason not to conformal-coat. That bench campaign will also be the
  **first hardware exercise of the revised client** — `looks_like_bootloader()` and the reworked
  branch structure have 96 host-side tests behind them but have not yet driven a real transfer.
  The 2026-09-03 OTA proves the code as it was that night, not as it is now.
- **The OTA advertising schedule is still not power-validated.**
- **Probe 5's USB migration** has not been done, and is not something OTA can do.
- **An offline backup of the signing key.** The code cannot destroy it, but a disk can.

## Interrupted-transfer campaign — 2026-09-04, adoption item 6

**Host-process interruption: six of six cases pass on the bench board**, one of which is a
post-completion control that tests nothing about interruption. Every interrupted transfer left the
probe either running a valid application or in authenticated BLE DFU recovery, and none was
unreachable *in any tested host-loss case*. That is not the same as "never becomes unreachable",
which stays unproven until RF loss and power loss are tested. The campaign also found one real
client defect, which is fixed and verified.

Note also that the hardware runs used serial-enabled bench images. The production configuration is
compile-verified only; it was not itself flashed in this campaign.

Harness: `scripts/ota_interrupt_test.py`. It runs the **unmodified** `ble_ota_update.py` as a
subprocess and kills it at a trigger point, so what is exercised is exactly what an operator runs,
with no test hook compiled into the production tool.

### The acceptance rule had to be reworded before it could be met

The adoption sequence said every interrupted case must "either boot the old application or return
to BLE recovery". For a **single-bank** bootloader the first half is unachievable once a transfer
has begun: the incoming image overwrites the running application in place, so by 10% there is no
old application left. Holding the code to that rule would have meant failing it for doing exactly
what single-bank does. The rule asserted instead is:

> after any interruption the board must expose **either** a bootable application **or**
> authenticated BLE DFU recovery, and must never become unreachable without SWD.

Each case declares which of the two outcomes are legitimate for how far the transfer got, rather
than sharing one blanket expectation.

### Results

| case | trigger | outcome | verdict |
|---|---|---|---|
| baseline | none | application, 32 s, 4.05 kB/s | PASS |
| activate-race | kill as `Activating & Resetting` is sent | application — device finished activation on its own | PASS |
| pre-validate | kill at `Verifying Upload` | recovery, `AdaDFU` | PASS |
| transfer-50 | kill at 50% | recovery, `AdaDFU` | PASS |
| transfer-10 | kill at 10% | recovery, `AdaDFU` | PASS |
| post-complete | kill after `DFU Complete` | application | PASS (control) |

Each case was followed by a clean recovery transfer, and every recovery succeeded.

### What this campaign does NOT cover

Three interruption classes exist and only one is tested here. Killing the host is a faithful **host
crash** — Windows tears the connection down and the bootloader sees a disconnect. It is not:

- **RF disappearance**, where the board instead waits out the BLE supervision timeout. Different
  code path, untested.
- **Power loss**, the only class that can interrupt a flash erase or program in progress. The MCU
  stays powered through a host crash, so this harness structurally cannot produce it.

Power-loss cases were deliberately not attempted: **SWD is not currently attached** (only the
MSV3 board's own CDC on COM9 was present), and power loss is precisely the class that could damage
bootloader metadata. They should be run with SWD ready, after RF loss.

`post-complete` deserves its own caveat. It was the first attempt at an activation test and it
tests nothing: `Transfer completed` is printed *after* `DFU Complete`, so the device had already
activated and reset. It is kept, relabelled, as a control. `activate-race` is the real one, and
even it is a race rather than a controlled interruption — a host crash cannot halt the device's
synchronous activation at a chosen internal point.

### The defect it found

Reproduced 2 for 2. **After any interrupted transfer, the bootloader keeps the dead session's
state and answers the next Start DFU with `INVALID_STATE` (status 2).** Upstream `dfu_lib` responds
by writing `OP_CODE_RESET` and raising — so the board *is* recovering — but the client then spent
its remaining retries during the reboot, against a board that was not there, and exited with a raw
WinRT `TimeoutError` traceback. A plain manual retry ~90 s later worked both times.

On a coated probe with no cable, that traceback is the difference between "wait and run it again"
and "the probe is bricked".

Fixed in `ble_ota_update.py`. To be precise about who does what: **`dfu_lib` sends the reset**,
and this wrapper waits for the recovery — it does not issue a reset of its own, because a second
reset arriving mid-recovery would be a new race for no measured benefit. If that upstream
behaviour ever changes, this loop degrades to a timeout rather than to anything unsafe. The pinned
`nrf_dfu_py` checkout is commit `409e4b7`.

A hook records every control-point `(op, status)` so a raised transfer can be diagnosed instead of
dumped; a Start-DFU `INVALID_STATE` now prints what happened, waits for the bootloader to
re-advertise, and restarts the transfer, bounded by
`--stale-session-retries` (default 3, 0 to opt out). The branch is deliberately narrow — only
Start DFU, only `INVALID_STATE`, only the two addresses the registry pins for that probe, and never
through a `KeyboardInterrupt`. It is **not** a protocol-level resume: nothing continues from an
offset, the whole image is re-sent.

Verified on the actual stuck board left by the transfer-10 interruption:

```
found bootloader at DB:AD:4D:9D:21:BA
<< RX Error: Command 0x1 failed with status 2
The bootloader refused Start DFU with INVALID_STATE: it is still holding the state of an
earlier interrupted transfer.
  dfu_lib has already sent it a reset, so it is rebooting. Waiting for it to come back...
found bootloader after its reset at DB:AD:4D:9D:21:BB
... DFU Complete. Transfer completed in 1m 29s.
```

### The recovery address is not stable, and neither is "which image is running"

Two things that would each have produced a wrong conclusion:

- **`...BB` is not the recovery oracle.** After the pre-validate interruption the bootloader came
  up as `AdaDFU` on `...BA` — the *application's* address, carried across by the peer-data handoff.
  Later, after it self-reset, it moved to `...BB`. Same board, same session, both addresses. The
  harness and the flasher both watch the pinned pair and disambiguate by DFU service UUID.
- **The bootloader stores no application version.** `dfu_init.c`'s version check is an
  unimplemented upstream template, so a package's `application_version` is never persisted and
  cannot be read back — and there is therefore **no rollback protection at all**. Device type *is*
  enforced (`dfu_init.c:216`, against a compile-time `0x3356`), as are the SoftDevice FWID and the
  P-256 signature. So `IDENTITY_CONTRACT_MIN_APP_VERSION` is a host-side release policy, not
  something the board will refuse.

That second point meant two builds for the same probe were indistinguishable. `FIRMWARE_BUILD_ID`
now exists as a literal in the image, printed in the boot banner, and read out of a package by
`msv3_fleet.read_package()`.

**And it immediately caught its own bug.** Two packages differing *only* in that string came out
byte-for-byte identical. `ENABLE_SERIAL_LOGS` is `false` in production, so the boot banner — the
only reference to the string — was compiled out, and `-fdata-sections -Wl,--gc-sections` discarded
it. Fixed with a `volatile` pointer written in `setup()`, outside any serial guard; verified the
string now survives a production build. This is the same linker-GC hazard that made the version
number the right choice for the identity-contract gate.

The bench images for this campaign additionally set `ENABLE_SERIAL_LOGS = true` so the banner is
readable. That is the one way they differ from production.

### Open, and deliberately so

- **RF-loss and power-loss interruption remain untested**, and power loss should wait for SWD.
  Until both are done, "safe under interrupted OTA" is proven for host crashes only, and that is
  the honest limit of what gates coating.
- ~~A coated production probe cannot report which firmware it runs.~~ **Done 2026-09-04.** The
  application publishes `FIRMWARE_BUILD_ID` as the Device Information Service firmware revision
  (`0x2A26`). It costs **zero advertising bytes** — the 31-byte PDU is already exactly full — because
  it is read over GATT after a central connects, which `ble_ota_update.py` already does to write the
  DFU jump. Cost: 944 bytes of flash, 124 of RAM.

  Verified end to end on hardware in the **production** configuration (`ENABLE_SERIAL_LOGS = false`,
  so no cable could have helped): the build was delivered to the bench board entirely over BLE, then
  read back over BLE as manufacturer `Treebeard`, model `MSV3`, firmware revision
  `MSV3-BUILD:2026-09-04-dis1`.

  The flasher now reads it while already connected for the jump and **refuses to take a live sensor
  off the air to install the build it is already running** unless `--reflash-same-build` is passed.
  A probe too old to publish a DIS reports `None`, which is treated as "unknown" and never as
  "wrong" — refusing to update a board because it cannot describe itself would strand exactly the
  boards most in need of one.

  That hardware run also exposed two client bugs, both fixed: the preflight's parsed package was
  never assigned to a variable, and `jump_to_bootloader`'s broad `except Exception` reported the
  resulting `NameError` as "link dropped during write -- normal if the board reset". Programming
  errors now propagate instead of being disguised as a healthy reset.

- **Dual-bank was re-examined on 2026-09-04 and is not fundamentally impossible.** See
  [MSV3_DUAL_BANK_ROOT_CAUSE_2026-09-04.md](MSV3_DUAL_BANK_ROOT_CAUSE_2026-09-04.md): the failure is
  a completion-gate race in the activation state machine, not a chip or capacity limit. It would
  protect the old firmware through an interrupted *transfer*, but it is staging rather than
  rollback and cannot revert a new image that boots badly.
