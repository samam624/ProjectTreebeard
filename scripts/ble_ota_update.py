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
bricking. Deterministic injection tests (dropped / delayed / early / stale / duplicate /
wrong-offset receipts, and mutated-image negative tests) are still outstanding.

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

Targeting is by ADDRESS, deliberately. Boards are re-identified in place (DEVICE_NAME /
SENSOR_ID in MSV3_Moisture_BLE_Sleep.ino), so two probes can legitimately share a name -- a
name-based scan can jump the wrong board into its bootloader and take a live sensor offline.

Usage:
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

    def __init__(self):
        self.armed = False          # true only while the firmware image is streaming
        self.desynced = False
        self.reason = None
        self.timeouts = 0
        self.last_reported = None   # byte count from the most recent packet-receipt notification

    def begin_attempt(self):
        """Fresh DFU attempt. Clears per-attempt state."""
        self.armed = False
        self.desynced = False
        self.reason = None
        self.timeouts = 0
        self.last_reported = None

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


async def wait_for_advertisement(addresses, timeout_s, label, require_dfu_service=False):
    """Scan until any of `addresses` appears.

    The application only opens a 5 s OTA window per minute (OTA_ADV_LISTEN_MS), so a short
    one-shot scan misses it far more often than it hits.

    `require_dfu_service` disambiguates the bootloader from the application when both can appear on
    the same address: after a buttonless jump the bootloader reuses the application's address, so
    the address alone no longer says which one is running. The DFU service UUID does.
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
        if require_dfu_service:
            uuids = [u.lower() for u in (advertisement.service_uuids or [])]
            name = (advertisement.local_name or device.name or "").lower()
            if DFU_SERVICE_UUID not in uuids and "dfu" not in name:
                return
        found.set_result((device, advertisement.rssi))

    scanner = BleakScanner(callback)
    await scanner.start()
    try:
        device, rssi = await asyncio.wait_for(found, timeout=timeout_s)
        print(f"  found {label} at {device.address} (rssi {rssi})", flush=True)
        return device
    except asyncio.TimeoutError:
        return None
    finally:
        await scanner.stop()


async def jump_to_bootloader(device, address_type):
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

    started = time.time()
    try:
        async with BleakClient(device, **kwargs) as client:
            print(f"  connected in {time.time() - started:.2f}s", flush=True)
            await client.start_notify(DFU_CONTROL_POINT_UUID, lambda _handle, _data: None)
            payload = bytearray([OP_CODE_ENTER_BOOTLOADER, UPLOAD_MODE_APPLICATION])
            write_started = time.time()
            await client.write_gatt_char(DFU_CONTROL_POINT_UUID, payload, response=True)
            return f"write acknowledged in {time.time() - write_started:.2f}s"
    except Exception as error:
        text = str(error).upper()
        if "NOT_PERMITTED" in text:
            return (f"REJECTED by firmware ({error}) -- ResetSafeBLEDfu's power gate is shut. "
                    "Check advertisement flag bit4 against OTA_MIN_BATTERY_MV.")
        return f"link dropped during write ({type(error).__name__}: {error}) -- normal if the board reset"


async def run(args):
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
    guard = ReceiptGuard()
    profile = TransferProfile() if args.profile else None
    install_transfer_hooks(updater, guard, profile)

    boot_addresses = ([args.bootloader_address] if args.bootloader_address
                      else bootloader_addresses_for(args.app_address))

    # Already sitting in the bootloader (a previous attempt left it there)? Go straight to
    # the transfer rather than waiting out an application window that will never come. The DFU
    # service check matters here: after a buttonless jump the bootloader shares the application's
    # address, so the address alone cannot tell them apart.
    probe_timeout = args.bootloader_timeout if args.skip_jump else 6.0
    device = await wait_for_advertisement(boot_addresses, probe_timeout, "bootloader",
                                          require_dfu_service=True)

    if device is None:
        if args.skip_jump:
            print("Bootloader never advertised.", flush=True)
            return 1

        app = await wait_for_advertisement(args.app_address, args.app_timeout, "application")
        if app is None:
            print(f"Application never advertised within {args.app_timeout:.0f}s.", flush=True)
            return 1

        print("Jumping to bootloader...", flush=True)
        print(f"  {await jump_to_bootloader(app, args.address_type)}", flush=True)

        device = await wait_for_advertisement(boot_addresses, args.bootloader_timeout, "bootloader",
                                              require_dfu_service=True)
        if device is None:
            print("\nBootloader did not appear -- the jump did not take effect.", flush=True)
            print("The board is still running its application; nothing was written.", flush=True)
            return 1

    await asyncio.sleep(args.connect_delay)
    chunking = "20-byte" if args.no_high_mtu else "negotiated-MTU"
    print(f"Transferring {package.name} ({chunking} chunks)...", flush=True)
    started = time.time()
    await updater.perform_update(device, max_retries=args.retry)
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
    parser.add_argument("--app-address", required=True,
                        help="BLE address of the RUNNING application, e.g. DB:AD:4D:9D:21:BA. "
                             "Address, not name -- boards can share a name after an in-place re-ID.")
    parser.add_argument("--bootloader-address",
                        help="Override the bootloader address (default: application address + 1)")
    parser.add_argument("--skip-jump", action="store_true",
                        help="Board is already sitting in its bootloader; do not try to jump it")
    parser.add_argument("--prn", type=int, default=16,
                        help="Packet receipt interval (default 16, the measured optimum -- see the "
                             "module docstring for the PRN 4/16/32 sweep)")
    parser.add_argument("--delay", type=float, default=0.6, help="Start/size delay seconds (default 0.6)")
    parser.add_argument("--retry", type=int, default=3, help="DFU connection attempts (default 3)")
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
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
