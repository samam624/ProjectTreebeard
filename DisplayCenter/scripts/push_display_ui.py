#!/usr/bin/env python
"""Push an MSV2 display ui.json file to the ESP32 runtime.

Use the IP printed in the ESP32 serial monitor. If it prints 192.168.4.1,
connect your computer to the board's MSV2-Display WiFi network first.

Examples:
  python scripts/push_display_ui.py --file ui/display_default.json
  python scripts/push_display_ui.py --url http://192.168.4.1 --ping
  python scripts/push_display_ui.py --url http://192.168.4.1/ui --file ui/display_default.json --watch
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request
from pathlib import Path


def endpoint(base_or_endpoint: str, route: str) -> str:
    url = base_or_endpoint.rstrip("/")
    if url.endswith(route):
        return url
    return f"{url}{route}"


def load_json(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    parsed = json.loads(text)
    return json.dumps(parsed, separators=(",", ":")).encode("utf-8")


def ping(url: str) -> None:
    ping_url = endpoint(url, "/ping")
    with urllib.request.urlopen(ping_url, timeout=5) as response:
        body = response.read().decode("utf-8", errors="replace").strip()
        print(f"Ping {ping_url}: HTTP {response.status} {body}")


def push(url: str, path: Path) -> None:
    payload = load_json(path)
    ui_url = endpoint(url, "/ui")
    request = urllib.request.Request(
        ui_url,
        data=payload,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=8) as response:
        body = response.read().decode("utf-8", errors="replace")
        print(f"Pushed {path} to {ui_url} ({len(payload)} bytes): HTTP {response.status} {body}")


def print_error(exc: BaseException, url: str) -> None:
    print(f"Push failed: {exc}")
    print(f"Check the board with: http://{url.split('://', 1)[-1].split('/', 1)[0]}/ping")
    print("If the board printed 192.168.4.1, your computer must be on the MSV2-Display WiFi network.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Push display UI JSON to an MSV2 ESP32 display.")
    parser.add_argument("--file", type=Path, default=Path("ui/display_default.json"), help="UI JSON file to push.")
    parser.add_argument("--url", default="http://192.168.4.1", help="ESP32 runtime base URL or /ui endpoint.")
    parser.add_argument("--ping", action="store_true", help="Only test the ESP32 /ping endpoint.")
    parser.add_argument("--watch", action="store_true", help="Push again whenever the JSON file changes.")
    parser.add_argument("--interval", type=float, default=1.0, help="Watch polling interval in seconds.")
    args = parser.parse_args()

    if args.ping:
        try:
            ping(args.url)
        except (OSError, urllib.error.URLError) as exc:
            print_error(exc, args.url)
            return 1
        return 0

    if not args.watch:
        try:
            push(args.url, args.file)
            return 0
        except (OSError, json.JSONDecodeError, urllib.error.URLError) as exc:
            print_error(exc, args.url)
            return 1

    last_mtime = None
    print(f"Watching {args.file}. Press Ctrl+C to stop.")
    while True:
        try:
            mtime = args.file.stat().st_mtime
            if mtime != last_mtime:
                push(args.url, args.file)
                last_mtime = mtime
        except (OSError, json.JSONDecodeError, urllib.error.URLError) as exc:
            print_error(exc, args.url)
        time.sleep(args.interval)


if __name__ == "__main__":
    raise SystemExit(main())
