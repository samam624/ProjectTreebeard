#!/usr/bin/env python3
"""Find out why NVMC ERASEALL is being ignored on an nRF52840, using only `rd`/`wr`.

Background
----------
2026-09-07, probe 99: the adapter's `erase CONFIRM` printed `erase: OK` and the application
survived. The instrumented `eraseall_diag` then showed READY==1 on all 64 samples across 65 ms
after the ERASEALL write -- NVMC never went busy at all. So the erase was not merely cancelled
early (the first hypothesis, now refuted); it was never accepted.

NVMC ignores ERASEALL unless CONFIG.WEN == EEN. Nothing in the existing code ever reads CONFIG
back, so "we set EEN" has always been an assumption. This checks it, plus the other documented
reasons an erase can be refused, one register at a time.

Everything here is read-only EXCEPT the CONFIG writes and the optional --try-erase, and CONFIG is
restored to REN at the end.

Usage
-----
    python scripts/swd_nvmc_probe.py --port COM17
    python scripts/swd_nvmc_probe.py --port COM17 --try-erase   # also issues ERASEALL
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from swd_flash_hex import LineReader, wait_for_contact  # noqa: E402

NVMC_READY = 0x4001E400
NVMC_READYNEXT = 0x4001E408
NVMC_CONFIG = 0x4001E504
NVMC_ERASEALL = 0x4001E50C
UICR_APPROTECT = 0x10001208
# nRF52840 ACL: 8 regions of 8 bytes at 0x4001E000 + 0x2000.
ACL_BASE = 0x4001E000 + 0x2000


def cmd(ser, text, settle=0.35):
    ser.reset_input_buffer()
    ser.write((text + "\n").encode())
    time.sleep(settle)
    return ser.read_all().decode("utf-8", "replace")


def rd(ser, addr):
    out = cmd(ser, f"rd {addr:X}")
    for line in out.splitlines():
        line = line.strip()
        if "=" in line and line.lower().startswith("0x"):
            try:
                return int(line.split("=")[1].strip(), 16)
            except ValueError:
                pass
    return None


def show(label, value, note=""):
    if value is None:
        print(f"  {label:26s} READ FAILED")
    else:
        print(f"  {label:26s} 0x{value:08X}{('  ' + note) if note else ''}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--try-erase", action="store_true",
                    help="After confirming CONFIG, actually issue ERASEALL and watch READY. "
                         "DESTRUCTIVE if it works -- which is the point.")
    ap.add_argument("--contact-timeout", type=float, default=60.0)
    ap.add_argument("--contact-stable", type=float, default=3.0)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.4)
    ser.reset_input_buffer()
    if not wait_for_contact(ser, LineReader(ser),
                            timeout_s=args.contact_timeout, stable_s=args.contact_stable):
        ser.close()
        return 1
    ser.reset_input_buffer()

    print("\n--- state before touching anything ---")
    show("NVMC READY", rd(ser, NVMC_READY), "1 = idle")
    show("NVMC READYNEXT", rd(ser, NVMC_READYNEXT))
    show("NVMC CONFIG", rd(ser, NVMC_CONFIG), "0=REN 1=WEN 2=EEN")
    ap_val = rd(ser, UICR_APPROTECT)
    show("UICR APPROTECT", ap_val,
         "0xFFFFFFFF = unprotected" if ap_val == 0xFFFFFFFF else "NOT unprotected")

    print("\n--- ACL regions (a protected region blocks erase) ---")
    any_acl = False
    for i in range(8):
        base = ACL_BASE + i * 0x10
        addr = rd(ser, base + 0x0)
        size = rd(ser, base + 0x4)
        perm = rd(ser, base + 0x8)
        if addr not in (None, 0, 0xFFFFFFFF) or (size not in (None, 0, 0xFFFFFFFF)):
            any_acl = True
            print(f"  ACL[{i}] addr=0x{addr:08X} size=0x{size:08X} perm=0x{perm:08X}")
    if not any_acl:
        print("  none configured")

    print("\n--- the actual question: does CONFIG=EEN stick? ---")
    cmd(ser, f"wr {NVMC_CONFIG:X} 2")
    back = rd(ser, NVMC_CONFIG)
    show("CONFIG after writing 2", back,
         "EEN held -- erase should be accepted" if back == 2 else "*** DID NOT STICK ***")

    if args.try_erase:
        if back != 2:
            print("\n  refusing to issue ERASEALL: CONFIG is not EEN, the erase cannot work")
        else:
            print("\n--- issuing ERASEALL with CONFIG confirmed EEN ---")
            cmd(ser, f"wr {NVMC_ERASEALL:X} 1", settle=0.05)
            for i in range(12):
                r = rd(ser, NVMC_READY)
                print(f"  t+{i}: READY = {r}")
                if r == 0:
                    print("  -> NVMC went BUSY: the erase started")
                    break
            for _ in range(40):
                if rd(ser, NVMC_READY) == 1:
                    break
            w = rd(ser, 0x00026000)
            show("word at 0x26000 after", w, "0xFFFFFFFF = erased" if w == 0xFFFFFFFF else "still programmed")

    cmd(ser, f"wr {NVMC_CONFIG:X} 0")
    show("CONFIG restored", rd(ser, NVMC_CONFIG), "0 = REN")
    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
