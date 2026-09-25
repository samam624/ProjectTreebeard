#!/usr/bin/env python
"""Read the MSV3 bootloader's retained DFU trace out of its BLE scan response.

WHY THIS EXISTS, AND WHY IT IS PASSIVE
--------------------------------------
A failed wired (USB CDC) DFU leaves a probe with its application already erased and its COM port
gone. There is no console to ask and, on a conformally coated probe, no SWD either. The bootloader
therefore keeps a small decision record in a RAM region the linker excludes from every section, so
it survives the reset that ends the session, and republishes it as scan-response manufacturer data.

A PASSIVE scan reads it. That matters: the failure under investigation is in the USB endpoint, and
opening a GATT connection to ask would perturb the very thing being measured. Nothing here connects.

READ THE NONCE FIRST. It is the only field deliberately not cleared when a record is armed, so it
increments once per DFU session. On 2026-09-20 four different bootloaders were read back
byte-identical and taken as "the fix did nothing"; in fact the record had never been re-armed and
every one of those four readings was stale. If the nonce has not moved since the previous read,
nothing below it is evidence.

LAYOUT IS AN ABI. It is written by scan_response_init() in
lib/sdk11/components/libraries/bootloader_dfu/dfu_transport_ble.c. Change one side and change this.
"""
from __future__ import annotations

import argparse
import asyncio
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from msv3_fleet import FleetError, load_fleet, normalise_address  # noqa: E402

# bleak hands back manufacturer data already split on the 2-byte company identifier, which it uses
# as the dict key. Every offset below is therefore relative to payload[2] in the firmware, not
# payload[0]. Getting this wrong shifts every field by two and still decodes to plausible numbers,
# which is how an earlier reader reported a transport byte that was really half of a nonce.
COMPANY_ID = 0xFFFF

TRACE_LEN = 26  # a full 1.0.13 record, company id already stripped
BARE_LEN = 5  # build_id + app_crc + trace_valid=0, published when no record is armed

STAGE = {
    0x01: "ARMED -- START was processed, nothing after it was",
    0x02: "VALIDATE_ENTER -- died inside dfu_image_validate()",
    0x03: "VALIDATE_RETURN",
    0x04: "ACTIVATE_ENTER -- died inside dfu_image_activate()",
    0x05: "ACTIVATE_RETURN",
}

# bootloader_types.h: NONE 0x00, SERIAL 0x01, BLE 0x02. Do not guess this -- an inverted copy of
# this table read a serial-transport record as a BLE one on 2026-09-21 and very nearly produced the
# opposite conclusion about which transport had failed.
TRANSPORT = {0x00: "none", 0x01: "serial/USB CDC", 0x02: "BLE"}

ALLOC_ERR = {
    0x00: "NRF_SUCCESS",
    0x04: "NRF_ERROR_NO_MEM -- the DFU packet queue was full",
    0x07: "NRF_ERROR_INVALID_PARAM -- the packet-type byte was rejected outright",
    0x08: "NRF_ERROR_INVALID_STATE",
    0xFF: "never failed",
}

# bootloader_types.h MSV3_RESET_TAG_*. 0 means none of the tagged sites fired, which by
# elimination means APP_ERROR_CHECK -- the SDK error handler -- reset the device.
RESET_TAG = {0: "none of the tagged sites -> APP_ERROR_CHECK reset it",
             1: "guard at START: wrong flash configuration",
             2: "guard at INIT: wrong flash configuration",
             3: "lease: VBUS lost",
             4: "lease: serial went idle"}

RESETREAS = {0x00: "none recorded", 0x01: "RESETPIN", 0x02: "DOG (watchdog)",
             0x04: "SREQ (soft reset)", 0x08: "LOCKUP"}

# The known-good application's first flash page, and an erased one. Measured on probe 8, 2026-09-20.
APP_CRC_KNOWN = {0x163C: "a good application is present", 0x0FE1: "the page is ERASED"}


EXTRACT_RC = {0x00: "NRF_SUCCESS", 0x04: "NRF_ERROR_NO_MEM -- the pool had nothing to give",
              0xEE: "not recorded"}

ALL_ZERO = bytes(4)


def decode(raw: bytes) -> dict:
    """Decode one manufacturer-data payload. Returns {} if it is not one of ours.

    1.0.13 and 1.0.14 both publish 26 visible bytes, so LENGTH cannot tell them apart. Dispatch on
    build_id, which is the first field of every layout precisely so that this stays possible.
    """
    if len(raw) == BARE_LEN:
        build, app_crc, valid = struct.unpack_from("<HHB", raw, 0)
        return {"bare": True, "build_id": build, "app_crc": app_crc, "trace_valid": bool(valid)}
    if len(raw) < TRACE_LEN:
        return {}
    build, app_crc, valid = struct.unpack_from("<HHB", raw, 0)
    out = {"bare": False, "build_id": build, "app_crc": app_crc, "trace_valid": bool(valid)}

    if build >= 0x010E:  # 1.0.14: the buffer-identity layout
        nonce8, tr_stage = struct.unpack_from("<BB", raw, 5)
        (good_c, good_x, fail_c, fail_hdr0, fail_ord,
         fail_rc, fail_x, fail_xhdr0) = struct.unpack_from("<BBBBBBBB", raw, 7)
        raw_len, adj_len = struct.unpack_from("<HH", raw, 15)
        idx, pool_avail = struct.unpack_from("<BH", raw, 23)
        out.update({
            "layout": "1.0.14", "nonce8": nonce8,
            "transport": tr_stage & 0x0F, "stage": (tr_stage >> 4) & 0x0F,
            # 1.0.23 reclaimed the buffer-identity baseline bytes; the first now carries
            # reset_tag. THIRD byte in this layout to be reused, hence the build_id keying --
            # hand-maintaining two parallel layouts has produced four mislabelled readings.
            "good_completed_slot": None if build >= 0x0117 else good_c,
            "good_extracted_slot": None if build >= 0x0117 else good_x,
            "reset_tag": good_c if build >= 0x0117 else None,
            # THIS BYTE HAS CHANGED MEANING TWICE, so it is keyed on build_id and never on a
            # guess. 1.0.14-1.0.17 published fail_completed_hdr0 here; the withdrawn 1.0.18-1.0.20
            # reused it for init_retry; 1.0.21+ is back to fail_completed_hdr0. Reading a withdrawn
            # build's label against a 1.0.21 record printed "the erase race was hit AND SURVIVED"
            # from a 0xEE sentinel -- a confident sentence about a field that did not exist.
            "fail_completed_slot": fail_c,
            "init_retry": fail_hdr0 if 0x0112 <= build <= 0x0114 else None,
            "fail_completed_hdr0": None if 0x0112 <= build <= 0x0114 else fail_hdr0,
            "fail_ordinal": fail_ord, "fail_extract_rc": fail_rc,
            # 1.0.22 reuses this byte for RESETREAS; before that it was fail_extracted_hdr0.
            "fail_extracted_slot": fail_x,
            "fail_extracted_hdr0": None if build >= 0x0116 else fail_xhdr0,
            "reset_reas": fail_xhdr0 if build >= 0x0116 else None,
            "fail_raw_length": raw_len, "fail_adj_length": adj_len,
            "fail_payload": bytes(raw[19:23]),
            "pool_read_index": idx & 0x0F, "pool_write_index": (idx >> 4) & 0x0F,
            "pool_read_avail": pool_avail & 0x1F,
            "pool_free_avail": (pool_avail >> 5) & 0x1F,
            "pool_free_window": (pool_avail >> 10) & 0x1F,
        })
        return out

    # 1.0.13 and earlier: the transmit-path layout.
    nonce8, transport, stage, flags = struct.unpack_from("<BBBB", raw, 5)
    alloc_type, alloc_rc8, reset8, seq_mm, produce_nomem = struct.unpack_from("<BBBBB", raw, 9)
    rx_seen, queued, dropped, tx_block, tx_resume, ack_done = struct.unpack_from("<HHHHHH", raw, 14)
    out.update({
        "layout": "1.0.13", "nonce8": nonce8, "transport": transport, "stage": stage,
        "stop_seen": bool(flags & 0x01), "stop_processed": bool(flags & 0x02),
        "sched_fail": bool(flags & 0x04), "produce_nomem_flag": bool(flags & 0x08),
        "alloc_fail_type": alloc_type, "alloc_fail_rc": alloc_rc8, "reset_reas": reset8,
        "seq_mismatch": seq_mm, "produce_nomem": produce_nomem,
        "rx_seen": rx_seen, "frames_queued": queued, "frames_dropped": dropped,
        "tx_block_count": tx_block, "tx_resume_count": tx_resume, "ack_frames_done": ack_done,
    })
    return out


def report_1014(t: dict) -> None:
    """The single question this build exists to answer: was it the same buffer?"""
    print(f"  nonce (low 8)    {t['nonce8']}   <-- MUST differ from your previous read")
    print(f"  transport        {t['transport']} = {TRANSPORT.get(t['transport'], '?')}")
    print(f"  stage            {t['stage']} = {STAGE.get(t['stage'], '?')}")

    r = t["init_retry"]
    if r is not None:
        print(f"  init_retry       {r}   INIT packets deferred while the app-bank erase finished"
              + ("   <-- the erase race was hit" if r else "   (no deferral needed)"))

    # 1.0.22 put RESETREAS back: with the board dying inside a multi-second blocking erase, DOG
    # (watchdog) versus SREQ (deliberate) is the whole question and nothing else answers it.
    if t.get("reset_tag") is not None:
        tag = t["reset_tag"]
        print(f"  reset_tag        {tag} = {RESET_TAG.get(tag, 'unknown site')}")

    if t.get("reset_reas") is not None:
        rr = t["reset_reas"]
        print(f"  RESETREAS        0x{rr:02X} = {RESETREAS.get(rr, 'multiple/other')}"
              + ("   <-- THE WATCHDOG FIRED" if rr == 0x02 else ""))

    if t["fail_ordinal"] == 0:
        print("  --- NO FRAME WAS REJECTED. The write-once latch never fired. ---")
    else:
        gc, gx = t["good_completed_slot"], t["good_extracted_slot"]
        fc, fx = t["fail_completed_slot"], t["fail_extracted_slot"]
        print("  --- BUFFER IDENTITY ---")
        print(f"  last GOOD frame   SLIP filled slot {gc}  ->  rx_extract returned slot {gx}"
              f"   {'MATCH' if gc == gx else '*** MISMATCH ***'}")
        print(f"  first BAD frame   SLIP filled slot {fc}  ->  rx_extract returned slot {fx}"
              f"   {'MATCH' if fc == fx else '*** MISMATCH ***'}   (received frame #{t['fail_ordinal']})")
        if fc != fx:
            print("    >>> THE HANDOFF IS THE FAULT. The DFU transport classified a buffer that is")
            print("        not the one hci_transport validated. Everything else is downstream.")
        xh = t["fail_extracted_hdr0"]
        print(f"  extracted hdr0   0x{xh:02X}" + ("   (deliberately not dereferenced)" if xh == 0xEE
                                                  else f"   (HCI seq {xh & 0x07})"))
        rc = t["fail_extract_rc"]
        print(f"  extract_rc       0x{rc:02X} = {EXTRACT_RC.get(rc, 'other NRF error')}")
        print(f"  length raw       {t['fail_raw_length']}   (before the unconditional -6)")
        adj = t["fail_adj_length"]
        print(f"  length adjusted  {adj}" + ("   <-- 0xFFFA: extraction FAILED and the pointer and "
                                             "length were adjusted anyway" if adj == 0xFFFA
                                             else "   (516 = a full DATA frame)"))
        p = t["fail_payload"]
        print(f"  PAYLOAD[0:4]     {p.hex(' ')}")
        if p == ALL_ZERO:
            print("    >>> ALL ZERO: a buffer nobody ever wrote -- produced, never filled.")
        elif p[0] == 0 and p[3] in (1, 3, 4, 5):
            print("    >>> BYTE-REVERSED: an endianness or alignment slip, not a lost buffer.")
        elif p[0] in (1, 3, 4, 5):
            print(f"    >>> INTACT (type 0x{p[0]:02X}). The type byte was fine; look elsewhere.")
        else:
            print("    >>> GARBAGE: neither zero nor byte-reversed, so genuine corruption.")

    # The pool fields are only latched when a frame is rejected. Un-latched they hold the arm()
    # sentinel 0xFF, which the scan-response bit-packing truncates to 15 and 31 -- values that look
    # like a badly drifted ring. Say "not recorded" instead of inventing a fault: a reader that
    # cries wolf on its own sentinels is how an inverted constant cost a build cycle earlier today.
    latched = t["fail_ordinal"] != 0 and not (
        t["pool_read_index"] == 15 and t["pool_write_index"] == 15
        and t["pool_read_avail"] == 31 and t["pool_free_avail"] == 31
        and t["pool_free_window"] == 31)
    print("  --- pool, latched at the moment of extraction ---")
    if not latched:
        print("  not recorded (the pool is only latched on a rejection, and none happened)")
        return
    print(f"  read_index {t['pool_read_index']}   write_index {t['pool_write_index']}"
          f"   read_avail {t['pool_read_avail']}   free_avail {t['pool_free_avail']}"
          f"   free_window {t['pool_free_window']}")
    if t["pool_read_index"] != (t["pool_write_index"] - 1) % 16:
        print("    NOTE: read_index is not one behind write_index -- the ring has drifted, which is")
        print("          exactly what would hand the DFU layer a buffer SLIP has not filled yet.")


def report(address: str, t: dict) -> None:
    print(f"\n{address}")
    crc_note = APP_CRC_KNOWN.get(t["app_crc"], "unrecognised -- neither the known-good app nor an erased page")
    print(f"  build_id         0x{t['build_id']:04X}")
    print(f"  app page CRC16   0x{t['app_crc']:04X}   {crc_note}")
    if t["bare"] or not t["trace_valid"]:
        print("  NO TRACE RECORD ARMED.")
        print("    This does NOT mean the application is missing -- read the CRC above, which is")
        print("    published unconditionally for exactly this reason. A probe parked in DFU by the")
        print("    1200-baud touch, with its application intact, looks like this.")
        return

    if t.get("layout") == "1.0.14":
        report_1014(t)
        return

    print(f"  nonce (low 8)    {t['nonce8']}   <-- MUST differ from your previous read")
    print(f"  transport        {t['transport']} = {TRANSPORT.get(t['transport'], '?')}")
    print(f"  stage            {t['stage']} = {STAGE.get(t['stage'], '?')}")
    print("  --- receive path ---")
    print(f"  rx_seen          {t['rx_seen']}      frames delivered to the DFU transport")
    print(f"  frames_queued    {t['frames_queued']}")
    print(f"  frames_dropped   {t['frames_dropped']}")
    rc = t["alloc_fail_rc"]
    print(f"  alloc_fail_rc    0x{rc:02X} = {ALLOC_ERR.get(rc, 'unknown NRF error')}")
    print(f"  alloc_fail_type  0x{t['alloc_fail_type']:02X}   the packet-type byte rejected"
          f"{'  (0x00 = INVALID_PACKET)' if t['alloc_fail_type'] == 0 else ''}")
    print(f"  STOP seen {int(t['stop_seen'])}   STOP processed {int(t['stop_processed'])}"
          f"   sched_fail {int(t['sched_fail'])}")
    print("  --- transmit path (1.0.13) ---")
    print(f"  tx_block_count   {t['tx_block_count']}   times the CDC TX FIFO refused a byte")
    print(f"  tx_resume_count  {t['tx_resume_count']}   times hci_slip_tx_pump() finished a stalled frame")
    print(f"  ack_frames_done  {t['ack_frames_done']}   acknowledgements that actually reached the wire")
    if t["ack_frames_done"] < t["rx_seen"]:
        print(f"    SHORT BY {t['rx_seen'] - t['ack_frames_done']}: frames were received and never"
              " answered. The host was left waiting.")
    else:
        print("    Every received frame was answered.")
    mm = t["seq_mismatch"]
    if mm == 0xFF:
        print("  seq mismatch     none -- the host never lost sequence")
    else:
        print(f"  seq mismatch     first: expected {mm >> 4}, received {mm & 0x0F}"
              "   (the host reset its counter after an unanswered frame)")
    print(f"  produce_nomem    {t['produce_nomem']}   hci_mem_pool_rx_produce() NO_MEM"
          f"{'  -- SLIP fell back to the 5-byte ACK buffer' if t['produce_nomem'] else ''}")
    print(f"  RESETREAS        0x{t['reset_reas']:02X} = {RESETREAS.get(t['reset_reas'], 'multiple/other')}")


async def scan(addresses: set[str], seconds: float) -> dict[str, bytes]:
    try:
        from bleak import BleakScanner
    except ImportError:
        raise SystemExit(
            "bleak is not importable. Use the portable interpreter:\n"
            "  MSV3\\.arduino-build\\python-portable\\runtime\\python.exe "
            "MSV3\\scripts\\msv3_read_dfu_trace.py --probe N")

    found: dict[str, bytes] = {}

    def cb(device, adv):
        addr = normalise_address(device.address)
        if addr not in addresses:
            return
        for cid, data in (adv.manufacturer_data or {}).items():
            if cid == COMPANY_ID and len(data) >= BARE_LEN:
                found[addr] = bytes(data)

    scanner = BleakScanner(detection_callback=cb, scanning_mode="active")
    await scanner.start()
    await asyncio.sleep(seconds)
    await scanner.stop()
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--probe", type=int, required=True,
                    help="fleet sensor id. Both its application and recovery addresses are watched, "
                         "because a probe parked in DFU advertises at the recovery address.")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--fleet", type=Path, default=None)
    args = ap.parse_args()

    try:
        probe = load_fleet(args.fleet).by_sensor_id(args.probe)
    except FleetError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    addresses = {normalise_address(a) for a in probe.all_addresses}
    print(f"probe {probe.sensor_id}: passively scanning {', '.join(sorted(addresses))} "
          f"for {args.seconds:g}s")
    found = asyncio.run(scan(addresses, args.seconds))
    if not found:
        print("\nNo advertisement seen. The probe may be asleep, unpowered, or out of range.")
        return 1
    for address, raw in sorted(found.items()):
        decoded = decode(raw)
        if not decoded:
            print(f"\n{address}: {len(raw)}-byte payload does not match the trace layout: {raw.hex()}")
            continue
        report(address, decoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
