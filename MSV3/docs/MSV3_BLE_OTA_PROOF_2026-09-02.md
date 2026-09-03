# MSV3 BLE OTA proof

**Status:** PASSED on the USB-recoverable bench MSV3. Reset-safe version 1 was loaded by USB, then
version 2 was transferred, validated, activated, and rebooted entirely over the computer's BLE
radio. The production sensing sketch did not gain OTA support as part of this proof.

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
