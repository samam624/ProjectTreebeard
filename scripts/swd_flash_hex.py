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
import hashlib
import json
import re
import sys
import time
from pathlib import Path

import serial

MSV3_RELEASES = Path(__file__).resolve().parents[1] / "MSV3" / "bootloader" / "generated"
MSV3_BOOT_HEX = "msv3-e73-recovery-signed-s140-6.1.1.hex"

# The SWD flasher sketch lives under DisplayCenter/, not MSV3/ -- it is shared receiver-side
# hardware, and the 2026-08-29 project-treebeard reorg moved every sketch from arduino/<Sketch>/
# to <Subsystem>/arduino/<Sketch>/. This path was left pointing at the pre-reorg location, so
# --bootloader failed on a missing file rather than flashing anything. Corrected 2026-09-03.
DEFAULT_BOOTLOADER_HEX = (
    Path(__file__).resolve().parents[1]
    / "DisplayCenter" / "arduino" / "Treebeard_ESP32_SWD_Flasher" / "bootloaders"
    / "pca10056_bootloader-0.11.0_s140_6.1.1.hex"
)

def resolve_msv3_release(version):
    """Path to an MSV3 release bootloader hex, verified against its own release manifest.

    Why the hash check is here and not left to the operator: this is the last moment before an
    image becomes the permanent, unflashable half of a conformally coated probe. Verifying that
    the bytes about to go over SWD are the exact reviewed release -- not a local rebuild that
    happens to be equivalent, not a half-copied file -- is cheap here and impossible afterwards.
    """
    release_dir = MSV3_RELEASES / version
    manifest_path = release_dir / "release-manifest.json"
    hex_path = release_dir / MSV3_BOOT_HEX
    if not manifest_path.exists():
        raise SystemExit(f"no MSV3 release manifest at {manifest_path}")
    if not hex_path.exists():
        raise SystemExit(f"no bootloader hex at {hex_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected = None
    for entry in manifest.get("files", []):
        if entry.get("name") == MSV3_BOOT_HEX:
            expected = (entry.get("sha256") or "").lower()
            break
    if not expected:
        raise SystemExit(f"{manifest_path} does not record a sha256 for {MSV3_BOOT_HEX}")

    actual = hashlib.sha256(hex_path.read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(
            f"REFUSING TO FLASH. {hex_path.name} hashes to\n"
            f"  {actual}\n"
            f"but {manifest_path.name} records\n"
            f"  {expected}\n"
            f"The file is not the released image. Rebuild the release, or restore the artifact, "
            f"before putting it on a board you are about to coat.")

    print(f"MSV3 release {version}: {MSV3_BOOT_HEX}")
    print(f"  sha256 {actual} -- matches release-manifest.json")
    return str(hex_path)


PROGRESS_RE = re.compile(r"^progress: (.+?) (\d+)/(\d+)\b")

# Cortex-M4 Debug Halting Control and Status Register, read back after the post-flash reset to
# prove it happened. S_RESET_ST is sticky and clears when DHCSR is read, so the first read after
# the reset is the only one that can show it.
DHCSR = 0xE000EDF0
DHCSR_S_HALT = 1 << 17
DHCSR_S_LOCKUP = 1 << 19
DHCSR_S_RESET_ST = 1 << 25
DHCSR_RELEASE = 0xA05F0001  # DBGKEY | C_DEBUGEN with C_HALT clear: let a halted core run
RD_RE = re.compile(r"0x([0-9A-Fa-f]+) = 0x([0-9A-Fa-f]+)")


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


def adapter_command(ser, reader, text, prefix, timeout_s=3.0):
    """Send one adapter command and return its reply lines, up to the first line starting with
    `prefix` (the adapter's own name for the command, e.g. "run:")."""
    ser.write((text + "\n").encode())
    lines = []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = reader.read_line(timeout_s=0.5)
        if line is None:
            continue
        lines.append(line)
        if line.startswith(prefix) or RD_RE.match(line):
            break
    return lines


def read_dhcsr(ser, reader):
    for line in adapter_command(ser, reader, f"rd {DHCSR:X}", "rd:"):
        m = RD_RE.match(line)
        if m and int(m.group(1), 16) == DHCSR:
            return int(m.group(2), 16)
    return None


def reset_and_verify(ser, reader):
    """Reset the target over SWD and prove from the core's own debug registers that it restarted
    and is running. Returns True only when DHCSR shows both.

    SYSRESETREQ (the adapter's `run`) rather than the nRESET pulse (`reset`), because the pulse
    depends on two things the flash itself can take away:
      - a GPIO7 -> RESET wire, which the adapter cannot sense (it printed "nRESET pulsed" with no
        wire attached on 2026-09-07), and
      - UICR.PSELRESET routing P0.18 to the reset circuit. Mass erase clears UICR, so straight
        after `--msv3` the RST pad is an ordinary GPIO until an application's SystemInit writes it
        back. MSV4 board 1, 2026-10-01: the pulse and a tweezer short of RST to GND both did
        nothing; SYSRESETREQ over SWD (swd_enter_dfu.py) started it at once.
    SYSRESETREQ travels over SWDIO/SWCLK, so it works whenever the flash itself just worked.
    """
    print("Resetting target over SWD (SYSRESETREQ) so the newly written image starts...")
    reply = adapter_command(ser, reader, "run", "run:")
    for line in reply:
        print(f"  {line}")
    if not any("run: core reset and resumed" in line for line in reply):
        # Fallback only. If the SWD reset could not be sent, the pulse may still reach a board
        # that has both the GPIO7 wire and a programmed PSELRESET.
        print("  SWD reset not confirmed by the adapter; trying the nRESET pulse instead...")
        for line in adapter_command(ser, reader, "reset", "reset:"):
            print(f"  {line}")
        return False

    # `run` writes the reset request and then clears C_HALT. If that second write landed while
    # the core was still coming out of reset, the core can sit halted at the reset vector.
    time.sleep(0.3)
    first = read_dhcsr(ser, reader)
    if first is None:
        print("  could not read DHCSR back after the reset (contact lost?)")
        return False
    reset_seen = bool(first & DHCSR_S_RESET_ST)
    latest = first
    if latest & DHCSR_S_HALT:
        print(f"  DHCSR = 0x{latest:08X}: core still halted after the reset, releasing it...")
        adapter_command(ser, reader, f"wr {DHCSR:X} {DHCSR_RELEASE:X}", "wr:")
        latest = read_dhcsr(ser, reader)
        if latest is None:
            print("  could not read DHCSR back after releasing the core")
            return False

    print(f"  DHCSR = 0x{first:08X}" + (f" -> 0x{latest:08X}" if latest != first else ""))
    if not reset_seen:
        print("  S_RESET_ST is clear: the core has NOT been reset since the flash.")
        return False
    if latest & DHCSR_S_LOCKUP:
        print("  S_LOCKUP is set: the core reset, then faulted straight into lockup.")
        return False
    if latest & DHCSR_S_HALT:
        print("  S_HALT is still set: the core reset but is halted.")
        return False
    print("  verified: the core reset (S_RESET_ST) and is running (S_HALT clear).")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="Serial port of the ESP32-S3 SWD flasher, e.g. COM3")
    ap.add_argument("--hex", help="Path to the Intel HEX image to flash "
                     "(defaults to the bundled pca10056 bootloader hex if --bootloader is given)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mass-erase", action="store_true",
                     help="Send 'erase CONFIRM' before writing the first segment")
    ap.add_argument("--msv3", metavar="VERSION",
                     help="Install an MSV3 release bootloader, e.g. --msv3 1.0.2. Resolves the hex "
                          "from MSV3/bootloader/generated/<VERSION>/, VERIFIES its sha256 against "
                          "that release's manifest, then waits for solid contact and mass-erases "
                          "exactly as --bootloader does. This is the correct flag for a fleet probe.")
    ap.add_argument("--no-reset", action="store_true",
                     help="Leave the target halted after a successful flash instead of resetting "
                          "it over SWD. Only useful when you intend to attach a debugger to the "
                          "halted core; a halted board does not enumerate or advertise.")
    ap.add_argument("--stock-bootloader", action="store_true",
                     help="Confirm that bare --bootloader really should install the generic "
                          "unsigned Adafruit image rather than an MSV3 release.")
    ap.add_argument("--bootloader", action="store_true",
                     help="One-shot new-bootloader install: waits for solid SWD contact, "
                     "always mass-erases (skipping erase before a bootloader write risks "
                     "leaving old app code that re-corrupts it), then flashes --hex or the "
                     "bundled pca10056 bootloader hex if --hex isn't given")
    ap.add_argument("--wait-contact", action="store_true",
                     help="No longer needed: the contact gate is now the default for every target "
                          "operation. Accepted so existing runbooks and docs keep working.")
    ap.add_argument("--no-wait-contact", action="store_true",
                     help="Skip the contact gate. Only for a target on a fixed jig where contact "
                          "cannot drift. Hand-held pogo pins must always use the gate -- a "
                          "flickering contact once passed a single check, the mass erase started, "
                          "and contact was lost mid-write.")
    ap.add_argument("--contact-timeout", type=float, default=30.0,
                     help="Total seconds to wait for contact to go stable (default 30)")
    ap.add_argument("--contact-stable", type=float, default=3.0,
                     help="Seconds of continuous, consistent-DPIDR contact required before "
                     "erase/flash starts in --bootloader mode (default 3)")
    args = ap.parse_args()

    if args.msv3 and args.hex:
        ap.error("--msv3 and --hex both name an image; pass one")

    if args.msv3:
        hex_path = resolve_msv3_release(args.msv3)
    elif args.hex:
        hex_path = args.hex
    elif args.bootloader:
        # Bare --bootloader installs the STOCK Adafruit loader. That is the right thing for a
        # fresh board being brought up, and the wrong thing for an MSV3 fleet probe: it accepts
        # generic 0x0052 packages, requires no signature, and would silently replace an MSV3
        # bootloader (and its anti-rollback floor) with one that has neither. Since 1.0.2 exists
        # and is what fleet probes are meant to carry, make the choice explicit rather than
        # letting the default decide it.
        print("About to install the STOCK Adafruit bootloader "
              f"({DEFAULT_BOOTLOADER_HEX.name}).")
        print("  It is UNSIGNED and accepts generic device-type 0x0052 packages.")
        print("  For an MSV3 fleet probe you almost certainly want:  --msv3 1.0.2")
        if not args.stock_bootloader:
            ap.error("bare --bootloader is ambiguous now that MSV3 releases exist. "
                     "Pass --msv3 <version> for an MSV3 probe, or add --stock-bootloader to "
                     "confirm you really mean the generic Adafruit image.")
        hex_path = str(DEFAULT_BOOTLOADER_HEX)
    else:
        ap.error("pass --msv3 <version>, --hex <file>, or --bootloader")

    # --msv3 is a bootloader install: it needs the same contact guard and mass erase, because a
    # partial old application left behind by a non-erased write is exactly what corrupts a freshly
    # written bootloader.
    bootloader_mode = args.bootloader or bool(args.msv3)
    mass_erase = args.mass_erase or bootloader_mode

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

    # CONTACT GATE IS THE DEFAULT FOR EVERY TARGET OPERATION, not just bootloader installs.
    #
    # It used to be `if bootloader_mode or args.wait_contact`, which meant a plain
    # `--hex foo.hex --mass-erase` would erase and program with no contact check at all. That is
    # the same shape of hole as the one that let `erase: OK` be printed without verifying: the
    # safe behaviour was available but opt-in, so it was absent exactly when nobody remembered to
    # ask for it. Anything that writes to, erases, or reads a hand-held target waits first, and
    # skipping it takes an explicit, self-describing flag.
    if not args.no_wait_contact:
        print()
        if not wait_for_contact(ser, reader, timeout_s=args.contact_timeout, stable_s=args.contact_stable):
            return 1

    if mass_erase:
        print("\nMass erasing target...")
        ser.write(b"erase CONFIRM\n")
        # Wait on PROGRESS, not on a wall clock. The erase now blank-checks every word of the
        # regions that could hold a bootable remnant before it will claim success, which takes
        # a couple of minutes -- far longer than the fixed 30 s budget this used to have, so the
        # host gave up mid-check and reported a failure that had not happened. Any line from the
        # adapter resets the idle timer; only genuine silence, or an overall cap, aborts.
        IDLE_TIMEOUT = 45
        HARD_CAP = 900
        started = time.time()
        idle_deadline = time.time() + IDLE_TIMEOUT
        result = None
        while time.time() < idle_deadline and (time.time() - started) < HARD_CAP:
            line = reader.read_line(timeout_s=5)
            if line is None:
                continue
            idle_deadline = time.time() + IDLE_TIMEOUT
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

    # Release the target. The adapter's `flash` command halts the core to program it and does
    # NOT resume it, so without this the board sits halted after a completely successful flash:
    # no USB enumeration, no BLE advertising, nothing. Measured on probe 99, 2026-09-07 -- every
    # segment verified, and then the follow-on BLE seed failed with "Bootloader never advertised"
    # because the bootloader had never been given the chance to start. It runs inside the same
    # contact hold as the flash, so keep the pins on until the verdict prints.
    if args.no_reset:
        print("--no-reset given: target left halted. Power-cycle it or send `run` to start it.")
        return 0

    if not reset_and_verify(ser, reader):
        print("The image is written and verified, but the reset was NOT confirmed.")
        print("Power-cycle the board (or run scripts\\swd_enter_dfu.py) to start it.")
        return 2
    print("You can let go of the pads now.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
