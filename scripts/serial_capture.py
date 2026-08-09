#!/usr/bin/env python
"""Capture serial output across disconnects - a plain `arduino-cli monitor` (or any
single-open serial session) exits the moment the port disappears and does not come
back on its own. On the XIAO nRF52840 boards this project uses, a DFU upload takes
the port down and re-enumerates it (sometimes under a different COM number
momentarily) before the freshly-flashed sketch boots - which is exactly when you
want to catch setup()'s boot-time Serial prints. This script retry-connects in a
loop for the whole capture window instead of dying on the first drop, so starting
it *before* an upload/reset reliably catches what happens after.

Example - start this in the background, then kick off the upload/reset:
  python scripts/serial_capture.py --port COM11 --duration 20 --out capture.log
"""

from __future__ import annotations

import argparse
import time
from datetime import datetime
from pathlib import Path

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture serial output across port drops/reconnects.")
    parser.add_argument("--port", required=True, help="COM port, e.g. COM11.")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=20.0, help="Total seconds to capture.")
    parser.add_argument("--out", type=Path, required=True, help="Log file to write (overwritten).")
    parser.add_argument("--retry-interval", type=float, default=0.3, help="Seconds between reconnect attempts.")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    log = args.out.open("w", encoding="utf-8")

    def emit(line: str) -> None:
        stamped = f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {line}"
        print(stamped, flush=True)
        log.write(stamped + "\n")
        log.flush()

    deadline = time.monotonic() + args.duration
    ser: serial.Serial | None = None
    buf = b""
    connected_once = False

    emit(f"--- capture started on {args.port} @ {args.baud}, {args.duration:.0f}s window ---")

    while time.monotonic() < deadline:
        if ser is None:
            try:
                ser = serial.Serial(args.port, args.baud, timeout=0.2)
                connected_once = True
                emit(f"--- connected to {args.port} ---")
            except serial.SerialException:
                time.sleep(args.retry_interval)
                continue

        try:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    emit(line.decode("utf-8", errors="replace").rstrip("\r"))
        except (serial.SerialException, OSError):
            emit(f"--- {args.port} dropped, reconnecting ---")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(args.retry_interval)

    if ser is not None:
        try:
            ser.close()
        except Exception:
            pass

    emit("--- capture window ended ---")
    log.close()

    if not connected_once:
        print(f"Never managed to connect to {args.port} - is the board plugged in?")
        return 1

    print(f"Wrote log to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
