#!/usr/bin/env python3
"""Send an Intel HEX image to the MSV2 ESP32-S3 SWD flasher over serial.

The flasher's `flash <hexaddr> <declen>` command only handles one contiguous
byte range per call, but a real firmware/bootloader image (see
arduino/Treebeard_ESP32_SWD_Flasher/bootloaders/) is usually several disjoint
regions -- e.g. a bootloader hex writes MBR, SoftDevice, bootloader code, the
bootloader settings page, and a UICR register, all at different addresses.
This parses the hex file into contiguous segments and drives one `flash`
call per segment.

Usage:
    python scripts/swd_flash_hex.py --port COM3 --hex path\\to\\image.hex
    python scripts/swd_flash_hex.py --port COM3 --hex path\\to\\image.hex --mass-erase

    # One-shot new-bootloader install (touch the pads, then run this --
    # waits for 3s of continuous, consistent-DPIDR contact before doing
    # anything, always mass-erases, flashes the bundled pca10056 bootloader
    # hex, no separate `erase CONFIRM` step, no separate `probe` session):
    python scripts/swd_flash_hex.py --port COM3 --bootloader

Doesn't mass-erase by default -- run `erase CONFIRM` yourself in an
interactive session first (see docs/SWD_FLASHER.md) unless you pass
--mass-erase to have this script do it before writing the first segment.
--bootloader always mass-erases regardless of --mass-erase/--no-mass-erase,
since a partial old application left behind by a non-erased write is exactly
what corrupts a freshly-written bootloader (see docs/SWD_FLASHER.md).
"""

import argparse
import re
import sys
import time
from pathlib import Path

import serial

DEFAULT_BOOTLOADER_HEX = (
    Path(__file__).resolve().parent.parent
    / "arduino" / "Treebeard_ESP32_SWD_Flasher" / "bootloaders"
    / "pca10056_bootloader-0.11.0_s140_6.1.1.hex"
)

PROGRESS_RE = re.compile(r"^progress: (.+?) (\d+)/(\d+)\b")


def render_progress_bar(line, bar_width=30):
    """Turn a 'progress: <label> <n>/<m> ...' line from the device into an
    in-place-updating bar string, or None if the line isn't a progress line."""
    m = PROGRESS_RE.match(line)
    if not m:
        return None
    label, num, den = m.group(1), int(m.group(2)), max(int(m.group(3)), 1)
    pct = min(100, int(num * 100 / den))
    filled = int(bar_width * pct / 100)
    bar = "#" * filled + "-" * (bar_width - filled)
    return f"\r  {label}: [{bar}] {pct:3d}% ({num}/{den})"


def parse_intel_hex(path):
    """Returns a list of (start_addr, bytes) contiguous segments."""
    segments = []
    cur_addr = None
    cur_data = bytearray()
    ext_addr = 0

    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"{path}:{lineno}: not an Intel HEX record: {line!r}")

            byte_count = int(line[1:3], 16)
            addr16 = int(line[3:7], 16)
            rectype = int(line[7:9], 16)
            data_hex = line[9:9 + byte_count * 2]
            data = bytes.fromhex(data_hex)

            if rectype == 0x04:  # extended linear address
                ext_addr = int(data_hex, 16) << 16
                continue
            if rectype == 0x02:  # extended segment address
                ext_addr = int(data_hex, 16) << 4
                continue
            if rectype == 0x01:  # EOF
                break
            if rectype != 0x00:  # skip start-linear-address etc.
                continue

            full_addr = ext_addr + addr16

            if cur_addr is not None and full_addr == cur_addr + len(cur_data):
                cur_data += data
            else:
                if cur_addr is not None:
                    segments.append((cur_addr, bytes(cur_data)))
                cur_addr = full_addr
                cur_data = bytearray(data)

    if cur_addr is not None:
        segments.append((cur_addr, bytes(cur_data)))

    return segments


class LineReader:
    """Buffers raw serial bytes so a burst containing multiple '\\n'-terminated
    lines doesn't silently drop everything after the first line."""

    def __init__(self, ser):
        self._ser = ser
        self._buf = bytearray()

    def read_line(self, timeout_s):
        deadline = time.time() + timeout_s
        while True:
            if b"\n" in self._buf:
                line, _, rest = self._buf.partition(b"\n")
                self._buf = bytearray(rest)
                return line.decode(errors="replace").rstrip("\r")
            if time.time() >= deadline:
                return None
            chunk = self._ser.read(self._ser.in_waiting or 1)
            if chunk:
                self._buf += chunk


DPIDR_RE = re.compile(r"DPIDR\s*=\s*0x([0-9A-Fa-f]+)")


def wait_for_contact(ser, reader, timeout_s=20, stable_s=3.0):
    """Drive the flasher's own `probe` command (a tight ~7x/sec contact-check
    loop meant for exactly this hand-held-wiring case) until it reports the
    *same* DPIDR continuously for `stable_s` seconds, not just one lucky read.

    A single successful check isn't enough evidence: a momentary/flickering
    contact can pass one parity check and then drop before erase+flash
    finishes (confirmed the hard way -- one lucky `id: OK` mid-thousands-of-
    retries let a real run start, mass-erase completed, then the first
    segment's own reconnect failed because contact had already let go). This
    also uses probe's DPIDR-consistency signal from docs/SWD_FLASHER.md:
    genuine contact reads the *same* DPIDR every time; different garbage
    between reads means noise passed the ACK/parity check, not a real link,
    so that resets the stability window same as a reported contact loss.
    """
    print(f"Waiting for {stable_s:.0f}s of continuous solid SWD contact "
          f"(touch the pads now, up to {timeout_s:.0f}s total)...")
    ser.reset_input_buffer()
    ser.write(b"probe\n")

    def stop_probe():
        ser.write(b"\n")
        stop_deadline = time.time() + 2
        while time.time() < stop_deadline:
            line = reader.read_line(timeout_s=0.3)
            if line is not None and "probe: stopped" in line:
                return

    deadline = time.time() + timeout_s
    contact_since = None
    contact_dpidr = None
    last_status_len = 0

    while time.time() < deadline:
        line = reader.read_line(timeout_s=0.2)
        if line is not None:
            m = DPIDR_RE.search(line)
            if "CONTACT MADE" in line or "holding contact" in line:
                dpidr = m.group(1) if m else None
                if contact_since is None:
                    contact_since = time.time()
                    contact_dpidr = dpidr
                elif dpidr is not None and dpidr != contact_dpidr:
                    print("\n  DPIDR changed mid-contact (0x%s -> 0x%s) -- treating as noise, "
                          "restarting stability window." % (contact_dpidr, dpidr))
                    contact_since = time.time()
                    contact_dpidr = dpidr
            elif "contact lost" in line:
                if contact_since is not None:
                    print()
                contact_since = None
                contact_dpidr = None

        if contact_since is not None:
            held_s = time.time() - contact_since
            status = f"  holding contact (DPIDR=0x{contact_dpidr}): {held_s:0.1f}/{stable_s:0.1f}s"
            print("\r" + status + " " * max(0, last_status_len - len(status)), end="", flush=True)
            last_status_len = len(status)
            if held_s >= stable_s:
                print()
                stop_probe()
                print(f"  contact stable for {stable_s:.0f}s -- proceeding.")
                return True

    stop_probe()
    print()
    print("  no continuously solid contact within the timeout -- adjust pad pressure/alignment and rerun.")
    return False


def send_segment(ser, reader, addr, data, verbose=True):
    cmd = f"flash {addr:X} {len(data)}\n"
    ser.write(cmd.encode())

    line = reader.read_line(timeout_s=5)
    if line is None or "send raw binary now" not in line:
        print(f"  ERROR: device didn't prompt for data (got: {line!r})")
        return False

    # Chunked with an explicit ack after each chunk (device prints
    # "chunkack <cumulative bytes>"), not a blind timed write -- a single
    # large write, and later a paced-but-unacknowledged chunked write, both
    # stalled partway through on this board's native USB CDC (confirmed on
    # the bench 2026-08-20, ~1100-2000 bytes into a 2816-byte transfer).
    # Waiting for each ack makes this correct regardless of the underlying
    # cause instead of hoping a delay value is big enough.
    chunk_size = 512
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent + chunk_size]
        ser.write(chunk)
        sent += len(chunk)
        ack = reader.read_line(timeout_s=10)  # > device's own 5s per-chunk timeout, so its message wins the race
        if ack is None or not ack.startswith("chunkack"):
            print(f"  ERROR: expected chunkack after {sent} bytes, got {ack!r}")
            return False
        acked = int(ack.split()[1])
        if acked != sent:
            print(f"  ERROR: chunkack mismatch -- sent {sent}, device acked {acked}")
            return False

    ok = False
    bar_open = False
    deadline = time.time() + 60  # erase+program+verify of ~150KB can take a while
    while time.time() < deadline:
        line = reader.read_line(timeout_s=5)
        if line is None:
            continue
        bar = render_progress_bar(line)
        if bar is not None:
            print(bar, end="", flush=True)
            bar_open = True
        else:
            if bar_open:
                print()  # close the in-place bar line before printing normally
                bar_open = False
            if verbose:
                print(f"  {line}")
        if "verify OK" in line:
            ok = True
            break
        if "verify FAILED" in line or "FAILED" in line or "failed" in line:
            ok = False
            break
    if bar_open:
        print()
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="Serial port of the ESP32-S3 SWD flasher, e.g. COM3")
    ap.add_argument("--hex", help="Path to the Intel HEX image to flash "
                     "(defaults to the bundled pca10056 bootloader hex if --bootloader is given)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mass-erase", action="store_true",
                     help="Send 'erase CONFIRM' before writing the first segment")
    ap.add_argument("--bootloader", action="store_true",
                     help="One-shot new-bootloader install: waits for solid SWD contact, "
                     "always mass-erases (skipping erase before a bootloader write risks "
                     "leaving old app code that re-corrupts it), then flashes --hex or the "
                     "bundled pca10056 bootloader hex if --hex isn't given")
    ap.add_argument("--contact-timeout", type=float, default=30.0,
                     help="Total seconds to wait for contact to go stable in --bootloader mode (default 30)")
    ap.add_argument("--contact-stable", type=float, default=3.0,
                     help="Seconds of continuous, consistent-DPIDR contact required before "
                     "erase/flash starts in --bootloader mode (default 3)")
    args = ap.parse_args()

    hex_path = args.hex or (str(DEFAULT_BOOTLOADER_HEX) if args.bootloader else None)
    if not hex_path:
        ap.error("--hex is required unless --bootloader is given")
    mass_erase = args.mass_erase or args.bootloader

    segments = parse_intel_hex(hex_path)
    if not segments:
        print("No data records found in hex file -- nothing to do.")
        return 1

    total_bytes = sum(len(d) for _, d in segments)
    print(f"{hex_path}: {len(segments)} segment(s), {total_bytes} bytes total")
    for addr, data in segments:
        print(f"  0x{addr:08X} - 0x{addr + len(data):08X}  ({len(data)} bytes)")

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()
    reader = LineReader(ser)

    if args.bootloader:
        print()
        if not wait_for_contact(ser, reader, timeout_s=args.contact_timeout, stable_s=args.contact_stable):
            return 1

    if mass_erase:
        print("\nMass erasing target...")
        ser.write(b"erase CONFIRM\n")
        deadline = time.time() + 30
        result = None
        while time.time() < deadline:
            line = reader.read_line(timeout_s=5)
            if line is None:
                continue
            print(f"  {line}")
            if "erase: OK" in line or "erase: FAILED" in line:
                result = line
                break
        if not result or "OK" not in result:
            print("Mass erase did not confirm success -- aborting before writing anything.")
            return 1

    print()
    for i, (addr, data) in enumerate(segments, 1):
        print(f"[{i}/{len(segments)}] writing 0x{addr:08X} ({len(data)} bytes)...")
        if not send_segment(ser, reader, addr, data):
            print(f"Segment at 0x{addr:08X} FAILED -- stopping, remaining segments not written.")
            return 1

    print("\nAll segments written and verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
