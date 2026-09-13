#!/usr/bin/env python3
"""Force a hung/unreachable nRF52840 into BLE DFU over SWD, without erasing anything.

Why this exists
---------------
2026-09-07/08, guard-2 acceptance test: probe 99 was deliberately given an image that hangs before
advertising. It hung, the watchdog reset it every 5 s, and the boot counter never advanced. SWD
saw the application's 0xB1 write during a hung instance, while every following app instance still
started at attempt 1. That proves the value is gone before the application reads it after a
watchdog reset; it does NOT prove whether hardware, MBR/SoftDevice, bootloader, or startup code is
responsible. The board therefore sat in the exact BLE-invisible loop guard 2 was meant to escape,
and SWD was the only recovery path.

The obvious recovery is a full mass erase and reinstall, but that is several minutes of holding
pogo pins against castellated pads by hand, and the erase is not what is actually needed: the
bootloader is fine, only the application is bad. The bootloader enters BLE DFU when GPREGRET holds
its DFU magic, so writing that one word and pulsing reset is enough -- roughly ten seconds of
contact, after which the recovery happens over the radio.

  halt -> wr GPREGRET = 0xA8 -> reset -> bootloader comes up advertising DFU

Nothing is erased and the anti-rollback floor in the settings page is untouched, so the rescue
image still has to satisfy it.

Usage
-----
    python scripts/swd_enter_dfu.py --port COM17
    python scripts/ble_ota_update.py --probe 99 --skip-jump <good package>   # then, no pads
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from swd_flash_hex import LineReader, wait_for_contact  # noqa: E402

# nRF52840 POWER peripheral. GPREGRET is the register the Adafruit bootloader reads at startup to
# decide whether to enter DFU. GPREGRET2 was the failed first boot-counter design and is read here
# only as forensic evidence; guard 2 now uses linker-reserved .noinit RAM.
POWER_GPREGRET = 0x4000051C
POWER_GPREGRET2 = 0x40000520
# Must match DFU_MAGIC_OTA_RESET in the bootloader and in MSV3_Moisture_BLE_Sleep.ino.
DFU_MAGIC_OTA_RESET = 0xA8

# nRF52840 WDT reload register. The MSV3 sketch configures the watchdog with WDT_CONFIG_HALT_Run,
# so it KEEPS COUNTING while a debugger has the core halted. On a board stuck in a hang loop that
# leaves only ~5 s between halting and the watchdog resetting the chip -- and a watchdog reset
# clears GPREGRET, wiping the DFU magic before the bootloader can ever act on it. Feeding it first
# buys a fresh full timeout to work in.
WDT_RR0 = 0x40010600
WDT_RELOAD_MAGIC = 0x6E524635

def cmd(ser, text, settle=0.4):
    ser.reset_input_buffer()
    ser.write((text + "\n").encode())
    time.sleep(settle)
    return ser.read_all().decode("utf-8", "replace")


def rd(ser, addr):
    for line in cmd(ser, f"rd {addr:X}").splitlines():
        line = line.strip()
        if "=" in line and line.lower().startswith("0x"):
            try:
                return int(line.split("=")[1].strip(), 16)
            except ValueError:
                pass
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--contact-timeout", type=float, default=120.0)
    ap.add_argument("--contact-stable", type=float, default=3.0)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.4)
    ser.reset_input_buffer()

    print("\nThis does NOT erase anything -- it writes the bootloader's DFU magic and resets.")
    if not wait_for_contact(ser, LineReader(ser),
                            timeout_s=args.contact_timeout, stable_s=args.contact_stable):
        ser.close()
        return 1
    ser.reset_input_buffer()

    # Halting first matters: on a board stuck in a watchdog reset loop the core is re-running every
    # few seconds, and a GPREGRET write could otherwise land moments before a reset wipes it.
    print("\nhalting the core...")
    print("  " + cmd(ser, "halt", settle=1.0).strip().replace("\n", "\n  "))

    before2 = rd(ser, POWER_GPREGRET2)
    if before2 is not None:
        # Direct evidence from the failed design. Seeing 0xB1 during the hang proves this app
        # instance wrote it; seeing attempt 1 again after the next watchdog reset proves only that
        # the next app instance did not receive it. Do not attribute that loss to hardware here.
        print(f"  GPREGRET2 before reset = 0x{before2:08X}  "
              f"(magic 0xB0|count expected if the boot counter survived)")

    # Feed the watchdog BEFORE writing the magic. Halting does not stop it, so without this the
    # reset that clears GPREGRET can land between the write and the soft reset below.
    print("\nfeeding the watchdog to buy a full timeout (it runs even while halted)...")
    cmd(ser, f"wr {WDT_RR0:X} {WDT_RELOAD_MAGIC:X}", settle=0.2)

    print(f"\nwriting GPREGRET = 0x{DFU_MAGIC_OTA_RESET:02X} (bootloader DFU magic)...")
    cmd(ser, f"wr {POWER_GPREGRET:X} {DFU_MAGIC_OTA_RESET:X}")
    back = rd(ser, POWER_GPREGRET)
    if back is None:
        print("  could not read GPREGRET back")
        ser.close()
        return 1
    if (back & 0xFF) != DFU_MAGIC_OTA_RESET:
        print(f"  GPREGRET reads back 0x{back:08X} -- the magic did not stick, aborting")
        ser.close()
        return 1
    print(f"  GPREGRET reads back 0x{back:08X} -- magic held")

    # Ask the adapter to issue ONE SYSRESETREQ and release the core. Do not write AIRCR here and
    # then send `run`: `run` already writes AIRCR itself. The first version did both, so the first
    # reset could enter the bootloader and consume/clear 0xA8, then the second reset immediately
    # launched the still-valid hung application again -- indistinguishable from DFU never working.
    #
    # This intentionally does not use the adapter's `reset` command. That command only pulses its
    # GPIO7 nRESET output and cannot know whether GPIO7 is physically wired to the target; during
    # the failed session it printed success while the wire was absent. A future SWD session should
    # still wire GPIO7 -> RESET, but this recovery path does not depend on that wire.
    #
    # Feed again immediately before the one reset. Everything between the first feed and here
    # costs serial round-trips, and the watchdog is still counting the whole time.
    cmd(ser, f"wr {WDT_RR0:X} {WDT_RELOAD_MAGIC:X}", settle=0.15)

    print("\nissuing one SYSRESETREQ and releasing the core through adapter `run`...")
    run_reply = cmd(ser, "run", settle=1.0).strip()
    print("  " + run_reply.replace("\n", "\n  "))
    if "run: core reset and resumed" not in run_reply:
        print("  adapter did not confirm the reset/release request; DFU entry is not established")
        ser.close()
        return 1
    ser.close()

    print("\nYou can let go of the pads now.")
    print("The bootloader should be advertising DFU within a few seconds. Recover with:")
    print("  python scripts/ble_ota_update.py --probe <N> --skip-jump <good package>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
