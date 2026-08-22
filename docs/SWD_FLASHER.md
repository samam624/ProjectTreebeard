# ESP32-S3 as an SWD flasher for nRF52840

`arduino/MSV2_ESP32_SWD_Flasher` turns a spare ESP32-S3 devkit into a standalone
SWD debug adapter for nRF52840 targets — built for and bench-validated against
a bare Ebyte E73-2G4M08S1C module (the MCU used on MSV3), and equally usable
against XIAO nRF52840 Sense probes. It bit-bangs the ARM SWD protocol from
scratch on three GPIOs — no OpenOCD, no J-Link, no external debug probe
hardware needed. Useful for boards with no USB/DFU bootloader at all, or a
broken one.

**Status: fully working, end to end, on real hardware (2026-08-20).** On a
bare Ebyte E73-2G4M08S1C module (hand-held pad contact, no soldering):
`id`/`probe` confirmed a real, unprotected nRF52840 (`DPIDR = 0x2BA01477`,
`AP0 IDR = 0x24770011`, correct 1MB/4KB-page flash geometry); the chip's prior
content was an incomplete MBR-only fragment, not a working bootloader; a full
mass erase plus 5-region bootloader install (`pca10056_bootloader-0.11.0_s140_6.1.1.hex`)
completed with every segment verified; and after `run`, the chip booted into
the new bootloader and enumerated correctly as `NRF52BOOT` with matching
`INFO_UF2.TXT` (version, board ID, and SoftDevice all exactly as flashed). A
test application (`arduino/MSV3_E73_LED_Test`, blinks the D2 LED on P1.10)
was then compiled for `adafruit:nrf52:pca10056`, converted to `.uf2`, and
drag-and-dropped onto the `NRF52BOOT` drive — it booted and the LED blinked,
confirming the bootloader can actually accept and run new firmware, not just
enumerate. See "Bring-up history" below for the issues hit and fixed along
the way.

## Wiring

3.3V logic on both sides — no level shifting.

| ESP32-S3 GPIO | Signal | Target (nRF52840) |
|---|---|---|
| GPIO5 | SWCLK | SWCLK |
| GPIO6 | SWDIO | SWDIO |
| GPIO7 | nRESET | RESET |
| GND | GND | GND |

Do **not** connect target VDD to the ESP32-S3. The target keeps powering itself
(its own USB/battery) — this adapter only drives debug signals.

On a bare E73-2G4M08S1C module, SWDIO/SWCLK/RESET are castellated pads on the
module itself (check Ebyte's E73-2G4M08S1C datasheet pinout for exact pad
locations). On a XIAO nRF52840 Sense, they're unpopulated test points on the
underside of the board instead — check Seeed's pinout diagram for that case.

## Building and flashing the adapter itself

```powershell
.\scripts\arduino_swd_flasher.ps1 -Action compile
.\scripts\arduino_swd_flasher.ps1 -Action upload -Port COM3
```

Swap `COM3` for whatever `.\scripts\arduino_swd_flasher.ps1 -Action board-list`
reports — ports shift.

## Talking to it

It's a plain USB-serial command console at 115200 baud (native USB CDC, so a
reset is a real USB disconnect/reconnect — same caveat as the display receiver,
see `flash-esp32-display`). Use `scripts/serial_capture.py` if you need a
capture that survives a reset; a live interactive session (`arduino-cli monitor`
or a terminal program) is fine for the id/halt/rd/wr commands since those don't
reset the adapter itself.

```
help                    - list commands
id                      - connect (read-only) and report DPIDR / AP IDR / flash geometry
probe                   - loop checking for SWD contact, for hand-held wiring bring-up
halt                    - hardware-reset target, then halt the core
run                     - resume core execution, release debug
reset                   - pulse nRESET only, no SWD traffic
rd <hexaddr>            - read one 32-bit word
wr <hexaddr> <hexval>   - write one 32-bit word (raw, expert use — this is a
                          direct AHB bus write, no safety net)
erase CONFIRM           - mass-erase the entire target flash
flash <hexaddr> <declen> - receive <declen> raw bytes over serial (chunked,
                          acked), then erase+program+verify at <hexaddr>,
                          with live progress lines
mem                     - report free heap AND largest contiguous block
                          (the second number is what actually bounds
                          `flash`'s image buffer)
restart                 - soft-reboot this adapter (clears heap fragmentation
                          from a long interactive session; doesn't touch the
                          target)
```

### Hand-held test-point contact (no soldering)

If you're holding wire tips against a board's SWD test pads instead of
soldering, `id`/`erase`/`flash` aren't the right first tool — a momentary
contact drop mid-transaction just looks like a generic failure, with no signal
about whether the wiring is right or you just need to press harder. Use
`probe` instead: it retries a lightweight connect check ~7x/second and only
prints on a state change, so you can watch it live while adjusting pressure.

Open a live terminal against the port (`arduino-cli monitor -p COM3 -c
baudrate=115200`, or PuTTY, or the Arduino IDE's Serial Monitor — any of these
work fine here since, unlike a flash/reset capture, this is a plain ongoing
session with no port renumbering involved), type `probe`, then press the wires
against the pads:

```
probe: watching for SWD contact. Send any character to stop.
probe: still no response (attempt 20)
>>> CONTACT MADE (attempt 34) DPIDR = 0x2BA01477
>>> contact lost
>>> CONTACT MADE (attempt 41) DPIDR = 0x2BA01477
```

A real target should report the *same* DPIDR value every time contact is
made — if it comes back with different nonzero garbage on different attempts,
that's a wiring/contact problem (e.g. SWCLK and SWDIO swapped, or a marginal
short), not a working connection, even though `probe` reported "contact." Once
you're getting solid, repeatable contact, send Enter to stop `probe` and move
on to `id` for the full picture (flash size, page count, etc.).

### Typical bring-up sequence on a new target

1. Wire it up, power the target normally, run `id`. Confirm `DPIDR`, the AP CSW
   readback matches what was written, and FICR page size/flash size look like a
   real nRF52840 (1MB flash / 4KB pages), not `0xFFFFFFFF` (see APPROTECT note
   below).
2. `rd`/`wr` a scratch RAM address (e.g. `0x20000000`) to sanity-check memory
   access before touching flash.
3. `erase CONFIRM`, then `id` again to confirm the chip still responds.
4. Flash a real image (see below), or a small known-good test region with
   `flash <hexaddr> <declen>` first if you just want to sanity-check the write
   path.

### Flashing a compiled Arduino sketch (single region, address 0)

Export a `.bin` from `arduino-cli compile --export-binaries` — that's a flat
image meant to run from address 0, not an Intel HEX file with embedded
addresses, so `scripts/swd_flash_hex.py` doesn't apply here. Instead, issue
`flash 0 <length>` yourself in an interactive session and send the raw file
bytes right after the `flash: send raw binary now` prompt (any short script
that writes the file to the serial port works; match the byte count exactly,
since the device only reads that many bytes before returning to command mode).

This is only relevant for a chip with **no bootloader** (or one you're
deliberately bypassing). Once a target has a working bootloader installed
(see below), flashing new applications to *that* chip no longer needs SWD at
all — see "Flashing an application once the bootloader is installed" further
down.

### Flashing a bootloader (multi-region .hex, e.g. restoring USB/DFU)

A bootloader image isn't one flat block — it's several disjoint regions (MBR,
SoftDevice, bootloader code, a settings page, and a UICR register that tells
the chip's own MBR where the bootloader lives). `scripts/swd_flash_hex.py`
parses a `.hex` file into its contiguous segments and drives one `flash` call
per segment, handling addresses automatically:

```powershell
python scripts/swd_flash_hex.py --port COM3 --hex arduino/MSV2_ESP32_SWD_Flasher/bootloaders/pca10056_bootloader-0.11.0_s140_6.1.1.hex --mass-erase
```

That bundled hex is Nordic's generic **pca10056** (nRF52840-DK) board profile
from [Adafruit_nRF52_Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader)
(MIT licensed) — the right starting point for a **bare nRF52840 module with
no board-specific bootloader of its own** (this project's actual hardware is
an Ebyte E73-2G4M08S1C module, not a XIAO board — see
`bootloaders/README.md` for why `pca10056` and not a XIAO-specific build).
The same overall recipe (bit-banged SWD probe → mass erase → program the hex)
is independently confirmed working for bare E73-2G4M08S1C modules in
[this Nordic DevZone thread](https://devzone.nordicsemi.com/f/nordic-q-a/93841/first-time-noob---flashing-bootloader-onto-e73-2g4m08s1c-nrf52840-modules)
and (for a XIAO board specifically) in
[this bootloader-recovery write-up](https://www.pavelp.cz/posts/eng-xiao-nrf52840-bootloader-recovery/).
Every image built on this bootloader project writes the same 5-region shape:

| Address | Size | Contents |
|---|---|---|
| 0x00000000 | 2.8 KB | MBR |
| 0x00001000 | 149 KB | SoftDevice (S140 7.3.0) |
| 0x000F4000 | 33 KB | Bootloader code |
| 0x000FD800 | 88 B | Bootloader settings page |
| 0x10001014 | 8 B | UICR (bootloader address pointer) |

**Always mass-erase before writing a bootloader from scratch** (`--mass-erase`
above, or `erase CONFIRM` yourself first) — per the write-up above, skipping
this leaves whatever old application was at address 0 in place; it keeps
executing after the bootloader write and can corrupt the bootloader again.
This is why `flash`'s own per-call erase isn't a substitute: it only erases
the region it's about to write, not the rest of the chip.

### Flashing an application once the bootloader is installed

This is the actual payoff of going through the SWD install at all: once a
target has this bootloader **and any application is already running on it**,
updating that application again **no longer needs SWD, and doesn't even need
manual `.uf2` handling** — just use the normal Arduino upload flow, exactly
like a stock Feather/XIAO nRF52840:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_msv3_probe.ps1 -Action upload -Port COM3
```

For an MSV3 sketch other than the default bring-up console, also pass `-Sketch
arduino/MSV3_E73_LED_Test`. Do not use a raw PCA10056 compile for MSV3: its stock variant selects
an LF crystal that this hardware does not have. The repo wrapper enforces LFRC and a clean build.

(or `--input-dir <build-dir>` to upload an already-compiled `.zip` without
recompiling.) **Confirmed working live, 2026-08-20**, on the real MSV3 probe
PCB with no SWD leads attached at all — output showed the whole automatic
cycle end to end:

```
Upgrading target on COM6 with DFU package ...MSV3_Probe_Bringup_Test.ino.zip. ...
########################################
Activating new firmware
Device programmed.
New upload port: COM3 (serial)
```

**Why this works**: `pca10056.upload.protocol=nrfutil` in this core's
`boards.txt`, with `use_1200bps_touch=true` and `wait_for_upload_port=true`.
`arduino-cli upload` opens the board's *currently running* CDC serial port at
1200 baud then closes it — this core's own firmware watches for that touch
and responds by soft-resetting into the bootloader (same mechanism Feather/
XIAO nRF52840 boards use for their "just click Upload" experience; nothing
board-specific needed to enable it). The bootloader re-enumerates as a
DFU-serial port, `adafruit-nrfutil` (bundled at
`tools/adafruit-nrfutil/win32/adafruit-nrfutil.exe` in this core) pushes a
signed `.zip` DFU package — built automatically alongside the `.hex`/`.uf2`
during `compile`, via `recipe.objcopy.zip.pattern`'s `nrfutil dfu genpkg
--sd-req {build.sd_fwid}`, which requires/checks the S140 6.1.1 SoftDevice
already on the chip — then verifies, activates, and the board re-enumerates
back on its normal port automatically. Unlike a manual `.uf2` drag, this
reports explicit success/failure, so there's no "did that actually work?"
ambiguity.

**This only works if an application is already running to detect the touch.**
For the very first application ever loaded after a fresh SWD bootloader
install (bootloader resident, no app yet — nothing listening for a 1200bps
touch), or if a bad application is hung/crashed and can't respond, fall back
to one of:
- **SWD, direct** (works regardless of what state the application is in —
  the reliable universal fallback): `python scripts/swd_flash_hex.py --port
  <adapter COM> --hex path\to\sketch\build\adafruit.nrf52.pca10056\<sketch>.ino.hex`
  (no `--mass-erase` needed, that's only for writing a new bootloader).
- **Manual `.uf2` drag** onto the bootloader's `NRF52BOOT` mass-storage
  drive (confirmed working 2026-08-20 as a real removable drive matching the
  Adafruit VID `0x239A`) — needs the bootloader to already be resident with
  no/dead app, and needs a real double-tap of the `RST` test point (this
  board has no physical reset button) to force DFU mode if an app is
  currently running. Build the `.uf2` with:
  ```powershell
  python "C:\Users\<you>\AppData\Local\Arduino15\packages\adafruit\hardware\nrf52\1.7.0\tools\uf2conv\uf2conv.py" `
    -f 0xADA52840 -c -o output.uf2 path\to\your\sketch\build\adafruit.nrf52.pca10056\<sketch>.ino.hex
  ```
  `0xADA52840` is this platform's nRF52840 "family ID"
  (`pca10056.build.uf2_family` in `boards.txt`). This path gives **no
  confirmation it actually took effect** beyond the drive disappearing — a
  wrong or stale file dragged here can silently leave old firmware running
  indefinitely, which is exactly what happened during this board's first
  real bring-up session; prefer the two options above whenever either is
  available.

In all cases, **board FQBN must be `adafruit:nrf52:pca10056`** (installed via
the `adafruit:nrf52` core, Adafruit board-manager URL
`https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`)
— a different board profile could link the app at a flash address this
bootloader doesn't expect. Confirmed link address for this bootloader/
SoftDevice combo: `0x26000` (immediately after the SoftDevice's
`0x00001000-0x00025DE8` range from the table above) — if a build ever links
somewhere else, the board profile doesn't actually match what's on the chip.

**Correction, 2026-08-20: the earlier claim here that "no board-specific
Arduino variant exists ... no meaningful Arduino pin number" for this FQBN
was wrong**, checked against the real installed core this session:
`adafruit:nrf52`'s `pca10056` variant's `g_ADigitalPinMap[]` is a straight
identity map (Arduino pin N = P0.N for N<32, P1.(N-32) for N>=32), so e.g.
P1.10 is reachable as plain Arduino pin 42 via normal `pinMode()`/
`digitalWrite()` — no raw register access needed. `arduino/MSV3_E73_LED_Test`
still uses raw `NRF_P1->DIRSET`/`OUTSET`/`OUTCLR` (harmless, just
unnecessary), but `arduino/MSV3_Probe_Bringup_Test` uses the standard
Arduino API throughout, including `Wire.setPins()` for I2C on non-default
pins. One real, separate gotcha that *is* still live: **P0.09/P0.10 default
to NFC-antenna function, not GPIO**, on this chip regardless of variant —
needs `-DCONFIG_NFCT_PINS_AS_GPIOS` (both `compiler.c.extra_flags` and
`compiler.cpp.extra_flags`) at compile time the first time either pin is
used as a GPIO; the core's `system_nrf52840.c` then self-heals the UICR and
silently resets once, permanently, before `setup()` ever runs.

## Bring-up history (issues hit and fixed, 2026-08-20)

Kept for the next time this adapter or a similar one needs debugging — these
were all real failures on the actual bench, not hypothetical:

- **Heap fragmentation broke `flash`'s image buffer.** A `malloc()` sized at
  300KB failed *mid-session* even though total free heap was still ~350KB —
  Arduino's `String`-based command parsing fragments the heap over a long
  interactive session, so total free bytes overstate what's actually
  available as one contiguous block. Even a **fresh boot** only has ~295KB
  contiguous (`ESP.getMaxAllocHeap()`), not the ~351KB `ESP.getFreeHeap()`
  reports. Fixed by lowering the image-buffer cap to 200KB (real margin under
  either number) and adding `mem`'s contiguous-block reading plus a `restart`
  command to reset a fragmented heap without touching the target.
- **Native USB CDC silently dropped bytes on bulk transfers.** The `flash`
  command's binary-receive loop stalled partway through transfers as small as
  ~350 bytes (not a hand-contact/target issue at all — this happens over the
  ESP32↔PC USB link, before any SWD activity). A `yield()` in the receive
  loop and host-side write pacing both measurably helped but didn't fully
  eliminate it. The actual fix was `Serial.setRxBufferSize(8192)` before
  `Serial.begin()` — the default native-CDC RX ring buffer was too small for
  a sustained burst.
- **Timing-based fixes alone weren't trustworthy enough to rely on.** Even
  with the RX buffer fixed, `flash`'s binary transfer was changed to an
  explicit chunk+ack protocol (device prints `chunkack <n>` after each 512
  bytes; `scripts/swd_flash_hex.py` waits for it before sending the next
  chunk) so correctness doesn't depend on a guessed delay being large enough
  on a given machine/USB stack.
- **Writing was 5-10x sped up safely.** The original per-word flow re-sent
  the target address and did a full NVMC-ready poll for *every single word*
  — both needless, since CSW already has auto-increment enabled and a single
  bit-banged AP transaction already takes longer than the nRF52840's ~40µs
  word-write time. Batching 64 words between address-sets/ready-checks cut
  a full mass bootloader flash from an estimated ~2 minutes down to well
  under a minute. This trades per-word confirmation for verify-after-the-fact
  (any timing edge case would show up as a verify mismatch, not silent
  corruption) — the verify pass itself was deliberately left unbatched.

## Known limitations / not implemented

- **No CTRL-AP recovery.** If a target has `APPROTECT` (readback protection)
  enabled, FICR/memory reads will come back as `0xFFFFFFFF` and `id` prints a
  warning. Unlocking that requires the nRF52-specific CTRL-AP mass-erase
  sequence (a different AP than the AHB-AP this tool uses) — not implemented.
- **No dormant-state/multidrop wake sequence.** Fine for nRF52840 (classic
  ADIv5 SW-DP); would need extending for ADIv6 multidrop targets (e.g. nRF53/91).
- **Bit-banged, moderately tuned.** `SWD_HALF_PERIOD_US` was cut from an
  initial conservative 3µs to 1µs after confirming `id`/`probe` stayed
  reliable on this bench's wiring at the faster speed — could likely still go
  faster with real register-level GPIO access instead of `digitalWrite()`,
  but that trades more signal-integrity margin on hand-held/flying-wire
  connections for speed, which wasn't worth it once flashing already dropped
  to well under a minute from the write-batching change alone.
- The one value in the protocol code that's a literature default rather than
  something derived here is the AHB-AP `CSW` register value
  (`CSW_WORD_AUTOINCR = 0x23000052`) — widely used across bit-banged SWD tools,
  but `id` reads it back after writing so a mismatch is visible immediately.
