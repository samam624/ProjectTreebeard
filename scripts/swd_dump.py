#!/usr/bin/env python3
"""Read target flash/UICR out through the ESP32-S3 SWD adapter's `rdblk` command.

Why this exists
---------------
Two jobs, both of which were impossible before `rdblk` was added on 2026-09-07:

1. **Forensics.** On 2026-09-07 a mass erase reported `erase: OK` and the application survived it
   (probe 99 came back running its two-day-old firmware, confirmed by its BLE build string and by
   serial logging being enabled when every package built that day had it disabled). Working out
   what actually happened means reading the target's memory rather than reasoning about it, and
   the adapter's `rd` reads one 32-bit word per serial round-trip -- ~262,000 round-trips for a
   1 MB device.

2. **The pre-coating capture.** The coating plan requires archiving probe 4's running image before
   its first erase, because that image is the unreproducible provenance-gap build and a mass erase
   is genuinely the last moment it exists anywhere. That acceptance criterion was written without
   checking the adapter could satisfy it. It could not. Now it can.

Reads are non-destructive: this never writes to the target.

Usage
-----
    # the regions that matter on an MSV3 probe, into one directory
    python scripts/swd_dump.py --port COM17 --preset msv3 --out dumps/probe99

    # or an explicit range
    python scripts/swd_dump.py --port COM17 --range 0x26000 0x21000 --out dumps/probe99
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

import serial

# Reused, not reimplemented. swd_flash_hex.py already owns the contact gate -- 3 continuous
# seconds of consistent-DPIDR contact, restarting the window if the DPIDR changes mid-hold --
# and it was written that way because a flickering hand contact once passed a single check, the
# mass erase started, and contact was then lost mid-write. Every command that needs SUSTAINED
# contact must go through it; a dump of a few hundred KB needs it more than a flash does, because
# it holds contact for minutes.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from swd_flash_hex import LineReader, wait_for_contact  # noqa: E402

# Named regions of an nRF52840 running the MSV3 stack. Sizes come from the bootloader's own
# dfu_types.h memory map rather than from guesses.
PRESETS = {
    "msv3": [
        ("mbr", 0x00000000, 0x00001000),
        ("softdevice", 0x00001000, 0x00025000),
        ("application", 0x00026000, 0x00021000),
        ("bootloader", 0x000F4000, 0x00008000),
        ("mbr_params", 0x000FE000, 0x00001000),
        ("settings", 0x000FF000, 0x00001000),
        ("uicr", 0x10001000, 0x00000100),
    ],
    # Everything needed to explain a failed erase, and nothing that only costs hold time. Skips
    # the 148 KB SoftDevice deliberately: it says nothing about whether ERASEALL ran, and the
    # operator is holding pogo pins against castellated pads by hand throughout.
    "forensics": [
        ("application", 0x00026000, 0x00021000),
        ("bootloader", 0x000F4000, 0x00008000),
        ("settings", 0x000FF000, 0x00001000),
        ("uicr", 0x10001000, 0x00000100),
    ],
    "settings": [
        ("settings", 0x000FF000, 0x00001000),
    ],
    "app": [
        ("application", 0x00026000, 0x00021000),
    ],
    "full": [
        ("flash_1mb", 0x00000000, 0x00100000),
        ("uicr", 0x10001000, 0x00000100),
    ],
}

# One rdblk call per chunk. Small enough that a stall costs little and progress is visible;
# large enough that per-command overhead is not the bottleneck.
CHUNK = 4096


def read_line(ser, timeout_s=10.0):
    deadline = time.time() + timeout_s
    buf = b""
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode("utf-8", "replace").strip()
        if b != b"\r":
            buf += b
    return None


def read_exact(ser, n, timeout_s=60.0):
    deadline = time.time() + timeout_s
    out = b""
    while len(out) < n and time.time() < deadline:
        got = ser.read(n - len(out))
        if got:
            out += got
            deadline = time.time() + timeout_s
    return out


def rdblk(ser, addr, length):
    """One bulk read. Returns bytes, or None on any protocol deviation."""
    ser.reset_input_buffer()
    ser.write(f"rdblk {addr:X} {length}\n".encode())

    # Wait for the "rdblk: <n> bytes" header, tolerating echo/noise ahead of it.
    header = None
    for _ in range(20):
        line = read_line(ser)
        if line is None:
            break
        if line.startswith("rdblk: ") and line.endswith("bytes"):
            header = line
            break
        if "usage" in line or "failed" in line:
            print(f"    adapter refused: {line}")
            return None
    if header is None:
        print("    no rdblk header from adapter")
        return None

    declared = int(header.split()[1])
    if declared != length:
        print(f"    adapter declared {declared} bytes, asked for {length}")
        return None

    payload = read_exact(ser, length)
    if len(payload) != length:
        print(f"    short read: {len(payload)}/{length} bytes")
        return None

    # Drain the trailing "rdblk: OK".
    for _ in range(4):
        line = read_line(ser, timeout_s=3)
        if line is None or line.startswith("rdblk: OK"):
            break
        if "read failed" in line:
            print(f"    {line}")
            return None
    return payload


def dump_region(ser, name, start, size, out_dir):
    path = out_dir / f"{name}_0x{start:08X}_{size}.bin"
    print(f"  {name}: 0x{start:08X} .. 0x{start + size:08X} ({size} bytes)")
    data = bytearray()
    t0 = time.time()
    off = 0
    while off < size:
        n = min(CHUNK, size - off)
        blk = rdblk(ser, start + off, n)
        if blk is None:
            print(f"    FAILED at offset 0x{off:X} -- writing partial dump")
            break
        data += blk
        off += n
        pct = 100 * off // size
        elapsed = max(time.time() - t0, 1e-6)
        rate = off / elapsed
        print(f"\r    {pct:3d}%  {off}/{size} bytes  {rate/1024:.1f} KiB/s", end="", flush=True)
    print()
    path.write_bytes(bytes(data))
    digest = hashlib.sha256(bytes(data)).hexdigest()
    blank = all(b == 0xFF for b in data)
    print(f"    -> {path.name}  sha256 {digest[:32]}...")
    print(f"    -> {'ALL 0xFF (blank/erased)' if blank else 'contains data'}")
    return path, digest, blank


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="Adapter serial port, e.g. COM17")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True, help="Directory to write the dump files into")
    ap.add_argument("--preset", choices=sorted(PRESETS), help="Named region set")
    ap.add_argument("--range", nargs=2, metavar=("HEXADDR", "HEXLEN"),
                    help="Explicit region instead of a preset")
    ap.add_argument("--no-wait-contact", action="store_true",
                    help="Skip the contact gate. Only for a target on a fixed jig where contact "
                         "cannot drift; hand-held pads must always use the gate.")
    ap.add_argument("--contact-timeout", type=float, default=60.0,
                    help="Seconds to wait for solid contact before giving up (default 60)")
    ap.add_argument("--contact-stable", type=float, default=3.0,
                    help="Seconds of continuous consistent-DPIDR contact required (default 3)")
    ap.add_argument("--eraseall-diag", action="store_true",
                    help="After the dump, run the adapter's instrumented ERASEALL in the SAME "
                         "held session. DESTRUCTIVE. Exists so the whole forensic sequence is "
                         "one command and one hold rather than two.")
    args = ap.parse_args()

    if not args.preset and not args.range:
        ap.error("pass --preset or --range")

    if args.range:
        start = int(args.range[0], 16)
        size = int(args.range[1], 16)
        regions = [(f"range_0x{start:X}", start, size)]
    else:
        regions = PRESETS[args.preset]

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    if not args.no_wait_contact:
        reader = LineReader(ser)
        if not wait_for_contact(ser, reader,
                                timeout_s=args.contact_timeout,
                                stable_s=args.contact_stable):
            ser.close()
            return 1
        ser.reset_input_buffer()

    print(f"Dumping via {args.port} into {out_dir}/")
    results = []
    for name, start, size in regions:
        results.append(dump_region(ser, name, start, size, out_dir))

    if args.eraseall_diag:
        print("\nRunning instrumented ERASEALL (DESTRUCTIVE) -- keep holding contact...")
        ser.reset_input_buffer()
        ser.write(b"eraseall_diag\n")
        # The adapter samples for up to 400 ms, then blank-checks 256 pages one word each over
        # SWD, which is the slow part. Read until it says done rather than sleeping a guess.
        deadline = time.time() + 180
        lines = []
        while time.time() < deadline:
            line = read_line(ser, timeout_s=20)
            if line is None:
                break
            print(f"  {line}")
            lines.append(line)
            if "eraseall_diag: done" in line:
                break
        (out_dir / "eraseall_diag.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"  -> {out_dir / 'eraseall_diag.txt'}")

    ser.close()

    manifest = out_dir / "SHA256SUMS.txt"
    with manifest.open("w", encoding="utf-8") as fh:
        for path, digest, blank in results:
            fh.write(f"{digest}  {path.name}{'  (blank)' if blank else ''}\n")
    print(f"\nWrote {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
