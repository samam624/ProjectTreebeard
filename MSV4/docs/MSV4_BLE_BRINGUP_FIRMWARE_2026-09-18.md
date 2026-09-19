# MSV4 BLE bring-up firmware (2026-09-18)

MSV4 has no USB, so MSV3's USB-serial bring-up console has no equivalent. The first application
flashed onto an MSV4 board is `MSV4/arduino/MSV4_Probe_Bringup_BLE`: a line console over the
Nordic UART Service. It does nothing on its own except advertise. You connect, type a command, and
it answers.

**Status:** it compiles, the variant, linker and package checks pass, and the signed package
verifies. **None of it has run on hardware yet.** The first-board checklist at the bottom is what
turns this into a known-good image.

## Files

| Path | What |
|---|---|
| `MSV4/arduino/variants/msv4_e73/` | Tracked board variant: `USE_LFXO`, LED on P1.10 active high, no dev-kit pin claims |
| `MSV4/arduino/MSV4_Probe_Bringup_BLE/` | The bring-up sketch |
| `MSV4/scripts/arduino_msv4_probe.ps1` | The only supported build (compile, verify, optional signed package) |
| `MSV4/scripts/msv4_ble_console.py` | PC console, interactive or scripted |

## Why a separate variant and build script

MSV4 has a 32.768 kHz crystal. MSV3 doesn't, and `MSV3/scripts/arduino_msv3_probe.ps1`
rewrites the **installed** core's `pca10056/variant.h` to `USE_LFRC` before every MSV3 build. If
MSV4 shared that file, the two generations would keep flipping it back and forth, and one of them
would always be wrong. So MSV4 never touches it:

- The variant lives in the repo and is selected with `--build-property build.variant.path=...`.
- The build script proves from the verbose log that `variant.cpp` was compiled from the tracked
  folder and that no command referenced `pca10056`.
- The sketch `#error`s unless `MSV4_VARIANT && USE_LFXO && !USE_LFRC`.
- The linker script is `MSV3/linker/msv3_nrf52840_s140_v6.ld`, used read-only. It reserves the last
  8 bytes of RAM (`0x2003FFF8`), which the bootloader also reserves. The script checks the link map.
- The variant's `initVariant()` no longer drives P0.13–P0.16 as dev-kit LEDs. On MSV4 those pins
  are `SWAP_CTRL` and `SWAP_nOE`. It drives `VBAT_DIV_EN` low from the first instruction, because
  Q1's gate has no pull resistor.

## Workflow

1. **Build**
   ```powershell
   powershell -ExecutionPolicy Bypass -File MSV4\scripts\arduino_msv4_probe.ps1 -BuildTag first -Package -AcceptSharedMsv3DeviceType
   ```
   Output goes to `MSV4/.arduino-build/msv4-out/`: the hex, `...-signed-ota.zip`, and the compile
   log. The package is signed with the MSV3 key, uses device type `0x3356`, and has application
   version `262145` (`0x40001`).

2. **Power the board with a harvester input.** A battery alone won't do it: the BQ25504 only
   connects VBAT to VSTOR once it sees a panel/`SP+` input. Use light on the panel, or a
   current-limited 2–2.5 V supply on `SP+`/`SP−`, with a charged cell on `VBAT`. Confirm `3V3`
   reads 3.3 V before touching the pogo pins.

3. **SWD: bootloader + SoftDevice only** (the contact gate is on by default):
   ```powershell
   python scripts\swd_flash_hex.py --port COMx --msv3 1.0.3
   ```
   This verifies the release hex against its manifest, waits for stable contact, mass-erases, and
   writes MBR + S140 6.1.1 + bootloader 1.0.3. No application is written over SWD, so the
   bootloader's settings page stays consistent. With no application present, the bootloader sits in
   BLE DFU.

4. **Install the app over BLE** from the bootloader. Have only this board powered, find its DFU
   address, then pass it explicitly:
   ```powershell
   MSV3\.arduino-build\python-portable\runtime\python.exe scripts\ble_dfu_recover.py `
     MSV4\.arduino-build\msv4-out\MSV4_Probe_Bringup_BLE-v262145-first-signed-ota.zip --device <address>
   ```

5. **Connect the console**
   ```powershell
   MSV3\.arduino-build\python-portable\runtime\python.exe MSV4\scripts\msv4_ble_console.py
   MSV3\.arduino-build\python-portable\runtime\python.exe MSV4\scripts\msv4_ble_console.py --cmd info --cmd vbat
   ```
   The board advertises as `MSV4-BU-XXXX`, where XXXX is the low 16 bits of its device address.
   nRF Toolbox or Bluefruit Connect's UART screen also work.

6. **Later images** go over BLE. Run `dfu enter` in the console, then repeat step 4. Buttonless DFU
   through `ResetSafeBLEDfu` is allowed by default (`dfu off` refuses it). Every new image needs a
   larger `-ApplicationVersion`, because bootloader 1.0.2+ enforces a version floor.

## Commands

| Command | Does |
|---|---|
| `info` | Build, app version, address, reset reason, boot count, LFCLK source now and at boot, HFXO, front-end state |
| `pins` | State of the seven front-end/power pins only (never the crystal or other pins) |
| `vbat` | Enables Q1, reads AIN0 with TACQ 40 µs, then disables Q1. Reports VBAT = 2 × node |
| `temp` | Pulses `TEMP_READ` and reads the NTC ratio against VDD. Uses β = 3380 K (R16 = 10k) |
| `vdd` | SAADC VDD channel |
| `drive <N> [k]` | Excitation at 16 MHz / N on P0.07 (high drive) and chopper at phase k on P0.17. HFXO is required, or the command refuses |
| `drive off`, `phase <k>` | Stop, or move the chopper phase |
| `swap a\|b\|open` | U3 electrode select, or `/OE` released so both electrodes float (the OPEN reference) |
| `chop on\|off` | U4 `/OE`: connects the averaging filters |
| `read` | `SENSE_OUT` and `DRV_OUT` in mV, oversampled (`os`) |
| `iq [N]` | 4-phase P, I, Q, A, φ on both channels |
| `sweep [N]` | All N phases (N ≤ 64) plus the least-squares residual |
| `settle <ms>`, `os <0..8>`, `cal` | Phase settle time, log2 oversample, SAADC offset calibration |
| `dfu on\|off\|enter`, `reboot`, `safe`, `stop` | `stop` cancels a sweep, even mid-run |

**Safe state** (at boot, on `safe`, and on **every** disconnect): both `/OE` lines released so the
pullups isolate the switches, then PPI/TIMER/GPIOTE off, DRV/TEMP_READ/VBAT_DIV_EN low, HFXO
released. The switches are isolated before the clocks stop.

## Front-end timing

TIMER3 at 16 MHz with `CC5 = N` and a `COMPARE5_CLEAR` short. PPI channels 8–11 go through the
`sd_ppi_*` API, since channels 17+ belong to S140. GPIOTE channels 6/7 are in toggle task mode.
An edge that falls on tick 0 is scheduled at N instead. Two edges on the same tick use separate CC
registers, so no PPI fork is needed.

| k (N=16) | CC0 | CC5 | CC1 | CC2 | DRV init | chop init | chop high |
|---:|---:|---:|---:|---:|:-:|:-:|---|
| 0 | 8 | 16 | 16 | 8 | 1 | 1 | [0, 8) |
| 4 | 8 | 16 | 4 | 12 | 1 | 0 | [4, 12) |
| 8 | 8 | 16 | 8 | 16 | 1 | 0 | [8, 16) |
| 12 | 8 | 16 | 12 | 4 | 1 | 1 | [12, 16) + [0, 4) |

The chopper signal is U4 `Sel`. Low connects the filters to the signal, so the demodulation window
is the chopper's **low** half. `iq` and `sweep` fit phase relative to that waveform. Reference-frame
questions come later; the same fit applies to both channels.

## Dead-crystal protection

A hand-soldered Y1 that doesn't start would stall FreeRTOS's RTC1 tick and SoftDevice enable. With
no USB and hand-held SWD, that board would be a brick. The sketch prevents it (full reasoning in
the sketch header):

1. Before the SoftDevice starts, it waits up to 2 s for LFCLK to be running **and** sourced from the
   crystal. If that fails, it blinks 5 times and resets into the bootloader's BLE DFU, which runs on
   LFRC. The WDT is started only after this check passes.
2. If LFCLK isn't still on the crystal after SoftDevice enable, it also resets into DFU.
3. As a backstop, a boot counter sits in the reserved RAM record. Three boots in a row that never
   reach advertising send the fourth one to DFU (3 blinks).

This relies on three things verified in source, not on hardware:
- The bootloader's `board_teardown()` stops LFCLK and RTC1 before jumping.
- The core's `wiring.c` starts LFCLK without waiting on it.
- The bootloader feeds an inherited WDT inside its DFU loop, so the app's 8 s WDT cannot kill a
  transfer.

The GPREGRET DFU entry has no timeout. An escaped board waits in DFU until it receives an image or
is power-cycled.

## Open

- **MSV4 needs its own DFU device type** before any fleet use. Today an MSV3 bootloader will accept
  this LFXO image. The crystal check would send a mis-flashed MSV3 board back to DFU, but that is a
  recovery path, not a refusal.
- **The version floor is shared per board.** Application version 262145 is above all MSV3 images.
  Future MSV4 production images must go higher still.
- BLE UART is unauthenticated. Anyone in range can drive the front end or trigger a DFU reset,
  though never an unsigned image. That's acceptable for bench bring-up and not for deployment.

## First-board checklist (nothing here has been observed yet)

- [ ] `info` shows LFCLK `XTAL running` both at setup entry and now, and a plausible start wait (~250 ms)
- [ ] `vbat` matches a meter on `VBAT+` within ~2 %; `vdd` ≈ 3300 mV
- [ ] `temp` is about room temperature; warming the blade tip with a finger moves it
- [ ] `drive 16` gives a 1.000 MHz square wave on DRV and the `pins` report shows `[GPIOTE] hi-drive`; `drive off` returns it low
- [ ] On a scope with DRV and READ_CNTL, the `phase 0/4/8/12` edge positions match the table above
- [ ] `swap a` / `swap b` / `swap open` and `chop on`, then `read`: `SENSE_OUT` moves between states. OPEN is the reference
- [ ] `iq` shows a stable φ between repeats, and `sweep 16` shows a residual small next to A
- [ ] After every cancel and failure mode (disconnect during `drive`, `sweep` then `stop`, `vbat`, `temp`),
      `pins` shows both `/OE` released, DRV/TEMP_READ/VBAT_DIV_EN low, and no 1 MHz waveform remains on the scope
- [ ] Ten `drive` start/stop and disconnect cycles in a row: `info` shows HFXO off afterwards, and drive still starts cleanly
- [ ] `dfu enter`, then reinstall with a larger version over BLE **without power-cycling**. The new app
      must advertise and `info` must show LFCLK on XTAL. This proves the WDT is fed during DFU AND that
      the LFRC-bootloader → LFXO-app handoff survives an inherited running WDT. If it bounces back into
      DFU, a power cycle recovers it, but that would justify an MSV4-specific LFXO bootloader.
- [ ] Dead-crystal escape: short Y1's load or lift C2 on a sacrificial board, then confirm 5 blinks and a DFU advertisement
