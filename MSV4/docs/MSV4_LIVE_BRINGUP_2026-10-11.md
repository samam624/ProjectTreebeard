# MSV4 bring-up image with LED signature and live stream — 2026-10-11

Dated record. Nothing here has run on hardware yet; "built" and "tested" below mean exactly that.

**Image:** `MSV4/.arduino-build/msv4-out/MSV4_Probe_Bringup_BLE-v262152-live-2026-10-11.hex`
(app version 262152, `MSV4-BUILD:bringup-live-2026-10-11`, sha256
`e876a85d8973d806c3f8a43e676f816546f88bef3a929894f006cb58b558f0c9`), plus the signed `-signed-ota.zip`.
Built with `arduino_msv4_probe.ps1 -BuildTag live-2026-10-11 -ApplicationVersion 262152 -Package -AcceptSharedMsv3DeviceType`.
Application-only hex: one segment, 0x26000-0x51488.

## What changed in the sketch

- **LED, no connection needed.** One 500 ms flash as soon as the CPU and the 32 kHz crystal are running,
  two short flashes once Bluetooth advertising is up, then an 80 ms blip every 2 s (every 0.5 s while
  connected; it flickers during `live`). The 3 and 5 short flashes of the boot guard are unchanged.
- **`live [a|b]`** streams `LIVE G=<uS> C=<pF> A=<|H|> PH=<deg>` about six times a second until `stop` or a
  disconnect. Each sample is one 1 MHz lock-in transfer H = SENSE/DRV_OUT, referenced to the transfer
  with the electrodes floating, taken once when the stream starts:
  `Yel = (1/H - 1/H_open)/Rs`, `G = Re(Yel)`, `C = Im(Yel)/w`. It is an index for watching a reaction,
  not the calibrated moisture value. Modelled with the firmware's own `specModel` (script in the
  session, not saved): air ~4 uS, 15 kohm soil 71 uS (true 67), 3 kohm 330 uS (true 333); a hand reads low
  (150 uS against 588 true) because its interface capacitance is small. The model's numbers are
  **inferred**; no board has produced a `LIVE` line yet.
- **`moisture loop`** takes the open-electrode `Cp` reference once, then the calibrated `moisture` fit back
  to back until `stop` (a few seconds each).
- `stop` also sets a flag the sweeps do not clear, so it cannot be lost between two readings.

## MSV4 Lab

New **Live response** panel at the top: Start live, live on electrode B, Stop, Zero here, and the slow
calibrated stream; big G, change-from-zero, C and calibrated Rsoil figures; a 60 s strip chart whose right
edge is the newest sample. The poll interval is 300 ms. `moisture` is now in the capture list and the quick
buttons. Parser keys added to `msv4_lab.py`: `live_g_us`, `live_c_pf`, `rsoil25_ohm`
(`test_msv4_lab.py`, 5 tests pass).

## Flashing over SWD in one hold

`scripts/swd_flash_hex.py` has a new `--app <hex>` option (with `--msv3` or `--bootloader`): the app is
written after the bootloader image inside the same contact hold. It refuses a hex with any segment outside
0x26000-0xF4000, or with nothing at 0x26000, before it touches the target.

```powershell
python scripts\swd_flash_hex.py --port COMx --msv3 1.0.3 --app MSV4\.arduino-build\msv4-out\MSV4_Probe_Bringup_BLE-v262152-live-2026-10-11.hex
```

- Expect about twice the hold time of a bootloader-only flash (estimate; the bootloader-only flash is "well
  under a minute" per `DisplayCenter/docs/SWD_FLASHER.md`).
- The bootloader accepts a debugger-written application when its settings page is erased and the vector table
  is plausible (`bootloader_app_is_valid()`, read in the pinned source). That is a source reading, not a
  hardware result.
- An SWD-written app does **not** seed the anti-rollback floor (`docs/KNOWN_ISSUES.md`). A board that goes
  into service still needs a signed OTA install of its production image.
