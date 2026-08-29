#!/usr/bin/env python
"""Measure MSV2 probe packet loss using the ESP32 display hub /data endpoint.

Examples:
  python scripts/esp32_packet_loss_test.py --url http://10.0.0.236 --sensors 1 2 --duration 300
  python scripts/esp32_packet_loss_test.py --url http://10.0.0.236 --sensors 1 2 --interval 0.5
  python scripts/esp32_packet_loss_test.py --url http://10.0.0.236 --sensors 1 2 --csv packet_test.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


WAKE_MODULUS = 1 << 24


@dataclass
class Baseline:
    wake: int
    received: int
    missed_total: int
    time_s: float
    has_counters: bool


def endpoint(base_or_endpoint: str, route: str) -> str:
    url = base_or_endpoint.rstrip("/")
    if url.endswith(route):
        return url
    return f"{url}{route}"


def fetch_data(url: str) -> dict:
    request = urllib.request.Request(
        endpoint(url, "/data"),
        headers={"Cache-Control": "no-cache"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def wake_delta(current: int, previous: int) -> int:
    return (current - previous) % WAKE_MODULUS


def format_pct(value: float | None) -> str:
    if value is None:
        return "--"
    return f"{value:5.2f}%"


def sensor_by_id(payload: dict) -> dict[int, dict]:
    return {int(item["sensor"]): item for item in payload.get("sensors", []) if "sensor" in item}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run an ESP32 /data packet-loss test for MSV2 BLE probes.")
    parser.add_argument("--url", default="http://192.168.4.1", help="ESP32 display hub base URL.")
    parser.add_argument("--sensors", type=int, nargs="+", default=[1, 2], help="Sensor IDs to track.")
    parser.add_argument("--interval", type=float, default=1.0, help="Polling interval in seconds.")
    parser.add_argument("--duration", type=float, default=0, help="Test duration in seconds. Use 0 to run until Ctrl+C.")
    parser.add_argument("--csv", type=Path, help="Optional CSV output path.")
    args = parser.parse_args()

    baselines: dict[int, Baseline] = {}
    csv_file = None
    writer = None

    if args.csv:
        csv_file = args.csv.open("w", newline="", encoding="utf-8")
        writer = csv.DictWriter(
            csv_file,
            fieldnames=[
                "elapsed_s",
                "sensor",
                "wake",
                "wake_delta",
                "received_delta",
                "missed_delta",
                "expected_delta",
                "loss_percent",
                "observed_period_s",
                "moisture",
                "battery_percent",
                "rssi",
                "age_ms",
            ],
        )
        writer.writeheader()

    start = time.monotonic()
    print(f"Polling {endpoint(args.url, '/data')} every {args.interval:g}s.")
    print("Press Ctrl+C to stop.\n")

    try:
        while True:
            elapsed = time.monotonic() - start
            if args.duration and elapsed > args.duration:
                break

            try:
                payload = fetch_data(args.url)
            except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
                print(f"[{elapsed:7.1f}s] fetch failed: {exc}")
                time.sleep(args.interval)
                continue

            sensors = sensor_by_id(payload)
            lines = []

            for sensor_id in args.sensors:
                item = sensors.get(sensor_id)
                if not item:
                    lines.append(f"S{sensor_id}: no data yet")
                    continue

                wake = int(item.get("wake", 0))
                has_counters = "received" in item and "missedTotal" in item
                received = int(item.get("received", 0))
                missed_total = int(item.get("missedTotal", 0))

                if sensor_id not in baselines:
                    baselines[sensor_id] = Baseline(
                        wake=wake,
                        received=received,
                        missed_total=missed_total,
                        time_s=elapsed,
                        has_counters=has_counters,
                    )
                    counter_note = "counters" if has_counters else "wake-only"
                    lines.append(f"S{sensor_id}: baseline wake={wake} {counter_note} rssi={item.get('rssi', '--')} dBm")
                    continue

                base = baselines[sensor_id]
                if not (has_counters and base.has_counters):
                    lines.append(
                        f"S{sensor_id}: counters missing; wake={wake} "
                        f"lastMissed={item.get('missed', '--')} rssi={item.get('rssi', '--')} "
                        "flash ESP32 packet-loss-stats build"
                    )
                    continue

                if received < base.received or missed_total < base.missed_total:
                    print(f"sensor {sensor_id}: counters reset (probe/receiver reboot?), rebaselining")
                    base = Baseline(
                        wake=wake,
                        received=received,
                        missed_total=missed_total,
                        time_s=elapsed,
                        has_counters=has_counters,
                    )
                    baselines[sensor_id] = base
                    lines.append(f"S{sensor_id}: rebaselined wake={wake} rssi={item.get('rssi', '--')} dBm")
                    continue

                received_delta = max(0, received - base.received)
                missed_delta = max(0, missed_total - base.missed_total)
                wake_delta_count = wake_delta(wake, base.wake)
                expected = received_delta + missed_delta
                loss_percent = (missed_delta * 100.0 / expected) if expected else None
                elapsed_since_baseline = max(0.0, elapsed - base.time_s)
                observed_period_s = (elapsed_since_baseline / wake_delta_count) if wake_delta_count else None

                period_text = f"period~{observed_period_s:4.1f}s" if observed_period_s else "period~--"
                lines.append(
                    f"S{sensor_id}: wakeDelta={wake_delta_count:4d} recv={received_delta:4d} "
                    f"missed={missed_delta:3d} expected={expected:4d} "
                    f"loss={format_pct(loss_percent)} {period_text} "
                    f"wake={wake} rssi={item.get('rssi', '--')} age={item.get('ageMs', '--')}ms"
                )

                if writer:
                    writer.writerow(
                        {
                            "elapsed_s": f"{elapsed:.3f}",
                            "sensor": sensor_id,
                            "wake": wake,
                            "wake_delta": wake_delta_count,
                            "received_delta": received_delta,
                            "missed_delta": missed_delta,
                            "expected_delta": expected,
                            "loss_percent": "" if loss_percent is None else f"{loss_percent:.4f}",
                            "observed_period_s": "" if observed_period_s is None else f"{observed_period_s:.4f}",
                            "moisture": item.get("moisture", ""),
                            "battery_percent": item.get("batteryPercent", ""),
                            "rssi": item.get("rssi", ""),
                            "age_ms": item.get("ageMs", ""),
                        }
                    )
                    csv_file.flush()

            print(f"[{elapsed:7.1f}s] " + " | ".join(lines))
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if csv_file:
            csv_file.close()

    print("\nFinal summary:")
    try:
        payload = fetch_data(args.url)
        sensors = sensor_by_id(payload)
    except Exception:
        sensors = {}

    for sensor_id, base in baselines.items():
        item = sensors.get(sensor_id)
        if not item:
            continue
        if not ("received" in item and "missedTotal" in item and base.has_counters):
            print(f"  Sensor {sensor_id}: counters missing; flash ESP32 packet-loss-stats build for loss percentage")
            continue

        received_delta = max(0, int(item.get("received", 0)) - base.received)
        missed_delta = max(0, int(item.get("missedTotal", 0)) - base.missed_total)
        expected = received_delta + missed_delta
        loss_percent = (missed_delta * 100.0 / expected) if expected else None
        print(
            f"  Sensor {sensor_id}: received={received_delta}, missed={missed_delta}, "
            f"expected={expected}, loss={format_pct(loss_percent)}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
