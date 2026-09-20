#!/usr/bin/env python3
"""One-shot MSV3 OTA: jump a running application into its bootloader, then transfer.

Replaces the two-tool dance of `nrf_dfu_py/dfu_cli.py` (jump) plus `ble_dfu_recover.py`
(transfer). Both of the problems that made the old path unusable on Windows live in
dfu_cli.py, and neither is fixable from the command line:

1. **dfu_cli reports a jump it never performed.** `dfu_lib.jump_to_bootloader()` wraps its
   control-point write in `except Exception: pass` and then logs "Jump command sent."
   unconditionally. A write the firmware actively rejected -- `ResetSafeBLEDfu` replies
   `WRITE_NOT_PERMITTED` whenever its power gate is shut, and `CPS_CCCD_CONFIG_ERROR` if
   notifications are not enabled first -- is indistinguishable from a real one in its output.
   This script reports what actually happened, and treats "bootloader appeared" as the only
   proof of success.

2. **dfu_cli has no WinRT workaround.** `ble_dfu_recover.py` runs the Windows BLE stack from
   an STA thread with a live message pump and binds BleakClient to the RANDOM address type;
   dfu_cli does none of that. On 2026-09-03 that cost 28 seconds between "Connected" and the
   control-point write -- against a firmware OTA hold of 30 s (OTA_CONNECTED_HOLD_MS), which
   is sized for "connect + service discovery + ONE write". The probe dropped the idle peer
   mid-write and the bootloader never came up. Same WinRT setup, same radio, same board: the
   recover script finds and talks to `AdaDFU` in well under a second.

Speed: the transfer runs with a negotiated MTU by default, so `dfu_lib` uses
`chunk_size = min(mtu - 3, 244)` instead of the 20-byte floor `_setup_mtu()` returns when
`high_mtu` is false -- note it returns that 23 without ever asking the link, which is why every
earlier transfer was pinned to 20-byte chunks regardless of what the connection supported.

Pass --no-high-mtu to fall back to the 20-byte path proven on 2026-09-02 if a board turns out not
to tolerate a larger MTU.

FLOW CONTROL. Upstream dfu_lib clears its packet-receipt event after sending the triggering packet
and, on a receipt timeout, logs "PRN Timeout, continuing anyway" and keeps streaming -- so a late
receipt can satisfy the wrong window and the transfer proceeds with flow control already lost. At
the old 20-byte chunk size that path produced eight timeouts and an image that failed Receive
Firmware Image with status 6. dfu_lib is a pinned third-party checkout under an ignored directory
and cannot be edited, so `ReceiptGuard` below enforces the abort instead: once synchronisation is
lost the next write raises rather than going out, so a poisoned attempt cannot reach Validate or
Activate.

It is deliberately narrow about what counts as a loss, because `CancelledError` is not a reliable
timeout identity -- task cancellation and disconnect cleanup raise it too:

  * State is per-attempt, cleared by a Start DFU. A Start seen while a transfer is already armed is
    treated as a fault instead, so it cannot wipe a poisoned state mid-image.
  * Detection is armed only between Receive Firmware Image and Validate. Connection- and
    init-phase cancellations therefore cannot poison the transfer -- one did, on a live probe,
    before this was narrowed.
  * A cancellation is ignored when a receipt for exactly the current offset already arrived. That
    is the clear()-after-receipt race: upstream clears the event after sending the triggering
    packet, so a receipt that beat the clear would otherwise look like a timeout.
  * The receipt's reported byte count is compared against what has actually been sent. Upstream
    parses that value and discards it, which makes a stale, duplicate or wrong-offset receipt
    indistinguishable from a correct one; comparing it is what makes those detectable.

NOT claimed: that this makes activating a corrupted image impossible. It narrows the host-side
window. The real integrity boundary is the bootloader's CRC and P-256 signature over the stored
image, plus DEFAULT_TO_OTA_DFU so an invalid application falls back to BLE recovery rather than
bricking.

Deterministic injection tests DO now exist: `scripts/test_receipt_guard.py` covers dropped,
delayed, early, stale, duplicate and wrong-offset receipts against the guard's state machine, and
`scripts/test_ota_preflight.py` covers the mutated-image, wrong-key and corrupt-signature negative
cases. What is still outstanding is the hardware half -- a transfer deliberately interrupted on a
real board (adoption sequence item 6).

WHERE THE TIME ACTUALLY GOES (--profile, 134,432 byte image = 551 image packets of 244 bytes plus
the init packet and size payload = 553 writes to the packet characteristic, 2026-09-03):

    data packet writes  n=553  sum=0.47s  p50=0.8ms   <- host submission, negligible
    control writes      n=8    sum=1.02s  p50=120ms   <- start/init/PRN/receive/validate/activate
    receipt waits       n=34   sum=13.8s  p50=407ms   <- essentially the whole transfer cost
    other                      ~15s                   <- jump, discovery, connect, activate

The per-packet cost is flat at roughly 26-31 ms regardless of PRN (29ms at PRN 4, 25ms at 16, 31ms
at 32), which means the constraint is packet delivery to the bootloader -- about one 244-byte
packet per connection interval -- and NOT receipt round-trips. So PRN tuning has a floor:

    PRN 4    receipts 19.9s
    PRN 16   receipts 15.0s   <- default
    PRN 32   receipts 18.5s   (worse; longer stalls, no fewer packets)

**Practical transfer-phase ceiling on this bootloader/WinRT path is about 8-10 kB/s, i.e. ~16 s for
a 131 KB image.** This is not a measured connection-interval claim -- the PPPC characteristic was
never read as raw bytes and none is needed for the conclusion. Going faster needs more than one
packet per connection event, which means shorter connection intervals compiled into the bootloader,
and installing a bootloader needs SWD. Not worth it: the transfer is already a small part of the
wall clock. Total run time varies 31-41 s mostly because the application only opens a 5 s OTA
advertising window per minute, so how long the client waits to catch one dominates everything else.

Signal matters more than parameters. Bench board at -55 dBm: 28 s / 4.8 kB/s. Fleet probe 4 in the
field at -74 dBm: 47 s / 2.75 kB/s for the same class of image, with one control write (activate)
taking 9.8 s. Same code, same settings -- expect roughly half the throughput at the weaker end.

TARGETING AND THE IDENTITY PREFLIGHT (2026-09-04). Targeting is by ADDRESS, deliberately: boards
are re-identified in place by editing SENSOR_ID in MSV3_Moisture_BLE_Sleep.ino, so a name-based
scan can jump the wrong board into its bootloader and take a live sensor offline.

An address alone was not enough either. Nothing connected the address you typed to the identity
compiled into the package, and on 2026-09-04 both stale packages sitting in the build directories
contained MSV3-MOISTURE-004 while the committed source said 005 -- an ordinary leftover from a
reverted local edit. Flashing one to probe 5 would have given two probes the same sensor ID and
silently mixed two plants' history in the receiver, with no error anywhere and no way to see it
over the radio.

So `--probe N` now resolves through MSV3/fleet.json, and every run checks, BEFORE any jump or
write:

  * the package contains exactly one MSV3-MOISTURE-NNN identity, read out of the application
    IMAGE (the firmware's own advertised name, derived from SENSOR_ID at compile time) rather than
    out of the ZIP manifest, which is unsigned metadata;
  * that identity is the probe being targeted;
  * the address is one the registry lists for that probe;
  * the package's device type matches the bootloader actually installed on it (probe 5 is still on
    stock 0x0052; probe 4 and the bench board are on MSV3's 0x3356);
  * the package's signature verifies against the public key compiled into that bootloader release,
    the hash and length match, and the application version is not the unset 0xFFFFFFFF the raw
    Arduino wrapper emits;
  * and, when the application is actually observed on air, that its advertised sensor ID and name
    agree with all of the above.

A jump is the point of no return for availability -- it takes a live sensor off the air for the
length of a transfer -- so every one of these fails before it, with nothing written.

`--allow-identity-mismatch` is the deliberate escape hatch for the bench board, which runs an
uncommitted identity. It relaxes ONLY the identity comparisons. Signature, device type, image
integrity, and the registry's address list are still enforced under it.

Usage:
    python scripts/ble_ota_update.py <signed-app.zip> --probe 4
    python scripts/ble_ota_update.py <signed-app.zip> --app-address DB:AD:4D:9D:21:BA

Add --profile to time where a transfer actually spends its wall clock: the per-write await sum and
distribution versus the packet-receipt wait sum. Tune PRN or chunking only after that partition is
known -- if the writes account for nearly all of it, PRN is not the constraint and the ceiling is
the host BLE stack.
"""

import argparse
import asyncio
import ctypes
import functools
import logging
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from msv3_fleet import (  # noqa: E402
    FleetError,
    any_advertisement_from_bleak,
    check_boot_guard_pairing,
    load_fleet,
    load_release_public_key,
    normalise_address,
    package_predates_identity_contract,
    read_package,
    verify_package_integrity,
)

# Lives at the repository root, not under MSV3/, because it is the flashing tool for the whole
# project rather than an MSV3 implementation detail. The DFU client and its pinned dependencies
# still live under MSV3/.arduino-build/ (gitignored) -- see the handoff doc for how to recreate it.
REPO_ROOT = Path(__file__).resolve().parents[1]
MSV3_ROOT = REPO_ROOT / "MSV3"
DFU_CLIENT = MSV3_ROOT / ".arduino-build" / "nrf_dfu_py"

OP_CODE_START_DFU = 0x01
OP_CODE_RECEIVE_FIRMWARE_IMAGE = 0x03
OP_CODE_VALIDATE = 0x04
OP_CODE_PACKET_RECEIPT_NOTIF = 0x11
OP_CODE_ENTER_BOOTLOADER = 0x01
UPLOAD_MODE_APPLICATION = 0x04
DFU_SERVICE_UUID = "00001530-1212-efde-1523-785feabcd123"

# Device Information Service firmware revision. Since 2026-09-04 the production application
# publishes FIRMWARE_BUILD_ID here, which is the only way to ask a coated probe what it is running:
# the advertisement has no spare bytes, serial logging is off in production, and the bootloader
# never persists the DFU package's application version.
DIS_FIRMWARE_REVISION_UUID = "00002a26-0000-1000-8000-00805f9b34fb"

# Mirrors OTA_MIN_BATTERY_MV in MSV3_Moisture_BLE_Sleep.ino. Below this the probe's own
# power gate refuses the DFU write, so there is no point jumping it.
OTA_MIN_BATTERY_MV = 3800

# Nordic legacy DFU control-point response codes.
DFU_STATUS_SUCCESS = 1
DFU_STATUS_INVALID_STATE = 2

from msv3_fleet import IDENTITY_CONTRACT_MIN_APP_VERSION  # noqa: E402


def progress(percent):
    print(f"\rTransfer: {percent:3d}%", end="", flush=True)
    if percent == 100:
        print()


async def pump_windows_messages():
    """Keep WinRT callbacks moving while the update runs in a Windows STA."""
    from ctypes import wintypes

    message = wintypes.MSG()
    user32 = ctypes.windll.user32
    while True:
        while user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(message))
            user32.DispatchMessageW(ctypes.byref(message))
        await asyncio.sleep(0.005)


def install_windows_ble_workarounds(address_type):
    """The STA + message-pump + RANDOM-address binding that ble_dfu_recover.py proved out.

    Without this, WinRT async completions stall for tens of seconds, which is longer than the
    probe's OTA connected-hold window.
    """
    if sys.platform != "win32":
        return None

    coinit_apartment_threaded = 0x2
    ctypes.windll.ole32.CoInitializeEx.restype = ctypes.c_long
    hresult = ctypes.windll.ole32.CoInitializeEx(None, coinit_apartment_threaded)
    if hresult not in (0, 1):  # S_OK or S_FALSE
        raise ctypes.WinError(hresult)

    from bleak.backends.winrt.util import allow_sta

    allow_sta()

    if address_type == "auto":
        return asyncio.create_task(pump_windows_messages())

    from bleak.backends.winrt.client import BleakClientWinRT
    from winrt.windows.devices.bluetooth import BluetoothAddressType, BluetoothLEDevice
    from winrt.windows.devices.enumeration import DeviceInformation, DeviceInformationKind

    original_create_requester = BleakClientWinRT._create_requester

    async def create_requester_with_device_id(client, bluetooth_address):
        try:
            return await original_create_requester(client, bluetooth_address)
        except OSError as address_error:
            resolved = (
                BluetoothAddressType.RANDOM
                if address_type == "random"
                else BluetoothAddressType.PUBLIC
            )
            selector = BluetoothLEDevice.get_device_selector_from_bluetooth_address_with_bluetooth_address_type(
                bluetooth_address,
                resolved,
            )
            devices = await DeviceInformation.find_all_async_with_kind_aqs_filter_and_additional_properties(
                selector,
                (),
                DeviceInformationKind.ASSOCIATION_ENDPOINT,
            )
            for device_info in devices:
                try:
                    requester = await BluetoothLEDevice.from_id_async(device_info.id)
                except OSError:
                    continue
                if requester is not None:
                    print("WinRT raw-address open failed; connected through discovered device ID.", flush=True)
                    return requester
            raise address_error

    BleakClientWinRT._create_requester = create_requester_with_device_id
    return asyncio.create_task(pump_windows_messages())


class ReceiptGuard:
    """Turns dfu_lib's "PRN Timeout, continuing anyway" into an abort.

    Upstream clears its receipt event, waits, and on timeout logs and carries on with
    `packets_since_prn` reset. That silently discards a receipt that arrives late and lets the next
    window be satisfied by a stale one, so the client can keep streaming with flow control already
    lost. The bootloader's CRC and signature are then the only thing standing between that and a
    bad image -- they are a last line of defence, not a reason to keep sending after desync.

    dfu_lib cannot be edited (it is a pinned third-party checkout under an ignored directory), so
    the abort is enforced here: a missed receipt marks the transfer desynced, and the next packet
    write raises instead of going out.
    """

    def __init__(self, image_size=0):
        self.armed = False          # true only while the firmware image is streaming
        self.desynced = False
        self.reason = None
        self.timeouts = 0
        self.last_reported = None   # byte count from the most recent packet-receipt notification
        self.image_size = image_size
        self.final_window_waits = 0

    def begin_attempt(self):
        """Fresh DFU attempt. Clears per-attempt state."""
        self.armed = False
        self.desynced = False
        self.reason = None
        self.timeouts = 0
        self.last_reported = None
        self.final_window_waits = 0

    def arm(self):
        """Called when Receive Firmware Image starts. Cancellations only count from here."""
        self.armed = True

    def disarm(self):
        """Called at Validate. The image is sent; later cancellations are not receipt losses."""
        self.armed = False

    def fail(self, reason):
        if not self.desynced:
            self.desynced = True
            self.reason = reason

    def note_receipt(self, reported, sent):
        """A packet-receipt notification arrived reporting `reported` bytes received.

        Upstream parses this value and throws it away, so a receipt for the wrong offset is
        indistinguishable from a correct one and silently satisfies the wait. Comparing it against
        what the client has actually sent is what makes a stale, duplicate or wrong-offset receipt
        detectable at all.
        """
        if not self.armed:
            return
        self.last_reported = reported
        if reported != sent:
            self.fail(f"receipt reported {reported} bytes received but {sent} had been sent")

    def note_cancel(self, sent):
        """The receipt wait was cancelled -- upstream's wait_for timing out, most likely.

        Only meaningful while armed. If a receipt for exactly this offset already arrived, this is
        the clear()-after-receipt race rather than a loss: upstream clears the event after sending
        the triggering packet, so a receipt that beat the clear() would otherwise look like a
        timeout. Not treated as desync in that case.
        """
        if not self.armed:
            return
        if self.last_reported == sent:
            return

        # THE LAST WINDOW IS DIFFERENT, and treating it like any other cost three identical
        # failures on the bench before it was understood.
        #
        # This guard exists to stop the client STREAMING after flow control is lost. Once every
        # byte of the image is out there is nothing left to stream, so that rationale is spent --
        # the only thing after this point is the Receive Firmware Image command, whose own status,
        # CRC and P-256 signature are the real integrity boundary and are all still enforced.
        #
        # And a late receipt here is expected rather than anomalous: when the packet count happens
        # to be an exact multiple of PRN, dfu_lib waits for one more receipt after the final
        # packet, and that wait races the bootloader's end-of-image flush. A 136,616-byte image is
        # 560 packets, an exact multiple of both PRN 4 and PRN 16, and failed 3/3 at exactly
        # 136,616 bytes sent -- its own full size. The 561-packet build immediately before it,
        # not a whole multiple, never did. PRN 32 (17.5 windows) flashed it first try.
        #
        # So: note it, let the transfer proceed to the completion command, and let the bootloader
        # answer. Deliberately NOT a blanket relaxation -- it applies only when `sent` has reached
        # the full image size, so every mid-transfer window keeps aborting exactly as before.
        if self.image_size and sent >= self.image_size:
            self.final_window_waits += 1
            print(f"\n  final packet-receipt was late at {sent}/{self.image_size} bytes -- the "
                  f"whole image is sent, so this is the bootloader's end-of-image flush, not a "
                  f"desync. Letting Receive Firmware Image adjudicate.", flush=True)
            return

        self.timeouts += 1
        self.fail(f"no packet receipt within timeout at {sent} bytes sent")

    def check(self):
        if self.desynced:
            raise RuntimeError(
                f"packet-receipt synchronisation lost ({self.reason}); aborting rather than "
                "streaming with flow control lost. Retry the transfer -- the bootloader keeps "
                "advertising and nothing has been activated."
            )


class TransferProfile:
    """Times the two things a DFU transfer can be spending its wall clock on.

    Reported separately because they imply completely different fixes: write-await time is the host
    BLE stack's submission rate (nothing PRN tuning can help), while packet-receipt wait time is
    application-level pacing that a larger PRN directly reduces.

    Data packets and control-point writes are counted separately -- lumping them together
    overstates the packet count (a 134,432 byte image is ceil(134432/244) = 551 data packets, plus
    about ten control writes for start/init/PRN/receive/validate/activate).
    """

    def __init__(self):
        self.writes = []
        self.control_writes = []
        self.receipt_waits = []

    @staticmethod
    def _summary(samples, label):
        if not samples:
            return f"  {label}: none recorded"
        ordered = sorted(samples)
        def pct(p):
            return ordered[min(len(ordered) - 1, int(len(ordered) * p))]
        return (f"  {label}: n={len(samples)} sum={sum(samples):.2f}s "
                f"p50={pct(0.50)*1000:.1f}ms p90={pct(0.90)*1000:.1f}ms "
                f"p99={pct(0.99)*1000:.1f}ms max={max(ordered)*1000:.1f}ms")

    def report(self, total_s):
        accounted = sum(self.writes) + sum(self.control_writes) + sum(self.receipt_waits)
        print("Transfer profile:", flush=True)
        print(self._summary(self.writes, "data packet writes"), flush=True)
        print(self._summary(self.control_writes, "control writes"), flush=True)
        print(self._summary(self.receipt_waits, "receipt waits"), flush=True)
        print(f"  accounted={accounted:.2f}s of {total_s:.2f}s "
              f"({accounted / total_s * 100:.0f}%), other={total_s - accounted:.2f}s", flush=True)


class ControlStatus:
    """Remembers the last control-point (op, status) so a raised transfer can be explained.

    dfu_lib logs the status and then raises a bare DfuException, so by the time the exception
    reaches us the reason is gone. Recording it is what turns a traceback into a diagnosis.
    """

    def __init__(self):
        self.last_op = None
        self.last_status = None
        self.history = []

    def record(self, op, status):
        self.last_op, self.last_status = op, status
        self.history.append((op, status))

    @property
    def start_dfu_invalid_state(self):
        """Deliberately narrow: ONLY a Start DFU answered INVALID_STATE.

        INVALID_STATE from Validate or Activate means something quite different -- the image or
        the device state is wrong, not that a dead session is in the way -- and restarting a
        destructive transfer on that basis would be guessing. Every other opcode, and every other
        status, falls through to the normal failure path.
        """
        return (self.last_op == OP_CODE_START_DFU
                and self.last_status == DFU_STATUS_INVALID_STATE)


def install_status_hook(updater, status):
    """Capture every control-point response code on the way past."""
    original = type(updater)._wait_for_response

    async def recording(self, expected_op_code, timeout=30.0):
        code = await original(self, expected_op_code, timeout)
        status.record(expected_op_code, code)
        return code

    type(updater)._wait_for_response = recording


def install_transfer_hooks(updater, guard, profile=None):
    """Enforce abort-on-desync, and optionally time the transfer.

    write_gatt_char is patched on the class because dfu_lib constructs its own client internally.
    The receipt event is swapped on the updater INSTANCE -- patching dfu_lib.asyncio.Event would
    replace asyncio.Event for the whole interpreter, since that attribute is the real module.
    """
    from bleak import BleakClient

    original_write = BleakClient.write_gatt_char

    async def guarded_write(self, char_specifier, data, response=False):
        target = str(char_specifier).lower()
        is_control = not target.startswith("00001532")
        if is_control and len(data):
            opcode = data[0]
            if opcode == OP_CODE_START_DFU:
                # A Start while a transfer is already armed is anomalous -- treat it as a fault
                # rather than letting it clear a poisoned state mid-image.
                if guard.armed:
                    guard.fail("Start DFU issued while a firmware transfer was already armed")
                else:
                    guard.begin_attempt()
            elif opcode == OP_CODE_RECEIVE_FIRMWARE_IMAGE:
                guard.arm()
            elif opcode == OP_CODE_VALIDATE:
                guard.disarm()
        # Refuse to put anything else on the wire once synchronisation has been lost. Checked
        # after the transitions above so a poisoned attempt cannot reach Validate or Activate.
        guard.check()
        started = time.perf_counter()
        try:
            return await original_write(self, char_specifier, data, response=response)
        finally:
            if profile is not None:
                elapsed = time.perf_counter() - started
                target = str(char_specifier).lower()
                if not is_control:
                    profile.writes.append(elapsed)
                else:
                    profile.control_writes.append(elapsed)

    BleakClient.write_gatt_char = guarded_write

    class GuardedEvent(asyncio.Event):
        async def wait(self):
            started = time.perf_counter()
            try:
                return await super().wait()
            except asyncio.CancelledError:
                # asyncio.wait_for cancels this inner wait when it times out, and dfu_lib swallows
                # the resulting TimeoutError, so this is the only place a miss is observable.
                # CancelledError is NOT a reliable timeout identity on its own -- task cancellation
                # and disconnect cleanup raise it too -- which is why note_cancel() only counts it
                # while armed (i.e. mid-image) and ignores it when the expected receipt already
                # arrived. A connection-phase cancellation cost a live probe one aborted attempt
                # before this was narrowed.
                guard.note_cancel(getattr(updater, "bytes_sent", -1))
                raise
            finally:
                if profile is not None:
                    profile.receipt_waits.append(time.perf_counter() - started)

    if hasattr(updater, "pkg_receipt_event"):
        updater.pkg_receipt_event = GuardedEvent()
    else:
        print("  WARNING: no pkg_receipt_event on updater -- receipt desync cannot be detected.",
              flush=True)

    # Upstream parses the receipt's reported byte count and discards it, so a stale, duplicate or
    # wrong-offset receipt satisfies the wait exactly like a correct one. Wrap the handler to
    # compare it against what has actually been sent.
    original_handler = getattr(updater, "_notification_handler", None)
    if original_handler is not None:
        async def validating_handler(sender, data):
            try:
                if len(data) >= 5 and data[0] == OP_CODE_PACKET_RECEIPT_NOTIF:
                    reported = int.from_bytes(bytes(data[1:5]), "little")
                    guard.note_receipt(reported, getattr(updater, "bytes_sent", -1))
            except Exception:
                # Never let validation bookkeeping break the transport's own handling.
                pass
            return await original_handler(sender, data)

        updater._notification_handler = validating_handler
    else:
        print("  WARNING: no _notification_handler on updater -- receipt offsets unvalidated.",
              flush=True)


def bootloader_addresses_for(app_address):
    """Both addresses the DFU bootloader can advertise on, most likely first.

    2026-09-03, measured: after a ResetSafeBLEDfu buttonless jump the bootloader comes up on the
    SAME address as the application -- the peer-data handoff at 0x20007F80 carries the connection's
    address across the reset, which is the entire point of it. Only a bootloader that starts with
    no valid application (a fresh SWD flash, or a failed update) advertises on address+1.

    Hard-coding +1 meant the client scanned an address nothing was on while the bootloader sat
    there advertising, and reported "the jump did not take effect" for a jump that had worked.
    """
    prefix, last = app_address[:-2], int(app_address[-2:], 16)
    return [app_address.upper(), f"{prefix}{(last + 1) & 0xFF:02X}".upper()]


def looks_like_bootloader(device, advertisement) -> bool:
    """Is this advertisement the DFU bootloader rather than the application?

    Address alone cannot answer it: after a buttonless jump the bootloader reuses the
    application's address with an entirely different GATT table. The advertisement can, because the
    production application registers the DFU service in GATT but deliberately does NOT put it in
    its advertising payload -- startAdv()'s static_assert shows Flags(3) + TxPower(3) + mfg(2+23)
    already fills the 31-byte legacy PDU exactly, leaving no room for a service UUID. So a DFU
    service UUID on air means the bootloader, and the application's MSV3-MOISTURE-NNN name contains
    no "dfu" to collide with the bootloader's "AdaDFU".

    This matters most under --skip-jump, where mistaking a running application for an orphaned
    bootloader would start a transfer against a GATT table that has no DFU packet characteristic.
    """
    uuids = [u.lower() for u in (advertisement.service_uuids or [])]
    name = (advertisement.local_name or getattr(device, "name", None) or "").lower()
    return DFU_SERVICE_UUID in uuids or "dfu" in name


async def wait_for_advertisement(addresses, timeout_s, label, require_dfu_service=False):
    """Scan until any of `addresses` appears.

    The application only opens a 5 s OTA window per minute (OTA_ADV_LISTEN_MS), so a short
    one-shot scan misses it far more often than it hits.

    `require_dfu_service` disambiguates the bootloader from the application when both can appear on
    the same address: after a buttonless jump the bootloader reuses the application's address, so
    the address alone no longer says which one is running. The DFU service UUID does.

    Returns `(device, advertisement)`, or `(None, None)` on timeout. The advertisement is returned
    rather than just its RSSI because the application's manufacturer payload carries the sensor ID
    the board is actually running -- the one piece of live identity evidence the preflight has.
    """
    from bleak import BleakScanner

    if isinstance(addresses, str):
        addresses = [addresses]
    wanted = {a.upper() for a in addresses}
    print(f"Waiting up to {timeout_s:.0f}s for {label} at {' or '.join(sorted(wanted))}...", flush=True)
    found = asyncio.get_running_loop().create_future()

    def callback(device, advertisement):
        if device.address.upper() not in wanted or found.done():
            return
        if require_dfu_service and not looks_like_bootloader(device, advertisement):
            return
        found.set_result((device, advertisement))

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        device, advertisement = await asyncio.wait_for(found, timeout=timeout_s)
        print(f"  found {label} at {device.address} (rssi {advertisement.rssi})", flush=True)
        return device, advertisement
    except asyncio.TimeoutError:
        return None, None
    finally:
        await scanner.stop()


class PreflightError(Exception):
    """A target/package check failed. Nothing has been written and nothing has been jumped."""


class JumpError(Exception):
    """The application connection failed before the enter-DFU write was attempted."""


def resolve_target(args):
    """Turn --probe / --app-address into a registry entry, or refuse.

    Requiring the address to be registered is the whole point: it is what makes the identity check
    more than "the operator typed the same string twice". A board already sitting in its bootloader
    advertises no manufacturer payload, so the registry is the only thing that can say which probe
    that address belongs to. A board that is not in the registry is therefore not flashable here --
    add it to MSV3/fleet.json first, which is a six-line edit and is how the registry stays true.
    """
    try:
        fleet = load_fleet(args.fleet)
    except FleetError as error:
        raise PreflightError(str(error)) from None

    if args.probe is not None:
        try:
            probe = fleet.by_sensor_id(args.probe)
        except FleetError as error:
            raise PreflightError(str(error)) from None
        if args.app_address:
            try:
                given = normalise_address(args.app_address)
            except FleetError as error:
                raise PreflightError(str(error)) from None
            if given != probe.application_address:
                raise PreflightError(
                    f"--probe {args.probe} is {probe.application_address} in {fleet.source}, but "
                    f"--app-address says {given}. Fix whichever is wrong; do not override.")
        return fleet, probe

    if not args.app_address:
        raise PreflightError("pass --probe N (preferred) or --app-address")

    try:
        given = normalise_address(args.app_address)
        probe = fleet.by_address(given)
    except FleetError as error:
        raise PreflightError(str(error)) from None
    if probe is None:
        known = "\n".join(f"    {entry.application_address}  probe {entry.sensor_id} "
                          f"({entry.name})"
                          for entry in sorted(fleet, key=lambda x: x.sensor_id))
        raise PreflightError(
            f"{given} is not in {fleet.source}, so nothing here knows which probe it is or which "
            f"bootloader it runs.\n  Registered probes:\n{known}\n"
            f"  Add an entry for this board before flashing it.")
    return fleet, probe


def preflight(args, probe, fleet_source_name="MSV3/fleet.json"):
    """Everything checkable before the radio is touched. Returns the parsed package.

    Ordering matters: integrity first, then identity. A package the bootloader would reject
    outright should never cost a jump, because a jump takes a live sensor off the air for the
    length of a transfer.
    """
    try:
        package = read_package(args.package.resolve())
    except FleetError as error:
        raise PreflightError(str(error)) from None
    print(f"Package: {package.describe()}", flush=True)
    print(f"Target:  {probe.describe()}", flush=True)

    # Target suitability first. Probe 5 is on the stock bootloader, and saying so
    # plainly beats letting the signature step fail later with the downstream
    # "no release manifest for adafruit-stock-0.11.0".
    if not probe.ota_capable:
        raise PreflightError(
            f"probe {probe.sensor_id} is registered ota_capable=false ({probe.bootloader}).\n"
            f"  {probe.notes}\n"
            f"  A board on the stock bootloader has to be migrated over USB first; there is "
            f"nothing on it for a BLE jump to talk to.")

    public_key = None
    if package.signed:
        release = probe.bootloader.replace("msv3-", "")
        try:
            public_key = load_release_public_key(release, probe.bootloader_key_sha256)
        except FleetError as error:
            raise PreflightError(
                f"cannot check this signed package against {probe.bootloader}: {error}") from None
    try:
        verify_package_integrity(package.path, probe.device_type, public_key)
    except FleetError as error:
        raise PreflightError(str(error)) from None
    how = (f"signature verifies against the key compiled into {probe.bootloader}"
           if package.signed else "unsigned, CRC16 OK")
    print(f"  integrity    OK -- {how}, device_type {package.device_type:#06x}", flush=True)

    # BOOT-GUARD / BOOTLOADER RAM PAIRING. Checked only after integrity above, because the marker
    # is read out of the image and an unverified image's contents are not evidence of anything.
    #
    # Guard 2's boot counter is an 8-byte record at 0x2003FFF8 shared with the bootloader. Only
    # MSV3 1.0.3+ keeps its stack below it; 1.0.1 and 1.0.2 boot with MSP = 0x20040000 and descend
    # straight through it. Sending a guard-enabled image to an older bootloader produces a guard
    # that looks armed and can never fire -- the exact GPREGRET2 failure the retained-RAM scheme
    # was built to replace. MSV3/bootloader/README.md has stated this rule since 1.0.3 landed;
    # until now nothing enforced it, and probe 4 is deployed, in soil, on solar, with no SWD.
    guard_refusal = check_boot_guard_pairing(probe, package.boot_guard, package.sha256)
    if guard_refusal:
        raise PreflightError(guard_refusal)
    guard_state = ({True: "guard 2 ON", False: "guard 2 off"}
                   .get(package.boot_guard, "unmarked (pre-2026-09-09 image)"))
    print(f"  boot guard   OK -- {guard_state}, bootloader {probe.bootloader}"
          f"{' reserves 0x2003FFF8' if probe.reserves_boot_guard_ram else ''}", flush=True)

    # The identity check below reads the name the firmware advertises. That only equals SENSOR_ID
    # for images built after the compile-time derivation landed; before it, the two were separate
    # hand-edited constants and could legitimately disagree. So an older package's name proves
    # nothing, and saying "identity OK" about it would be a false assurance.
    if package_predates_identity_contract(package):
        allowlisted = package.sha256 in probe.legacy_package_sha256
        detail = (f"{package.path.name} is application version {package.application_version}, "
                  f"which predates the compile-time guarantee that the advertised name equals "
                  f"SENSOR_ID (introduced at {IDENTITY_CONTRACT_MIN_APP_VERSION}).\n"
                  f"  Its name says {package.name}, but nothing here can prove the sensor ID "
                  f"compiled into it agrees.")
        if allowlisted:
            print(f"  legacy       ALLOWLISTED -- sha256 {package.sha256[:16]}... is recorded for "
                  f"probe {probe.sensor_id}", flush=True)
        elif probe.deployed:
            # --allow-legacy-package on a deployed probe would restore the exact defect this
            # whole preflight exists to prevent: an old image whose name says 004 while its
            # compiled SENSOR_ID is 5 would pass the name check, and the live check describes the
            # application currently RUNNING, not the one about to be sent. So on a deployed board
            # the flag is not enough -- the specific image has to be hashed into the registry,
            # which is a reviewable edit rather than a keystroke.
            raise PreflightError(
                f"{detail}\n"
                f"  probe {probe.sensor_id} is deployed=true, so --allow-legacy-package is not "
                f"sufficient on its own.\n"
                f"  Rebuild from current source (preferred), or -- only if deployment records "
                f"establish this exact image -- add its sha256 to that probe's "
                f'"legacy_package_sha256" in MSV3/fleet.json:\n'
                f"    {package.sha256}")
        elif not args.allow_legacy_package:
            raise PreflightError(
                f"{detail}\n"
                f"  Rebuild from current source, or pass --allow-legacy-package if you know what "
                f"is in this image.")
        else:
            print(f"  legacy       OVERRIDDEN -- pre-{IDENTITY_CONTRACT_MIN_APP_VERSION} package "
                  f"on a non-deployed board", flush=True)

    # Rollback. Measured on 2026-09-05: the bootloader installs an older validly signed image
    # without complaint -- dfu_init.c's version check is an explicit empty template that "allows
    # for any application to be installed". That is a freshness limitation rather than an
    # integrity failure, but it is not harmless: rolling a probe back past guard 1 restores the
    # defect where a sensor fault closes the radio and makes a coated probe unreachable.
    #
    # This check is HOST-SIDE and says so. It cannot stop anyone who does not run it; a real
    # control needs a monotonic version in the bootloader settings page, which needs SWD to
    # install. What it does stop is the likely case: reaching for a stale package by mistake.
    def _bootloader_enforces_rollback(name: str) -> bool:
        """True when the probe's bootloader itself refuses a below-floor image.

        Anti-rollback landed in MSV3 bootloader 1.0.2. Parsed rather than string-matched so
        1.0.3 and later inherit it without another edit here.
        """
        prefix = "msv3-"
        if not name or not name.startswith(prefix):
            return False
        try:
            parts = tuple(int(p) for p in name[len(prefix):].split("."))
        except ValueError:
            return False
        return parts >= (1, 0, 2)

    if (probe.installed_application_version
            and package.application_version < probe.installed_application_version):
        detail = (f"{package.path.name} is application version {package.application_version}, "
                  f"below the {probe.installed_application_version} recorded as installed on "
                  f"probe {probe.sensor_id} in {fleet_source_name}.")
        if not args.allow_downgrade:
            # What the DEVICE will do depends on which bootloader this probe carries, and saying
            # the wrong thing here is worse than saying nothing. Before 1.0.2 the check was an
            # unimplemented upstream template and the host was genuinely the only gate. From 1.0.2
            # the bootloader keeps a monotonic floor in its settings page and refuses below it --
            # confirmed on probe 99, 2026-09-07, floor 65551 read back at 0x000FF01C.
            on_device = _bootloader_enforces_rollback(probe.bootloader)
            device_note = (
                "  The bootloader on this probe ALSO refuses this: 1.0.2 keeps a monotonic floor "
                "in its settings page.\n"
                "  Overriding here still costs the running application -- the image is erased at "
                "Start DFU, before the version is ever checked.\n"
                if on_device else
                "  The bootloader will NOT refuse this -- on this probe the version check is an "
                "unimplemented upstream template, verified by measurement -- so this is the only "
                "place a rollback gets noticed.\n")
            raise PreflightError(
                f"{detail}\n"
                f"{device_note}"
                f"  Rolling back past guard 1 restores the fault that makes a coated probe "
                f"unreachable.\n"
                f"  Pass --allow-downgrade if the rollback is deliberate, and update "
                f'"installed_application_version" for this probe afterwards.')
        print(f"  rollback     OVERRIDDEN -- {detail}", flush=True)

    if package.sensor_id != probe.sensor_id:
        headline = (f"package identity mismatch: this image advertises as {package.name} "
                    f"(sensor {package.sensor_id}), but the target is probe {probe.sensor_id} "
                    f"({probe.name}).")
        if not args.allow_identity_mismatch:
            raise PreflightError(
                f"{headline}\n"
                f"  Sending it would give two probes the same sensor ID and mix their history in "
                f"the receiver.\n"
                f"  Set SENSOR_ID = {probe.sensor_id} in MSV3_Moisture_BLE_Sleep.ino and rebuild, "
                f"or pass --allow-identity-mismatch if this is deliberate.")
        # The override exists for the bench board's uncommitted identity. Allowing it against a
        # deployed probe would leave the fleet's main safety property defeatable by one flag, and
        # the legitimate fix for a deployed board is to correct the registry -- which leaves an
        # auditable change behind, where a command-line flag does not.
        if probe.deployed:
            raise PreflightError(
                f"{headline}\n"
                f"  --allow-identity-mismatch is refused for probe {probe.sensor_id} because it is "
                f"deployed=true in the registry. If the registry is wrong about this board, fix "
                f"MSV3/fleet.json; if the package is wrong, rebuild it.")
        print(f"  identity     OVERRIDDEN -- {headline}", flush=True)
    else:
        print(f"  identity     OK -- {package.name} matches probe {probe.sensor_id}", flush=True)

    return package


def check_live_identity(args, probe, advertisement):
    """Compare the running application's own advertisement against the registry, before the jump.

    This is the only check backed by the board itself rather than by a file, so it is also what
    keeps the registry honest -- a wrong entry surfaces here as a mismatch instead of quietly
    authorising the wrong transfer.

    --allow-identity-mismatch relaxes only the identity comparisons. A shut power gate or pre-OTA
    firmware still blocks under it: those are not disagreements about which probe this is, they
    are the board telling you it will refuse the jump.
    """
    # A targeted bring-up heartbeat carries the same identity, battery and OTA-gate prefix as
    # production telemetry, so either one can authorise the jump.
    advert = any_advertisement_from_bleak(advertisement)
    name = advertisement.local_name or ""
    identity_problems = []
    blocking_problems = []

    if advert is None:
        blocking_problems.append(
            "the application advertised no MSV manufacturer payload, so its sensor ID could not "
            "be read")
    else:
        if advert.sensor_id != probe.sensor_id:
            identity_problems.append(
                f"the board at {probe.application_address} advertises sensor {advert.sensor_id}, "
                f"not {probe.sensor_id} -- MSV3/fleet.json is wrong about this address")
        if advert.wide and advert.ota_gate_open is False:
            if advert.battery_mv and advert.battery_mv >= OTA_MIN_BATTERY_MV:
                blocking_problems.append(
                    f"it will refuse a DFU jump: flag bit4 is clear at {advert.battery_mv} mV, "
                    f"above the {OTA_MIN_BATTERY_MV} mV gate, which means pre-OTA firmware rather "
                    f"than a shut gate")
            else:
                blocking_problems.append(
                    f"it will refuse a DFU jump: the OTA power gate is shut at "
                    f"{advert.battery_mv} mV, below {OTA_MIN_BATTERY_MV} mV")

    if name and name != probe.name:
        identity_problems.append(f"it advertises the name {name!r}, not {probe.name!r}")

    if identity_problems and args.allow_identity_mismatch and not probe.deployed:
        for problem in identity_problems:
            print(f"  live check   OVERRIDDEN -- {problem}", flush=True)
        identity_problems = []
    elif identity_problems and args.allow_identity_mismatch:
        identity_problems.append(
            f"--allow-identity-mismatch does not apply: probe {probe.sensor_id} is deployed=true")

    problems = blocking_problems + identity_problems
    if problems:
        detail = "\n".join(f"  - {problem}" for problem in problems)
        raise PreflightError(
            f"the board on air does not match the target:\n{detail}\n"
            f"  Nothing was written and the board was not jumped.")

    extra = (f", sensor {advert.sensor_id}, {advert.battery_mv} mV, OTA gate open"
             if advert is not None and advert.wide else "")
    print(f"  live check   OK -- {name or 'unnamed'}{extra}", flush=True)


async def read_firmware_revision(client):
    """The build the probe is actually running, or None if it does not publish one.

    None is not a failure: every image built before 2026-09-04 predates the DIS and cannot answer.
    Treat it as "unknown", never as "wrong" -- refusing an update because a probe is too old to
    describe itself would strand exactly the boards most in need of one.
    """
    try:
        value = await client.read_gatt_char(DIS_FIRMWARE_REVISION_UUID)
    except Exception:  # noqa: BLE001
        return None
    text = bytes(value).split(b"\x00")[0].decode("utf-8", "replace").strip()
    if text.startswith("MSV3-BUILD:"):
        text = text[len("MSV3-BUILD:"):]
    return text or None


async def jump_to_bootloader(device, address_type, on_running_build=None, retries=3,
                             reconnect=None, connect_delay=0.0, retry_scan_delay=5.0):
    """Enable notifications, then write the enter-bootloader opcode. Returns a status string.

    ResetSafeBLEDfu requires notifications enabled before it will authorize the write, and
    refuses outright when its battery/USB power gate is shut. A successful jump resets the
    board immediately, so the write frequently surfaces as a disconnect rather than a clean
    completion -- that is expected, and is reported as such rather than as an error.
    """
    from bleak import BleakClient
    from dfu_lib import DFU_CONTROL_POINT_UUID

    kwargs = {"timeout": 20.0}
    if sys.platform == "win32" and address_type != "auto":
        kwargs["winrt"] = {"address_type": address_type}

    retries = max(1, int(retries))
    for attempt in range(1, retries + 1):
        # Let WinRT finish tearing down the advertisement watcher before it creates the GATT
        # requester. The recovery client has always observed this gap; the application jump path
        # accidentally skipped it even though --connect-delay says it applies after a scan.
        if connect_delay > 0:
            await asyncio.sleep(connect_delay)

        started = time.time()
        write_started = None
        try:
            async with BleakClient(device, **kwargs) as client:
                print(f"  connected in {time.time() - started:.2f}s", flush=True)

                running = await read_firmware_revision(client)
                if running is None:
                    print("  running build   not reported (pre-2026-09-04 firmware has no DIS)",
                          flush=True)
                else:
                    print(f"  running build   {running}", flush=True)
                if on_running_build is not None:
                    # Raising here aborts BEFORE the jump opcode is written, so a refusal still
                    # costs the probe nothing.
                    on_running_build(running)

                await client.start_notify(DFU_CONTROL_POINT_UUID, lambda _handle, _data: None)
                payload = bytearray([OP_CODE_ENTER_BOOTLOADER, UPLOAD_MODE_APPLICATION])
                write_started = time.time()
                await client.write_gatt_char(DFU_CONTROL_POINT_UUID, payload, response=True)
                return f"write acknowledged in {time.time() - write_started:.2f}s"
        except PreflightError:
            raise
        except (NameError, AttributeError, TypeError, KeyError, ImportError):
            # A bug in this file is not "the board reset". Fail loudly instead of turning a host
            # programming error into evidence about the probe.
            raise
        except Exception as error:
            # A disconnect is evidence of a possible reset ONLY after write_gatt_char was entered.
            # Previously a flaky connection failed before "connected in ..." was printed, yet the
            # broad handler called it a normal reset and waited a full minute for a bootloader that
            # had never been requested. Pre-write failures are safe to retry because no command has
            # reached the probe. Once the write starts, never retry blindly: its acknowledgement may
            # have been lost precisely because the requested reset happened.
            if write_started is not None:
                text = str(error).upper()
                if "NOT_PERMITTED" in text:
                    return (f"REJECTED by firmware ({error}) -- ResetSafeBLEDfu's power gate is "
                            "shut. Check advertisement flag bit4 against OTA_MIN_BATTERY_MV.")
                return (f"link dropped during write ({type(error).__name__}: {error}) -- normal "
                        "if the board reset")

            if attempt < retries:
                print(f"  application connection attempt {attempt}/{retries} failed before the "
                      f"DFU write ({type(error).__name__}: {error}); rescanning", flush=True)
                if reconnect is None:
                    await asyncio.sleep(float(attempt))
                else:
                    # The application advertises for only five seconds per wake. Reusing the
                    # BLEDevice from that expired window made the nominal retries three calls
                    # against one stale WinRT handle. Wait out the current window, then require a
                    # fresh advertisement (and let the caller recheck its live identity) before
                    # another connection attempt.
                    await asyncio.sleep(retry_scan_delay)
                    device = await reconnect()
                    if device is None:
                        raise JumpError(
                            "application did not re-advertise for a safe connection retry; "
                            "no reset or firmware write was requested") from error
                continue
            sandbox_hint = ""
            if (sys.platform == "win32" and
                    any(code in str(error) for code in ("-2147467260", "-2147467259"))):
                sandbox_hint = (
                    " If this is running under Codex or another workspace sandbox, rerun the "
                    "same checked command outside the sandbox: passive WinRT scans can work "
                    "while active GATT opens are blocked with E_ABORT/E_FAIL.")
            raise JumpError(
                f"application connection failed {retries} time(s) before the enter-DFU write; "
                f"no reset or firmware write was requested ({type(error).__name__}: {error})."
                f"{sandbox_hint}") from error

    raise AssertionError("unreachable jump retry state")


async def run(args):
    # Every fail-closed check happens here, before the BLE stack is even initialised: a
    # refusal should cost nothing and disturb nothing.
    fleet, probe = resolve_target(args)
    package_info = preflight(args, probe, fleet.source.name)

    if args.preflight_only:
        # The happy path needs to be provable without taking a sensor off the air. Everything
        # above is every check that does not require the board; reaching here means the only thing
        # left is the transfer itself.
        print("\nPreflight passed. Stopping before the radio (--preflight-only).", flush=True)
        return 0

    sys.path.insert(0, str(DFU_CLIENT))
    try:
        import dfu_lib
    except ModuleNotFoundError as error:
        raise SystemExit(
            f"Missing BLE DFU client at {DFU_CLIENT}. Recreate the pinned nrf_dfu_py checkout "
            "documented in MSV3/docs/MSV3_OTA_BOOTLOADER_HANDOFF_2026-09-03.md."
        ) from error

    # Keep a reference for the lifetime of the update. asyncio holds only a weak reference to a
    # running task, so dropping this on the floor lets the message pump be garbage-collected
    # mid-transfer and the WinRT completions it services stall.
    pump_task = install_windows_ble_workarounds(args.address_type)

    # The jump above uses its own BleakClient with the RANDOM address type bound explicitly, but
    # dfu_lib constructs the transfer client itself and would otherwise get WinRT's default. MSV3
    # addresses are static-random, so bind it the same way ble_dfu_recover.py does.
    if sys.platform == "win32":
        # use_cached_services=False is not optional here. After a buttonless jump the bootloader
        # occupies the SAME address the application just had, with an entirely different GATT
        # table. Windows caches services per address, serves the stale application table, and the
        # first connect dies with E_CANCELED -- observed on every single transfer, recovering only
        # on dfu_lib's retry. Forcing uncached discovery removes that wasted attempt.
        winrt_options = {"use_cached_services": False}
        if args.address_type != "auto":
            winrt_options["address_type"] = args.address_type
        dfu_lib.BleakClient = functools.partial(dfu_lib.BleakClient, winrt=winrt_options)

    # RAISE THE PACKET-RECEIPT DEADLINE. dfu_lib computes it as
    #     prn_timeout = max(0.8, self.prn * 0.08)
    # which is derived from PRN alone and allows nothing for the HOST stack's notification
    # latency -- the thing that actually varies. It has the perverse consequence that lowering
    # PRN TIGHTENS the deadline: --prn 16 gets 1.28 s, --prn 4 gets 0.8 s. So the standard advice
    # for a marginal link ("drop to --prn 4") simultaneously makes the client less patient, and
    # a transfer that was progressing fine gets aborted by its own guard. Measured on probe 99,
    # 2026-09-07: --prn 16 aborted at 74,176 bytes, --prn 4 got to 126,880 of 132,448 (96%) and
    # aborted there too.
    #
    # Patched here rather than in dfu_lib.py because that file lives under the gitignored
    # .arduino-build/ and an edit there is not part of this repository. Scoped two ways so it
    # cannot affect anything else: the proxy replaces asyncio ONLY on the dfu_lib module object,
    # and it only substitutes SHORT timeouts. dfu_lib's other waits are 20 s, 30 s and 60 s, so
    # the sub-2 s ceiling picks out the packet-receipt wait and nothing else.
    #
    # This does not weaken the desync guard. A genuinely lost receipt still aborts the transfer --
    # it just has to actually be lost, rather than merely slower than 0.8 s.
    class _DfuAsyncioProxy:
        def __init__(self, real, floor):
            self._real = real
            self._floor = floor

        def __getattr__(self, name):
            return getattr(self._real, name)

        async def wait_for(self, aw, timeout=None):
            if timeout is not None and timeout <= 2.0 and timeout < self._floor:
                timeout = self._floor
            return await self._real.wait_for(aw, timeout)

    # getattr, not args.prn_timeout: the test suite constructs Args objects by hand, so reaching
    # for a new attribute directly turns every one of them into an AttributeError.
    prn_timeout = getattr(args, "prn_timeout", 0.0) or 0.0
    if prn_timeout > 0:
        dfu_lib.asyncio = _DfuAsyncioProxy(asyncio, prn_timeout)
        print(f"  packet-receipt deadline raised to {prn_timeout:.1f}s "
              f"(dfu_lib would use {max(0.8, args.prn * 0.08):.2f}s)", flush=True)

    package = args.package.resolve()
    updater = dfu_lib.NordicLegacyDFU(
        str(package),
        prn=args.prn,
        packet_delay=args.delay,
        high_mtu=not args.no_high_mtu,
        progress_callback=progress,
    )
    updater.parse_zip()
    if updater.upload_mode != UPLOAD_MODE_APPLICATION:
        raise SystemExit("Package must contain an application only; refusing bootloader DFU over BLE.")

    # The guard is always installed -- it is a correctness property, not a diagnostic. Profiling
    # is opt-in.
    # parse_zip() has already run, so bin_data is the real image the transfer will send.
    guard = ReceiptGuard(image_size=len(getattr(updater, "bin_data", b"") or b""))
    profile = TransferProfile() if args.profile else None
    install_transfer_hooks(updater, guard, profile)
    control_status = ControlStatus()
    install_status_hook(updater, control_status)

    # Registry-derived, so a bootloader found on either address is provably still this probe.
    if args.bootloader_address:
        override = normalise_address(args.bootloader_address)
        if override not in probe.bootloader_addresses:
            raise PreflightError(
                f"--bootloader-address {override} is not one of probe {probe.sensor_id}'s "
                f"addresses ({', '.join(probe.bootloader_addresses)}).")
        boot_addresses = [override]
    else:
        boot_addresses = probe.bootloader_addresses

    # Already sitting in the bootloader (a previous attempt left it there)? Go straight to
    # the transfer rather than waiting out an application window that will never come. The DFU
    # service check matters here: after a buttonless jump the bootloader shares the application's
    # address, so the address alone cannot tell them apart.
    probe_timeout = args.bootloader_timeout if args.skip_jump else 6.0
    device, _ = await wait_for_advertisement(boot_addresses, probe_timeout, "bootloader",
                                             require_dfu_service=True)

    if device is not None:
        # Already in the bootloader, so there is no application advertisement left to check
        # against. The registry is what ties this address to a probe, and preflight() has already
        # matched the package to it.
        print(f"  already in the bootloader -- identity rests on {fleet.source.name}, which "
              f"lists {device.address.upper()} as probe {probe.sensor_id}", flush=True)
    else:
        if args.skip_jump:
            print("Bootloader never advertised.", flush=True)
            return 1

        app, advertisement = await wait_for_advertisement(
            probe.application_address, args.app_timeout, "application")
        if app is None:
            print(f"Application never advertised within {args.app_timeout:.0f}s.", flush=True)
            return 1

        # The last check before the point of no return for availability.
        check_live_identity(args, probe, advertisement)

        def check_running_build(running):
            """Refuse to take a live sensor off the air to install what it already runs."""
            if running is None or not package_info.build_id:
                return
            if running == package_info.build_id and not args.reflash_same_build:
                raise PreflightError(
                    f"probe {probe.sensor_id} is already running build {running}, which is "
                    f"exactly what {package_info.path.name} contains.\n"
                    f"  Jumping it would take a live sensor off the air for a transfer that "
                    f"changes nothing. Pass --reflash-same-build if that is deliberate.")

        async def rescan_application():
            refreshed, refreshed_advertisement = await wait_for_advertisement(
                probe.application_address, args.app_timeout, "application retry")
            if refreshed is None:
                return None
            check_live_identity(args, probe, refreshed_advertisement)
            return refreshed

        print("Jumping to bootloader...", flush=True)
        jump_result = await jump_to_bootloader(
            app, args.address_type, check_running_build, args.retry,
            rescan_application, args.connect_delay)
        print(f"  {jump_result}", flush=True)

        device, _ = await wait_for_advertisement(boot_addresses, args.bootloader_timeout,
                                                 "bootloader", require_dfu_service=True)
        if device is None:
            print("\nBootloader did not appear -- the jump did not take effect.", flush=True)
            print("No firmware image bytes were transferred; re-scan the application before "
                  "retrying.", flush=True)
            return 1

    await asyncio.sleep(args.connect_delay)
    chunking = "20-byte" if args.no_high_mtu else "negotiated-MTU"
    print(f"Transferring {package.name} ({chunking} chunks)...", flush=True)
    started = time.time()

    # RESUMING AFTER AN INTERRUPTED TRANSFER, measured on the bench 2026-09-04.
    #
    # Kill a transfer partway and the bootloader keeps the half-finished session's state. The next
    # Start DFU is answered INVALID_STATE (status 2), and upstream dfu_lib responds by sending
    # OP_CODE_RESET and raising -- so the board IS recovering, it just needs a moment to reboot
    # and re-advertise. The old code spent its remaining retries during that reboot, failed all of
    # them against a board that was not there, and exited with a raw WinRT traceback.
    #
    # Two interrupted runs both looked like that, and both worked on a plain manual retry
    # afterwards. On a coated probe in the field that traceback is the difference between "wait
    # twenty seconds and run it again" and "the probe is bricked".
    # 0 opts out entirely; anything else is that many total attempts.
    attempts_left = max(1, args.stale_session_retries) if args.stale_session_retries else 1
    while True:
        try:
            await updater.perform_update(device, max_retries=args.retry)
            break
        except (KeyboardInterrupt, asyncio.CancelledError):
            # An operator stopping this is never a condition to retry through.
            raise
        except Exception as error:  # noqa: BLE001
            if not control_status.start_dfu_invalid_state or attempts_left <= 1:
                raise
            attempts_left -= 1
            print(f"\nThe bootloader refused Start DFU with INVALID_STATE: it is still holding "
                  f"the state of an earlier interrupted transfer.", flush=True)
            # dfu_lib writes OP_CODE_RESET before raising on a Start DFU failure (dfu_lib.py,
            # the `if status != 1` branch). That upstream side effect is what actually clears the
            # stale session; this branch only waits it out. If that behaviour ever changes, this
            # loop will simply time out waiting rather than doing anything unsafe.
            print(f"  dfu_lib has already sent it a reset, so it is rebooting. Waiting for it to "
                  f"come back, then restarting the transfer from the beginning "
                  f"({attempts_left} attempt(s) left).", flush=True)
            print(f"  ({type(error).__name__}: {error})", flush=True)
            # It can return on EITHER address: the peer-data handoff keeps the application's
            # address across a buttonless jump, but a bootloader that restarts with no valid
            # application uses +1. Both were observed on this board within minutes.
            #
            # Only ever the two addresses the registry pins for THIS probe. Widening to "any
            # nearby AdaDFU" would be a wrong-target flash waiting to happen: device type 0x3356
            # is shared by every MSV3 board and identifies nothing.
            device, _ = await wait_for_advertisement(
                probe.bootloader_addresses, args.bootloader_timeout, "bootloader after its reset",
                require_dfu_service=True)
            if device is None:
                print("The bootloader did not come back within "
                      f"{args.bootloader_timeout:.0f}s. Nothing further was written; the board "
                      "should still be in BLE recovery, so rerun this command.", flush=True)
                return 1
            if normalise_address(device.address) not in probe.bootloader_addresses:
                raise PreflightError(
                    f"the bootloader reappeared at {device.address}, which is not one of probe "
                    f"{probe.sensor_id}'s addresses. Refusing to transfer to it.")
            await asyncio.sleep(args.connect_delay)

    elapsed = time.time() - started
    payload = len(getattr(updater, "bin_data", b"") or b"")
    rate = f", {payload / elapsed / 1024:.2f} kB/s" if payload and elapsed > 0 else ""
    print(f"Transfer completed in {int(elapsed // 60)}m {elapsed % 60:.0f}s{rate}.", flush=True)
    if profile is not None:
        profile.report(elapsed)
    if pump_task is not None:
        pump_task.cancel()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="Signed application-only DFU ZIP")
    parser.add_argument("--probe", type=int,
                        help="Sensor ID from MSV3/fleet.json. Preferred over --app-address: it "
                             "resolves the address, the allowed bootloader addresses, and the "
                             "device type the target's bootloader accepts, all from one number")
    parser.add_argument("--fleet", type=Path,
                        help="Registry path (default MSV3/fleet.json)")
    parser.add_argument("--preflight-only", action="store_true",
                        help="Run every package/target check and exit without scanning, jumping "
                             "or transferring. Use it to prove a release is deliverable before "
                             "taking a sensor off the air")
    parser.add_argument("--allow-legacy-package", action="store_true",
                        help=f"Accept a package older than application version "
                             f"{IDENTITY_CONTRACT_MIN_APP_VERSION}, whose advertised name is not "
                             f"guaranteed to equal the SENSOR_ID compiled into it")
    parser.add_argument("--prn-timeout", type=float, default=6.0,
                        help="Seconds to wait for a packet receipt before declaring the transfer "
                             "desynced. dfu_lib's own value is max(0.8, prn*0.08), which ignores "
                             "host-stack latency and gets SHORTER as PRN drops. 0 disables this "
                             "override and restores dfu_lib's behaviour.")
    parser.add_argument("--allow-downgrade", action="store_true",
                        help="Install a package older than the version recorded as installed. The "
                             "DEVICE does not enforce this; the registry is the only record.")
    parser.add_argument("--allow-identity-mismatch", action="store_true",
                        help="Send a package built for a different sensor ID. Intended for the "
                             "bench board, which runs an uncommitted identity. Relaxes ONLY the "
                             "identity comparisons -- signature, device type, image integrity and "
                             "the registry's address list are still enforced")
    parser.add_argument("--app-address",
                        help="Target the probe registered at this BLE address. Must be in "
                             "MSV3/fleet.json -- an unregistered board is refused, because "
                             "nothing would then know which probe it is or which bootloader it "
                             "runs. Add it to the registry first")
    parser.add_argument("--bootloader-address",
                        help="Pin the bootloader to one address instead of accepting either of "
                             "the probe's two. Must still be one the registry lists for it")
    parser.add_argument("--skip-jump", action="store_true",
                        help="Board is already sitting in its bootloader; do not try to jump it")
    parser.add_argument("--prn", type=int, default=16,
                        help="Packet receipt interval (default 16, the measured optimum -- see the "
                             "module docstring for the PRN 4/16/32 sweep)")
    parser.add_argument("--delay", type=float, default=0.6, help="Start/size delay seconds (default 0.6)")
    parser.add_argument("--retry", type=int, default=3,
                        help="Application-jump and DFU connection attempts (default 3)")
    parser.add_argument("--reflash-same-build", action="store_true",
                        help="Send the image even when the probe already reports running that "
                             "exact build. Off by default so a redundant update cannot take a "
                             "live sensor off the air for nothing")
    parser.add_argument("--stale-session-retries", type=int, default=3,
                        help="How many times to clear a bootloader still holding an earlier "
                             "interrupted transfer's state and start the transfer again "
                             "(default 3; 0 to opt out). NOT a protocol-level resume -- nothing "
                             "continues from an offset, the whole image is re-sent. Each attempt "
                             "rescans, because the bootloader may return on either of the probe's "
                             "two addresses")
    parser.add_argument("--app-timeout", type=float, default=180.0,
                        help="Seconds to wait for the application's OTA advertising window (default 180)")
    parser.add_argument("--bootloader-timeout", type=float, default=60.0,
                        help="Seconds to wait for the bootloader after a jump (default 60)")
    parser.add_argument("--connect-delay", type=float, default=2.0,
                        help="Seconds between ending a scan and opening GATT (default 2.0)")
    parser.add_argument("--no-high-mtu", action="store_true",
                        help="Force the 20-byte chunk path instead of a negotiated MTU")
    parser.add_argument("--profile", action="store_true",
                        help="Time write awaits vs packet-receipt waits and print the breakdown. "
                             "Do this before tuning --prn: if the writes account for nearly all of "
                             "the wall clock, PRN is not the constraint.")
    parser.add_argument("--address-type", choices=("random", "public", "auto"), default="random",
                        help="Windows BLE address type (default: random, as used by MSV3)")
    args = parser.parse_args()

    if not args.package.is_file():
        parser.error(f"package not found: {args.package}")
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        return asyncio.run(run(args))
    except (PreflightError, FleetError) as error:
        # A refused run is an ordinary outcome, not a crash: it means a check did its job and the
        # target was left alone. Exit 2 so a script can tell it from a transfer that failed.
        print(f"\nREFUSED: {error}", file=sys.stderr)
        return 2
    except JumpError as error:
        print(f"\nFAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
