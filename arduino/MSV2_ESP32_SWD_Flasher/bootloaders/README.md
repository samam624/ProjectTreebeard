# Bundled bootloader images

Neither file here was authored in this repo — both kept so
`scripts/swd_flash_hex.py` has known-good reference images without needing
network access at flash time. See
[docs/SWD_FLASHER.md](../../../docs/SWD_FLASHER.md) for how they're used and
what address ranges each one writes (same 5-region shape: MBR, SoftDevice,
bootloader code, settings page, UICR pointer).

## `pca10056_bootloader-0.11.0_s140_6.1.1.hex`

Downloaded 2026-08-20 from the current (0.11.0) release of
[adafruit/Adafruit_nRF52_Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases)
(MIT licensed). `pca10056` is Nordic's own nRF52840-DK reference board
definition — the generic, board-agnostic choice for a **bare nRF52840 module
with no board-specific bootloader build of its own**, e.g. the Ebyte
E73-2G4M08S1C module this project actually uses. Confirmed as the right
starting point for bare E73-2G4M08S1C modules by an independent
[Nordic DevZone thread](https://devzone.nordicsemi.com/f/nordic-q-a/93841/first-time-noob---flashing-bootloader-onto-e73-2g4m08s1c-nrf52840-modules)
covering exactly this situation. Unused LED/button GPIOs it assumes just sit
idle if not wired to anything on the actual hardware.

## `Seeed_XIAO_nRF52840_Sense_bootloader-0.6.1_s140_7.3.0.hex`

Downloaded 2026-08-20 from
[0hotpotman0/BLE_52840_Core](https://github.com/0hotpotman0/BLE_52840_Core/tree/main/bootloader/Seeed_XIAO_nRF52840_Sense).
**Not the right image for a bare E73-2G4M08S1C module** — it's built
specifically for Seeed's XIAO nRF52840 Sense board layout. Kept here in case
this tool is later used on an actual XIAO board.
