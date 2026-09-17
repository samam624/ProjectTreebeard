#!/usr/bin/env python3
"""Scan for MSV probes and report decoded health against MSV3/fleet.json.

Written because "is probe 4 alive?" was being answered by hand-decoding manufacturer hex out of a
generic scan, which is slow and gets the byte offsets wrong. A probe advertises for five seconds
once a minute, so a short scan misses a healthy board far more often than it finds one -- the
default duration here is sized to catch at least two windows.

Two advertisements from the same address are worth much more than one: a single hit proves a
healthy moment, while a rising wake counter proves the board is actually cycling rather than stuck
mid-boot or being re-observed from a stale cache. Use --require-progress when that matters.

A probe running a targeted bring-up image advertises a distinct "MSV3" heartbeat instead of
telemetry. It is reported as DIAGNOSTIC -- never as a reading -- and is a problem on a deployed probe,
because the receiver ignores it and that plant's history stops.

Examples:
  python scripts/msv3_fleet_scan.py                       # whole fleet, 150s
  python scripts/msv3_fleet_scan.py --probe 4 --require-progress
  python scripts/msv3_fleet_scan.py --seconds 300 --json health.json
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from msv3_fleet import (  # noqa: E402
    FleetError,
    any_advertisement_from_bleak,
    load_fleet,
)

# Below this the probe's own OTA power gate is shut and it will refuse a DFU jump.
OTA_MIN_BATTERY_MV = 3800

# Largest believable DATA_CH0 difference between the two electrode-swap phases within one scan --
# MOISTURE_SWING_THRESHOLD in MSV3_Moisture_BLE_Sleep.ino. Healthy boards overlap within a few
# thousand counts; probe 7's cracked TMUX joint put phase 1 about 15,000,000 above phase 0.
PHASE_GAP_LIMIT = 1_200_000


class Sighting:
    def __init__(self, address, name, rssi, advert):
        self.address = address
        self.name = name
        self.first_seen = self.last_seen = datetime.now()
        self.rssis = [rssi]
        self.adverts = [advert]
        self.names = {name} if name else set()

    def add(self, name, rssi, advert):
        self.last_seen = datetime.now()
        self.rssis.append(rssi)
        if name:
            self.names.add(name)
        # Only keep advertisements that move the wake counter -- a scanner reports the same one
        # many times per window and the repeats say nothing about liveness.
        if advert.wake != self.adverts[-1].wake:
            self.adverts.append(advert)

    @property
    def latest(self):
        return self.adverts[-1]

    @property
    def wake_progress(self):
        return self.adverts[-1].wake - self.adverts[0].wake


async def scan(seconds: float) -> dict:
    from bleak import BleakScanner

    sightings: dict[str, Sighting] = {}

    def callback(device, advertisement):
        advert = any_advertisement_from_bleak(advertisement)
        if advert is None:
            return
        name = advertisement.local_name or device.name or ""
        address = device.address.upper()
        if address in sightings:
            sightings[address].add(name, advertisement.rssi, advert)
        else:
            sightings[address] = Sighting(address, name, advertisement.rssi, advert)

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        await asyncio.sleep(seconds)
    finally:
        await scanner.stop()
    return sightings


def report(fleet, sightings, wanted, require_progress) -> tuple[int, list]:
    """Print one block per probe. Returns (exit_code, json-serialisable rows)."""
    by_sensor: dict[int, list[Sighting]] = {}
    for sighting in sightings.values():
        by_sensor.setdefault(sighting.latest.sensor_id, []).append(sighting)

    problems = []
    rows = []

    for probe in sorted(fleet, key=lambda p: p.sensor_id):
        if wanted and probe.sensor_id not in wanted:
            continue
        claimants = by_sensor.get(probe.sensor_id, [])
        print(f"\nprobe {probe.sensor_id}  {probe.name}")
        print(f"  registry     {probe.application_address}  bootloader={probe.bootloader} "
              f"device_type={probe.device_type:#06x} ota_capable={probe.ota_capable}")

        if not claimants:
            print("  NOT SEEN     no advertisement carrying this sensor ID")
            # A bench board that is powered off most of the time tells you nothing by being
            # silent. Only a deployed probe's absence is a finding -- unless it was asked for by
            # name, in which case the caller wants to know either way.
            if probe.deployed or probe.sensor_id in wanted:
                problems.append(f"probe {probe.sensor_id} not seen")
            else:
                print("               (deployed=false in the registry, so this is not a fault)")
            rows.append({"sensor_id": probe.sensor_id, "seen": False,
                         "deployed": probe.deployed})
            continue

        # More than one address claiming an ID is the exact corruption the fleet registry and the
        # flasher's identity preflight exist to prevent -- two probes writing one plant's history.
        if len(claimants) > 1:
            print(f"  DUPLICATE ID {len(claimants)} addresses claim sensor {probe.sensor_id}: "
                  + ", ".join(s.address for s in claimants))
            problems.append(f"sensor {probe.sensor_id} claimed by {len(claimants)} addresses")

        for sighting in claimants:
            advert = sighting.latest
            registered = sighting.address == probe.application_address
            print(f"  address      {sighting.address} "
                  f"{'(matches registry)' if registered else '<-- NOT THE REGISTERED ADDRESS'}")
            if not registered:
                problems.append(
                    f"sensor {probe.sensor_id} seen at unregistered {sighting.address}")
            if sighting.names and probe.name not in sighting.names:
                print(f"  name         {sorted(sighting.names)} <-- expected {probe.name}")
                problems.append(f"probe {probe.sensor_id} advertises an unexpected name")
            else:
                print(f"  name         {probe.name}")

            rssi = sum(sighting.rssis) / len(sighting.rssis)
            print(f"  rssi         {rssi:.0f} dBm mean over {len(sighting.rssis)} packets")
            print(f"  wake         {advert.wake} "
                  f"({len(sighting.adverts)} distinct, +{sighting.wake_progress} during scan)")

            if not advert.wide:
                print(f"  payload      {len(advert.raw_bytes)} bytes -- legacy/narrow format, "
                      f"only magic/sensor/wake decoded")
                rows.append({"sensor_id": probe.sensor_id, "seen": True, "wide": False,
                             "address": sighting.address, "wake": advert.wake})
                continue

            gate = advert.ota_gate_open
            phase_means = {}
            print(f"  battery      {advert.battery_mv} mV"
                  f"{' charging' if advert.charging else ''}")
            if advert.diagnostic:
                print("  image        DIAGNOSTIC bring-up heartbeat ('MSV3' magic) -- not production; "
                      "the receiver ignores it")
                print(f"  flags        {advert.flags:#04x} otaGate={int(bool(gate))} "
                      f"(the bring-up layout defines only bit0 and bit4)")
                print("  DATA_CH0     0 -- heartbeat, no measurement taken" if advert.data_ch0 == 0
                      else f"  DATA_CH0     {advert.data_ch0:,} -- console-requested diagnostic "
                           f"reading, not telemetry")
                if probe.deployed:
                    problems.append(
                        f"probe {probe.sensor_id} is deployed but running a diagnostic image -- no "
                        f"telemetry reaches the receiver")
            else:
                print(f"  flags        {advert.flags:#04x} phase={advert.phase} "
                      f"tempValid={int(bool(advert.temp_valid))} "
                      f"bareValid={int(bool(advert.bare_valid))} otaGate={int(bool(gate))} "
                      f"invalid={int(bool(advert.reading_invalid))} "
                      f"capless={int(bool(advert.capless))}")
                if advert.die_temp_c is not None:
                    print(f"  die temp     {advert.die_temp_c:.2f} C")
                reading = ("INVALID (FDC read failed)" if advert.reading_invalid
                           else f"{advert.data_ch0:,}")
                bare = (f"bare {advert.bare_ch0:,}" if advert.bare_ch0 is not None
                        else "bare invalid")
                print(f"  DATA_CH0     {reading}  {bare}")

                # C17 was removed from every MSV3 on 2026-09-14 (docs/KNOWN_ISSUES.md), so a
                # production packet without bit6 is capless hardware on a pre-capless image -- one
                # that still selects the disconnected mux 2/3 and invents a reference reading.
                if not advert.capless:
                    print("  CAPLESS      bit6 clear -- pre-capless image on C17-removed hardware; "
                          "install the capless build")
                    problems.append(f"probe {probe.sensor_id} runs a pre-capless image")

                production = [a for a in sighting.adverts if not a.diagnostic]
                invalid = sum(1 for a in production if a.reading_invalid)
                if invalid:
                    print(f"  INVALID      {invalid} of {len(production)} readings flagged bit5")
                    problems.append(
                        f"probe {probe.sensor_id} sent {invalid} invalid reading(s)")

                by_phase: dict[int, list[int]] = {}
                for a in production:
                    if not a.reading_invalid:
                        by_phase.setdefault(a.phase, []).append(a.data_ch0)
                phase_means = {p: sum(v) / len(v) for p, v in sorted(by_phase.items())}
                if len(phase_means) == 2:
                    gap = abs(phase_means[0] - phase_means[1])
                    print(f"  phases       0: {phase_means[0]:,.0f} (n={len(by_phase[0])})  "
                          f"1: {phase_means[1]:,.0f} (n={len(by_phase[1])})  gap {gap:,.0f}")
                    if gap > PHASE_GAP_LIMIT:
                        print(f"  PHASE GAP    above {PHASE_GAP_LIMIT:,} -- a phase-dependent "
                              f"analog-path fault, unless the pot was watered during the scan")
                        problems.append(
                            f"probe {probe.sensor_id} phases differ by {gap:,.0f} counts")
                elif phase_means:
                    print(f"  phases       only phase {next(iter(phase_means))} seen -- scan "
                          f"longer to compare both")

            # bit4 is only ever SET by OTA-aware firmware, so a clear bit well above the gate
            # threshold means the firmware has no OTA feature rather than a shut gate.
            if not gate and advert.battery_mv and advert.battery_mv >= OTA_MIN_BATTERY_MV:
                print(f"  OTA          UNAVAILABLE -- bit4 clear at {advert.battery_mv} mV, above "
                      f"the {OTA_MIN_BATTERY_MV} mV gate. This is pre-OTA firmware, not a shut "
                      f"gate.")
                if probe.ota_capable:
                    problems.append(
                        f"probe {probe.sensor_id} is registered ota_capable but advertises "
                        f"pre-OTA firmware")
            elif not gate:
                print(f"  OTA          gate shut -- {advert.battery_mv} mV is below "
                      f"{OTA_MIN_BATTERY_MV} mV; a DFU jump would be refused")
            else:
                print("  OTA          gate open, would accept a DFU jump")
                if not probe.ota_capable:
                    problems.append(
                        f"probe {probe.sensor_id} advertises OTA-capable firmware but the registry "
                        f"says ota_capable=false -- update MSV3/fleet.json")

            if require_progress and sighting.wake_progress <= 0:
                print("  PROGRESS     wake counter did not advance during the scan")
                problems.append(f"probe {probe.sensor_id} wake counter did not advance")

            rows.append({
                "sensor_id": probe.sensor_id, "seen": True, "wide": True,
                "diagnostic": advert.diagnostic,
                "address": sighting.address, "name": sorted(sighting.names),
                "wake": advert.wake, "wake_progress": sighting.wake_progress,
                "battery_mv": advert.battery_mv, "flags": advert.flags,
                "ota_gate_open": gate, "die_temp_c": advert.die_temp_c,
                "capless": advert.capless, "reading_invalid": advert.reading_invalid,
                "data_ch0": advert.data_ch0, "bare_ch0": advert.bare_ch0,
                "phase_means": {str(p): round(m) for p, m in phase_means.items()},
                "rssi_mean": round(rssi, 1), "packets": len(sighting.rssis),
                "raw": advert.raw_bytes.hex(),
            })

    unregistered = [s for s in sightings.values() if fleet.by_address(s.address) is None]
    if unregistered:
        print("\nnot in the registry:")
        for sighting in unregistered:
            advert = sighting.latest
            print(f"  {sighting.address}  {sighting.name or '(no name)'}  "
                  f"sensor={advert.sensor_id} wake={advert.wake} "
                  f"{len(advert.raw_bytes)}-byte payload"
                  f"{' (diagnostic heartbeat)' if advert.diagnostic else ''}")
        print("  (MSV1/MSV2-generation nodes are expected here; an unexpected MSV3 is not)")

    print()
    if problems:
        print(f"{len(problems)} problem(s):")
        for problem in problems:
            print(f"  - {problem}")
        return 1, rows
    print("all requested probes healthy")
    return 0, rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=150.0,
                        help="Scan duration. Probes advertise 5s per 60s, so the default catches "
                             "at least two windows (default 150)")
    parser.add_argument("--probe", type=int, action="append", dest="probes",
                        help="Only report this sensor ID; repeatable")
    parser.add_argument("--require-progress", action="store_true",
                        help="Fail unless each probe's wake counter advances during the scan")
    parser.add_argument("--json", type=Path, help="Also write the decoded rows here")
    parser.add_argument("--fleet", type=Path, help="Registry path (default MSV3/fleet.json)")
    args = parser.parse_args()

    try:
        fleet = load_fleet(args.fleet)
    except FleetError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    wanted = set(args.probes or [])
    print(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] scanning {args.seconds:.0f}s for "
          f"{'probe(s) ' + ','.join(map(str, sorted(wanted))) if wanted else 'the whole fleet'}...",
          flush=True)
    sightings = asyncio.run(scan(args.seconds))
    code, rows = report(fleet, sightings, wanted, args.require_progress)

    if args.json:
        args.json.write_text(json.dumps(
            {"scanned_at": datetime.now().isoformat(timespec="seconds"),
             "seconds": args.seconds, "probes": rows}, indent=2), encoding="utf-8")
        print(f"wrote {args.json}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
