# MSV3 BLE OTA proof

**Status:** PASSED on the USB-recoverable bench MSV3. Reset-safe version 1 was loaded by USB, then
version 2 was transferred, validated, activated, and rebooted entirely over the computer's BLE
radio. The production sensing sketch did not gain OTA support as part of this proof.

> **2026-09-03 update: the production sensing sketch now HAS OTA, and it has been proven on
> hardware.** See "Production sketch integration" at the end of this document. The adoption
> sequence below is unchanged and still gates field use — in particular, **do not conformal-coat a
> probe yet**: the single-bank bootloader is still the blocking item, and OTA cannot replace a
> bootloader.

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
